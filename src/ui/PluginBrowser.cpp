#include "PluginBrowser.h"
#include "Theme.h"
#include "../app/AppController.h"

namespace whynot
{

namespace
{
    juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();
        for (auto& [k, v] : kv) o->setProperty (k, v);
        return juce::var (o);
    }

    constexpr int rowHeight = 30;
}

PluginBrowser::PluginBrowser (AppController& a) : app (a)
{
    scanButton.setTooltip ("Procura plugins VST3 nas pastas. Cada plugin novo abre num processo separado: se travar, ele é bloqueado e o app continua."_t);
    scanButton.onClick = [this] { startScan(); };

    foldersButton.setTooltip ("Pastas onde procurar plugins VST3");
    foldersButton.onClick = [this] { showFoldersMenu(); };

    blockedButton.setTooltip ("Plugins que travaram ou derrubaram o scan, com o motivo");
    blockedButton.onClick = [this] { showBlockedMenu(); };

    favouritesButton.setTooltip ("Só os favoritos. A estrela no fim de cada linha marca um plugin."_t);
    favouritesButton.setClickingTogglesState (true);
    favouritesButton.onClick = [this] { setFavouritesOnly (favouritesButton.getToggleState()); };

    search.setTextToShowWhenEmpty ("Buscar plugin", theme::colour::textFaint);
    search.setFont (theme::font (theme::type::body));
    search.setIndents (32, 8);
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.onTextChange = [this] { rebuildRows(); };
    search.onEscapeKey = [this] { search.clear(); rebuildRows(); };

    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setMultipleSelectionEnabled (false);

    for (juce::Component* c : { (juce::Component*) &favouritesButton, (juce::Component*) &scanButton,
                                (juce::Component*) &foldersButton, (juce::Component*) &blockedButton })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }

    addAndMakeVisible (search);
    addAndMakeVisible (list);
    Favorites::get().addListener (this);
    refresh();
}

PluginBrowser::~PluginBrowser()
{
    Favorites::get().removeListener (this);
    list.setModel (nullptr);
}

void PluginBrowser::setFavouritesOnly (bool on)
{
    favouritesButton.setToggleState (on, juce::dontSendNotification);
    favouritesButton.setIcon (on ? Icon::starFilled : Icon::star);
    rebuildRows();
}

bool PluginBrowser::isFavourite (const juce::PluginDescription& d) const
{
    return Favorites::get().contains (Favorites::Kind::plugin, d.createIdentifierString());
}

void PluginBrowser::favoritesChanged()
{
    // Só o filtro muda de linhas; na lista inteira a ordem fica (o plugin não pula de lugar ao marcar).
    if (isFavouritesOnly())
        rebuildRows();
    else
        list.repaint();
}

int PluginBrowser::rowOf (const juce::String& pluginId) const
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (! rows[i].header && rows[i].desc.createIdentifierString() == pluginId)
            return (int) i;

    return -1;
}

juce::Rectangle<int> PluginBrowser::starColumn (int width, int height) const
{
    // os 26 px da estrela e a margem da direita (a linha tem 16 px de recuo), para o clique não exigir mira
    return { width - 16 - favui::starColumn, 0, 16 + favui::starColumn, height };
}

void PluginBrowser::refresh()
{
    all = app.knownPlugins();
    numBlocked = app.blockedPlugins().size();
    blockedButton.setLabel ("Bloqueados (" + juce::String (numBlocked) + ")");
    blockedButton.setEnabled (numBlocked > 0);
    rebuildRows();
}

void PluginBrowser::rebuildRows()
{
    const auto query = search.getText().trim();
    std::vector<Row> instruments, effects;

    for (auto& d : all)
    {
        if (query.isNotEmpty() && ! d.name.containsIgnoreCase (query) && ! d.manufacturerName.containsIgnoreCase (query))
            continue;

        if (isFavouritesOnly() && ! isFavourite (d))
            continue;

        (d.isInstrument ? instruments : effects).push_back ({ false, d.name, d });
    }

    rows.clear();

    if (! instruments.empty())
    {
        rows.push_back ({ true, "Instrumentos (" + juce::String ((int) instruments.size()) + ")", {} });
        rows.insert (rows.end(), instruments.begin(), instruments.end());
    }

    if (! effects.empty())
    {
        rows.push_back ({ true, "Efeitos (" + juce::String ((int) effects.size()) + ")", {} });
        rows.insert (rows.end(), effects.begin(), effects.end());
    }

    list.updateContent();
    list.repaint();
    repaint();
}

void PluginBrowser::setScanStatus (const juce::String& text, double progress)
{
    scanText = text;
    scanProgress = progress;
    scanButton.setEnabled (progress < 0.0);
    repaint (getLocalBounds().removeFromBottom (footerHeight));
}

//==============================================================================
int PluginBrowser::getNumRows()
{
    return (int) rows.size();
}

void PluginBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    using namespace theme;

    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    const auto& r = rows[(size_t) row];
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);

    if (r.header)
    {
        g.setColour (colour::textFaint);
        g.setFont (captionFont());
        g.drawText (r.text.toUpperCase(), area.withTrimmedTop (8), juce::Justification::centredLeft);
        return;
    }

    if (selected)
    {
        g.setColour (colour::raisedHigh);
        g.fillRoundedRectangle (area.toFloat().reduced (0.0f, 2.0f), metric::radius);
    }

    area.reduce (8, 0);
    icons::draw (g, r.desc.isInstrument ? Icon::note : Icon::plugin, area.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                 selected ? colour::text : colour::textDim);
    area.removeFromLeft (8);

    // estrela no fim da linha: cheia se é favorito; o contorno aparece com o mouse na linha
    const auto starBox = area.removeFromRight (favui::starColumn).withTrimmedLeft (6);
    favui::drawStar (g, starBox.toFloat(), isFavourite (r.desc), selected || hover.getRow() == row,
                     hover.isOver (row, starColumn (width, height)));

    const auto maker = r.desc.manufacturerName;
    const int makerWidth = juce::jmin (area.getWidth() / 2,
                                       juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (type::small), maker)) + 4);

    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawText (maker, area.removeFromRight (makerWidth), juce::Justification::centredRight, true);

    g.setColour (colour::text);
    g.setFont (font (type::body, Weight::medium));
    g.drawText (r.desc.name, area.withTrimmedRight (6), juce::Justification::centredLeft, true);
}

void PluginBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
        return;

    if (e.mods.isPopupMenu())
        showRowMenu (row);
    else if (starColumn (list.getVisibleRowWidth(), list.getRowHeight()).contains (e.getPosition()))
        Favorites::get().toggle (Favorites::Kind::plugin, rows[(size_t) row].desc.createIdentifierString());
}

void PluginBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
        return;

    // dois cliques rápidos na estrela marcam e desmarcam: não adicionam o plugin
    if (starColumn (list.getVisibleRowWidth(), list.getRowHeight()).contains (e.getPosition()))
        return;

    addPlugin (rows[(size_t) row].desc);
}

void PluginBrowser::showRowMenu (int row)
{
    const auto d = rows[(size_t) row].desc;
    const bool favourite = isFavourite (d);
    juce::PopupMenu menu;
    menu.addItem (1, favourite ? "Tirar dos favoritos" : "Marcar como favorito");

    if (d.isInstrument)
        menu.addItem (2, "Adicionar ao channel rack");

    juce::Component::SafePointer<PluginBrowser> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list), [safe, d, favourite] (int choice)
    {
        if (safe == nullptr)
            return;

        if (choice == 1)
            Favorites::get().set (Favorites::Kind::plugin, d.createIdentifierString(), ! favourite);
        else if (choice == 2)
            safe->addPlugin (d);
    });
}

void PluginBrowser::addPlugin (const juce::PluginDescription& d)
{
    if (d.isInstrument)
    {
        if (onAddInstrument) onAddInstrument (d);
    }
    else if (onAddEffect)
    {
        onAddEffect (d);
    }
}

juce::var PluginBrowser::getDragSourceDescription (const juce::SparseSet<int>& selectedRows)
{
    if (selectedRows.size() != 1)
        return {};

    const int row = selectedRows[0];

    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
        return {};

    return juce::String (dragPrefix) + rows[(size_t) row].desc.createIdentifierString();
}

juce::String PluginBrowser::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
        return {};

    const auto& d = rows[(size_t) row].desc;
    return d.name + " " + d.version + " (" + d.pluginFormatName + ")\n"
           + (d.isInstrument ? "Arraste para o channel rack ou dê duplo clique para criar um canal."_t
                             : "Efeito: vai num insert do mixer."_t)
           + "\n" + "A estrela no fim da linha marca como favorito."_t;
}

//==============================================================================
void PluginBrowser::startScan()
{
    const auto report = app.run ("plugins.scan");

    if (! report.ok && onStatus)
        onStatus (report.error, true);
}

