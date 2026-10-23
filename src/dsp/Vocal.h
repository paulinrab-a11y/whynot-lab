#pragma once

// Voz: nivelador (sobe e desce o volume sozinho, devagar) e dobrador (cópias que engrossam e abrem a voz).

#include "DelayLine.h"
#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** Nivelador de voz: mede o volume (RMS de ~30 ms, sem o grave abaixo de 100 Hz) e anda o ganho
    devagar até o alvo, dentro de uma faixa. Desce mais rápido do que sobe. Abaixo da sensibilidade
    (silêncio, respiração, ruído) o ganho fica parado: não sobe o chiado entre as frases.
    Sem latência: é para vir antes do compressor, que fica com menos trabalho. */
class LevelerProcessor final : public Processor
{
public:
    enum Param { target, range, speed, sensitivity, output, count };
    enum Meter { gainMeter, levelMeter, activeMeter };

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

    SvfCoeffs hpCoeffs;
    Svf hpL, hpR;
    float rmsCoeff = 0.0f, meanSquare = 0.0f;
    float upCoeff = 0.0f, downCoeff = 0.0f, gainDb = 0.0f;
    float recentMaxDb = -120.0f, maxDecayPerSample = 0.0f;   // pico recente do nível, caindo 20 dB/s
    Smoothed outGain;
};

/** Dobrador: 2 ou 4 cópias da voz com atraso de 5 a 40 ms e desafinação de poucos cents,
    abertas no estéreo. A desafinação vem de uma modulação lenta e irregular do atraso
    (a cópia nunca fica igual à voz, como um segundo take). No modo coro a modulação é regular.
    As cópias passam por um corte de grave: o grave da voz fica no centro. O original não muda
    e não há latência. */
class DoublerProcessor final : public Processor
{
public:
    enum Param { mode, voices, delay, detune, width, hpf, level, count };
    enum Meter { correlation };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    double tailSecondsFor (const float* v) const noexcept override   { return v[delay] * 0.0016 + 0.01; }

    static constexpr int maxVoices = 4;

    /** Amplitude da modulação do atraso (em segundos) que dá `cents` de desvio máximo de tom. */
    static double modulationSeconds (double cents, bool chorus) noexcept;

private:
    static constexpr int controlRate = 16;
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    DelayLine line;
    SvfCoeffs hpCoeffs;
    Svf hpState;
    std::array<double, maxVoices> phaseA {}, phaseB {};
    std::array<float, maxVoices> currentDelay {}, delayStep {}, gainL {}, gainR {};
    int controlCounter = 0, activeVoices = 2;
    Smoothed baseMs, modSeconds, widthAmount, copyLevel, extraVoices;
    double corrLR = 0.0, corrLL = 0.0, corrRR = 0.0;
};

} // namespace whynot::dsp
