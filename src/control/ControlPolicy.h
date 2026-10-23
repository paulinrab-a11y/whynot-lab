#pragma once

// Regras de segurança da ponte de controle (Fase 4), sem JUCE: quem pode ligar (Host, Origin e o token),
// os caminhos que a IA usa sem perguntar e os limites. Ver docs/fase-4-plano.md, seção 5.
// A tabela de risco de cada comando fica em CommandPolicy.cpp (fatia 4.2).

#include "WsProtocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace whynot::control
{

namespace limits
{
    constexpr std::size_t handshakeBytes = ws::maxHandshakeBytes;   // 8 KB
    constexpr std::size_t messageInBytes = 1024 * 1024;             // o que chega ao app
    constexpr std::size_t messageOutBytes = 16 * 1024 * 1024;       // o que sai do app
    constexpr int jsonDepth = 64;
    constexpr int batchSteps = 200;
    constexpr int notesPerCall = 5000;
    constexpr std::size_t paramStringBytes = 4096;
    constexpr int queuedRequests = 32;
    constexpr double requestsPerSecond = 20.0;
    constexpr double requestBurst = 50.0;
    constexpr int pathCodePoints = 1024;
    constexpr int authFailuresBeforePause = 5;
    constexpr double authPauseSeconds = 30.0;
    constexpr double authFailureDelaySeconds = 1.0;
}

//==============================================================================
// Quem pode ligar

/** Host exatamente "127.0.0.1:<porta>" ou "localhost:<porta>". Barra o DNS rebinding: uma página que faz um
    nome dela apontar para 127.0.0.1 manda o próprio nome no Host. */
bool isAllowedHost (std::string_view hostHeader, int port) noexcept;

/** "Bearer <token>" (o esquema em qualquer caixa) → o token, só com caracteres de base64url. */
std::optional<std::string_view> bearerToken (std::string_view authorizationHeader) noexcept;

/** Compara sem sair mais cedo no primeiro byte diferente. */
bool constantTimeEquals (std::string_view a, std::string_view b) noexcept;

/** 32 bytes aleatórios (do BCryptGenRandom, na fatia 4.2) → 43 caracteres de base64url, sem '='. */
std::string encodeToken (const std::array<std::uint8_t, 32>& randomBytes);

struct HandshakeDecision
{
    int status = 400;            // 101 = aceito
    std::string reason;          // por que recusou: vai no corpo da resposta e no registro
    std::string clientKey;       // Sec-WebSocket-Key, quando aceito
    bool authFailure = false;    // conta no AuthThrottle
};

/** O aperto de mão inteiro, nesta ordem: WebSocket bem formado, um Host local, nenhum Origin (navegador
    sempre manda; o Node e os testes não) e o token certo num cabeçalho Authorization. */
HandshakeDecision decideHandshake (const ws::HttpRequest&, int port, std::string_view expectedToken);

/** Erros de token seguidos: com 5, a ponte recusa conexões por 30 s. (A espera de 1 s em cada erro é da ponte.) */
class AuthThrottle
{
public:
    bool isPaused (double nowSeconds) const noexcept   { return nowSeconds < pausedUntil; }
    void recordFailure (double nowSeconds) noexcept;
    void recordSuccess() noexcept                      { consecutiveFailures = 0; }

private:
    int consecutiveFailures = 0;
    double pausedUntil = -1.0e300;
};

/** Balde de fichas: em média `rate` pedidos por segundo, com rajada de até `burst`. */
class RateLimiter
{
public:
    explicit RateLimiter (double rate = limits::requestsPerSecond, double burst = limits::requestBurst) noexcept;
    bool tryTake (double nowSeconds) noexcept;

private:
    double rate, burst, tokens;
    double last = 0.0;
    bool started = false;
};

//==============================================================================
// Caminhos (regras do Windows)

struct NormalisedPath
{
    bool ok = false;
    std::string path;            // UTF-8, "C:\\pasta\\arquivo.wav", sem barra no fim (a raiz fica "C:\\")
    std::string reason;          // quando ok == false
};

/** Só no texto: caminho completo com a letra da unidade; junta "." e ".."; aceita / e \.
    Recusa: UNC, "\\?\" e "\\.\"; fluxo alternativo ("beat.wav:oculto"); nomes de dispositivo (CON, NUL,
    COM1...); os caracteres < > " | ? *; caractere de controle; pedaço terminado em ponto ou espaço (o Windows
    tira, e dois nomes viram o mesmo arquivo); subir acima da raiz; mais de 1024 caracteres; UTF-8 inválido. */
NormalisedPath normalisePath (std::string_view utf8);

/** Resolve junctions e links das partes que já existem (std::filesystem::weakly_canonical) e normaliza de novo. */
NormalisedPath resolveLinks (const NormalisedPath&);

/** child fica dentro de root, ou é ele? Os dois já normalizados. Compara pedaço por pedaço, nunca por prefixo
    de texto ("WHYNOT LABx" não está em "WHYNOT LAB").
    Sem exactCase, as letras ASCII valem em qualquer caixa (uma letra acentuada em outra caixa conta como
    diferente, e o caminho cai fora). O checkPath usa exactCase: depois do resolveLinks as partes que existem
    vêm com a caixa do disco, e numa pasta com diferença de caixa (o recurso do WSL) "whynot lab" e
    "WHYNOT LAB" são pastas diferentes. */
bool isWithin (std::string_view child, std::string_view root, bool exactCase = false) noexcept;

/** Normaliza e resolve os links das pastas permitidas; as inválidas saem da lista. */
std::vector<std::string> prepareRoots (const std::vector<std::string>& utf8Roots);

enum class PathVerdict { inside, outside, invalid };

struct PathCheck
{
    PathVerdict verdict = PathVerdict::invalid;
    std::string path;            // normalizado, com os links resolvidos
    std::string reason;          // quando invalid
};

/** Um caminho que a IA mandou, contra as pastas permitidas (de prepareRoots). Fora delas, o comando pede
    confirmação; inválido, é recusado. */
PathCheck checkPath (std::string_view utf8, const std::vector<std::string>& preparedRoots);

} // namespace whynot::control
