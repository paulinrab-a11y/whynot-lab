#include "InsertPanel.h"
#include "FxWidgets.h"
#include "../FavoriteStar.h"
#include "../Icons.h"
#include "../Theme.h"
#include "../../app/AppController.h"
#include "../../dsp/FxCatalog.h"
#include "../../engine/UserMix.h"

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

    juce::var list (const juce::StringArray& items)
    {
        juce::Array<juce::var> a;

        for (auto& s : items)
            a.add (s);

        return a;
    }

    const juce::String arrow (juce::CharPointer_UTF8 (" \xe2\x96\xbe"));
    const juce::String leftArrow (juce::CharPointer_UTF8 ("\xe2\x86\x90 "));

    juce::String roleName (const juce::String& id)
    {
        const auto* r = dsp::findRole (id.toStdString());
        return r != nullptr ? fxui::utf8 (r->name) : id;
    }
}

//==============================================================================
class InsertPanel::Slot : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    Slot (InsertPanel& p, int i) : panel (p), index (i)
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void set (const Insert* ins, const Project& p, bool firstEmpty, const juce::String& problem)
    {
        showAddHint = firstEmpty;
        hasProblem = problem.isNotEmpty();

        if (ins == nullptr)
        {
            insertId = {};
            title = subtitle = sidechain = {};
            setTooltip (firstEmpty ? juce::String ("Adicionar um efeito (clique)") : juce::String());
        }
        else
        {
            insertId = ins->id;
            bypass = ins->bypass;

            if (const auto* info = ins->kind == "whynot" ? dsp::findEffect (ins->type.toStdString()) : nullptr)
            {
                title = fxui::utf8 (info->name);
                const auto* preset = ins->preset.isEmpty() ? nullptr : dsp::findPreset (info->type, ins->preset.toStdString());
                subtitle = preset != nullptr ? fxui::utf8 (preset->name) : juce::String ("Padrão"_t);
            }
            else
            {
                title = ins->type;
                subtitle = ins->kind.toUpperCase();
            }

            sidechain = {};

            if (auto* src = p.findMixer (ins->sidechain))
                sidechain = src->name;

            setTooltip (hasProblem ? problem
                                   : title + " (" + subtitle + "). Clique abre; o ponto liga e desliga; botão direito: mais opções."_t
                                         + (sidechain.isEmpty() ? juce::String()
                                                                : " Sidechain: o som de "_t + sidechain + " comanda este efeito."_t));
        }

        repaint();
    }

    bool isEmpty() const noexcept               { return insertId.isEmpty(); }
    const juce::String& getInsertId() const     { return insertId; }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto r = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
        const bool hover = isMouseOver (true);

        if (panel.dropSlot == index)
        {
            g.setColour (colour::signal);
            g.fillRect (r.getX(), r.getY() - 1.0f, r.getWidth(), 2.0f);
        }

        auto number = r.removeFromLeft (18.0f);
        g.setColour (colour::textFaint);
        g.setFont (numberFont (type::small));
        g.drawText (juce::String (index + 1), number, juce::Justification::centredLeft);

        if (isEmpty())
        {
            if (hover || showAddHint)
            {
                g.setColour (hover ? colour::raised : colour::raised.withAlpha (0.35f));
                g.fillRoundedRectangle (r, metric::radiusSmall);
                icons::draw (g, Icon::plus, r.removeFromLeft (26.0f).withSizeKeepingCentre (14.0f, 14.0f), hover ? colour::text : colour::textFaint);
                g.setColour (hover ? colour::text : colour::textFaint);
                g.setFont (font (type::small));
                g.drawText ("Adicionar efeito", r, juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (colour::raised.withAlpha (0.25f));
                g.fillRoundedRectangle (r, metric::radiusSmall);
            }

            return;
        }

        g.setColour (hover ? colour::raisedHigh : colour::raised);
        g.fillRoundedRectangle (r, metric::radiusSmall);

        if (hasProblem)
        {
            g.setColour (colour::meterHigh);
            g.drawRoundedRectangle (r.reduced (0.5f), metric::radiusSmall, 1.0f);
        }

        // LED: laranja = efeito ligado; anel cinza = em bypass.
        const auto led = ledArea().toFloat();
        if (bypass)
        {
            g.setColour (colour::textFaint);
            g.drawEllipse (led.reduced (1.0f), 1.5f);
        }
        else
        {
            g.setColour (colour::signal);
            g.fillEllipse (led.reduced (1.0f));
        }

        r.removeFromLeft (24.0f);
        auto right = r.removeFromRight (sidechain.isNotEmpty() ? 64.0f : 4.0f);

        g.setColour (bypass ? colour::textDim : colour::text);
        g.setFont (font (type::body, Weight::medium));
        const float titleWidth = juce::jmin (r.getWidth() * 0.62f,
                                             juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title) + 8.0f);
        g.drawFittedText (title, r.removeFromLeft (titleWidth).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        g.setColour (colour::textDim);
        g.setFont (font (type::small));
        g.drawFittedText (subtitle, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        if (sidechain.isNotEmpty())
        {
            g.setColour (colour::textFaint);
            g.drawFittedText (leftArrow + sidechain, right.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredRight, 1, 0.7f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;

        if (e.mods.isPopupMenu())
        {
            if (isEmpty()) panel.showAddMenu (index, this);
            else           panel.showSlotMenu (index, this);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isEmpty() || dragged || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragged = true;
            container->startDragging ("insert:" + panel.trackId + ":" + insertId, this);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragged || e.mods.isPopupMenu() || ! getLocalBounds().contains (e.getPosition()))
            return;

        if (isEmpty())
            panel.showAddMenu (index, this);
        else if (ledArea().expanded (4).contains (e.getPosition()))
            panel.run ("mixer.setInsertBypass", args ({ { "track", panel.trackId }, { "insert", insertId } }));
        else if (! panel.app.showInsertEditor (panel.trackId, insertId) && panel.onStatus)
            panel.onStatus ("Não consegui abrir a janela deste efeito (o áudio está ligado?)."_t, true);
    }

private:
    juce::Rectangle<int> ledArea() const   { return { 22, getHeight() / 2 - 6, 12, 12 }; }

    InsertPanel& panel;
    const int index;
    juce::String insertId, title, subtitle, sidechain;
    bool bypass = false, showAddHint = false, hasProblem = false, dragged = false;
};

//==============================================================================
class InsertPanel::SendRow : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    SendRow (InsertPanel& p, const juce::String& target) : panel (p), to (target)
    {
        setTooltip ("Envio: uma cópia do som desta trilha vai para este bus, e o som daqui continua igual."_t);

        preButton.setButtonText ("PRÉ"_t);
        preButton.setClickingTogglesState (false);
        preButton.setTooltip ("Pré-fader: o envio sai antes do volume e do pan desta trilha (abaixar o fader não tira o reverb). "
                              "Apagado: pós-fader, o normal."_t);
        preButton.onClick = [this]
        {
            panel.run ("mixer.setSend", args ({ { "track", panel.trackId }, { "to", to }, { "db", (double) valueDb }, { "pre", ! preFader } }));
        };
        preButton.setWantsKeyboardFocus (false);
        addAndMakeVisible (preButton);

        level.setRange (-60.0, 6.0, 0.1);
        level.setSkewFactorFromMidPoint (-12.0);
        level.setDoubleClickReturnValue (true, -12.0);
        level.setTooltip ("Nível do envio. Duplo clique volta a -12 dB."_t);
        level.onDragStart = [this] { panel.app.getCommands().beginGesture ("Envio"); };
        level.onDragEnd = [this] { panel.app.getCommands().endGesture(); };
        level.onValueChange = [this]
        {
            const double db = level.getValue() <= -59.9 ? -100.0 : level.getValue();
            panel.run ("mixer.setSend", args ({ { "track", panel.trackId }, { "to", to }, { "db", db } }));
        };
        level.setWantsKeyboardFocus (false);
        addAndMakeVisible (level);

        remove.setTooltip ("Remover este envio");
        remove.onClick = [this] { panel.run ("mixer.removeSend", args ({ { "track", panel.trackId }, { "to", to } })); };
        remove.setWantsKeyboardFocus (false);
        addAndMakeVisible (remove);
    }

    const juce::String& getTarget() const noexcept   { return to; }

    void set (const juce::String& name, float db, bool pre)
    {
        targetName = name;
        valueDb = db;
        preFader = pre;
        preButton.setToggleState (pre, juce::dontSendNotification);
        level.setValue (juce::jmax (-60.0f, db), juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto r = getLocalBounds().reduced (0, 2);
        g.setColour (colour::text);
        g.setFont (font (type::small, Weight::medium));
        g.drawFittedText (targetName, r.removeFromLeft (72), juce::Justification::centredLeft, 1, 0.8f);

        r.removeFromRight (24);
        g.setColour (colour::textDim);
        g.setFont (numberFont (type::small));
        g.drawText (valueDb <= -59.9f ? juce::String ("-inf") : juce::String (valueDb, 1).replaceCharacter ('.', ','),
                    r.removeFromRight (40), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        r.removeFromLeft (72);
        preButton.setBounds (r.removeFromLeft (34).reduced (0, 2));
        r.removeFromLeft (4);
        remove.setBounds (r.removeFromRight (22).reduced (0, 2));
        r.removeFromRight (42);
        level.setBounds (r);
    }

private:
    InsertPanel& panel;
    const juce::String to;
    juce::String targetName;
    float valueDb = -12.0f;
    bool preFader = false;
    juce::TextButton preButton;
    juce::Slider level { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton remove { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
};

//==============================================================================
InsertPanel::InsertPanel (AppController& a) : app (a)
{
    for (int i = 0; i < maxInserts; ++i)
        addAndMakeVisible (slots.add (new Slot (*this, i)));

    theme::styleComboBox (roleBox, Icon::none, true);
    roleBox.setTooltip ("Papel da trilha na mixagem: escolhe a cadeia que os estilos aplicam.");
    roleBox.onChange = [this]
    {
        const int i = roleBox.getSelectedId() - 1;

        if (i >= 0 && i < roleIds.size())
            run ("mixer.setRole", args ({ { "track", trackId }, { "role", roleIds[i] } }));
    };
    addAndMakeVisible (roleBox);

    chainButton.setTooltip ("Cadeia pronta: troca os efeitos desta trilha pela receita de um gênero."_t);
    chainButton.onClick = [this] { showChainMenu(); };
    addAndMakeVisible (chainButton);

    addSendButton.setButtonText ("+ Envio");
    addSendButton.setTooltip ("Envio: manda uma cópia do som desta trilha para um bus, a trilha que junta o som de várias (um reverb só para as vozes, por exemplo)."_t);
    addSendButton.onClick = [this] { showSendMenu(); };
    addAndMakeVisible (addSendButton);
}

InsertPanel::~InsertPanel() = default;

void InsertPanel::setTrack (const juce::String& mixerId)
{
    if (mixerId != trackId)
    {
        trackId = mixerId;
        refresh (app.getModel().get());
    }
}

void InsertPanel::run (const juce::String& command, const juce::var& a, const juce::String& success)
{
    const auto result = app.run (command, a);

    if (onStatus)
    {
        if (! result.ok)
            onStatus (result.error, true);
        else if (success.isNotEmpty())
            onStatus (success, false);
    }
}

void InsertPanel::refresh (const Project& p)
{
    const auto* m = p.findMixer (trackId);

    if (m == nullptr)
    {
        trackId = masterTrackId;
        m = p.findMixer (trackId);
    }

    if (m == nullptr)
        return;

    isMaster = m->isMaster();
    trackName = m->name;

    const auto& problems = app.getProjector().insertProblems();
    bool firstEmptySeen = false;

    for (int i = 0; i < maxInserts; ++i)
    {
        const auto* ins = i < (int) m->inserts.size() ? &m->inserts[(size_t) i] : nullptr;
        const bool firstEmpty = ins == nullptr && ! firstEmptySeen;
        firstEmptySeen |= ins == nullptr;
        slots[i]->set (ins, p, firstEmpty, ins != nullptr ? problems[ins->id] : juce::String());
    }

    // Papel (o master não tem).
    roleBox.setVisible (! isMaster);
    roleBox.clear (juce::dontSendNotification);
    roleIds.clear();
    roleIds.add ({});
    roleBox.addItem ("Papel: automático ("_t + roleName (dsp::guessRole (m->name.toStdString())) + ")", 1);

    for (auto& r : dsp::allRoles())
    {
        roleIds.add (r.id);
        roleBox.addItem ("Papel: " + fxui::utf8 (r.name), roleIds.size());
    }

    roleBox.setSelectedId (juce::jmax (1, roleIds.indexOf (m->role) + 1), juce::dontSendNotification);
    chainButton.setButtonText ((isMaster ? juce::String ("Cadeia de master") : juce::String ("Cadeia pronta")) + arrow);

    // Envios
    juce::StringArray wanted, existing;

    for (auto& s : m->sends)
        wanted.add (s.to);

    for (auto* row : sends)
        existing.add (row->getTarget());

    if (wanted != existing)
    {
        sends.clear();

        for (auto& t : wanted)
            addAndMakeVisible (sends.add (new SendRow (*this, t)));
    }

    for (auto* row : sends)
        if (auto* target = p.findMixer (row->getTarget()))
            for (auto& s : m->sends)
                if (s.to == row->getTarget())
                    row->set (target->name, s.levelDb, s.preFader);

    addSendButton.setVisible (! isMaster);
    resized();
    repaint();
}

int InsertPanel::getIdealHeight() const
{
    int h = headerHeight + (isMaster ? 0 : roleHeight) + captionHeight + maxInserts * slotHeight;

    if (! isMaster)
        h += captionHeight + sends.size() * sendHeight + 30;

    return h + 8;
}

void InsertPanel::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);

    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawFittedText (isMaster ? juce::String ("Master") : trackName, header.withTrimmedRight (128), juce::Justification::centredLeft, 1, 0.8f);

    if (! isMaster)
        r.removeFromTop (roleHeight);

    g.setFont (captionFont());
    g.setColour (colour::textFaint);
    g.drawText ("EFEITOS (NA ORDEM DO SINAL)", r.removeFromTop (captionHeight), juce::Justification::bottomLeft);
    r.removeFromTop (maxInserts * slotHeight);

    if (! isMaster)
    {
        g.drawText ("ENVIOS", r.removeFromTop (captionHeight), juce::Justification::bottomLeft);

        if (sends.isEmpty())
        {
            g.setFont (font (type::small));
            g.drawText ("Nenhum envio.", r.removeFromTop (sendHeight), juce::Justification::centredLeft);
        }
    }
}

void InsertPanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);
    chainButton.setBounds (header.removeFromRight (128).reduced (0, 4));

    if (! isMaster)
        roleBox.setBounds (r.removeFromTop (roleHeight).reduced (0, 2));

    r.removeFromTop (captionHeight);

    for (auto* s : slots)
        s->setBounds (r.removeFromTop (slotHeight));

    if (isMaster)
        return;

    r.removeFromTop (captionHeight);

    for (auto* row : sends)
        row->setBounds (r.removeFromTop (sendHeight));

    if (sends.isEmpty())
        r.removeFromTop (sendHeight);

    addSendButton.setBounds (r.removeFromTop (28).removeFromLeft (100).reduced (0, 2));
}

//==============================================================================
void InsertPanel::addFxMenuItems (juce::PopupMenu& menu, std::vector<std::function<void()>>& actions, int slot,
                                  const juce::String& replacing, bool onlyFavourites)
{
    // Trocando um efeito: tira o antigo e põe o novo no mesmo lugar, num passo só de desfazer.
    auto push = [this, &actions, replacing] (std::function<void()> f)
    {
        if (replacing.isEmpty())
        {
            actions.push_back (std::move (f));
        }
        else
        {
            actions.push_back ([this, replacing, f]
            {
                const CommandRegistry::ScopedGesture gesture (app.getCommands(), "Trocar efeito");
                run ("mixer.removeInsert", args ({ { "track", trackId }, { "insert", replacing } }));
                f();
            });
        }

        return (int) actions.size();
    };

    using Kind = Favorites::Kind;
    auto& favourites = Favorites::get();

    auto addEffect = [this, slot] (const juce::String& type, const juce::String& preset)
    {
        return [this, slot, type, preset]
        {
            if (preset.isEmpty())
                run ("mixer.addInsert", args ({ { "track", trackId }, { "type", type }, { "slot", slot } }));
            else
                run ("mixer.addInsert", args ({ { "track", trackId }, { "type", type }, { "preset", preset }, { "slot", slot } }));
        };
    };

    auto addPlugin = [this, slot] (const juce::String& id)
    {
        return [this, slot, id] { run ("mixer.addPluginInsert", args ({ { "track", trackId }, { "plugin", id }, { "slot", slot } })); };
    };

    auto pluginLabel = [] (const juce::PluginDescription& desc)
    {
        return desc.name + (desc.manufacturerName.isNotEmpty() ? " (" + desc.manufacturerName + ")" : juce::String());
    };

    // Efeitos VST3 de terceiros (a hospedagem vem do scan de plugins), favoritos primeiro.
    std::vector<juce::PluginDescription> vstEffects;

    for (auto& desc : app.knownPlugins())
        if (! desc.isInstrument)
            vstEffects.push_back (desc);

    favourites.favoritesFirst (vstEffects, Kind::plugin, [] (const juce::PluginDescription& d) { return d.createIdentifierString(); });

    //--------------------------------------------------------------------------
    // Favoritos primeiro (pedido de 21/10): os efeitos, os presets e os VST3 marcados com a estrela.
    // A estrela de cada item desmarca sem fechar o menu.
    int numFavourites = 0;
    auto favouritesHeader = [&menu, &numFavourites]
    {
        if (numFavourites++ == 0)
            menu.addSectionHeader ("Favoritos");
    };

    for (auto& type : favourites.list (Kind::effect))
    {
        if (const auto* e = dsp::findEffect (type.toStdString()))
        {
            favouritesHeader();
            menu.addItem (favui::menuItem (push (addEffect (type, {})), fxui::utf8 (e->name), Kind::effect, type));
        }
    }

    for (auto& id : favourites.list (Kind::effectPreset))
    {
        const auto type = id.upToFirstOccurrenceOf ("/", false, false), presetId = id.fromFirstOccurrenceOf ("/", false, false);
        const auto* e = dsp::findEffect (type.toStdString());
        const auto* preset = e != nullptr ? dsp::findPreset (e->type, presetId.toStdString()) : nullptr;

        if (preset != nullptr)
        {
            favouritesHeader();
            menu.addItem (favui::menuItem (push (addEffect (type, presetId)), fxui::utf8 (e->name) + ": " + fxui::utf8 (preset->name),
                                           Kind::effectPreset, id));
        }
    }

    for (auto& desc : vstEffects)
    {
        if (const auto id = desc.createIdentifierString(); favourites.contains (Kind::plugin, id))
        {
            favouritesHeader();
            menu.addItem (favui::menuItem (push (addPlugin (id)), pluginLabel (desc), Kind::plugin, id));
        }
    }

    if (onlyFavourites)
    {
        if (numFavourites == 0)
            menu.addItem (-1, "Nenhum favorito ainda: passe o mouse num efeito ou preset e clique na estrela."_t, false);

        return;
    }

    //--------------------------------------------------------------------------
    // Todos, por categoria. Passar o mouse no efeito abre os presets; clicar no nome põe o padrão.
    juce::String category;

    for (auto* e : dsp::allEffects())
    {
        if (category != fxui::utf8 (e->category))
        {
            category = fxui::utf8 (e->category);
            menu.addSectionHeader (category);
        }

        const juce::String type (e->type);
        auto presets = std::make_unique<juce::PopupMenu>();
        presets->addItem (push (addEffect (type, {})), "Padrão"_t);
        presets->addSeparator();

        auto ordered = dsp::presetsFor (e->type);
        favourites.favoritesFirst (ordered, Kind::effectPreset,
                                   [&type] (const dsp::Preset* pr) { return Favorites::effectPresetId (type, pr->id); });

        for (auto* preset : ordered)
        {
            const juce::String id (preset->id);
            presets->addItem (favui::menuItem (push (addEffect (type, id)), fxui::utf8 (preset->name),
                                               Kind::effectPreset, Favorites::effectPresetId (type, id)));
        }

        // meus presets (Efeitos nativos) no fim do submenu do efeito, que agora leva a estrela (Favoritos)
        if (const auto mine = usermix::presetsFor (type); ! mine.empty())
        {
            presets->addSeparator();
            presets->addSectionHeader ("Meus presets"_t);

            for (auto& user : mine)
                presets->addItem (push (addEffect (type, user.id)), user.name);
        }

        menu.addItem (favui::menuItem (push (addEffect (type, {})), fxui::utf8 (e->name), Kind::effect, type, false, std::move (presets)));
    }

    juce::PopupMenu vst;

    for (auto& desc : vstEffects)
    {
        const auto id = desc.createIdentifierString();
        vst.addItem (favui::menuItem (push (addPlugin (id)), pluginLabel (desc), Kind::plugin, id));
    }

    menu.addSeparator();

    if (vst.getNumItems() > 0)
        menu.addSubMenu ("Plugins VST3", vst);
    else
        menu.addItem (-1, "Plugins VST3: nenhum efeito encontrado", false);
}

juce::PopupMenu InsertPanel::createAddMenu (int slot, std::vector<std::function<void()>>& actions)
{
    juce::PopupMenu menu;
    menu.addItem (filterItemId, "Só favoritos"_t, true, favouritesOnly);
    menu.addSeparator();
    addFxMenuItems (menu, actions, slot, {}, favouritesOnly);
    return menu;
}

void InsertPanel::showAddMenu (int slot, juce::Component* target)
{
    auto actions = std::make_shared<std::vector<std::function<void()>>>();
    auto menu = createAddMenu (slot, *actions);

    juce::Component::SafePointer<InsertPanel> safe (this);
    juce::Component::SafePointer<juce::Component> safeTarget (target);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [safe, safeTarget, actions, slot] (int choice)
                        {
                            if (safe == nullptr)
                                return;

                            // o filtro reabre o menu no mesmo lugar, já filtrado (ou com tudo de novo)
                            if (choice == filterItemId)
                            {
                                safe->favouritesOnly = ! safe->favouritesOnly;

                                if (safeTarget != nullptr)
                                    safe->showAddMenu (slot, safeTarget.getComponent());

                                return;
                            }

                            if (choice > 0 && choice <= (int) actions->size())
                                (*actions)[(size_t) choice - 1]();
                        });
}

