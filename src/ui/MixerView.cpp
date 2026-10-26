#include "MixerView.h"
#include "MidiLearnMenu.h"
#include "Fluid.h"
#include "Theme.h"
#include "../record/RecordEngine.h"
#include "../app/AppController.h"
#include "../dsp/FxCatalog.h"
#include "../dsp/LoudnessMeter.h"

#include <tuple>

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

    juce::String formatDb (float db)
    {
        if (db <= -99.9f)
            return "-inf";

        return (db > 0.0f ? "+" : "") + juce::String (db, 1).replaceCharacter ('.', ',');
    }
}

//==============================================================================
MixerStrip::MixerStrip (AppController& a, const juce::String& id) : app (a), trackId (id)
{
    fader.setRange (-100.0, 6.0, 0.01);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setTooltip ("Volume. Duplo clique volta a 0 dB.");
    fader.onDragStart = [this] { app.getCommands().beginGesture ("Volume"); };
    fader.onDragEnd = [this] { app.getCommands().endGesture(); };
    fader.onValueChange = [this]
    {
        app.run ("mixer.setVolume", args ({ { "track", trackId }, { "db", fader.getValue() } }));
    };

    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setTooltip ("Pan: leva o som para a esquerda ou a direita sem mudar o volume que se sente. Duplo clique centraliza."_t);
    pan.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    pan.onDragStart = [this] { app.getCommands().beginGesture ("Pan"); };
    pan.onDragEnd = [this] { app.getCommands().endGesture(); };
    pan.onValueChange = [this] { app.run ("mixer.setPan", args ({ { "track", trackId }, { "pan", pan.getValue() } })); };

    // MIDI learn (bloco 4d): o botão direito no fader ou no pan liga um fader ou knob do controlador
    fader.onMenu = [this] { midilearn::showMenu (app, MidiLink { 0, 0, "mixer", trackId, {}, "volume" }, "Volume", &fader); };
    pan.onMenu = [this] { midilearn::showMenu (app, MidiLink { 0, 0, "mixer", trackId, {}, "pan" }, "Pan", &pan); };

    // Mudo não é laranja: o laranja mostra o que soa (no rack, o hexágono apaga quando o canal está mudo).
    // O M ligado usa a pílula neutra de "escolhido"; o S ligado continua laranja, porque é o que vai soar.
    muteButton.setTooltip ("Mute: silencia a trilha (M aceso = mudo)");
    muteButton.getProperties().set ("neutral", true);
    muteButton.setColour (juce::TextButton::textColourOnId, theme::colour::text);
    muteButton.setClickingTogglesState (false);
    muteButton.onClick = [this] { app.run ("mixer.setMute", args ({ { "track", trackId } })); };

    soloButton.setTooltip ("Solo: ouvir só esta trilha e o que entra nela (quem dispara o sidechain dela continua agindo, sem som)"_t);
    soloButton.onClick = [this] { app.run ("mixer.setSolo", args ({ { "track", trackId } })); };

    // Gravação (3.5): armar = a próxima gravação entra por esta trilha (com os efeitos dela). Uma por vez.
    armButton.setTooltip ("Armar para gravar: com o REC aceso, o play grava a entrada da placa nesta trilha."
                          " Botão direito: monitorar a entrada."_t);
    armButton.addMouseListener (this, false);
    armButton.onClick = [this]
    {
        if (std::exchange (armMenuClick, false))
            return;

        const auto result = app.run ("record.arm", args ({ { "track", trackId }, { "on", ! armed } }));

        if (onStatus && ! result.ok)
            onStatus (result.error, true);
        else if (onStatus && result.value["warning"].toString().isNotEmpty())
            onStatus (result.value["warning"].toString(), true);   // a entrada da placa não abriu
    };

    for (juce::Component* c : { (juce::Component*) &meter, (juce::Component*) &fader, (juce::Component*) &pan,
                                (juce::Component*) &muteButton, (juce::Component*) &soloButton, (juce::Component*) &armButton })
    {
        addAndMakeVisible (c);
        c->setWantsKeyboardFocus (false);
    }
}

