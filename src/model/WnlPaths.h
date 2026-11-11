#pragma once

// Regras dos caminhos de arquivo que um .wnl guarda (sample do canal, camadas, áudio do bloco), sem JUCE:
// só texto, nada aqui toca o disco nem a rede. Ver docs/arquitetura.md, seção 4 ("Caminhos que o arquivo traz").
// As regras de nome do Windows (dispositivo, fluxo alternativo, caracteres proibidos) são as da ponte de
// controle (control::normalisePath), para não existirem duas listas.

#include "../control/ControlPolicy.h"

#include <string>
#include <string_view>

namespace whynot::wnl::paths
{

enum class Kind
{
    empty,      // sem caminho
    relative,   // "samples/kick.wav": fica dentro da pasta do projeto
    absolute,   // "C:/Samples/kick.wav": letra da unidade e caminho completo
    network,    // "\\host\pasta", "//host/pasta", "\\?\UNC\...", "\\?\C:\..." e "\\.\...": rede ou dispositivo
    escapes,    // relativo que sai da pasta do projeto com ".."
    invalid     // o resto: "C:arquivo", "\pasta", nome de dispositivo, "arquivo:fluxo", caractere proibido...
};

inline bool isSeparator (char c) noexcept   { return c == '/' || c == '\\'; }

/** Que tipo de caminho é este texto (UTF-8)? Só olha o texto. */
inline Kind classify (std::string_view utf8)
{
    if (utf8.empty())
        return Kind::empty;

    if (utf8.size() >= 2 && isSeparator (utf8[0]) && isSeparator (utf8[1]))
        return Kind::network;

    if (isSeparator (utf8[0]))
        return Kind::invalid;   // "\pasta": a raiz da unidade em que o app estiver

    const bool letter = (utf8[0] >= 'a' && utf8[0] <= 'z') || (utf8[0] >= 'A' && utf8[0] <= 'Z');

    if (letter && utf8.size() >= 2 && utf8[1] == ':')
        return control::normalisePath (utf8).ok ? Kind::absolute : Kind::invalid;   // "C:arquivo" cai aqui e é recusado

    // relativo: nenhum ".." pode subir além da pasta do projeto, nem no meio do caminho
    int depth = 0;

    for (auto rest = utf8; ! rest.empty();)
    {
        std::size_t cut = 0;

        while (cut < rest.size() && ! isSeparator (rest[cut]))
            ++cut;

        const auto part = rest.substr (0, cut);
        rest.remove_prefix (cut < rest.size() ? cut + 1 : cut);

        if (part.empty() || part == ".")
            continue;

        if (part == "..")
        {
            if (--depth < 0)
                return Kind::escapes;
        }
        else
        {
            ++depth;
        }
    }

    if (depth == 0)
        return Kind::invalid;   // "." ou "a/..": a própria pasta, não um arquivo

    std::string rooted ("C:\\");
    rooted += utf8;
    return control::normalisePath (rooted).ok ? Kind::relative : Kind::invalid;
}

/** O app pode usar este caminho vindo de um arquivo? (vazio conta: é "sem sample") */
inline bool isUsable (Kind k) noexcept   { return k == Kind::empty || k == Kind::relative || k == Kind::absolute; }

/** Só o nome do arquivo (o que vem depois da última barra), se ele sozinho for um caminho relativo válido;
    senão, vazio. É o que sobra de um caminho recusado: o app procura por esse nome na pasta do projeto. */
inline std::string safeFileName (std::string_view utf8)
{
    std::size_t start = utf8.size();

    while (start > 0 && ! isSeparator (utf8[start - 1]))
        --start;

    const auto name = utf8.substr (start);
    return classify (name) == Kind::relative ? std::string (name) : std::string();
}

/** O caminho como o app aceita ao ABRIR um arquivo: o relativo que fica na pasta e o absoluto com letra de
    unidade passam como vieram; o de rede, o que sai da pasta e o inválido viram só o nome do arquivo (ou vazio). */
inline std::string sanitised (std::string_view utf8)
{
    return isUsable (classify (utf8)) ? std::string (utf8) : safeFileName (utf8);
}

/** A extensão é uma das que o app importa como áudio? (a mesma lista do navegador de samples e do rack:
    wav, aif, aiff, flac, ogg e mp3; em qualquer caixa) */
inline bool hasAudioExtension (std::string_view utf8)
{
    std::size_t start = utf8.size();

    while (start > 0 && ! isSeparator (utf8[start - 1]))
        --start;

    const auto name = utf8.substr (start);
    const auto dot = name.rfind ('.');

    if (dot == std::string_view::npos || dot == 0)
        return false;

    std::string ext (name.substr (dot + 1));

    for (auto& c : ext)
        if (c >= 'A' && c <= 'Z')
            c = (char) (c - 'A' + 'a');

    for (const char* known : { "wav", "aif", "aiff", "flac", "ogg", "mp3" })
        if (ext == known)
            return true;

    return false;
}

/** Texto curto para o registro e para a mensagem ao usuário. */
inline const char* describe (Kind k) noexcept
{
    switch (k)
    {
        case Kind::network:   return "caminho de rede";
        case Kind::escapes:   return "sai da pasta do projeto";
        case Kind::invalid:   return "caminho inválido";
        case Kind::empty:
        case Kind::relative:
        case Kind::absolute:  break;
    }

    return "";
}

} // namespace whynot::wnl::paths
