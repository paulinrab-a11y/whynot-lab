#pragma once

#include <algorithm>
#include <cmath>

namespace whynot::audio
{

/** O pitch wheel e o mod wheel de um canal do sampler nativo e do WHYNOT Piano (27/10): as curvas desenhadas na
    nota chegam como pitch wheel e CC1 (NoteCurves.h), e aqui viram um desvio de tom em semitons, amostra a amostra,
    que as vozes somam ao tom delas.

    - Pitch wheel: (valor - 8192) / 8192 × alcance, a mesma conta do WHYNOT Synth e do curves::fromWheel. O alcance
      vem do canal (setRange, o bendRange do canal) ou do RPN 0 que o MIDI exportado manda (CC 101 = 0, CC 100 = 0,
      CC 6 = semitons, CC 38 = cents); vale o último que chegou.
    - Mod wheel (CC1): um vibrato leve, de 5,5 Hz e no máximo ±0,5 semitom com a roda toda. Num sample não há filtro
      nem matriz para a roda mexer; o vibrato é o uso comum dela e dá para ouvir em qualquer som. O LFO é um só no
      canal (um acorde vibra junto) e recomeça do zero numa nota que chega com o canal em silêncio, então o mesmo
      trecho soa igual ao vivo e no render.
    - Sem clique: cada valor novo chega numa rampa reta de 5 ms; um valor novo no meio da rampa parte de onde ela
      está. Uma nota nova pula a rampa (snap): o projetor manda o valor da nota no mesmo instante dela, e a nota tem
      de começar no tom dela, não deslizando do bend da anterior.
    - Em descanso (wheel no meio, roda em 0, sem rampa), fill() não escreve nada e devolve false: a voz não soma
      nada e o som sai bit a bit igual ao de antes.

    Só a thread de áudio, sem alocação nem trava. */
class ChannelPitch
{
public:
    static constexpr float defaultRange = 2.0f, maxRange = 24.0f;
    static constexpr float vibratoHz = 5.5f;
    static constexpr float vibratoSemis = 0.5f;   // a roda toda: ±50 cents
    static constexpr double rampMs = 5.0;
    static constexpr int wheelCentre = 8192;

    void prepare (double sampleRate) noexcept
    {
        const double rate = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 48000.0;
        rampFrames = std::max (1, (int) std::lround (rampMs * 0.001 * rate));
        lfoStep = twoPi * (double) vibratoHz / rate;
        reset();
    }

    /** Volta ao descanso: wheel no meio e roda em 0, na hora (transporte parado, reset dos controladores). O alcance
        fica. */
    void reset() noexcept
    {
        wheel = wheelCentre;
        bend = bendTarget = bendStep = 0.0f;
        mod = modTarget = modStep = 0.0f;
        bendLeft = modLeft = 0;
        phase = 0.0;
        rpnMsb = rpnLsb = 127;
    }

    /** O alcance do pitch wheel inteiro, em semitons (0 a 24; o bendRange do canal). */
    void setRange (float semis) noexcept
    {
        range = std::isfinite (semis) ? std::clamp (semis, 0.0f, maxRange) : defaultRange;
        retargetBend();
    }

    float getRange() const noexcept   { return range; }

    void pitchWheel (int value) noexcept
    {
        wheel = std::clamp (value, 0, 16383);
        retargetBend();
    }

    /** Um controlador: 1 (a roda), 101 e 100 (qual RPN), 6 e 38 (o valor do RPN 0, o alcance) e 121 (reset dos
        controladores). Devolve false nos outros, para quem chama tratar. */
    bool controller (int number, int value) noexcept
    {
        value = std::clamp (value, 0, 127);

        switch (number)
        {
            case 1:
                modTarget = (float) value / 127.0f;
                startRamp (mod, modTarget, modStep, modLeft);
                return true;

            case 101: rpnMsb = value; return true;
            case 100: rpnLsb = value; return true;
            case 99: case 98: rpnMsb = rpnLsb = 127; return true;   // um NRPN desarma o RPN: o CC 6 dele não é o alcance

            case 6:
                if (rpnMsb == 0 && rpnLsb == 0)
                {
                    rpnSemis = value;
                    rpnCents = 0;   // o MSB do alcance zera os cents, como a especificação manda
                    setRange ((float) rpnSemis);
                }
                return true;

            case 38:
                if (rpnMsb == 0 && rpnLsb == 0)
                {
                    rpnCents = std::min (value, 99);
                    setRange ((float) rpnSemis + (float) rpnCents / 100.0f);
                }
                return true;

            case 121:
                reset();
                return true;

            default:
                return false;
        }
    }

    /** A nota que começa agora: o canal vai já para o valor pedido (sem a rampa). `silent` = nenhuma voz soava:
        o vibrato recomeça do zero. */
    void noteStarts (bool silent) noexcept
    {
        bend = bendTarget;
        mod = modTarget;
        bendLeft = modLeft = 0;

        if (silent)
            phase = 0.0;
    }

    bool atRest() const noexcept   { return bend == 0.0f && bendLeft == 0 && mod == 0.0f && modLeft == 0; }

    /** O desvio de tom (semitons) das próximas `num` amostras em out[0..num). Em descanso não escreve nada e
        devolve false (o LFO anda do mesmo jeito). */
    bool fill (float* out, int num) noexcept
    {
        if (num <= 0)
            return false;

        if (atRest())
        {
            phase = std::fmod (phase + lfoStep * num, twoPi);
            return false;
        }

        for (int i = 0; i < num; ++i)
        {
            if (bendLeft > 0)
                bend = --bendLeft == 0 ? bendTarget : bend + bendStep;

            if (modLeft > 0)
                mod = --modLeft == 0 ? modTarget : mod + modStep;

            const float vibrato = mod != 0.0f ? mod * vibratoSemis * (float) std::sin (phase) : 0.0f;
            phase += lfoStep;

            if (phase >= twoPi)
                phase -= twoPi;

            out[i] = bend + vibrato;
        }

        return true;
    }

private:
    static constexpr double twoPi = 6.283185307179586;

    void retargetBend() noexcept
    {
        bendTarget = (float) (wheel - wheelCentre) / (float) wheelCentre * range;
        startRamp (bend, bendTarget, bendStep, bendLeft);
    }

    void startRamp (float from, float to, float& step, int& left) const noexcept
    {
        if (from == to)
        {
            step = 0.0f;
            left = 0;
            return;
        }

        left = rampFrames;
        step = (to - from) / (float) rampFrames;
    }

    float range = defaultRange;
    int wheel = wheelCentre;
    float bend = 0.0f, bendTarget = 0.0f, bendStep = 0.0f;
    float mod = 0.0f, modTarget = 0.0f, modStep = 0.0f;
    int bendLeft = 0, modLeft = 0, rampFrames = 240;
    double phase = 0.0, lfoStep = 0.0;
    int rpnMsb = 127, rpnLsb = 127, rpnSemis = 2, rpnCents = 0;
};

} // namespace whynot::audio
