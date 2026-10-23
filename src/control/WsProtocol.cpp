#include "WsProtocol.h"

#include <algorithm>
#include <cstring>

namespace whynot::control::ws
{

namespace
{
    std::uint32_t rotl (std::uint32_t x, int n) noexcept   { return (x << n) | (x >> (32 - n)); }

    char lowerAscii (char c) noexcept   { return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c; }

    bool equalsIgnoreCase (std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size())
            return false;

        for (std::size_t i = 0; i < a.size(); ++i)
            if (lowerAscii (a[i]) != lowerAscii (b[i]))
                return false;

        return true;
    }

    std::string_view trim (std::string_view s) noexcept
    {
        while (! s.empty() && (s.front() == ' ' || s.front() == '\t'))  s.remove_prefix (1);
        while (! s.empty() && (s.back() == ' ' || s.back() == '\t'))    s.remove_suffix (1);
        return s;
    }

    /** "keep-alive, Upgrade" tem o token "upgrade"? (lista separada por vírgula, sem diferença de caixa) */
    bool hasToken (std::string_view list, std::string_view token) noexcept
    {
        while (! list.empty())
        {
            const auto comma = list.find (',');
            const auto item = trim (list.substr (0, comma));

            if (equalsIgnoreCase (item, token))
                return true;

            if (comma == std::string_view::npos)
                break;

            list.remove_prefix (comma + 1);
        }

        return false;
    }

    // tchar da RFC 9110: o que pode aparecer no nome de um cabeçalho e no método
    bool isTokenChar (unsigned char c) noexcept
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            return true;

        return std::strchr ("!#$%&'*+-.^_`|~", c) != nullptr && c != 0;
    }

    const char* statusText (int status) noexcept
    {
        switch (status)
        {
            case 101: return "Switching Protocols";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 426: return "Upgrade Required";
            case 429: return "Too Many Requests";
            case 431: return "Request Header Fields Too Large";
            case 503: return "Service Unavailable";
            default:  return "Error";
        }
    }

    bool isValidCloseCode (std::uint16_t code) noexcept
    {
        return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1011) || (code >= 3000 && code <= 4999);
    }

    /** Corta em no máximo `limit` bytes sem partir um caractere UTF-8. */
    std::string_view cutUtf8 (std::string_view s, std::size_t limit) noexcept
    {
        if (s.size() <= limit)
            return s;

        std::size_t end = limit;

        while (end > 0 && (((unsigned char) s[end]) & 0xC0) == 0x80)
            --end;

        return s.substr (0, end);
    }

    void appendLength (std::vector<std::uint8_t>& out, std::uint8_t maskBit, std::size_t size)
    {
        if (size < 126)
        {
            out.push_back ((std::uint8_t) (maskBit | size));
        }
        else if (size <= 0xFFFF)
        {
            out.push_back ((std::uint8_t) (maskBit | 126));
            out.push_back ((std::uint8_t) (size >> 8));
            out.push_back ((std::uint8_t) (size & 0xFF));
        }
        else
        {
            out.push_back ((std::uint8_t) (maskBit | 127));

            for (int shift = 56; shift >= 0; shift -= 8)
                out.push_back ((std::uint8_t) (((std::uint64_t) size >> shift) & 0xFF));
        }
    }

    constexpr char base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int base64Value (char c) noexcept
    {
        if (c >= 'A' && c <= 'Z')  return c - 'A';
        if (c >= 'a' && c <= 'z')  return c - 'a' + 26;
        if (c >= '0' && c <= '9')  return c - '0' + 52;
        if (c == '+')              return 62;
        if (c == '/')              return 63;
        return -1;
    }
}

