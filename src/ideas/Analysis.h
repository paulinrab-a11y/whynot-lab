#pragma once

// Ouvir o que já existe: a tonalidade e os acordes de um conjunto de notas.

#include "Generator.h"
#include "Theory.h"

#include <vector>

namespace whynot::ideas
{

struct DetectedKey
{
    int root = 0;
    bool major = false;
    double confidence = 0.0;   // correlação com o perfil da tonalidade (-1..1)

    Scale scale() const   { return Scale::fromName (root, major ? "major" : "minor"); }
};

/** Tonalidade pelo perfil de Krumhansl-Kessler (notas pesadas pela duração). */
DetectedKey detectKey (const std::vector<GenNote>&);

struct DetectedChord
{
    ChordEvent event;
    double score = 0.0;   // 0..1: quanto as notas batem com o acorde
};

/** Acordes em janelas de `segment` ticks (meio compasso por padrão), juntando vizinhos iguais.
    Janelas sem nota continuam o acorde anterior. */
std::vector<DetectedChord> detectChords (const std::vector<GenNote>&, int totalTicks, int segment = ticksPerBar / 2);

} // namespace whynot::ideas
