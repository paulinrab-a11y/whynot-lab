#pragma once

// Imagem e master: estéreo (largura e grave mono), limitador com clipper e medidor de loudness.

#include "DelayLine.h"
#include "LoudnessMeter.h"
#include "Oversampler.h"
#include "Processor.h"
#include "SpectrumTap.h"
#include "Svf.h"

#include <array>
#include <vector>

namespace whynot::dsp
{

class StereoProcessor final : public Processor
{
public:
    enum Param { width, monoBass, pan, gain, invertLeft, invertRight, mono, count };
    enum Meter { correlation };

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

    LinkwitzRiley4 midSplit, sideSplit;
    bool splitRunning = false;
    float monoFreq = 0.0f;
    Smoothed widthAmount, outGain, invertL, invertR, balanceL, balanceR, bassMonoMix;
    double corrLR = 0.0, corrLL = 0.0, corrRR = 0.0, corrCoeff = 0.999;
};

/** Mínimo deslizante numa janela de tamanho fixo (fila monotônica, sem alocação no áudio). */
class SlidingMinimum
{
public:
    void prepare (int windowSize);
    void reset() noexcept;
    float push (float value) noexcept;   // devolve o mínimo dos últimos windowSize valores

private:
    std::vector<float> values;
    std::vector<std::int64_t> stamps;
    int head = 0, tail = 0, size = 0, window = 1;
    std::int64_t now = 0;
};

/** Limitador de master com antecipação (lookahead), detecção de pico real 4x e um clipper
    suave antes (sobreamostrado), para ganhar volume sem esmagar. Inclui medidor de loudness. */
class LimiterProcessor final : public Processor
{
public:
    enum Param { gain, ceiling, release, clip, target, count };
    enum Meter { gainReduction, outputPeak };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return latency; }

    LoudnessMeter& loudness() noexcept   { return loudnessMeter; }

private:
    double sampleRate = 48000.0;
    std::array<float, count> values {};
    int lookahead = 96, detectDelay = 0, clipLatency = 0, latency = 0;

    std::array<Oversampler4x, 2> clipOversamplers, detectors;
    std::array<DelayLine, 2> audioDelay;
    SlidingMinimum holdMin;
    std::vector<float> boxRing;
    int boxPos = 0;
    double boxSum = 0.0;
    float envelope = 1.0f, releaseCoeff = 0.0f, clipThreshold = 1.0f;
    Smoothed inGain, ceilingGain, clipAmount;
    LoudnessMeter loudnessMeter;
};

/** Só mede: loudness (momentâneo, curto prazo, integrado), pico real e correlação. */
class MeterProcessor final : public Processor
{
public:
    enum Param { target, count };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override                    { loudnessMeter.reset(); }
    void setParam (int, float) noexcept override      {}
    void process (AudioBlock&) noexcept override;

    LoudnessMeter& loudness() noexcept   { return loudnessMeter; }
    SpectrumTap& spectrum() noexcept     { return tap; }

private:
    LoudnessMeter loudnessMeter;
    SpectrumTap tap;
};

/** Fora do tempo real (export): limita o áudio inteiro a `ceilingDb` de pico real (dBTP), com o
    limitador do master sem ganho e sem clipper, e já devolve tudo alinhado (sem a latência).
    Para antes de codificar MP3: o MP3 cria picos entre amostras, e -1 dBTP deixa folga para eles.
    Áudio que já está abaixo do teto passa praticamente igual (só o filtro de sobreamostragem do
    clipper, abaixo de -70 dB). 1 ou 2 canais; aloca (não é para a thread de áudio).
    Devolve a maior redução aplicada, em dB (0 = não precisou). */
float limitTruePeakOffline (float* const* channels, int numChannels, int numSamples, double sampleRate,
                            float ceilingDb = -1.0f);

} // namespace whynot::dsp