//==============================================================================
std::array<std::uint8_t, 20> sha1 (std::string_view data)
{
    std::uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };

    auto block = [&h] (const std::uint8_t* p)
    {
        std::uint32_t w[80];

        for (int i = 0; i < 16; ++i)
            w[i] = ((std::uint32_t) p[4 * i] << 24) | ((std::uint32_t) p[4 * i + 1] << 16)
                 | ((std::uint32_t) p[4 * i + 2] << 8) | (std::uint32_t) p[4 * i + 3];

        for (int i = 16; i < 80; ++i)
            w[i] = rotl (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        for (int i = 0; i < 80; ++i)
        {
            std::uint32_t f, k;

            if (i < 20)       { f = (b & c) | (~b & d);            k = 0x5A827999u; }
            else if (i < 40)  { f = b ^ c ^ d;                     k = 0x6ED9EBA1u; }
            else if (i < 60)  { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDCu; }
            else              { f = b ^ c ^ d;                     k = 0xCA62C1D6u; }

            const std::uint32_t temp = rotl (a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl (b, 30);
            b = a;
            a = temp;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    };

    const auto* bytes = reinterpret_cast<const std::uint8_t*> (data.data());
    const std::size_t fullBlocks = data.size() / 64;

    for (std::size_t i = 0; i < fullBlocks; ++i)
        block (bytes + 64 * i);

    // o resto, o 0x80 e o tamanho em bits (big-endian) cabem em um ou dois blocos
    std::uint8_t tail[128] = {};
    const std::size_t rest = data.size() - 64 * fullBlocks;
    std::memcpy (tail, bytes + 64 * fullBlocks, rest);
    tail[rest] = 0x80;
    const std::size_t tailSize = rest < 56 ? 64 : 128;
    const std::uint64_t bits = (std::uint64_t) data.size() * 8u;

    for (int i = 0; i < 8; ++i)
        tail[tailSize - 1 - (std::size_t) i] = (std::uint8_t) ((bits >> (8 * i)) & 0xFF);

    block (tail);

    if (tailSize == 128)
        block (tail + 64);

    std::array<std::uint8_t, 20> digest {};

    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j)
            digest[(std::size_t) (4 * i + j)] = (std::uint8_t) ((h[i] >> (24 - 8 * j)) & 0xFF);

    return digest;
}

std::string base64Encode (const std::uint8_t* data, std::size_t size)
{
    std::string out;
    out.reserve ((size + 2) / 3 * 4);

    for (std::size_t i = 0; i < size; i += 3)
    {
        const std::uint32_t n = ((std::uint32_t) data[i] << 16)
                              | (i + 1 < size ? (std::uint32_t) data[i + 1] << 8 : 0u)
                              | (i + 2 < size ? (std::uint32_t) data[i + 2] : 0u);

        out += base64Alphabet[(n >> 18) & 63];
        out += base64Alphabet[(n >> 12) & 63];
        out += i + 1 < size ? base64Alphabet[(n >> 6) & 63] : '=';
        out += i + 2 < size ? base64Alphabet[n & 63] : '=';
    }

    return out;
}

std::optional<std::vector<std::uint8_t>> base64Decode (std::string_view text)
{
    if (text.size() % 4 != 0)
        return std::nullopt;

    std::vector<std::uint8_t> out;
    out.reserve (text.size() / 4 * 3);

    for (std::size_t i = 0; i < text.size(); i += 4)
    {
        const bool last = i + 4 == text.size();
        const int pad = (last && text[i + 3] == '=') ? (text[i + 2] == '=' ? 2 : 1) : 0;
        std::uint32_t n = 0;

        for (int j = 0; j < 4; ++j)
        {
            const char c = text[i + (std::size_t) j];

            if (j >= 4 - pad)
            {
                if (c != '=')
                    return std::nullopt;

                n <<= 6;
                continue;
            }

            const int v = base64Value (c);

            if (v < 0)
                return std::nullopt;

            n = (n << 6) | (std::uint32_t) v;
        }

        // os bits que sobram antes do '=' precisam ser zero (forma única)
        if ((pad == 1 && (n & 0xFF) != 0) || (pad == 2 && (n & 0xFFFF) != 0))
            return std::nullopt;

        out.push_back ((std::uint8_t) ((n >> 16) & 0xFF));

        if (pad < 2)  out.push_back ((std::uint8_t) ((n >> 8) & 0xFF));
        if (pad < 1)  out.push_back ((std::uint8_t) (n & 0xFF));
    }

    return out;
}

std::string acceptKey (std::string_view clientKey)
{
    std::string joined (clientKey);
    joined += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";   // o GUID fixo da RFC 6455
    const auto digest = sha1 (joined);
    return base64Encode (digest.data(), digest.size());
}

bool isValidUtf8 (std::string_view text) noexcept
{
    const auto* s = reinterpret_cast<const unsigned char*> (text.data());
    const std::size_t n = text.size();
    std::size_t i = 0;

    while (i < n)
    {
        const unsigned char c = s[i];

        if (c < 0x80)
        {
            ++i;
            continue;
        }

        int extra;
        std::uint32_t cp;

        if (c >= 0xC2 && c <= 0xDF)        { extra = 1; cp = c & 0x1Fu; }
        else if (c >= 0xE0 && c <= 0xEF)   { extra = 2; cp = c & 0x0Fu; }
        else if (c >= 0xF0 && c <= 0xF4)   { extra = 3; cp = c & 0x07u; }
        else                               return false;   // continuação solta, 0xC0/0xC1 (forma longa) ou acima de 0xF4

        if (n - i <= (std::size_t) extra)
            return false;

        for (int k = 1; k <= extra; ++k)
        {
            const unsigned char cc = s[i + (std::size_t) k];

            if ((cc & 0xC0) != 0x80)
                return false;

            cp = (cp << 6) | (cc & 0x3Fu);
        }

        if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000))   return false;   // forma longa
        if (cp >= 0xD800 && cp <= 0xDFFF)                                   return false;   // metade de par substituto
        if (cp > 0x10FFFF)                                                  return false;

        i += (std::size_t) extra + 1;
    }

    return true;
}

