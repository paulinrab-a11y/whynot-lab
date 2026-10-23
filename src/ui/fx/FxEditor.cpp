#include "FxEditor.h"
#include "../FavoriteStar.h"
#include "../MainWindow.h"
#include "../Theme.h"
#include "../../app/AppController.h"
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

    // "id" ou "id:LEGENDA": dentro de um grupo a legenda do knob pode ser mais curta que o nome
    // do parâmetro (no grupo SINO 1, "GANHO" em vez de "GANHO 1").
    struct GroupSpec
    {
        const char* title;
        std::vector<const char*> ids;
    };

    std::vector<GroupSpec> groupsFor (const juce::String& type)
    {
        if (type == "eq")
            return { { "CORTE GRAVE", { "hpOn:LIGA", "hpFreq:FREQUÊNCIA", "hpSlope:INCLINAÇÃO" } },
                     { "GRAVE", { "lowFreq:FREQUÊNCIA", "lowGain:GANHO" } },
                     { "SINO 1", { "b1Freq:FREQUÊNCIA", "b1Gain:GANHO", "b1Q:LARGURA" } },
                     { "SINO 2", { "b2Freq:FREQUÊNCIA", "b2Gain:GANHO", "b2Q:LARGURA" } },
                     { "SINO 3", { "b3Freq:FREQUÊNCIA", "b3Gain:GANHO", "b3Q:LARGURA" } },
                     { "AGUDO", { "highFreq:FREQUÊNCIA", "highGain:GANHO" } },
                     { "CORTE AGUDO", { "lpOn:LIGA", "lpFreq:FREQUÊNCIA", "lpSlope:INCLINAÇÃO" } },
                     { "SAÍDA", { "output:GANHO" } } };
        if (type == "comp")
            return { { "NÍVEL", { "threshold", "ratio", "knee" } }, { "TEMPO", { "attack", "release" } },
                     { "SAÍDA", { "makeup", "mix" } }, { "DISPARO", { "detector", "scHpf:FILTRO" } } };
        if (type == "sat")
            return { { "COR", { "mode", "drive", "tone" } }, { "GRAVE", { "protect" } }, { "SAÍDA", { "mix", "autoGain", "output" } } };
        if (type == "reverb")
            return { { "ESPAÇO", { "mode", "size", "decay", "predelay" } }, { "TOM", { "damping", "lowCut" } }, { "SAÍDA", { "width", "mix" } } };
        if (type == "delay")
            return { { "TEMPO", { "sync", "division", "time" } }, { "REPETIÇÕES", { "feedback", "mode" } },
                     { "TOM", { "hpf", "lpf" } }, { "SAÍDA", { "ducking", "width", "mix" } } };
        if (type == "limiter")
            return { { "ENTRADA", { "gain", "clip" } }, { "SAÍDA", { "ceiling", "release" } }, { "LOUDNESS", { "target" } } };
        if (type == "stereo")
            return { { "IMAGEM", { "width", "monoBass", "mono" } }, { "NÍVEL", { "gain", "pan" } }, { "FASE", { "invertLeft", "invertRight" } } };
        if (type == "deesser")
            return { { "DETECÇÃO", { "freq", "threshold" } }, { "REDUÇÃO", { "range:MÁXIMA", "mode" } }, { "ESCUTA", { "listen:OUVIR" } } };
        if (type == "transient")
            return { { "FORMA", { "attack", "sustain" } }, { "SAÍDA", { "output", "mix" } } };
        if (type == "filter")
            return { { "CORTE", { "mode", "cutoff", "resonance", "slope" } }, { "SAÍDA", { "drive", "mix" } } };
        if (type == "meter")
            return { { "REFERÊNCIA", { "target" } } };
        if (type == "tune")
            return { { "NOTAS", { "key", "scale", "follow:DO PROJETO" } }, { "CORREÇÃO", { "speed", "amount", "humanize" } },
                     { "VOZ", { "range", "mix" } } };
        if (type == "voicepitch")
            return { { "TOM", { "semitones", "formant" } }, { "VOZ", { "range", "mix" } } };
        if (type == "harmony")
            return { { "NOTAS", { "key", "scale", "follow:DO PROJETO" } }, { "VOZES", { "voices", "formant", "level:NÍVEL", "width" } } };
        if (type == "gate")
            return { { "DETECÇÃO", { "mode", "threshold", "hysteresis", "scHpf:FILTRO" } }, { "FECHA", { "range:REDUÇÃO", "ratio" } },
                     { "TEMPO", { "attack", "hold", "release" } } };
        if (type == "opto")
            return { { "COMPRESSÃO", { "reduction", "mode", "emphasis:AGUDO" } }, { "SAÍDA", { "gain", "mix" } } };
        if (type == "fet")
            return { { "NÍVEL", { "threshold", "ratio" } }, { "TEMPO", { "attack", "release" } }, { "SAÍDA", { "makeup", "color", "mix" } } };
        if (type == "multiband")
            return { { "DIVISÕES", { "x1:DIVISÃO 1", "x2:DIVISÃO 2", "x3:DIVISÃO 3" } },
                     { "GRAVE", { "b1Thr:LIMIAR", "b1Ratio:RAZÃO", "b1Gain:GANHO" } },
                     { "MÉDIO-GRAVE", { "b2Thr:LIMIAR", "b2Ratio:RAZÃO", "b2Gain:GANHO" } },
                     { "MÉDIO-AGUDO", { "b3Thr:LIMIAR", "b3Ratio:RAZÃO", "b3Gain:GANHO" } },
                     { "AGUDO", { "b4Thr:LIMIAR", "b4Ratio:RAZÃO", "b4Gain:GANHO" } },
                     { "GERAL", { "attack", "release", "solo" } }, { "SAÍDA", { "output" } } };
        if (type == "leveler")
            return { { "ALVO", { "target", "range" } }, { "REAÇÃO", { "speed", "sensitivity" } }, { "SAÍDA", { "output" } } };
        if (type == "doubler")
            return { { "CÓPIAS", { "mode", "voices", "level:NÍVEL" } }, { "TEMPO E TOM", { "delay", "detune" } },
                     { "IMAGEM", { "width", "hpf:CORTE GRAVE" } } };
        if (type == "bassplus")
            return { { "GRAVE", { "freq", "sub:SUB" } }, { "HARMÔNICOS", { "amount:QUANTIDADE", "color" } }, { "SAÍDA", { "output" } } };
        if (type == "denoise")
            return { { "LIMPEZA", { "reduction", "threshold" } }, { "SOM", { "smoothing", "listen:OUVIR" } } };
        if (type == "console")
            return { { "ENTRADA", { "hpf:CORTE", "gate" } }, { "COMPRESSOR", { "comp:LIMIAR", "ratio", "fast:RÁPIDO" } },
                     { "EQ", { "low", "mid", "midFreq:FREQ.", "high" } }, { "SAÍDA", { "drive", "output" } } };
        if (type == "vintageEq")
            return { { "GRAVE", { "lowFreq:FREQ.", "lowBoost:REFORÇO", "lowAtten:ATENUA" } },
                     { "AGUDO", { "highFreq:FREQ.", "highBoost:REFORÇO", "highWidth:LARGURA" } },
                     { "AR E SAÍDA", { "airFreq:CORTE EM", "highAtten:ATENUA", "output" } } };

        return {};
    }

    constexpr int cellW = ParamControl::cellWidth, cellH = ParamControl::cellHeight;
    constexpr int maxCellsInRow = 9;
}

