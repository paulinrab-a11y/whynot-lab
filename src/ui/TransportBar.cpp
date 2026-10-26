#include "TransportBar.h"
#include "Theme.h"
#include "../app/AppController.h"
#include "../record/RecordEngine.h"

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

    juce::String formatBpm (double bpm)
    {
        // vírgula decimal, como se escreve no Brasil
        return juce::String (bpm, 2).replaceCharacter ('.', ',');
    }

    const char* const timeSignatures[] = { "4/4", "3/4", "6/8", "7/8", "5/4" };

    // medidas do visor: valor em cima, legenda embaixo
    constexpr int lcdHeight = 42;
    juce::Rectangle<int> valueArea (juce::Rectangle<int> section)     { return section.withTrimmedTop (5).withHeight (22); }
    juce::Rectangle<int> captionArea (juce::Rectangle<int> section)   { return section.withTrimmedTop (27).withHeight (10); }
}

//==============================================================================
BpmDisplay::BpmDisplay (AppController& a) : app (a)
{
    setTooltip ("Arraste para mudar o BPM (Shift: ajuste fino). Duplo clique para digitar."_t);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

    addChildComponent (editor);
    editor.setJustification (juce::Justification::centred);
    editor.setFont (theme::numberFont (17.0f));
    editor.setInputRestrictions (7, "0123456789.,");

    editor.onReturnKey = [this]
    {
        const auto value = editor.getText().replaceCharacter (',', '.').getDoubleValue();
        editor.setVisible (false);

        if (value > 0.0)
            apply (value);
    };

    editor.onEscapeKey = [this] { editor.setVisible (false); };
    editor.onFocusLost = [this] { editor.setVisible (false); };
}

void BpmDisplay::setBpm (double value)
{
    if (! dragging && value != bpm)
    {
        bpm = value;
        repaint();
    }
}

void BpmDisplay::apply (double value)
{
    value = juce::jlimit (20.0, 999.0, value);
    app.run ("transport.setBpm", args ({ { "bpm", value } }));
    bpm = app.getModel().get().bpm;
    repaint();
}

void BpmDisplay::paint (juce::Graphics& g)
{
    using namespace theme;
    g.setColour (colour::text);
    drawTabularText (g, formatBpm (bpm), getLocalBounds().toFloat(), numberFont (19.0f), juce::Justification::centred);
}

void BpmDisplay::resized()
{
    editor.setBounds (getLocalBounds());
}

void BpmDisplay::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    dragStart = bpm;
    app.getCommands().beginGesture ("Mudar BPM");
}

void BpmDisplay::mouseDrag (const juce::MouseEvent& e)
{
    const double perPixel = e.mods.isShiftDown() ? 0.01 : 0.25;
    double value = dragStart - e.getDistanceFromDragStartY() * perPixel;

    if (! e.mods.isShiftDown())
        value = std::round (value);

    value = juce::jlimit (20.0, 999.0, value);

    if (value != bpm)
    {
        bpm = value;
        app.run ("transport.setBpm", args ({ { "bpm", value } }));
        repaint();
    }
}

void BpmDisplay::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    app.getCommands().endGesture();
}

void BpmDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    editor.setText (formatBpm (bpm), false);
    editor.setVisible (true);
    editor.grabKeyboardFocus();
    editor.selectAll();
}

void BpmDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Cada clique da rodinha vale ~0,14 de deltaY no Windows; vários cliques podem chegar num evento só.
    const int notches = juce::jmax (1, juce::roundToInt (std::abs (w.deltaY) / 0.14f));
    const double step = (e.mods.isShiftDown() ? 0.1 : 1.0) * notches;
    apply (bpm + (w.deltaY > 0 ? step : -step));
}

//==============================================================================
PerformanceMeter::PerformanceMeter (AppController& a) : app (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    update (0.0f, 0.0f, 0, false);
}

