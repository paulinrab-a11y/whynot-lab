#pragma once

#include <JuceHeader.h>

namespace whynot
{

/** Texto da interface em português: "Padrão 1"_t.

    O juce::String lê literais char* como ASCII; com acentos o texto sairia
    corrompido. O projeto compila com /utf-8, então o literal já está em UTF-8
    e este sufixo o converte do jeito certo.
*/
inline juce::String operator""_t (const char* text, std::size_t length)
{
    return juce::String::fromUTF8 (text, (int) length);
}

} // namespace whynot