FxEditor::FxEditor (AppController& a, WhynotFxPlugin& p)
    : app (a), keepAlive (&p), plugin (p), info (p.getInfo()),
      trackId (p.getOwnerTrack()), insertId (p.getOwnerInsert())
{
    setOpaque (true);
    setWantsKeyboardFocus (true);   // clicar num knob deixa o foco aqui: os atalhos continuam valendo
    byIndex.assign (info.params.size(), nullptr);

    for (size_t i = 0; i < info.params.size(); ++i)
    {
        const auto& param = info.params[i];

        if (param.hidden)
            continue;

        auto* c = controls.add (new ParamControl (param));
        byIndex[i] = c;
        const int index = (int) i;
        c->onChange = [this, index] (float v) { sendParam (index, v); };
        c->onGestureStart = [this, c] { beginGesture (fxui::utf8 (c->getInfo().name)); };
        c->onGestureEnd = [this] { endGesture(); };
        addAndMakeVisible (c);
    }

    presetButton.setTooltip ("Presets: pontos de partida por papel e gênero"_t);
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    bypassButton.setClickingTogglesState (false);
    bypassButton.setTooltip ("Liga ou desliga o efeito (bypass sem estalo)");
    bypassButton.onClick = [this] { app.run ("mixer.setInsertBypass", args ({ { "track", trackId }, { "insert", insertId } })); };
    addAndMakeVisible (bypassButton);

    if (info.sidechain)
    {
        theme::styleComboBox (sidechainBox);
        sidechainBox.setTooltip ("Disparo (sidechain): outra trilha comanda o efeito (o kick abaixa o 808, a voz abre espaço no beat, "_t
                                 "o chimbal abre o gate)."_t);
        sidechainBox.onChange = [this]
        {
            const int i = sidechainBox.getSelectedId() - 1;
            const auto source = i <= 0 ? juce::String() : sidechainIds[i];
            const auto result = app.run ("mixer.setInsertSidechain", args ({ { "track", trackId }, { "insert", insertId }, { "source", source } }));

            if (! result.ok)
                refresh();
        };
        addAndMakeVisible (sidechainBox);
    }

    visual = FxVisual::create (plugin);

    if (visual != nullptr)
    {
        visual->onParam = [this] (int index, float v) { sendParam (index, v); };
        visual->onGestureStart = [this] { beginGesture (fxui::utf8 (info.name)); };
        visual->onGestureEnd = [this] { endGesture(); };
        addAndMakeVisible (*visual);
    }

    buildGroups();

    // Tamanho: grupos lado a lado quando cabem numa linha; senão, cada grupo vira uma coluna.
    int cells = 0, rows = 1;

    for (auto& g : groups)
    {
        cells += (int) g.controls.size();
        rows = juce::jmax (rows, (int) g.controls.size());
    }

    const bool columns = cells > maxCellsInRow;
    const int contentWidth = columns ? (int) groups.size() * (cellW + 8) : cells * cellW + ((int) groups.size() - 1) * 16;
    const int contentHeight = groupTitle + (columns ? rows : 1) * cellH;
    const int visualHeight = visual != nullptr ? FxVisual::heightFor (plugin.getFxType()) + theme::metric::gap : 0;

    setSize (juce::jmax (info.sidechain ? 720 : 600, contentWidth + 2 * theme::metric::pad),
             headerHeight + visualHeight + contentHeight + theme::metric::pad + 8);

    app.getModel().addListener (this);
    refresh();
    startTimerHz (30);
}

