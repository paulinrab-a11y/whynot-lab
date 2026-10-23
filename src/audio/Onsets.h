#pragma once

// Ataques de um som (onde cada batida começa), para o fatiador do sampler e o "picotar" do clipe
// de áudio na playlist. Roda na thread de mensagem: aloca e não serve para a thread de áudio.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace whynot::audio
{

/** Quanto o fatiador é sensível: "suave" acha só as batidas fortes; "forte" acha até as fracas. */
enum class Sensitivity { dull, medium, sharp };

/** Começos dos ataques em `mono` (em frames, em ordem).

    A energia vai em quadros de 10 ms a cada 5 ms, em dB. Um ataque é um quadro cuja energia sobe
    pelo menos 9, 6 ou 3,5 dB (suave, médio, forte) sobre o menor dos dois quadros anteriores, acima
    de -50 dB do quadro mais forte do som, e é o maior da vizinhança. Dois ataques ficam pelo menos
    120, 70 ou 40 ms longe um do outro. A marca cai no começo da subida: na primeira amostra que
    passa de um quarto do pico da batida (e do dobro do que soava antes), recuada até o cruzamento
    de zero anterior (no máximo 2 ms), para a fatia não começar no meio de um ciclo.
*/
inline std::vector<double> detectOnsets (const float* mono, std::size_t n, double rate, Sensitivity sensitivity)
{
    std::vector<double> out;

    if (mono == nullptr || rate <= 0.0 || n < 64)
        return out;

    const auto hop = std::max<std::size_t> (16, (std::size_t) std::lround (0.005 * rate));
    const auto frame = 2 * hop;

    if (n < 3 * frame)
        return out;

    std::vector<double> energy;

    for (std::size_t pos = 0; pos + frame <= n; pos += hop)
    {
        double sum = 0.0;

        for (std::size_t i = pos; i < pos + frame; ++i)
            sum += (double) mono[i] * (double) mono[i];

        energy.push_back (10.0 * std::log10 (sum / (double) frame + 1.0e-12));
    }

    const double top = *std::max_element (energy.begin(), energy.end());
    const double threshold = sensitivity == Sensitivity::dull ? 9.0 : sensitivity == Sensitivity::medium ? 6.0 : 3.5;
    const double gapSeconds = sensitivity == Sensitivity::dull ? 0.12 : sensitivity == Sensitivity::medium ? 0.07 : 0.04;
    const auto minGap = (double) gapSeconds * rate;
    const double floorDb = top - 50.0;

    std::vector<double> rise (energy.size(), 0.0);

    for (std::size_t k = 1; k < energy.size(); ++k)
    {
        const double before = k >= 2 ? std::min (energy[k - 1], energy[k - 2]) : energy[k - 1];

        if (energy[k] >= floorDb)
            rise[k] = std::max (0.0, energy[k] - before);
    }

    double last = -std::numeric_limits<double>::infinity();

    for (std::size_t k = 1; k < rise.size(); ++k)
    {
        if (rise[k] < threshold)
            continue;

        bool biggest = true;

        for (std::size_t j = k > 2 ? k - 2 : 0; j <= std::min (rise.size() - 1, k + 2) && biggest; ++j)
            if (j != k && (rise[j] > rise[k] || (rise[j] == rise[k] && j < k)))
                biggest = false;

        if (! biggest)
            continue;

        // A subida está entre o fim do quadro anterior e o fim deste.
        const std::size_t from = k * hop;
        const std::size_t to = std::min (n, k * hop + frame + hop);
        float peak = 0.0f, prior = 0.0f;

        for (std::size_t i = from; i < to; ++i)
            peak = std::max (peak, std::abs (mono[i]));

        for (std::size_t i = from >= hop ? from - hop : 0; i < from; ++i)
            prior = std::max (prior, std::abs (mono[i]));

        const float level = std::max (0.25f * peak, 2.0f * prior);
        std::size_t at = from + hop;

        for (std::size_t i = from; i < to; ++i)
        {
            if (std::abs (mono[i]) >= level)
            {
                at = i;
                break;
            }
        }

        // Recua até o cruzamento de zero anterior (no máximo 2 ms).
        const std::size_t limit = at > (std::size_t) (0.002 * rate) ? at - (std::size_t) (0.002 * rate) : 0;

        while (at > limit && mono[at - 1] != 0.0f && (mono[at - 1] > 0.0f) == (mono[at] > 0.0f))
            --at;

        if ((double) at - last >= minGap)
        {
            out.push_back ((double) at);
            last = (double) at;
        }
    }

    return out;
}

/** Começos das fatias: o começo do som sempre, mais os ataques (ou a grade), em frações do
    sample de `numFrames`, em ordem e sem fatias menores que `minFrames`. */
inline std::vector<float> sliceStarts (const std::vector<double>& marks, std::size_t numFrames, double minFrames)
{
    std::vector<float> out { 0.0f };

    if (numFrames < 2)
        return out;

    const double last = (double) (numFrames - 1);
    double previous = 0.0;

    for (double m : marks)
    {
        if (m - previous >= minFrames && last - m >= minFrames)
        {
            out.push_back ((float) (m / last));
            previous = m;
        }
    }

    return out;
}

} // namespace whynot::audio
