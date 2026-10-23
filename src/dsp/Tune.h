#pragma once

// Afinação de voz (o "auto-tune" da WHYNOT), sem JUCE.
//
// 1. Detecção: YIN em tempo real sobre o sinal reduzido para ~12 kHz (passa-baixas antes).
// 2. Alvo: a nota mais perto na escala escolhida, com histerese (não pula entre duas notas).
// 3. Correção: a diferença em semitons, vezes a força, suavizada pela velocidade
//    (0 ms = salto na hora, o efeito "robô").
// 4. Deslocamento: duas cabeças de leitura numa linha de atraso que andam na razão da correção;
//    quando uma chega ao limite, a outra entra deslocada de um múltiplo exato do período
//    detectado (a emenda cai na mesma fase da onda) e as duas se cruzam numa janela de cosseno.
//    Sem correção, a cabeça fica no atraso nominal e a saída é a entrada atrasada, igual.
//
// Latência fixa (informada ao host) de ~15 ms a 48 kHz. As formantes andam junto com o tom,
// o que é imperceptível nas correções pequenas de afinação.
//
// Humanizar: numa nota que se sustenta (mais de 120 ms no mesmo alvo), a correção vai ficando mais
// lenta (até +350 ms), e o vibrato natural passa; a troca de nota continua na velocidade. Com
// velocidade 0 e humanizar 0 é o efeito duro do trap: salto na hora e nota reta.
// "Seguir o tom do projeto" não mexe no som: o app troca o tom e a escala quando o projeto muda.

#include "DelayLine.h"
#include "Processor.h"
#include "Svf.h"

#include <array>
#include <vector>

namespace whynot::dsp
{

class TuneProcessor final : public Processor
{
public:
    enum Param { key, scale, speed, amount, range, mix, humanize, follow, count };
    enum Meter { detectedNote, targetNote, correctionCents, voicedMeter };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return latency; }
    double tailSecondsFor (const float*) const noexcept override   { return 0.05; }

    /** Graus (semitons a partir da tônica) da escala de índice `scaleIndex`, como máscara de 12 bits.
        Ordem igual à do projeto: menor, maior, menor harmônica, frígio, dórico, pentatônica menor, cromática. */
    static int scaleMask (int scaleIndex) noexcept;

    /** A nota (MIDI) mais perto de `midi` que está na escala. */
    static int nearestInScale (float midi, int keyRoot, int mask) noexcept;

private:
    void analyse() noexcept;
    float detect (float& clarity) noexcept;   // Hz (0 = sem tom)
    void startSplice (float newDelay, int fadeLength) noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};

    // Detecção
    int decimation = 4, decimCounter = 0;
    double detectRate = 12000.0;
    SvfCoeffs antiAlias;
    Svf aa1, aa2;
    std::vector<float> ring, frame, diff, rawDiff;   // diff = normalizada (escolhe o período); rawDiff = crua (afina a posição)
    int ringPos = 0, ringFilled = 0, hopCounter = 0;
    int window = 400, tauMaxAll = 200, hop = 64;
    int lastTarget = -1;
    float detectedMidi = -1.0f, targetMidi = -1.0f;
    bool voiced = false;
    float periodSamples = 0.0f;               // período detectado, na taxa cheia

    // Correção (semitons)
    float correctionTarget = 0.0f, correction = 0.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, currentAttack = 0.0f;
    int heldSamples = 0;   // há quanto tempo o alvo é a mesma nota (humanizar)

    // Deslocamento
    DelayLine lineL, lineR;
    float delayA = 0.0f, delayB = 0.0f;
    int fadePos = 0, fadeLen = 0;
    bool fading = false;
    float minDelay = 0.0f, maxDelay = 0.0f, maxPeriod = 800.0f;
    int latency = 0;
    Smoothed wet;
};

} // namespace whynot::dsp
