#include "CommandPalette.h"

#include "Icons.h"
#include "../app/AppController.h"
#include "../app/KeyMap.h"
#include "../common/Text.h"

namespace whynot
{

namespace
{
    constexpr int searchHeight = theme::metric::toolbar;   // a faixa do campo de busca
    constexpr int listTop = 4, listBottom = 8;             // respiro em cima e embaixo da lista

    // Comandos que uma ação do KeyMap já cobre com outro id (a ação passa pela tela: pergunta se salva, mostra o
    // aviso no pé). Os de id igual ao da ação saem sozinhos.
    constexpr const char* coveredByAction[] = {
        "library.switchDeck",        // projects.switchDeck
        "project.new",               // file.new (pergunta antes de largar o projeto aberto)
        "project.save",              // file.save (pergunta onde, num projeto nunca salvo)
        "project.saveNewVersion",    // file.saveNewVersion
        "record.setCountIn",         // transport.countIn
        "transport.setSongMode",     // transport.songMode
    };

    // Comandos sem parâmetro obrigatório que não servem numa busca de ações: as consultas da IA (devolvem dados e
    // nada muda na tela) e os que, sem dizer o alvo, não fazem nada ou apagam uma preferência.
    constexpr const char* notAnAction[] = {
        "audio.detectKey", "audio.devices", "audio.findSilence", "audio.normalize", "audio.performance",
        "audio.removeSilence", "audio.setDevice", "automation.list", "automation.targets", "browser.find",
        "export.producerTag", "export.setProducerTag", "export.status", "favorites.list", "fx.list",
        "ideas.detectChords", "ideas.options", "ideas.progressions", "library.combos", "library.deck", "library.list",
        "library.search", "midi.inputs", "midi.mappings", "midi.unmap", "mixer.listChains", "piano.listPresets",
        "piano.status", "plugin.openEditor", "plugin.parameters", "plugins.blocked", "plugins.list", "record.arm",
        "record.commitTake", "record.guide", "record.inputDevices", "record.setInput", "record.state", "shortcuts.list",
        "synth.listPresets", "synth.params", "takes.list",
    };

    template <std::size_t n>
    bool listed (const char* const (&list)[n], const juce::String& id)
    {
        for (auto* entry : list)
            if (id == entry)
                return true;

        return false;
    }

    // o schema do comando pede algum parâmetro? (params = { type, properties, required: [...] })
    bool needsParams (const juce::var& schema)
    {
        const auto* required = schema["required"].getArray();
        return required != nullptr && ! required->isEmpty();
    }
}

CommandPalette::CommandPalette (AppController& a) : app (a)
{
    search.setFont (theme::font (theme::type::title));
    search.setIndents (36, 10);
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.setPopupMenuEnabled (false);
    search.onTextChange = [this] { refilter(); };
    search.addKeyListener (&keys);   // antes do campo: as setas andam na lista em vez de mexer no cursor

    // O clique fora (ou o Alt+Tab) fecha. A conferência fica para depois do evento: o foco pode só ter trocado de
    // lugar aqui dentro, e fechar no meio do clique numa linha perderia o clique.
    search.onFocusLost = [this]
    {
        juce::Component::SafePointer<CommandPalette> safe (this);

        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr && safe->isVisible() && ! safe->hasKeyboardFocus (true))
                safe->close();
        });
    };

    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setMultipleSelectionEnabled (false);
    list.setWantsKeyboardFocus (false);   // o foco fica no campo: dá para digitar e andar na lista ao mesmo tempo

    addAndMakeVisible (search);
    addAndMakeVisible (list);
    setVisible (false);
}

CommandPalette::~CommandPalette()
{
    search.removeKeyListener (&keys);
    list.setModel (nullptr);
}

//==============================================================================
void CommandPalette::rebuildItems()
{
    items.clear();
    auto& keyMap = app.getKeyMap();

    // as ações do KeyMap, na ordem do editor de atalhos (nenhuma pede parâmetro)
    for (auto& action : KeyMap::actions())
        if (action.id != actionId)
            items.push_back ({ action.id, action.title, keyMap.describeKeys (action.id), true });

    // os comandos do registro que rodam sem parâmetro obrigatório, em ordem alfabética de id (a do describe)
    const auto described = app.getCommands().describe();

    if (const auto* commands = described.getArray())
    {
        for (auto& command : *commands)
        {
            const auto id = command["id"].toString();

            if (id.isEmpty() || id == actionId || needsParams (command["params"]) || KeyMap::findAction (id) != nullptr
                || listed (coveredByAction, id) || listed (notAnAction, id))
                continue;

            items.push_back ({ id, command["title"].toString(), {}, false });
        }
    }

    entries.clear();
    entries.reserve (items.size());

    for (auto& item : items)
        entries.push_back ({ item.id.toStdString(), item.title.toStdString() });
}

