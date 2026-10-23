#pragma once

#include <JuceHeader.h>

namespace whynot
{

/** O nome de uma nota em português (Dó, Ré, Mi...), com a mesma oitava do Project::keyName: Dó3 é o C3.
    Quem só lê português reconhece "Dó"; o nome em letra (C3) aparece entre parênteses nas dicas. */
inline juce::String noteNamePt (int key, bool withOctave = true)
{
    static const char* const names[] = { "Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si" };
    const auto name = juce::String::fromUTF8 (names[((key % 12) + 12) % 12]);
    return withOctave ? name + juce::String (key / 12 - 1) : name;
}

} // namespace whynot