void PerformanceMeter::update (float newCpu, float newPeak, int newXruns, bool newOverloaded)
{
    const auto pct = [] (float v) { return juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 100.0f); };

    if (pct (newCpu) == pct (cpu) && pct (newPeak) == pct (peak) && newXruns == xruns && newOverloaded == overloaded)
        return;

    cpu = newCpu;
    peak = newPeak;
    xruns = newXruns;
    overloaded = newOverloaded;

    setTooltip ("CPU da thread de áudio: "_t + juce::String (pct (cpu)) + "% (pico " + juce::String (pct (peak)) + "%). "
                + "Falhas de buffer: "_t + juce::String (xruns) + (xruns > 0 ? ". Clique para zerar." : ".")
                + (overloaded ? " Pesado: congele uma trilha (botão direito no nome, no mixer ou no rack) ou suba o buffer em Áudio e MIDI. (F10)"_t
                              : juce::String()));
    repaint();
}

void PerformanceMeter::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat();
    const auto ink = overloaded ? colour::signal : colour::textDim;

    g.setColour (colour::textFaint);
    g.setFont (captionFont());
    g.drawText ("CPU", r.removeFromLeft (28.0f), juce::Justification::centredLeft, false);

    // trilho com a carga e uma marquinha no pico
    const auto bar = r.removeFromLeft (40.0f).withSizeKeepingCentre (40.0f, 4.0f);
    g.setColour (colour::raised);
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (ink);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, cpu)), 2.0f);
    g.setColour (colour::text.withAlpha (0.7f));
    g.fillRect (juce::Rectangle<float> (bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, peak) - 0.5f, bar.getY() - 2.0f, 1.0f, 8.0f));

    r.removeFromLeft (6.0f);
    g.setColour (overloaded ? colour::signal : colour::text);
    drawTabularText (g, juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, cpu) * 100.0f)) + "%",
                     r.removeFromLeft (36.0f), numberFont (type::small), juce::Justification::centredLeft);

    // falhas: só aparecem quando houve alguma
    if (xruns > 0)
    {
        auto badge = r.removeFromLeft (30.0f);
        icons::draw (g, Icon::warning, badge.removeFromLeft (14.0f).withSizeKeepingCentre (13.0f, 13.0f), overloaded ? colour::signal : colour::warning);
        badge.removeFromLeft (3.0f);
        g.setColour (overloaded ? colour::signal : colour::warning);
        drawTabularText (g, juce::String (juce::jmin (xruns, 999)), badge, numberFont (type::small), juce::Justification::centredLeft);
    }
}

void PerformanceMeter::mouseDown (const juce::MouseEvent&)
{
    if (xruns > 0)
        app.run ("audio.resetXruns");
}

