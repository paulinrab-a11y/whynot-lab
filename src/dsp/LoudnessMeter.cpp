#include "LoudnessMeter.h"

namespace whynot::dsp
{

namespace
{
    float energyToLufs (double meanSquare) noexcept
    {
        return meanSquare <= 1.0e-20 ? LoudnessMeter::silence
                                     : (float) std::max (-100.0, -0.691 + 10.0 * std::log10 (meanSquare));
    }
}

void LoudnessMeter::prepare (double sampleRate)
{
    rate = sampleRate;
    hopSamples = std::max (1, (int) std::lround (sampleRate * 0.1));

    // Filtro K do BS.1770, na forma que reproduz exatamente os coeficientes de 48 kHz da norma
    // em qualquer taxa: prateleira alta (efeito da cabeça) + passa-altas RLB.
    {
        constexpr double f0 = 1681.974450955533, q = 0.7071752369554196, gainDb = 3.999843853973347;
        const double vh = std::pow (10.0, gainDb / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        // H(s) = (Vh s² + Vb s/Q + 1) / (s² + s/Q + 1)
        shelf = SvfCoeffs::custom (f0, q, vh, (vb - vh) / q, 1.0 - vh, sampleRate);
    }

    {
        constexpr double f0 = 38.13547087602444, q = 0.5003270373238773;
        // A norma usa o numerador [1, -2, 1] sem normalizar: ganho a0 na banda passante.
        const double k = std::tan (pi * f0 / sampleRate);
        const double a0 = 1.0 + k / q + k * k;
        highpass = SvfCoeffs::custom (f0, q, a0, -a0 / q, -a0, sampleRate);
    }

    upL.prepare();
    upR.prepare();
    corrCoeff = std::exp (-1.0 / (0.3 * sampleRate));
    reset();
}

void LoudnessMeter::reset() noexcept
{
    shelfL.reset(); shelfR.reset(); hpL.reset(); hpR.reset();
    upL.reset(); upR.reset();
    hopCount = 0;
    hopEnergy = 0.0;
    hops.fill (0.0);
    hopsWritten = 0;
    binEnergy.fill (0.0);
    binCount.fill (0.0);
    peakSinceReset = 0.0f;
    corrLR = corrLL = corrRR = 0.0;

    momentaryLufs = silence;
    shortTermLufs = silence;
    integratedLufs = silence;
    truePeakDb = silence;
    correlationValue = 1.0f;
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
        reset();

    float peak = peakSinceReset;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];

        const float kl = hpL.process (shelfL.process (l, shelf), highpass);
        const float kr = hpR.process (shelfR.process (r, shelf), highpass);
        hopEnergy += (double) kl * kl + (double) kr * kr;

        float os[4];
        upL.upsample (l, os);

        for (float v : os)
            peak = std::max (peak, std::abs (v));

        upR.upsample (r, os);

        for (float v : os)
            peak = std::max (peak, std::abs (v));

        corrLR = corrLR * corrCoeff + (double) l * r;
        corrLL = corrLL * corrCoeff + (double) l * l;
        corrRR = corrRR * corrCoeff + (double) r * r;

        if (++hopCount >= hopSamples)
            finishHop();
    }

    peakSinceReset = peak;
    truePeakDb.store (gainToDb (peak, silence), std::memory_order_relaxed);

    const double denom = std::sqrt (corrLL * corrRR);
    correlationValue.store (denom > 1.0e-12 ? (float) std::clamp (corrLR / denom, -1.0, 1.0) : 1.0f,
                            std::memory_order_relaxed);
}

void LoudnessMeter::finishHop() noexcept
{
    hops[(size_t) (hopsWritten % maxHops)] = hopEnergy / hopCount;
    ++hopsWritten;
    hopEnergy = 0.0;
    hopCount = 0;

    auto meanOfLast = [this] (int n)
    {
        n = std::min (n, std::min (hopsWritten, maxHops));
        double sum = 0.0;

        for (int i = 0; i < n; ++i)
            sum += hops[(size_t) ((hopsWritten - 1 - i) % maxHops)];

        return n > 0 ? sum / n : 0.0;
    };

    const double blockEnergy = meanOfLast (4);
    momentaryLufs.store (energyToLufs (blockEnergy), std::memory_order_relaxed);
    shortTermLufs.store (energyToLufs (meanOfLast (maxHops)), std::memory_order_relaxed);

    // Só blocos completos de 400 ms entram no integrado.
    if (hopsWritten < 4)
        return;

    const float blockLufs = energyToLufs (blockEnergy);

    if (blockLufs <= -70.0f)
        return;

    const int bin = std::clamp ((int) ((blockLufs + 70.0f) * 10.0f), 0, numBins - 1);
    binEnergy[(size_t) bin] += blockEnergy;
    binCount[(size_t) bin] += 1.0;

    double sum = 0.0, count = 0.0;

    for (int b = 0; b < numBins; ++b)
    {
        sum += binEnergy[(size_t) b];
        count += binCount[(size_t) b];
    }

    if (count <= 0.0)
        return;

    const double relativeGate = -0.691 + 10.0 * std::log10 (sum / count) - 10.0;
    const int firstBin = std::clamp ((int) std::ceil ((relativeGate + 70.0) * 10.0 - 0.5), 0, numBins);
    sum = count = 0.0;

    for (int b = firstBin; b < numBins; ++b)
    {
        sum += binEnergy[(size_t) b];
        count += binCount[(size_t) b];
    }

    integratedLufs.store (count > 0.0 ? energyToLufs (sum / count) : silence, std::memory_order_relaxed);
}

} // namespace whynot::dsp