void PluginBrowser::showFoldersMenu()
{
    juce::PopupMenu menu;
    const auto folders = app.pluginFolders();
    menu.addSectionHeader ("Pastas de plugins VST3");

    for (int i = 0; i < folders.size(); ++i)
    {
        juce::PopupMenu sub;
        sub.addItem (1000 + i, "Tirar esta pasta");
        menu.addSubMenu (folders[i], sub);
    }

    if (folders.isEmpty())
        menu.addItem (-1, "(nenhuma pasta)", false);

    menu.addSeparator();
    menu.addItem (1, "Adicionar pasta...");
    menu.addItem (2, "Voltar à pasta padrão do Windows"_t);

    juce::Component::SafePointer<PluginBrowser> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&foldersButton), [safe, folders] (int choice)
    {
        if (safe == nullptr || choice <= 0)
            return;

        auto& app = safe->app;
        auto setFolders = [&app] (const juce::StringArray& f)
        {
            juce::Array<juce::var> list;
            for (auto& s : f) list.add (s);
            app.run ("plugins.setFolders", args ({ { "folders", list } }));
        };

        if (choice >= 1000)
        {
            auto next = folders;
            next.remove (choice - 1000);
            setFolders (next);
        }
        else if (choice == 2)
        {
            setFolders (app.defaultPluginFolders());
        }
        else if (choice == 1)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("Pasta de plugins VST3",
                                                                 juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory));
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                        [safe, folders, setFolders] (const juce::FileChooser& fc)
                                        {
                                            const auto dir = fc.getResult();

                                            if (safe == nullptr || ! dir.isDirectory())
                                                return;

                                            auto next = folders;
                                            next.addIfNotAlreadyThere (dir.getFullPathName());
                                            setFolders (next);

                                            if (safe->onStatus)
                                                safe->onStatus ("Pasta adicionada. Clique em Escanear para procurar os plugins dela.", false);
                                        });
        }

        if (safe != nullptr)
            safe->repaint();
    });
}

void PluginBrowser::showBlockedMenu()
{
    juce::PopupMenu menu;
    const auto blocked = app.blockedPlugins();
    juce::StringArray files;
    menu.addSectionHeader ("Bloqueados: o app não abre estes plugins"_t);

    for (auto& b : blocked)
    {
        files.add (b["file"].toString());
        juce::PopupMenu sub;
        sub.addItem (-1, b["reason"].toString(), false);
        sub.addSeparator();
        sub.addItem (files.size(), "Desbloquear e escanear de novo");
        menu.addSubMenu (b["name"].toString(), sub);
    }

    juce::Component::SafePointer<PluginBrowser> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&blockedButton), [safe, files] (int choice)
    {
        if (safe == nullptr || ! juce::isPositiveAndBelow (choice - 1, files.size()))
            return;

        safe->app.run ("plugins.unblock", args ({ { "file", files[choice - 1] } }));
        safe->startScan();
    });
}

//==============================================================================
void PluginBrowser::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Plugins", getLocalBounds().removeFromTop (headerHeight).reduced (metric::pad, 0), juce::Justification::centredLeft);

    // busca: fundo de controle com a lupa à esquerda (o TextEditor fica transparente por cima)
    const auto box = searchArea().toFloat();
    g.setColour (colour::raised);
    g.fillRoundedRectangle (box, metric::radius);
    icons::draw (g, Icon::search, box.withWidth (32.0f).withSizeKeepingCentre (16.0f, 16.0f), colour::textDim);

    if (rows.empty())
    {
        g.setColour (colour::textDim);
        g.setFont (font (type::body));
        const auto text = all.isEmpty() ? "Nenhum plugin ainda. Confira as pastas e clique em Escanear."_t
                        : isFavouritesOnly() && search.getText().trim().isEmpty()
                            ? "Nenhum favorito ainda. Passe o mouse num plugin e clique na estrela do fim da linha."_t
                            : "Nada com esse nome."_t;
        g.drawFittedText (text, list.getBounds().reduced (16, 16).withHeight (60), juce::Justification::topLeft, 3);
    }

    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (metric::pad, 0);
    g.setColour (colour::line);
    g.fillRect (footer.removeFromTop (1));
    auto status = footer.removeFromTop (24);

    if (scanProgress >= 0.0)
    {
        auto bar = status.removeFromBottom (3).toFloat();
        g.setColour (colour::raised);
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (colour::signal);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, scanProgress)), 1.5f);
    }

    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawFittedText (scanText.isNotEmpty() ? scanText : juce::String (all.size()) + " plugins conhecidos",
                      status, juce::Justification::centredLeft, 1, 0.8f);
}

juce::Rectangle<int> PluginBrowser::searchArea() const
{
    return { theme::metric::pad, headerHeight, getWidth() - 2 * theme::metric::pad, theme::metric::control };
}

void PluginBrowser::resized()
{
    using namespace theme;
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight).reduced (metric::pad, (headerHeight - metric::control) / 2);
    scanButton.setBounds (header.removeFromRight (scanButton.getIdealWidth (metric::control)));
    header.removeFromRight (metric::gap);
    favouritesButton.setBounds (header.removeFromRight (metric::control));

    auto footer = r.removeFromBottom (footerHeight).reduced (metric::pad, 0);
    footer.removeFromTop (30);
    auto buttons = footer.removeFromTop (metric::control);
    foldersButton.setBounds (buttons.removeFromLeft (foldersButton.getIdealWidth (metric::control)));
    buttons.removeFromLeft (metric::gap);
    blockedButton.setBounds (buttons.removeFromLeft (juce::jmin (buttons.getWidth(), blockedButton.getIdealWidth (metric::control))));

    search.setBounds (searchArea());
    r.removeFromTop (metric::control + metric::gap);
    list.setBounds (r.reduced (metric::gap - 4, 0));
}

} // namespace whynot
