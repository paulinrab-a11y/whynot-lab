#pragma once

// Gate e expansor: abaixa o que fica abaixo do limiar (ruído, vazamento do fone, respiração).

#include "DelayLine.h"
#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** O coração do gate, sem parâmetros de interface: o Console usa o mesmo.

    - Detector: pico do sinal de disparo (já filtrado por quem chama), com queda de ~10 ms.
    - Gate: abre acima do limiar e só fecha abaixo de limiar - histerese, depois do "segurar".
      Abre num polo (o ataque) e fecha numa queda reta em dB que chega ao piso no tempo da soltura.
    - Expansor: abaixo do limiar, cada dB a menos vira `ratio` dB a menos, até a faixa. */
class GateCore
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        envDecay = onePoleCoeff (10.0f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        gain = 1.0f;
        open = false;
        holdLeft = 0;
    }

    void set (bool expander, float thresholdDb, float rangeDb, float ratio, float attackMs, float holdMs,
              float releaseMs, float hysteresisDb) noexcept
    {
        expand = expander;
        openDb = thresholdDb;
        closeDb = thresholdDb - std::max (0.0f, hysteresisDb);
        floorGain = dbToGain (-std::max (0.0f, rangeDb));
        range = std::max (0.0f, rangeDb);
        slope = std::max (0.0f, ratio - 1.0f);
        attack = onePoleCoeff (attackMs, rate);
        release = onePoleCoeff (releaseMs, rate);
        const double releaseSamples = std::max (1.0, (double) releaseMs * 0.001 * rate);
        closeStep = (float) std::pow (10.0, -(double) range / (20.0 * releaseSamples));
        holdSamples = (int) (std::max (0.0f, holdMs) * 0.001 * rate);
    }

    /** Uma amostra do sinal de disparo (já em valor absoluto). Devolve o ganho a aplicar. */
    float next (float keyAbs) noexcept
    {
        env = std::max (keyAbs, env * envDecay);
        const float levelDb = gainToDb (env);
        float target = 1.0f;

        if (levelDb > openDb)
        {
            open = true;
            holdLeft = holdSamples;
        }
        else if (levelDb < closeDb || expand)
        {
            if (holdLeft > 0)
                --holdLeft;
            else
                open = false;
        }

        if (! open)
        {
            if (expand)
                target = levelDb >= openDb ? 1.0f : dbToGain (std::max (-range, (levelDb - openDb) * slope));
            else
                target = floorGain;
        }

        if (target > gain)
            gain = target + (gain - target) * attack;
        else if (expand)
            gain = target + (gain - target) * release;
        else
            gain = std::max (target, gain * closeStep);

        return gain;
    }

    bool isOpen() const noexcept       { return open; }
    float currentGain() const noexcept { return gain; }

private:
    double rate = 48000.0;
    float envDecay = 0.0f, env = 0.0f, gain = 1.0f;
    float openDb = -45.0f, closeDb = -49.0f, floorGain = 0.03f, range = 30.0f, slope = 1.0f;
    float attack = 0.0f, release = 0.0f, closeStep = 1.0f;
    int holdSamples = 0, holdLeft = 0;
    bool open = false, expand = false;
};

class GateProcessor final : public Processor
{
public:
    enum Param { mode, threshold, range, ratio, attack, hold, release, hysteresis, scHpf, count };
    enum Meter { gainReduction, inputLevel, openState };

    // Sem áudio ainda (a janela aberta com o som desligado), a tela diz "sem sinal", e não "entrada em 0 dB".
    GateProcessor() noexcept   { setMeter (inputLevel, -120.0f); }

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return lookahead; }
    double tailSecondsFor (const float*) const noexcept override   { return 0.005; }

    /** Antecipação fixa: o detector vê o som 1,5 ms antes, e o ataque da palavra não é comido. */
    static constexpr double lookaheadSeconds = 0.0015;

private:
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true, useHp = false;
    int lookahead = 72;

    GateCore core;
    SvfCoeffs hpCoeffs;
    Svf hpL, hpR;
    DelayLine delayL, delayR;
};

} // namespace whynot::dsp
