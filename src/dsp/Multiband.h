#pragma once

// Compressor multibanda de 4 bandas, com divisores Linkwitz-Riley de 4ª ordem.

#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** Multibanda: três divisões (grave | médio-grave | médio-agudo | agudo), cada banda com
    limiar, razão e ganho; ataque e soltura gerais; ouvir uma banda só.

    A divisão é em árvore: primeiro na divisão do meio; o ramo grave passa por um passa-tudo da
    divisão de cima antes de se dividir, e o ramo agudo por um passa-tudo da divisão de baixo.
    Assim as quatro bandas têm a mesma fase e, com tudo em 0 dB, a soma é um passa-tudo:
    o volume fica plano em todas as frequências (o teste mede a diferença máxima).

    Com disparo externo (sidechain), cada banda é disparada pela mesma banda do outro sinal:
    a voz abaixa só os 2 a 5 kHz do beat, por exemplo. */
class MultibandProcessor final : public Processor
{
public:
    static constexpr int numBands = 4;

    enum Param
    {
        x1, x2, x3,
        b1Thr, b1Ratio, b1Gain,
        b2Thr, b2Ratio, b2Gain,
        b3Thr, b3Ratio, b3Gain,
        b4Thr, b4Ratio, b4Gain,
        attack, release, solo, output,
        count
    };

    enum Meter { reduction1, reduction2, reduction3, reduction4 };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;

    /** As três divisões em ordem crescente, afastadas pelo menos 1,25x e abaixo de 45% da taxa. */
    static std::array<float, 3> crossovers (const float* values, double sampleRate) noexcept;

private:
    static constexpr int subBlock = 16;

    struct Splitter
    {
        LinkwitzRiley4 mid, low, high;   // divisões 2, 1 e 3
        Svf apHigh, apLow;               // passa-tudo da divisão 3 (no ramo grave) e da 1 (no agudo)

        void reset() noexcept   { mid.reset(); low.reset(); high.reset(); apHigh.reset(); apLow.reset(); }
    };

    void updateTargets() noexcept;
    void updateFilters() noexcept;
    void split (Splitter&, float x, float* bands, bool compensate) noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true;

    std::array<Smoothed, 3> logFreq;
    SvfCoeffs apLowCoeffs, apHighCoeffs;
    std::array<Splitter, 2> channels;
    Splitter key;

    std::array<float, numBands> grDb {}, meanSquare {}, rmsCoeff {};
    std::array<Smoothed, numBands> bandGain, audible;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    Smoothed outGain;
};

} // namespace whynot::dsp