void MixerStrip::update (const Project& p, const MixerTrack& m)
{
    // o mixer inteiro se atualiza a cada mudança do projeto: só repinta a trilha cujo desenho mudou (fluidez, 21/10)
    const auto shown = [this] { return std::make_tuple (name, colour, isMaster, volumeDb, outputName, outputIsMaster, armed, frozen, monitor, fxSummary, fxCount); };
    const auto before = shown();

    name = m.name;
    colour = theme::fromHex (m.colour);
    isMaster = m.isMaster();
    volumeDb = m.volumeDb;
    outputName = isMaster ? juce::String ("Placa de som") : (p.findMixer (p.resolveOutput (m.output)) != nullptr
                                                                  ? p.findMixer (p.resolveOutput (m.output))->name : juce::String ("Master"));
    outputIsMaster = ! isMaster && p.resolveOutput (m.output) == masterTrackId;

    fader.setTooltip (isMaster ? juce::String ("Volume do master: entra antes dos efeitos do master, e o limitador no fim segura o pico. "
                                               "Duplo clique volta a 0 dB."_t)
                               : juce::String ("Volume. Duplo clique volta a 0 dB."_t));
    fader.setValue (m.volumeDb, juce::dontSendNotification);
    pan.setValue (m.pan, juce::dontSendNotification);
    muteButton.setToggleState (m.mute, juce::dontSendNotification);
    soloButton.setToggleState (m.solo, juce::dontSendNotification);
    soloButton.setVisible (! isMaster);
    muteButton.setVisible (! isMaster);

    armed = m.armed;
    frozen = m.frozen;
    monitor = m.monitor;
    armButton.setToggleState (armed, juce::dontSendNotification);
    armButton.setVisible (! isMaster);      // o master não grava

    if (! armed)
    {
        inputLevel = 0.0f;
        armButton.setAlert (0.0f);
    }

    meter.setSource (app.getProjector().meterFor (m.id));

    // Resumo dos efeitos ("EQ · Comp · Sat"), para ver a cadeia sem abrir o painel.
    juce::StringArray names;

    for (auto& ins : m.inserts)
    {
        const auto* info = ins.kind == "whynot" ? dsp::findEffect (ins.type.toStdString()) : nullptr;
        names.add (info != nullptr ? juce::String::fromUTF8 (info->shortName) : ins.type);
    }

    fxCount = names.size();
    fxSummary = names.isEmpty() ? juce::String ("sem efeitos") : names.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")));
    setTooltip (isMaster ? juce::String ("Saída final do projeto"_t)
                         : "Trilha "_t + name + (frozen ? ", congelada" : "")
                               + ". Duplo clique no nome renomeia; botão direito no nome: renomear e congelar."_t);

    if (shown() != before)
        repaint();
}

void MixerStrip::paint (juce::Graphics& g)
{
    using namespace theme;
    // cada trilha é uma coluna um degrau acima do cartão; a master, mais um
    g.setColour (isMaster ? colour::raised.withAlpha (0.8f) : colour::raised.withAlpha (0.45f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), metric::radius + 2.0f);

    if (selected)
    {
        g.setColour (colour::textDim.withAlpha (0.55f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.5f), metric::radius + 2.0f, 1.5f);
    }

    // O medidor anda a cada quadro e, como não é opaco, repinta este fundo atrás dele: o texto de cima e o
    // volume só são desenhados quando a parte suja encosta neles (fluidez, 21/10).
    // congelada: o floco na placa; monitoração fora do padrão (desligada ou sempre): o fone
    if (fluid::needsPaint (g, tapeArea()))
        drawNamePlate (g, tapeArea().toFloat(), name, colour, selected, false, type::body,
                       frozen ? std::optional<Icon> (Icon::snowflake)
                              : monitor.isNotEmpty() ? std::optional<Icon> (Icon::headphones) : std::nullopt);

    if (fluid::needsPaint (g, outputArea()))
        drawRoutePill (g, outputArea().toFloat(), outputName, false, isMaster || outputIsMaster, type::small);

    if (fluid::needsPaint (g, fxArea()))
    {
        g.setColour (fxCount > 0 ? colour::textDim : colour::textFaint);
        g.setFont (font (type::small));
        g.drawFittedText (fxSummary, fxArea().reduced (4, 0), juce::Justification::centred, 1, 0.75f);
    }

    // armada: o medidor fino da entrada, logo abaixo do resumo dos efeitos
    if (armed)
    {
        const auto bar = juce::Rectangle<float> (8.0f, (float) fxArea().getBottom() + 1.0f, (float) getWidth() - 16.0f, 3.0f);
        g.setColour (colour::background);
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (inputLevel > 0.95f ? colour::meterHigh : colour::meterLow);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * inputLevel), 1.5f);
    }

    if (const juce::Rectangle<int> volumeArea (0, getHeight() - 108, getWidth(), 18); fluid::needsPaint (g, volumeArea))
    {
        g.setColour (colour::text);
        g.setFont (numberFont (type::body));
        g.drawText (formatDb (volumeDb), volumeArea, juce::Justification::centred);
    }

    // o que cada botão faz, embaixo da linha (revisão de iniciante, 22/10)
    if (! isMaster)
    {
        // cada legenda cabe na largura do próprio botão (um terço da linha): 8 px, pouco espaçada
        g.setColour (colour::textFaint);
        g.setFont (font (8.0f, Weight::semibold).withExtraKerningFactor (0.04f));

        for (auto [button, text] : { std::pair<juce::Component*, const char*> (&muteButton, "MUDO"),
                                     std::pair<juce::Component*, const char*> (&soloButton, "SOLO"),
                                     std::pair<juce::Component*, const char*> (&armButton, "REC") })
            if (const auto area = button->getBounds().translated (0, button->getHeight() + 1).withHeight (11).expanded (2, 0);
                fluid::needsPaint (g, area))
                g.drawFittedText (text, area, juce::Justification::centred, 1, 0.85f);
    }
}

