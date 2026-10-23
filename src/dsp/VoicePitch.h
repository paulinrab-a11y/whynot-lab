#pragma once

// Tom da voz e Harmonia: mudam o tom da voz por PSOLA, com as formantes no lugar (ou mexidas à parte).
// Para ad-libs de voz grossa ou fina, oitava abaixo, o timbre de outra pessoa e vozes de harmonia na escala.

#include "Processor.h"
#include "Svf.h"

#include <array>
#include <vector>

namespace whynot::dsp
{

/** Detector de período por YIN em tempo real (o mesmo método da Afinação), na taxa reduzida para ~12 kHz.
    Cada atraso usa as amostras mais novas do quadro. */
class YinTracker
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void setRange (double lowHz, double highHz) noexcept   { fLow = lowHz; fHigh = highHz; }

    /** Uma amostra; devolve true quando há uma medida nova (a cada ~5 ms). */
    bool push (float x) noexcept;

    bool voiced() const noexcept           { return isVoiced; }
    float periodSamples() const noexcept   { return period; }   // na taxa cheia

private:
    bool analyse() noexcept;

    double rate = 48000.0, detectRate = 12000.0, fLow = 60.0, fHigh = 1000.0;
    int decimation = 4, decimCounter = 0, window = 400, tauMaxAll = 200, hop = 60, hopCounter = 0;
    SvfCoeffs antiAlias;
    Svf aa1, aa2;
    std::vector<float> ring, frame, diff, rawDiff;
    int ringPos = 0, ringFilled = 0;
    float period = 0.0f;
    bool isVoiced = false;
};

/** PSOLA: marca os começos de período da voz (picos do sinal filtrado, um por período detectado), recorta
    grãos de dois períodos com janela de Hann e os recoloca com o espaçamento do tom novo. Como cada grão
    guarda a forma de onda de um período, as formantes não mudam com o tom. Para mudar as formantes à parte,
    cada grão é lido mais rápido ou mais devagar antes de ser recolocado.

    Até `maxReads` leituras dos mesmos grãos, cada uma com o próprio tom (a Harmonia usa duas).
    Nos trechos sem tom (consoantes, respiração) os grãos são de 5 ms e não mudam de tom; uma leitura
    marcada como `voicedOnly` (as vozes da harmonia) fica calada neles.
    Latência fixa: três vezes o maior período (60 Hz) mais uma folga, ~51 ms a 48 kHz. */
class PsolaCore
{
public:
    static constexpr int maxReads = 2;

    void prepare (double sampleRate, int numReads);
    void reset() noexcept;
    void setRange (double lowHz, double highHz) noexcept   { yin.setRange (lowHz, highHz); }

    /** Tom (razão de frequência) e formante (razão) de uma leitura, a partir da próxima marca. */
    void setShift (int read, float ratio, float formantRatio, bool voicedOnly) noexcept;

    /** Uma amostra entra. `wetL/wetR[k]` recebe a leitura k, e `dryL/dryR` a entrada, as duas atrasadas `latency()`. */
    void process (float l, float r, float* wetL, float* wetR, float& dryL, float& dryR) noexcept;

    int latency() const noexcept           { return latencySamples; }
    bool voiced() const noexcept           { return yin.voiced(); }
    float periodSamples() const noexcept   { return yin.periodSamples(); }

private:
    struct Mark
    {
        double pos = 0.0;      // posição absoluta na entrada (amostras)
        float period = 0.0f;   // meio comprimento do grão na entrada
        bool voiced = false;
    };

    struct Read
    {
        std::vector<float> accL, accR;
        double nextSynthesis = 0.0;
        float ratio = 1.0f, formant = 1.0f;
        bool voicedOnly = false;
    };

    static constexpr int maxMarks = 256;

    void placeMarks() noexcept;
    void synthesize (Read&) noexcept;
    const Mark* nearestMark (double pos) const noexcept;

    double sampleRate = 48000.0;
    YinTracker yin;
    SvfCoeffs lowCoeffs;
    Svf lowFilter;
    std::vector<float> inL, inR, lowRing;
    std::array<Read, maxReads> reads;
    int numReads = 1;
    std::int64_t mask = 0, now = -1;   // `now` = índice absoluto da última amostra que entrou
    int latencySamples = 0;
    float maxPeriod = 800.0f, unvoicedPeriod = 240.0f;

    std::array<Mark, maxMarks> marks {};
    int markCount = 0, markHead = 0;   // anel: markHead = a próxima posição livre
    double lastMark = 0.0;
};

/** Tom da voz: o tom em semitons e a formante à parte, com mistura. */
class VoicePitchProcessor final : public Processor
{
public:
    enum Param { semitones, formant, range, mix, count };
    enum Meter { detectedHz, voicedMeter };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return core.latency(); }
    double tailSecondsFor (const float*) const noexcept override   { return 0.1; }

private:
    double sampleRate = 48000.0;
    std::array<float, count> values {};
    PsolaCore core;
    Smoothed ratio, formantFactor, wet;
};

/** Harmonia: uma ou duas vozes a mais, em terças, quintas ou oitavas da escala do projeto. A nota cantada é
    lida a cada 5 ms; cada voz vai para a nota da escala que fica o número pedido de graus acima ou abaixo
    (então as vozes já saem afinadas). A voz original passa igual, atrasada junto. */
class HarmonyProcessor final : public Processor
{
public:
    enum Param { key, scale, voices, formant, level, width, follow, count };
    enum Meter { detectedNote, voicedMeter, harmony1, harmony2 };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void snapToTargets() noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return core.latency(); }
    double tailSecondsFor (const float*) const noexcept override   { return 0.1; }

    /** A nota (MIDI) `degrees` graus da escala acima (ou abaixo, se negativo) de `note`, que já está na escala. */
    static int scaleStep (int note, int degrees, int keyRoot, int mask) noexcept;

    /** As vozes de uma escolha: deslocamento em graus da escala; `octaveDown` marca a oitava exata abaixo. */
    static int voiceCount (int choice) noexcept;

private:
    void updateTargets() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    PsolaCore core;
    std::array<Smoothed, 2> ratio;
    Smoothed formantFactor, levelGain, widthAmount;
    int lastNote = -1;
    std::array<int, 2> lastHarmony { -1, -1 };
    bool wasVoiced = false;
};

} // namespace whynot::dsp