FxEditor::~FxEditor()
{
    while (gestureDepth > 0)
        endGesture();

    app.getModel().removeListener (this);
}

void FxEditor::buildGroups()
{
    groups.clear();
    std::vector<bool> used (info.params.size(), false);

    for (auto& spec : groupsFor (plugin.getFxType()))
    {
        Group g { juce::String::fromUTF8 (spec.title), {} };

        for (auto* entry : spec.ids)
        {
            const auto text = juce::String::fromUTF8 (entry);
            const auto id = text.upToFirstOccurrenceOf (":", false, false);

            if (const int i = info.indexOf (id.toRawUTF8()); i >= 0 && byIndex[(size_t) i] != nullptr)
            {
                g.controls.push_back (byIndex[(size_t) i]);
                used[(size_t) i] = true;

                if (text.containsChar (':'))
                    byIndex[(size_t) i]->setCaption (text.fromFirstOccurrenceOf (":", false, false));
            }
        }

        if (! g.controls.empty())
            groups.push_back (std::move (g));
    }

    // O que não está em nenhum grupo vai num grupo "OUTROS" (efeito novo sem tabela).
    Group rest { "AJUSTES", {} };

    for (size_t i = 0; i < byIndex.size(); ++i)
        if (byIndex[i] != nullptr && ! used[i])
            rest.controls.push_back (byIndex[i]);

    if (! rest.controls.empty())
        groups.push_back (std::move (rest));
}