void MixerStrip::setRecordState (float inputPeak, bool recording)
{
    // pico linear da entrada para a escala do medidor (-60 a 0 dB), com queda suave
    const float db = inputPeak > 1.0e-6f ? 20.0f * std::log10 (inputPeak) : -100.0f;
    const float target = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    // a mesma queda de antes (0,04 por quadro a 30 Hz), agora por segundo: o tique pode vir a 60 Hz ou mais
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastInputTick < 0.0 ? 1.0f / 30.0f : (float) juce::jlimit (0.0, 0.1, (now - lastInputTick) * 0.001);
    lastInputTick = now;
    const float next = juce::jmax (target, inputLevel - 1.2f * dt);

    if (std::abs (next - inputLevel) > 0.002f)
    {
        inputLevel = next;
        repaint (0, fxArea().getBottom(), getWidth(), 6);
    }

    armButton.setAlert (recording ? ((juce::Time::getMillisecondCounter() / 500) % 2 == 0 ? 1.0f : 0.35f) : 0.0f);
}

void MixerStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (96);

    auto bottom = r.removeFromBottom (110);
    bottom.removeFromTop (20);
    pan.setBounds (bottom.removeFromTop (40).withSizeKeepingCentre (38, 38));
    bottom.removeFromTop (4);

    // M, S e o armar (●) numa linha só, como num console; no master, nenhum dos três
    auto buttons = bottom.removeFromTop (26);
    const int third = buttons.getWidth() / 3;
    muteButton.setBounds (buttons.removeFromLeft (third).reduced (2, 0));
    soloButton.setBounds (buttons.removeFromLeft (third).reduced (2, 0));
    armButton.setBounds (buttons.reduced (2, 0));

    r.removeFromBottom (4);
    meter.setBounds (r.removeFromLeft (14));
    r.removeFromLeft (4);
    fader.setBounds (r);
}

void MixerStrip::setSelected (bool s)
{
    if (s != selected)
    {
        selected = s;
        repaint();
    }
}

void MixerStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &armButton)
    {
        if (e.mods.isPopupMenu())
        {
            armMenuClick = true;
            showMonitorMenu();
        }

        return;
    }

    if (onSelect)
        onSelect();

    if (outputArea().contains (e.getPosition()) && ! isMaster)
        showOutputMenu();
    else if (tapeArea().contains (e.getPosition()) && e.mods.isPopupMenu())
        showNameMenu();
}