void InsertPanel::showSlotMenu (int slot, juce::Component* target)
{
    const auto& p = app.getModel().get();
    const auto* m = p.findMixer (trackId);

    if (m == nullptr || slot >= (int) m->inserts.size())
        return;

    const auto ins = m->inserts[(size_t) slot];
    const auto* info = ins.kind == "whynot" ? dsp::findEffect (ins.type.toStdString()) : nullptr;
    auto actions = std::make_shared<std::vector<std::function<void()>>>();
    auto add = [&actions] (std::function<void()> f) { actions->push_back (std::move (f)); return (int) actions->size(); };
    const auto insertId = ins.id;

    juce::PopupMenu menu;
    menu.addItem (add ([this, insertId] { app.showInsertEditor (trackId, insertId); }), "Abrir janela");

    if (info != nullptr)
    {
        juce::PopupMenu presets;
        presets.addItem (add ([this, insertId] { run ("mixer.loadInsertPreset", args ({ { "track", trackId }, { "insert", insertId }, { "preset", "" } })); }),
                         "Padrão"_t, true, ins.preset.isEmpty());

        // favoritos primeiro, com a estrela em cada preset
        const juce::String type (info->type);
        auto ordered = dsp::presetsFor (info->type);
        Favorites::get().favoritesFirst (ordered, Favorites::Kind::effectPreset,
                                         [&type] (const dsp::Preset* pr) { return Favorites::effectPresetId (type, pr->id); });

        for (auto* preset : ordered)
        {
            const juce::String id (preset->id);
            presets.addItem (favui::menuItem (add ([this, insertId, id] { run ("mixer.loadInsertPreset", args ({ { "track", trackId }, { "insert", insertId }, { "preset", id } })); }),
                                              fxui::utf8 (preset->name), Favorites::Kind::effectPreset, Favorites::effectPresetId (type, id),
                                              ins.preset == id));
        }

        if (const auto mine = usermix::presetsFor (info->type); ! mine.empty())
        {
            presets.addSeparator();
            presets.addSectionHeader ("Meus presets"_t);

            for (auto& user : mine)
            {
                const auto id = user.id;
                presets.addItem (add ([this, insertId, id] { run ("mixer.loadInsertPreset", args ({ { "track", trackId }, { "insert", insertId }, { "preset", id } })); }),
                                 user.name, true, ins.preset == id);
            }
        }

        menu.addSubMenu ("Preset", presets);
    }

    // VST3: só se ele tem uma entrada de sidechain (um segundo bus de entrada).
    bool acceptsSidechain = info != nullptr && info->sidechain;

    if (ins.kind == "vst3")
        if (auto* instance = app.insertPluginInstance (trackId, ins.id))
            acceptsSidechain = instance->getBusCount (true) > 1 && instance->getTotalNumInputChannels() > instance->getChannelCountOfBus (true, 0);

    if (acceptsSidechain)
    {
        juce::PopupMenu sc;
        sc.addItem (add ([this, insertId] { run ("mixer.setInsertSidechain", args ({ { "track", trackId }, { "insert", insertId }, { "source", "" } })); }),
                    "Sem sidechain", true, ins.sidechain.isEmpty());

        for (auto& other : p.mixer)
        {
            if (other.isMaster() || other.id == trackId || p.reaches (trackId, other.id))
                continue;

            const auto source = other.id;
            sc.addItem (add ([this, insertId, source] { run ("mixer.setInsertSidechain", args ({ { "track", trackId }, { "insert", insertId }, { "source", source } })); }),
                        other.name, true, ins.sidechain == other.id);
        }

        sc.addSeparator();
        sc.addItem (-1, "Sidechain: outra trilha comanda este efeito (o kick abaixa o 808, por exemplo)."_t, false);
        menu.addSubMenu ("Sidechain (quem dispara)", sc);
    }

    menu.addItem (add ([this, insertId] { run ("mixer.setInsertBypass", args ({ { "track", trackId }, { "insert", insertId } })); }),
                  ins.bypass ? "Ligar" : "Desligar (bypass)");
    menu.addSeparator();
    menu.addItem (add ([this, insertId, slot] { run ("mixer.moveInsert", args ({ { "track", trackId }, { "insert", insertId }, { "slot", slot - 1 } })); }),
                  "Mover para cima", slot > 0);
    menu.addItem (add ([this, insertId, slot] { run ("mixer.moveInsert", args ({ { "track", trackId }, { "insert", insertId }, { "slot", slot + 1 } })); }),
                  "Mover para baixo", slot + 1 < (int) m->inserts.size());

    // Trocar: remove e põe o novo no mesmo lugar, num passo só de desfazer.
    juce::PopupMenu replace;
    addFxMenuItems (replace, *actions, slot, insertId);
    menu.addSubMenu ("Trocar por", replace);
    menu.addSeparator();
    menu.addItem (add ([this, insertId] { run ("mixer.removeInsert", args ({ { "track", trackId }, { "insert", insertId } })); }), "Remover");

    juce::Component::SafePointer<InsertPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [safe, actions] (int choice)
                        {
                            if (safe != nullptr && choice > 0 && choice <= (int) actions->size())
                                (*actions)[(size_t) choice - 1]();
                        });
}