//==============================================================================
TransportBar::TransportBar (AppController& a) : app (a), bpm (a), performance (a)
{
    fileButton.setShowsMenuArrow (true);
    fileButton.setTooltip ("Arquivo: novo, abrir, salvar, exportar e áudio"_t);
    fileButton.onClick = [this] { if (onFileMenu) onFileMenu(); };

    undoButton.setTooltip ("Desfazer (Ctrl+Z)");
    undoButton.onClick = [this] { app.run ("edit.undo"); };
    redoButton.setTooltip ("Refazer (Ctrl+Y)");
    redoButton.onClick = [this] { app.run ("edit.redo"); };
    saveButton.setTooltip ("Salvar o projeto (Ctrl+S)");
    saveButton.onClick = [this] { if (onSave) onSave(); };

    modeSwitch.setItems ({ { "PAT",  Icon::none, "PAT: o play toca o padrão atual em loop (L)"_t },
                           { "SONG", Icon::none, "SONG: o play toca a playlist inteira (L)" } });
    modeSwitch.onChange = [this] (int index) { app.run ("transport.setSongMode", args ({ { "on", index == 1 } })); };

    playButton.setTooltip ("Tocar ou pausar (barra de espaço)"_t);
    playButton.onClick = [this] { app.run ("transport.togglePlay"); };
    stopButton.setTooltip ("Parar e voltar à agulha (o clique na régua da playlist; no PAT, o começo)"_t);
    stopButton.onClick = [this] { app.run ("transport.stop"); };
    recButton.setTooltip ("Gravar: com o REC aceso, o play grava e o stop guarda o take. Arme a trilha do mixer (voz) ou o canal (MIDI). (R)"
                          " Botão direito: contagem."_t);
    recButton.onClick = [this]
    {
        if (! std::exchange (recMenuClick, false))
            app.run ("transport.record");
    };
    recButton.addMouseListener (this, false);

    metronomeButton.setTooltip ("Metrônomo (Ctrl+M)"_t);
    metronomeButton.onClick = [this] { app.run ("transport.setMetronome", args ({ { "on", ! app.getModel().get().metronome } })); };

    typingButton.setTooltip ("Teclado do computador como piano (Ctrl+T): Z a M e Q a U tocam o canal escolhido. "
                             "- e = mudam a oitava, [ e ] a força. Enquanto ele está ligado, os atalhos de uma letra ficam desligados."_t);
    typingButton.setClickingTogglesState (false);
    typingButton.onClick = [this] { if (onTypingKeyboard) onTypingKeyboard(); };

    tapButton.setTooltip ("Tap tempo: clique no ritmo da música (4 vezes ou mais) e o BPM segue as batidas. "
                          "Uma pausa de 2 s recomeça a conta."_t);
    tapButton.onClick = [this] { tapTempo(); };

    loopButton.setTooltip ("Repetir: o padrão em PAT, a música em SONG"_t);
    loopButton.onClick = [this] { app.run ("transport.setLoop", args ({ { "on", ! app.getModel().get().loop } })); };

    for (auto* ts : timeSignatures)
        timeSig.addItem (ts, timeSig.getNumItems() + 1);

    theme::styleComboBox (timeSig, Icon::none, true);
    timeSig.getProperties().set ("style", "lcd");
    timeSig.setTooltip ("Fórmula de compasso"_t);
    timeSig.onChange = [this]
    {
        if (refreshing) return;
        const auto parts = juce::StringArray::fromTokens (timeSig.getText(), "/", {});
        app.run ("transport.setTimeSignature", args ({ { "numerator", parts[0].getIntValue() }, { "denominator", parts[1].getIntValue() } }));
    };

    theme::styleComboBox (patternBox, Icon::pattern);
    patternBox.setTooltip ("Padrão que o channel rack mostra e que toca em PAT"_t);
    patternBox.onChange = [this]
    {
        if (refreshing) return;
        const int index = patternBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (index, patternIds.size()))
            app.run ("pattern.select", args ({ { "pattern", patternIds[index] } }));
    };

    newPatternButton.setTooltip ("Novo padrão"_t);
    newPatternButton.onClick = [this] { app.run ("pattern.add"); };

    masterMeter.setTooltip ("Saída master. Clique para limpar o aviso de clipe."_t);

    for (juce::Component* c : { (juce::Component*) &fileButton, (juce::Component*) &undoButton, (juce::Component*) &redoButton,
                                (juce::Component*) &saveButton, (juce::Component*) &modeSwitch, (juce::Component*) &playButton,
                                (juce::Component*) &stopButton, (juce::Component*) &recButton, (juce::Component*) &bpm, (juce::Component*) &timeSig,
                                (juce::Component*) &loopButton, (juce::Component*) &metronomeButton, (juce::Component*) &typingButton,
                                (juce::Component*) &tapButton, (juce::Component*) &patternBox,
                                (juce::Component*) &newPatternButton, (juce::Component*) &performance, (juce::Component*) &masterMeter })
        addAndMakeVisible (c);
}

TransportBar::~TransportBar()
{
    if (std::exchange (tapGesture, false))
        app.getCommands().endGesture();
}

void TransportBar::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (tapper.idleSince (now) && std::exchange (tapGesture, false))
        app.getCommands().endGesture();

    const double value = tapper.tap (now);

    if (value <= 0.0)
    {
        if (onStatus) onStatus ("Tap tempo: continue clicando no ritmo..."_t, false);
        return;
    }

    if (! tapGesture)
    {
        tapGesture = true;
        app.getCommands().beginGesture ("Tap tempo");
    }

    app.run ("transport.setBpm", args ({ { "bpm", value } }));

    if (onStatus)
        onStatus ("Tap tempo: "_t + juce::String (juce::roundToInt (value)) + " BPM (" + juce::String (tapper.count())
                      + " batidas). Continue para afinar; pare 2 s para fechar."_t, false);
}

