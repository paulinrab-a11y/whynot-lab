#pragma once

// Vintage: compressor óptico, compressor FET e EQ vintage (reforço e atenuação no mesmo grave).
// Comportamento estudado só por fora (manuais e textos públicos); nenhum nome comercial na interface.

#include "DelayLine.h"
#include "Oversampler.h"
#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** Compressor óptico: um controle de redução, ganho e modo (comprimir ou limitar).
    Ataque perto de 10 ms e soltura em dois estágios: ~60% da redução sai em ~60 ms e o resto
    devagar, de 0,4 s a 4 s conforme o tempo que o som ficou comprimido (a "memória" da célula).
    Detector RMS curto, com realce opcional dos agudos (comprime mais os "s" e os agudos duros). */
class OptoProcessor final : public Processor
{
public:
    enum Param { reduction, gain, mode, emphasis, mix, count };
    enum Meter { gainReduction, inputLevel, outputLevel, memoryMeter };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;

private:
    static constexpr int subBlock = 32;
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true, useEmphasis = false;

    SvfCoeffs emphasisCoeffs;
    Svf emphL, emphR;
    float rmsCoeff = 0.0f, meanSquare = 0.0f;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f;
    float memCharge = 0.0f, memDischarge = 0.0f;
    float fastGr = 0.0f, slowGr = 0.0f, memory = 0.0f;
    float thresholdDb = -16.0f, ratio = 3.0f, kneeDb = 10.0f;
    Smoothed makeupGain, wet;
};

/** Compressor FET: limiar, razões 4, 8, 12 e 20 e o modo "Todos" (todas as razões ao mesmo tempo:
    joelho duro e redução além da conta, o som esmagado), ataque de 20 µs a 0,8 ms, soltura de
    50 a 1100 ms e uma cor de transistor (harmônicos ímpares, 4x sobreamostrada). Detector de pico. */
class FetProcessor final : public Processor
{
public:
    enum Param { threshold, ratio, attack, release, makeup, color, mix, count };
    enum Meter { gainReduction, inputLevel, outputLevel };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return latency; }

    /** A curva de cor (para a interface desenhar e para o teste): `amount` de 0 a 1. */
    static float colorCurve (float x, float amount) noexcept;

private:
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;
    int latency = 0;

    float attackCoeff = 0.0f, releaseCoeff = 0.0f, smoothedGr = 0.0f;
    float ratioValue = 4.0f, kneeDb = 6.0f, overshoot = 1.0f;
    std::array<Oversampler4x, 2> oversamplers;
    std::array<DelayLine, 2> dryDelay;
    Smoothed makeupGain, wet, colorAmount;
};

/** EQ vintage: grave com reforço e atenuação na mesma frequência escolhida (juntos dão o
    reforço com um vale logo acima, o "truque" do EQ passivo), agudo com reforço e largura,
    e um corte de ar. Os controles vão de 0 a 10, como nos aparelhos. */
class VintageEqProcessor final : public Processor
{
public:
    enum Param { lowFreq, lowBoost, lowAtten, highFreq, highBoost, highWidth, airFreq, highAtten, output, count };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;

    /** Resposta total em dB para um conjunto de valores (na ordem de Param), em `freq`. */
    static double responseDb (const float* values, double freq, double sampleRate);

private:
    static constexpr int numBands = 4, subBlock = 16;

    struct Band
    {
        Smoothed logFreq, gainDb, q;
        SvfCoeffs coeffs;
        Svf left, right;
        bool active = false;
    };

    void updateTargets() noexcept;
    void updateCoefficients() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;
    std::array<Band, numBands> bands;
    Smoothed outGain;
};

} // namespace whynot::dsp
