#pragma once

// Console: a trilha de uma mesa de som num efeito só (corte grave, gate, compressor, EQ e cor).

#include "Gate.h"
#include "Oversampler.h"
#include "Processor.h"
#include "Svf.h"

#include <array>

namespace whynot::dsp
{

/** Ordem do sinal: corte grave (24 dB/oit) → gate → compressor → EQ de 3 bandas (grave em 100 Hz,
    médio com frequência, agudo em 10 kHz) → cor (saturação de válvula 4x sobreamostrada) → saída.
    Cada parte desligada (limiar no fim da escala, ganho 0, cor 0) não mexe no som.
    Latência fixa: a da sobreamostragem (38 amostras), para não mudar quando a cor liga. */
class ConsoleProcessor final : public Processor
{
public:
    enum Param { hpf, gate, comp, ratio, fast, low, mid, midFreq, high, drive, output, count };
    enum Meter { compReduction, gateReduction, gateOpen };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return latency; }

    /** Resposta do EQ (corte grave e as 3 bandas) em dB, para a interface desenhar. */
    static double responseDb (const float* values, double freq, double sampleRate);

private:
    static constexpr int subBlock = 16;
    void update() noexcept;
    void updateEq() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    bool dirty = true, useHp = false;
    int latency = 0;

    std::array<SvfCoeffs, 2> hpCoeffs;
    std::array<Svf, 2> hpL, hpR;
    GateCore gateCore;
    bool gateOn = false;
    float compAttack = 0.0f, compRelease = 0.0f, compGr = 0.0f, rmsCoeff = 0.0f, meanSquare = 0.0f;

    struct Band
    {
        FilterType type = FilterType::bell;
        Smoothed gainDb, logFreq;
        SvfCoeffs coeffs;
        Svf left, right;
        bool active = false;
    };

    std::array<Band, 3> eq;
    std::array<Oversampler4x, 2> oversamplers;
    std::array<DcBlocker, 2> colorDc;   // na taxa 4x
    Smoothed driveAmount, outGain;
};

} // namespace whynot::dsp
