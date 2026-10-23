#pragma once

// Peças básicas do DSP da WHYNOT: sem JUCE, para os testes compilarem em segundos.

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
 #include <xmmintrin.h>
 #define WHYNOT_DSP_X86 1
#endif

namespace whynot::dsp
{

constexpr double pi = 3.14159265358979323846;

inline float dbToGain (float db) noexcept
{
    return db <= -120.0f ? 0.0f : std::exp (db * 0.11512925f);   // 10^(db/20), mais barato que pow
}

inline float gainToDb (float gain, float floorDb = -120.0f) noexcept
{
    return gain <= 0.0f ? floorDb : std::max (floorDb, 20.0f * std::log10 (gain));
}

/** Coeficiente de um polo: o valor percorre ~63% do caminho até o alvo em `ms`. */
inline float onePoleCoeff (float ms, double sampleRate) noexcept
{
    const double samples = std::max (1.0e-3, (double) ms * 0.001 * sampleRate);
    return (float) std::exp (-1.0 / samples);
}

/** Desliga números desnormais (FTZ/DAZ) enquanto existe. Reverb e delay
    decaem até lá, e a CPU fica dezenas de vezes mais lenta com eles. */
class DenormalGuard
{
public:
#if WHYNOT_DSP_X86
    DenormalGuard() noexcept : previous (_mm_getcsr())   { _mm_setcsr (previous | 0x8040u); }
    ~DenormalGuard() noexcept                            { _mm_setcsr (previous); }
private:
    unsigned int previous;
#endif
};

/** Valor suavizado por um polo (ganhos, misturas, frequências em escala log). */
class Smoothed
{
public:
    void prepare (double sampleRate, float ms) noexcept   { coeff = onePoleCoeff (ms, sampleRate); }
    void setTarget (float t) noexcept                     { target = t; }
    void snap (float v) noexcept                          { target = current = v; }

    float next() noexcept
    {
        const float previous = current;
        current = target + (current - target) * coeff;
        settle (previous);
        return current;
    }

    /** Avança n amostras de uma vez (para quem atualiza coeficientes por sub-bloco). */
    float skip (int n) noexcept
    {
        const float previous = current;
        current = target + (current - target) * std::pow (coeff, (float) n);
        settle (previous);
        return current;
    }

    bool isSmoothing() const noexcept   { return current != target; }
    float getTarget() const noexcept    { return target; }
    float getCurrent() const noexcept   { return current; }

private:
    // Chega no alvo quando está perto o bastante ou quando o float parou de andar
    // (perto de 1,0 o passo vira menor que a precisão e o valor "empaca" para sempre).
    void settle (float previous) noexcept
    {
        if (current == previous || std::abs (current - target) < 1.0e-5f * (1.0f + std::abs (target)))
            current = target;
    }

    float coeff = 0.0f, target = 0.0f, current = 0.0f;
};

/** Filtro de um polo para envelopes: sobe com `attack`, desce com `release`. */
struct Ballistics
{
    float attack = 0.0f, release = 0.0f;

    void set (float attackMs, float releaseMs, double sampleRate) noexcept
    {
        attack = onePoleCoeff (attackMs, sampleRate);
        release = onePoleCoeff (releaseMs, sampleRate);
    }

    float apply (float state, float input) const noexcept
    {
        const float c = input > state ? attack : release;
        return input + (state - input) * c;
    }
};

/** Bloqueio de DC de primeira ordem (~10 Hz), para depois de distorções assimétricas. */
class DcBlocker
{
public:
    void prepare (double sampleRate) noexcept   { r = (float) std::exp (-2.0 * pi * 10.0 / sampleRate); }
    void reset() noexcept                       { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

} // namespace whynot::dsp
