#pragma once

// Sujeira boa (pedido de 26/10): o clipper, que corta os picos sem bombear, e o lo-fi, que gasta o som de
// propósito (menos bits, menos amostras, fita oscilando e chiado).

#include "DelayLine.h"
#include "Oversampler.h"
#include "Processor.h"
#include "Svf.h"

#include <array>
#include <cstdint>

namespace whynot::dsp
{

/** Clipper: corta tudo o que passa do teto, na hora, sem ataque nem soltura (o compressor e o limitador
    reagem com tempo; o clipper só muda a forma do pico). É o jeito do trap de deixar o kick e o 808 altos
    sem o limitador bombear. A suavidade arredonda a quina a partir de 40% abaixo do teto com uma tangente
    hiperbólica (a curva e a inclinação continuam no ponto onde ela começa), e o corte roda 4x
    sobreamostrado para os harmônicos do corte não voltarem dobrados como chiado. A volta da sobreamostragem
    faz a onda passar um pouco do teto de novo (até ~2 dB num agudo cortado reto); garantir o teto ali traria
    o serrilhado de volta, então o teto exato fica com o limitador depois. Latência do sobreamostrador,
    informada. */
class ClipperProcessor final : public Processor
{
public:
    enum Param { gain, ceiling, softness, output, count };
    enum Meter { clipping };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return latency; }

    /** A curva para um teto `c` (ganho) e suavidade 0..1. Ímpar e nunca passa de c. */
    static float clip (float x, float c, float soft) noexcept;

private:
    static constexpr int subBlock = 32;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    int latency = 0;

    std::array<Oversampler4x, 2> oversamplers;
    Smoothed inGain, ceilingGain, soft, outGain;
    float meterHold = 0.0f, meterRelease = 0.0f;
};

/** Lo-fi: o caminho de uma fita ou de um sampler velho, nesta ordem: a fita oscilando (wow lento mais
    flutter rápido, num atraso modulado), menos amostras por segundo (segura a amostra, sem filtro: o
    serrilhado é o som), menos bits, o filtro do brilho e um chiado que segue o volume do som (no silêncio,
    silêncio). O seco passa pelo mesmo atraso fixo do wow, então a mistura não desalinha; a latência é esse
    atraso, informada. */
class LofiProcessor final : public Processor
{
public:
    enum Param { bits, rate, wow, tone, noise, mix, count };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return baseDelay; }
    /** A fita lê até 4,15 ms além da latência, e o chiado segue o envelope (soltura de 150 ms). */
    double tailSecondsFor (const float* v) const noexcept override   { return 0.012 + (v[noise] > 0.5f ? 0.5 : 0.0); }

private:
    static constexpr int controlRate = 16;
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;
    int baseDelay = 0;

    std::array<DelayLine, 2> lines;
    std::array<Svf, 2> toneFilter;
    SvfCoeffs toneCoeffs;
    double wowPhase = 0.0, flutterPhase = 0.0, holdPhase = 0.0;
    float currentDelay = 0.0f, delayStep = 0.0f, quantStep = 2048.0f;
    int controlCounter = 0;
    std::array<float, 2> held {}, env {};
    Ballistics envBallistics;
    std::uint32_t noiseSeed = 1;
    Smoothed wowAmount, quantSteps, noiseLevel, wet;
};

} // namespace whynot::dsp