//==============================================================================
const std::string* HttpRequest::header (std::string_view lowerName) const noexcept
{
    for (auto& [name, value] : headers)
        if (name == lowerName)
            return &value;

    return nullptr;
}

int HttpRequest::count (std::string_view lowerName) const noexcept
{
    int n = 0;

    for (auto& h : headers)
        if (h.first == lowerName)
            ++n;

    return n;
}

ParseResult parseHttpRequest (std::string_view bytes)
{
    ParseResult r;
    const auto end = bytes.find ("\r\n\r\n");

    if (end == std::string_view::npos)
    {
        if (bytes.size() > maxHandshakeBytes)
        {
            r.status = ParseStatus::tooLarge;
            r.error = "Pedido HTTP maior que 8 KB.";
        }

        return r;   // incomplete
    }

    if (end + 4 > maxHandshakeBytes)
    {
        r.status = ParseStatus::tooLarge;
        r.error = "Pedido HTTP maior que 8 KB.";
        return r;
    }

    auto invalid = [&r] (const char* why)
    {
        r.status = ParseStatus::invalid;
        r.error = why;
        r.request = {};
        return r;
    };

    const std::string_view head = bytes.substr (0, end + 2);   // cada linha termina em CRLF
    std::size_t pos = 0;
    bool first = true;

    while (pos < head.size())
    {
        const auto eol = head.find ("\r\n", pos);
        const auto line = head.substr (pos, eol - pos);
        pos = eol + 2;

        for (const char ch : line)
        {
            const auto c = (unsigned char) ch;

            if ((c < 0x20 && c != '\t') || c == 0x7F)
                return invalid ("Caractere de controle no pedido HTTP.");
        }

        if (first)
        {
            first = false;
            const auto sp1 = line.find (' ');
            const auto sp2 = sp1 == std::string_view::npos ? sp1 : line.find (' ', sp1 + 1);

            if (sp1 == std::string_view::npos || sp2 == std::string_view::npos || line.find (' ', sp2 + 1) != std::string_view::npos)
                return invalid ("Linha do pedido HTTP malformada.");

            const auto method = line.substr (0, sp1);
            const auto target = line.substr (sp1 + 1, sp2 - sp1 - 1);
            const auto version = line.substr (sp2 + 1);

            if (method.empty() || target.empty() || version.empty())
                return invalid ("Linha do pedido HTTP malformada.");

            for (const char ch : method)
                if (! isTokenChar ((unsigned char) ch))
                    return invalid ("Método HTTP malformado.");

            r.request.method = std::string (method);
            r.request.target = std::string (target);
            r.request.version = std::string (version);
            continue;
        }

        if (line.empty())
            return invalid ("Linha vazia no meio dos cabeçalhos.");

        if (line.front() == ' ' || line.front() == '\t')
            return invalid ("Cabeçalho dobrado em várias linhas não é aceito.");

        const auto colon = line.find (':');

        if (colon == std::string_view::npos || colon == 0)
            return invalid ("Cabeçalho sem nome ou sem ':'.");

        std::string name;
        name.reserve (colon);

        for (const char ch : line.substr (0, colon))
        {
            if (! isTokenChar ((unsigned char) ch))
                return invalid ("Nome de cabeçalho malformado (espaço antes do ':'?).");

            name += lowerAscii (ch);
        }

        if ((int) r.request.headers.size() >= maxHeaders)
            return invalid ("Cabeçalhos demais no pedido HTTP.");

        r.request.headers.emplace_back (std::move (name), std::string (trim (line.substr (colon + 1))));
    }

    if (first)
        return invalid ("Pedido HTTP vazio.");

    r.status = ParseStatus::complete;
    r.consumed = end + 4;
    return r;
}

