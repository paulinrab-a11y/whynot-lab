#pragma once

#include <JuceHeader.h>

#include "../../synth/SynthPresets.h"
#include "../FavoriteStar.h"

namespace whynot::synthui
{

/** Pílula que liga e desliga (gênero, favoritos). */
class Chip : public juce::Button
{
public:
    explicit Chip (const juce::String& text, bool withStar = false);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    int idealWidth() const;

private:
    bool star;
};

/** Navegador de presets do WHYNOT Synth: busca, categoria, gênero e favoritos.
    Clicar num preset (ou andar com as setas) já carrega e toca uma prévia.
    A estrela no fim da linha marca o favorito (favoritos.json) sem carregar o preset. */
class SynthBrowser : public juce::Component,
                     private juce::ListBoxModel,
                     private Favorites::Listener
{
public:
    SynthBrowser();
    ~SynthBrowser() override;

    /** Preset do canal (fica destacado); modified = mexido depois de carregar. */
    void setCurrent (const juce::String& presetId);

    /** Refaz a lista (favoritos mudaram, por exemplo). */
    void refresh();

    /** Ids na ordem da lista filtrada (as setas do cabeçalho andam por ela). */
    juce::StringArray visibleIds() const;

    std::function<void (const juce::String& presetId)> onChoose;

    /** Algum favorito mudou (pela estrela da lista, por outra tela ou pela IA): o editor atualiza a estrela dele. */
    std::function<void()> onFavouritesChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Para o autoteste: só os favoritos, a linha de um preset (-1 = fora da lista) e a lista. */
    void setFavouritesOnly (bool);
    int rowOf (const juce::String& presetId) const;
    juce::ListBox& getList() noexcept   { return list; }

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    juce::String getTooltipForRow (int row) override;
    void favoritesChanged() override;
    juce::Rectangle<int> starColumn (int width, int height) const;

    void filter();

    juce::TextEditor search;
    juce::ComboBox category;
    juce::OwnedArray<Chip> genreChips;
    Chip favourites { "Favoritos", true };
    juce::ListBox list;
    favui::RowHover hover { list };
    juce::Label count;

    std::vector<const synth::Preset*> shown;
    juce::String current, genre;
    bool updating = false;
};

} // namespace whynot::synthui