void MixerStrip::showMonitorMenu()
{
    // ouvir a voz pelo app, com os efeitos desta trilha; o take continua seco
    auto* rec = app.recorder();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Monitorar a entrada"_t);
    menu.addItem (1, "Desligada (só a da placa)"_t, true, monitor == "off");
    menu.addItem (2, "Quando armada"_t, true, monitor.isEmpty());
    menu.addItem (3, "Sempre"_t, true, monitor == "on");
    menu.addSeparator();
    menu.addItem (4, "Baixa latência ao gravar (tira da escuta os efeitos com atraso)"_t, rec != nullptr,
                  rec != nullptr && rec->settings().lowLatency);
    menu.addItem (5, "Estou de fone (\"Quando armada\" só escuta de fone: nas caixas, vira microfonia)"_t, rec != nullptr,
                  rec != nullptr && rec->settings().headphones == 1);

    juce::Component::SafePointer<MixerStrip> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&armButton), [safe] (int choice)
    {
        if (safe == nullptr)
            return;

        safe->armMenuClick = false;

        if (choice >= 1 && choice <= 3)
            safe->app.run ("record.setMonitor", args ({ { "track", safe->trackId }, { "mode", choice == 1 ? "off" : choice == 2 ? "armed" : "on" } }));
        else if (choice == 4)
            safe->app.run ("record.setLowLatency", args ({ { "on", ! safe->app.recorder()->settings().lowLatency } }));
        else if (choice == 5)
            safe->app.run ("record.setHeadphones", args ({ { "answer", safe->app.recorder()->settings().headphones == 1 ? "no" : "yes" } }));
    });
}

void MixerStrip::showNameMenu()
{
    // o que é raro fica no menu do nome: renomear e congelar (o master só renomeia)
    juce::PopupMenu menu;
    menu.addItem (1, "Renomear");

    if (! isMaster)
    {
        menu.addSeparator();
        menu.addItem (2, "Congelar trilha: o som vira um WAV e a CPU folga"_t, true, frozen);
    }

    juce::Component::SafePointer<MixerStrip> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (tapeArea())),
                        [safe] (int choice)
                        {
                            if (safe == nullptr)
                                return;

                            if (choice == 1) safe->rename();
                            if (choice == 2) safe->toggleFrozen();
                        });
}

void MixerStrip::toggleFrozen()
{
    const auto result = app.run ("mixer.setFrozen", args ({ { "track", trackId }, { "on", ! frozen } }));

    if (! onStatus)
        return;

    if (! result.ok)
        onStatus (result.error, true);
    else if (const auto message = result.value["message"].toString(); message.isNotEmpty())
        onStatus (message, false);   // os canais que saem pela mesma trilha congelam juntos
}

void MixerStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (tapeArea().contains (e.getPosition()))
        rename();
}

void MixerStrip::showOutputMenu()
{
    juce::PopupMenu menu;
    const auto& p = app.getModel().get();
    const auto current = p.resolveOutput (p.findMixer (trackId) != nullptr ? p.findMixer (trackId)->output : juce::String());
    juce::StringArray ids;

    for (auto& m : p.mixer)
    {
        if (m.id == trackId)
            continue;

        ids.add (m.id);
        menu.addItem (ids.size(), m.name, true, m.id == current);
    }

    juce::Component::SafePointer<MixerStrip> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (outputArea())),
                        [safe, ids] (int choice)
                        {
                            if (choice <= 0 || safe == nullptr)
                                return;

                            const auto result = safe->app.run ("mixer.setOutput", args ({ { "track", safe->trackId }, { "output", ids[choice - 1] } }));

                            if (! result.ok && safe != nullptr && safe->onStatus)
                                safe->onStatus (result.error, true);
                        });
}