void TransportBar::setTypingKeyboard (bool on)
{
    typingButton.setToggleState (on, juce::dontSendNotification);
}

void TransportBar::refresh (const Project& p)
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);

    bpm.setBpm (p.bpm);
    modeSwitch.setSelected (p.songMode ? 1 : 0);
    metronomeButton.setToggleState (p.metronome, juce::dontSendNotification);
    loopButton.setToggleState (p.loop, juce::dontSendNotification);
    timeSig.setText (juce::String (p.timeSigNum) + "/" + juce::String (p.timeSigDen), juce::dontSendNotification);

    juce::StringArray ids, names;
    for (auto& pat : p.patterns) { ids.add (pat.id); names.add (pat.name); }

    // os nomes também: o pattern.rename (pela IA ou pelo menu) não muda o id (teste 6.5 da Fase 4, 23/10)
    if (ids != patternIds || names != patternNames)
    {
        patternIds = ids;
        patternNames = names;
        patternBox.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            patternBox.addItem (names[i], i + 1);
    }

    patternBox.setSelectedItemIndex (patternIds.indexOf (p.currentPattern), juce::dontSendNotification);
    masterMeter.setSource (app.getProjector().meterFor (masterTrackId));
}

void TransportBar::tick()
{
    // tap tempo: 2 s sem batida fecham o passo de desfazer das batidas
    if (tapGesture && tapper.idleSince (juce::Time::getMillisecondCounterHiRes()))
    {
        tapGesture = false;
        app.getCommands().endGesture();
    }

    // tocando: o play acende e vira pausa
    const bool playing = app.isPlaying();
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setIcon (playing ? Icon::pause : Icon::play);
    masterMeter.tick();

    // REC: aceso em laranja quando o próximo play grava; gravando, pisca no vermelho funcional
    auto* rec = app.recorder();
    const bool recordMode = rec != nullptr && rec->isRecordMode();
    const bool recording = rec != nullptr && rec->isRecording();
    recButton.setToggleState (recordMode, juce::dontSendNotification);
    recButton.setAlert (recording ? ((juce::Time::getMillisecondCounter() / 500) % 2 == 0 ? 1.0f : 0.35f) : 0.0f);

    const auto& perf = app.performanceSnapshot();
    performance.update (perf.cpu, perf.cpuPeak, perf.xruns, perf.overloaded);

    const auto& p = app.getModel().get();
    const int steps = (int) std::floor (app.positionInSteps() + 1.0e-6);
    const int stepsPerBeat = juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen));
    const int stepsPerBar = stepsPerBeat * p.timeSigNum;
    const int countIn = app.countInBeatsLeft();
    const auto text = countIn > 0 ? juce::String (countIn)   // a contagem: 4, 3, 2, 1
                                  : juce::String (juce::jmax (0, steps) / stepsPerBar + 1) + "." + juce::String ((juce::jmax (0, steps) % stepsPerBar) / stepsPerBeat + 1)
                                        + "." + juce::String (juce::jmax (0, steps) % stepsPerBeat + 1);

    if (text != position || countingIn != (countIn > 0))
    {
        position = text;
        countingIn = countIn > 0;
        repaint (positionSection);
    }
}

void TransportBar::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &recButton && e.mods.isPopupMenu())
    {
        recMenuClick = true;
        showCountInMenu();
    }
}

void TransportBar::showCountInMenu()
{
    auto* rec = app.recorder();
    const int now = rec != nullptr ? rec->settings().countInBars : 0;

    juce::PopupMenu menu;

    if (rec != nullptr && rec->isRecording())
    {
        menu.addItem (9, "Cancelar o take (apaga o arquivo)"_t);
        menu.addSeparator();
    }

    menu.addSectionHeader ("Contagem antes de gravar (Ctrl+P)"_t);
    menu.addItem (1, "Sem contagem"_t, true, now == 0);
    menu.addItem (2, "1 compasso"_t, true, now == 1);
    menu.addItem (3, "2 compassos"_t, true, now == 2);

    juce::Component::SafePointer<TransportBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recButton), [safe] (int choice)
    {
        if (safe == nullptr)
            return;

        safe->recMenuClick = false;

        if (choice == 9)
            safe->app.run ("record.cancel");
        else if (choice > 0)
            safe->app.run ("record.setCountIn", args ({ { "bars", choice - 1 } }));
    });
}

