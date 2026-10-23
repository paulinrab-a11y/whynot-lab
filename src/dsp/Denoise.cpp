#include "Denoise.h"

#include <limits>

namespace whynot::dsp
{

namespace
{
    const ParamInfo denoiseParams[] = {
        { "reduction", "Redução",       0, 40, 12, "dB", ParamKind::continuous, false, {},
          "Quanto o ruído desce. 8 a 15 dB limpa sem mexer na voz; acima de 20 pode soar artificial." },
        { "threshold", "Sensibilidade", 0, 20, 6,  "dB", ParamKind::continuous, false, {},
          "Quanto acima do ruído o som precisa estar para passar inteiro. Mais alto limpa mais, mas come o fim das palavras." },
        { "smoothing", "Suavidade",     0, 100, 50, "%", ParamKind::continuous, false, {},
          "Mais suave = menos \"ruído aquático\", com o fim das notas um pouco mais longo." },
        { "listen",    "Ouvir o ruído", 0, 1, 0,   "",   ParamKind::toggle,     false, {},
          "Toca só o que está sendo tirado: se aparecer voz aqui, diminua a sensibilidade." },
    };

    static_assert (std::size (denoiseParams) == DenoiseProcessor::count);

    constexpr float huge = std::numeric_limits<float>::max();
}

const FxInfo& DenoiseProcessor::staticInfo()
{
    static const FxInfo info { "denoise", "Limpa-voz", "Limpa", "Voz",
                               "Tira o ruído de fundo da voz gravada em casa (ventilador, chiado, rua). Aprende o ruído sozinho "
                               "nas pausas. Atrasa 21 ms, compensados pelo app.",
                               denoiseParams };
    return info;
}

int DenoiseProcessor::fftSizeFor (double rate) noexcept
{
    return rate <= 50000.0 ? 1024 : rate <= 100000.0 ? 2048 : 4096;
}

void DenoiseProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = denoiseParams[i].def;

    fftSize = fftSizeFor (rate);
    hop = fftSize / 4;
    bins = fftSize / 2 + 1;
    framesPerWindow = std::max (8, (int) std::lround (1.5 * rate / hop));
    riseLimit = (float) std::pow (10.0, 0.4 * hop / rate);   // +4 dB por segundo
    fft.prepare (fftSize);

    window.resize ((size_t) fftSize);

    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * pi * n / fftSize));   // raiz de Hann periódica

    for (auto* v : { &inL, &inR, &accL, &accR })
        v->assign ((size_t) fftSize, 0.0f);

    readyL.assign ((size_t) hop, 0.0f);
    readyR.assign ((size_t) hop, 0.0f);
    spectrum.assign ((size_t) fftSize, {});

    for (auto* v : { &smoothedPower, &minCurrent, &minPrevious, &noise, &gains, &scratch })
        v->assign ((size_t) bins, 0.0f);

    reset();
}

void DenoiseProcessor::reset() noexcept
{
    for (auto* v : { &inL, &inR, &accL, &accR, &readyL, &readyR, &smoothedPower, &noise })
        std::fill (v->begin(), v->end(), 0.0f);

    std::fill (minCurrent.begin(), minCurrent.end(), huge);
    std::fill (minPrevious.begin(), minPrevious.end(), huge);
    std::fill (gains.begin(), gains.end(), 1.0f);
    pos = frameCounter = 0;
    firstFrame = true;
    setMeter (reductionMeter, 0.0f);
    setMeter (noiseMeter, -120.0f);
}

void DenoiseProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
        values[(size_t) index] = value;
}

