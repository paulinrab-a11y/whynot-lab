#pragma once

// Tom do beat (pedido do usuário de 21/10, noite): o produtor pega um beat da internet e o app diz o tom,
// sem sair dele; e o piano roll mostra onde o 808 casa em cada compasso. A análise de sinal é a do
// src/ideas/KeyDetect (feita por nós, sem modelo de fora); aqui ficam o arquivo, a thread e o projeto.
// Dono: frente Tom do beat (extra-tom).

#include <JuceHeader.h>

#include "../ideas/KeyDetect.h"
#include "../model/ProjectModel.h"

#include <deque>
#include <map>
#include <memory>

namespace whynot
{

/** Um beat do projeto: o arquivo e onde ele toca na música. */
struct BeatSource
{
    juce::String channel;          // id do canal (áudio, ou sampler com sample longo)
    juce::String name;
    juce::File file;
    bool inPlaylist = false;       // tem bloco na playlist (o primeiro, pelo início, manda)
    int startTicks = 0;            // início do bloco na música
    int lengthTicks = 0;
    double offsetSeconds = 0.0;    // quanto do começo do arquivo o bloco corta
    double stretch = 1.0;          // segundos na música por segundo do arquivo
    double fileSeconds = 0.0;

    /** Segundo do arquivo que toca no tick `t` da música. */
    double fileTimeAt (double t, double bpm) const noexcept
    {
        return offsetSeconds + ticksToSeconds (t - startTicks, bpm) / juce::jmax (0.01, stretch);
    }
};

/** A dica do piano roll num compasso do padrão: o acorde do beat ali e onde o 808 casa (a fundamental). */
struct BeatHint
{
    int start = 0, end = 0;        // ticks do padrão
    int songBar = 0;               // compasso da música (0 = o primeiro)
    int root = 0;                  // 0 = Dó
    bool minor = false;
    double confidence = 0.0;       // 0..1

    bool contains (int key) const noexcept
    {
        const int rel = ((key - root) % 12 + 12) % 12;
        return rel == 0 || rel == 7 || rel == (minor ? 3 : 4);
    }
};

/** Analisa os beats numa thread de trabalho, um de cada vez, e guarda o resultado por arquivo.
    Nunca na thread de áudio, nunca travando a tela: request() volta na hora. */
class BeatKeyAnalyzer : private juce::Thread,
                        private juce::AsyncUpdater
{
public:
    struct Result
    {
        juce::File file;
        juce::String stamp;            // data e tamanho do arquivo: mudou, analisa de novo
        bool ok = false;               // false = não leu o arquivo (error diz por quê)
        juce::String error;
        double fileSeconds = 0.0;
        double millis = 0.0;           // quanto a análise levou
        ideas::BeatAnalysis analysis;
    };

    BeatKeyAnalyzer();
    ~BeatKeyAnalyzer() override;

    /** Pede a análise (se o arquivo ainda não foi analisado como está agora). Volta na hora. */
    void request (const juce::File&);

    /** O último resultado do arquivo, ou nullptr. */
    std::shared_ptr<const Result> find (const juce::File&) const;
    bool isPending (const juce::File&) const;
    bool isIdle() const;

    /** Pede e espera o resultado (até timeoutMs). Bloqueia quem chama: só para o comando com wait e o autoteste. */
    std::shared_ptr<const Result> waitFor (const juce::File&, int timeoutMs);

    /** Espera tudo que está na fila e entrega os avisos agora (a captura de tela usa). */
    void finishAll (int timeoutMs);

    /** Os beats do projeto, o principal primeiro: canais de áudio e samples de 8 s ou mais num sampler.
        Thread de mensagem (guarda a duração dos arquivos). */
    std::vector<BeatSource> beatsOf (const ProjectModel&);

    /** Uma análise terminou (thread de mensagem). */
    std::function<void (const juce::File&)> onAnalysed;

    static constexpr double maxSeconds = 150.0;   // só os 2 min 30 s do começo
    static constexpr double longSampleSeconds = 8.0;

private:
    void run() override;
    void handleAsyncUpdate() override;
    std::shared_ptr<Result> analyse (const juce::File&);
    double fileSeconds (const juce::File&);
    static juce::String stampOf (const juce::File&);

    mutable juce::CriticalSection lock;
    std::map<juce::String, std::shared_ptr<const Result>> results;   // pelo caminho completo
    std::deque<juce::File> queue;
    juce::StringArray pending;                                       // caminhos na fila ou analisando
    std::vector<juce::File> finished;                                // para avisar na thread de mensagem
    juce::WaitableEvent done;
    std::atomic<bool> cancel { false };
    std::map<juce::String, double> durations;                        // thread de mensagem
};

//==============================================================================
// O que o comando, o aviso e o piano roll mostram.

/** O nome que a tela mostra: "Lá menor". */
juce::String keyName (const ideas::KeyCandidate&);

/** "Tom do beat: Lá menor (84%)", ou "talvez" abaixo de 50%. */
juce::String beatKeyText (const ideas::BeatAnalysis&);

/** A dica com as alternativas e o relativo explicado (para o aviso e a dica do piano roll). */
juce::String beatKeyDetails (const ideas::BeatAnalysis&);

/** Os acordes da música nos compassos que o beat toca (até 128). */
std::vector<ideas::BarChord> songBarChords (const BeatKeyAnalyzer::Result&, const Project&, const BeatSource&);

/** O resultado do audio.detectKey em JSON. */
juce::var describeBeatKey (const BeatKeyAnalyzer::Result&, const Project&, const BeatSource&);

/** As dicas do piano roll para um padrão: o acorde do beat em cada compasso do padrão. O padrão
    conta a partir do primeiro bloco dele na playlist; sem bloco, a partir do começo do beat. */
std::vector<BeatHint> beatHintsForPattern (BeatKeyAnalyzer&, const ProjectModel&, const juce::String& patternId);

} // namespace whynot