void TransportBar::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::background);

    // as legendas dos botões do centro, no mesmo estilo das do visor
    g.setColour (colour::textFaint);
    g.setFont (captionFont());

    for (const auto& [area, text] : captions)
        g.drawText (text, area, juce::Justification::centred, false);

    // visor: um cartão rebaixado com três leituras, cada uma com a legenda embaixo
    const auto lcd = lcdArea.toFloat();
    g.setColour (colour::panel);
    g.fillRoundedRectangle (lcd, metric::radius + 2.0f);
    g.setColour (colour::line);
    g.drawRoundedRectangle (lcd.reduced (0.5f), metric::radius + 2.0f, 1.0f);

    for (auto x : { positionSection.getRight(), bpmSection.getRight() })
        g.fillRect (x, lcdArea.getY() + 10, 1, lcdArea.getHeight() - 20);

    g.setColour (colour::text);
    drawTabularText (g, position, valueArea (positionSection).toFloat(), numberFont (19.0f), juce::Justification::centred);

    g.setColour (colour::textFaint);
    g.setFont (captionFont());
    g.drawText (countingIn ? "CONTAGEM"_t : "POSIÇÃO"_t, captionArea (positionSection), juce::Justification::centred, false);
    g.drawText ("BPM", captionArea (bpmSection), juce::Justification::centred, false);
    g.drawText ("COMPASSO", captionArea (signatureSection), juce::Justification::centred, false);

    if (masterMeter.isVisible())
        icons::draw (g, Icon::speaker, speakerArea.toFloat(), colour::textFaint);
}

