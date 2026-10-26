#pragma once

// O que se sabe de um arquivo de som para o navegador de sons e a IA (bloco 4b, 23/10): a duração, o BPM e o tom, ou
// a nota de um one-shot (808, baixo). O nome vale primeiro ("Loop Am 140bpm"), como os produtores mandam os sons; sem
// ele, a leitura do áudio numa thread de trabalho, guardada num cache em disco (o mesmo arquivo, com o mesmo tamanho
// e a mesma data, não é lido de novo).

#include <JuceHeader.h>

#include <atomic>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace whynot
{

struct SoundInfo
{
    juce::String stamp;          // tamanho e data do arquivo quando foi lido; vazio = só o nome
    bool analysed = false;       // a leitura do áudio já rodou
    bool readable = true;        // o áudio abriu
    double seconds = 0.0;        // 0 = ainda não leu
    double bpm = 0.0;            // 0 = sem andamento (one-shot, ou não deu)
    juce::String bpmSource;      // "nome" ou "análise"
    int keyRoot = -1;            // 0 = Dó ... 11 = Si; -1 = sem tom
    bool keyMajor = false;
    bool isNote = false;         // um one-shot com nota: o keyRoot é a nota, sem maior nem menor
    juce::String keySource;      // "nome" ou "análise"
    juce::String role;           // "kick", "808", "loop"... (o roleId do SampleAnalysis)

    bool hasKey() const noexcept   { return keyRoot >= 0; }
    bool isLoop() const noexcept   { return bpm > 0.0; }

    /** "Lá menor", "Fá" (a nota de um one-shot) ou vazio. */
    juce::String keyText() const;

    /** O que a linha do navegador mostra: "140 BPM · Lá menor", "Fá", ou vazio. */
    juce::String tags() const;

    /** Para os comandos: { path, name, seconds, bpm, bpmSource, key, keySource, note, loop, role, analysed }. */
    juce::var toVar (const juce::File&) const;
};

namespace soundinfo
{
    /** O tom escrito no nome: "Am", "C#min", "Bbmaj", "F minor", "E-minor", "Lá menor"... Uma letra sozinha não conta
        (pode ser o take, a versão), nem palavras como "Amen". A forma por extenso ("Lá menor", "A minor", "Cmaj") vence
        a curta ("Am"): em "Loop Em Lá Menor", o "Em" é a preposição. Um "M" maiúsculo colado ("AM") só vale num nome
        todo em maiúsculas, e como menor ("LOOP_AM_120"); "FM" e "GM" são síntese e General MIDI, não tom.
        Devolve a tônica (0 = Dó) e se é maior. */
    std::optional<std::pair<int, bool>> keyFromName (const juce::String& name);

    /** Só pelo nome (BPM e tom), sem abrir o arquivo. */
    SoundInfo fromName (const juce::File&);

    /** A leitura do áudio (até 60 s): a duração, o BPM (um loop curto é repetido até dar para o detector; o
        tamanho dele tem de fechar em tempos inteiros), o tom de um loop com nota e a nota de um one-shot que não é
        bateria. O que o nome já diz fica. */
    SoundInfo analyse (const juce::File&, const std::atomic<bool>* cancel = nullptr);

    /** O tamanho e a data do arquivo: mudou, lê de novo. */
    juce::String stampOf (const juce::File&);

    enum class KeyFilter { any, project, compatible };
    enum class BpmFilter { any, project };
    enum class KindFilter { any, loops, oneShots };

    /** O tom (ou a nota) cabe no tom do projeto? A escala do projeto vem em graus (Project::scaleDegrees: maior,
        menor, dórico, frígio, menor harmônica, pentatônica menor, cromática).
        - project: as mesmas notas (o mesmo tom, o relativo, ou o tom de um modo: Ré dórico aceita Dó maior e Lá
          menor); numa escala de 7 notas que não é de um tom (a menor harmônica), também o tom da mesma tônica que
          muda uma nota só; na pentatônica, o tom dela e o relativo. A nota de um one-shot: dentro da escala.
        - compatible: também os vizinhos (os tons que mudam uma nota só: uma quinta acima e abaixo, com os
          relativos); na pentatônica, todo tom que tem as 5 notas.
        Na cromática, todo tom serve. */
    bool keyFits (const SoundInfo&, int projectRoot, const std::vector<int>& projectDegrees, KeyFilter);

    /** O mesmo, com a escala maior ou a menor natural. */
    bool keyFits (const SoundInfo&, int projectRoot, bool projectMajor, KeyFilter);

    /** Os tons (tônica, maior) que passam no filtro, de Dó a Si, para o menu dizer quais são. */
    std::vector<std::pair<int, bool>> keysThatFit (int projectRoot, const std::vector<int>& projectDegrees, KeyFilter);

    /** O tom do projeto em português: "Lá menor", "Ré dórico", "Mi menor harmônica", "Lá pentatônica menor". */
    juce::String projectKeyText (int root, const juce::String& scale);

    /** O arquivo só existe na nuvem (OneDrive, "Arquivos sob demanda")? Abrir baixaria o arquivo: a leitura pula. */
    bool isCloudOnly (const juce::File&);

    /** A até 4% do BPM do projeto, da metade ou do dobro dele. */
    bool bpmFits (double bpm, double projectBpm, BpmFilter);

    bool kindFits (const SoundInfo&, KindFilter);
}

/** O cache do que já se sabe dos sons, com a thread que lê os arquivos. Um por app (o AppController). */
class SoundInfoCache : private juce::Thread,
                       private juce::AsyncUpdater
{
public:
    explicit SoundInfoCache (juce::File cacheFile);
    ~SoundInfoCache() override;

    /** O que se sabe agora (no mínimo, o nome). Não espera nem lê o arquivo. */
    SoundInfo get (const juce::File&) const;

    /** Só o que já foi lido (sem o nome): para varrer milhares de arquivos sem ler o nome de cada um. */
    std::optional<SoundInfo> find (const juce::File&) const;

    /** Pede a leitura (o último pedido vai primeiro: o que está na tela). Um arquivo já conferido nesta sessão,
        ou na fila, não entra de novo. */
    void request (const juce::File&);

    /** Pede vários de uma vez (uma trava só), na ordem da lista: o primeiro é lido primeiro. A fila guarda até
        maxQueue; quando ela esvazia, onAnalysed avisa, e quem pediu pede o resto. */
    void requestMany (const std::vector<juce::File>&);

    /** O que se sabe de vários arquivos de uma vez (uma trava só): o que já foi lido (ou nada) e se já foi
        conferido nesta sessão. Para o filtro do navegador varrer o índice inteiro. */
    struct Status
    {
        std::optional<SoundInfo> info;
        bool settled = false;
    };

    std::vector<Status> statusOf (const juce::Array<juce::File>&) const;

    /** Pede na frente e espera (até timeoutMs). Bloqueia quem chama: só o comando e o autoteste. Um arquivo que
        mudou (tamanho ou data) desde a leitura desta sessão é lido de novo. */
    SoundInfo waitFor (const juce::File&, int timeoutMs);

    /** Já foi conferido nesta sessão (lido agora, ou o do cache ainda vale)? */
    bool isSettled (const juce::File&) const;

    int pending() const;

    /** Uma ou mais leituras terminaram, ou a fila esvaziou (thread de mensagem, juntando as que chegam juntas). */
    std::function<void()> onAnalysed;

    /** Grava o cache em disco. A thread de leitura grava sozinha a cada 50 leituras e quando a fila esvazia; o
        destrutor grava no fim. A montagem do texto fica fora da trava. */
    void save();

    const juce::File& getCacheFile() const noexcept   { return file; }

private:
    void run() override;
    void handleAsyncUpdate() override;
    void load();
    static juce::String keyOf (const juce::File&);

    juce::File file;
    mutable juce::CriticalSection lock;
    std::map<juce::String, SoundInfo> known;     // pelo caminho em minúsculas
    std::map<juce::String, juce::String> paths;  // o caminho como ele é
    std::deque<juce::File> queue;
    std::set<juce::String> queued, settled;
    juce::WaitableEvent wake;
    std::atomic<bool> stopping { false };
    int unsaved = 0;
    juce::CriticalSection saveLock;   // uma gravação de cada vez (a thread de leitura e o destrutor)

    static constexpr int maxEntries = 100000;
public:
    static constexpr int maxQueue = 4000;
private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundInfoCache)
};

} // namespace whynot
