#pragma once

// Dinâmica: compressor (com sidechain externo), transiente e de-esser.

#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** Curva de ganho do compressor em dB, com joelho suave (forma quadrática no joelho,
    como em Giannoulis, Massberg e Reiss, "Digital Dynamic Range Compressor Design", JAES 2012).
    Devolve a redução (<= 0) para um nível de entrada em dB. */
inline float compressorGainDb (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    const float over = inputDb - thresholdDb;
    const float slope = 1.0f / ratio - 1.0f;

    if (kneeDb > 0.01f && 2.0f * std::abs (over) <= kneeDb)
    {
        const float t = over + kneeDb * 0.5f;
        return slope * t * t / (2.0f * kneeDb);
    }

    return over > 0.0f ? slope * over : 0.0f;
}

class CompressorProcessor final : public Processor
{
public:
    enum Param { threshold, ratio, attack, release, knee, makeup, mix, detector, scHpf, count };
    enum Meter { gainReduction, inputLevel, outputLevel };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;

private:
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    Ballistics ballistics;
    float rmsCoeff = 0.0f;
    SvfCoeffs detectorHp;
    Svf hpL, hpR;
    bool useHp = false;

    float meanSquare = 0.0f;
    float smoothedGainDb = 0.0f;
    Smoothed makeupGain, wet;
};

/** Transiente: realça ou amacia o ataque e a cauda, sem limiar (envelopes diferenciais). */
class TransientProcessor final : public Processor
{
public:
    enum Param { attack, sustain, output, mix, count };
    enum Meter { gainChange };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;

private:
    double sampleRate = 48000.0;
    std::array<float, count> values {};

    Ballistics fastAttack, slowAttack, longRelease, shortRelease;
    float envFastA = 0.0f, envSlowA = 0.0f, envLongR = 0.0f, envShortR = 0.0f;
    Smoothed attackAmount, sustainAmount, outGain, wet, gainDb;
};

/** De-esser: reduz o "s" chiado da voz. Detecta numa faixa e reduz só os agudos (ou tudo). */
class DeEsserProcessor final : public Processor
{
public:
    enum Param { freq, threshold, range, mode, listen, count };
    enum Meter { reduction };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;

private:
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    SvfCoeffs detectCoeffs, splitCoeffs;
    Svf detect, splitL, splitR;
    Ballistics ballistics;
    float env = 0.0f;
    Smoothed gain, wideMix, listenMix;
};

} // namespace whynot::dsp
