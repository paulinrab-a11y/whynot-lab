#include "FavoriteStar.h"
#include "Icons.h"
#include "Theme.h"

namespace whynot::favui
{

void drawStar (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool rowHover, bool starHover)
{
    using namespace theme;
    const auto box = area.withSizeKeepingCentre (16.0f, 16.0f);

    if (on)
        icons::draw (g, Icon::starFilled, box, starHover ? colour::signal.brighter (0.25f) : colour::signal);
    else if (rowHover || starHover)
        icons::draw (g, Icon::star, box, starHover ? colour::text : colour::textDim);
}

//==============================================================================
RowHover::RowHover (juce::ListBox& l) : list (l)
{
    list.addMouseListener (this, true);
}

RowHover::~RowHover()
{
    list.removeMouseListener (this);
}

void RowHover::setRow (int r)
{
    if (r == row)
        return;

    list.repaintRow (row);
    row = r;
    list.repaintRow (row);
}

void RowHover::update (const juce::MouseEvent& e)
{
    position = e.getEventRelativeTo (&list).getPosition();
    const int r = list.getRowContainingPosition (position.x, position.y);
    setRow (r);
    list.repaintRow (row);   // a estrela acende quando o mouse chega nela
}

void RowHover::mouseMove (const juce::MouseEvent& e)    { update (e); }
void RowHover::mouseEnter (const juce::MouseEvent& e)   { update (e); }
void RowHover::mouseDown (const juce::MouseEvent& e)    { update (e); }
void RowHover::mouseDrag (const juce::MouseEvent& e)    { position = e.getEventRelativeTo (&list).getPosition(); }

void RowHover::mouseExit (const juce::MouseEvent& e)
{
    // passar de uma linha para outra também sai de um filho: só limpa quando sai da lista
    if (! list.getScreenBounds().contains (e.getScreenPosition()))
        setRow (-1);
}

bool RowHover::isOver (int r, juce::Rectangle<int> column) const
{
    if (r < 0 || r != row)
        return false;

    const auto rowBounds = list.getRowPosition (r, true);
    return column.translated (rowBounds.getX(), rowBounds.getY()).contains (position);
}

bool RowHover::isPressingIn (int r, juce::Rectangle<int> column) const
{
    if (r < 0 || ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return false;

    // a posição de agora, não a do último movimento: o aviso de seleção chega antes do mouseDown daqui
    const auto at = list.getMouseXYRelative();
    const auto rowBounds = list.getRowPosition (r, true);
    return rowBounds.contains (at) && column.translated (rowBounds.getX(), rowBounds.getY()).contains (at);
}

//==============================================================================
MenuItem::MenuItem (const juce::String& t, Favorites::Kind k, const juce::String& favId, bool isTicked, bool hasSubMenu, bool clickTriggers)
    : juce::PopupMenu::CustomComponent (false),   // o clique é tratado aqui: na estrela não fecha o menu
      text (t), kind (k), id (favId), ticked (isTicked), subMenu (hasSubMenu), triggers (clickTriggers)
{
    setRepaintsOnMouseActivity (true);
    Favorites::get().addListener (this);
}

MenuItem::~MenuItem()
{
    Favorites::get().removeListener (this);
}

void MenuItem::getIdealSize (int& width, int& height)
{
    getLookAndFeel().getIdealPopupMenuItemSize (text, false, 0, width, height);
    width += starColumn + 4;
}

juce::Rectangle<int> MenuItem::starArea() const
{
    // o mesmo recuo do LookAndFeel: 8 px de margem e, com submenu, a setinha de 14 px no fim
    auto r = getLocalBounds().reduced (8, 0);

    if (subMenu)
        r.removeFromRight (14 + 4);

    return r.removeFromRight (20);
}

void MenuItem::paint (juce::Graphics& g)
{
    // O ItemComponent do menu encolhe este componente 5 px de cada lado; o LookAndFeel desenha o item com
    // 4 px de recuo. Desenhando 4 px para fora, o fundo do item aceso ocupa a largura toda daqui.
    getLookAndFeel().drawPopupMenuItem (g, getLocalBounds().expanded (4, 0), false, true, isItemHighlighted(), ticked, subMenu,
                                        text, {}, nullptr, nullptr);
    drawStar (g, starArea().toFloat(), Favorites::get().contains (kind, id), isItemHighlighted(), overStar);
}

void MenuItem::mouseMove (const juce::MouseEvent& e)
{
    const bool over = starArea().expanded (4, 4).contains (e.getPosition());

    if (over != overStar)
    {
        overStar = over;
        repaint();
    }
}

void MenuItem::mouseExit (const juce::MouseEvent&)
{
    overStar = false;
    repaint();
}

void MenuItem::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()) || e.mods.isPopupMenu())
        return;

    if (starArea().expanded (4, 4).contains (e.getPosition()))
    {
        Favorites::get().toggle (kind, id);   // o ouvinte repinta; o menu continua aberto
        return;
    }

    if (triggers)
        triggerMenuItem();
}

//==============================================================================
juce::PopupMenu::Item menuItem (int itemId, const juce::String& text, Favorites::Kind kind, const juce::String& id,
                                bool ticked, std::unique_ptr<juce::PopupMenu> subMenu)
{
    juce::PopupMenu::Item item (text);
    item.itemID = itemId;
    item.isTicked = ticked;
    item.customComponent = new MenuItem (text, kind, id, ticked, subMenu != nullptr, itemId != 0);
    item.subMenu = std::move (subMenu);
    return item;
}

} // namespace whynot::favui
