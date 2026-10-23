#pragma once

#include <JuceHeader.h>

#include "DeckPlayer.h"
#include "PreviewRenderer.h"
#include "ProjectIndex.h"

#include <array>
#include <set>
#include <vector>

namespace whynot::library
{

/** A tela Projetos sem a tela: o índice dos projetos, a fila de prévias e os decks.

    Mora no AppController, um por app. Só um projeto fica vivo no motor (o aberto); os outros entram
    como dado (o .wnl lido pelo WnlFormat) ou como prévia em áudio. Thread de mensagem.

    O índice fica em biblioteca.json, na pasta de dados: os projetos abertos por último e um cache do
    que se leu de cada .wnl (tamanho e data iguais = não relê).
*/
class ProjectLibrary : public juce::ChangeBroadcaster,
                       private juce::Timer
{
public:
    struct Host
    {
        tracktion::Engine& engine;
        SampleCache& cache;
        const PluginCatalog* catalog = nullptr;
        juce::File dataFolder;
        std::function<bool()> isPlaying;           // a música do projeto aberto está tocando
        std::function<bool()> isExporting;         // uma exportação em segundo plano está rodando
        std::function<void()> stopLive;            // tocar um deck para a música
        juce::AudioDeviceManager* devices = nullptr;   // nullptr = sem placa (modo sem janela)
    };

    /** Um projeto da lista. */
    struct Item
    {
        ProjectEntry entry;
        PreviewInfo preview;
        PreviewState state = PreviewState::none;
        float progress = 0.0f;   // renderizando: de 0 a 1
        juce::String error;      // a última prévia falhou
        bool recent = false;     // aberto ou salvo há pouco
        bool reference = false;  // um arquivo de áudio de fora (num combo): toca direto, sem prévia
    };

    explicit ProjectLibrary (Host);
    ~ProjectLibrary() override;

    //==========================================================================
    // Índice. As pastas varridas são as de base (a Música\WHYNOT LAB, com janela) mais as que a pessoa
    // acrescenta (guardadas no biblioteca.json).
    void setRoots (const juce::Array<juce::File>&);
    const juce::Array<juce::File>& getRoots() const noexcept   { return roots; }
    const juce::Array<juce::File>& getExtraFolders() const noexcept   { return extraFolders; }
    void addFolder (const juce::File&);
    void removeFolder (const juce::File&);

    /** Varre as pastas e os recentes numa thread de trabalho; a lista muda quando terminar. */
    void refresh();
    bool isScanning() const noexcept   { return scan != nullptr; }

    /** Do mais novo para o mais velho (data do arquivo). */
    const std::vector<Item>& items() const noexcept   { return list; }
    const Item* find (const juce::File&) const;

    /** O app abriu ou salvou o projeto: entra nos recentes e a entrada é relida. Salvar põe a prévia na fila. */
    void noteOpened (const juce::File&);
    void projectSaved (const juce::File&);

    /** Renderizar a prévia a cada Ctrl+S (decisão do usuário, 21/10). Com janela, ligado. */
    bool renderOnSave = true;

    //==========================================================================
    // Organizar (fatia 3). Estrela, cor, tags e status ficam no biblioteca.json, não no .wnl: o
    // esquema do projeto não muda, e o mesmo projeto pode ter uma estrela aqui e outra em outro PC.
    enum class Status { none, idea, production, mix, done };

    struct Meta
    {
        bool favorite = false;
        juce::String colour;        // "#RRGGBB" da paleta dos canais; vazio = sem cor
        juce::StringArray tags;
        Status status = Status::none;
        bool empty() const noexcept   { return ! favorite && colour.isEmpty() && tags.isEmpty() && status == Status::none; }
    };

    Meta metaOf (const juce::File&) const;
    void setFavorite (const juce::File&, bool);
    void setColour (const juce::File&, const juce::String& hex);
    void setTags (const juce::File&, const juce::StringArray&);
    void setStatus (const juce::File&, Status);

    /** Todas as tags em uso, em ordem alfabética, com quantos projetos têm cada uma. */
    std::vector<std::pair<juce::String, int>> allTags() const;
    int countWithStatus (Status) const;
    int countFavorites() const;

    static juce::String statusId (Status);                  // "ideia", "producao", "mix", "pronto" ou ""
    static juce::String statusLabel (Status);               // "Ideia", "Em produção"...
    static Status statusFromText (const juce::String&);     // aceita o id, o rótulo e "produção"

    /** O que a lista mostra: uma vista da barra lateral e a busca. */
    struct Filter
    {
        enum class View { all, recent, favorites, status, tag, folder, combo };
        View view = View::all;
        Status status = Status::none;
        juce::String tag;
        juce::File folder;
        juce::String combo;   // a vista de um combo: os itens na ordem dele
        juce::String query;   // texto livre e bpm:140-150 (acha também 70-75), tom:Am, tag:x, status:mix, fav
    };

    void setFilter (const Filter&);
    const Filter& getFilter() const noexcept   { return filter; }
    bool matches (const Item&, const Filter&) const;

    /** Uma linha da lista: as versões do mesmo projeto (Beat, Beat_2, Beat_3, na mesma pasta) ficam empilhadas,
        a mais nova em cima; aberta, a pilha mostra as outras logo abaixo. */
    struct Row
    {
        int index = -1;             // em items()
        int versions = 1;           // na cabeça da pilha: quantas versões (com o filtro)
        bool child = false;         // uma versão mais velha, com a pilha aberta
        bool expanded = false;
        juce::String stack;         // a chave da pilha (pasta + nome sem o _N)
        int extra = -1;             // num combo: um item fora da lista (referência, outra pasta)
        int comboIndex = -1;        // num combo: a posição do item
    };

    const std::vector<Row>& rows() const noexcept   { return visible; }

    /** O projeto (ou a referência) de uma linha. */
    const Item& itemFor (const Row&) const;
    void toggleStack (const juce::String& stack);

    /** A chave da pilha de versões: a pasta mais o nome sem o "_N" do fim. */
    static juce::String stackKey (const juce::File&);

    //==========================================================================
    // Combos (fatia 4): listas com ordem e uma nota por item. Cada combo é um .wnlcombo na pasta de dados
    // (combos\<nome>.wnlcombo). Um item é um projeto (numa versão fixa ou "a mais nova" da pilha) ou uma
    // referência externa.
    struct ComboItem
    {
        juce::File file;
        bool latest = false;   // segue a versão mais nova da pilha (Beat → Beat_3)
        juce::String note;
    };

    struct Combo
    {
        juce::String name;
        std::vector<ComboItem> items;
    };

    const std::vector<Combo>& combos() const noexcept   { return comboList; }
    const Combo* findCombo (const juce::String& name) const;
    juce::Result createCombo (const juce::String& name);
    juce::Result renameCombo (const juce::String& from, const juce::String& to);
    juce::Result deleteCombo (const juce::String& name);   // o arquivo vai para a lixeira
    juce::Result addToCombo (const juce::String& combo, const juce::File&, bool latest = false, const juce::String& note = {}, int index = -1);
    juce::Result removeFromCombo (const juce::String& combo, int index);
    juce::Result moveInCombo (const juce::String& combo, int from, int to);
    juce::Result setComboNote (const juce::String& combo, int index, const juce::String& note);
    juce::Result setComboLatest (const juce::String& combo, int index, bool latest);

    /** O arquivo que o item toca: com "a mais nova", a versão mais nova que existe na pilha dele. */
    juce::File resolve (const ComboItem&) const;

    /** Toca o combo em sequência no deck que soa, a partir do item `from`: quando um acaba, entra o próximo.
        Item sem prévia é pulado. */
    juce::Result playCombo (const juce::String& combo, int from = 0);
    void stopCombo();
    const juce::String& playingCombo() const noexcept   { return comboPlaying; }
    int playingComboIndex() const noexcept              { return comboPos; }

    juce::File comboFolder() const   { return host.dataFolder.getChildFile ("combos"); }
    juce::var describeCombos() const;

    //==========================================================================
    // Prévias
    void renderPreview (const juce::File& wnl, bool urgent = true);

    /** Todos sem prévia, desatualizados ou que falharam. Devolve quantos entraram na fila. */
    int renderPending();

    PreviewRenderer& getRenderer() noexcept   { return renderer; }

    /** Uma linha para a barra da tela: o que a fila está fazendo agora ("" = nada). */
    juce::String renderStatusText() const;

    //==========================================================================
    // Decks A e B (fatia 2). Os dois andam no mesmo relógio e a troca é só de ganho (20 ms). O volume
    // é igualado pelo LUFS integrado, pelo lado mais baixo. Um som por vez: a música aberta para o deck.

    /** Como o deck que entra acha o ponto do outro: mesmo compasso (o padrão; usa o BPM de cada .wnl),
        mesmo segundo, ou livre (cada um do seu jeito). */
    enum class Sync { bar, second, free };

    struct Deck
    {
        juce::File project;        // o .wnl (vazio numa referência externa)
        juce::File audio;          // o FLAC da prévia ou o arquivo da referência; vazio = deck vazio
        juce::String name;
        double bpm = 0.0;          // 0 = sem BPM (referência)
        double barSeconds = 0.0;   // 0 = compasso desconhecido
        float lufs = -100.0f;
        bool lufsKnown = false;    // referência: o LUFS chega quando a medição termina
        bool loaded() const noexcept   { return audio != juce::File(); }
    };

    /** O ▶ da lista: a prévia do projeto no deck que soa, e toca. fromSeconds < 0 = alinhado com o
        outro deck se ele estiver tocando (pela sincronia), senão do começo. */
    juce::Result playPreview (const juce::File& wnl, double fromSeconds = -1.0);

    /** Carrega no deck sem trocar o que soa: a prévia de um .wnl ou um arquivo de áudio de referência
        (WAV, MP3, FLAC...). Com o outro deck tocando, entra alinhado. */
    juce::Result loadDeck (int deck, const juce::File& wnlOrAudio, double fromSeconds = -1.0);
    void unloadDeck (int deck);

    void setActiveDeck (int deck);
    void toggleActiveDeck()                   { setActiveDeck (1 - active); }
    int getActiveDeck() const noexcept        { return active; }

    void setMatchLoudness (bool);
    bool getMatchLoudness() const noexcept    { return matchLoudness; }
    void setSync (Sync);
    Sync getSync() const noexcept             { return sync; }

    /** Loop de N compassos a partir do compasso de agora, nos dois decks (0 = desliga). */
    void setLoopBars (int bars);
    int getLoopBars() const noexcept          { return loopBars; }

    /** Quanto o volume igualado mexe no deck, em dB (0 = nada). */
    float matchGainDb (int deck) const;

    void toggleDeck();                        // tocar ou pausar os decks
    void stopDeck();                          // pausa e volta os dois ao início
    void seekDeck (double seconds)            { seekDeck (active, seconds); }
    void seekDeck (int deck, double seconds); // o outro deck vai junto, pela sincronia

    bool isDeckPlaying() const noexcept       { return decks.isPlaying(); }
    const Deck& deckInfo (int deck) const     { return info[(size_t) juce::jlimit (0, 1, deck)]; }
    double deckPosition (int deck) const      { return decks.positionSeconds (deck); }
    double deckLength (int deck) const        { return decks.lengthSeconds (deck); }

    // o deck que soa (a tela da fatia 1 e a lista usam estes)
    juce::File deckProject() const            { return info[(size_t) active].project; }
    double deckPosition() const               { return deckPosition (active); }
    double deckLength() const                 { return deckLength (active); }

    DeckPlayer& getDecks() noexcept           { return decks; }

    static juce::String syncName (Sync);

    //==========================================================================
    // Para os comandos e a IA
    juce::var describe() const;
    juce::var describeRows() const;   // as linhas à vista (com o filtro), para library.search
    juce::var deckStatus() const;

    static juce::String stateName (PreviewState);

    static constexpr int maxRecent = 30;

private:
    struct ScanJob;

    void timerCallback() override;
    void finishScan();
    void updateStates();
    void reread (const juce::File&);
    void addRecent (const juce::File&);
    void loadIndex();
    void saveIndex() const;
    void rebuildRows();
    juce::Array<juce::File> scanRoots() const;
    void metaChanged();
    void loadCombos();
    juce::Result saveCombo (const Combo&) const;
    Combo* comboNamed (const juce::String&);
    juce::File comboFile (const juce::String& name) const;
    void combosChanged (const Combo&);
    juce::Result advanceCombo();
    juce::File indexFile() const   { return host.dataFolder.getChildFile ("biblioteca.json"); }

    Host host;
    juce::AudioFormatManager formats;
    DeckPlayer decks;
    PreviewRenderer renderer;

    juce::Array<juce::File> roots, extraFolders;
    std::map<juce::String, Meta> meta;              // caminho → estrela, cor, tags e status
    Filter filter;
    std::vector<Row> visible;
    std::set<juce::String> openStacks;
    std::vector<Item> extras;                       // itens de combo fora da lista (referências, outras pastas)
    std::vector<Combo> comboList;                   // em ordem de nome
    juce::String comboPlaying;
    int comboPos = -1;
    juce::StringArray recent;                       // caminhos, o mais novo primeiro
    std::map<juce::String, ProjectEntry> known;     // cache do que se leu de cada .wnl
    std::vector<Item> list;
    std::unique_ptr<ScanJob> scan;
    bool rescan = false;                            // pediram outra varredura enquanto uma rodava

    struct Measure;

    double mapPosition (int from, int to, double seconds) const;
    DeckPlayer::Loop loopFor (int deck, double seconds) const;
    void loadAt (int deck, double seconds);   // o mesmo arquivo do deck, noutro ponto (refaz o loop se sair dele)
    void applyGains();
    void startMeasure (int deck, const juce::File&);

    std::array<Deck, DeckPlayer::numDecks> info;
    int active = 0;
    bool matchLoudness = true;
    Sync sync = Sync::bar;
    int loopBars = 0;
    std::unique_ptr<Measure> measuring;             // LUFS de uma referência externa, numa thread

    std::array<double, DeckPlayer::numDecks> reloadAt { -1.0, -1.0 };   // a prévia do deck foi refeita: volta aqui
    bool reloadPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectLibrary)
};

} // namespace whynot::library
