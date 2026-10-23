#pragma once

#include "Processor.h"
#include "SpectrumTap.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** Equalizador paramétrico: passa-altas (12/24/48 dB/oit), prateleira grave, três sinos,
    prateleira aguda, passa-baixas (12/24) e ganho de saída. Ligar e desligar os cortes e
    trocar a inclinação fazem crossfade, sem estalo. Alimenta o analisador de espectro (pós-EQ). */
class EqProcessor final : public Processor
{
public:
    enum Param
    {
        hpOn, hpFreq, hpSlope,
        lowFreq, lowGain,
        b1Freq, b1Gain, b1Q,
        b2Freq, b2Gain, b2Q,
        b3Freq, b3Gain, b3Q,
        highFreq, highGain,
        lpOn, lpFreq, lpSlope,
        output,
        count
    };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;

    SpectrumTap& spectrum() noexcept   { return tap; }

    /** Resposta total em dB para um conjunto de valores (na ordem de Param), em `freq`.
        Usada pela interface para desenhar a curva. */
    static double responseDb (const float* values, double freq, double sampleRate);

private:
    static constexpr int numTone = 5;    // prateleira grave, 3 sinos, prateleira aguda
    static constexpr int subBlock = 16;

    struct ToneBand
    {
        FilterType type = FilterType::bell;
        Smoothed logFreq, gainDb, q;
        SvfCoeffs coeffs;
        Svf left, right;
        bool active = false;
    };

    /** Corte (passa-altas ou passa-baixas): liga e desliga por mistura, sem estalo. */
    struct Cut
    {
        FilterType type = FilterType::highpass;
        Smoothed logFreq, amount;
        SvfCascade filters;
        bool active = false;
    };

    void updateTargets() noexcept;
    void updateCoefficients() noexcept;
    void updateCut (Cut&) noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    std::array<ToneBand, numTone> tone;
    Cut hp, lp;
    Smoothed outGain;
    SpectrumTap tap;
};

} // namespace whynot::dsp
