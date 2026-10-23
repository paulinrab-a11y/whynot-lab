#include "SynthEditor.h"
#include "../MainWindow.h"
#include "../Theme.h"
#include "../../app/AppController.h"
#include "../../engine/SynthFavorites.h"
#include "../../engine/SynthState.h"

namespace whynot
{

using namespace synth;

namespace
{
    juce::String utf8 (const char* s)          { return juce::String::fromUTF8 (s); }
    juce::String utf8 (const std::string& s)   { return juce::String::fromUTF8 (s.c_str()); }

    juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();
        for (auto& [k, v] : kv) o->setProperty (k, v);
        return juce::var (o);
    }

    constexpr int cellH = ParamControl::cellHeight;

    // Listas com rótulo longo ("Passa-baixas", "Só no slide", "Corte do filtro"): com 80 px o Theme
    // encolhia e punha reticências ("Passa-ba…"); com 96 cabem.
    constexpr int wideList = 96;
}

//==============================================================================
SynthEditor::SynthEditor (AppController& a, WhynotSynthPlugin& p)
    : app (a), keepAlive (&p), plugin (p), channelId (p.getOwnerChannel())
{
    setOpaque (true);
    setWantsKeyboardFocus (true);   // clicar num knob deixa o foco aqui: os atalhos continuam valendo

    controls.resize ((size_t) numParams);

    for (int i = 0; i < numParams; ++i)
    {
        auto c = std::make_unique<ParamControl> (param (i));
        c->onChange = [this, i] (float v) { sendParam (i, v); };
        c->onGestureStart = [this, i] { beginGesture (utf8 (param (i).name)); };
        c->onGestureEnd = [this] { endGesture(); };
        addChildComponent (*c);
        controls[(size_t) i] = std::move (c);
    }

    browser.onChoose = [this] (const juce::String& id) { choosePreset (id); };
    browser.onFavouritesChanged = [this] { refresh(); };   // a estrela da lista (ou a IA) muda a do cabeçalho
    addAndMakeVisible (browser);

    variarMenuButton.setTooltip ("Quanto variar: sutil, médio ou forte"_t);
    previewButton.setTooltip ("Toca uma nota com o timbre atual (Prévia)"_t);
    presetButton.setTooltip ("Escolher preset por categoria");

    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    starButton.onClick = [this]
    {
        if (presetId.isNotEmpty() && presetId != "init")
        {
            app.run ("synth.setFavorite", args ({ { "preset", presetId } }));
            browser.refresh();
            refresh();
        }
    };
    variarButton.onClick = [this] { variar (variation); };
    variarMenuButton.onClick = [this] { showVariarMenu(); };
    previewButton.onClick = [this] { preview(); };
    presetButton.onClick = [this] { showPresetMenu(); };

    for (auto* b : std::initializer_list<juce::Component*> { &prevButton, &nextButton, &starButton, &variarButton,
                                                              &variarMenuButton, &previewButton, &presetButton })
        addAndMakeVisible (b);

    pages.setItems ({ { "Camadas", Icon::none, "Osciladores: até 4 camadas e o sub"_t },
                      { "Filtro e envelopes", Icon::none, "Filtro e os três envelopes"_t },
                      { "Modulação"_t, Icon::none, "LFOs e a matriz de modulação"_t },
                      { "Efeitos e voz", Icon::none, "Saturação, EQ, estéreo, delay, reverb, polifonia e glide"_t } });
    pages.onChange = [this] (int i) { showPage (i, layer); };
    addAndMakeVisible (pages);

    layers.onChange = [this] (int i) { showPage (0, i); };
    addChildComponent (layers);

    addChildComponent (wave);
    addChildComponent (filterView);

    for (auto& v : envViews) addChildComponent (v);
    for (auto& v : lfoViews) addChildComponent (v);

    setSize (editorWidth, editorHeight);
    app.getModel().addListener (this);
    refresh();
    showPage (0, 0);
}

SynthEditor::~SynthEditor()
{
    while (gestureDepth > 0)
        endGesture();

    app.getModel().removeListener (this);
}

//==============================================================================
const Channel* SynthEditor::channel() const
{
    return app.getModel().get().findChannel (channelId);
}

std::vector<float> SynthEditor::currentValues() const
{
    const auto* ch = channel();
    return ch != nullptr ? synthstate::valuesFor (*ch, app.getModel().get().bpm) : defaultValues();
}

