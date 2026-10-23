#pragma once

#include <JuceHeader.h>

#include "../app/Favorites.h"

namespace whynot::favui
{

/** A estrela dos favoritos (docs/design.md: star = não, starFilled = sim).
    Marcado: estrela cheia em laranja, como o visto dos menus. Não marcado: o contorno só aparece com o mouse
    na linha (hover) ou na linha escolhida, para a lista ficar limpa; com o mouse em cima da estrela, fica branco. */
void drawStar (juce::Graphics&, juce::Rectangle<float> area, bool on, bool rowHover, bool starHover = false);

/** Largura da coluna da estrela no fim de uma linha de lista (a estrela tem 16 px). */
constexpr int starColumn = 26;

/** A coluna da estrela dentro de uma linha: os starColumn px da direita de `row`. */
inline juce::Rectangle<int> starColumnOf (juce::Rectangle<int> row)   { return row.removeFromRight (starColumn); }

/** Segue a linha e a posição do mouse numa ListBox, para a estrela aparecer ao passar o mouse.
    `onChange` repinta quem desenha (as linhas antiga e nova já são repintadas aqui). */
class RowHover : private juce::MouseListener
{
public:
    explicit RowHover (juce::ListBox&);
    ~RowHover() override;

    int getRow() const noexcept                       { return row; }
    juce::Point<int> getPosition() const noexcept     { return position; }   // relativa à ListBox

    /** O mouse está sobre a área `column` (relativa à linha) da linha `r`? */
    bool isOver (int r, juce::Rectangle<int> column) const;

    /** O mesmo, com o botão apertado agora. Serve para o selectedRowsChanged saber que o clique foi na
        estrela (e não tocar a prévia nem carregar o preset): a ListBox escolhe a linha antes de avisar o clique. */
    bool isPressingIn (int r, juce::Rectangle<int> column) const;

private:
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void update (const juce::MouseEvent&);
    void setRow (int);

    juce::ListBox& list;
    int row = -1;
    juce::Point<int> position;
};

/** Item de menu com a estrela clicável à direita.
    - Clicar na estrela marca ou desmarca o favorito e o menu continua aberto (a estrela muda na hora).
    - Clicar no resto escolhe o item, como um item comum (o id volta no callback do showMenuAsync).
    - Com submenu, passar o mouse abre o submenu, como sempre; o clique no nome escolhe o item (se o id não for 0).
    O desenho é o do LookAndFeel (docs/design.md, Menus), com a estrela por cima. */
class MenuItem : public juce::PopupMenu::CustomComponent,
                 private Favorites::Listener
{
public:
    MenuItem (const juce::String& text, Favorites::Kind, const juce::String& id, bool ticked, bool hasSubMenu, bool triggers);
    ~MenuItem() override;

    void getIdealSize (int& width, int& height) override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Rectangle<int> starArea() const;

private:
    void favoritesChanged() override   { repaint(); }

    const juce::String text;
    const Favorites::Kind kind;
    const juce::String id;
    const bool ticked, subMenu, triggers;
    bool overStar = false;
};

/** Um item pronto para o PopupMenu::addItem: texto, estrela e (opcional) submenu e visto.
    itemId 0 = o clique no nome não escolhe nada (só abre o submenu). */
juce::PopupMenu::Item menuItem (int itemId, const juce::String& text, Favorites::Kind, const juce::String& id,
                                bool ticked = false, std::unique_ptr<juce::PopupMenu> subMenu = nullptr);

} // namespace whynot::favui