void MixerStrip::rename()
{
    auto* w = new juce::AlertWindow ("Renomear trilha", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", name);
    w->addButton ("Renomear", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // A faixa pode ser recriada enquanto o diálogo está aberto: guarda só o app e o id.
    auto& controller = app;
    const auto id = trackId;

    w->enterModalState (true, juce::ModalCallbackFunction::create ([&controller, id, w] (int choice)
    {
        if (choice == 1)
            controller.run ("mixer.rename", args ({ { "track", id }, { "name", w->getTextEditorContents ("name") } }));
    }), true);
}

//==============================================================================
MixerView::MixerView (AppController& a) : app (a), insertPanel (a)
{
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    panelViewport.setViewedComponent (&insertPanel, false);
    panelViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (panelViewport);
    insertPanel.onStatus = [this] (const juce::String& msg, bool err) { if (onStatus) onStatus (msg, err); };
    insertPanel.onLayoutChanged = [this] { resized(); };   // os macros abriram ou fecharam (bloco 4c)

    styleButton.setShowsMenuArrow (true);
    styleButton.onClick = [this] { showStyleMenu(); };
    addAndMakeVisible (styleButton);

    // comparar com e sem efeitos (o comando mixer.compare existia sem botão; bloco 1, 22/10)
    compareButton.setShowsMenuArrow (true);
    compareButton.setTooltip ("Comparar: ouça a mixagem sem os efeitos, ou com os efeitos no mesmo volume (LUFS) da versão sem, "
                              "para o volume não enganar o ouvido. Só para ouvir: o projeto não muda."_t);
    compareButton.onClick = [this] { showCompareMenu(); };
    addAndMakeVisible (compareButton);
}

juce::Rectangle<int> MixerView::loudnessArea() const
{
    // entre o título "Mixer" e o botão de comparar
    const int left = theme::metric::pad + 90;
    return { left, 0, juce::jmax (0, compareButton.getX() - 16 - left), headerHeight };
}

juce::String MixerView::getLoudnessText() const
{
    if (shownLufs <= -70.0f)
        return "LUFS do master: toque o beat para medir (clique para escolher um alvo)"_t;

    auto fmt = [] (float v) { return juce::String (v, 1).replaceCharacter ('.', ','); };
    juce::String s = "Master "_t + fmt (shownLufs) + " LUFS" + (shownLive ? juce::String() : " (integrado)"_t);

    if (shownPeak > -70.0f)
        s << "  ·  pico "_t << fmt (shownPeak) << " dBTP";

    if (targetLufs < 0.0f)
    {
        const float delta = shownLufs - targetLufs;
        s << "  ·  alvo "_t << targetName << " " << fmt (targetLufs) << ": "
          << (std::abs (delta) < 0.5f ? "no alvo"_t : fmt (std::abs (delta)) + (delta < 0.0f ? " abaixo"_t : " acima"_t));
    }

    return s;
}

void MixerView::setLoudnessTarget (float lufs, const juce::String& name)
{
    targetLufs = lufs;
    targetName = name;
}

void MixerView::showLoudnessMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Alvo de volume do master (LUFS)"_t);
    menu.addItem (1, "Sem alvo"_t, true, targetLufs >= 0.0f);

    struct Target { const char* name; float lufs; };
    static const Target targets[] { { "Streaming", -14.0f }, { "Pop", -9.0f }, { "Rap", -8.0f }, { "Trap", -7.0f }, { "Funk", -5.5f } };

    for (int i = 0; i < (int) std::size (targets); ++i)
        menu.addItem (i + 2, juce::String (targets[i].name) + "  (" + juce::String (targets[i].lufs, 1).replaceCharacter ('.', ',') + ")",
                      true, std::abs (targetLufs - targets[i].lufs) < 0.01f);

    menu.addSeparator();
    menu.addItem (-1, "Toque o beat: o número é o loudness dos últimos 3 s; parado, o da música toda que tocou (integrado)."_t, false);
    menu.addItem (-1, "As plataformas tocam em -14 e baixam o que vier mais alto. O pico real (dBTP) deve ficar abaixo de -1."_t, false);

    juce::Component::SafePointer<MixerView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (loudnessArea())), [safe] (int choice)
    {
        if (safe == nullptr || choice <= 0)
            return;

        if (choice == 1)
            setLoudnessTarget (0.0f, {});
        else
            setLoudnessTarget (targets[choice - 2].lufs, targets[choice - 2].name);

        safe->repaint (safe->loudnessArea());
    });
}

void MixerView::showCompareMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Comparar (só para ouvir; o projeto não muda)"_t);
    menu.addItem (1, "Normal: com os efeitos"_t, true, compareMode == "off");
    menu.addItem (2, "Sem efeitos: todos em bypass e os envios desligados"_t, true, compareMode == "bypass");
    menu.addItem (3, "Com efeitos, no mesmo volume da versão sem"_t, true, compareMode == "matched");

    juce::Component::SafePointer<MixerView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&compareButton), [safe] (int choice)
    {
        if (safe != nullptr && choice > 0)
            safe->setCompareMode (choice == 2 ? "bypass" : choice == 3 ? "matched" : "off");
    });
}

