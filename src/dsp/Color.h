#pragma once

// Cor: saturador (4x sobreamostrado) e filtro.

#include "DelayLine.h"
#include "Oversampler.h"
#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

enum class SaturationMode { tape, tube, soft, hard, fuzz };

/** Curva de saturação para uma entrada já amplificada pelo drive (ganho ~1 em sinal baixo). */
float saturate (SaturationMode, float x) noexcept;

class SaturatorProcessor final : public Processor
{
public:
    enum Param { mode, drive, tone, protect, mix, autoGain, output, count };
    enum Meter { compensation };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return latency; }

private:
    static constexpr int subBlock = 32;

    void update() noexcept;
    void updateTilt() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;
    int latency = 0;

    std::array<Oversampler4x, 2> oversamplers;
    std::array<DelayLine, 2> cleanDelay;          // para a faixa grave protegida
    std::array<LinkwitzRiley4, 2> cleanSplit, dirtySplit;
    std::array<Svf, 2> lowTilt, highTilt;
    std::array<DcBlocker, 2> dcBlock;
    SvfCoeffs lowTiltCoeffs, highTiltCoeffs;
    bool protectRunning = false, dcActive = false;
    float protectFreq = 0.0f;

    SaturationMode currentMode = SaturationMode::tape, previousMode = SaturationMode::tape;
    Smoothed modeFade, protectMix, toneAmount;
    Smoothed driveGain, wet, outGain, autoCompensation;
    float inMeanSquare = 0.0f, outMeanSquare = 0.0f, rmsCoeff = 0.0f;
};

class FilterProcessor final : public Processor
{
public:
    enum Param { mode, cutoff, resonance, slope, drive, mix, count };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;

private:
    static constexpr int subBlock = 8;

    void updateCoefficients() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    Smoothed logCutoff, q, driveGain, driveAmount, wet, typeFade;
    FilterType currentType = FilterType::lowpass, previousType = FilterType::lowpass;
    SvfCascade filters;
};

} // namespace whynot::dsp
