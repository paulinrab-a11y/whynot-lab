#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace whynot::ideas
{

/** PCG32: a mesma sequência em qualquer compilador. As distribuições da biblioteca padrão
    não garantem isso, e a mesma semente precisa dar a mesma ideia no Windows e nos testes. */
class Random
{
public:
    explicit Random (uint64_t seed, uint64_t stream = 54) noexcept
    {
        inc = (stream << 1u) | 1u;
        next();
        state += seed;
        next();
    }

    uint32_t next() noexcept
    {
        const uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const auto xorshifted = (uint32_t) (((old >> 18u) ^ old) >> 27u);
        const auto rot = (uint32_t) (old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    /** De 0 (incluso) a 1 (excluso). */
    double unit() noexcept                 { return (next() >> 8) * (1.0 / 16777216.0); }

    /** De lo a hi, os dois inclusos. */
    int between (int lo, int hi) noexcept
    {
        if (hi <= lo)
            return lo;

        return lo + (int) (next() % (uint32_t) (hi - lo + 1));
    }

    bool chance (double p) noexcept        { return unit() < p; }

    /** Índice sorteado pelos pesos; peso <= 0 nunca sai; tudo zero devolve 0. */
    size_t weighted (const std::vector<double>& weights) noexcept
    {
        double total = 0.0;

        for (double w : weights)
            if (w > 0.0)
                total += w;

        if (total <= 0.0)
            return 0;

        double r = unit() * total;
        size_t last = 0;

        for (size_t i = 0; i < weights.size(); ++i)
        {
            if (weights[i] <= 0.0)
                continue;

            if (r < weights[i])
                return i;

            r -= weights[i];
            last = i;
        }

        return last;
    }

private:
    uint64_t state = 0, inc = 0;
};

/** Deriva uma semente independente (progressão, batida, cada parte...) de outra. */
inline uint64_t mixSeed (uint64_t a, uint64_t b) noexcept
{
    uint64_t z = a + 0x9E3779B97F4A7C15ULL * (b + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

} // namespace whynot::ideas