UpgradeCheck checkUpgrade (const HttpRequest& req)
{
    UpgradeCheck c;

    auto refuse = [&c] (int status, const char* why)
    {
        c.status = status;
        c.reason = why;
        return c;
    };

    if (req.method != "GET")         return refuse (405, "Só GET abre o WebSocket.");
    if (req.version != "HTTP/1.1")   return refuse (400, "Só HTTP/1.1.");
    if (req.target != "/")           return refuse (404, "O WebSocket da WHYNOT fica em \"/\".");

    if (req.count ("upgrade") != 1 || ! hasToken (*req.header ("upgrade"), "websocket"))
        return refuse (400, "Falta \"Upgrade: websocket\".");

    if (req.count ("connection") < 1 || ! hasToken (*req.header ("connection"), "upgrade"))
        return refuse (400, "Falta \"Connection: Upgrade\".");

    if (req.count ("sec-websocket-version") != 1 || *req.header ("sec-websocket-version") != "13")
        return refuse (426, "Só a versão 13 do WebSocket.");

    if (req.count ("sec-websocket-key") != 1)
        return refuse (400, "Falta uma Sec-WebSocket-Key (uma só).");

    const auto& key = *req.header ("sec-websocket-key");
    const auto decoded = base64Decode (key);

    if (! decoded || decoded->size() != 16)
        return refuse (400, "Sec-WebSocket-Key precisa ter 16 bytes em base64.");

    c.ok = true;
    c.status = 101;
    c.clientKey = key;
    return c;
}

std::string switchingProtocolsResponse (std::string_view clientKey)
{
    return "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: " + acceptKey (clientKey) + "\r\n\r\n";
}

