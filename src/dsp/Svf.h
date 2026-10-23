#pragma once

#include "DspCommon.h"

#include <array>
#include <complex>

namespace whynot::dsp
{

enum class FilterType { lowpass, highpass, bandpass, notch, allpass, bell, lowShelf, highShelf };

/** Coeficientes do filtro de variáveis de estado trapezoidal (topologia de Andrew Simper,
    "Linear Trapezoidal Integrated SVF", publicada em cytomic.com). Mesma resposta de um
    biquad do "Audio EQ Cookbook" (RBJ), mas estável quando a frequência muda depressa. */
struct SvfCoeffs
{
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;   // padrão: passa direto
    double g = 0.0, k = 1.0;

    static SvfCoeffs make (FilterType type, double freq, double q, double gainDb, double sampleRate) noexcept
    {
        SvfCoeffs c;
        freq = std::clamp (freq, 1.0, sampleRate * 0.49);
        q = std::max (q, 0.025);

        const double A = std::pow (10.0, gainDb / 40.0);
        double g = std::tan (pi * freq / sampleRate);
        double k = 1.0 / q;

        switch (type)
        {
            case FilterType::lowpass:   c.m0 = 0.0; c.m1 = 0.0;  c.m2 = 1.0;  break;
            case FilterType::highpass:  c.m0 = 1.0; c.m1 = -k;   c.m2 = -1.0; break;
            case FilterType::bandpass:  c.m0 = 0.0; c.m1 = k;    c.m2 = 0.0;  break;
            case FilterType::notch:     c.m0 = 1.0; c.m1 = -k;   c.m2 = 0.0;  break;
            case FilterType::allpass:   c.m0 = 1.0; c.m1 = -2*k; c.m2 = 0.0;  break;

            case FilterType::bell:
                k = 1.0 / (q * A);
                c.m0 = 1.0; c.m1 = k * (A * A - 1.0); c.m2 = 0.0;
                break;

            case FilterType::lowShelf:
                g /= std::sqrt (A);
                c.m0 = 1.0; c.m1 = k * (A - 1.0); c.m2 = A * A - 1.0;
                break;

            case FilterType::highShelf:
                g *= std::sqrt (A);
                c.m0 = A * A; c.m1 = k * (1.0 - A) * A; c.m2 = 1.0 - A * A;
                break;
        }

        c.g = g;
        c.k = k;
        c.a1 = 1.0 / (1.0 + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }

    /** Filtro com mistura própria das saídas: H(s) = m0 + m1 * passa-banda + m2 * passa-baixas,
        com passa-banda = s / (s² + s/Q + 1) e passa-baixas = 1 / (s² + s/Q + 1). */
    static SvfCoeffs custom (double freq, double q, double m0, double m1, double m2, double sampleRate) noexcept
    {
        SvfCoeffs c;
        freq = std::clamp (freq, 1.0, sampleRate * 0.49);
        c.g = std::tan (pi * freq / sampleRate);
        c.k = 1.0 / std::max (q, 0.025);
        c.m0 = m0; c.m1 = m1; c.m2 = m2;
        c.a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
        c.a2 = c.g * c.a1;
        c.a3 = c.g * c.a2;
        return c;
    }

    /** Resposta exata do filtro digital em `freq` (para desenhar curvas de EQ). */
    std::complex<double> response (double freq, double sampleRate) const noexcept
    {
        if (g <= 0.0)
            return { m0, 0.0 };

        const double w = std::tan (pi * std::clamp (freq, 0.0, sampleRate * 0.4999) / sampleRate) / g;
        const std::complex<double> s (0.0, w);
        const auto den = s * s + k * s + 1.0;
        return m0 + m1 * s / den + m2 / den;
    }

    double magnitudeDb (double freq, double sampleRate) const noexcept
    {
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (response (freq, sampleRate))));
    }
};

/** Um canal do filtro. Estado em double: graves a 20 Hz em 96 kHz continuam limpos. */
class Svf
{
public:
    void reset() noexcept   { ic1 = ic2 = 0.0; }

    float process (float x, const SvfCoeffs& c) noexcept
    {
        const double v0 = x;
        const double v3 = v0 - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return (float) (c.m0 * v0 + c.m1 * v1 + c.m2 * v2);
    }

private:
    double ic1 = 0.0, ic2 = 0.0;
};