void SynthEditor::projectChanged (const ProjectModel&)
{
    refresh();
    layoutPage();   // o tipo da camada pode ter mudado (cada tipo tem os seus controles)
}

void SynthEditor::refresh()
{
    const auto* ch = channel();

    if (ch == nullptr || ! ch->isSynth())
        return;   // o canal saiu: o Tracktion fecha a janela junto com o plugin

    const auto values = currentValues();

    for (int i = 0; i < numParams; ++i)
        if (controls[(size_t) i]->getValue() != values[(size_t) i])
            controls[(size_t) i]->setValue (values[(size_t) i]);

    channelName = ch->name;
    presetId = ch->synthPreset;
    const auto* preset = findPreset (presetId.toStdString());
    presetName = preset != nullptr ? utf8 (preset->name) : juce::String ("Sem preset");
    modified = preset == nullptr || ! synthstate::matchesPreset (*ch);

    presetInfo = {};

    if (preset != nullptr && preset->id != "init")
    {
        juce::StringArray genres;

        for (auto& g : allGenres())
            if (preset->hasGenre (g.id))
                genres.add (utf8 (g.name));

        const auto* cat = findCategory (preset->category);
        presetInfo = (cat != nullptr ? utf8 (cat->name) : juce::String()) + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  "
                     + genres.joinIntoString (", ");
    }

    presetButton.setButtonText (presetName + (modified ? "*" : "") + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")));
    const bool favourite = synthfavorites::isFavorite (presetId);
    starButton.setToggleState (favourite, juce::dontSendNotification);
    starButton.setIcon (favourite ? Icon::starFilled : Icon::star);
    starButton.setEnabled (preset != nullptr && preset->id != "init");
    browser.setCurrent (presetId);

    // Abas das camadas: o que cada uma toca agora.
    std::vector<SegmentedControl::Item> items;

    for (int l = 0; l < numLayers; ++l)
    {
        auto at = [&values, l] (int p) { return values[(size_t) P::layer (l, p)]; };
        juce::String what = "desligada";

        if (at (lOn) >= 0.5f)
        {
            const auto type = (LayerType) juce::roundToInt (at (lType));
            const int choice = type == LayerType::analog ? P::layer (l, lWave)
                             : type == LayerType::wavetable ? P::layer (l, lTable) : P::layer (l, lType);
            what = utf8 (param (choice).choices[(size_t) juce::roundToInt (values[(size_t) choice])]);
        }

        items.push_back ({ juce::String (l + 1) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + what, Icon::none,
                           "Camada "_t + juce::String (l + 1) });
    }

    items.push_back ({ values[(size_t) P::subOn] >= 0.5f ? juce::String ("Sub") : juce::String ("Sub (desligado)"), Icon::none,
                       "Oscilador uma ou duas oitavas abaixo"_t });
    layers.setItems (items);
    layers.setSelected (layer);

    wave.show (values, layer < numLayers ? layer : -1);
    filterView.show (values);

    for (int e = 0; e < 3; ++e)
        envViews[e].show (values, e);

    for (int l = 0; l < 2; ++l)
        lfoViews[l].show (values, l);

    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
        w->setName ("WHYNOT Synth - " + channelName);

    repaint();
}

//==============================================================================
void SynthEditor::sendParam (int index, float value)
{
    CommandResult result;

    if (index == P::mono)
        result = app.run ("channel.setMono", args ({ { "channel", channelId }, { "on", value >= 0.5f } }));
    else if (index == P::glide)
        result = app.run ("channel.setGlide", args ({ { "channel", channelId }, { "ms", value } }));
    else
        result = app.run ("synth.setParam", args ({ { "channel", channelId }, { "param", param (index).id }, { "value", value } }));

    jassert (result.ok);
    juce::ignoreUnused (result);
}

void SynthEditor::beginGesture (const juce::String& name)
{
    ++gestureDepth;
    app.getCommands().beginGesture (name);
}

void SynthEditor::endGesture()
{
    if (gestureDepth > 0)
    {
        --gestureDepth;
        app.getCommands().endGesture();
    }
}

//==============================================================================
void SynthEditor::choosePreset (const juce::String& id)
{
    if (id != presetId || modified)
        app.run ("synth.loadPreset", args ({ { "channel", channelId }, { "preset", id } }));

    preview();
}

void SynthEditor::stepPreset (int delta)
{
    const auto ids = browser.visibleIds();

    if (ids.isEmpty())
        return;

    const int now = ids.indexOf (presetId);
    const int next = now < 0 ? (delta > 0 ? 0 : ids.size() - 1) : (now + delta + ids.size()) % ids.size();
    choosePreset (ids[next]);
}

void SynthEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    juce::StringArray ids;
    ids.add ("init");
    menu.addItem (1, "Init (timbre inicial)", true, presetId == "init");
    menu.addSeparator();

    // Favoritos primeiro, com a estrela que desmarca sem fechar o menu; depois as categorias, com estrela em cada preset.
    if (const auto favouriteIds = synthfavorites::all(); ! favouriteIds.isEmpty())
    {
        menu.addSectionHeader ("Favoritos");

        for (auto& id : favouriteIds)
        {
            if (const auto* p = findPreset (id.toStdString()))
            {
                ids.add (id);
                menu.addItem (favui::menuItem (ids.size(), utf8 (p->name), Favorites::Kind::synthPreset, id, id == presetId));
            }
        }

        menu.addSeparator();
    }

    for (auto& cat : allCategories())
    {
        juce::PopupMenu sub;

        for (auto* p : findPresets (cat.id, "", ""))
        {
            ids.add (utf8 (p->id));
            sub.addItem (favui::menuItem (ids.size(), utf8 (p->name), Favorites::Kind::synthPreset, utf8 (p->id), utf8 (p->id) == presetId));
        }

        menu.addSubMenu (utf8 (cat.name), sub);
    }

    juce::Component::SafePointer<SynthEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe, ids] (int choice)
                        {
                            if (safe != nullptr && choice > 0)
                                safe->choosePreset (ids[choice - 1]);
                        });
}