void InsertPanel::showChainMenu()
{
    const auto& p = app.getModel().get();
    const auto* m = p.findMixer (trackId);

    if (m == nullptr)
        return;

    const auto role = m->role.isNotEmpty() ? m->role : juce::String (dsp::guessRole (m->name.toStdString()));
    juce::PopupMenu menu;
    juce::StringArray ids;

    menu.addSectionHeader (isMaster ? juce::String ("Cadeia de master por gênero"_t)
                                    : "Cadeia para " + roleName (role) + " (troca os efeitos desta trilha)");

    for (auto& s : dsp::allStyles())
    {
        const bool available = isMaster || s.recipeFor (role.toStdString()) != nullptr;
        ids.add (s.id);
        menu.addItem (ids.size(), fxui::utf8 (s.name) + (isMaster ? "  (alvo " + juce::String (s.targetLufs, 1).replaceCharacter ('.', ',') + " LUFS)" : juce::String()),
                      available);
    }

    if (! isMaster)
    {
        menu.addSeparator();
        menu.addItem (-1, "Mude o papel acima se a receita não for a certa."_t, false);
    }

    // As cadeias do artista: valem em todos os projetos (pasta de dados do app).
    constexpr int saveItem = 900, loadBase = 1000, deleteBase = 2000;
    const auto mine = usermix::chainNames();
    menu.addSeparator();
    menu.addSectionHeader ("Minhas cadeias"_t);
    menu.addItem (saveItem, "Salvar os efeitos desta trilha como cadeia…"_t, ! m->inserts.empty());

    for (int i = 0; i < mine.size(); ++i)
        menu.addItem (loadBase + i, mine[i]);

    if (! mine.isEmpty())
    {
        juce::PopupMenu remove;

        for (int i = 0; i < mine.size(); ++i)
            remove.addItem (deleteBase + i, mine[i]);

        menu.addSubMenu ("Apagar uma cadeia"_t, remove);
    }

    juce::Component::SafePointer<InsertPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chainButton),
                        [safe, ids, mine, saveItem, loadBase, deleteBase] (int choice)
                        {
                            if (safe == nullptr || choice <= 0)
                                return;

                            if (choice == saveItem)
                            {
                                safe->askChainName();
                                return;
                            }

                            if (choice >= deleteBase)
                            {
                                safe->run ("mixer.deleteChain", args ({ { "name", mine[choice - deleteBase] } }),
                                           "Cadeia \""_t + mine[choice - deleteBase] + "\" apagada (o arquivo foi para cadeias/apagadas)."_t);
                                return;
                            }

                            if (choice >= loadBase)
                            {
                                safe->run ("mixer.loadChain", args ({ { "track", safe->trackId }, { "name", mine[choice - loadBase] } }),
                                           "Cadeia \""_t + mine[choice - loadBase] + "\" carregada em "_t + safe->trackName + ". Ctrl+Z desfaz.");
                                return;
                            }

                            const auto style = ids[choice - 1];
                            safe->run ("mixer.applyStyle", args ({ { "style", style }, { "tracks", list ({ safe->trackId }) } }),
                                       "Cadeia de "_t + fxui::utf8 (dsp::findStyle (style.toStdString())->name) + " aplicada em "
                                           + safe->trackName + ". Ctrl+Z desfaz.");
                        });
}