void CommandPalette::refilter()
{
    order = cmdsearch::rank (entries, search.getText().toStdString(), recent);
    list.updateContent();
    select (0);
    updateBounds();
    repaint();
}

void CommandPalette::select (int row)
{
    if (order.empty())
        list.deselectAllRows();
    else
        list.selectRow (juce::jlimit (0, (int) order.size() - 1, row));
}

const CommandPalette::Item* CommandPalette::itemAt (int row) const
{
    if (! juce::isPositiveAndBelow (row, (int) order.size()))
        return nullptr;

    const int index = order[(size_t) row];
    return juce::isPositiveAndBelow (index, (int) items.size()) ? &items[(size_t) index] : nullptr;
}

juce::String CommandPalette::resultId (int row) const
{
    const auto* item = itemAt (row);
    return item != nullptr ? item->id : juce::String();
}

juce::String CommandPalette::resultTitle (int row) const
{
    const auto* item = itemAt (row);
    return item != nullptr ? item->title : juce::String();
}

juce::String CommandPalette::resultKeys (int row) const
{
    const auto* item = itemAt (row);
    return item != nullptr ? item->keys : juce::String();
}

//==============================================================================
void CommandPalette::open()
{
    if (isVisible())
    {
        close();
        return;
    }

    rebuildItems();
    search.setTextToShowWhenEmpty ("Buscar uma ação: exportar, bpm, mixer..."_t, theme::colour::textFaint);
    search.setText ({}, false);
    refilter();

    setVisible (true);
    toFront (false);

    if (isShowing())
        search.grabKeyboardFocus();
}

void CommandPalette::close()
{
    // o JUCE devolve o foco ao pai quando quem some estava com ele: os atalhos voltam a valer
    setVisible (false);
}

void CommandPalette::setQuery (const juce::String& text)
{
    search.setText (text, false);
    refilter();
}

void CommandPalette::runRow (int row)
{
    const auto* found = itemAt (row);

    if (found == nullptr)
        return;

    const auto item = *found;   // cópia: a ação pode abrir a paleta de novo e refazer a lista
    cmdsearch::touchRecent (recent, item.id.toStdString());
    close();   // antes de executar: a ação pode abrir um diálogo ou levar o foco para outra tela

    if (item.action)
    {
        if ((onAction == nullptr || ! onAction (item.id)) && onStatus)
            onStatus (item.title + ": esta ação não está disponível aqui."_t, true);

        return;
    }

    // pelo registro, como a tela e os atalhos: nada muda o modelo por fora
    const auto result = app.run (item.id);

    if (onStatus)
        onStatus (result.ok ? item.title : result.error, ! result.ok);
}

bool CommandPalette::handleKey (const juce::KeyPress& key)
{
    if (! isVisible())
        return false;

    const int count = (int) order.size();
    const int current = list.getSelectedRow();
    const int page = juce::jmax (1, visibleRows() - 1);

    if (key == juce::KeyPress::escapeKey)          { close(); return true; }
    if (key == juce::KeyPress::returnKey)          { runRow (current); return true; }
    if (key == juce::KeyPress::downKey)            { select (count > 0 ? (current + 1) % count : 0); return true; }   // dá a volta
    if (key == juce::KeyPress::upKey)              { select (count > 0 ? (current + count - 1) % count : 0); return true; }
    if (key == juce::KeyPress::pageDownKey)        { select (current + page); return true; }
    if (key == juce::KeyPress::pageUpKey)          { select (current - page); return true; }

    // Home e End sem Ctrl continuam do campo (começo e fim do texto); com Ctrl, a primeira e a última linha
    if (key == juce::KeyPress (juce::KeyPress::homeKey, juce::ModifierKeys::commandModifier, 0))   { select (0); return true; }
    if (key == juce::KeyPress (juce::KeyPress::endKey, juce::ModifierKeys::commandModifier, 0))    { select (count - 1); return true; }

    return false;
}

