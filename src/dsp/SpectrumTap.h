#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace whynot::dsp
{

/** Derivação do sinal para o analisador de espectro da interface.

    A thread de áudio escreve amostras (mono) num anel de atomics; a interface lê as
    últimas N quando quiser. Sem trava e sem corrida de dados: no pior caso a leitura
    mistura dois blocos vizinhos, o que num analisador não se vê. */
class SpectrumTap
{
public:
    static constexpr int size = 8192;

    void push (float x) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        ring[w & (size - 1)].store (x, std::memory_order_relaxed);
        writePos.store (w + 1, std::memory_order_release);
    }

    /** Copia as últimas `count` amostras (count <= size), da mais antiga para a mais nova.
        Devolve quantas amostras já foram escritas desde o começo (para saber se há novidade). */
    std::uint32_t read (float* dest, int count) const noexcept
    {
        const auto w = writePos.load (std::memory_order_acquire);

        for (int i = 0; i < count; ++i)
            dest[i] = ring[(w - (std::uint32_t) count + (std::uint32_t) i) & (size - 1)].load (std::memory_order_relaxed);

        return w;
    }

private:
    std::array<std::atomic<float>, size> ring {};
    std::atomic<std::uint32_t> writePos { 0 };
};

} // namespace whynot::dsp
