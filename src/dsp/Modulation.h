#pragma once

// Modulação (pedido de 26/10): chorus, flanger e phaser. Os três vivem de um LFO lento mexendo num atraso
// curto (chorus e flanger) ou numa fila de filtros passa-tudo (phaser), e a mistura com o seco faz o resto.

#include "DelayLine.h"
#include "Processor.h"

#include <array>

namespace whynot::dsp
{

/** Chorus: duas ou quatro cópias com um atraso de 7 a 30 ms que ondula devagar, abertas no estéreo. O som
    engorda e brilha sem virar eco. Cada lado tem o próprio LFO, defasado, para a largura não depender da
    fonte ser estéreo. Sem latência: o seco passa direto. */
class ChorusProcessor final : public Processor
{
public:
    enum Param { rate, depth, delay, voices, width, tone, mix, count };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    double tailSecondsFor (const float* v) const noexcept override   { return v[delay] * 0.001 + 0.02; }

    static constexpr int maxVoices = 4;

private:
    static constexpr int controlRate = 16;
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    std::array<DelayLine, 2> lines;
    std::array<double, maxVoices> phase {};
    std::array<float, maxVoices> currentDelay {}, delayStep {}, gainL {}, gainR {};
    int controlCounter = 0;
    float lpCoeff = 0.0f;
    std::array<float, 2> lpState {};
    Smoothed baseMs, depthMs, widthAmount, wet, extraVoices;
};

/** Flanger: uma cópia com atraso de 0,1 a 10 ms que sobe e desce; somada ao seco, ela cava uma fila de
    vales no espectro que andam juntos (o "avião passando"). A realimentação devolve parte da saída do
    atraso para a entrada e deixa os vales mais fundos e metálicos (negativa: mais oca). Fica abaixo de 95%,
    então nunca dispara, e a cópia é abaixada por raiz(1 - |realimentação|) para os picos não estourarem.
    Sem latência. */
class FlangerProcessor final : public Processor
{
public:
    enum Param { rate, depth, delay, feedback, stereo, mix, count };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    double tailSecondsFor (const float* v) const noexcept override;

private:
    static constexpr int controlRate = 16;
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    std::array<DelayLine, 2> lines;
    double lfoPhase = 0.0;
    std::array<float, 2> currentDelay {}, delayStep {}, lastTap {};
    int controlCounter = 0;
    Smoothed minMs, sweepMs, fb, phaseOffset, wet;
};

/** Phaser: 4 a 12 filtros passa-tudo de 1ª ordem em série. Cada um atrasa a fase sem mudar o volume;
    somado ao seco, o resultado tem vales onde a fase chega a 180°, e o LFO passeia com eles pelo
    espectro. Os coeficientes são recalculados a cada 8 amostras, com a frequência em escala de oitavas.
    A realimentação (abaixo de 90%) dá picos entre os vales; a cópia é abaixada por raiz(1 - |realimentação|)
    para os picos não estourarem. Os 12 estágios rodam sempre: trocar a contagem é um crossfade. Sem latência. */
class PhaserProcessor final : public Processor
{
public:
    enum Param { stages, rate, depth, centre, feedback, stereo, mix, count };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    double tailSecondsFor (const float* v) const noexcept override;

    static constexpr int maxStages = 12;

    /** Coeficiente do passa-tudo de 1ª ordem com a fase em -90° na frequência `freq`. */
    static float allpassCoeff (double freq, double sampleRate) noexcept;

private:
    static constexpr int controlRate = 8;
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    int activeStages = 6, previousStages = 6;   // trocar a contagem é um crossfade de 15 ms entre as duas saídas
    double lfoPhase = 0.0;
    std::array<std::array<float, maxStages>, 2> state {};
    std::array<float, 2> coeff {}, coeffStep {}, lastOut {};
    int controlCounter = 0;
    Smoothed logCentre, octaves, fb, phaseOffset, wet, stageFade;
};

} // namespace whynot::dsp