std::string errorResponse (int status, std::string_view reason)
{
    std::string out = "HTTP/1.1 " + std::to_string (status) + " " + statusText (status) + "\r\n";
    out += "Content-Type: text/plain; charset=utf-8\r\n";
    out += "Content-Length: " + std::to_string (reason.size()) + "\r\n";
    out += "Connection: close\r\n";

    if (status == 401)  out += "WWW-Authenticate: Bearer\r\n";
    if (status == 405)  out += "Allow: GET\r\n";
    if (status == 426)  out += "Sec-WebSocket-Version: 13\r\n";

    out += "\r\n";
    out += reason;
    return out;
}

//==============================================================================
Decoder::Decoder (std::size_t maxMessageBytes, bool fromClient) noexcept
    : maxMessage (maxMessageBytes), expectMasked (fromClient) {}

void Decoder::feed (const void* data, std::size_t size)
{
    if (failed() || size == 0)
        return;

    // descarta o que já foi lido antes de crescer
    if (readPos == buffer.size())
    {
        buffer.clear();
        readPos = 0;
    }
    else if (readPos > 64 * 1024)
    {
        buffer.erase (buffer.begin(), buffer.begin() + (std::ptrdiff_t) readPos);
        readPos = 0;
    }

    const auto* p = static_cast<const std::uint8_t*> (data);
    buffer.insert (buffer.end(), p, p + size);
}

Decoder::Status Decoder::fail (std::uint16_t code, std::string why)
{
    failure = code;
    reason = std::move (why);
    buffer.clear();
    readPos = 0;
    partial.clear();
    return Status::error;
}

Decoder::Status Decoder::next (Message& out)
{
    for (;;)
    {
        if (failed())
            return Status::error;

        const std::size_t avail = buffer.size() - readPos;

        if (avail < 2)
            return Status::needMore;

        const std::uint8_t* p = buffer.data() + readPos;
        const bool fin = (p[0] & 0x80) != 0;
        const auto op = (std::uint8_t) (p[0] & 0x0F);
        const bool masked = (p[1] & 0x80) != 0;
        const std::uint8_t len7 = p[1] & 0x7F;

        if ((p[0] & 0x70) != 0)
            return fail (closeCode::protocolError, "Bits reservados ligados: a WHYNOT não aceita extensões.");

        const bool control = (op & 0x08) != 0;

        switch (op)
        {
            case (std::uint8_t) Opcode::continuation:
            case (std::uint8_t) Opcode::text:
            case (std::uint8_t) Opcode::close:
            case (std::uint8_t) Opcode::ping:
            case (std::uint8_t) Opcode::pong:
                break;

            case (std::uint8_t) Opcode::binary:
                return fail (closeCode::unsupportedData, "A ponte só aceita texto.");

            default:
                return fail (closeCode::protocolError, "Tipo de quadro desconhecido.");
        }

        if (masked != expectMasked)
            return fail (closeCode::protocolError, expectMasked ? "Quadro do cliente sem máscara." : "Quadro do servidor com máscara.");

        if (control && (! fin || len7 > 125))
            return fail (closeCode::protocolError, "Quadro de controle partido ou maior que 125 bytes.");

        std::size_t header = 2;
        std::uint64_t length = len7;

        if (len7 == 126)
        {
            if (avail < 4)
                return Status::needMore;

            length = ((std::uint64_t) p[2] << 8) | p[3];
            header = 4;

            if (length < 126)
                return fail (closeCode::protocolError, "Tamanho do quadro sem a forma mínima.");
        }
        else if (len7 == 127)
        {
            if (avail < 10)
                return Status::needMore;

            length = 0;

            for (int i = 0; i < 8; ++i)
                length = (length << 8) | p[2 + i];

            header = 10;

            if ((length >> 63) != 0 || length <= 0xFFFF)
                return fail (closeCode::protocolError, "Tamanho do quadro inválido ou sem a forma mínima.");
        }

        if (! control)
        {
            const std::uint64_t soFar = fragmented ? partial.size() : 0;

            if (length > maxMessage || soFar + length > maxMessage)
                return fail (closeCode::tooBig, "Mensagem maior que o limite de " + std::to_string (maxMessage) + " bytes.");

            if (op == (std::uint8_t) Opcode::continuation && ! fragmented)
                return fail (closeCode::protocolError, "Continuação sem mensagem começada.");

            if (op == (std::uint8_t) Opcode::text && fragmented)
                return fail (closeCode::protocolError, "Mensagem nova antes de terminar a anterior.");
        }

        if (masked)
            header += 4;

        if (avail < header || avail - header < length)
            return Status::needMore;

        const std::uint8_t* src = p + header;
        std::string payload ((std::size_t) length, '\0');

        if (masked)
        {
            const std::uint8_t* mask = p + header - 4;

            for (std::size_t i = 0; i < payload.size(); ++i)
                payload[i] = (char) (src[i] ^ mask[i & 3]);
        }
        else if (! payload.empty())
        {
            std::memcpy (payload.data(), src, payload.size());
        }

        readPos += header + (std::size_t) length;

        switch (op)
        {
            case (std::uint8_t) Opcode::ping:
                out = { Message::Kind::ping, std::move (payload), 0 };
                return Status::message;

            case (std::uint8_t) Opcode::pong:
                out = { Message::Kind::pong, std::move (payload), 0 };
                return Status::message;

            case (std::uint8_t) Opcode::close:
            {
                if (payload.empty())
                {
                    out = { Message::Kind::close, {}, closeCode::noStatus };
                    return Status::message;
                }

                if (payload.size() < 2)
                    return fail (closeCode::protocolError, "Close com 1 byte.");

                const auto code = (std::uint16_t) (((unsigned char) payload[0] << 8) | (unsigned char) payload[1]);

                if (! isValidCloseCode (code))
                    return fail (closeCode::protocolError, "Código de close inválido.");

                auto why = payload.substr (2);

                if (! isValidUtf8 (why))
                    return fail (closeCode::invalidData, "Motivo do close não é UTF-8.");

                out = { Message::Kind::close, std::move (why), code };
                return Status::message;
            }

            case (std::uint8_t) Opcode::text:
                if (! fin)
                {
                    fragmented = true;
                    partial = std::move (payload);
                    continue;
                }
                break;

            default:   // continuação
                partial += payload;

                if (! fin)
                    continue;

                fragmented = false;
                payload = std::move (partial);
                partial.clear();
                break;
        }

        if (! isValidUtf8 (payload))
            return fail (closeCode::invalidData, "Texto que não é UTF-8.");

        out = { Message::Kind::text, std::move (payload), 0 };
        return Status::message;
    }
}