void InsertPanel::askChainName()
{
    nameDialog = std::make_unique<juce::AlertWindow> ("Salvar minha cadeia"_t,
                                                      "Nome da cadeia com os efeitos de "_t + trackName
                                                          + ". Ela fica no app e aparece em \"Minhas cadeias\" em todos os projetos."_t,
                                                      juce::MessageBoxIconType::NoIcon, this);
    nameDialog->addTextEditor ("name", trackName);
    nameDialog->addButton ("Salvar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<InsertPanel> safe (this);
    nameDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr || safe->nameDialog == nullptr)
            return;

        const auto name = safe->nameDialog->getTextEditorContents ("name").trim();
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->nameDialog.reset(); });

        if (result == 1 && name.isNotEmpty())
            safe->run ("mixer.saveChain", args ({ { "track", safe->trackId }, { "name", name } }),
                       "Cadeia \""_t + name + "\" salva. Ela aparece em \"Cadeia pronta\" de qualquer trilha."_t);
    }), false);
}

void InsertPanel::showSendMenu()
{
    const auto& p = app.getModel().get();
    const auto* m = p.findMixer (trackId);

    if (m == nullptr)
        return;

    juce::PopupMenu menu;
    juce::StringArray ids;
    auto addTarget = [&] (const MixerTrack& t)
    {
        ids.add (t.id);
        menu.addItem (ids.size(), t.name);
    };

    auto isBus = [] (const MixerTrack& t)
    {
        const auto role = t.role.isNotEmpty() ? t.role : juce::String (dsp::guessRole (t.name.toStdString()));
        return role == "reverbBus" || role == "delayBus" || role == "bus";
    };

    auto valid = [&] (const MixerTrack& t)
    {
        return ! t.isMaster() && t.id != trackId && m->findSend (t.id) == nullptr && ! p.reaches (t.id, trackId);
    };

    menu.addSectionHeader ("Buses");

    for (auto& t : p.mixer)
        if (valid (t) && isBus (t))
            addTarget (t);

    const int busCount = ids.size();
    menu.addItem (1000, "Novo bus de reverb (placa)");
    menu.addItem (1001, "Novo bus de delay (1/4)");
    menu.addSectionHeader ("Outras trilhas");

    for (auto& t : p.mixer)
        if (valid (t) && ! isBus (t))
            addTarget (t);

    menu.addSeparator();
    menu.addItem (-1, "Bus: uma trilha que recebe o som de várias outras (um reverb só para todas as vozes)."_t, false);

    juce::ignoreUnused (busCount);
    juce::Component::SafePointer<InsertPanel> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addSendButton),
                        [safe, ids] (int choice)
                        {
                            if (safe == nullptr || choice <= 0)
                                return;

                            if (choice == 1000)      safe->createBus ("reverbBus");
                            else if (choice == 1001) safe->createBus ("delayBus");
                            else if (choice <= ids.size())
                                safe->run ("mixer.setSend", args ({ { "track", safe->trackId }, { "to", ids[choice - 1] }, { "db", -12.0 } }));
                        });
}

