#pragma once

// A prévia de áudio da IA (Fase 4, fatia 4.6; docs/fase-4-plano.md, seção 4.5): as medidas de um WAV que dão para
// conferir em texto. Com elas a IA vê se o 808 entrou, se algo cortou, se um compasso ficou mudo. Não é ouvir de verdade.

#include <JuceHeader.h>

#include <vector>

namespace whynot::control
{

struct AudioMeasures
{
    bool ok = false;
    juce::String error;

    double seconds = 0.0, sampleRate = 0.0;
    int channels = 0;
    float peakDb = -100.0f;        // o maior valor de amostra (dBFS)
    float truePeakDb = -100.0f;    // sobreamostrado 4x (dBTP)
    float rmsDb = -100.0f;         // o arquivo inteiro, todos os canais
    float lufs = -100.0f;          // integrado (BS.1770 / EBU R128)
    int clipped = 0;               // amostras em 0 dBFS ou acima
    double silenceStart = 0.0;     // segundos abaixo de -60 dBFS no começo
    double silenceEnd = 0.0;       // e no fim
    double barSeconds = 0.0;
    std::vector<float> barsRmsDb;  // um por compasso, até maxBars
    bool barsCut = false;          // o arquivo tem mais compassos que maxBars
    std::vector<float> beatsRmsDb; // um por tempo, até maxBeats: um clap a mais muda o tempo dele, não só o compasso
    bool beatsCut = false;         // o arquivo tem mais tempos que maxBeats
    bool lastBarPartial = false;   // o último compasso não chega ao fim (padrão quebrado, fim da música)

    static constexpr int maxBars = 256;
    static constexpr int maxBeats = 256;   // 64 compassos em 4/4: uma prévia de padrão cabe inteira

    /** Uma sobra (ou falta) no fim menor que isto, em compassos, é arredondamento do render, não um compasso. */
    static constexpr double barTolerance = 0.01;
};

/** Lê o arquivo em blocos (não carrega tudo na memória) e mede. bpm e beatsPerBar dão o tamanho do compasso. */
AudioMeasures measureAudio (const juce::File&, double bpm, int beatsPerBar);

/** As medidas em JSON, com uma casa decimal (o que vai para a IA). */
juce::var toVar (const AudioMeasures&);

} // namespace whynot::control
