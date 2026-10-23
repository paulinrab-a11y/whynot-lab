#pragma once

// Grave+: harmônicos psicoacústicos para o grave (808, baixo) aparecer em caixa pequena e no celular.

#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** O ouvido "ouve" o fundamental quando os harmônicos dele estão lá (fundamental ausente). O Grave+
    divide o som na frequência escolhida, gera o 2º e o 3º harmônicos só da parte grave e soma acima
    do corte. Os harmônicos são feitos pelos polinômios de Chebyshev sobre o grave normalizado pelo
    envelope (2u² - 1 e 4u³ - 3u): num seno eles dão exatamente o dobro e o triplo da nota, e a
    quantidade não depende do volume (o 808 baixo ganha o mesmo tanto que o alto). Um passa-altas no
    corte e um passa-baixas acima limpam o que sobra. O sub original pode ser abaixado para ganhar
    headroom. Sem latência. */
class BassEnhancerProcessor final : public Processor
{
public:
    enum Param { freq, amount, color, sub, output, count };
    enum Meter { harmonicLevel };

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

    std::array<LinkwitzRiley4, 2> split;
    SvfCoeffs hpCoeffs, lpCoeffs;
    Svf hp1, hp2, lp1;
    float env = 0.0f, envRelease = 0.0f;
    int envHold = 0, envHoldSamples = 1500;
    Smoothed harmonicGain, oddMix, subGain, outGain;
};

} // namespace whynot::dsp
