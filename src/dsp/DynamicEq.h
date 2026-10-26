#pragma once

// EQ dinâmico (pedido de 26/10): duas bandas de sino que só mexem quando aquela faixa passa do limiar.

#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** Cada banda escuta a própria faixa (um passa-banda na mesma frequência e no mesmo Q do sino) e, quando ela
    passa do limiar, o sino corta (faixa negativa) ou realça (faixa positiva) na razão escolhida, até o limite
    da faixa. Abaixo do limiar o sino fica em 0 dB e o som passa idêntico. É o jeito de tirar o "embolado"
    do 808 só nas notas que embolam, ou o agudo que fura da voz só nas sílabas que furam, sem deixar o resto
    opaco. Com sidechain, o disparo vem de outra trilha (o grave do beat abre espaço só quando a voz canta).
    O ganho do sino anda até o alvo numa rampa de ~3 ms, com os coeficientes recalculados a cada 4 amostras:
    mexer no limiar, na faixa ou desligar a banda tocando não estala. Sem latência. */
class DynamicEqProcessor final : public Processor
{
public:
    enum Param { b1On, b1Freq, b1Q, b1Thr, b1Range, b2On, b2Freq, b2Q, b2Thr, b2Range, ratio, attack, release, output, count };
    enum Meter { band1Gain, band2Gain };

    static constexpr int numBands = 2;
    static constexpr int paramsPerBand = 5;

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;

    /** A mudança de ganho (dB) para um nível `levelDb` na faixa: 0 abaixo do limiar; acima, o excesso
        vezes (1 - 1/razão), com o sinal e o teto da faixa. */
    static float gainChange (float levelDb, float thresholdDb, float rangeDb, float ratio) noexcept;

private:
    static constexpr int subBlock = 16, rampStep = 4;
    void update() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    struct Band
    {
        SvfCoeffs detectCoeffs, bellCoeffs;
        Svf detector;
        std::array<Svf, 2> bell;
        float env = 0.0f, targetDb = 0.0f, appliedDb = 0.0f;
    };

    std::array<Band, numBands> bands;
    Ballistics ballistics;
    float rampCoeff = 0.0f;   // o ganho do sino anda até o alvo com ~3 ms, recalculado a cada 4 amostras
    Smoothed outGain;
};

} // namespace whynot::dsp
