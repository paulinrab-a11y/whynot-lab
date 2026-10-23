#pragma once

// Nota de um sample (808, baixo, melodia) pelo algoritmo YIN (de Cheveigné e Kawahara, 2002).
// Roda numa thread de trabalho, ao carregar o sample: aloca memória e não serve para a thread de áudio.

#include <cstddef>
#include <vector>

namespace whynot::dsp
{

struct PitchEstimate
{
    bool found = false;
    double hz = 0.0;
    double midi = 0.0;          // nota MIDI fracionária (69 = Lá 440 Hz)
    int key = -1;               // nota MIDI mais próxima
    double cents = 0.0;         // quanto a nota está fora da mais próxima, de -50 a +50
    double confidence = 0.0;    // 0..1: quanto do sample concorda com essa nota
};

struct PitchOptions
{
    double minHz = 27.5;        // Lá 0: nenhum 808 que se ouve fica abaixo disso
    double maxHz = 2000.0;
    double threshold = 0.15;    // limiar do YIN na diferença normalizada
    double voicedLimit = 0.25;  // acima disso o quadro não tem tom (ruído, chimbal)
};

/** Tom de um trecho curto. Usa no máximo 2 × (taxa / minHz) amostras a partir de `x`.
    `confidence` aqui é 1 − aperiodicidade do quadro.
*/
PitchEstimate detectFramePitch (const float* x, std::size_t numFrames, double sampleRate,
                                const PitchOptions& options = {});

/** Nota principal de um sample inteiro ([canal][frame]). Mistura os canais, pula o ataque,
    analisa vários quadros ao longo do som e fica com o grupo que concorda (±50 cents).
    Assim a "queda" de tom do começo de um 808 não decide a nota. Sample sem tom (ruído,
    chimbal, silêncio) ou com notas que não se decidem devolve found = false.
*/
PitchEstimate detectSamplePitch (const std::vector<std::vector<float>>& channels, double sampleRate,
                                 const PitchOptions& options = {});

/** Preenche midi, key e cents a partir de hz. */
PitchEstimate pitchFromHz (double hz, double confidence);

} // namespace whynot::dsp
