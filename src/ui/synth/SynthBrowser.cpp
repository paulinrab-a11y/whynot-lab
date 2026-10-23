#include "SynthBrowser.h"
#include "SynthVisuals.h"
#include "../Theme.h"
#include "../../engine/SynthFavorites.h"

namespace whynot::synthui
{

namespace
{
    juce::String utf8 (const std::string& s)   { return juce::String::fromUTF8 (s.c_str()); }
    juce::String utf8 (const char* s)          { return juce::String::fromUTF8 (s); }

    constexpr int rowHeight = 30, chipHeight = 24;

    /** Rótulo curto da pílula de gênero; o nome inteiro fica na dica. */
    juce::String chipLabel (const synth::GenreInfo& g)
    {
        const juce::String id (g.id);

        if (id == "funk") return "Funk";
        if (id == "lofi") return "Lo-fi";

        return utf8 (g.name);
    }
}

//==============================================================================
Chip::Chip (const juce::String& text, bool withStar) : juce::Button (text), star (withStar)
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);
}

int Chip::idealWidth() const
{
    // Folga além das margens de 10 px: a largura medida fica uns pixels abaixo da desenhada, e o
    // drawText cortava com reticências ("Tr…").
    return (int) std::ceil (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::small, theme::Weight::medium), getButtonText()))
           + (star ? 42 : 28);
}

void Chip::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    using namespace theme;
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (on ? colour::signalTint : (highlighted ? colour::raisedHigh : colour::raised));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

    auto content = r.reduced (10.0f, 0.0f);

    if (star)
    {
        g.setColour (on ? colour::signal : colour::textDim);
        g.fillPath (starPath (content.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 12.0f)));
        content.removeFromLeft (4.0f);
    }

    g.setColour (on ? colour::signal : colour::textDim);
    g.setFont (font (type::small, Weight::medium));
    g.drawText (getButtonText(), content, star ? juce::Justification::centredLeft : juce::Justification::centred);
}

//==============================================================================
SynthBrowser::SynthBrowser()
{
    using namespace theme;

    search.setTextToShowWhenEmpty ("Buscar: 808, sino, escuro, flauta...", colour::textFaint);
    search.setColour (juce::TextEditor::backgroundColourId, colour::raised);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, colour::raisedHigh);
    search.setColour (juce::TextEditor::textColourId, colour::text);
    search.setColour (juce::CaretComponent::caretColourId, colour::signal);
    search.setFont (font (type::body));
    search.setIndents (32, 8);
    search.setTooltip ("Procura no nome e na descrição dos presets (sem diferenciar acento)."_t);
    search.onTextChange = [this] { filter(); };
    search.onEscapeKey = [this] { search.clear(); filter(); };
    addAndMakeVisible (search);

    theme::styleComboBox (category);
    category.addItem ("Todas as categorias", 1);

    int id = 2;

    for (auto& c : synth::allCategories())
        category.addItem (utf8 (c.name) + "  (" + juce::String ((int) synth::findPresets (c.id, "", "").size()) + ")", id++);

    category.setSelectedId (1, juce::dontSendNotification);
    category.setTooltip ("Tipo de som");
    category.onChange = [this] { filter(); };
    addAndMakeVisible (category);

    for (auto& g : synth::allGenres())
    {
        auto* chip = genreChips.add (new Chip (chipLabel (g)));
        chip->setTooltip ("Presets pensados para "_t + utf8 (g.name) + ". Clique de novo para ver todos.");
        const juce::String gid (g.id);
        chip->onClick = [this, chip, gid]
        {
            genre = chip->getToggleState() ? gid : juce::String();

            for (auto* other : genreChips)
                if (other != chip)
                    other->setToggleState (false, juce::dontSendNotification);

            filter();
        };
        addAndMakeVisible (chip);
    }

    favourites.setTooltip ("Só os presets marcados com estrela"_t);
    favourites.onClick = [this] { filter(); };
    addAndMakeVisible (favourites);

    list.setModel (this);
    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    list.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);

    count.setFont (captionFont());
    count.setColour (juce::Label::textColourId, colour::textFaint);
    count.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (count);

    Favorites::get().addListener (this);
    filter();
}

SynthBrowser::~SynthBrowser()
{
    Favorites::get().removeListener (this);
    list.setModel (nullptr);
}

void SynthBrowser::favoritesChanged()
{
    // Com o filtro ligado, o preset desmarcado sai da lista; sem filtro, só a estrela muda.
    if (favourites.getToggleState())
        filter();
    else
        list.repaint();

    if (onFavouritesChanged)
        onFavouritesChanged();
}

void SynthBrowser::setFavouritesOnly (bool on)
{
    favourites.setToggleState (on, juce::dontSendNotification);
    filter();
}

int SynthBrowser::rowOf (const juce::String& presetId) const
{
    for (size_t i = 0; i < shown.size(); ++i)
        if (utf8 (shown[i]->id) == presetId)
            return (int) i;

    return -1;
}

juce::Rectangle<int> SynthBrowser::starColumn (int width, int height) const
{
    // a estrela (18 px) e a margem da direita da linha (14 px), para o clique não exigir mira
    return { width - 14 - 18 - 4, 0, 14 + 18 + 4, height };
}

void SynthBrowser::filter()
{
    const int catIndex = category.getSelectedId() - 2;
    const auto cats = synth::allCategories();
    const std::string cat = juce::isPositiveAndBelow (catIndex, (int) cats.size()) ? cats[(size_t) catIndex].id : "";
    const auto found = synth::findPresets (cat, genre.toStdString(), search.getText().toStdString());

    shown.clear();

    for (auto* p : found)
        if (! favourites.getToggleState() || synthfavorites::isFavorite (utf8 (p->id)))
            shown.push_back (p);

    count.setText (juce::String ((int) shown.size()) + " de " + juce::String ((int) synth::allPresets().size()), juce::dontSendNotification);
    list.updateContent();
    setCurrent (current);
    list.repaint();
}