void SynthEditor::showVariarMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Sutil", true, variation < 0.25f);
    menu.addItem (2, "Médio"_t, true, variation >= 0.25f && variation < 0.5f);
    menu.addItem (3, "Forte", true, variation >= 0.5f);

    juce::Component::SafePointer<SynthEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&variarMenuButton),
                        [safe] (int choice)
                        {
                            if (safe == nullptr || choice <= 0)
                                return;

                            safe->variation = choice == 1 ? 0.2f : (choice == 2 ? 0.4f : 0.7f);
                            safe->variar (safe->variation);
                        });
}

void SynthEditor::variar (float amount)
{
    app.run ("synth.randomize", args ({ { "channel", channelId }, { "amount", amount } }));
    preview();
}

void SynthEditor::preview()
{
    auto* track = app.getProjector().trackFor (channelId);

    if (track == nullptr)
        return;

    const auto* preset = findPreset (presetId.toStdString());
    const auto* cat = preset != nullptr ? findCategory (preset->category) : nullptr;
    const int key = cat != nullptr ? cat->testKey : 60;
    const auto values = currentValues();
    const int ms = juce::jlimit (450, 2500, juce::roundToInt (values[(size_t) P::env (0, eAttack)] + 700.0f));

    te::AudioTrack::Ptr keep (track);

    // Uma prévia por vez: a anterior solta já (senão o 808 mono deslizaria para a nota nova).
    if (previewKey >= 0)
        keep->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, previewKey), te::createUniqueMPESourceID());

    previewKey = key;
    const int serial = ++previewSerial;
    keep->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), te::createUniqueMPESourceID());

    juce::Component::SafePointer<SynthEditor> safe (this);
    juce::Timer::callAfterDelay (ms, [keep, key, safe, serial]
    {
        if (safe != nullptr && safe->previewSerial != serial)
            return;   // outra prévia já soltou esta

        keep->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, key), te::createUniqueMPESourceID());

        if (safe != nullptr)
            safe->previewKey = -1;
    });
}