//==============================================================================
std::vector<std::uint8_t> encodeFrame (Opcode op, std::string_view payload)
{
    std::vector<std::uint8_t> out;
    out.reserve (payload.size() + 10);
    out.push_back ((std::uint8_t) (0x80 | (std::uint8_t) op));
    appendLength (out, 0, payload.size());
    out.insert (out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::uint8_t> encodeClose (std::uint16_t code, std::string_view reason)
{
    std::string payload;
    payload += (char) (code >> 8);
    payload += (char) (code & 0xFF);
    payload += cutUtf8 (reason, 123);
    return encodeFrame (Opcode::close, payload);
}

std::vector<std::uint8_t> encodeClientFrame (Opcode op, std::string_view payload, std::array<std::uint8_t, 4> mask, bool fin)
{
    std::vector<std::uint8_t> out;
    out.reserve (payload.size() + 14);
    out.push_back ((std::uint8_t) ((fin ? 0x80 : 0x00) | (std::uint8_t) op));
    appendLength (out, 0x80, payload.size());
    out.insert (out.end(), mask.begin(), mask.end());

    for (std::size_t i = 0; i < payload.size(); ++i)
        out.push_back ((std::uint8_t) ((std::uint8_t) payload[i] ^ mask[i & 3]));

    return out;
}

} // namespace whynot::control::ws
