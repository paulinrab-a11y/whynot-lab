#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "FavoriteStar.h"
#include "../engine/SoundInfo.h"

#include <set>

namespace whynot
{

class AppController;

/** Navegador de sons (Alt+F8): o Kit WHYNOT, a Biblioteca (Músicas/WHYNOT LAB/Biblioteca, onde o
    usuário coloca drum kits, loops e one-shots) e as pastas extras que ele adicionar.

    Clique num som ouve a prévia; setas também. Arraste para o channel rack (canal novo, ou troca
    o sample se soltar no nome) ou para a playlist (clipe de áudio). Duplo clique ou Enter cria um canal.
    A busca procura em todas as pastas, num índice montado em segundo plano.
    A estrela no fim da linha marca o som ou a pasta (kit) como favorito; a estrela do cabeçalho mostra só os favoritos.
    Cada som mostra o BPM e o tom (ou a nota, num one-shot), pelo nome ou pela leitura do áudio em segundo plano
    (bloco 4b, 23/10). O filtro do cabeçalho mostra só os que combinam com o projeto: o tom, o BPM, loops ou one-shots.
*/
class SampleBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::Timer,
                      private Favorites::Listener
{
public:
    explicit SampleBrowser (AppController&);
    ~SampleBrowser() override;

    std::function<void (const juce::String&, bool isError)> onStatus;

    /** Prefixo da descrição de arrasto: "sample:" + caminho completo do arquivo. */
    static constexpr const char* dragPrefix = "sample:";

    /** Extensões que o navegador mostra e que o rack e a playlist aceitam. */
    static constexpr const char* audioWildcard = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";

    /** A pasta onde o usuário põe os sons dele (criada na primeira vez). */
    juce::File getLibraryFolder() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    /** Só os favoritos (a estrela do cabeçalho): as pastas marcadas e os sons marcados. */
    void setFavouritesOnly (bool);
    bool isFavouritesOnly() const noexcept   { return favouritesButton.getToggleState(); }

    /** O filtro (bloco 4b): o tom do projeto (ou também os vizinhos), o BPM perto do projeto, só loops ou só one-shots. */
    void setFilters (soundinfo::KeyFilter, soundinfo::BpmFilter, soundinfo::KindFilter);
    bool isFiltering() const noexcept;

    /** Para o autoteste: o índice da busca e do filtro, sem ler as pastas (as de verdade são as suas). */
    void setIndexForTest (juce::Array<juce::File>);

    /** Para o autoteste: a linha desse arquivo ou pasta (-1 = não está na lista) e a lista. */
    int rowOf (const juce::File&) const;
    juce::ListBox& getList() noexcept        { return list; }

private:
    enum class Kind { root, folder, file, hint, caption };

    struct Node
    {
        Kind kind = Kind::file;
        juce::File file;
        juce::String label, detail;
        int depth = 0;
        bool removable = false;   // pasta extra (sai da lista pelo menu)
    };

    struct Root
    {
        juce::File folder;
        juce::String label;
        bool removable = false;
    };

    class Indexer;

    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void returnKeyPressed (int lastRowSelected) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
    juce::String getTooltipForRow (int row) override;

    void timerCallback() override;
    void favoritesChanged() override;

    void loadSettings();
    void saveSettings();
    void rebuildRoots();
    void rebuildRows();
    void appendChildren (const juce::File& folder, int depth);
    void toggle (int row);
    void preview (int row);
    void addToRack (const juce::File&);
    void sliceToRack (const juce::File&);
    void addKitToRack (const juce::File& folder);
    void showRowMenu (int row);
    void chooseFolderToAdd();
    void startIndexing();
    void indexReady (juce::Array<juce::File>);
    bool isSearching() const;
    juce::Rectangle<int> searchArea() const;
    void appendFavourites();
    bool canStar (const Node&) const;
    bool isFavourite (const Node&) const;
    void toggleFavourite (const Node&);
    juce::Rectangle<int> starColumn (int width, int height) const;

    // o filtro por tom e BPM (bloco 4b)
    void showFilterMenu();
    void soundsAnalysed();
    bool filterPasses (const SoundInfo&) const;
    juce::String filterCaption() const;
    void updateFilterButton();

    AppController& app;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::vector<Root> roots;
    juce::StringArray extraFolders;
    std::set<juce::String> expanded;
    std::vector<Node> rows;

    juce::Array<juce::File> index;
    std::unique_ptr<Indexer> indexer;
    bool indexing = false;

    juce::String previewPath;
    bool previewing = false;
    juce::uint32 lastSelectionChange = 0;

    IconButton favouritesButton { "Favoritos", Icon::star };
    IconButton filterButton { "Filtrar por tom e BPM", Icon::note };
    soundinfo::KeyFilter keyFilter = soundinfo::KeyFilter::any;
    soundinfo::BpmFilter bpmFilter = soundinfo::BpmFilter::any;
    soundinfo::KindFilter kindFilter = soundinfo::KindFilter::any;
    int filterPending = 0, filterMatched = 0;
    bool rebuildQueued = false;   // com o filtro, a lista se refaz no máximo a cada 250 ms (1 s numa biblioteca grande) enquanto os sons são lidos
    bool restoringSelection = false;   // a lista refeita devolve a seleção ao mesmo arquivo, sem tocar a prévia
    IconButton openButton { "Abrir pasta", Icon::folder };
    IconButton addButton { "Adicionar pasta", Icon::plus };
    juce::TextEditor search;
    juce::ListBox list { "sons", this };
    favui::RowHover hover { list };
    std::unique_ptr<juce::FileChooser> chooser;

    static constexpr int headerHeight = theme::metric::toolbar, footerHeight = 66, rowHeight = 28, indent = 14;
    static constexpr int maxResults = 500, maxIndexed = 50000;
};

} // namespace whynot