//==============================================================================
int CommandPalette::getNumRows()
{
    return (int) order.size();
}

void CommandPalette::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    using namespace theme;
    const auto* item = itemAt (row);

    if (item == nullptr)
        return;

    auto area = juce::Rectangle<int> (0, 0, width, height);

    // a linha ativa (a que o Enter executa) é o único laranja da paleta
    if (selected)
    {
        g.setColour (colour::signalTint);
        g.fillRoundedRectangle (area.toFloat().reduced (0.0f, 1.0f), metric::radius);
    }

    area.reduce (12, 0);

    if (item->keys.isNotEmpty())
    {
        const auto keyFont = font (type::small, Weight::medium);
        const int keyWidth = juce::jmin (area.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (keyFont, item->keys) + 4);
        g.setColour (selected ? colour::signal : colour::textDim);
        g.setFont (keyFont);
        g.drawText (item->keys, area.removeFromRight (keyWidth), juce::Justification::centredRight, true);
        area.removeFromRight (12);
    }

    const auto titleFont = font (type::body, Weight::medium);
    const int titleWidth = juce::jmin (area.getWidth(), juce::GlyphArrangement::getStringWidthInt (titleFont, item->title) + 2);
    g.setColour (colour::text);
    g.setFont (titleFont);
    g.drawText (item->title, area.removeFromLeft (titleWidth), juce::Justification::centredLeft, true);

    // o id, apagado: é por ele que a IA e o editor de atalhos chamam a mesma ação, e a busca também vale por ele
    if (area.getWidth() > 40)
    {
        area.removeFromLeft (12);
        g.setColour (colour::textFaint);
        g.setFont (font (type::small));
        g.drawText (item->id, area, juce::Justification::centredLeft, true);
    }
}

void CommandPalette::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        runRow (row);
}

//==============================================================================
int CommandPalette::visibleRows() const noexcept
{
    return juce::jlimit (1, maxRows, (int) order.size());   // sem resultado, uma linha para o "nada com esse nome"
}

juce::Rectangle<int> CommandPalette::card() const
{
    return getLocalBounds().reduced (shadow);
}

bool CommandPalette::hitTest (int x, int y)
{
    return card().contains (x, y);   // a sombra não pega cliques
}

void CommandPalette::updateBounds()
{
    auto* parent = getParentComponent();

    if (parent == nullptr)
        return;

    // centrada no alto do pai, logo abaixo da barra de cima; a altura é a soma das linhas
    const int width = juce::jmin (cardWidth, parent->getWidth() - 2 * theme::metric::pad);
    const int height = searchHeight + 1 + listTop + visibleRows() * rowHeight + listBottom;
    const int top = juce::jmin (theme::metric::topBar + theme::metric::gap, juce::jmax (0, parent->getHeight() - height));

    setBounds (juce::Rectangle<int> ((parent->getWidth() - width) / 2, top, width, height).expanded (shadow));
}

void CommandPalette::parentSizeChanged()
{
    if (isVisible())
        updateBounds();
}

void CommandPalette::resized()
{
    auto r = card();
    search.setBounds (r.removeFromTop (searchHeight).reduced (theme::metric::gap, 4));
    r.removeFromTop (1 + listTop);
    r.removeFromBottom (listBottom);
    list.setBounds (r.reduced (theme::metric::gap, 0));
}

void CommandPalette::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto c = card();

    // cartão flutuante, como os menus e a pergunta da IA
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), shadow, { 0, 6 }).drawForRectangle (g, c);
    g.setColour (colour::floating);
    g.fillRoundedRectangle (c.toFloat(), metric::radiusCard);
    g.setColour (colour::floatingEdge);
    g.drawRoundedRectangle (c.toFloat().reduced (0.5f), metric::radiusCard, 1.0f);

    // a lupa do campo e o fio que separa a busca da lista
    auto top = c.withHeight (searchHeight);
    icons::draw (g, Icon::search, top.withWidth (metric::gap + 36).withTrimmedLeft (metric::gap).toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                 colour::textDim);
    g.setColour (colour::line);
    g.fillRect (c.getX() + 1, top.getBottom(), c.getWidth() - 2, 1);

    if (order.empty())
    {
        g.setColour (colour::textFaint);
        g.setFont (font (type::body));
        g.drawText ("Nenhuma ação com esse nome."_t, list.getBounds().reduced (12, 0), juce::Justification::centredLeft, true);
    }
}

} // namespace whynot
