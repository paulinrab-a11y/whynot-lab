#pragma once

// Afinação de voz (o "auto-tune" da WHYNOT), sem JUCE.
//
// 1. Detecção: o YIN do núcleo PSOLA, em tempo real sobre o sinal reduzido para ~12 kHz.
// 2. Alvo: a nota mais perto entre as permitidas (a escala, ou as 12 notas ligadas uma a uma na Personalizada),
//    numa grade que segue o Lá de referência e o ajuste fino, com histerese (não pula entre duas notas).
// 3. Correção: a diferença em semitons, vezes a força, suavizada pela velocidade
//    (0 ms = salto na hora, o efeito "robô").
// 4. Deslocamento: PSOLA (PsolaCore, o mesmo do Tom da voz). Cada grão guarda a forma de onda de um período,
//    então a voz muda de nota sem mudar de timbre; a Formante muda o timbre à parte (±12 st).
//
// Latência fixa (informada ao host para a compensação): 3,5 períodos de 60 Hz mais uma folga, 58,5 ms a 48 kHz.
// O núcleo antigo (duas cabeças numa linha de atraso, 15 ms) foi trocado em 27/10: as formantes andavam junto
// com o tom e não havia como mexer nelas. Ao gravar com a monitoração ligada, o app tira da escuta os efeitos com
// latência (o take é seco), então a latência maior não pesa para quem canta. A decisão está em docs/afinacao.md.
//
// Sem nada a fazer (força 0, ou nenhuma nota ligada, e formante 0) a saída é a entrada atrasada, exata.
//
// Nota guia (sidechain): com "Seguir a guia" ligado e uma trilha no sidechain, a nota alvo é a que a guia toca,
// na oitava mais perto da voz (fora da escala também). Sem tom na guia, volta para a escala.
// O MIDI direto do piano roll num efeito de trilha de mixer não existe no motor hoje (ver docs/afinacao.md).
//
// Harmonia: uma voz a mais (terça ou quinta da escala, terça abaixo ou oitava abaixo) que sai da nota corrigida, com a
// mesma velocidade e a mesma formante; fica calada sem voz e nas consoantes.
//
// Humanizar: numa nota que se sustenta (mais de 120 ms no mesmo alvo), a correção vai ficando mais
// lenta (até +350 ms), e o vibrato natural passa; a troca de nota continua na velocidade. Com
// velocidade 0 e humanizar 0 é o efeito duro do trap: salto na hora e nota reta.
// "Seguir o tom do projeto" não mexe no som: o app troca o tom e a escala quando o projeto muda.

#include "Processor.h"
#include "VoicePitch.h"

#include <array>

namespace whynot::dsp
{

class TuneProcessor final : public Processor
{
public:
    enum Param { key, scale, speed, amount, range, mix, humanize, follow, reference, fine,
                 noteC, noteCs, noteD, noteDs, noteE, noteF, noteFs, noteG, noteGs, noteA, noteAs, noteB,
                 formant, guide, harmony, harmonyLevel, count };
    enum Meter { detectedNote, targetNote, correctionCents, voicedMeter, guideMeter, harmonyNote };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return core.latency(); }
    double tailSecondsFor (const float*) const noexcept override   { return 0.1; }

    /** Graus (semitons a partir da tônica) da escala de índice `scaleIndex`, como máscara de 12 bits.
        Ordem igual à do projeto: menor, maior, menor harmônica, frígio, dórico, pentatônica menor, cromática. */
    static int scaleMask (int scaleIndex) noexcept;

    /** A nota (MIDI) mais perto de `midi` que está na escala. */
    static int nearestInScale (float midi, int keyRoot, int mask) noexcept;

    /** A escala "Personalizada": as 12 notas ligadas e desligadas uma a uma. */
    static constexpr int customScale = 7;

    /** As notas permitidas, como máscara de 12 bits em classes de nota absolutas (bit 0 = Dó), para os valores
        dos parâmetros (`values` na ordem de Param). 0 = nenhuma nota: o efeito não corrige. */
    static int activeNotes (const float* values) noexcept;

    /** Quanto a grade de notas está deslocada do Lá 440 sem ajuste, em semitons (Lá de referência e ajuste fino). */
    static float gridOffset (const float* values) noexcept;

private:
    void analyse (int elapsed) noexcept;
    void updateNeutral() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    PsolaCore core;

    // As medidas do YIN dos últimos blocos de controle: o grão que o PSOLA monta agora é de ~6 ms antes do meio da
    // janela do YIN (numa voz aguda), então a nota de cada grão sai da medida de quando ele foi cantado.
    struct Measure { float period = 0.0f, guidePeriod = 0.0f; bool voiced = false, guideVoiced = false; };
    std::array<Measure, 32> history {};
    int historyPos = 0;
    float halfMaxPeriod = 400.0f;

    const Measure& measureFor (bool guideTrack) const noexcept;

    // Nota guia: o tom de outra trilha (o sidechain: um synth ou piano tocando a melodia) manda na nota alvo.
    YinTracker guideYin;
    bool guiding = false, guideListening = false;

    // Alvo
    int notesMask = 0xFFF, lastTarget = -1;
    float detectedMidi = -1.0f, targetMidi = -1.0f;
    bool voiced = false;

    // Correção (semitons)
    float correctionTarget = 0.0f, correction = 0.0f;
    float releaseCoeff = 0.0f, currentAttack = 0.0f;
    int heldSamples = 0;   // há quanto tempo o alvo é a mesma nota (humanizar)

    Smoothed formantFactor, wet;

    // Harmonia: a segunda leitura do PSOLA, a um intervalo da escala acima ou abaixo da nota corrigida.
    Smoothed harmonyShift, harmonyGain, harmonyGate;
    int harmonyTarget = -1;
};

} // namespace whynot::dsp