const Insert* FxEditor::currentInsert() const
{
    const auto* m = app.getModel().get().findMixer (trackId);
    return m != nullptr ? m->findInsert (insertId) : nullptr;
}

void FxEditor::projectChanged (const ProjectModel&)
{
    refresh();
}

void FxEditor::refresh()
{
    const auto& p = app.getModel().get();
    const auto* ins = currentInsert();

    if (ins == nullptr)
        return;   // o efeito saiu: o Tracktion fecha a janela junto com o plugin

    std::vector<float> values;

    for (size_t i = 0; i < info.params.size(); ++i)
    {
        const auto& param = info.params[i];
        float v = param.def;

        if (std::string_view (param.id) == "bpm")
            v = (float) p.bpm;
        else if (auto it = ins->params.find (param.id); it != ins->params.end())
            v = it->second;

        values.push_back (v);

        if (byIndex[i] != nullptr && byIndex[i]->getValue() != v)
            byIndex[i]->setValue (v);
    }

    if (visual != nullptr)
        visual->setValues (values);

    bypassed = ins->bypass;
    bypassButton.setButtonText (bypassed ? "Desligado" : "Ligado");
    bypassButton.setToggleState (! bypassed, juce::dontSendNotification);

    const auto user = usermix::isUserPresetId (ins->preset) ? usermix::findPreset (info.type, ins->preset) : std::nullopt;
    const auto* preset = ins->preset.isEmpty() || user ? nullptr : dsp::findPreset (info.type, ins->preset.toStdString());
    presetName = user ? user->name : preset != nullptr ? fxui::utf8 (preset->name) : juce::String ("Padrão"_t);

    // Preset alterado à mão ganha um asterisco.
    if (preset != nullptr || user || ins->preset.isEmpty())
    {
        auto reference = dsp::presetValues (info, preset);

        if (user)
            for (auto& [id, v] : user->params)
                if (const int i = info.indexOf (id.toRawUTF8()); i >= 0)
                    reference[(size_t) i] = info.params[(size_t) i].clamp (v);

        // A afinação e a harmonia não guardam o tom no preset: o tom e o "seguir" não contam como mudança.
        for (auto* id : { "key", "scale", "follow" })
            if (const int i = info.indexOf (id); i >= 0 && (std::string_view (info.type) == "tune" || std::string_view (info.type) == "harmony"))
                reference[(size_t) i] = values[(size_t) i];

        for (size_t i = 0; i < info.params.size(); ++i)
            if (! info.params[i].hidden && std::abs (reference[i] - values[i]) > 1.0e-3f)
            {
                presetName << "*";
                break;
            }
    }

    presetButton.setButtonText (presetName + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")));

    if (auto* m = p.findMixer (trackId))
        trackName = m->isMaster() ? juce::String ("Master") : m->name;

    if (info.sidechain)
    {
        sidechainBox.clear (juce::dontSendNotification);
        sidechainIds.clear();
        sidechainIds.add ({});
        sidechainBox.addItem ("Sem sidechain", 1);

        for (auto& m : p.mixer)
        {
            if (m.isMaster() || m.id == trackId || p.reaches (trackId, m.id))
                continue;

            sidechainIds.add (m.id);
            sidechainBox.addItem ("Disparo: "_t + m.name, sidechainIds.size());
        }

        sidechainBox.setSelectedId (juce::jmax (1, sidechainIds.indexOf (ins->sidechain) + 1), juce::dontSendNotification);
    }

    updateWindowTitle();
    repaint();
}

void FxEditor::timerCallback()
{
    if (visual != nullptr)
        visual->tick();

    // O knob que "age agora" ganha arco laranja: o limiar quando há redução de ganho.
    const auto& fxType = plugin.getFxType();

    if (fxType == "comp" || fxType == "limiter" || fxType == "opto" || fxType == "fet")
    {
        const float reduction = bypassed ? 0.0f : plugin.getHost().processor().meter (0);
        const int i = info.indexOf (fxType == "limiter" ? "gain" : fxType == "opto" ? "reduction" : "threshold");

        if (i >= 0 && byIndex[(size_t) i] != nullptr)
            byIndex[(size_t) i]->setAccent (reduction > 0.5f);
    }
}

void FxEditor::sendParam (int index, float value)
{
    const auto result = app.run ("mixer.setInsertParam", args ({ { "track", trackId }, { "insert", insertId },
                                                                 { "param", info.params[(size_t) index].id }, { "value", value } }));
    jassert (result.ok);
    juce::ignoreUnused (result);
}

void FxEditor::beginGesture (const juce::String& name)
{
    ++gestureDepth;
    app.getCommands().beginGesture (name);
}

void FxEditor::endGesture()
{
    if (gestureDepth > 0)
    {
        --gestureDepth;
        app.getCommands().endGesture();
    }
}

void FxEditor::showPresetMenu()
{
    const auto* ins = currentInsert();

    if (ins == nullptr)
        return;

    juce::PopupMenu menu;
    juce::StringArray ids;
    ids.add ({});
    menu.addItem (1, "Padrão"_t, true, ins->preset.isEmpty());
    menu.addSeparator();

    // favoritos primeiro, com a estrela em cada preset (desmarca sem fechar o menu)
    const juce::String type (info.type);
    auto ordered = dsp::presetsFor (info.type);
    Favorites::get().favoritesFirst (ordered, Favorites::Kind::effectPreset,
                                     [&type] (const dsp::Preset* pr) { return Favorites::effectPresetId (type, pr->id); });

    for (auto* preset : ordered)
    {
        ids.add (preset->id);
        menu.addItem (favui::menuItem (ids.size(), fxui::utf8 (preset->name), Favorites::Kind::effectPreset,
                                       Favorites::effectPresetId (type, preset->id), ins->preset == preset->id));
    }

    // Os presets do artista (pasta de dados do app, valem em todos os projetos).
    const auto mine = usermix::presetsFor (info.type);
    juce::StringArray mineIds;

    if (! mine.empty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Meus presets"_t);

        for (auto& p : mine)
        {
            ids.add (p.id);
            juce::PopupMenu::Item item (p.name);
            item.itemID = ids.size();
            item.setTicked (ins->preset == p.id);
            menu.addItem (item);
        }
    }

    constexpr int saveItem = 10000, deleteBase = 20000;
    menu.addSeparator();
    menu.addItem (saveItem, "Salvar como meu preset…"_t);

    if (! mine.empty())
    {
        juce::PopupMenu remove;

        for (size_t i = 0; i < mine.size(); ++i)
        {
            mineIds.add (mine[i].id);
            remove.addItem (deleteBase + (int) i, mine[i].name);
        }

        menu.addSubMenu ("Apagar meu preset"_t, remove);
    }

    juce::Component::SafePointer<FxEditor> safe (this);   // o `type` (info.type) é o de cima, dos favoritos

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe, ids, mineIds, type, saveItem, deleteBase] (int choice)
                        {
                            if (safe == nullptr || choice <= 0)
                                return;

                            if (choice == saveItem)
                            {
                                safe->askPresetName();
                                return;
                            }

                            if (choice >= deleteBase)
                            {
                                safe->app.run ("mixer.deleteInsertPreset", args ({ { "type", type }, { "preset", mineIds[choice - deleteBase] } }));
                                safe->refresh();
                                return;
                            }

                            safe->app.run ("mixer.loadInsertPreset", args ({ { "track", safe->trackId }, { "insert", safe->insertId },
                                                                              { "preset", ids[choice - 1] } }));
                        });
}