void MixerView::setCompareMode (const juce::String& mode)
{
    const auto result = app.run ("mixer.compare", args ({ { "mode", mode } }));

    if (! result.ok)
    {
        if (onStatus) onStatus (result.error, true);
        return;
    }

    compareMode = mode;
    compareButton.setToggleState (mode != "off", juce::dontSendNotification);
    compareButton.setLabel (mode == "bypass" ? "Sem efeitos"_t : mode == "matched" ? "Mesmo volume"_t : "Comparar"_t);
    resized();

    if (onStatus)
    {
        auto fmt = [] (const juce::var& v) { return juce::String ((double) v, 1).replaceCharacter ('.', ','); };

        if (mode == "bypass")
            onStatus ("Sem efeitos: ouça a diferença. Comparar > Normal volta."_t, false);
        else if (mode == "matched")
            onStatus ("Com efeitos, "_t + fmt (result.value["trimDb"]) + " dB de ajuste: o mesmo LUFS da versão sem efeitos ("_t
                          + fmt (result.value["lufsWithout"]) + "). Comparar > Normal volta."_t, false);
        else
            onStatus ("Comparar desligado: a mixagem toca normal."_t, false);
    }
}

void MixerView::mouseDown (const juce::MouseEvent& e)
{
    if (loudnessArea().contains (e.getPosition()))
        showLoudnessMenu();
}

void MixerView::selectTrack (const juce::String& mixerId)
{
    selectedTrack = mixerId;

    for (auto* s : strips)
        s->setSelected (s->getTrackId() == mixerId);

    if (masterStrip != nullptr)
        masterStrip->setSelected (mixerId == masterTrackId);

    insertPanel.setTrack (mixerId);
    resized();
}

void MixerView::showStyleMenu()
{
    juce::PopupMenu menu;
    juce::StringArray ids;
    menu.addSectionHeader ("Troca os efeitos de todas as trilhas e do master");

    for (auto& s : dsp::allStyles())
    {
        ids.add (s.id);
        juce::PopupMenu::Item item (juce::String::fromUTF8 (s.name) + "  (alvo " + juce::String (s.targetLufs, 1).replaceCharacter ('.', ',') + " LUFS)");
        item.itemID = ids.size();
        menu.addItem (item);
    }

    menu.addSeparator();
    menu.addItem (-1, "O papel de cada trilha vem do nome (Kick, 808, Voz...) ou do painel da direita.", false);
    menu.addItem (-1, "LUFS é o volume que o ouvido sente: quanto mais perto de 0, mais alto (as plataformas tocam em -14)."_t, false);

    juce::Component::SafePointer<MixerView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&styleButton),
                        [safe, ids] (int choice)
                        {
                            if (safe == nullptr || choice <= 0)
                                return;

                            const auto* style = dsp::findStyle (ids[choice - 1].toStdString());
                            const auto result = safe->app.run ("mixer.applyStyle", args ({ { "style", ids[choice - 1] } }));

                            if (safe->onStatus)
                            {
                                if (! result.ok)
                                    safe->onStatus (result.error, true);
                                else
                                    safe->onStatus ("Estilo "_t + juce::String::fromUTF8 (style->name) + " aplicado: "
                                                        + juce::String (result.value["tracks"].size()) + " trilhas, buses e master. "
                                                        + juce::String::fromUTF8 (style->loudnessNote) + " Ctrl+Z desfaz.",
                                                    false);
                            }
                        });
}

void MixerView::refresh (const Project& p)
{
    juce::StringArray wanted;

    for (auto& m : p.mixer)
        if (! m.isMaster())
            wanted.add (m.id);

    juce::StringArray existing;

    for (auto* s : strips)
        existing.add (s->getTrackId());

    if (wanted != existing)
    {
        strips.clear();

        for (auto& id : wanted)
        {
            auto* s = strips.add (new MixerStrip (app, id));
            s->onStatus = [this] (const juce::String& msg, bool err) { if (onStatus) onStatus (msg, err); };
            s->onSelect = [this, id] { selectTrack (id); };
            stripHolder.addAndMakeVisible (s);
        }
    }

    for (auto* s : strips)
        if (auto* m = p.findMixer (s->getTrackId()))
            s->update (p, *m);

    if (masterStrip == nullptr)
    {
        masterStrip = std::make_unique<MixerStrip> (app, masterTrackId);
        masterStrip->onSelect = [this] { selectTrack (masterTrackId); };
        addAndMakeVisible (*masterStrip);
    }

    if (auto* master = p.findMixer (masterTrackId))
        masterStrip->update (p, *master);

    // A trilha escolhida saiu (ou nenhuma ainda): a primeira, ou o master.
    if (selectedTrack.isEmpty() || p.findMixer (selectedTrack) == nullptr)
        selectedTrack = wanted.isEmpty() ? masterTrackId : wanted[0];

    for (auto* s : strips)
        s->setSelected (s->getTrackId() == selectedTrack);

    masterStrip->setSelected (selectedTrack == masterTrackId);
    insertPanel.setTrack (selectedTrack);
    insertPanel.refresh (p);
    resized();
}

