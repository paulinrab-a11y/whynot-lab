#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

namespace whynot
{

/** Grava o projeto num .mid (formato 1, 960 PPQ): uma faixa por canal com notas, com o nome do canal,
    mais o tempo e o compasso na primeira faixa. As notas saem exatamente como o motor toca:
    passos viram notas, o padrão se repete e as regras de mono/slide valem.
    - song = false: o padrão atual, `loops` vezes;
    - song = true: a playlist inteira.
    Canais de áudio ficam de fora (não têm notas).
*/
juce::Result writeMidiFile (const Project&, bool song, int loops, const juce::File& out);

} // namespace whynot