//==============================================================================
std::vector<SynthEditor::Row> SynthEditor::rowsFor (int pageIndex, int layerIndex) const
{
    std::vector<Row> rows;

    if (pageIndex == 0)
    {
        if (layerIndex >= numLayers)
        {
            rows.push_back ({ { "SUB", { { P::subOn, "LIGA" }, { P::subLevel, "VOLUME" }, { P::subOct, "OITAVA" }, { P::subWave, "ONDA" } } } });
            return rows;
        }

        auto L = [layerIndex] (int p) { return P::layer (layerIndex, p); };
        rows.push_back ({ { "CAMADA " + juce::String (layerIndex + 1), { { L (lOn), "LIGA" }, { L (lType), "TIPO" } } },
                          { "NÍVEL"_t, { { L (lLevel), "VOLUME" }, { L (lPan), "PAN" } } },
                          { "AFINAÇÃO"_t, { { L (lOct), "OITAVA" }, { L (lSemi), "SEMITOM" }, { L (lFine), "FINO" } } },
                          { "UNISON", { { L (lVoices), "VOZES" }, { L (lDetune), "DESAFINAÇÃO"_t }, { L (lWidth), "ABERTURA" } } } });

        // "Pelo filtro" não é do começo da nota: o título diz os dois.
        const Group start { "FASE E FILTRO", { { L (lPhase), "FASE LIVRE" }, { L (lFilter), "PELO FILTRO" } } };
        const auto type = (LayerType) juce::roundToInt (controls[(size_t) L (lType)]->getValue());

        switch (type)
        {
            case LayerType::analog:
                rows.push_back ({ { "ONDA", { { L (lWave), "ONDA" }, { L (lShape), "LARGURA PULSO" } } }, start });
                break;

            case LayerType::wavetable:
                rows.push_back ({ { "TABELA", { { L (lTable), "TABELA" }, { L (lShape), "POSIÇÃO"_t } } }, start });
                break;

            case LayerType::fm:
                rows.push_back ({ { "OPERADORES", { { L (lFmAlgo), "ALGORITMO" }, { L (lFmRatio2), "RAZÃO B"_t }, { L (lFmAmt2), "ÍNDICE B"_t },
                                                    { L (lFmRatio3), "RAZÃO C"_t }, { L (lFmAmt3), "ÍNDICE C"_t },
                                                    { L (lFmRatio4), "RAZÃO D"_t }, { L (lFmAmt4), "ÍNDICE D"_t } } } });
                rows.push_back ({ { "BRILHO", { { L (lShape), "BRILHO" }, { L (lFmDecay), "QUEDA" }, { L (lFmSustain), "SUSTENTADO" },
                                                { L (lFmFeedback), "REALIMENT." } } }, start });
                break;

            case LayerType::noise:
                rows.push_back ({ { "RUÍDO"_t, { { L (lShape), "COR" } } }, { "FILTRO", { { L (lFilter), "PELO FILTRO" } } } });
                break;
        }

        return rows;
    }

    if (pageIndex == 1)
    {
        rows.push_back ({ { "FILTRO", { { P::fltOn, "LIGA" }, { P::fltMode, "TIPO", wideList }, { P::fltSlope, "INCLINAÇÃO"_t }, { P::fltCutoff, "CORTE" },
                                        { P::fltReso, "RESSONÂNCIA"_t }, { P::fltDrive, "DRIVE" } } },
                          { "MOVIMENTO", { { P::fltKey, "SEGUE A NOTA" }, { P::fltEnv, "ENVELOPE" }, { P::fltVel, "FORÇA ABRE"_t } } } });

        for (int e = 0; e < numEnvs; ++e)
            rows.push_back ({ { utf8 (sectionOf (P::env (e, eAttack))).toUpperCase(),
                                { { P::env (e, eAttack), "ATAQUE" }, { P::env (e, eDecay), "DECAIMENTO" }, { P::env (e, eSustain), "SUSTENTAÇÃO"_t },
                                  { P::env (e, eRelease), "SOLTURA" }, { P::env (e, eCurve), "CURVA" } } } });
        return rows;
    }

    if (pageIndex == 2)
    {
        for (int l = 0; l < numLfos; ++l)
            rows.push_back ({ { "LFO " + juce::String (l + 1),
                                { { P::lfo (l, lfoShape), "ONDA" }, { P::lfo (l, lfoSync), "NO BPM" }, { P::lfo (l, lfoRate), "TAXA" },
                                  { P::lfo (l, lfoDiv), "DIVISÃO"_t }, { P::lfo (l, lfoRetrig), "REINÍCIO"_t }, { P::lfo (l, lfoFade), "ENTRADA" } } } });

        for (int r = 0; r < 3; ++r)
        {
            Row row;

            for (int s = r * 3; s < juce::jmin (numSlots, r * 3 + 3); ++s)
                row.push_back ({ "ROTA " + juce::String (s + 1),
                                 { { P::slot (s, mSrc), "FONTE" }, { P::slot (s, mDst), "DESTINO", wideList }, { P::slot (s, mAmt), "QUANTIDADE" } } });

            rows.push_back (row);
        }

        return rows;
    }

    rows.push_back ({ { "VOZ", { { P::volume, "VOLUME" }, { P::poly, "POLIFONIA" }, { P::mono, "MONO" }, { P::glide, "GLIDE" },
                                 { P::legato, "LEGATO" }, { P::porta, "GLIDE QUANDO", wideList } } },
                      { "TOQUE", { { P::velAmp, "FORÇA"_t }, { P::bend, "PITCH BEND" }, { P::drift, "DERIVA" } } } });
    rows.push_back ({ { "SATURAÇÃO"_t, { { P::satOn, "LIGA" }, { P::satMode, "TIPO" }, { P::satDrive, "DRIVE" }, { P::satMix, "MISTURA" } } },
                      { "ESTÉREO"_t, { { P::width, "LARGURA" } } } });
    rows.push_back ({ { "EQ", { { P::eqOn, "LIGA" }, { P::eqHp, "CORTE GRAVE" }, { P::eqLow, "GRAVE" }, { P::eqMid, "MÉDIO"_t },
                                { P::eqMidFreq, "FREQ. MÉDIO"_t }, { P::eqHigh, "AGUDO" } } } });
    rows.push_back ({ { "DELAY", { { P::dlyOn, "LIGA" }, { P::dlyDiv, "DIVISÃO"_t }, { P::dlyFb, "REPETIÇÕES"_t }, { P::dlyMix, "MISTURA" } } },
                      { "REVERB", { { P::revOn, "LIGA" }, { P::revSize, "TAMANHO" }, { P::revDecay, "DECAIMENTO" }, { P::revMix, "MISTURA" } } } });
    return rows;
}

