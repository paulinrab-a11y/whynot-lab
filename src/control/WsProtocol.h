#pragma once

// O pedaço do WebSocket (RFC 6455) que a ponte de controle da Fase 4 usa, sem JUCE: o aperto de mão HTTP,
// os quadros e o texto em UTF-8. Não toca em socket, só transforma bytes, para testar com `cl` direto.
// A ponte só fala texto, com um cliente só, sem extensões (permessage-deflate) e sem subprotocolo.
// Ver docs/fase-4-plano.md, seção 3.2.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace whynot::control::ws
{

/** SHA-1 (RFC 3174). Só para a chave do aperto de mão, que o WebSocket pede em SHA-1: não serve para proteger nada. */
std::array<std::uint8_t, 20> sha1 (std::string_view data);

/** Base64 padrão (RFC 4648), com '='. */
std::string base64Encode (const std::uint8_t* data, std::size_t size);

/** Base64 padrão estrito: tamanho múltiplo de 4, '=' só no fim, nenhum caractere de fora. */
std::optional<std::vector<std::uint8_t>> base64Decode (std::string_view text);

/** Sec-WebSocket-Accept de uma Sec-WebSocket-Key. */
std::string acceptKey (std::string_view clientKey);

/** UTF-8 bem formado (RFC 3629): sem forma longa, sem metade de par substituto, nada acima de U+10FFFF. */
bool isValidUtf8 (std::string_view text) noexcept;

//==============================================================================
// Aperto de mão HTTP

constexpr std::size_t maxHandshakeBytes = 8 * 1024;
constexpr int maxHeaders = 64;

struct HttpRequest
{
    std::string method, target, version;
    std::vector<std::pair<std::string, std::string>> headers;   // nomes em minúsculas, valores sem espaço nas pontas

    /** O primeiro cabeçalho com esse nome (em minúsculas), ou nullptr. */
    const std::string* header (std::string_view lowerName) const noexcept;
    int count (std::string_view lowerName) const noexcept;
};

enum class ParseStatus { incomplete, complete, invalid, tooLarge };

struct ParseResult
{
    ParseStatus status = ParseStatus::incomplete;
    HttpRequest request;
    std::size_t consumed = 0;    // bytes do pedido, até a linha em branco (quando complete)
    std::string error;           // quando invalid ou tooLarge
};

/** Lê o pedido até a linha em branco. Estrito: linhas em CRLF, sem cabeçalho dobrado, sem espaço antes
    do ':', sem caractere de controle, no máximo 8 KB e 64 cabeçalhos. */
ParseResult parseHttpRequest (std::string_view bytes);

struct UpgradeCheck
{
    bool ok = false;
    int status = 400;            // a resposta HTTP quando não está ok
    std::string reason;
    std::string clientKey;
};

/** Só a parte do WebSocket: GET em "/", HTTP/1.1, Upgrade: websocket, Connection: Upgrade, versão 13 e uma
    chave de 16 bytes. Host, Origin e o token são da ControlPolicy. */
UpgradeCheck checkUpgrade (const HttpRequest&);

/** A resposta 101, sem extensão nem subprotocolo. */
std::string switchingProtocolsResponse (std::string_view clientKey);

/** Resposta de recusa com o motivo no corpo e Connection: close. */
std::string errorResponse (int status, std::string_view reason);

//==============================================================================
// Quadros

enum class Opcode : std::uint8_t
{
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    close = 0x8,
    ping = 0x9,
    pong = 0xA
};

namespace closeCode
{
    constexpr std::uint16_t normal = 1000;
    constexpr std::uint16_t goingAway = 1001;
    constexpr std::uint16_t protocolError = 1002;
    constexpr std::uint16_t unsupportedData = 1003;
    constexpr std::uint16_t noStatus = 1005;        // nunca vai num quadro: o close veio vazio
    constexpr std::uint16_t invalidData = 1007;     // texto que não é UTF-8
    constexpr std::uint16_t policy = 1008;
    constexpr std::uint16_t tooBig = 1009;
    constexpr std::uint16_t internalError = 1011;
}

struct Message
{
    enum class Kind { text, close, ping, pong };

    Kind kind = Kind::text;
    std::string payload;         // texto, o motivo do close ou os dados do ping/pong
    std::uint16_t code = 0;      // só no close
};

/** Junta quadros em mensagens. Na ponte, lê os do cliente (sempre mascarados); com fromClient = false,
    lê os do servidor (nunca mascarados), para o cliente do autoteste. Os bytes chegam em pedaços quaisquer:
    feed() e depois next() até devolver needMore. O tamanho declarado é conferido antes de esperar o
    conteúdo, então um quadro gigante falha na hora, sem guardar nada. Depois de um erro, next() devolve
    sempre error; feche a conexão com failureCode(). */
class Decoder
{
public:
    explicit Decoder (std::size_t maxMessageBytes, bool fromClient = true) noexcept;

    void feed (const void* data, std::size_t size);

    enum class Status { needMore, message, error };
    Status next (Message& out);

    bool failed() const noexcept                        { return failure != 0; }
    std::uint16_t failureCode() const noexcept          { return failure; }
    const std::string& failureReason() const noexcept   { return reason; }
    std::size_t buffered() const noexcept               { return buffer.size() - readPos; }

private:
    Status fail (std::uint16_t code, std::string why);

    std::vector<std::uint8_t> buffer;
    std::size_t readPos = 0;
    const std::size_t maxMessage;
    const bool expectMasked;
    std::string partial;                 // texto de uma mensagem em pedaços
    bool fragmented = false;
    std::uint16_t failure = 0;
    std::string reason;
};

/** Quadro do servidor: FIN, sem máscara. */
std::vector<std::uint8_t> encodeFrame (Opcode, std::string_view payload);

/** Close com código e motivo (cortado em 123 bytes, sem partir um caractere). */
std::vector<std::uint8_t> encodeClose (std::uint16_t code, std::string_view reason);

/** Quadro de cliente, mascarado: para os testes e o cliente do autoteste. */
std::vector<std::uint8_t> encodeClientFrame (Opcode, std::string_view payload, std::array<std::uint8_t, 4> mask, bool fin = true);

} // namespace whynot::control::ws
