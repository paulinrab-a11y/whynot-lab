#pragma once

#include "DspCommon.h"

#include <array>
#include <vector>

namespace whynot::dsp
{

/** Linha de atraso circular de um canal. Aloca só em prepare(). */
class DelayLine
{
public:
    /** maxDelay em amostras (o tamanho real vira a próxima potência de 2). */
    void prepare (int maxDelay)
    {
        int size = 4;

        while (size < maxDelay + 4)
            size *= 2;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        write = 0;
    }

    void reset() noexcept   { std::fill (buffer.begin(), buffer.end(), 0.0f); write = 0; }

    void push (float x) noexcept
    {
        buffer[(size_t) write] = x;
        write = (write + 1) & mask;
    }

    /** Amostra de `delay` amostras atrás (0 = a última empurrada). */
    float read (int delay) const noexcept
    {
        return buffer[(size_t) ((write - 1 - delay) & mask)];
    }

    /** Leitura fracionária com interpolação cúbica de Hermite (delay >= 1). */
    float readHermite (float delay) const noexcept
    {
        const int i = (int) delay;
        const float f = delay - (float) i;
        const float xm1 = read (i - 1), x0 = read (i), x1 = read (i + 1), x2 = read (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    /** Leitura fracionária linear (barata; boa para modulação pequena). */
    float readLinear (float delay) const noexcept
    {
        const int i = (int) delay;
        const float f = delay - (float) i;
        return read (i) + f * (read (i + 1) - read (i));
    }

    int capacity() const noexcept   { return mask - 3; }
    int size() const noexcept       { return (int) buffer.size(); }

    /** Zera até `budget` amostras a partir de `cursor` (que avança). Devolve quantas zerou. */
    int clearSome (int& cursor, int budget) noexcept
    {
        const int n = std::max (0, std::min (budget, size() - cursor));
        std::fill (buffer.begin() + cursor, buffer.begin() + cursor + n, 0.0f);
        cursor += n;
        return n;
    }

private:
    std::vector<float> buffer;
    int mask = 0, write = 0;
};

/** Zera várias linhas de atraso aos poucos (para não zerar megabytes num bloco de áudio). */
struct IncrementalClear
{
    int line = 0, cursor = 0;

    template <size_t N>
    bool step (const std::array<DelayLine*, N>& all, int budget) noexcept
    {
        while (line < (int) N && budget > 0)
        {
            auto* d = all[(size_t) line];
            budget -= d->clearSome (cursor, budget);

            if (cursor >= d->size())
            {
                ++line;
                cursor = 0;
            }
        }

        if (line < (int) N)
            return false;

        line = 0;
        return true;
    }
};

} // namespace whynot::dsp
