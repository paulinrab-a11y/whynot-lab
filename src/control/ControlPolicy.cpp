#include "ControlPolicy.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace whynot::control
{

namespace
{
    char lowerAscii (char c) noexcept   { return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c; }
    char upperAscii (char c) noexcept   { return (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c; }
    bool isAsciiLetter (char c) noexcept   { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
    bool isSeparator (char c) noexcept     { return c == '\\' || c == '/'; }

    bool equalsIgnoreCase (std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size())
            return false;

        for (std::size_t i = 0; i < a.size(); ++i)
            if (lowerAscii (a[i]) != lowerAscii (b[i]))
                return false;

        return true;
    }

    bool isTokenChar (char c) noexcept
    {
        return isAsciiLetter (c) || (c >= '0' && c <= '9') || c == '-' || c == '_';
    }

    /** CON, PRN, AUX, NUL, CONIN$, CONOUT$, COM0-9 e LPT0-9 (também com ¹ ² ³), com ou sem extensão. */
    bool isDeviceName (std::string_view part) noexcept
    {
        auto base = part.substr (0, part.find ('.'));

        while (! base.empty() && base.back() == ' ')
            base.remove_suffix (1);

        if (base.size() > 7)
            return false;

        char up[8] = {};

        for (std::size_t i = 0; i < base.size(); ++i)
            up[i] = upperAscii (base[i]);

        const std::string_view b (up, base.size());

        for (const char* fixed : { "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$" })
            if (b == fixed)
                return true;

        if (b.size() >= 4 && (b.substr (0, 3) == "COM" || b.substr (0, 3) == "LPT"))
        {
            const auto tail = b.substr (3);

            if (tail.size() == 1 && tail[0] >= '0' && tail[0] <= '9')
                return true;

            if (tail == "\xC2\xB9" || tail == "\xC2\xB2" || tail == "\xC2\xB3")   // ¹ ² ³
                return true;
        }

        return false;
    }

    NormalisedPath failPath (const char* why)
    {
        NormalisedPath n;
        n.reason = why;
        return n;
    }
}

//==============================================================================
bool isAllowedHost (std::string_view host, int port) noexcept
{
    if (port <= 0 || port > 65535)
        return false;

    const auto colon = host.rfind (':');

    if (colon == std::string_view::npos || colon + 1 >= host.size())
        return false;

    const auto name = host.substr (0, colon);
    const auto digits = host.substr (colon + 1);

    if (digits.size() > 5 || (digits.size() > 1 && digits[0] == '0'))
        return false;

    int value = 0;

    for (const char c : digits)
    {
        if (c < '0' || c > '9')
            return false;

        value = value * 10 + (c - '0');
    }

    return value == port && (name == "127.0.0.1" || equalsIgnoreCase (name, "localhost"));
}

std::optional<std::string_view> bearerToken (std::string_view header) noexcept
{
    if (header.size() < 8 || ! equalsIgnoreCase (header.substr (0, 6), "bearer") || header[6] != ' ')
        return std::nullopt;

    auto token = header.substr (7);

    while (! token.empty() && token.front() == ' ')
        token.remove_prefix (1);

    if (token.empty() || token.size() > 256)
        return std::nullopt;

    for (const char c : token)
        if (! isTokenChar (c))
            return std::nullopt;

    return token;
}

bool constantTimeEquals (std::string_view a, std::string_view b) noexcept
{
    // percorre o tamanho de b (o valor esperado) sempre inteiro; tamanho diferente já conta como diferença
    std::size_t diff = a.size() ^ b.size();

    for (std::size_t i = 0; i < b.size(); ++i)
    {
        const auto x = (unsigned char) (i < a.size() ? a[i] : 0);
        diff |= (std::size_t) (x ^ (unsigned char) b[i]);
    }

    return diff == 0;
}

std::string encodeToken (const std::array<std::uint8_t, 32>& randomBytes)
{
    auto s = ws::base64Encode (randomBytes.data(), randomBytes.size());

    for (auto& c : s)
    {
        if (c == '+')       c = '-';
        else if (c == '/')  c = '_';
    }

    while (! s.empty() && s.back() == '=')
        s.pop_back();

    return s;
}

HandshakeDecision decideHandshake (const ws::HttpRequest& req, int port, std::string_view expectedToken)
{
    HandshakeDecision d;

    auto refuse = [&d] (int status, std::string why, bool auth = false)
    {
        d.status = status;
        d.reason = std::move (why);
        d.authFailure = auth;
        return d;
    };

    if (expectedToken.empty())
        return refuse (503, "A ponte ainda não tem token.");

    const auto upgrade = ws::checkUpgrade (req);

    if (! upgrade.ok)
        return refuse (upgrade.status, upgrade.reason);

    if (req.count ("host") != 1)
        return refuse (400, "Mande um cabeçalho Host, só um.");

    if (! isAllowedHost (*req.header ("host"), port))
        return refuse (403, "Host que não é local: a WHYNOT só aceita 127.0.0.1 ou localhost nesta porta.");

    if (req.count ("origin") != 0)
        return refuse (403, "Pedido com Origin (de navegador) não é aceito.");

    if (req.count ("authorization") != 1)
        return refuse (401, "Falta o token: mande \"Authorization: Bearer <token>\", uma vez só.", true);

    const auto token = bearerToken (*req.header ("authorization"));

    if (! token || ! constantTimeEquals (*token, expectedToken))
        return refuse (401, "Token errado.", true);

    d.status = 101;
    d.clientKey = upgrade.clientKey;
    return d;
}

void AuthThrottle::recordFailure (double nowSeconds) noexcept
{
    if (++consecutiveFailures >= limits::authFailuresBeforePause)
    {
        consecutiveFailures = 0;
        pausedUntil = nowSeconds + limits::authPauseSeconds;
    }
}

RateLimiter::RateLimiter (double r, double b) noexcept : rate (r), burst (b), tokens (b) {}

bool RateLimiter::tryTake (double nowSeconds) noexcept
{
    if (! started)
    {
        started = true;
        last = nowSeconds;
    }
    else if (nowSeconds > last)
    {
        tokens = std::min (burst, tokens + (nowSeconds - last) * rate);
        last = nowSeconds;
    }

    if (tokens < 1.0)
        return false;

    tokens -= 1.0;
    return true;
}

//==============================================================================
NormalisedPath normalisePath (std::string_view utf8)
{
    if (utf8.empty())
        return failPath ("Caminho vazio.");

    if (! ws::isValidUtf8 (utf8))
        return failPath ("Caminho com UTF-8 inválido.");

    int codePoints = 0;

    for (const char ch : utf8)
    {
        const auto c = (unsigned char) ch;

        if (c < 0x20 || c == 0x7F)
            return failPath ("Caractere de controle no caminho.");

        if ((c & 0xC0) != 0x80)
            ++codePoints;
    }

    if (codePoints > limits::pathCodePoints)
        return failPath ("Caminho com mais de 1024 caracteres.");

    if (utf8.size() >= 2 && isSeparator (utf8[0]) && isSeparator (utf8[1]))
        return failPath ("Caminho de rede ou de dispositivo (\\\\...) não é aceito.");

    if (utf8.size() < 3 || ! isAsciiLetter (utf8[0]) || utf8[1] != ':' || ! isSeparator (utf8[2]))
        return failPath ("Use o caminho completo, com a letra da unidade (C:\\...).");

    std::vector<std::string_view> parts;
    auto rest = utf8.substr (3);

    while (! rest.empty())
    {
        std::size_t cut = 0;

        while (cut < rest.size() && ! isSeparator (rest[cut]))
            ++cut;

        const auto part = rest.substr (0, cut);
        rest.remove_prefix (std::min (rest.size(), cut + 1));

        if (part.empty() || part == ".")
            continue;

        if (part == "..")
        {
            if (parts.empty())
                return failPath ("O caminho sobe acima da raiz da unidade.");

            parts.pop_back();
            continue;
        }

        if (part.find (':') != std::string_view::npos)
            return failPath ("Fluxo alternativo (\"arquivo:nome\") não é aceito.");

        if (part.find_first_of ("<>\"|?*") != std::string_view::npos)
            return failPath ("Caractere proibido no Windows (< > \" | ? *).");

        if (part.back() == '.' || part.back() == ' ')
            return failPath ("Nome terminado em ponto ou espaço.");

        if (isDeviceName (part))
            return failPath ("Nome reservado do Windows (CON, NUL, COM1...).");

        parts.push_back (part);
    }

    NormalisedPath n;
    n.ok = true;
    n.path = std::string (1, upperAscii (utf8[0])) + ":\\";

    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        if (i > 0)
            n.path += '\\';

        n.path += parts[i];
    }

    return n;
}

NormalisedPath resolveLinks (const NormalisedPath& in)
{
    if (! in.ok)
        return in;

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p (std::u8string_view (reinterpret_cast<const char8_t*> (in.path.data()), in.path.size()));
    const auto canonical = fs::weakly_canonical (p, ec);

    if (ec)
        return failPath ("Não consegui conferir o caminho no disco.");

    const auto u8 = canonical.u8string();
    std::string s (reinterpret_cast<const char*> (u8.data()), u8.size());

    // o Windows pode devolver a forma longa "\\?\C:\..."; a de rede ("\\?\UNC\...") continua recusada
    if (s.size() >= 7 && s.compare (0, 4, "\\\\?\\") == 0 && isAsciiLetter (s[4]) && s[5] == ':' && s[6] == '\\')
        s.erase (0, 4);

    return normalisePath (s);
}

bool isWithin (std::string_view child, std::string_view root, bool exactCase) noexcept
{
    if (root.empty() || child.size() < root.size())
        return false;

    for (std::size_t i = 0; i < root.size(); ++i)
        if (exactCase ? child[i] != root[i] : lowerAscii (child[i]) != lowerAscii (root[i]))
            return false;

    if (child.size() == root.size() || root.back() == '\\')   // o mesmo caminho, ou a raiz da unidade
        return true;

    return child[root.size()] == '\\';
}

std::vector<std::string> prepareRoots (const std::vector<std::string>& utf8Roots)
{
    std::vector<std::string> out;

    for (auto& raw : utf8Roots)
    {
        const auto resolved = resolveLinks (normalisePath (raw));

        if (resolved.ok && std::find (out.begin(), out.end(), resolved.path) == out.end())
            out.push_back (resolved.path);
    }

    return out;
}

PathCheck checkPath (std::string_view utf8, const std::vector<std::string>& preparedRoots)
{
    PathCheck c;
    const auto resolved = resolveLinks (normalisePath (utf8));

    if (! resolved.ok)
    {
        c.reason = resolved.reason;
        return c;
    }

    c.path = resolved.path;
    c.verdict = PathVerdict::outside;

    for (auto& root : preparedRoots)
    {
        if (isWithin (c.path, root, true))
        {
            c.verdict = PathVerdict::inside;
            break;
        }
    }

    return c;
}

} // namespace whynot::control