void MixerView::tick()
{
    // a trilha armada (uma por vez) lê o pico da entrada; a leitura zera, então é uma só por tique
    auto* rec = app.recorder();
    const bool recording = rec != nullptr && rec->isRecording();

    for (auto* s : strips)
    {
        if (s->isArmed())
            s->setRecordState (rec != nullptr ? rec->takeInputPeak() : 0.0f, recording);

        s->tick();
    }

    if (masterStrip != nullptr)
        masterStrip->tick();

    // a comparação desliga sozinha (exportar, salvar, trocar de projeto, mexer no mixer): o botão acompanha
    if (compareMode != "off" && ! app.isAuditioning())
    {
        compareMode = "off";
        compareButton.setToggleState (false, juce::dontSendNotification);
        compareButton.setLabel ("Comparar"_t);
        resized();
    }

    // o LUFS do master: tocando, o dos últimos 3 s; parado, o integrado do que tocou. Cada play mede do zero.
    if (auto* loudness = app.getProjector().masterLoudness())
    {
        const bool playing = app.isPlaying();

        if (playing && ! wasPlaying)
            loudness->requestReset();

        wasPlaying = playing;
        const float lufs = std::round ((playing ? loudness->shortTerm() : loudness->integrated()) * 10.0f) / 10.0f;
        const float peak = std::round (loudness->truePeak() * 10.0f) / 10.0f;

        if (lufs != shownLufs || peak != shownPeak || playing != shownLive)
        {
            shownLufs = lufs;
            shownPeak = peak;
            shownLive = playing;
            repaint (loudnessArea());
        }
    }
}

void MixerView::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);
    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Mixer", metric::pad, 0, 80, headerHeight, juce::Justification::centredLeft);

    // o LUFS do master, sempre à vista; o pico real acima de -1 dBTP fica em alerta
    const auto area = loudnessArea();

    if (area.getWidth() > 60)
    {
        g.setFont (font (type::small, Weight::medium));
        g.setColour (shownPeak > -1.0f && shownPeak > -70.0f ? colour::warning : colour::textDim);
        g.drawFittedText (getLoudnessText(), area, juce::Justification::centredLeft, 1, 0.85f);
    }
}

void MixerView::resized()
{
    const int styleWidth = styleButton.getIdealWidth (32);
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (theme::metric::pad, 0);
    styleButton.setBounds (header.removeFromRight (styleWidth).withSizeKeepingCentre (styleWidth, 32));
    header.removeFromRight (8);
    const int compareWidth = compareButton.getIdealWidth (32);
    compareButton.setBounds (header.removeFromRight (compareWidth).withSizeKeepingCentre (compareWidth, 32));

    auto r = getLocalBounds().withTrimmedTop (headerHeight).reduced (10, 0).withTrimmedBottom (6);

    // Painel de efeitos da trilha escolhida, à direita de tudo.
    auto panel = r.removeFromRight (InsertPanel::idealWidth + 12).withTrimmedLeft (12);
    panelViewport.setBounds (panel);
    const bool scroll = insertPanel.getIdealHeight() > panel.getHeight();
    insertPanel.setSize (panel.getWidth() - (scroll ? panelViewport.getScrollBarThickness() + 2 : 0),
                         juce::jmax (panel.getHeight(), insertPanel.getIdealHeight()));

    if (masterStrip != nullptr)
        masterStrip->setBounds (r.removeFromRight (stripWidth + 8).withTrimmedLeft (8));

    viewport.setBounds (r);
    const int height = r.getHeight() - (viewport.isHorizontalScrollBarShown() ? viewport.getScrollBarThickness() : 0);
    stripHolder.setSize (juce::jmax (r.getWidth(), strips.size() * stripWidth), juce::jmax (100, height));

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * stripWidth, 0, stripWidth, stripHolder.getHeight());
}

} // namespace whynot