void InsertPanel::createBus (const juce::String& role)
{
    const bool reverb = role == "reverbBus";
    const CommandRegistry::ScopedGesture gesture (app.getCommands(), reverb ? "Novo bus de reverb" : "Novo bus de delay");

    const auto created = app.run ("mixer.addTrack", args ({ { "name", reverb ? "Reverb" : "Delay" }, { "role", role } }));

    if (! created.ok)
    {
        if (onStatus) onStatus (created.error, true);
        return;
    }

    const auto bus = created.value["track"].toString();
    run ("mixer.addInsert", args ({ { "track", bus }, { "type", reverb ? "reverb" : "delay" }, { "preset", reverb ? "plate-vocal" : "quarter-vocal" } }));
    run ("mixer.addInsert", args ({ { "track", bus }, { "type", "eq" }, { "preset", reverb ? "reverb-return" : "delay-return" } }));
    run ("mixer.setSend", args ({ { "track", trackId }, { "to", bus }, { "db", -12.0 } }),
         (reverb ? "Bus de reverb criado e ligado a "_t : "Bus de delay criado e ligado a "_t) + trackName + ".");
}

//==============================================================================
int InsertPanel::slotAt (juce::Point<int> p) const
{
    for (int i = 0; i < slots.size(); ++i)
        if (slots[i]->getBounds().contains (p))
            return i;

    return -1;
}