void SynthEditor::showPage (int newPage, int newLayer)
{
    page = juce::jlimit (0, 3, newPage);
    layer = juce::jlimit (0, numLayers, newLayer);
    pages.setSelected (page);
    layers.setSelected (layer);
    refresh();
    layoutPage();
}

void SynthEditor::layoutPage()
{
    using namespace theme;

    for (auto& c : controls)
        c->setVisible (false);

    for (juce::Component* v : std::initializer_list<juce::Component*> { &wave, &filterView, &layers, &envViews[0], &envViews[1], &envViews[2],
                                                                        &lfoViews[0], &lfoViews[1] })
        v->setVisible (false);

    placed.clear();

    auto content = getLocalBounds().withTrimmedLeft (metric::pad + browserWidth + metric::pad + metric::gap)
                                   .withTrimmedTop (headerHeight + tabsHeight).reduced (0, 0).withTrimmedRight (metric::pad);

    if (page == 0)
    {
        layers.setVisible (true);
        layers.setBounds (content.removeFromTop (metric::control));
        content.removeFromTop (metric::gap + 4);
    }

    if (page == 1)
    {
        filterView.setVisible (true);
        filterView.setBounds (content.removeFromTop (104));
        content.removeFromTop (metric::gap);
    }

    std::vector<juce::Rectangle<int>> rowAreas;
    int y = content.getY();

    for (auto& row : rowsFor (page, layer))
    {
        int x = content.getX();

        for (auto& group : row)
        {
            int w = 0;

            for (auto& cell : group.cells)
                w += cell.width;

            placed.push_back ({ group.title, { x, y, w, groupTitle + cellH } });
            int cx = x;

            for (auto& cell : group.cells)
            {
                auto& c = *controls[(size_t) cell.param];
                c.setCaption (cell.caption);
                c.setBounds (cx, y + groupTitle, cell.width, cellH);
                c.setVisible (true);
                cx += cell.width;
            }

            x += w + 16;
        }

        rowAreas.push_back ({ content.getX(), y, x - 16 - content.getX(), groupTitle + cellH });
        y += groupTitle + cellH + metric::gap;
    }

    auto besideRows = [&content] (const juce::Rectangle<int>& first, const juce::Rectangle<int>& last)
    {
        const int x = juce::jmax (first.getRight(), last.getRight()) + 24;
        return juce::Rectangle<int> (x, first.getY() + groupTitle, content.getRight() - x, last.getBottom() - first.getY() - groupTitle);
    };

    if (page == 0 && ! rowAreas.empty())
    {
        wave.setVisible (true);

        if (rowAreas.size() >= 2)
            wave.setBounds (besideRows (rowAreas[1], rowAreas.back()));
        else
            wave.setBounds (juce::Rectangle<int> (rowAreas[0].getX(), rowAreas[0].getBottom() + metric::gap, content.getWidth(), 140));
    }

    if (page == 1)
        for (int e = 0; e < 3 && e + 1 < (int) rowAreas.size(); ++e)
        {
            envViews[e].setVisible (true);
            envViews[e].setBounds (besideRows (rowAreas[(size_t) e + 1], rowAreas[(size_t) e + 1]));
        }

    if (page == 2)
        for (int l = 0; l < 2 && l < (int) rowAreas.size(); ++l)
        {
            lfoViews[l].setVisible (true);
            lfoViews[l].setBounds (besideRows (rowAreas[(size_t) l], rowAreas[(size_t) l]));
        }

    repaint();
}

