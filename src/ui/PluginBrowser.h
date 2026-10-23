#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "FavoriteStar.h"

namespace whynot
{

class AppController;

/** Navegador de plugins (F8): instrumentos e efeitos que o scan achou, busca, escanear,
    pastas e plugins bloqueados. Arraste um instrumento para o channel rack (ou dê duplo clique).
    A estrela no fim da linha marca o favorito (favoritos.json); a estrela do cabeçalho mostra só os favoritos.
*/
class PluginBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private Favorites::Listener
{
public:
    explicit PluginBrowser (AppController&);
    ~PluginBrowser() override;

    /** Relê a lista do app (depois de um scan, bloqueio ou desbloqueio). */
    void refresh();

    /** Texto e andamento do scan; progress < 0 = parado. */
    void setScanStatus (const juce::String& text, double progress);

    std::function<void (const juce::String&, bool isError)> onStatus;
    std::function<void (const juce::PluginDescription&)> onAddInstrument;
    std::function<void (const juce::PluginDescription&)> onAddEffect;

    /** Prefixo da descrição de arrasto: "plugin:" + PluginDescription::createIdentifierString(). */
    static constexpr const char* dragPrefix = "plugin:";

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Só os favoritos (a estrela do cabeçalho). */
    void setFavouritesOnly (bool);
    bool isFavouritesOnly() const noexcept   { return favouritesButton.getToggleState(); }

    /** Para o autoteste: a linha do plugin com esse id (-1 = não está na lista) e a lista. */
    int rowOf (const juce::String& pluginId) const;
    juce::ListBox& getList() noexcept        { return list; }

private:
    struct Row
    {
        bool header = false;
        juce::String text;
        juce::PluginDescription desc;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
    juce::String getTooltipForRow (int row) override;

    void favoritesChanged() override;

    void rebuildRows();
    void startScan();
    void showFoldersMenu();
    void showBlockedMenu();
    void showRowMenu (int row);
    void addPlugin (const juce::PluginDescription&);
    bool isFavourite (const juce::PluginDescription&) const;
    juce::Rectangle<int> starColumn (int width, int height) const;

    AppController& app;
    juce::Array<juce::PluginDescription> all;
    std::vector<Row> rows;
    int numBlocked = 0;

    IconButton favouritesButton { "Favoritos", Icon::star };
    IconButton scanButton { "Escanear", Icon::search, "Escanear", IconButton::Style::filled };
    IconButton foldersButton { "Pastas", Icon::folder, "Pastas" };
    IconButton blockedButton { "Bloqueados", Icon::warning, "Bloqueados" };
    juce::TextEditor search;
    juce::ListBox list { "plugins", this };
    favui::RowHover hover { list };
    juce::String scanText;
    double scanProgress = -1.0;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> searchArea() const;

    static constexpr int headerHeight = theme::metric::toolbar, footerHeight = 76;
};

} // namespace whynot