bool InsertPanel::isInterestedInDragSource (const SourceDetails& d)
{
    const auto s = d.description.toString();
    return s.startsWith ("insert:" + trackId + ":") || s.startsWith ("plugin:");
}

void InsertPanel::itemDragMove (const SourceDetails& d)
{
    const int s = slotAt (d.localPosition);

    if (s != dropSlot)
    {
        dropSlot = s;
        repaint();

        for (auto* slot : slots)
            slot->repaint();
    }
}

void InsertPanel::itemDragExit (const SourceDetails&)
{
    dropSlot = -1;

    for (auto* slot : slots)
        slot->repaint();
}

void InsertPanel::itemDropped (const SourceDetails& d)
{
    const int target = slotAt (d.localPosition);
    dropSlot = -1;

    for (auto* slot : slots)
        slot->repaint();

    if (target < 0)
        return;

    const auto s = d.description.toString();

    if (s.startsWith ("plugin:"))
    {
        run ("mixer.addPluginInsert", args ({ { "track", trackId }, { "plugin", s.fromFirstOccurrenceOf ("plugin:", false, false) }, { "slot", target } }));
        return;
    }

    const auto insertId = s.fromLastOccurrenceOf (":", false, false);
    const auto* m = app.getModel().get().findMixer (trackId);

    if (m == nullptr)
        return;

    const int count = (int) m->inserts.size();
    run ("mixer.moveInsert", args ({ { "track", trackId }, { "insert", insertId }, { "slot", juce::jmin (target, count - 1) } }));
}

} // namespace whynot