void DenoiseProcessor::processFrame() noexcept
{
    const int n = fftSize;

    for (int i = 0; i < n; ++i)
        spectrum[(size_t) i] = { inL[(size_t) i] * window[(size_t) i], inR[(size_t) i] * window[(size_t) i] };

    fft.transform (spectrum.data(), false);

    const float red = values[reduction], thr = values[threshold], smooth = values[smoothing] * 0.01f;
    const bool listening = values[listen] >= 0.5f;
    const float releaseCoeff = (float) std::exp (-(double) hop / ((0.02 + 0.2 * smooth) * sampleRate));
    const bool swapWindows = ++frameCounter >= framesPerWindow;
    double total = 0.0, kept = 0.0, noiseTotal = 0.0;

    for (int k = 0; k < bins; ++k)
    {
        const auto uk = (size_t) k;
        const auto& a = spectrum[uk];
        const auto& b = spectrum[(size_t) ((n - k) % n)];

        // |L|² + |R|² da faixa, a partir da FFT complexa dos dois canais juntos.
        const float power = 0.5f * (std::norm (a) + std::norm (b));
        smoothedPower[uk] = firstFrame ? power : 0.7f * smoothedPower[uk] + 0.3f * power;
        minCurrent[uk] = std::min (minCurrent[uk], smoothedPower[uk]);

        // Estatística de mínimos: o menor valor do último 1 a 2 s, vezes 4 (6 dB). O mínimo de uma energia
        // que flutua fica bem abaixo da média; com 2x (3 dB) o ruído branco do teste descia só 9 dB dos 12 pedidos,
        // com 4x desce 11,5 dB e a voz continua igual.
        const float estimate = 4.0f * std::min (minCurrent[uk], minPrevious[uk]);
        // Subida limitada só onde há som bem acima do ruído (10 dB): uma nota longa não vira "ruído",
        // e o ruído que muda de verdade (perto da estimativa) continua sendo acompanhado na hora.
        const float rise = smoothedPower[uk] > 10.0f * noise[uk] ? riseLimit : 1.0e9f;
        noise[uk] = firstFrame ? estimate : std::min (0.8f * noise[uk] + 0.2f * estimate, noise[uk] * rise);
        noiseTotal += noise[uk];

        if (swapWindows)
        {
            minPrevious[uk] = minCurrent[uk];
            minCurrent[uk] = huge;
        }

        const float snrDb = 10.0f * std::log10 (std::max (1.0e-20f, smoothedPower[uk]) / std::max (1.0e-20f, noise[uk]));
        const float open = std::clamp ((snrDb - thr) / 6.0f, 0.0f, 1.0f);
        const float target = red <= 0.0f ? 1.0f : dbToGain (-red * (1.0f - open));

        // Abre na hora, fecha com a suavidade.
        gains[uk] = target >= gains[uk] ? target : target + (gains[uk] - target) * releaseCoeff;
        total += power;
        kept += power * gains[uk] * gains[uk];
    }

    if (swapWindows)
        frameCounter = 0;

    // Alisa entre faixas vizinhas (uma passada; duas com mais suavidade) numa cópia: o estado não acumula.
    std::copy (gains.begin(), gains.end(), scratch.begin());

    for (int pass = 0; pass < (smooth > 0.5f ? 2 : smooth > 0.0f ? 1 : 0); ++pass)
    {
        float previous = scratch[0];

        for (int k = 0; k < bins; ++k)
        {
            const float current = scratch[(size_t) k];
            const float next = k + 1 < bins ? scratch[(size_t) k + 1] : current;
            scratch[(size_t) k] = 0.25f * previous + 0.5f * current + 0.25f * next;
            previous = current;
        }
    }

    for (int k = 0; k < bins; ++k)
    {
        const float g = listening ? 1.0f - scratch[(size_t) k] : scratch[(size_t) k];
        spectrum[(size_t) k] *= g;

        if (k > 0 && k < n / 2)
            spectrum[(size_t) (n - k)] *= g;
    }

    fft.transform (spectrum.data(), true);

    // Janela de volta e soma com sobreposição (Hann com 75% soma 2: o 0,5 compensa).
    for (int i = 0; i < n; ++i)
    {
        const float w = 0.5f * window[(size_t) i];
        accL[(size_t) i] += spectrum[(size_t) i].real() * w;
        accR[(size_t) i] += spectrum[(size_t) i].imag() * w;
    }

    std::copy (accL.begin(), accL.begin() + hop, readyL.begin());
    std::copy (accR.begin(), accR.begin() + hop, readyR.begin());

    for (auto* acc : { &accL, &accR })
    {
        std::copy (acc->begin() + hop, acc->end(), acc->begin());
        std::fill (acc->end() - hop, acc->end(), 0.0f);
    }

    for (auto* in : { &inL, &inR })
        std::copy (in->begin() + hop, in->end(), in->begin());

    firstFrame = false;
    setMeter (reductionMeter, total > 1.0e-20 ? (float) std::max (0.0, -10.0 * std::log10 (std::max (1.0e-20, kept / total))) : 0.0f);
    setMeter (noiseMeter, (float) (10.0 * std::log10 (std::max (1.0e-20, noiseTotal / (0.5 * (double) n * n)))));
}

void DenoiseProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;
    const int writeBase = fftSize - hop;

    for (int i = 0; i < block.numSamples; ++i)
    {
        inL[(size_t) (writeBase + pos)] = block.left[i];
        inR[(size_t) (writeBase + pos)] = block.right[i];
        block.left[i] = readyL[(size_t) pos];
        block.right[i] = readyR[(size_t) pos];

        if (++pos == hop)
        {
            processFrame();
            pos = 0;
        }
    }
}

} // namespace whynot::dsp
