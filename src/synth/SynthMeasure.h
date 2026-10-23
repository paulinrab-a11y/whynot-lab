#pragma once

// Medição de nível de um timbre (sem JUCE): toca uma nota e mede loudness e pico.
// Usada para nivelar os presets, nos testes e no autoteste do app.

#include "SynthPresets.h"

#include <array>
#include <span>

namespace whynot::synth
{

struct LevelReading
{
    float lufs = -100.0f;     // loudness momentânea máxima (janela de 400 ms, BS.1770)
    float peakDb = -100.0f;   // pico antes do soft clip
    bool finite = true;
};

/** Toca `key` por levelNoteSeconds (com levelVelocity) e mede. */
LevelReading measureLevel (std::span<const float> values, int key, double sampleRate = 48000.0);

/** O mesmo com várias notas juntas (até 8), como o acorde de teste dos timbres de acorde. */
LevelReading measureLevel (std::span<const float> values, std::span<const int> keys, double sampleRate = 48000.0);

/** Acorde de teste: menor com sétima a partir de `root` (4 notas, como os acordes do Ideias). */
std::array<int, 4> chordKeys (int root) noexcept;

/** Volume (dB) que leva o timbre ao alvo de loudness sem passar do teto de pico (nos timbres de
    acorde, nem o pico de um acorde de 4 notas passa de levelChordPeakCeilingDb). */
float levelledVolume (const Preset&, double sampleRate = 48000.0);

} // namespace whynot::synth
