#include "Wavetables.h"
#include "SynthParams.h"

#include <cmath>
#include <complex>
#include <functional>
#include <mutex>

namespace whynot::synth
{

namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;

    const char* const tableNames[] = { "Clássica", "Pulso", "Sync", "Vogais", "Voz escura", "Órgão", "Dobra", "Saturada",
                                       "Vidro", "Sino", "Varredura", "Fase", "Ar", "Digital", "Pente" };

    using Complex = std::complex<double>;

    /** FFT radix-2 iterativa, no lugar. inverse = sem normalizar (soma direta). */
    void fft (std::vector<Complex>& x, bool inverse)
    {
        const size_t n = x.size();

        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;

            for (; j & bit; bit >>= 1)
                j ^= bit;

            j ^= bit;

            if (i < j)
                std::swap (x[i], x[j]);
        }

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double angle = (inverse ? twoPi : -twoPi) / (double) len;
            const Complex wlen (std::cos (angle), std::sin (angle));

            for (size_t i = 0; i < n; i += len)
            {
                Complex w (1.0, 0.0);

                for (size_t k = 0; k < len / 2; ++k)
                {
                    const Complex u = x[i + k], v = x[i + k + len / 2] * w;
                    x[i + k] = u + v;
                    x[i + k + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    //==========================================================================
    // Formas básicas no tempo, t em [0, 1). Todas sobem cruzando o zero perto de t = 0.

    double sine (double t)       { return std::sin (twoPi * t); }
    double triangle (double t)   { return t < 0.25 ? 4.0 * t : (t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0); }
    double saw (double t)        { double u = t + 0.5; u -= std::floor (u); return 2.0 * u - 1.0; }
    double pulse (double t, double w)   { return t < w ? 1.0 : -1.0; }

    double lerp (double a, double b, double f)   { return a + (b - a) * f; }

    /** Passa por uma sequência de formas conforme x (0..1). */
    template <typename... Shapes>
    double morph (double x, double t, Shapes... shapes)
    {
        const std::function<double (double)> list[] = { shapes... };
        const int n = (int) sizeof...(Shapes);
        const double pos = std::clamp (x, 0.0, 1.0) * (n - 1);
        const int i = std::min (n - 2, (int) pos);
        return lerp (list[i] (t), list[i + 1] (t), pos - i);
    }

    double fold (double v)
    {
        // Reflete nos limites -1 e +1 (dobra de onda).
        while (v > 1.0 || v < -1.0)
            v = v > 1.0 ? 2.0 - v : -2.0 - v;

        return v;
    }

    //==========================================================================
    // Espectros: amplitude e fase (em ciclos) de cada harmônico n >= 1.

    struct Formant { double freq, width, gain; };

    /** Envelope de formantes em `hz` (soma de ressonâncias em escala log). */
    double formantGain (double hz, const Formant* f, int count)
    {
        double g = 0.02;

        for (int i = 0; i < count; ++i)
        {
            const double d = std::log2 (hz / f[i].freq) / f[i].width;
            g += f[i].gain * std::exp (-0.5 * d * d);
        }

        return g;
    }

    /** Vogal interpolada (F1, F2, F3) numa sequência, pela posição x. */
    void vowelAt (double x, const double (*vowels)[3], int count, double out[3])
    {
        const double pos = std::clamp (x, 0.0, 1.0) * (count - 1);
        const int i = std::min (count - 2, (int) pos);

        for (int k = 0; k < 3; ++k)
            out[k] = std::exp (lerp (std::log (vowels[i][k]), std::log (vowels[i + 1][k]), pos - i));
    }

    struct Generator
    {
        std::function<double (double x, double t)> time;                     // forma no tempo, ou
        std::function<void (int n, double x, double& amp, double& phase)> spectrum;   // espectro direto
    };

    Generator generator (int table)
    {
        switch (table)
        {
            case 0:   // Clássica: seno, triângulo, serra, quadrada, pulsos
                return { [] (double x, double t)
                         {
                             return morph (x, t, sine, triangle, saw, [] (double u) { return pulse (u, 0.5); },
                                           [] (double u) { return pulse (u, 0.3); }, [] (double u) { return pulse (u, 0.15); });
                         }, {} };

            case 1:   // Pulso: de quadrada a pulso bem fino
                return { [] (double x, double t) { return pulse (t, 0.5 - 0.46 * x); }, {} };

            case 2:   // Sync: serra escrava sincronizada, de 1 a 8 vezes a nota
                return { [] (double x, double t)
                         {
                             const double r = 1.0 + 7.0 * x * x;
                             double u = r * t;
                             u -= std::floor (u);
                             return (2.0 * u - 1.0) * (0.6 + 0.4 * (1.0 - t));
                         }, {} };

            case 3:   // Vogais: a - é - i - ó - u sobre um espectro de serra
            case 4:   // Voz escura: u - ó - a, formantes mais baixos e mais largos
                return { {}, [table] (int n, double x, double& amp, double& phase)
                         {
                             static const double bright[][3] = { { 800, 1200, 2600 }, { 550, 1850, 2550 }, { 300, 2250, 3000 },
                                                                 { 500, 900, 2500 }, { 320, 750, 2400 } };
                             static const double dark[][3] = { { 300, 650, 2300 }, { 420, 800, 2350 }, { 650, 1080, 2450 } };
                             double f[3];

                             if (table == 3) vowelAt (x, bright, 5, f);
                             else            vowelAt (x, dark, 3, f);

                             const double w = table == 3 ? 0.18 : 0.26;
                             const Formant formants[] = { { f[0], w, 1.0 }, { f[1], w, 0.6 }, { f[2], w, 0.3 } };
                             const double hz = 110.0 * n;   // referência: as vogais "falam" melhor perto de Lá2-Lá4
                             amp = formantGain (hz, formants, 3) / std::pow ((double) n, table == 3 ? 0.6 : 0.9);
                             phase = 0.0;
                         } };

            case 5:   // Órgão: registros de tubos (8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1')
                return { {}, [] (int n, double x, double& amp, double& phase)
                         {
                             static const double regs[8][7] = {
                                 { 1, 0, 0, 0, 0, 0, 0 },          { 1, 0.7, 0, 0.3, 0, 0, 0 },
                                 { 1, 0.8, 0.6, 0.5, 0, 0, 0 },    { 1, 0, 0.8, 0, 0.6, 0, 0 },
                                 { 0.8, 1, 0.7, 0.7, 0.4, 0.3, 0.2 }, { 1, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6 },
                                 { 0.6, 1, 0.3, 1, 0.2, 0.5, 0.8 }, { 1, 1, 1, 1, 1, 1, 1 } };
                             static const int harmonics[7] = { 1, 2, 3, 4, 5, 6, 8 };
                             const int frame = std::clamp ((int) std::lround (x * 7.0), 0, 7);
                             amp = 0.0;
                             phase = 0.0;

                             for (int k = 0; k < 7; ++k)
                                 if (harmonics[k] == n)
                                     amp = regs[frame][k];
                         } };

            case 6:   // Dobra: seno cada vez mais dobrado
                return { [] (double x, double t) { return fold ((1.0 + 5.0 * x) * sine (t)); }, {} };

            case 7:   // Saturada: seno saturado, com assimetria nos últimos quadros (harmônicos pares)
                return { [] (double x, double t)
                         {
                             const double g = 1.0 + 11.0 * x * x;
                             const double bias = x > 0.5 ? (x - 0.5) * 0.8 : 0.0;
                             return std::tanh (g * sine (t) + bias) - std::tanh (bias);
                         }, {} };

            case 8:   // Vidro: harmônicos ímpares com um realce que sobe
                return { {}, [] (int n, double x, double& amp, double& phase)
                         {
                             const double centre = 3.0 + 22.0 * x, sigma = 2.5 + 3.0 * x;
                             const double bump = std::exp (-0.5 * std::pow ((n - centre) / sigma, 2.0));
                             amp = (n % 2 == 1 ? 1.0 : 0.15) * (0.25 / std::pow ((double) n, 0.7) + bump) + (n == 1 ? 0.6 : 0.0);
                             phase = 0.0;
                         } };

            case 9:   // Sino: poucos harmônicos espaçados, o peso passa para os agudos
                return { {}, [] (int n, double x, double& amp, double& phase)
                         {
                             static const int partials[] = { 1, 3, 5, 9, 13, 18, 24, 31 };
                             static const double weights[] = { 1.0, 0.45, 0.6, 0.5, 0.35, 0.25, 0.18, 0.12 };
                             amp = 0.0;
                             phase = 0.0;

                             for (int k = 0; k < 8; ++k)
                                 if (partials[k] == n)
                                     amp = weights[k] * (k == 0 ? 1.0 : std::pow (0.35 + 1.6 * x, std::min (k, 3) * 0.5));
                         } };

            case 10:  // Varredura: uma faixa de harmônicos que passeia de 1 a 48
                return { {}, [] (int n, double x, double& amp, double& phase)
                         {
                             const double centre = std::pow (48.0, x);
                             const double d = std::log2 (n / centre) / 0.35;
                             amp = (n == 1 ? 0.35 : 0.0) + std::exp (-0.5 * d * d);
                             phase = 0.0;
                         } };

            case 11:  // Fase: distorção de fase (cosseno entortado) e depois ressonância janelada
                return { [] (double x, double t)
                         {
                             if (x <= 0.5)
                             {
                                 const double d = 0.5 - 0.47 * (x / 0.5);
                                 const double warped = t < d ? 0.5 * t / d : 0.5 + 0.5 * (t - d) / (1.0 - d);
                                 return -std::cos (twoPi * warped);
                             }

                             const double r = 1.0 + 7.0 * (x - 0.5) / 0.5;
                             return (1.0 - t) * std::cos (twoPi * r * t) - 0.5 * (1.0 - t);
                         }, {} };

            case 12:  // Ar: espectro macio com fases espalhadas; os pares somem aos poucos (oco)
                return { {}, [] (int n, double x, double& amp, double& phase)
                         {
                             Random rng ((std::uint32_t) (n * 7919 + 17));
                             amp = std::pow ((double) n, -1.3) * (n % 2 == 0 ? 1.0 - 0.85 * x : 1.0);
                             phase = rng.uniform();
                         } };

            case 13:  // Digital: seno e serra quantizados em cada vez menos degraus
                return { [] (double x, double t)
                         {
                             const double steps = std::round (std::pow (2.0, 5.0 - 4.0 * x));
                             const double v = lerp (sine (t), saw (t), std::min (1.0, x * 1.5));
                             return std::round (v * steps) / steps;
                         }, {} };

            default:  // Pente: serra com entalhes que andam (som de phaser parado)
                return { {}, [] (int n, double x, double& amp, double& phase)
                         {
                             const double d = 0.02 + 0.23 * x;
                             amp = std::abs (std::cos (3.14159265358979 * n * d)) / n;
                             phase = 0.0;
                         } };
        }
    }

    static_assert (std::size (tableNames) == 15);
}

std::span<const char* const> Wavetables::names()
{
    return { tableNames, std::size (tableNames) };
}

const Wavetables& Wavetables::get()
{
    static const Wavetables tables;
    return tables;
}

Wavetables::Wavetables()
{
    constexpr int oversampled = tableSize * 8;   // a forma no tempo é amostrada 8x mais fina: quase sem dobra
    const int tables = numTables();
    data.assign ((size_t) tables * framesPerTable * numLevels * stride, 0.0f);

    std::vector<Complex> big ((size_t) oversampled), small ((size_t) tableSize);
    std::vector<Complex> harmonics ((size_t) maxHarmonics + 1);

    for (int table = 0; table < tables; ++table)
    {
        const auto gen = generator (table);

        for (int frameIndex = 0; frameIndex < framesPerTable; ++frameIndex)
        {
            const double x = frameIndex / (double) (framesPerTable - 1);

            // Espectro do quadro: c_n tal que s(t) = Re (soma c_n e^(i 2 pi n t)).
            if (gen.time)
            {
                for (int i = 0; i < oversampled; ++i)
                    big[(size_t) i] = Complex (gen.time (x, i / (double) oversampled), 0.0);

                fft (big, false);

                for (int n = 1; n <= maxHarmonics; ++n)
                    harmonics[(size_t) n] = big[(size_t) n] * (2.0 / oversampled);
            }
            else
            {
                for (int n = 1; n <= maxHarmonics; ++n)
                {
                    double amp = 0.0, phase = 0.0;
                    gen.spectrum (n, x, amp, phase);
                    // seno na fase pedida: sin (2 pi (n t + phase)) = Re (e^(i 2 pi (n t + phase - 1/4)))
                    harmonics[(size_t) n] = std::polar (amp, twoPi * (phase - 0.25));
                }
            }

            renderLevels (harmonics, data.data() + ((size_t) ((table * framesPerTable + frameIndex) * numLevels)) * stride, true, small);
        }
    }

    // Ondas analógicas: séries de Fourier exatas, sem normalizar.
    basicData.assign ((size_t) numBasic * numLevels * stride, 0.0f);
    constexpr double piD = 3.14159265358979323846;

    // serra 2t - 1 = -(2/pi) soma sin (2 pi n t) / n
    for (int n = 1; n <= maxHarmonics; ++n)
        harmonics[(size_t) n] = std::polar (2.0 / (piD * n), twoPi * (0.5 - 0.25));

    renderLevels (harmonics, basicData.data() + (size_t) (basicSaw * numLevels) * stride, false, small);

    // triângulo 1 - 4|t - 0,5| = -(8/pi^2) soma (n ímpar) cos (2 pi n t) / n^2
    for (int n = 1; n <= maxHarmonics; ++n)
        harmonics[(size_t) n] = n % 2 == 1 ? std::polar (8.0 / (piD * piD * n * n), twoPi * (0.75 - 0.25)) : Complex();

    renderLevels (harmonics, basicData.data() + (size_t) (basicTriangle * numLevels) * stride, false, small);
}

void Wavetables::renderLevels (const std::vector<std::complex<double>>& harmonics, float* first, bool normalise,
                               std::vector<std::complex<double>>& scratch)
{
    float gain = 1.0f;

    for (int level = 0; level < numLevels; ++level)
    {
        const int limit = maxHarmonics >> level;
        std::fill (scratch.begin(), scratch.end(), Complex());

        for (int n = 1; n <= limit; ++n)
            scratch[(size_t) n] = harmonics[(size_t) n];

        fft (scratch, true);
        float* out = first + (size_t) level * stride;

        for (int i = 0; i < tableSize; ++i)
            out[i] = (float) scratch[(size_t) i].real();

        // O quadro inteiro (nível 0) define o ganho: pico em 1, igual em todos os níveis.
        if (normalise && level == 0)
        {
            float peak = 0.0f;

            for (int i = 0; i < tableSize; ++i)
                peak = std::max (peak, std::abs (out[i]));

            gain = peak > 1.0e-6f ? 1.0f / peak : 0.0f;
        }

        for (int i = 0; i < tableSize; ++i)
            out[i] *= gain;

        out[tableSize] = out[0];
    }
}

} // namespace whynot::synth