void TransportBar::resized()
{
    using namespace theme;
    const int c = metric::control;

    // os botões ficam alinhados com o topo do visor; embaixo dos do centro, a legenda
    const int keyTop = (getHeight() - lcdHeight) / 2;
    auto r = juce::Rectangle<int> (0, keyTop, getWidth(), c);
    r.removeFromLeft (4);
    r.removeFromRight (metric::pad);

    // cada botão do centro ocupa a largura da própria legenda, para as palavras não se encostarem
    auto column = [] (const juce::String& caption, int keyWidth)
    {
        return juce::jmax (keyWidth, juce::roundToInt (juce::GlyphArrangement::getStringWidth (captionFont(), caption)) + 6);
    };

    const juce::String playCap ("TOCAR"), stopCap ("PARAR"), recCap ("GRAVAR"), loopCap ("LOOP"), clickCap ("CLIQUE"), typingCap ("TECLADO"),
                       tapCap ("BATER");
    const int playCol = column (playCap, 40), stopCol = column (stopCap, 40), recCol = column (recCap, 40);
    const int loopCol = column (loopCap, c), clickCol = column (clickCap, c), typingCol = column (typingCap, c);
    const int tapKey = tapButton.getIdealWidth (c), tapCol = column (tapCap, tapKey);

    // esquerda: arquivo e edição
    fileButton.setBounds (r.removeFromLeft (fileButton.getIdealWidth (c)));
    r.removeFromLeft (metric::gap);
    undoButton.setBounds (r.removeFromLeft (c));
    r.removeFromLeft (2);
    redoButton.setBounds (r.removeFromLeft (c));
    r.removeFromLeft (2);
    saveButton.setBounds (r.removeFromLeft (c));
    r.removeFromLeft (metric::pad);

    // o que não cabe sai primeiro: o medidor master, depois a CPU, e por fim o visor encolhe
    int lcdWidth = 288;
    const int fixedCentre = 112 + 12 + playCol + 4 + stopCol + 4 + recCol + 12 + 12 + loopCol + 2 + clickCol + 2 + typingCol + 2 + tapCol;
    const int rightBase = 164 + 4 + c;
    const int perfBlock = PerformanceMeter::idealWidth + metric::pad;
    const int meterBlock = 24 + 16 + 8 + 96;
    const int needed = fixedCentre + lcdWidth + rightBase + metric::pad * 2;
    const bool showPerf = r.getWidth() >= needed + perfBlock;
    const bool showMeter = r.getWidth() >= needed + perfBlock + meterBlock;

    if (! showPerf)
        lcdWidth = juce::jlimit (210, 288, r.getWidth() - fixedCentre - rightBase - metric::pad * 2);

    // direita: padrão, CPU e saída master
    masterMeter.setVisible (showMeter);
    performance.setVisible (showPerf);

    if (showMeter)
    {
        masterMeter.setBounds (r.removeFromRight (96).withSizeKeepingCentre (96, 8));
        r.removeFromRight (8);
        speakerArea = r.removeFromRight (16).withSizeKeepingCentre (16, 16);
        r.removeFromRight (24);
    }

    if (showPerf)
    {
        performance.setBounds (r.removeFromRight (PerformanceMeter::idealWidth));
        r.removeFromRight (metric::pad);
    }

    newPatternButton.setBounds (r.removeFromRight (c));
    r.removeFromRight (4);
    patternBox.setBounds (r.removeFromRight (164));
    r.removeFromRight (metric::pad);

    // centro: centrado na barra quando cabe; senão, no meio do espaço que sobrou
    const int centreWidth = fixedCentre + lcdWidth;
    int x = (getWidth() - centreWidth) / 2;

    if (x < r.getX() || x + centreWidth > r.getRight())
        x = r.getX() + juce::jmax (0, (r.getWidth() - centreWidth) / 2);

    auto centre = juce::Rectangle<int> (x, r.getY(), centreWidth, c);
    captions.clear();

    auto place = [&centre, this] (juce::Component& button, int columnWidth, int keyWidth, const juce::String& caption)
    {
        const auto col = centre.removeFromLeft (columnWidth);
        button.setBounds (col.withSizeKeepingCentre (keyWidth, col.getHeight()));
        captions.push_back ({ juce::Rectangle<int> (col.getX(), col.getBottom() + 1, col.getWidth(), 11), caption });
    };

    // PAT | SONG: as siglas ganham a palavra embaixo de cada metade
    const auto mode = centre.removeFromLeft (112);
    modeSwitch.setBounds (mode);
    captions.push_back ({ juce::Rectangle<int> (mode.getX(), mode.getBottom() + 1, mode.getWidth() / 2, 11), "PADRÃO"_t });
    captions.push_back ({ juce::Rectangle<int> (mode.getCentreX(), mode.getBottom() + 1, mode.getWidth() / 2, 11), "MÚSICA"_t });
    centre.removeFromLeft (12);
    place (playButton, playCol, 40, playCap);
    centre.removeFromLeft (4);
    place (stopButton, stopCol, 40, stopCap);
    centre.removeFromLeft (4);
    place (recButton, recCol, 40, recCap);
    centre.removeFromLeft (12);
    lcdArea = juce::Rectangle<int> (centre.getX(), (getHeight() - lcdHeight) / 2, lcdWidth, lcdHeight);
    centre.removeFromLeft (lcdWidth);
    centre.removeFromLeft (12);
    place (loopButton, loopCol, c, loopCap);
    centre.removeFromLeft (2);
    place (metronomeButton, clickCol, c, clickCap);
    centre.removeFromLeft (2);
    place (typingButton, typingCol, c, typingCap);
    centre.removeFromLeft (2);
    place (tapButton, tapCol, tapKey, tapCap);

    auto lcd = lcdArea;
    positionSection = lcd.removeFromLeft (lcd.getWidth() * 36 / 100);
    bpmSection = lcd.removeFromLeft (lcd.getWidth() * 55 / 100);
    signatureSection = lcd;

    bpm.setBounds (valueArea (bpmSection).reduced (6, 0));
    timeSig.setBounds (valueArea (signatureSection).reduced (10, 0));
}

} // namespace whynot
