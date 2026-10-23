#pragma once

// Espaço: reverb (rede de atraso com realimentação) e delay sincronizado ao andamento.

#include "DelayLine.h"
#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** Reverb algorítmico: difusores em série na entrada e uma FDN de 8 linhas com matriz
    de Hadamard, abafamento por linha e modulação lenta (evita o som metálico). */
class ReverbProcessor final : public Processor
{
public:
    enum Param { mode, size, decay, predelay, damping, lowCut, width, mix, count };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    bool clearStep (int budget) noexcept override;
    void process (AudioBlock&) noexcept override;
    double tailSecondsFor (const float* v) const noexcept override   { return v[decay] * 1.2 + v[predelay] * 0.001 + 0.1; }

private:
    static constexpr int numLines = 8, numDiffusers = 4, subBlock = 32;

    struct Allpass
    {
        DelayLine line;
        int delay = 1;
        float gain = 0.6f;

        float process (float x) noexcept
        {
            const float delayed = line.read (delay - 1);
            const float w = x + gain * delayed;
            line.push (w);
            return delayed - gain * w;
        }
    };

    void configureMode() noexcept;
    void updateLoop() noexcept;
    void resetSmallState() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    int currentMode = -1;

    std::array<DelayLine, numLines> lines;
    std::array<float, numLines> baseDelayMs {};
    std::array<float, numLines> delaySamples {}, loopGain {}, dampState {};
    std::array<float, numLines> lfoCos {}, lfoSin {}, lfoStepCos {}, lfoStepSin {};
    float modDepth = 0.0f, damp = 0.0f;

    std::array<Allpass, numDiffusers> diffuseL, diffuseR;
    DelayLine preL, preR;
    SvfCoeffs lowCutCoeffs;
    Svf lowCutL, lowCutR;

    Smoothed sizeFactor, predelaySamples, wet, widthAmount;
    Smoothed modeGain;            // troca de tipo: o molhado some, troca e volta (sem estalo)
    IncrementalClear clearer;
};

class DelayProcessor final : public Processor
{
public:
    enum Param { sync, division, time, feedback, mode, hpf, lpf, ducking, width, mix, bpm, count };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    bool clearStep (int budget) noexcept override;
    void process (AudioBlock&) noexcept override;
    double tailSecondsFor (const float* values) const noexcept override;

    /** Tempo de cada repetição em ms para um conjunto de valores (a interface mostra). */
    static double delayMs (const float* values) noexcept;

private:
    void update() noexcept;

    static constexpr float maxSeconds = 4.0f;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    std::array<DelayLine, 2> lines;
    std::array<Svf, 2> hpState, lpState;
    SvfCoeffs hpCoeffs, lpCoeffs;
    Smoothed delaySamples, fb, wet, widthAmount, monoMix, duckAmount;
    Ballistics duckBallistics;
    float duckEnv = 0.0f;
    IncrementalClear clearer;
};

} // namespace whynot::dsp