//==============================================================================
void SynthEditor::parentHierarchyChanged()
{
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
        w->setName ("WHYNOT Synth - " + channelName);

    juce::Component::SafePointer<SynthEditor> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });
}

bool SynthEditor::keyPressed (const juce::KeyPress& key)
{
    // Com a janela do synth em foco, Ctrl+Z, espaço e os outros atalhos agem no app inteiro.
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
        if (auto* main = dynamic_cast<MainWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
            if (auto* content = main->getContentComponent())
                return content->keyPressed (key);

    return false;
}

void SynthEditor::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    // cabeçalho
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (metric::pad, 0);
    g.setColour (colour::text);
    g.setFont (displayFont (20.0f));
    g.drawText ("WHYNOT Synth", header.removeFromTop (38).withTrimmedTop (12), juce::Justification::bottomLeft);
    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawText (channelName + (presetInfo.isNotEmpty() ? "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + presetInfo : juce::String()),
                header.withWidth (420), juce::Justification::topLeft);

    g.setColour (colour::line);
    g.drawHorizontalLine (headerHeight - 1, (float) metric::pad, (float) (getWidth() - metric::pad));

    // fundo do navegador e divisória
    auto side = browser.getBounds().expanded (metric::gap, metric::gap).toFloat();
    g.setColour (colour::background.withAlpha (0.25f));
    g.fillRoundedRectangle (side, metric::radiusCard);

    // títulos dos grupos
    g.setFont (captionFont());

    for (auto& grp : placed)
    {
        g.setColour (colour::textDim);
        g.drawText (grp.title, grp.area.getX(), grp.area.getY(), grp.area.getWidth(), groupTitle - 4, juce::Justification::centred);
        g.setColour (colour::line);
        g.drawHorizontalLine (grp.area.getY() + groupTitle - 3, (float) grp.area.getX() + 6.0f, (float) grp.area.getRight() - 6.0f);
    }

    if (page == 2)
    {
        auto hint = getLocalBounds().withTrimmedLeft (metric::pad + browserWidth + metric::pad + metric::gap).removeFromBottom (30).withTrimmedRight (metric::pad);
        g.setColour (colour::textFaint);
        g.setFont (font (type::small));
        g.drawFittedText ("Cada rota leva uma fonte a um destino. 100% = afinação ±24 semitons, corte ±8 oitavas, forma e pan de ponta a ponta."_t,
                          hint, juce::Justification::centredLeft, 2);
    }
}

void SynthEditor::resized()
{
    using namespace theme;
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (metric::pad, 14);

    variarMenuButton.setBounds (header.removeFromRight (28));
    variarButton.setBounds (header.removeFromRight (juce::jmax (100, variarButton.getIdealWidth (header.getHeight()))));
    header.removeFromRight (metric::gap);
    previewButton.setBounds (header.removeFromRight (previewButton.getIdealWidth (header.getHeight())));
    header.removeFromRight (metric::gap);
    starButton.setBounds (header.removeFromRight (36));
    nextButton.setBounds (header.removeFromRight (32));
    presetButton.setBounds (header.removeFromRight (250));
    prevButton.setBounds (header.removeFromRight (32));

    pages.setBounds (getLocalBounds().withTrimmedLeft (metric::pad + browserWidth + metric::pad + metric::gap)
                                     .withTrimmedTop (headerHeight + 8).withHeight (metric::control)
                                     .withWidth (pages.getIdealWidth()));

    browser.setBounds (juce::Rectangle<int> (metric::pad, headerHeight + metric::pad, browserWidth, getHeight() - headerHeight - 2 * metric::pad));
    layoutPage();
}

} // namespace whynot
