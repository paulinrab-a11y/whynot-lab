#pragma once

// Peças pequenas do WHYNOT Synth: seno por tabela, envelope ADSR com curva, saturação suave e o
// soft clip da saída. Sem JUCE, sem alocação.
//
// As ondas analógicas (serra, pulso, triângulo) não usam polyBLEP: medido no teste, o polyBLEP de
// 2 pontos deixava -34 dB de aliasing em Dó7. Elas vêm de tabelas de banda limitada por oitava
// (Wavetables::basic), que ficam abaixo de -80 dB.

#include <algorithm>
#include <array>
#include <cmath>

#if defined (_MSC_VER) && (defined (_M_X64) || defined (_M_AMD64))
 #include <smmintrin.h>
 #define WHYNOT_SYNTH_SSE41_FLOOR 1   // o Windows 11 exige SSE4.1: o roundss existe em toda máquina do app
#endif

namespace whynot::synth
{

/** Seno por tabela (4096 pontos, interpolação linear: erro < 1e-6). */
class SineTable
{
public:
    static constexpr int size = 4096;

    static const SineTable& get()
    {
        static const SineTable table;
        return table;
    }

    /** sin (2 pi * phase), com phase em [0, 1). */
    float operator() (float phase) const noexcept
    {
        const float pos = phase * (float) size;
        const int i = (int) pos;
        const float frac = pos - (float) i;
        const float a = values[(size_t) (i & (size - 1))], b = values[(size_t) ((i & (size - 1)) + 1)];
        return a + frac * (b - a);
    }

private:
    SineTable()
    {
        for (int i = 0; i <= size; ++i)
            values[(size_t) i] = (float) std::sin (6.283185307179586 * i / size);
    }

    std::array<float, size + 1> values {};
};

/** Parte fracionária, também para negativos (fase de FM pode sair de [0, 1)). */
inline float wrap (float x) noexcept
{
    // O FM chama isto até 5 vezes por amostra, numa cadeia em que cada passo espera o anterior.
    // Medido (banco A/B): o std::floor do MSVC vira chamada de biblioteca; trocar por (int) com um "if"
    // errava a previsão (fase negativa ao acaso) e sem "if" alongava a cadeia (-26% no FM de 4
    // operadores). O roundss faz o floor numa instrução, com o mesmo resultado.
#if WHYNOT_SYNTH_SSE41_FLOOR
    const __m128 v = _mm_set_ss (x);
    return _mm_cvtss_f32 (_mm_sub_ss (v, _mm_floor_ss (v, v)));
#else
    return x - std::floor (x);
#endif
}

/** tanh aproximada (racional), boa para saturar: erro < 2% e satura em +-1. */
inline float fastTanh (float x) noexcept
{
    if (x > 3.0f)  return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Soft clip da saída: linear até 0,8 e depois curva até no máximo 1 (nunca passa de 0 dBFS). */
inline float softClip (float x) noexcept
{
    constexpr float knee = 0.8f;
    const float a = std::abs (x);

    if (a <= knee)
        return x;

    const float over = (a - knee) / (1.0f - knee);
    return std::copysign (knee + (1.0f - knee) * fastTanh (over), x);
}

/** Coeficientes de um envelope ADSR (compartilhados por todas as vozes).
    Segmentos exponenciais com alvo "além" do fim (como no ADSR de Nigel Redmon, earlevel.com):
    a curva vai de reta (alvo muito além) a bem exponencial (alvo quase no fim). */
struct EnvelopeCoeffs
{
    float attackCoef = 0.0f, attackBase = 1.0f;
    float decayCoef = 0.0f, decayBase = 0.0f;
    float releaseCoef = 0.0f, releaseBase = 0.0f;
    float sustain = 1.0f;
    float sustainGlide = 0.998f;   // sustentação mudada com a nota soando: chega lá em ~10 ms em qualquer taxa

    void set (float attackMs, float decayMs, float sustainLevel, float releaseMs, float curve, double sampleRate) noexcept
    {
        curve = std::clamp (curve, 0.0f, 1.0f);
        sustain = std::clamp (sustainLevel, 0.0f, 1.0f);

        const float ratioAttack = std::pow (10.0f, 1.7f - 4.7f * curve);    // 50 (reta) ... 0,001 (estalado)
        const float ratioFall = std::pow (10.0f, 1.0f - 5.0f * curve);      // 10 (reta) ... 0,0001

        auto coef = [sampleRate] (float ms, float ratio)
        {
            const double samples = std::max (1.0, (double) ms * 0.001 * sampleRate);
            return (float) std::exp (-std::log ((1.0 + ratio) / ratio) / samples);
        };

        attackCoef = coef (attackMs, ratioAttack);
        attackBase = (1.0f + ratioAttack) * (1.0f - attackCoef);
        decayCoef = coef (decayMs, ratioFall);
        decayBase = (sustain - ratioFall) * (1.0f - decayCoef);
        releaseCoef = coef (releaseMs, ratioFall);
        releaseBase = -ratioFall * (1.0f - releaseCoef);
        sustainGlide = (float) std::exp (-1.0 / (0.002 * sampleRate));
    }
};

class Envelope
{
public:
    enum class Stage { idle, attack, decay, sustain, release };

    /** Começa (ou recomeça) o ataque a partir do valor atual: redisparo sem estalo. */
    void noteOn() noexcept               { stage = Stage::attack; }
    void noteOff() noexcept              { if (stage != Stage::idle) stage = Stage::release; }
    void reset() noexcept                { stage = Stage::idle; value = 0.0f; }

    bool isActive() const noexcept       { return stage != Stage::idle; }
    Stage getStage() const noexcept      { return stage; }
    float getValue() const noexcept      { return value; }

    float next (const EnvelopeCoeffs& c) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                value = c.attackBase + value * c.attackCoef;

                if (value >= 1.0f)
                {
                    value = 1.0f;
                    stage = Stage::decay;
                }
                break;

            case Stage::decay:
                value = c.decayBase + value * c.decayCoef;

                if (value <= c.sustain)
                {
                    value = c.sustain;
                    stage = Stage::sustain;
                }
                break;

            case Stage::sustain:
                // Sustentação mudou com a nota soando: chega lá em ~10 ms, sem degrau.
                value = c.sustain + (value - c.sustain) * c.sustainGlide;
                break;

            case Stage::release:
                value = c.releaseBase + value * c.releaseCoef;

                if (value <= 0.0f)
                {
                    value = 0.0f;
                    stage = Stage::idle;
                }
                break;

            case Stage::idle:
                break;
        }

        return value;
    }

private:
    Stage stage = Stage::idle;
    float value = 0.0f;
};

} // namespace whynot::synth
