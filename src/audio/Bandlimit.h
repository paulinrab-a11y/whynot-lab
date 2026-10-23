#pragma once

// Leitura do sample sem aliasing quando ele toca mais agudo que o original (a razão de leitura passa
// de 1: tom acima da raiz, ou um sample de 96 kHz numa saída de 48). Pedido da revisão de 22/10.
//
// - Por oitava, níveis do sample com metade da taxa do anterior, filtrados antes (mipmaps). Feitos uma
//   vez, fora da thread de áudio, quando o sample carrega.
// - Dentro da oitava (razão de 1 a 2 no nível), uma sinc janelada com o corte em 1/razão, por uma
//   tabela feita antes. Na thread de áudio, só leitura da tabela e multiplicações.
//
// Com a razão até 1 a voz continua no Hermite de sempre: o som no tom original fica bit a bit igual.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace whynot::audio
{

/** Os coeficientes da sinc janelada para ler o sample na razão r, de 1 a 2.

    32 pontos (16 de cada lado), janela de Kaiser com beta 7 e o corte em 0,85 do Nyquist da saída
    (0,85 / r do Nyquist da fonte). Escolhidos por medida (scratchpad, experimento.cpp, 22/10): com
    16 pontos, o 18 kHz sete semitons acima ainda dobrava a -12,7 dB; assim, todo alias fica abaixo de
    -73 dB (o que cairia em 15 kHz, a -78 dB), e o que cabe perde no máximo 0,3 dB (em 16 kHz na
    saída). O agudo acima de 0,85 do Nyquist da saída (20,4 kHz a 48 kHz) é o preço.
*/
class BandlimitTable
{
public:
    static constexpr int halfTaps = 16;             // amostras de cada lado da posição
    static constexpr int taps = 2 * halfTaps;
    static constexpr int phases = 128;              // posições entre duas amostras (mais uma, para interpolar); 540 KB
    static constexpr int ratios = 33;               // r = 1, 1 + 1/32, ..., 2
    static constexpr double cutoff = 0.85;          // em fração do Nyquist da saída

    BandlimitTable()
    {
        coef.resize ((size_t) ratios * (phases + 1) * taps);

        for (int ri = 0; ri < ratios; ++ri)
        {
            const double r = 1.0 + (double) ri / (double) (ratios - 1);

            for (int p = 0; p <= phases; ++p)
            {
                const double frac = (double) p / (double) phases;
                float* row = coef.data() + ((size_t) ri * (phases + 1) + (size_t) p) * taps;
                double sum = 0.0;

                for (int k = 0; k < taps; ++k)
                {
                    const double t = (double) (k - (halfTaps - 1)) - frac;   // da posição até a amostra
                    const double x = t * std::min (1.0, cutoff / r);
                    const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
                    const double w = kaiser (t / (double) halfTaps);
                    row[k] = (float) (sinc * w);
                    sum += row[k];
                }

                for (int k = 0; k < taps; ++k)   // ganho 1 no grave: a soma de cada linha é 1
                    row[k] = (float) (row[k] / sum);
            }
        }
    }

    /** Uma amostra do canal na posição `pos` (frames do nível), na razão r (de 1 a 2). */
    float read (const std::vector<float>& src, double pos, double r) const noexcept
    {
        const auto length = (std::int64_t) src.size();
        const auto i0 = (std::int64_t) std::floor (pos);
        const double frac = pos - (double) i0;
        const double fp = frac * phases;
        const int p = std::min (phases - 1, (int) fp);
        const float pf = (float) (fp - (double) p);
        const int ri = std::clamp ((int) std::lround ((r - 1.0) * (ratios - 1)), 0, ratios - 1);
        const float* row0 = coef.data() + ((size_t) ri * (phases + 1) + (size_t) p) * taps;
        const float* row1 = row0 + taps;
        const auto first = i0 - (halfTaps - 1);
        float sum = 0.0f;

        if (first >= 0 && first + taps <= length)   // o caso comum: sem conferir as pontas
        {
            const float* x = src.data() + first;

            for (int k = 0; k < taps; ++k)
                sum += x[k] * (row0[k] + (row1[k] - row0[k]) * pf);
        }
        else
        {
            for (int k = 0; k < taps; ++k)
            {
                const auto i = std::clamp (first + k, (std::int64_t) 0, length - 1);
                sum += src[(size_t) i] * (row0[k] + (row1[k] - row0[k]) * pf);
            }
        }

        return sum;
    }

private:
    static constexpr double pi = 3.14159265358979323846;

    /** Janela de Kaiser em x de -1 a 1 (fora, zero). */
    static double kaiser (double x)
    {
        if (std::abs (x) >= 1.0)
            return 0.0;

        constexpr double beta = 7.0;
        return besselI0 (beta * std::sqrt (1.0 - x * x)) / besselI0 (beta);
    }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;

            if (term < sum * 1.0e-12)
                break;
        }

        return sum;
    }

    std::vector<float> coef;
};

/** A tabela única do app. A primeira chamada faz a tabela (uns 136 mil senos): chame uma vez fora da
    thread de áudio (o sampler chama no construtor) antes de a voz precisar dela. */
inline const BandlimitTable& bandlimitTable()
{
    static const BandlimitTable table;
    return table;
}

/** Metade da taxa, com um passa-baixas antes (sinc janelada de 63 pontos, corte em 0,225 da taxa de
    entrada: o nível novo guarda até 0,45 do Nyquist dele). Centrada: a amostra n do nível novo fica na
    posição 2n do anterior, sem atraso. */
inline std::vector<float> halfRate (const std::vector<float>& x)
{
    constexpr int half = 31;
    static const std::vector<float> h = []
    {
        std::vector<float> c ((size_t) (2 * half + 1));
        constexpr double pi = 3.14159265358979323846, fc = 0.225, beta = 8.0;
        auto i0 = [] (double v)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k) { term *= (v / (2.0 * k)) * (v / (2.0 * k)); sum += term; }
            return sum;
        };
        double total = 0.0;

        for (int m = -half; m <= half; ++m)
        {
            const double t = 2.0 * fc * m;
            const double sinc = m == 0 ? 1.0 : std::sin (pi * t) / (pi * t);
            const double r = (double) m / (double) half;
            const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
            c[(size_t) (m + half)] = (float) (sinc * w);
            total += c[(size_t) (m + half)];
        }

        for (auto& v : c)
            v = (float) (v / total);

        return c;
    }();

    const auto n = (std::int64_t) x.size();
    std::vector<float> y ((size_t) ((n + 1) / 2));

    for (std::int64_t o = 0; o < (std::int64_t) y.size(); ++o)
    {
        double sum = 0.0;

        for (int m = -half; m <= half; ++m)
        {
            const auto i = std::clamp (2 * o - m, (std::int64_t) 0, n - 1);
            sum += (double) x[(size_t) i] * h[(size_t) (m + half)];
        }

        y[(size_t) o] = (float) sum;
    }

    return y;
}

} // namespace whynot::audio
