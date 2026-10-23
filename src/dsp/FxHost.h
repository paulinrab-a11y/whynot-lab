#pragma once

#include "DelayLine.h"
#include "Processor.h"

#include <memory>
#include <vector>

namespace whynot::dsp
{

/** Hospeda um efeito: parâmetros entre threads, bypass sem estalo e blocos de qualquer tamanho.

    - Qualquer thread: setParam(), setBypassed() (atomics; valem a partir do próximo bloco),
      tailSeconds() (calcula pelos atomics, sem tocar no estado do efeito).
    - Com o áudio parado: prepare(). Os valores atuais já entram sem rampa (o render começa certo).
    - Thread de áudio: process().

    No bypass o sinal seco sai atrasado pela mesma latência do efeito, para a compensação
    de atraso (PDC) do motor continuar valendo. A troca leva ~20 ms, sem estalo; depois de
    totalmente em bypass, o efeito para de processar e é zerado aos poucos.
    Amostras NaN ou infinitas (de um plugin de terceiros antes, por exemplo) viram silêncio
    em vez de travar filtros e realimentações. */
class FxHost
{
public:
    explicit FxHost (std::unique_ptr<Processor>);

    const FxInfo& info() const noexcept       { return proc->info(); }
    Processor& processor() noexcept           { return *proc; }
    const Processor& processor() const noexcept { return *proc; }

    void setParam (int index, float value) noexcept;
    float getParam (int index) const noexcept;
    void setBypassed (bool b) noexcept         { bypassed.store (b, std::memory_order_relaxed); }
    bool isBypassed() const noexcept           { return bypassed.load (std::memory_order_relaxed); }

    void prepare (double sampleRate, int maxBlockSize);
    bool isPrepared() const noexcept           { return prepared; }

    void process (float* left, float* right, int numSamples,
                  const float* sideLeft = nullptr, const float* sideRight = nullptr) noexcept;

    int latencySamples() const noexcept        { return proc->latencySamples(); }
    double tailSeconds() const noexcept;

private:
    static constexpr int maxParams = 64;

    void processChunk (float* left, float* right, int n, const float* sideLeft, const float* sideRight) noexcept;

    std::unique_ptr<Processor> proc;
    int numParams = 0;
    std::unique_ptr<std::atomic<float>[]> values;
    std::vector<float> applied;
    std::atomic<bool> bypassed { false };

    Smoothed dryAmount;          // 0 = efeito, 1 = seco
    bool fullyBypassed = false, cleared = true;
    DelayLine dryDelay[2];
    std::vector<float> dry[2];
    int maxBlock = 0, latency = 0;
    bool prepared = false;
};

} // namespace whynot::dsp