void FxEditor::askPresetName()
{
    nameDialog = std::make_unique<juce::AlertWindow> ("Salvar meu preset"_t,
                                                      "Nome do preset de "_t + fxui::utf8 (info.name)
                                                          + ". Ele fica no app e aparece em \"Meus presets\" em todos os projetos."_t,
                                                      juce::MessageBoxIconType::NoIcon, this);
    nameDialog->addTextEditor ("name", presetName.trimCharactersAtEnd ("*"));
    nameDialog->addButton ("Salvar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FxEditor> safe (this);
    nameDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr || safe->nameDialog == nullptr)
            return;

        const auto name = safe->nameDialog->getTextEditorContents ("name").trim();
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->nameDialog.reset(); });

        if (result != 1 || name.isEmpty())
            return;

        const auto saved = safe->app.run ("mixer.saveInsertPreset", args ({ { "track", safe->trackId }, { "insert", safe->insertId },
                                                                            { "name", name } }));

        // O nome novo aparece no topo da janela (carregar devolve os mesmos valores: o som não muda).
        if (saved.ok)
            safe->app.run ("mixer.loadInsertPreset", args ({ { "track", safe->trackId }, { "insert", safe->insertId },
                                                              { "preset", saved.value["preset"] } }));
    }), false);
}

void FxEditor::parentHierarchyChanged()
{
    updateWindowTitle();

    juce::Component::SafePointer<FxEditor> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });
}

