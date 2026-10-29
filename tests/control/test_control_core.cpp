// Testes da ponte de controle (Fase 4), sem JUCE: SHA-1 e base64 com os vetores das RFCs, o aperto de mão
// do WebSocket, os quadros, Host/Origin/token, os caminhos, os limites, a tabela de risco dos comandos,
// o aleatório do sistema e um fuzz do que vem da rede.

#include "../../src/control/CommandPolicy.h"
#include "../../src/control/ControlPlatform.h"
#include "../../src/control/ControlPolicy.h"
#include "../../src/control/WsProtocol.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace whynot::control;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static std::string hex (const std::array<std::uint8_t, 20>& d)
{
    static const char* digits = "0123456789abcdef";
    std::string s;

    for (auto b : d)
    {
        s += digits[b >> 4];
        s += digits[b & 15];
    }

    return s;
}

static std::string bytesToString (const std::vector<std::uint8_t>& v)   { return std::string (v.begin(), v.end()); }

static constexpr std::array<std::uint8_t, 4> mask { 0x37, 0xFA, 0x21, 0x3D };

//==============================================================================
static void testSha1AndBase64()
{
    std::printf ("SHA-1 e base64\n");

    CHECK (hex (ws::sha1 ("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1 de \"\" (RFC 3174)");
    CHECK (hex (ws::sha1 ("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 de \"abc\" (RFC 3174)");
    CHECK (hex (ws::sha1 ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
           "SHA-1 de 56 bytes, o caso que pede dois blocos de fim (RFC 3174)");
    CHECK (hex (ws::sha1 (std::string (1000000, 'a'))) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f", "SHA-1 de um milhão de \"a\" (RFC 3174)");
    CHECK (hex (ws::sha1 ("The quick brown fox jumps over the lazy dog")) == "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12",
           "SHA-1 da frase da raposa");

    const std::pair<const char*, const char*> vectors[] = {
        { "", "" }, { "f", "Zg==" }, { "fo", "Zm8=" }, { "foo", "Zm9v" },
        { "foob", "Zm9vYg==" }, { "fooba", "Zm9vYmE=" }, { "foobar", "Zm9vYmFy" } };

    bool encodeOk = true, decodeOk = true;

    for (auto& [plain, coded] : vectors)
    {
        const std::string p (plain);
        encodeOk = encodeOk && ws::base64Encode (reinterpret_cast<const std::uint8_t*> (p.data()), p.size()) == coded;
        const auto back = ws::base64Decode (coded);
        decodeOk = decodeOk && back && bytesToString (*back) == p;
    }

    CHECK (encodeOk, "base64: os 7 vetores da RFC 4648 codificam certo");
    CHECK (decodeOk, "base64: e voltam ao original");

    bool rejects = true;

    for (const char* bad : { "Zg=", "Zm9v====", "Z===", "====", "Zm9v YmFy", "Zm=v", "Zh==", "Zm9=", "Zm9vYg=a", "Zm9v\nYmFy" })
        rejects = rejects && ! ws::base64Decode (bad);

    CHECK (rejects, "base64: tamanho errado, '=' fora do fim, espaço e bits sobrando são recusados");

    CHECK (ws::acceptKey ("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "a chave de aceite do exemplo da RFC 6455");

    std::array<std::uint8_t, 32> bytes {};

    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = (std::uint8_t) (i * 37 + 251);

    const auto token = encodeToken (bytes);
    bool urlSafe = token.size() == 43;

    for (char c : token)
        urlSafe = urlSafe && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_');

    CHECK (urlSafe, "token de 32 bytes vira 43 caracteres de base64url, sem '=': %s", token.c_str());
}

static void testUtf8()
{
    std::printf ("UTF-8\n");

    CHECK (ws::isValidUtf8 ("ação, 808 e 😀"), "texto com acento e emoji (4 bytes) é válido");
    CHECK (ws::isValidUtf8 (""), "texto vazio é válido");

    bool rejects = true;

    for (const char* bad : { "\xC0\xAF", "\xC1\xBF", "\xE0\x80\xAF", "\xF0\x80\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80",
                             "\xE2\x82", "\x80", "\xFF", "a\xC3", "\xF5\x80\x80\x80" })
        rejects = rejects && ! ws::isValidUtf8 (bad);

    CHECK (rejects, "formas longas, metade de par substituto, acima de U+10FFFF, cortado e continuação solta são recusados");
}

//==============================================================================
// O pedido que o WebSocket do Node 24 manda de verdade (capturado em 20/10, para o plano).
static std::string nodeRequest (int port, const std::string& token, const std::string& extra = {})
{
    return "GET / HTTP/1.1\r\n"
           "host: 127.0.0.1:" + std::to_string (port) + "\r\n"
           "connection: upgrade\r\n"
           "upgrade: websocket\r\n"
           "Authorization: Bearer " + token + "\r\n"
           "sec-websocket-key: 1P9bzGwEBk65fb+aJWOy6Q==\r\n"
           "sec-websocket-version: 13\r\n"
           "sec-websocket-extensions: permessage-deflate; client_max_window_bits\r\n"
           "accept: */*\r\n"
           "sec-fetch-mode: websocket\r\n"
           "user-agent: node\r\n" + extra + "\r\n";
}

static ws::HttpRequest parsed (const std::string& text)
{
    return ws::parseHttpRequest (text).request;
}

static void testHttpParsing()
{
    std::printf ("Pedido HTTP\n");

    const auto good = nodeRequest (47830, "TESTE");
    const auto r = ws::parseHttpRequest (good + "\x81\x80" "abcd");   // um quadro já colado no fim

    CHECK (r.status == ws::ParseStatus::complete && r.request.method == "GET" && r.request.target == "/" && r.request.version == "HTTP/1.1",
           "o pedido do Node é lido inteiro");
    CHECK (r.consumed == good.size(), "consumed para na linha em branco, antes do quadro colado (%zu bytes)", r.consumed);
    CHECK (r.request.header ("authorization") != nullptr && *r.request.header ("authorization") == "Bearer TESTE",
           "nomes de cabeçalho viram minúsculas e o valor fica igual");
    CHECK (r.request.count ("sec-websocket-key") == 1 && r.request.header ("x-nada") == nullptr, "count e header de quem não existe");

    CHECK (ws::parseHttpRequest (good.substr (0, good.size() - 2)).status == ws::ParseStatus::incomplete, "sem a linha em branco: incomplete");

    const std::string big = "GET / HTTP/1.1\r\nx-a: " + std::string (9000, 'a');
    CHECK (ws::parseHttpRequest (big).status == ws::ParseStatus::tooLarge, "mais de 8 KB sem terminar: tooLarge");
    CHECK (ws::parseHttpRequest (big + "\r\n\r\n").status == ws::ParseStatus::tooLarge, "terminado depois dos 8 KB: tooLarge");

    std::string manyHeaders = "GET / HTTP/1.1\r\n";

    for (int i = 0; i < 65; ++i)
        manyHeaders += "x-" + std::to_string (i) + ": 1\r\n";

    const char* invalid[] = {
        "GET / HTTP/1.1\nhost: a\r\n\r\n",                   // LF sozinho
        "GET / HTTP/1.1\r\nhost a\r\n\r\n",                  // sem ':'
        "GET / HTTP/1.1\r\nhost : a\r\n\r\n",                // espaço antes do ':'
        "GET / HTTP/1.1\r\nhost: a\r\n b\r\n\r\n",           // cabeçalho dobrado
        "GET / HTTP/1.1\r\n: a\r\n\r\n",                     // nome vazio
        "GET /HTTP/1.1\r\n\r\n",                             // linha do pedido com 2 pedaços
        "GET  / HTTP/1.1\r\n\r\n",                           // dois espaços
        "G(T / HTTP/1.1\r\n\r\n",                            // método com caractere de fora
        "\r\n\r\n",                                          // vazio
    };

    bool allInvalid = true;

    for (const char* bad : invalid)
        allInvalid = allInvalid && ws::parseHttpRequest (bad).status == ws::ParseStatus::invalid;

    const std::string withNul = std::string ("GET / HTTP/1.1\r\nhost: a") + '\0' + "b\r\n\r\n";
    allInvalid = allInvalid && ws::parseHttpRequest (withNul).status == ws::ParseStatus::invalid;
    allInvalid = allInvalid && ws::parseHttpRequest (manyHeaders + "\r\n").status == ws::ParseStatus::invalid;

    CHECK (allInvalid, "LF sozinho, sem ':', espaço antes do ':', dobra, nome vazio, linha torta, NUL e 65 cabeçalhos: invalid");
}

static void testUpgradeAndHandshake()
{
    std::printf ("Aperto de mão\n");

    const int port = 47830;
    const std::string token = "abcDEF123-_xyz";

    const auto ok = decideHandshake (parsed (nodeRequest (port, token)), port, token);
    CHECK (ok.status == 101 && ok.clientKey == "1P9bzGwEBk65fb+aJWOy6Q==", "o pedido do Node com o token certo é aceito (101)");

    auto withLine = [&] (const std::string& from, const std::string& to)
    {
        auto text = nodeRequest (port, token);
        const auto at = text.find (from);
        text.replace (at, from.size(), to);
        return decideHandshake (parsed (text), port, token);
    };

    CHECK (withLine ("GET ", "POST ").status == 405, "POST: 405");
    CHECK (withLine ("HTTP/1.1\r\nhost", "HTTP/1.0\r\nhost").status == 400, "HTTP/1.0: 400");
    CHECK (withLine ("GET / ", "GET /x ").status == 404, "outro caminho que não \"/\": 404");
    CHECK (withLine ("upgrade: websocket", "upgrade: h2c").status == 400, "Upgrade que não é websocket: 400");
    CHECK (withLine ("connection: upgrade", "connection: keep-alive").status == 400, "Connection sem upgrade: 400");
    CHECK (withLine ("connection: upgrade", "connection: keep-alive, Upgrade").status == 101, "Connection com lista \"keep-alive, Upgrade\": aceito");
    CHECK (withLine ("sec-websocket-version: 13", "sec-websocket-version: 8").status == 426, "versão 8 do WebSocket: 426");
    CHECK (withLine ("1P9bzGwEBk65fb+aJWOy6Q==", "dG9vc2hvcnQ=").status == 400, "chave com menos de 16 bytes: 400");
    CHECK (withLine ("sec-websocket-version", "sec-websocket-key: 1P9bzGwEBk65fb+aJWOy6Q==\r\nsec-websocket-version").status == 400,
           "duas chaves: 400");

    CHECK (withLine ("127.0.0.1:47830", "evil.example:47830").status == 403, "Host de outro nome (DNS rebinding): 403");
    CHECK (withLine ("127.0.0.1:47830", "127.0.0.1:47831").status == 403, "Host com outra porta: 403");
    CHECK (withLine ("127.0.0.1:47830", "127.0.0.1").status == 403, "Host sem porta: 403");
    CHECK (withLine ("127.0.0.1:47830", "127.0.0.1:047830").status == 403, "Host com zero à esquerda na porta: 403");
    CHECK (withLine ("127.0.0.1:47830", "LocalHost:47830").status == 101, "localhost em qualquer caixa: aceito");
    CHECK (withLine ("127.0.0.1:47830", "127.0.0.1:47830\r\nhost: 127.0.0.1:47830").status == 400, "dois Host: 400");
    CHECK (withLine ("host: 127.0.0.1:47830\r\n", "").status == 400, "sem Host: 400");

    bool origins = true;

    for (const char* origin : { "https://site.example", "null", "http://127.0.0.1:47830", "http://localhost:47830" })
        origins = origins && decideHandshake (parsed (nodeRequest (port, token, std::string ("Origin: ") + origin + "\r\n")), port, token).status == 403;

    CHECK (origins, "qualquer Origin, até o do próprio 127.0.0.1 e o \"null\": 403");

    const auto noAuth = withLine ("Authorization: Bearer " + token + "\r\n", "");
    CHECK (noAuth.status == 401 && noAuth.authFailure, "sem Authorization: 401, conta como erro de token");

    const auto wrong = withLine ("Bearer " + token, "Bearer " + token + "x");
    CHECK (wrong.status == 401 && wrong.authFailure, "token com um caractere a mais: 401");
    CHECK (withLine ("Bearer " + token, "Bearer abcDEF").status == 401, "só o começo do token: 401");
    CHECK (withLine ("Bearer " + token, "Basic " + token).status == 401, "esquema Basic: 401");
    CHECK (withLine ("Bearer " + token, "bearer " + token).status == 101, "\"bearer\" em minúsculas: aceito");
    CHECK (withLine ("Bearer " + token, "Bearer " + token + "\r\nAuthorization: Bearer " + token).status == 401, "dois Authorization: 401");
    CHECK (withLine ("Bearer " + token, "Bearer abc+DEF").status == 401, "token com caractere fora do base64url: 401");
    CHECK (decideHandshake (parsed (nodeRequest (port, token)), port, "").status == 503, "ponte sem token configurado: 503");

    CHECK (constantTimeEquals ("abc", "abc") && ! constantTimeEquals ("abd", "abc") && ! constantTimeEquals ("ab", "abc")
           && ! constantTimeEquals ("abcd", "abc") && constantTimeEquals ("", ""),
           "comparação em tempo constante: igual, diferente, mais curto, mais longo, vazio");

    const auto accept = ws::switchingProtocolsResponse ("dGhlIHNhbXBsZSBub25jZQ==");
    CHECK (accept.find ("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != std::string::npos
           && accept.find ("Extensions") == std::string::npos && accept.find ("Protocol:") == std::string::npos
           && accept.size() > 4 && accept.compare (accept.size() - 4, 4, "\r\n\r\n") == 0,
           "a resposta 101 tem a chave de aceite e não aceita extensão nem subprotocolo");

    const auto refuse = ws::errorResponse (426, "Só a versão 13 do WebSocket.");
    const auto body = std::string ("Só a versão 13 do WebSocket.");
    CHECK (refuse.rfind ("HTTP/1.1 426 Upgrade Required\r\n", 0) == 0 && refuse.find ("Sec-WebSocket-Version: 13\r\n") != std::string::npos
           && refuse.find ("Content-Length: " + std::to_string (body.size()) + "\r\n") != std::string::npos
           && refuse.find ("Connection: close") != std::string::npos && refuse.size() >= body.size()
           && refuse.compare (refuse.size() - body.size(), body.size(), body) == 0,
           "a recusa 426 diz a versão, o tamanho certo do corpo e fecha a conexão");
    CHECK (ws::errorResponse (401, "x").find ("WWW-Authenticate: Bearer\r\n") != std::string::npos, "a recusa 401 pede Bearer");
}

//==============================================================================
static std::vector<ws::Message> decodeAll (ws::Decoder& d)
{
    std::vector<ws::Message> out;
    ws::Message m;

    while (d.next (m) == ws::Decoder::Status::message)
        out.push_back (m);

    return out;
}

static void feed (ws::Decoder& d, const std::vector<std::uint8_t>& bytes)   { d.feed (bytes.data(), bytes.size()); }

static std::uint16_t failureOf (const std::vector<std::uint8_t>& bytes, std::size_t max = 1024 * 1024)
{
    ws::Decoder d (max);
    feed (d, bytes);
    decodeAll (d);
    return d.failureCode();
}

static void testFrames()
{
    std::printf ("Quadros\n");

    bool sizesOk = true;

    for (std::size_t size : { 0, 5, 125, 126, 65535, 65536, 70000 })
    {
        std::string text (size, 'x');

        for (std::size_t i = 0; i < size; ++i)
            text[i] = (char) ('a' + (i % 26));

        ws::Decoder d (1024 * 1024);
        feed (d, ws::encodeClientFrame (ws::Opcode::text, text, mask));
        const auto got = decodeAll (d);
        sizesOk = sizesOk && got.size() == 1 && got[0].kind == ws::Message::Kind::text && got[0].payload == text && d.buffered() == 0;
    }

    CHECK (sizesOk, "texto de 0, 5, 125, 126, 65535, 65536 e 70000 bytes volta igual (tamanho de 7, 16 e 64 bits)");

    {
        const auto frame = ws::encodeClientFrame (ws::Opcode::text, "olá, WHYNOT", mask);
        ws::Decoder d (1024);
        std::vector<ws::Message> got;

        for (auto b : frame)
        {
            d.feed (&b, 1);
            auto more = decodeAll (d);
            got.insert (got.end(), more.begin(), more.end());
        }

        CHECK (got.size() == 1 && got[0].payload == "olá, WHYNOT", "chegando um byte por vez, sai uma mensagem só");
    }

    {
        auto two = ws::encodeClientFrame (ws::Opcode::text, "um", mask);
        const auto second = ws::encodeClientFrame (ws::Opcode::text, "dois", mask);
        two.insert (two.end(), second.begin(), second.end());
        ws::Decoder d (1024);
        feed (d, two);
        const auto got = decodeAll (d);
        CHECK (got.size() == 2 && got[0].payload == "um" && got[1].payload == "dois", "dois quadros juntos no mesmo pedaço viram duas mensagens");
    }

    {
        ws::Decoder d (1024);
        feed (d, ws::encodeClientFrame (ws::Opcode::text, "Ol", mask, false));
        feed (d, ws::encodeClientFrame (ws::Opcode::ping, "p", mask));
        feed (d, ws::encodeClientFrame (ws::Opcode::continuation, "á ", mask, false));
        feed (d, ws::encodeClientFrame (ws::Opcode::continuation, "IA", mask, true));
        const auto got = decodeAll (d);
        CHECK (got.size() == 2 && got[0].kind == ws::Message::Kind::ping && got[0].payload == "p"
               && got[1].kind == ws::Message::Kind::text && got[1].payload == "Olá IA",
               "mensagem em 3 pedaços, com um ping no meio e um acento partido entre dois quadros");
    }

    {
        ws::Decoder d (1024);
        feed (d, ws::encodeClientFrame (ws::Opcode::pong, "", mask));
        feed (d, ws::encodeClientFrame (ws::Opcode::close, std::string ("\x03\xE8") + "tchau", mask));
        const auto got = decodeAll (d);
        CHECK (got.size() == 2 && got[0].kind == ws::Message::Kind::pong && got[1].kind == ws::Message::Kind::close
               && got[1].code == 1000 && got[1].payload == "tchau", "pong vazio e close 1000 com motivo");
    }

    {
        ws::Decoder d (1024);
        feed (d, ws::encodeClientFrame (ws::Opcode::close, "", mask));
        const auto got = decodeAll (d);
        CHECK (got.size() == 1 && got[0].code == ws::closeCode::noStatus, "close vazio vira 1005 (sem código)");
    }

    CHECK (failureOf (ws::encodeFrame (ws::Opcode::text, "sem máscara")) == ws::closeCode::protocolError, "quadro sem máscara: 1002");

    {
        auto deflate = ws::encodeClientFrame (ws::Opcode::text, "x", mask);
        deflate[0] |= 0x40;   // RSV1, o bit do permessage-deflate
        CHECK (failureOf (deflate) == ws::closeCode::protocolError, "RSV1 ligado (compressão que não foi aceita): 1002");
    }

    CHECK (failureOf (ws::encodeClientFrame (ws::Opcode::binary, "bin", mask)) == ws::closeCode::unsupportedData, "quadro binário: 1003");

    {
        auto unknown = ws::encodeClientFrame (ws::Opcode::text, "x", mask);
        unknown[0] = (std::uint8_t) ((unknown[0] & 0xF0) | 0x3);
        CHECK (failureOf (unknown) == ws::closeCode::protocolError, "tipo de quadro 3 (reservado): 1002");
    }

    CHECK (failureOf (ws::encodeClientFrame (ws::Opcode::ping, std::string (126, 'p'), mask)) == ws::closeCode::protocolError,
           "ping de 126 bytes: 1002");
    CHECK (failureOf (ws::encodeClientFrame (ws::Opcode::ping, "p", mask, false)) == ws::closeCode::protocolError, "ping partido: 1002");
    CHECK (failureOf (ws::encodeClientFrame (ws::Opcode::continuation, "x", mask)) == ws::closeCode::protocolError,
           "continuação sem começo: 1002");

    {
        auto twoStarts = ws::encodeClientFrame (ws::Opcode::text, "a", mask, false);
        const auto other = ws::encodeClientFrame (ws::Opcode::text, "b", mask);
        twoStarts.insert (twoStarts.end(), other.begin(), other.end());
        CHECK (failureOf (twoStarts) == ws::closeCode::protocolError, "mensagem nova antes de terminar a anterior: 1002");
    }

    {
        const std::vector<std::uint8_t> nonMinimal16 { 0x81, 0x80 | 126, 0x00, 100, 1, 2, 3, 4 };
        std::vector<std::uint8_t> nonMinimal64 { 0x81, 0x80 | 127, 0, 0, 0, 0, 0, 0, 0x03, 0xE8, 1, 2, 3, 4 };
        std::vector<std::uint8_t> highBit { 0x81, 0x80 | 127, 0x80, 0, 0, 0, 0, 0x01, 0, 0, 1, 2, 3, 4 };
        CHECK (failureOf (nonMinimal16) == ws::closeCode::protocolError, "tamanho 100 escrito em 16 bits: 1002");
        CHECK (failureOf (nonMinimal64) == ws::closeCode::protocolError, "tamanho 1000 escrito em 64 bits: 1002");
        CHECK (failureOf (highBit) == ws::closeCode::protocolError, "tamanho de 64 bits com o bit de cima ligado: 1002");
    }

    {
        // só o cabeçalho de um quadro de 2 MB: o limite de 1 MB falha na hora, sem esperar o conteúdo
        const std::vector<std::uint8_t> huge { 0x81, 0x80 | 127, 0, 0, 0, 0, 0, 0x20, 0, 0 };
        ws::Decoder d (1024 * 1024);
        feed (d, huge);
        ws::Message m;
        CHECK (d.next (m) == ws::Decoder::Status::error && d.failureCode() == ws::closeCode::tooBig && d.buffered() == 0,
               "quadro de 2 MB com limite de 1 MB: 1009 já no cabeçalho");
    }

    {
        ws::Decoder d (100);
        feed (d, ws::encodeClientFrame (ws::Opcode::text, std::string (60, 'a'), mask, false));
        feed (d, ws::encodeClientFrame (ws::Opcode::continuation, std::string (60, 'b'), mask));
        decodeAll (d);
        CHECK (d.failureCode() == ws::closeCode::tooBig, "pedaços que somam mais que o limite: 1009");
    }

    CHECK (failureOf (ws::encodeClientFrame (ws::Opcode::text, "\xC3(", mask)) == ws::closeCode::invalidData, "texto que não é UTF-8: 1007");
    CHECK (failureOf (ws::encodeClientFrame (ws::Opcode::close, "\x03", mask)) == ws::closeCode::protocolError, "close de 1 byte: 1002");

    bool badCodes = true;

    for (int code : { 999, 1004, 1005, 1006, 1015, 2999, 5000 })
    {
        const std::string payload { (char) (code >> 8), (char) (code & 0xFF) };
        badCodes = badCodes && failureOf (ws::encodeClientFrame (ws::Opcode::close, payload, mask)) == ws::closeCode::protocolError;
    }

    CHECK (badCodes, "close com código reservado (999, 1004, 1005, 1006, 1015, 2999, 5000): 1002");

    {
        ws::Decoder d (1024);
        feed (d, ws::encodeClientFrame (ws::Opcode::close, std::string ("\x0B\xB8"), mask));   // 3000
        const auto got = decodeAll (d);
        CHECK (got.size() == 1 && got[0].code == 3000, "close 3000 (faixa de bibliotecas) é aceito");
    }

    CHECK (failureOf (ws::encodeClientFrame (ws::Opcode::close, std::string ("\x03\xE8\xFF"), mask)) == ws::closeCode::invalidData,
           "motivo do close que não é UTF-8: 1007");

    {
        ws::Decoder d (1024);
        feed (d, ws::encodeFrame (ws::Opcode::text, "x"));
        decodeAll (d);
        feed (d, ws::encodeClientFrame (ws::Opcode::text, "depois", mask));
        ws::Message m;
        CHECK (d.next (m) == ws::Decoder::Status::error && d.next (m) == ws::Decoder::Status::error && d.buffered() == 0,
               "depois de um erro, o decodificador fica em erro e não guarda mais nada");
    }

    // codificação do servidor
    auto headerSize = [] (std::size_t n)
    {
        const auto f = ws::encodeFrame (ws::Opcode::text, std::string (n, 'z'));
        return f.size() - n;
    };

    CHECK (headerSize (0) == 2 && headerSize (125) == 2 && headerSize (126) == 4 && headerSize (65535) == 4 && headerSize (65536) == 10,
           "cabeçalho do servidor com 2, 4 e 10 bytes nos limites de 125/126 e 65535/65536");

    const auto f = ws::encodeFrame (ws::Opcode::text, "oi");
    CHECK (f.size() == 4 && f[0] == 0x81 && f[1] == 2 && f[2] == 'o' && f[3] == 'i', "quadro do servidor: FIN, texto, sem máscara");

    const auto close = ws::encodeClose (ws::closeCode::tooBig, "Mensagem grande");
    CHECK (close[0] == 0x88 && close[2] == 0x03 && close[3] == 0xF1 && std::string (close.begin() + 4, close.end()) == "Mensagem grande",
           "close 1009 com o motivo");

    std::string longReason;

    for (int i = 0; i < 100; ++i)
        longReason += "ç";   // 2 bytes cada: o corte em 123 cairia no meio de um

    const auto cut = ws::encodeClose (1000, longReason);
    const std::string cutReason (cut.begin() + 4, cut.end());
    CHECK (cut[1] <= 125 && cutReason.size() == 122 && ws::isValidUtf8 (cutReason), "motivo longo cortado em 122 bytes, sem partir o ç");
}

//==============================================================================
static void testPaths()
{
    std::printf ("Caminhos\n");

    auto norm = [] (const std::string& s) { return normalisePath (s); };
    auto okAs = [&] (const std::string& in, const std::string& want) { const auto n = norm (in); return n.ok && n.path == want; };

    CHECK (okAs ("C:\\Users\\a\\Music\\WHYNOT LAB\\beat.wav", "C:\\Users\\a\\Music\\WHYNOT LAB\\beat.wav"), "caminho comum fica igual");
    CHECK (okAs ("c:/users/a/x/../y/./z.wav", "C:\\users\\a\\y\\z.wav"), "/ vira \\, a unidade em maiúscula, . e .. resolvidos");
    CHECK (okAs ("C:\\", "C:\\") && okAs ("C:\\a\\", "C:\\a") && okAs ("C:\\a\\\\b", "C:\\a\\b"), "raiz, barra no fim e barra dupla no meio");
    CHECK (okAs ("C:\\Música\\WHYNOT LAB\\808 ação.wav", "C:\\Música\\WHYNOT LAB\\808 ação.wav"), "acentos passam");
    CHECK (okAs ("C:\\a\\CONSOLE.wav", "C:\\a\\CONSOLE.wav") && okAs ("C:\\a\\COM10", "C:\\a\\COM10"), "CONSOLE e COM10 não são dispositivo");

    const char* invalid[] = {
        "", "C:", "C:foo", "foo\\bar", "\\foo", "\\\\server\\share\\x", "\\\\?\\C:\\x", "\\\\.\\PhysicalDrive0", "//server/x", "/\\server\\x",
        "C:\\a\\..\\..\\x", "C:\\a\\beat.wav:oculto", "C:\\a\\b?.wav", "C:\\a\\b*.wav", "C:\\a\\<b>", "C:\\a\\b|c", "C:\\a\\\"b\"",
        "C:\\a\\CON", "C:\\a\\nul.txt", "C:\\a\\COM1.wav", "C:\\a\\lpt9", "C:\\a\\conout$", "C:\\a\\NUL .txt", "C:\\a\\COM\xC2\xB9",
        "C:\\a\\pasta.\\x", "C:\\a\\pasta \\x", "C:\\a\\...", "C:\\a\tb", "C:\\a\x7F", "C:\\\xC3(", "1:\\a" };

    bool allInvalid = true;

    for (const char* bad : invalid)
    {
        const auto n = norm (bad);

        if (n.ok)
            std::printf ("    aceitou sem querer: %s -> %s\n", bad, n.path.c_str());

        allInvalid = allInvalid && ! n.ok && ! n.reason.empty();
    }

    CHECK (allInvalid, "recusados, com motivo: vazio, sem unidade, relativo, UNC, \\\\?\\, \\\\.\\, acima da raiz, fluxo alternativo, "
                       "< > \" | ? *, CON/NUL/COM1/LPT9/CONOUT$/COM¹, ponto ou espaço no fim, controle, UTF-8 inválido");

    std::string longPath = "C:\\";

    while (longPath.size() < 1024)
        longPath += "a";

    CHECK (norm (longPath).ok && ! norm (longPath + "b").ok, "1024 caracteres passam, 1025 não");

    std::string accented = "C:\\";

    for (int i = 0; i < 1021; ++i)
        accented += "é";   // 1024 caracteres, 2045 bytes

    CHECK (norm (accented).ok, "o limite conta caracteres, não bytes");

    CHECK (isWithin ("C:\\Music\\WHYNOT LAB\\x.wav", "C:\\Music\\WHYNOT LAB"), "arquivo dentro da pasta");
    CHECK (isWithin ("C:\\Music\\WHYNOT LAB", "C:\\Music\\WHYNOT LAB"), "a própria pasta");
    CHECK (! isWithin ("C:\\Music\\WHYNOT LABx\\x.wav", "C:\\Music\\WHYNOT LAB"), "\"WHYNOT LABx\" não está em \"WHYNOT LAB\"");
    CHECK (isWithin ("c:\\music\\whynot lab\\x.wav", "C:\\Music\\WHYNOT LAB"), "letras ASCII em outra caixa contam como iguais");
    CHECK (! isWithin ("C:\\MÚSICA\\x.wav", "C:\\Música"), "acento em outra caixa conta como diferente (cai fora, o lado seguro)");
    CHECK (! isWithin ("C:\\Music", "C:\\Music\\WHYNOT LAB") && ! isWithin ("D:\\Music\\WHYNOT LAB\\x", "C:\\Music\\WHYNOT LAB"),
           "a pasta de cima e outra unidade ficam fora");
    CHECK (isWithin ("C:\\x", "C:\\") && ! isWithin ("C:\\x", ""), "a raiz da unidade contém tudo; raiz vazia não contém nada");
    CHECK (! isWithin ("C:\\Music\\whynot lab\\x.wav", "C:\\Music\\WHYNOT LAB", true) && isWithin ("C:\\Music\\WHYNOT LAB\\x.wav", "C:\\Music\\WHYNOT LAB", true),
           "com exactCase, outra caixa é outra pasta (pasta com diferença de caixa, do WSL)");

    // no disco: uma pasta permitida (com caixa mista), uma de fora e uma junction que aponta para fora
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto base = fs::temp_directory_path (ec) / ("wnl_control_test_" + std::to_string (std::random_device {}()));
    fs::create_directories (base / "Permitida" / "sub", ec);
    fs::create_directories (base / "outra", ec);

    const auto baseText = base.u8string();
    const std::string b (reinterpret_cast<const char*> (baseText.data()), baseText.size());
    const auto roots = prepareRoots ({ b + "\\Permitida", "C:\\a\\CON", "" });

    CHECK (roots.size() == 1, "prepareRoots tira as pastas inválidas (%zu ficou)", roots.size());

    const auto inside = checkPath (b + "\\Permitida\\sub\\beat.wav", roots);
    CHECK (inside.verdict == PathVerdict::inside, "arquivo novo dentro da pasta permitida: dentro (%s)", inside.path.c_str());

    // o resolveLinks devolve a caixa do disco nas partes que existem; é o que deixa o checkPath comparar exato
    const auto otherCase = checkPath (b + "\\PERMITIDA\\SUB\\nova\\beat.wav", roots);
    CHECK (otherCase.verdict == PathVerdict::inside && otherCase.path.find ("\\Permitida\\sub\\nova\\beat.wav") != std::string::npos,
           "o caminho em outra caixa volta com a caixa do disco e fica dentro (%s)", otherCase.path.c_str());

    CHECK (checkPath (b + "\\outra\\beat.wav", roots).verdict == PathVerdict::outside, "arquivo na outra pasta: fora");
    CHECK (checkPath (b + "\\Permitida\\..\\outra\\beat.wav", roots).verdict == PathVerdict::outside, "subindo com .. para a outra: fora");
    CHECK (checkPath (b + "\\Permitida\\CON", roots).verdict == PathVerdict::invalid, "nome de dispositivo: inválido");
    CHECK (checkPath ("relativo\\beat.wav", roots).verdict == PathVerdict::invalid, "caminho relativo: inválido");

    const auto junction = base / "Permitida" / "atalho";
    const auto command = "mklink /J \"" + junction.string() + "\" \"" + (base / "outra").string() + "\" >nul 2>&1";

    if (std::system (command.c_str()) == 0 && fs::exists (junction, ec))
    {
        const auto escaped = checkPath (b + "\\Permitida\\atalho\\beat.wav", roots);
        CHECK (escaped.verdict == PathVerdict::outside, "junction dentro da pasta que aponta para fora: fora (%s)", escaped.path.c_str());
    }
    else
    {
        std::printf ("    (sem mklink /J aqui: o teste da junction foi pulado)\n");
    }

    fs::remove_all (base, ec);
}

static void testLimits()
{
    std::printf ("Limites\n");

    AuthThrottle t;
    bool fourOk = true;

    for (int i = 0; i < 4; ++i)
    {
        t.recordFailure (100.0 + i);
        fourOk = fourOk && ! t.isPaused (100.0 + i);
    }

    CHECK (fourOk, "4 erros de token seguidos ainda não pausam");
    t.recordFailure (104.0);
    CHECK (t.isPaused (104.0) && t.isPaused (133.9) && ! t.isPaused (134.1), "o 5º erro pausa as conexões por 30 s");

    AuthThrottle u;

    for (int i = 0; i < 4; ++i)
        u.recordFailure (1.0);

    u.recordSuccess();
    u.recordFailure (2.0);
    CHECK (! u.isPaused (2.0), "um acerto zera a conta");

    RateLimiter r;
    int burst = 0;

    while (r.tryTake (0.0) && burst < 1000)
        ++burst;

    int afterOneSecond = 0;

    while (r.tryTake (1.0) && afterOneSecond < 1000)
        ++afterOneSecond;

    int afterLongIdle = 0;

    while (r.tryTake (1000.0) && afterLongIdle < 1000)
        ++afterLongIdle;

    CHECK (burst == 50 && afterOneSecond == 20 && afterLongIdle == 50,
           "rajada de 50, depois 20 por segundo, e parado muito tempo não junta mais que 50 (%d, %d, %d)", burst, afterOneSecond, afterLongIdle);

    RateLimiter back;

    for (int i = 0; i < 50; ++i)
        back.tryTake (10.0);

    CHECK (! back.tryTake (5.0), "relógio que volta não dá fichas de graça");
}

//==============================================================================
// Fuzz: nada que venha da rede derruba a ponte nem lê fora do buffer.
static void testFuzz()
{
    std::printf ("Fuzz\n");
    const auto started = std::chrono::steady_clock::now();
    std::mt19937 rng (20261020u);
    auto pick = [&rng] (std::size_t n) { return (std::size_t) (rng() % (std::uint32_t) n); };

    // 1. quadros: bytes aleatórios e quadros válidos estragados, em pedaços aleatórios
    std::vector<std::vector<std::uint8_t>> seeds;

    for (auto op : { ws::Opcode::text, ws::Opcode::ping, ws::Opcode::close, ws::Opcode::continuation, ws::Opcode::pong })
        for (std::size_t n : { 0, 3, 125, 126, 300, 70000 })
            seeds.push_back (ws::encodeClientFrame (op, std::string (n, 'q'), mask, n != 3));

    bool framesOk = true;
    long messages = 0;

    for (int iter = 0; iter < 100000; ++iter)
    {
        std::vector<std::uint8_t> bytes;

        if (iter % 3 == 0)
        {
            bytes.resize (pick (300));

            for (auto& b : bytes)
                b = (std::uint8_t) rng();
        }
        else
        {
            bytes = seeds[pick (seeds.size())];
            const int edits = 1 + (int) pick (4);

            for (int e = 0; e < edits && ! bytes.empty(); ++e)
            {
                const auto at = pick (std::min<std::size_t> (bytes.size(), 16));   // o cabeçalho é o que mais importa
                bytes[at] = (std::uint8_t) rng();
            }

            if (pick (4) == 0)
                bytes.resize (pick (bytes.size() + 1));
        }

        ws::Decoder d (4096);
        std::size_t pos = 0;

        while (pos < bytes.size())
        {
            const auto chunk = std::min (bytes.size() - pos, 1 + pick (64));
            d.feed (bytes.data() + pos, chunk);
            pos += chunk;
            ws::Message m;
            ws::Decoder::Status s;

            while ((s = d.next (m)) == ws::Decoder::Status::message)
            {
                ++messages;
                framesOk = framesOk && m.payload.size() <= 4096
                        && (m.kind != ws::Message::Kind::text || ws::isValidUtf8 (m.payload));
            }

            if (s == ws::Decoder::Status::error)
            {
                framesOk = framesOk && d.failureCode() != 0 && d.buffered() == 0;
                break;
            }

            framesOk = framesOk && d.buffered() <= 4096 + 14 + 64;
        }
    }

    CHECK (framesOk, "100 mil entradas no decodificador de quadros: sem queda, texto sempre UTF-8, nada acima do limite (%ld mensagens)", messages);

    // 2. pedido HTTP: mutações do pedido do Node
    const auto request = nodeRequest (47830, "abc");
    bool httpOk = true;
    int accepted = 0;

    for (int iter = 0; iter < 100000; ++iter)
    {
        auto text = request;
        const int edits = 1 + (int) pick (6);

        for (int e = 0; e < edits; ++e)
        {
            const auto at = pick (text.size() + 1);

            switch (pick (3))
            {
                case 0:  if (at < text.size()) text[at] = (char) rng(); break;
                case 1:  text.insert (text.begin() + (std::ptrdiff_t) at, (char) rng()); break;
                default: if (at < text.size()) text.erase (at, 1 + pick (8)); break;
            }
        }

        const auto r = ws::parseHttpRequest (text);

        if (r.status == ws::ParseStatus::complete)
        {
            httpOk = httpOk && r.consumed <= text.size() && r.consumed <= ws::maxHandshakeBytes;

            for (auto& [name, value] : r.request.headers)
                for (char c : name)
                    httpOk = httpOk && ! (c >= 'A' && c <= 'Z') && c != ' ' && c != ':';

            if (decideHandshake (r.request, 47830, "abc").status == 101)
                ++accepted;
        }
    }

    CHECK (httpOk, "100 mil pedidos HTTP estragados: sem queda, nomes sempre em minúsculas e sem espaço (%d ainda aceitos)", accepted);

    // 3. caminhos: texto aleatório com os caracteres que dão problema
    const std::vector<std::string> pieces { "C:", "c:", "\\", "/", "\\\\", ".", "..", " ", ":", "*", "?", "\"", "<", ">", "|", "a", "Z",
                                            "pasta", "CON", "nul", "COM1", "é", "ç", "\xC3", "\t", "\x01", "\\\\?\\", "WHYNOT LAB", "~1" };
    bool pathsOk = true;

    for (int iter = 0; iter < 100000; ++iter)
    {
        std::string s;
        const int n = 1 + (int) pick (12);

        for (int i = 0; i < n; ++i)
            s += pieces[pick (pieces.size())];

        const auto p = normalisePath (s);

        if (! p.ok)
        {
            pathsOk = pathsOk && ! p.reason.empty();
            continue;
        }

        const auto& q = p.path;
        bool shape = q.size() >= 3 && q[0] >= 'A' && q[0] <= 'Z' && q[1] == ':' && q[2] == '\\'
                  && q.find ('/') == std::string::npos && q.find (':', 2) == std::string::npos
                  && q.find ("\\\\") == std::string::npos && (q.size() == 3 || q.back() != '\\')
                  && q.find ("\\..\\") == std::string::npos && q.find ("\\.\\") == std::string::npos
                  && ws::isValidUtf8 (q);

        const auto again = normalisePath (q);
        pathsOk = pathsOk && shape && again.ok && again.path == q;
    }

    CHECK (pathsOk, "100 mil caminhos aleatórios: os aceitos ficam na forma C:\\..., sem .., sem / e sem ':', e normalizar de novo não muda");

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - started).count();
    std::printf ("    (fuzz em %lld ms)\n", (long long) ms);
}

//==============================================================================
static void testServerFrames()
{
    std::printf ("Quadros do servidor (lado do cliente do autoteste)\n");

    ws::Decoder d (1024, false);
    feed (d, ws::encodeFrame (ws::Opcode::text, "resposta"));
    feed (d, ws::encodeClose (ws::closeCode::policy, "outra conexão"));
    const auto got = decodeAll (d);
    CHECK (got.size() == 2 && got[0].payload == "resposta" && got[1].kind == ws::Message::Kind::close && got[1].code == 1008
           && got[1].payload == "outra conexão", "o cliente lê o texto e o close 1008 do servidor, sem máscara");

    ws::Decoder masked (1024, false);
    feed (masked, ws::encodeClientFrame (ws::Opcode::text, "x", mask));
    decodeAll (masked);
    CHECK (masked.failureCode() == ws::closeCode::protocolError, "quadro do servidor com máscara: 1002");
}

static void testCommandPolicy()
{
    std::printf ("Tabela de risco dos comandos\n");

    CHECK (ruleCount() == 264, "264 comandos classificados: os 220 do main d27a974 (23/10), os 18 do bloco 4, as 2 das raias sem teto, as 2 do pitch bend, os 3 da "
                               "automação completa (26/10), os 6 da voz na playlist, os 7 da afinação nota a nota e os 3 da playlist fatia 6 (27/10) "
                               "e os 3 do swing e do começo do play no PAT (29/10) (%zu)",
           ruleCount());
    CHECK (ruleFor ("project.setSwing").risk == Risk::free && ruleFor ("channel.setGroove").risk == Risk::free
           && ruleFor ("transport.setPatternStart").risk == Risk::free,
           "swing e começo do play no PAT: os 3 comandos são livres (o projeto, com desfazer, e o transporte)");
    CHECK (ruleFor ("audio.setProperties").risk == Risk::free && ruleFor ("playlist.gotoMarker").risk == Risk::free
           && ruleFor ("playlist.selectMarkers").risk == Risk::free,
           "playlist fatia 6: o painel do bloco, pular de marcador e o trecho entre dois são livres (só o projeto e a agulha)");
    CHECK (ruleFor ("audio.analyzeNotes").risk == Risk::free && ruleFor ("audio.setNoteTune").risk == Risk::free
           && ruleFor ("audio.editNotes").risk == Risk::free && ruleFor ("audio.splitNote").risk == Risk::free
           && ruleFor ("audio.mergeNotes").risk == Risk::free && ruleFor ("audio.tuneToScale").risk == Risk::free
           && ruleFor ("audio.clearNoteTune").risk == Risk::free,
           "afinação nota a nota: os 7 comandos mexem só no projeto, com desfazer");
    CHECK (ruleFor ("playlist.cutAt").risk == Risk::free && ruleFor ("playlist.removeSpace").risk == Risk::free
           && ruleFor ("playlist.insertSpace").risk == Risk::free && ruleFor ("audio.setPitch").risk == Risk::free
           && ruleFor ("audio.findSilence").risk == Risk::free && ruleFor ("audio.removeSilence").risk == Risk::free,
           "voz na playlist: cortar, apagar o trecho, abrir espaço, o tom e o silêncio são livres (só mexem no projeto, com desfazer)");
    CHECK (ruleFor ("automation.recordPass").risk == Risk::free && ruleFor ("automation.setTension").risk == Risk::free
           && ruleFor ("automation.setRecording").risk == Risk::free, "gravar uma passada, curvar e o modo de gravar automação: livres");
    CHECK (ruleFor ("record.captureMidi").risk == Risk::free && ruleFor ("record.voice").risk == Risk::blocked
           && ruleFor ("record.setMic").risk == Risk::blocked && ruleFor ("favorites.toggle").risk == Risk::confirm,
           "o Capture MIDI é livre; gravar voz e o microfone ficam com a pessoa; os favoritos confirmam");
    CHECK (ruleFor ("project.exportPack").writesIntoFolder && ruleFor ("project.exportPack").path.name == "folder"
           && ruleFor ("pianoroll.importMidi").path.access == Access::read,
           "o pacote para vender grava dentro da pasta escolhida; importar MIDI só lê o arquivo");
    CHECK (ruleFor ("export.status").risk == Risk::free && ruleFor ("export.cancel").risk == Risk::free,
           "export.status e export.cancel (exportação em segundo plano): livres");

    bool reasons = true, paths = true;

    for (std::size_t i = 0; i < ruleCount(); ++i)
    {
        const auto& r = *findRule (ruleIdAt (i));

        if (r.risk != Risk::free && r.why.empty())
            reasons = false;

        if ((r.path.access == Access::write && r.path.name.empty()) || (! r.path.extension.empty() && r.path.access != Access::write))
            paths = false;
    }

    CHECK (reasons, "todo comando que confirma ou é bloqueado diz por quê");
    CHECK (paths, "extensão só em parâmetro de escrita, e escrita sempre com o nome do parâmetro");

    CHECK (findRule ("comando.inventado") == nullptr && ruleFor ("comando.inventado").risk == Risk::confirm
           && ! ruleFor ("comando.inventado").why.empty(), "comando sem classe pede confirmação, com o motivo");

    const auto& save = ruleFor ("project.save");
    CHECK (save.risk == Risk::free && save.savesProject && save.path.name == "path" && save.path.access == Access::write
           && save.path.extension == ".wnl", "project.save: livre, mas confere o caminho, a extensão .wnl e o projeto aberto");
    CHECK (ruleFor ("project.open").confirmIfUnsaved && ruleFor ("project.open").path.access == Access::read
           && ruleFor ("project.new").confirmIfUnsaved, "project.new e project.open confirmam com alterações não salvas");
    CHECK (ruleFor ("project.exportStems").writesIntoFolder && ruleFor ("project.exportStems").path.name == "folder", "stems: a pasta inteira");
    CHECK (ruleFor ("channel.addPlugin").risk == Risk::confirm && ruleFor ("mixer.addPluginInsert").risk == Risk::confirm
           && ruleFor ("plugins.scan").risk == Risk::confirm, "abrir e escanear VST3 de terceiros: confirma");

    bool blockedOk = true;

    for (const char* id : { "audio.setDevice", "midi.setInputEnabled", "plugins.setFolders", "plugins.unblock", "shortcuts.set",
                            "shortcuts.setPreset", "live.note", "edit.undoToggle", "transport.record", "record.arm", "record.note",
                            "record.commitTake", "record.setInput", "record.setLatency" })
        blockedOk = blockedOk && ruleFor (id).risk == Risk::blocked;

    CHECK (blockedOk, "bloqueados para a IA: placa, MIDI, pastas e desbloqueio de plugin, atalhos, nota ao vivo, Ctrl+Z que alterna e a gravação");
    CHECK (ruleFor ("pianoroll.addNotes").risk == Risk::free && ruleFor ("synth.setParam").risk == Risk::free
           && ruleFor ("ideas.generate").risk == Risk::free && ruleFor ("channel.addSynth").risk == Risk::free,
           "editar o projeto (notas, synth, Ideias, synth nativo) é livre");
    CHECK (riskName (Risk::free) == "free" && riskName (Risk::confirm) == "confirm" && riskName (Risk::blocked) == "blocked",
           "os nomes de risco do command.list");
}

static void testPlatform()
{
    std::printf ("Sistema\n");

    std::array<std::uint8_t, 32> a {}, b {};
    const bool gotA = secureRandomBytes (a.data(), a.size());
    const bool gotB = secureRandomBytes (b.data(), b.size());
    int zeros = 0;

    for (auto x : a)
        zeros += x == 0 ? 1 : 0;

    CHECK (gotA && gotB && a != b && zeros < 8, "BCryptGenRandom dá 32 bytes, diferentes a cada vez");
    CHECK (isProcessAlive (currentProcessId()), "o próprio processo está vivo");
    CHECK (! isProcessAlive (0) && ! isProcessAlive (0xFFFFFFF0u), "pid 0 e um pid que não existe: não está vivo");
}

int main()
{
    testSha1AndBase64();
    testUtf8();
    testHttpParsing();
    testUpgradeAndHandshake();
    testFrames();
    testServerFrames();
    testPaths();
    testLimits();
    testCommandPolicy();
    testPlatform();
    testFuzz();

    std::printf ("\n%s: %d falha(s)\n", failures == 0 ? "TUDO OK" : "FALHOU", failures);
    return failures;
}
