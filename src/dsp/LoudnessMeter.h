#pragma once

#include "Oversampler.h"
#include "Svf.h"

#include <array>
#include <atomic>

namespace whynot::dsp
{

/** Medidor de loudness no padrão ITU-R BS.1770-4 / EBU R128, mais pico real e correlação.

    - Momentâneo: janela de 400 ms. Curto prazo: 3 s. Integrado: blocos de 400 ms a cada 100 ms,
      com porta absoluta em -70 LUFS e relativa em -10 LU (histograma de 0,1 LU, memória fixa).
    - Pico real: 4x sobreamostrado (dBTP), o maior desde o último reset.

    process() na thread de áudio; os valores são atomics lidos pela interface.
    Valor "sem medida" = -100. */
class LoudnessMeter
{
public:
    static constexpr float silence = -100.0f;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Qualquer thread: o próximo process() zera o integrado e o pico. */
    void requestReset() noexcept   { resetRequested.store (true); }

    void process (const float* left, const float* right, int numSamples) noexcept;

    float momentary() const noexcept     { return momentaryLufs.load (std::memory_order_relaxed); }
    float shortTerm() const noexcept     { return shortTermLufs.load (std::memory_order_relaxed); }
    float integrated() const noexcept    { return integratedLufs.load (std::memory_order_relaxed); }
    float truePeak() const noexcept      { return truePeakDb.load (std::memory_order_relaxed); }
    float correlation() const noexcept   { return correlationValue.load (std::memory_order_relaxed); }

private:
    void finishHop() noexcept;

    static constexpr int maxHops = 30;           // 3 s de blocos de 100 ms
    static constexpr int numBins = 800;          // -70 a +10 LUFS em passos de 0,1

    double rate = 48000.0;
    int hopSamples = 4800, hopCount = 0;
    double hopEnergy = 0.0;
    std::array<double, maxHops> hops {};
    int hopsWritten = 0;

    std::array<double, numBins> binEnergy {};
    std::array<double, numBins> binCount {};

    SvfCoeffs shelf, highpass;
    Svf shelfL, shelfR, hpL, hpR;
    Oversampler4x upL, upR;
    float peakSinceReset = 0.0f;

    double corrLR = 0.0, corrLL = 0.0, corrRR = 0.0;
    double corrCoeff = 0.999;

    std::atomic<bool> resetRequested { false };
    std::atomic<float> momentaryLufs { silence }, shortTermLufs { silence }, integratedLufs { silence },
                       truePeakDb { silence }, correlationValue { 1.0f };
};

} // namespace whynot::dsp