bool FxEditor::keyPressed (const juce::KeyPress& key)
{
    // Com a janela do efeito em foco, Ctrl+Z, espaço e os outros atalhos agem no app inteiro.
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
        if (auto* main = dynamic_cast<MainWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
            if (auto* content = main->getContentComponent())
                return content->keyPressed (key);

    return false;
}

void FxEditor::updateWindowTitle()
{
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
        w->setName (fxui::utf8 (info.name) + " - " + trackName);
}

void FxEditor::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (metric::pad, 0);
    g.setColour (bypassed ? colour::textDim : colour::text);
    g.setFont (displayFont (20.0f));
    g.drawText (fxui::utf8 (info.name), header.removeFromTop (34).withTrimmedTop (10), juce::Justification::bottomLeft);
    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawText (trackName + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + fxui::utf8 (info.category),
                header, juce::Justification::topLeft);

    g.setColour (colour::line);
    g.drawHorizontalLine (headerHeight - 1, (float) metric::pad, (float) (getWidth() - metric::pad));

    // títulos e separadores dos grupos
    g.setFont (captionFont());

    for (auto& grp : groups)
    {
        if (grp.controls.empty())
            continue;

        juce::Rectangle<int> area = grp.controls.front()->getBounds();

        for (auto* c : grp.controls)
            area = area.getUnion (c->getBounds());

        g.setColour (colour::textDim);
        g.drawText (grp.title, area.getX(), area.getY() - groupTitle, area.getWidth(), groupTitle - 4, juce::Justification::centred);
        g.setColour (colour::line);
        g.drawHorizontalLine (area.getY() - 3, (float) area.getX() + 6.0f, (float) area.getRight() - 6.0f);
    }
}

void FxEditor::resized()
{
    using namespace theme;
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight).reduced (metric::pad, 12);

    bypassButton.setBounds (header.removeFromRight (104));
    header.removeFromRight (metric::gap);
    presetButton.setBounds (header.removeFromRight (190));

    if (info.sidechain)
    {
        header.removeFromRight (metric::gap);
        sidechainBox.setBounds (header.removeFromRight (180));
    }

    r.reduce (metric::pad, 0);
    r.removeFromTop (metric::gap);

    if (visual != nullptr)
    {
        visual->setBounds (r.removeFromTop (FxVisual::heightFor (plugin.getFxType())));
        r.removeFromTop (metric::gap);
    }

    r.removeFromTop (groupTitle);

    int cells = 0;

    for (auto& grp : groups)
        cells += (int) grp.controls.size();

    if (cells > maxCellsInRow)
    {
        const int total = (int) groups.size() * (cellW + 8);
        int x = r.getX() + (r.getWidth() - total) / 2;

        for (auto& grp : groups)
        {
            int y = r.getY();

            for (auto* c : grp.controls)
            {
                c->setBounds (x + 4, y, cellW, cellH);
                y += cellH;
            }

            x += cellW + 8;
        }
    }
    else
    {
        const int total = cells * cellW + ((int) groups.size() - 1) * 16;
        int x = r.getX() + (r.getWidth() - total) / 2;

        for (auto& grp : groups)
        {
            for (auto* c : grp.controls)
            {
                c->setBounds (x, r.getY(), cellW, cellH);
                x += cellW;
            }

            x += 16;
        }
    }
}

} // namespace whynot
