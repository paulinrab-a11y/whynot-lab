#pragma once

#include <JuceHeader.h>

#include "Text.h"

namespace whynot
{

/** Um .wnl, o atalhos.json e afins ficam abaixo de 10 níveis; 256 é folga de sobra. */
constexpr int maxJsonDepth = 256;

/** Maior profundidade de [ e { fora de strings (trata \" e \\). */
inline int jsonDepth (const juce::String& text)
{
    int depth = 0, deepest = 0;
    bool inString = false, escaped = false;

    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;

        if (inString)
        {
            if (escaped)            escaped = false;
            else if (c == '\\')     escaped = true;
            else if (c == '"')      inString = false;
            continue;
        }

        if (c == '"')                   inString = true;
        else if (c == '[' || c == '{')  deepest = juce::jmax (deepest, ++depth);
        else if (c == ']' || c == '}')  depth = juce::jmax (0, depth - 1);
    }

    return deepest;
}

/** juce::JSON::parse com teto de aninhamento. O parser do JUCE é recursivo: milhares de colchetes
    aninhados (arquivo corrompido) estourariam a pilha antes de dar erro. */
inline juce::Result parseJsonSafely (const juce::String& text, juce::var& out)
{
    if (jsonDepth (text) > maxJsonDepth)
        return juce::Result::fail ("O arquivo está corrompido: aninhamento demais."_t);

    return juce::JSON::parse (text, out);
}

} // namespace whynot