/** Até 4 seções de 2ª ordem em série, estéreo. Trocar a quantidade de seções (inclinação)
    não estala: a cadeia antiga continua soando e some em ~10 ms enquanto a nova entra. */
class SvfCascade
{
public:
    static constexpr int maxSections = 4;

    void prepare (double sampleRate) noexcept
    {
        fade.prepare (sampleRate, 3.0f);
        fade.snap (1.0f);
        warmupLength = (int) (0.08 * sampleRate);
        warmup = 0;
    }

    void reset() noexcept
    {
        for (auto* chain : { &current, &previous })
            for (int s = 0; s < maxSections; ++s)
            {
                chain->left[(size_t) s].reset();
                chain->right[(size_t) s].reset();
            }

        fade.snap (1.0f);
        warmup = 0;
    }

    int sections() const noexcept   { return count; }

    void setSections (int n) noexcept
    {
        n = std::clamp (n, 1, maxSections);

        if (n == count)
            return;

        previous = current;
        previousCount = count;
        count = n;

        for (int s = 0; s < maxSections; ++s)
        {
            current.left[(size_t) s].reset();
            current.right[(size_t) s].reset();
        }

        // A cadeia nova roda calada até assentar (filtros partindo do zero dão um tranco), e só
        // então entra no lugar da antiga.
        fade.snap (0.0f);
        warmup = warmupLength;
    }

    void setCoeffs (int section, const SvfCoeffs& c) noexcept   { current.coeffs[(size_t) section] = c; }
    void finishFade() noexcept                                  { fade.snap (1.0f); warmup = 0; }

    void process (float& l, float& r) noexcept
    {
        float nl = l, nr = r;

        for (int s = 0; s < count; ++s)
        {
            nl = current.left[(size_t) s].process (nl, current.coeffs[(size_t) s]);
            nr = current.right[(size_t) s].process (nr, current.coeffs[(size_t) s]);
        }

        if (warmup == 0 && ! fade.isSmoothing())
        {
            l = nl;
            r = nr;
            return;
        }

        float ol = l, orr = r;

        for (int s = 0; s < previousCount; ++s)
        {
            ol = previous.left[(size_t) s].process (ol, previous.coeffs[(size_t) s]);
            orr = previous.right[(size_t) s].process (orr, previous.coeffs[(size_t) s]);
        }

        if (warmup > 0 && --warmup == 0)
            fade.setTarget (1.0f);

        const float f = fade.next();
        l = ol + f * (nl - ol);
        r = orr + f * (nr - orr);
    }

private:
    struct Chain
    {
        std::array<SvfCoeffs, maxSections> coeffs;
        std::array<Svf, maxSections> left, right;
    };

    Chain current, previous;
    int count = 1, previousCount = 1;
    int warmup = 0, warmupLength = 3840;
    Smoothed fade;
};

/** Q de cada seção de um Butterworth de ordem 2n (n seções de 2ª ordem). */
inline double butterworthQ (int numSections, int section) noexcept
{
    const int order = numSections * 2;
    const double angle = pi * (2.0 * section + 1.0) / (2.0 * order);
    return 1.0 / (2.0 * std::sin (angle));
}

/** Divisor Linkwitz-Riley de 4ª ordem: grave + agudo somam um passa-tudo (sem buraco no corte). */
class LinkwitzRiley4
{
public:
    void setup (double freq, double sampleRate) noexcept
    {
        lowCoeffs = SvfCoeffs::make (FilterType::lowpass, freq, 0.70710678, 0.0, sampleRate);
        highCoeffs = SvfCoeffs::make (FilterType::highpass, freq, 0.70710678, 0.0, sampleRate);
    }

    void reset() noexcept   { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }

    void process (float x, float& low, float& high) noexcept
    {
        low = lp2.process (lp1.process (x, lowCoeffs), lowCoeffs);
        high = hp2.process (hp1.process (x, highCoeffs), highCoeffs);
    }

private:
    SvfCoeffs lowCoeffs, highCoeffs;
    Svf lp1, lp2, hp1, hp2;
};

} // namespace whynot::dsp