void SynthBrowser::refresh()
{
    filter();
}

void SynthBrowser::setCurrent (const juce::String& presetId)
{
    current = presetId;
    const juce::ScopedValueSetter<bool> guard (updating, true);

    for (size_t i = 0; i < shown.size(); ++i)
    {
        if (utf8 (shown[i]->id) == presetId)
        {
            // Já escolhido: não rola a lista de volta (o usuário pode estar olhando outro trecho).
            if (list.getSelectedRow() != (int) i)
                list.selectRow ((int) i, false, true);

            return;
        }
    }

    list.deselectAllRows();
}

juce::StringArray SynthBrowser::visibleIds() const
{
    juce::StringArray ids;

    for (auto* p : shown)
        ids.add (utf8 (p->id));

    return ids;
}

int SynthBrowser::getNumRows()
{
    return (int) shown.size();
}

void SynthBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    using namespace theme;

    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;

    const auto& p = *shown[(size_t) row];
    auto r = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (2.0f, 1.0f);

    if (selected)
    {
        g.setColour (colour::raisedHigh);
        g.fillRoundedRectangle (r, metric::radiusSmall);
        g.setColour (colour::signal);
        g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 7.0f), 1.5f);
    }

    auto text = r.reduced (12.0f, 0.0f);

    // estrela no fim da linha: cheia se é favorito; o contorno aparece com o mouse na linha (o Init não tem)
    const auto starBox = text.removeFromRight (18.0f);
    text.removeFromRight (6.0f);

    if (p.id != "init")
        favui::drawStar (g, starBox, synthfavorites::isFavorite (utf8 (p.id)), selected || hover.getRow() == row,
                         hover.isOver (row, starColumn (width, height)));

    const auto* cat = synth::findCategory (p.category);
    g.setFont (font (type::small));
    g.setColour (colour::textFaint);
    g.drawText (cat != nullptr ? utf8 (cat->name) : juce::String(), text.removeFromRight (78.0f), juce::Justification::centredRight);

    g.setFont (font (type::body, selected ? Weight::semibold : Weight::medium));
    g.setColour (selected ? colour::text : colour::textDim.brighter (0.25f));
    g.drawFittedText (utf8 (p.name), text.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

void SynthBrowser::selectedRowsChanged (int row)
{
    if (updating || ! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;

    // Clique na estrela: a ListBox já escolheu a linha, mas só se marca o favorito. A seleção volta ao preset
    // do canal, e o preset clicado não carrega.
    if (hover.isPressingIn (row, starColumn (list.getVisibleRowWidth(), list.getRowHeight())))
    {
        setCurrent (current);
        return;
    }

    current = utf8 (shown[(size_t) row]->id);

    if (onChoose)
        onChoose (current);
}

void SynthBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;

    const auto id = utf8 (shown[(size_t) row]->id);

    if (starColumn (list.getVisibleRowWidth(), list.getRowHeight()).contains (e.getPosition()))
    {
        if (id != "init")
            synthfavorites::set (id, ! synthfavorites::isFavorite (id));   // o ouvinte refaz a lista e o editor

        return;
    }

    // Clicar no preset que já está escolhido toca a prévia de novo.
    if (id == current && onChoose)
        onChoose (current);
}

void SynthBrowser::returnKeyPressed (int row)
{
    if (juce::isPositiveAndBelow (row, (int) shown.size()) && onChoose)
        onChoose (utf8 (shown[(size_t) row]->id));
}

juce::String SynthBrowser::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return {};

    const auto& p = *shown[(size_t) row];
    juce::StringArray genres;

    for (auto& g : synth::allGenres())
        if (p.hasGenre (g.id))
            genres.add (utf8 (g.name));

    return utf8 (p.description) + " (" + genres.joinIntoString (", ") + ")";
}

//==============================================================================
void SynthBrowser::paint (juce::Graphics& g)
{
    using namespace theme;
    g.setColour (colour::textFaint);
    g.setFont (captionFont());
    g.drawText ("PRESETS", getLocalBounds().removeFromTop (20), juce::Justification::centredLeft);

    // lupa dentro do campo de busca
    icons::draw (g, Icon::search, search.getBounds().toFloat().removeFromLeft (32.0f).withSizeKeepingCentre (16.0f, 16.0f), colour::textFaint);
}

void SynthBrowser::resized()
{
    using namespace theme;
    auto r = getLocalBounds();
    auto top = r.removeFromTop (20);
    count.setBounds (top);

    r.removeFromTop (4);
    search.setBounds (r.removeFromTop (metric::control));
    r.removeFromTop (metric::gap);
    category.setBounds (r.removeFromTop (metric::control));
    r.removeFromTop (metric::gap);

    // pílulas de gênero em linhas que se quebram
    int x = r.getX(), y = r.getY();
    std::vector<Chip*> chips (genreChips.begin(), genreChips.end());
    chips.push_back (&favourites);

    for (auto* chip : chips)
    {
        const int w = chip->idealWidth();

        if (x + w > r.getRight())
        {
            x = r.getX();
            y += chipHeight + 6;
        }

        chip->setBounds (x, y, w, chipHeight);
        x += w + 6;
    }

    r.setTop (y + chipHeight + metric::gap);
    list.setBounds (r);
}

} // namespace whynot::synthui
