#include "MainComponent.h"
#include "DeviceSettings.h"
#include "NoteNames.h"
#include "MainWindow.h"
#include "ShortcutEditor.h"
#include "PluginWindow.h"
#include "WindowOwner.h"
#include "fx/FxEditor.h"
#include "synth/SynthEditor.h"
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

    constexpr int statusHeight = theme::metric::status;
    constexpr int transportHeight = theme::metric::topBar;
    constexpr int gap = theme::metric::gap;

    // O rack ocupa a largura toda do cartão; só cede espaço à barra de rolagem quando ela aparece.
    void fitRack (ChannelRack& rack, juce::Viewport& viewport)
    {
        const bool scrolls = rack.getIdealHeight() > viewport.getHeight();
        rack.setSize (juce::jmax (viewport.getWidth() - (scrolls ? viewport.getScrollBarThickness() : 0), 900), rack.getIdealHeight());
    }

    // altura que sobra para os cartões (tela de cima + fresta + mixer)
    int contentHeight (int totalHeight)   { return totalHeight - statusHeight - transportHeight - gap; }

    // Teclado do computador como piano, no desenho do FL: a fileira de baixo é uma oitava
    // (Z = Dó, S = Dó#...), a de cima é a seguinte (Q = Dó central).
    const std::vector<std::pair<int, int>>& typingLayout()
    {
        static const std::vector<std::pair<int, int>> keys {
            { 'Z', 48 }, { 'S', 49 }, { 'X', 50 }, { 'D', 51 }, { 'C', 52 }, { 'V', 53 }, { 'G', 54 },
            { 'B', 55 }, { 'H', 56 }, { 'N', 57 }, { 'J', 58 }, { 'M', 59 }, { ',', 60 },
            { 'Q', 60 }, { '2', 61 }, { 'W', 62 }, { '3', 63 }, { 'E', 64 }, { 'R', 65 }, { '5', 66 },
            { 'T', 67 }, { '6', 68 }, { 'Y', 69 }, { '7', 70 }, { 'U', 71 }, { 'I', 72 }, { '9', 73 },
            { 'O', 74 }, { '0', 75 }, { 'P', 76 },
        };
        return keys;
    }

    bool isTypingKey (const juce::KeyPress& k)
    {
        const int code = (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode());

        for (auto& [key, note] : typingLayout())
            if (key == code)
                return true;

        return false;
    }

    // Atalho "de uma tecla": sem Ctrl nem Alt e numa tecla de escrever (L, R...). Espaço e F5-F10 continuam.
    bool isSingleKeyShortcut (const juce::KeyPress& k)
    {
        const auto mods = k.getModifiers();
        const int code = k.getKeyCode();
        return ! mods.isCommandDown() && ! mods.isAltDown() && code > 32 && code < 127;
    }
}

MainComponent::MainComponent (AppController& a)
    : app (a), transport (a), browser (a), sounds (a), playlist (a), rack (a), pianoRoll (a), projects (a.library()), mixer (a)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setLookAndFeel (&lookAndFeel);

    juce::PropertiesFile::Options options;
    options.applicationName = "WHYNOT LAB";
    options.filenameSuffix = ".settings";
    options.folderName = "WHYNOT LAB";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (options);

    auto toStatus = [this] (const juce::String& m, bool e) { showStatus (m, e); };
    transport.onFileMenu = [this] { showFileMenu(); };
    transport.onSave = [this] { saveProject (false); };
    transport.onStatus = [this] (const juce::String& m, bool isError) { showStatus (m, isError); };
    transport.onTypingKeyboard = [this] { setTypingKeyboard (! typingKeyboard); };
    rack.onStatus = toStatus;
    mixer.onStatus = toStatus;
    pianoRoll.onStatus = toStatus;
    playlist.onStatus = toStatus;
    projects.onStatus = toStatus;
    projects.onOpenProject = [this] (const juce::File& f) { openProjectFile (f); };
    projects.onCommand = [this] (const juce::String& id, const juce::var& params) { return app.run (id, params); };
    app.onTogglePanel = [this] (const juce::String& panel) { togglePanel (panel); };
    rail.onToggle = [this] (const juce::String& panel) { togglePanel (panel); };
    rail.onSettings = [this] { showAudioSettings(); };
    sounds.onStatus = toStatus;
    app.onShortcutsChanged = [this] { repaint(); };

    // tom do beat: a análise acabou (em segundo plano) -> aviso discreto no pé e as dicas do piano roll
    addChildComponent (beatNotice);
    beatNotice.onUse = [this] { useBeatKey(); };
    beatNotice.onDismiss = [this]
    {
        dismissedBeatKey = beatNoticeId;
        updateBeatNotice();
    };
    app.onBeatKey = [this] (const juce::File&) { beatKeyAnalysed(); };

    // a IA (Fase 4): o aviso no pé só aparece com a ponte ligada (--control), pelo setAiHooks
    addChildComponent (aiNotice);
    aiNotice.onToggle = [this]
    {
        if (aiHooks.setEnabled && aiHooks.enabled)
        {
            const bool turnOn = ! aiHooks.enabled();
            aiHooks.setEnabled (turnOn);
            showStatus (turnOn ? "IA ligada: ela volta a usar a WHYNOT no próximo pedido."_t
                               : "IA desligada: a conexão fechou, e as próximas são recusadas até você ligar de novo."_t, false);
        }

        updateAiNotice();
    };

    // a dica tem cantos redondos: a janela dela não pode ser opaca
    tooltips.setOpaque (false);

    // plugins: janela do editor, navegador (F8) e aviso de plugin bloqueado na abertura
    app.setPluginWindowFactory ([this] (te::Plugin& p)
    {
        auto window = PluginWindow::create (p);

        // A janela do plugin pertence à principal: clicar no app não a esconde atrás dele
        // (como as janelas soltas do FL), mas ela não fica por cima dos outros programas.
        if (window != nullptr)
            if (auto* top = getTopLevelComponent(); top != nullptr && top->getPeer() != nullptr && window->getPeer() != nullptr)
                setNativeWindowOwner (window->getWindowHandle(), top->getWindowHandle());

        return window;
    });
    PluginWindow::onClosed = [this] (te::Plugin&) { app.capturePluginStates(); updateTitle(); };

    // Janelas dos efeitos nativos (EQ, compressor...): a mesma janela de plugin, com o nosso editor.
    PluginWindow::nativeEditorFactory = [this] (te::Plugin& p) -> std::unique_ptr<juce::Component>
    {
        if (auto* fx = dynamic_cast<WhynotFxPlugin*> (&p))
            return std::make_unique<FxEditor> (app, *fx);

        if (auto* synthPlugin = dynamic_cast<WhynotSynthPlugin*> (&p))
            return std::make_unique<SynthEditor> (app, *synthPlugin);

        return {};
    };
    app.onPluginsChanged = [this] { browser.refresh(); };
    app.onScanProgress = [this] (const juce::String& t, double p) { browser.setScanStatus (t, p); };
    browser.onStatus = toStatus;
    browser.onAddInstrument = [this] (const juce::PluginDescription& d)
    {
        const auto r = app.run ("channel.addPlugin", args ({ { "plugin", d.createIdentifierString() } }));

        if (! r.ok)
        {
            showStatus (r.error, true);
            return;
        }

        selectChannel (r.value["channel"].toString());
        showStatus (d.name + " no channel rack", false);
    };
    browser.onAddEffect = [this] (const juce::PluginDescription& d)
    {
        showStatus (d.name + ": para usar este efeito, abra o mixer (F9) e clique em \"Adicionar efeito\" numa trilha."_t, false);
    };
    browser.refresh();

    // o canal escolhido no rack é o que o piano roll edita
    rack.onChannelSelected = [this] (const juce::String& id) { selectChannel (id); };
    rack.onOpenPianoRoll = [this] (const juce::String& id) { selectChannel (id); showView (View::pianoRoll); };
    pianoRoll.onChannelPicked = [this] (const juce::String& id) { selectChannel (id); };

    playlist.onOpenPattern = [this] (const juce::String& id)
    {
        runCommand ("pattern.select", args ({ { "pattern", id } }));
        showView (View::channelRack);
    };

    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (true, true);

    addAndMakeVisible (rail);
    addAndMakeVisible (transport);
    addChildComponent (browser);
    addChildComponent (sounds);
    addChildComponent (playlist);
    addChildComponent (rackViewport);
    addChildComponent (pianoRoll);
    addChildComponent (projects);
    addAndMakeVisible (divider);
    addAndMakeVisible (mixer);
    addAndMakeVisible (statusBar);
    addChildComponent (welcome);

    // "Comece aqui" (revisão de iniciante, 22/10): três passos; cada um encolhe o cartão e deixa a orientação no pé
    welcome.onBeat = [this]
    {
        // kick, clap e hats no padrão atual, num passo de desfazer: o play já toca alguma coisa
        const auto& p = app.getModel().get();
        auto find = [&p] (const char* word)
        {
            for (auto& c : p.channels)
                if (! c.isAudio() && c.name.equalsIgnoreCase (word))
                    return c.id;

            for (auto& c : p.channels)
                if (! c.isAudio() && c.name.containsIgnoreCase (word))
                    return c.id;

            return juce::String();
        };

        const auto kick = find ("kick"), clap = find ("clap"), hat = find ("hat");

        if (kick.isEmpty() && clap.isEmpty() && hat.isEmpty())
        {
            statusBar.showNote ("Não achei kick, clap e hat no rack. Arraste sons do navegador de Sons (Alt+F8) para o rack."_t);
            return;
        }

        auto put = [this] (const juce::String& channel, std::initializer_list<int> steps)
        {
            if (channel.isNotEmpty())
                for (int s : steps)
                    app.run ("steps.set", args ({ { "channel", channel }, { "index", s }, { "on", true } }));
        };

        app.getCommands().beginGesture ("Beat de exemplo"_t);
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        put (kick, { 0, 10 });
        put (clap, { 4, 12 });
        put (hat, { 0, 2, 4, 6, 8, 10, 12, 14 });
        app.getCommands().endGesture();

        showView (View::channelRack);
        welcome.setCollapsed (true);
        statusBar.showNote ("Beat de exemplo no padrão: aperte o play (espaço) para ouvir. Clique nos passos para mudar."_t);
    };

    welcome.onVoice = [this]
    {
        if (! showMixer)
            togglePanel ("mixer");

        welcome.setCollapsed (true);

        // com o "gravar voz em um clique" da Gravação, o passo grava de verdade: o record.voice monta e arma a trilha
        // "Voz" e liga a contagem e o REC; o play começa (como o botão "Gravar voz" da playlist). Sem ele, mostra o caminho.
        if (app.getCommands().find ("record.voice") != nullptr)
        {
            auto result = app.run ("record.voice");

            if (result.ok)
                result = app.run ("transport.play");

            if (! result.ok)
                statusBar.showError (result.error, "Abrir Áudio e MIDI"_t, [this] { showAudioSettings(); });
            else
                statusBar.showNote ("Gravando a voz depois da contagem. Para parar, aperte o espaço (ou PARAR)."_t);

            return;
        }

        statusBar.showNote ("Para gravar a voz: no mixer, aperte REC numa trilha e depois GRAVAR, lá em cima. "
                            "O microfone e o fone se escolhem em Áudio e MIDI."_t,
                            "Abrir Áudio e MIDI"_t, [this] { showAudioSettings(); });
    };

    welcome.onExport = [this]
    {
        welcome.setCollapsed (true);
        showExport (ExportDialog::Format::mp3);
    };

    welcome.onClose = [this] (bool forever)
    {
        welcome.setVisible (false);

        if (forever)
            settings->setValue ("welcomeHidden", true);
    };

    layout.setItemLayout (0, 160, -1.0, -0.48);   // tela de cima
    layout.setItemLayout (1, gap, gap, gap);      // a fresta entre os cartões é a divisória
    layout.setItemLayout (2, 220, -1.0, -0.52);   // mixer

    buildActions();
    setWantsKeyboardFocus (true);

    // Clique numa área que não guarda o foco (o fundo do rack, um cartão): sem isto ninguém fica com
    // o foco, a tecla vai para a janela e não desce até aqui, e os atalhos param.
    addMouseListener (this, true);

    app.getModel().addListener (this);
    refreshAll();
    setUiScale ((float) settings->getDoubleValue ("uiScale", 1.0));

    // só num projeto novo (não num aberto do disco) e se a pessoa não pediu para não mostrar
    welcome.setVisible (! settings->getBoolValue ("welcomeHidden", false) && app.getModel().file == juce::File());

    setSize (1440, 880);
    startTimerHz (30);

    if (const auto warning = app.takeStartupWarning(); warning.isNotEmpty())
        showStatus (warning, true);

    // queda na última sessão: oferece o autosave depois que a janela aparecer
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->offerRecovery(); });
}

MainComponent::~MainComponent()
{
    stopTimer();
    app.getModel().removeListener (this);
    releaseTypedNotes();
    app.onTogglePanel = nullptr;
    app.onShortcutsChanged = nullptr;
    app.onBeatKey = nullptr;
    app.onPluginsChanged = nullptr;
    app.onScanProgress = nullptr;
    PluginWindow::onClosed = nullptr;
    PluginWindow::nativeEditorFactory = nullptr;
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::projectChanged (const ProjectModel&)
{
    refreshAll();
    updateBeatNotice();
}

//==============================================================================
// Tom do beat
const BeatKeyAnalyzer::Result* MainComponent::mainBeatKey (std::shared_ptr<const BeatKeyAnalyzer::Result>& keep) const
{
    auto* analyzer = app.beatKey();

    if (analyzer == nullptr)
        return nullptr;

    const auto beats = analyzer->beatsOf (app.getModel());

    if (beats.empty())
        return nullptr;

    keep = analyzer->find (beats.front().file);
    return keep != nullptr && keep->ok && keep->analysis.found() ? keep.get() : nullptr;
}

void MainComponent::updateBeatNotice()
{
    std::shared_ptr<const BeatKeyAnalyzer::Result> keep;
    const auto* r = mainBeatKey (keep);
    bool show = false;

    if (r != nullptr)
    {
        const auto& best = r->analysis.best();
        const auto& p = app.getModel().get();
        beatNoticeId = r->file.getFullPathName() + "|" + r->stamp + "|" + juce::String (best.root) + (best.major ? "M" : "m");

        // só quando muda algo: o tom do projeto já é esse, ou a pessoa disse "agora não"
        show = ! (p.keyRoot == best.root && p.scale == (best.major ? "major" : "minor")) && beatNoticeId != dismissedBeatKey;

        if (show)
            beatNotice.show (beatKeyText (r->analysis), beatKeyDetails (r->analysis));
    }

    if (show != beatNotice.isVisible())
    {
        beatNotice.setVisible (show);
        layoutBeatNotice();
        repaint (getLocalBounds().removeFromBottom (statusHeight));
    }
    else if (show)
    {
        layoutBeatNotice();
    }
}

void MainComponent::beatKeyAnalysed()
{
    pianoRoll.refresh (app.getModel().get());   // as dicas do 808 dependem da análise
    updateBeatNotice();

    // o tom do beat é o do projeto: só uma mensagem, sem pedir nada
    std::shared_ptr<const BeatKeyAnalyzer::Result> keep;

    if (const auto* r = mainBeatKey (keep); r != nullptr && ! beatNotice.isVisible() && beatNoticeId != dismissedBeatKey)
        showStatus (beatKeyText (r->analysis) + ", o mesmo do projeto"_t, false);
}

void MainComponent::useBeatKey()
{
    std::shared_ptr<const BeatKeyAnalyzer::Result> keep;

    if (const auto* r = mainBeatKey (keep))
    {
        const auto& best = r->analysis.best();
        const auto result = app.run ("project.setKey", args ({ { "root", best.root }, { "scale", best.major ? "major" : "minor" } }));
        showStatus (result.ok ? "Tom do projeto: "_t + keyName (best) + ". No piano roll, as notas fora da escala ficam escuras."_t
                              : result.error, ! result.ok);
    }

    updateBeatNotice();
}

void MainComponent::layoutBeatNotice()
{
    auto bar = getLocalBounds().removeFromBottom (statusHeight).reduced (theme::metric::pad, 0);
    bar.removeFromRight (typingKeyboard ? 520 : 300);
    int limit = -1;

    // o pé agora é a StatusBar (Design), um componente: os avisos ficam por cima dela, e a mensagem acaba antes.
    // A IA (Fase 4) fica mais à direita; o tom do beat, à esquerda dela.
    if (aiNotice.isVisible())
    {
        aiNotice.setBounds (bar.removeFromRight (juce::jmin (aiNotice.getIdealWidth(), bar.getWidth() / 2)));
        aiNotice.toFront (false);
        limit = aiNotice.getX();
        bar.removeFromRight (16);
    }

    if (beatNotice.isVisible())
    {
        const int width = juce::jmin (beatNotice.getIdealWidth(), bar.getWidth() / 2);
        beatNotice.setBounds (bar.removeFromRight (width));
        beatNotice.toFront (false);
        limit = beatNotice.getX();
    }

    statusBar.setMessageLimit (limit < 0 ? -1 : limit - statusBar.getX() - 16);
}

//==============================================================================
// A IA (Fase 4, fatia 4.5)
void MainComponent::setAiHooks (AiHooks hooks)
{
    aiHooks = std::move (hooks);
    updateAiNotice();
}

void MainComponent::updateAiNotice()
{
    if (! aiHooks.connected)
        return;   // sem ponte (o app abriu sem --control): o aviso nunca aparece

    using State = AiStatusNotice::State;
    const auto state = ! aiHooks.enabled()                          ? State::off
                     : aiCard != nullptr                            ? State::waiting
                     : aiHooks.connected()                          ? State::connected
                                                                    : State::hidden;

    if (state != aiNotice.getState())
    {
        aiNotice.setState (state);
        layoutBeatNotice();
        repaint (getLocalBounds().removeFromBottom (statusHeight));
    }
}

void MainComponent::askAi (const juce::String& title, const juce::String& commandId, const juce::String& reason,
                           const juce::String& details, int seconds, std::function<void (bool)> answer)
{
    cancelAiQuestion();   // a ponte pergunta uma coisa por vez; uma pergunta velha aberta não vale mais

    aiCard = std::make_unique<AiConfirmCard> (title, commandId, reason, details, seconds);
    aiCard->onAnswer = [this, answer] (bool allowed)
    {
        cancelAiQuestion();   // fecha o cartão (ele sai depois: estamos dentro do clique dele)
        answer (allowed);
    };

    addAndMakeVisible (*aiCard);
    aiCard->setBounds (AiConfirmCard::boundsIn (getLocalBounds().withTrimmedTop (theme::metric::topBar).withTrimmedLeft (theme::metric::rail)));
    aiCard->toFront (false);   // por cima das telas, sem tirar o foco dos atalhos
    updateAiNotice();
}

juce::Rectangle<int> MainComponent::capturePng (const juce::String& view, int maxWidth, const juce::File& png, juce::String& error)
{
    static const std::map<juce::String, View> views { { "playlist", View::playlist }, { "channelRack", View::channelRack },
                                                      { "pianoRoll", View::pianoRoll }, { "projects", View::projects } };

    if (view.isNotEmpty())
    {
        const auto found = views.find (view);

        if (found == views.end())
        {
            error = "Tela desconhecida: "_t + view + ". Use playlist, channelRack, pianoRoll ou projects."_t;
            return {};
        }

        showView (found->second);
    }

    if (getWidth() <= 0 || getHeight() <= 0)
    {
        error = "A tela ainda não tem tamanho."_t;
        return {};
    }

    // de software: no Windows a imagem nativa é do Direct2D, e ler dela com um Graphics aberto já travou o autoteste
    auto image = juce::SoftwareImageType().convert (createComponentSnapshot (getLocalBounds(), true, 1.0f));

    if (image.getWidth() > maxWidth)
        image = image.rescaled (maxWidth, juce::jmax (1, image.getHeight() * maxWidth / image.getWidth()), juce::Graphics::highResamplingQuality);

    png.deleteFile();
    juce::FileOutputStream out (png);
    juce::PNGImageFormat format;

    if (! out.openedOk() || ! format.writeImageToStream (image, out))
    {
        error = "Não consegui gravar a captura em "_t + png.getFullPathName();
        return {};
    }

    return image.getBounds();
}

juce::Rectangle<int> MainComponent::captureOffscreen (AppController& app, const juce::String& view, int maxWidth,
                                                      const juce::File& png, juce::String& error)
{
    MainComponent screen (app);
    screen.setSize (1440, 900);
    return screen.capturePng (view, maxWidth, png, error);
}

void MainComponent::cancelAiQuestion()
{
    if (aiCard != nullptr)
    {
        removeChildComponent (aiCard.get());
        retiredAiCard = std::move (aiCard);   // pode estar dentro do clique do próprio cartão: sai na próxima vez
    }

    updateAiNotice();
}

void MainComponent::refreshAll()
{
    const auto& p = app.getModel().get();

    // canal do piano roll: o escolhido, se ainda existe; senão o primeiro com notas
    // no padrão atual; senão o primeiro sampler
    const auto* chosen = p.findChannel (selectedChannel);

    if (chosen == nullptr || chosen->isAudio())
    {
        selectedChannel.clear();
        const auto* pattern = p.currentPatternPtr();

        for (auto& c : p.channels)
            if (! c.isAudio() && pattern != nullptr && pattern->notesFor (c.id) != nullptr) { selectedChannel = c.id; break; }

        for (auto& c : p.channels)
            if (selectedChannel.isEmpty() && ! c.isAudio()) selectedChannel = c.id;
    }

    if (selectedChannel != app.liveChannel())
        app.setLiveChannel (selectedChannel);   // o controlador e o teclado tocam o canal escolhido

    transport.refresh (p);
    rack.setSelectedChannel (selectedChannel);
    rack.refresh (p);
    pianoRoll.setChannel (selectedChannel);
    pianoRoll.refresh (p);
    playlist.refresh (p);
    mixer.refresh (p);

    // O channel rack ocupa a altura do conteúdo (até 60% da tela); o mixer fica com o resto.
    // Só reajusta quando o número de canais muda, para não desfazer um arrasto da divisória.
    if (rack.getIdealHeight() != lastRackHeight)
    {
        lastRackHeight = rack.getIdealHeight();
        updateSplit();
        resized();
    }

    fitRack (rack, rackViewport);
    updateTitle();
}

void MainComponent::selectChannel (const juce::String& id)
{
    if (id == selectedChannel)
        return;

    releaseTypedNotes();   // a nota segurada não fica presa no canal anterior
    selectedChannel = id;
    app.setLiveChannel (id);
    rack.setSelectedChannel (id);
    pianoRoll.setChannel (id);

    if (view == View::pianoRoll && showUpper)
        pianoRoll.focusGrid();   // o Ctrl+V depois de trocar de canal já cola (vídeo do produtor, 22/10)
}

void MainComponent::updateSplit()
{
    // O channel rack ocupa a altura do conteúdo (até 60%); playlist e piano roll pedem mais espaço.
    // O mixer precisa de uns 300 px para o fader ser usável.
    const int available = contentHeight (getHeight());
    const int upperHeight = view == View::channelRack
        ? juce::jlimit (120, juce::jmax (120, (int) (available * 0.6)), lastRackHeight + 2)
        : (available >= 820 ? (int) (available * 0.64) : juce::jmax (200, available - 300));
    layout.setItemLayout (0, 120, -0.85, upperHeight);
    layout.setItemLayout (2, 160, -1.0, juce::jmax (160, available - upperHeight));
}

juce::Component& MainComponent::upperComponent()
{
    switch (view)
    {
        case View::playlist:    return playlist;
        case View::pianoRoll:   return pianoRoll;
        case View::projects:    return projects;
        case View::channelRack: break;
    }

    return rackViewport;
}

void MainComponent::selectMixerTrack (const juce::String& idOrName)
{
    // Primeiro pelo id (mx_...), depois pelo nome; "Master" também vale para o master.
    const auto& p = app.getModel().get();

    if (p.findMixer (idOrName) != nullptr)
    {
        mixer.selectTrack (idOrName);
        return;
    }

    for (auto& m : p.mixer)
    {
        if (m.name == idOrName || (idOrName == "Master" && m.isMaster()))
        {
            mixer.selectTrack (m.id);
            return;
        }
    }
}

void MainComponent::showIdeas (const juce::String& idOrName)
{
    const auto& p = app.getModel().get();

    for (auto& c : p.channels)
        if (c.id == idOrName || c.name == idOrName)
            selectChannel (c.id);

    showView (View::pianoRoll);
    pianoRoll.setIdeasVisible (true);
}

void MainComponent::importMidi()
{
    // as notas entram no canal escolhido, no padrão atual: o piano roll aparece para mostrar onde
    showView (View::pianoRoll);
    pianoRoll.chooseMidiFile();
}

void MainComponent::showView (View v)
{
    // Em tela baixa, piano roll e playlist precisam do espaço do mixer: ele sai (F9 traz de volta)
    // e volta sozinho quando o channel rack aparece de novo.
    const int available = contentHeight (getHeight());

    if (v != View::channelRack && showMixer && getHeight() > 0 && available < 820)
    {
        showMixer = false;
        mixerAutoHidden = true;
        showStatus ("Mixer escondido para dar espaço. F9 mostra de novo."_t, false);
    }
    else if (v == View::channelRack && mixerAutoHidden)
    {
        showMixer = true;
        mixerAutoHidden = false;
    }

    view = v;
    showUpper = true;
    updateSplit();
    resized();

    if (v == View::pianoRoll)
        pianoRoll.focusGrid();   // Ctrl+C/V, setas e Delete valem sem clicar na grade antes
}

void MainComponent::updateTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        const auto& model = app.getModel();
        const auto name = model.file == juce::File() ? model.get().name : model.file.getFileNameWithoutExtension();
        window->setName ((model.dirty ? "*" : "") + name + " - WHYNOT LAB");
    }
}

void MainComponent::timerCallback()
{
    // agulha, medidores e o visor andam no ritmo da tela (animateFrame, pelo VBlank). Sem VBlank (a janela
    // ainda não apareceu, área de trabalho remota) eles seguem por aqui, a 30 Hz, como antes.
    if (! frames.isActive())
        animateFrame (1.0 / 30.0);

    if (! focusedOnce && isShowing())
    {
        focusedOnce = true;
        grabKeyboardFocus();   // ao abrir, os atalhos já valem sem precisar clicar
    }

    updateAiNotice();   // "IA conectada" (só com a ponte ligada; muda a tela só quando o estado muda)

    // tecla solta sem evento (a janela perdeu o foco no meio): a nota não fica presa
    for (auto it = typedNotes.begin(); it != typedNotes.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first) || ! hasKeyboardFocus (true))
        {
            app.liveNote ({}, it->second, 0, false);
            it = typedNotes.erase (it);
        }
        else
        {
            ++it;
        }
    }

    // a agulha, o rack e o piano roll andam no animateFrame (VBlank, Fluidez); a agulha parada do SONG fica aqui
    const auto& p = app.getModel().get();
    const bool playing = app.isPlaying();
    playlist.setNeedle (! playing && p.songMode ? app.playStart() : -1);

    // a gravação avisa (passou para o SONG, nada armado, take curto...): vai para a barra de status
    if (auto* rec = app.recorder(); rec != nullptr && rec->messageSerial() != recordMessageSerial)
    {
        recordMessageSerial = rec->messageSerial();

        if (rec->lastMessage().isNotEmpty())
            showStatus (rec->lastMessage(), false);
    }

    // a escuta pelo app pergunta uma vez se a pessoa está de fone (fatia 8): nas caixas, a voz vira microfonia
    if (auto* rec = app.recorder(); rec != nullptr && rec->headphoneQuestionSerial() != headphoneQuestionSeen)
    {
        headphoneQuestionSeen = rec->headphoneQuestionSerial();
        askHeadphones();
    }

    // o pé: o problema que continua valendo e a luz do teclado-piano (a StatusBar só repinta se mudar)
    const auto& problems = app.getProjector().sampleProblems();
    statusBar.setStandingProblem (problems.size() == 0 ? juce::String()
                                  : juce::String (problems.size()) + (problems.size() == 1 ? " canal sem sample: "_t : " canais sem sample: "_t)
                                        + problems.getAllValues()[0]);
    statusBar.setLamp (typingKeyboard ? "Teclado-piano: "_t + noteNamePt (juce::jlimit (0, 127, 48 + 12 * typingOctave))
                                            + " · "_t + juce::String (typingVelocity)
                                      : juce::String());

    // apertou play e a placa de som não abriu: o erro fica, com o caminho para resolver
    if (playing && ! wasPlaying && app.getEngine().getDeviceManager().deviceManager.getCurrentAudioDevice() == nullptr)
        statusBar.showError ("Sem saída de som: a placa de som não abriu. Escolha a saída e teste o som em Áudio e MIDI."_t,
                             "Abrir Áudio e MIDI"_t, [this] { showAudioSettings(); });

    wasPlaying = playing;

    // abriu um projeto do disco (ou a recuperação abriu): o "Comece aqui" sai
    if (welcome.isVisible() && app.getModel().file != juce::File())
        welcome.setVisible (false);

    updateTitle();
}

void MainComponent::animateFrame (double dt)
{
    transport.tick();
    mixer.tick();

    const auto& p = app.getModel().get();
    const auto* pattern = p.currentPatternPtr();
    const int length = pattern != nullptr ? pattern->lengthSteps : 16;
    const bool playing = app.isPlaying();

    // A posição do transporte só anda a cada bloco de áudio (~10 ms); entre um e outro a agulha segue no
    // andamento, para andar lisa a 60 Hz ou mais (fluidez, 21/10).
    const double ticks = playheadSmoother.update (playing ? app.positionInSteps() * ticksPerStep : -1.0, dt,
                                                  p.bpm / 60.0 * ticksPerBeat);

    // No modo song o padrão não tem um "agora" único: o playhead fica só na playlist.
    rack.setPlayheadStep (playing && ! p.songMode ? ((int) std::floor (ticks / ticksPerStep + 1.0e-6)) % juce::jmax (1, length) : -1);
    pianoRoll.setPlayhead (playing && ! p.songMode ? std::fmod (ticks, (double) juce::jmax (1, length * ticksPerStep)) : -1.0);
    playlist.setPlayhead (playing && p.songMode ? ticks : -1.0);
}

void MainComponent::showStatus (const juce::String& text, bool isError)
{
    // o aviso comum some em 7 s; o erro fica até ser fechado (revisão de iniciante, 22/10)
    if (isError)
        statusBar.showError (text);
    else
        statusBar.showInfo (text);
}

void MainComponent::runCommand (const juce::String& id, const juce::var& params)
{
    const auto result = app.run (id, params);

    if (! result.ok)
        showStatus (result.error, true);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::background);

    // o pé (mensagem, erro, teclado-piano e escala) é a StatusBar
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    // As telas são cartões: recorta os cantos com o preto do fundo.
    g.setColour (theme::colour::background);

    for (juce::Component* c : { &upperComponent(), (juce::Component*) &mixer, (juce::Component*) &browser, (juce::Component*) &sounds })
        if (c->isVisible())
            g.fillPath (theme::cornerMask (c->getBounds().toFloat(), theme::metric::radiusCard));
}

void MainComponent::resized()
{
    layoutBeatNotice();
    updateSplit();

    if (aiCard != nullptr)
        aiCard->setBounds (AiConfirmCard::boundsIn (getLocalBounds().withTrimmedTop (theme::metric::topBar).withTrimmedLeft (theme::metric::rail)));
    auto r = getLocalBounds();
    statusBar.setBounds (r.removeFromBottom (statusHeight));
    rail.setBounds (r.removeFromLeft (theme::metric::rail));
    rail.setState (view, showUpper, showMixer, showBrowser, showSounds);
    transport.setBounds (r.removeFromTop (transportHeight));
    r.removeFromRight (gap);

    // navegador de sons ou de plugins: um cartão fixo à esquerda das telas (um de cada vez).
    // O que sai é escondido antes: o de sons para a prévia ao ficar invisível.
    (showSounds ? (juce::Component&) browser : (juce::Component&) sounds).setVisible (false);
    browser.setVisible (showBrowser);
    sounds.setVisible (showSounds);

    if (showBrowser || showSounds)
    {
        (showSounds ? (juce::Component&) sounds : (juce::Component&) browser).setBounds (r.removeFromLeft (272));
        r.removeFromLeft (gap);
    }

    auto& upper = upperComponent();

    for (juce::Component* c : { (juce::Component*) &playlist, (juce::Component*) &rackViewport, (juce::Component*) &pianoRoll,
                                (juce::Component*) &projects })
        c->setVisible (showUpper && c == &upper);

    mixer.setVisible (showMixer);
    divider.setVisible (showUpper && showMixer);

    if (showUpper && showMixer)
    {
        juce::Component* comps[] = { &upper, &divider, &mixer };
        layout.layOutComponents (comps, 3, r.getX(), r.getY(), r.getWidth(), r.getHeight(), true, true);
    }
    else if (showUpper)
    {
        upper.setBounds (r);
    }
    else if (showMixer)
    {
        mixer.setBounds (r);
    }

    fitRack (rack, rackViewport);

    // "Comece aqui" no canto de cima à direita da tela de cima (ou do mixer, se ela estiver escondida)
    if (welcome.isVisible())
    {
        const auto host = showUpper ? upper.getBounds() : mixer.getBounds();
        const auto size = welcome.idealSize();
        welcome.setBounds (host.getRight() - size.x - theme::metric::pad, host.getY() + theme::metric::toolbar + theme::metric::gap,
                           size.x, size.y);
        welcome.toFront (false);
    }
}

//==============================================================================
void MainComponent::buildActions()
{
    // O que cada ação do KeyMap faz. As teclas ficam no KeyMap (preset FL ou WHYNOT, trocáveis).
    auto toggle = [this] (const char* panel) { return [this, panel] { runCommand ("view.toggle", args ({ { "panel", panel } })); }; };

    actions = {
        { "transport.togglePlay",  [this] { runCommand ("transport.togglePlay"); } },
        { "transport.togglePause", [this] { runCommand ("transport.togglePause"); } },
        { "transport.songMode",    [this] { runCommand ("transport.setSongMode"); } },
        { "transport.metronome",   [this] { runCommand ("transport.setMetronome", args ({ { "on", ! app.getModel().get().metronome } })); } },
        { "transport.record",      [this] { runCommand ("transport.record"); } },
        { "record.cancel",         [this] { runCommand ("record.cancel"); } },
        { "transport.countIn",     [this]
          {
              const auto result = app.run ("record.setCountIn");
              const int bars = result.ok ? (int) result.value["countIn"] : 0;
              showStatus (! result.ok ? result.error
                                      : bars == 0 ? "Contagem desligada."_t
                                                  : "Contagem de "_t + juce::String (bars) + (bars == 1 ? " compasso antes de gravar."_t : " compassos antes de gravar."_t),
                          ! result.ok);
          } },
        { "file.new",              [this] { newProject(); } },
        { "file.open",             [this] { openProject(); } },
        { "file.save",             [this] { saveProject (false); } },
        { "file.saveAs",           [this] { saveProject (true); } },
        { "file.saveNewVersion",   [this] { saveNewVersion(); } },
        { "file.importMidi",       [this] { importMidi(); } },
        { "export.wav",            [this] { showExport (ExportDialog::Format::wav); } },
        { "export.mp3",            [this] { showExport (ExportDialog::Format::mp3); } },
        { "export.midi",           [this] { showExport (ExportDialog::Format::midi); } },
        { "export.stems",          [this] { showExport (ExportDialog::Format::stems); } },
        { "pattern.new",           [this] { goToEmptyPattern(); } },
        { "pattern.clone",         [this] { runCommand ("pattern.clone"); } },
        { "pattern.next",          [this] { stepPattern (1); } },
        { "pattern.previous",      [this] { stepPattern (-1); } },
        { "edit.undoToggle",       [this] { runCommand ("edit.undoToggle"); } },
        { "edit.undo",             [this] { runCommand ("edit.undo"); } },
        { "edit.redo",             [this] { runCommand ("edit.redo"); } },
        { "playlist.addMarker",    [this] { addMarkerAtNeedle(); } },
        { "transport.tapTempo",    [this] { transport.tapTempo(); } },
        { "view.playlist",         toggle ("playlist") },
        { "view.channelRack",      toggle ("channelRack") },
        { "view.pianoRoll",        toggle ("pianoRoll") },
        { "view.projects",         toggle ("projects") },
        { "projects.switchDeck",   [this] { if (view == View::projects && showUpper) projects.switchDeck(); } },
        { "view.files",            toggle ("sounds") },
        { "view.plugins",          toggle ("plugins") },
        { "view.mixer",            toggle ("mixer") },
        { "view.settings",         [this] { showAudioSettings(); } },
        { "view.shortcuts",        [this] { ShortcutEditor::show (app, this); } },
        { "keyboard.typing",       [this] { setTypingKeyboard (! typingKeyboard); } },
        { "record.captureMidi",    [this]
          {
              // a última frase tocada ao vivo vira notas do padrão, no canal escolhido (bloco 3)
              const auto result = app.run ("record.captureMidi");
              showStatus (result.ok ? juce::String ((int) result.value["notes"]) + " notas capturadas no padrão atual."_t : result.error, ! result.ok);
          } },
        { "ui.zoomIn",             [this] { setUiScale (uiScale + 0.1f); } },
        { "ui.zoomOut",            [this] { setUiScale (uiScale - 0.1f); } },
        { "ui.zoomReset",          [this] { setUiScale (1.0f); } },
    };
}

void MainComponent::addMarkerAtNeedle()
{
    // Alt+T, como no FL: tocando a música, onde a agulha está agora; parado, na agulha (o clique na régua)
    const auto& p = app.getModel().get();
    const int at = app.isPlaying() && p.songMode ? juce::roundToInt (app.positionInSteps() * ticksPerStep) : app.playStart();
    const auto result = app.run ("playlist.addMarker", args ({ { "at", juce::jmax (0, at) } }));
    const int barTicks = juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen)) * juce::jmax (1, p.timeSigNum) * ticksPerStep;

    showStatus (result.ok ? "Marcador no compasso "_t + juce::String (juce::jmax (0, at) / barTicks + 1)
                                + ". Botão direito nele (na régua da playlist) dá o nome: Intro, Refrão..."_t
                          : result.error, ! result.ok);
}

void MainComponent::stepPattern (int delta)
{
    const auto& p = app.getModel().get();

    for (int i = 0; i < (int) p.patterns.size(); ++i)
        if (p.patterns[(size_t) i].id == p.currentPattern)
        {
            const int next = i + delta;

            if (juce::isPositiveAndBelow (next, (int) p.patterns.size()))
                runCommand ("pattern.select", args ({ { "pattern", p.patterns[(size_t) next].id } }));

            return;
        }
}

void MainComponent::goToEmptyPattern()
{
    // Como o F4 do FL: o próximo padrão vazio depois do atual; se não houver, um novo
    const auto& p = app.getModel().get();
    bool afterCurrent = false;

    for (auto& pat : p.patterns)
    {
        if (pat.id == p.currentPattern)
        {
            afterCurrent = true;
            continue;
        }

        if (! afterCurrent)
            continue;

        const bool empty = pat.notes.empty() || std::all_of (pat.notes.begin(), pat.notes.end(), [] (const auto& kv) { return kv.second.empty(); });
        const bool noSteps = std::all_of (pat.steps.begin(), pat.steps.end(), [] (const auto& kv)
                                          { return std::none_of (kv.second.begin(), kv.second.end(), [] (const Step& s) { return s.on; }); });

        if (empty && noSteps)
        {
            runCommand ("pattern.select", args ({ { "pattern", pat.id } }));
            return;
        }
    }

    runCommand ("pattern.add");
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Com o teclado-piano ligado, as teclas de nota tocam (em keyStateChanged) e os atalhos
    // de uma tecla só ficam desligados, como no FL. Ctrl, Alt, Espaço e as teclas F continuam.
    // - e = mudam a oitava e [ e ] a força (o FL usa o teclado numérico, que notebook não tem).
    if (typingKeyboard && ! key.getModifiers().isCommandDown() && ! key.getModifiers().isAltDown())
    {
        const auto c = key.getTextCharacter();
        const int octave = c == '-' ? -1 : (c == '=' || c == '+' ? 1 : 0);
        const int force = c == '[' ? -20 : (c == ']' ? 20 : 0);

        if (octave != 0 || force != 0)
        {
            typingOctave = juce::jlimit (-3, 3, typingOctave + octave);
            typingVelocity = juce::jlimit (10, 127, typingVelocity + force);
            showStatus ("Teclado-piano: "_t + typingRangeText(), false);
            repaint (getLocalBounds().removeFromBottom (statusHeight));
            return true;
        }
    }

    if (typingKeyboard && isSingleKeyShortcut (key))
        return isTypingKey (key) || app.getKeyMap().actionFor (key).isNotEmpty();

    const auto id = app.getKeyMap().actionFor (key);

    if (auto it = actions.find (id); id.isNotEmpty() && it != actions.end())
    {
        it->second();
        return true;
    }

    // Ctrl+C/X/V/B com o piano roll na tela e o foco fora da grade (o clique no channel rack para escolher o canal):
    // valem nas notas, como no FL (vídeo do produtor, 22/10)
    if (view == View::pianoRoll && showUpper && pianoRoll.handleClipboardKey (key))
        return true;

    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    if (! typingKeyboard)
        return false;

    const auto mods = juce::ModifierKeys::currentModifiers;
    bool handled = false;

    for (auto& [code, note] : typingLayout())
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (code) && ! mods.isCommandDown() && ! mods.isAltDown();
        const auto held = typedNotes.find (code);

        if (down && held == typedNotes.end())
        {
            const int played = juce::jlimit (0, 127, note + 12 * typingOctave);

            if (app.liveNote ({}, played, typingVelocity, true))
                typedNotes[code] = played;   // solta a mesma nota, mesmo se a oitava mudar no meio

            handled = true;
        }
        else if (! down && held != typedNotes.end())
        {
            app.liveNote ({}, held->second, 0, false);
            typedNotes.erase (held);
            handled = true;
        }
    }

    return handled;
}

void MainComponent::mouseDown (const juce::MouseEvent&)
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();

    if (focused == nullptr || (focused != this && ! isParentOf (focused)))
        grabKeyboardFocus();
}

void MainComponent::releaseTypedNotes()
{
    for (auto& [code, note] : typedNotes)
        app.liveNote ({}, note, 0, false);

    typedNotes.clear();
}

juce::String MainComponent::typingRangeText() const
{
    // Z é a nota mais grave do mapa e P a mais aguda (48 e 76 sem mudar a oitava)
    return noteNamePt (juce::jlimit (0, 127, 48 + 12 * typingOctave)) + " a "_t
           + noteNamePt (juce::jlimit (0, 127, 76 + 12 * typingOctave))
           + ", força "_t + juce::String (typingVelocity) + " (- = oitava, [ ] força)"_t;
}

void MainComponent::setTypingKeyboard (bool on)
{
    if (on == typingKeyboard)
        return;

    typingKeyboard = on;
    transport.setTypingKeyboard (on);
    layoutBeatNotice();
    PianoRollGrid::singleKeyShortcuts = ! on;
    PlaylistGrid::singleKeyShortcuts = ! on;
    releaseTypedNotes();

    const auto* channel = app.getModel().get().findChannel (selectedChannel);
    showStatus (on ? "Teclado-piano ligado: Z a M e Q a U tocam "_t + (channel != nullptr ? channel->name : juce::String ("o canal escolhido"))
                       + ", " + typingRangeText() + ". Atalhos de uma tecla (como L) ficam desligados até o Ctrl+T."_t
                   : "Teclado-piano desligado: as letras voltam a ser atalhos."_t,
                false);
    repaint (getLocalBounds().removeFromBottom (statusHeight));
}

void MainComponent::setUiScale (float scale)
{
    uiScale = juce::jlimit (0.8f, 2.0f, std::round (scale * 10.0f) / 10.0f);
    juce::Desktop::getInstance().setGlobalScaleFactor (uiScale);
    settings->setValue ("uiScale", (double) uiScale);
    statusBar.setRightText ("Interface " + juce::String (juce::roundToInt (uiScale * 100)) + "%   Ctrl+= / Ctrl+-");
    repaint();
}

void MainComponent::setPluginsVisible (bool visible)
{
    showBrowser = visible;
    showSounds = showSounds && ! visible;
    resized();
}

void MainComponent::setSoundsVisible (bool visible)
{
    showSounds = visible;
    showBrowser = showBrowser && ! visible;
    resized();
}

void MainComponent::togglePanel (const juce::String& panel)
{
    // A tecla da tela que já está à vista esconde ela (se o mixer estiver aberto);
    // a de outra tela troca para ela. Nunca fica tudo escondido.
    if (panel == "plugins")
    {
        setPluginsVisible (! showBrowser);
        return;
    }

    if (panel == "sounds")
    {
        setSoundsVisible (! showSounds);
        return;
    }

    if (panel == "mixer")
    {
        mixerAutoHidden = false;
        showMixer = ! showMixer || ! showUpper;
        resized();
        return;
    }

    View target = View::channelRack;
    if (panel == "playlist")        target = View::playlist;
    else if (panel == "pianoRoll")  target = View::pianoRoll;
    else if (panel == "projects")   target = View::projects;
    else if (panel != "channelRack") return;

    if (target == view && showUpper && showMixer)
    {
        showUpper = false;
        resized();
        return;
    }

    showView (target);
}

//==============================================================================
void MainComponent::showFileMenu()
{
    auto& keys = app.getKeyMap();
    juce::PopupMenu menu;

    auto add = [&menu, &keys] (const char* action, const juce::String& text, std::function<void()> run, bool enabled = true)
    {
        juce::PopupMenu::Item item (text);
        item.setEnabled (enabled);
        item.shortcutKeyDescription = keys.describeKeys (action);
        item.setAction (std::move (run));
        menu.addItem (item);
    };

    const bool saved = app.getModel().file != juce::File();

    add ("file.new", "Novo projeto", [this] { newProject(); });
    add ("file.open", "Abrir...", [this] { openProject(); });
    add ("file.openAutosave", "Abrir um autosave...", [this] { openAutosave(); });
    add ("file.importMidi", "Importar MIDI...", [this] { importMidi(); });
    menu.addSeparator();
    add ("file.save", "Salvar", [this] { saveProject (false); });
    add ("file.saveAs", "Salvar como...", [this] { saveProject (true); });
    add ("file.saveNewVersion", "Salvar nova versão"_t, [this] { saveNewVersion(); }, saved);
    menu.addSeparator();
    add ("export.wav", "Exportar WAV...", [this] { showExport (ExportDialog::Format::wav); });
    add ("export.mp3", "Exportar MP3...", [this] { showExport (ExportDialog::Format::mp3); });
    add ("export.midi", "Exportar MIDI...", [this] { showExport (ExportDialog::Format::midi); });
    add ("export.stems", "Exportar stems...", [this] { showExport (ExportDialog::Format::stems); });
    menu.addSeparator();
    add ("view.settings", "Áudio e MIDI..."_t, [this] { showAudioSettings(); });
    add ("view.shortcuts", "Atalhos de teclado...", [this] { ShortcutEditor::show (app, this); });
    add ("keyboard.typing", typingKeyboard ? "Desligar teclado-piano" : "Ligar teclado-piano", [this] { setTypingKeyboard (! typingKeyboard); });

    // Tema de cores (docs/design.md): troca na hora, em todas as janelas abertas, e fica salvo
    auto useScheme = [this] (theme::ColourScheme s)
    {
        if (s == theme::currentScheme())
            return;

        const auto before = theme::currentScheme();
        theme::applyScheme (s);
        lookAndFeel.refreshColours();
        theme::reskinAll (before);

        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
            window->setBackgroundColour (theme::colour::background);

        settings->setValue ("theme", theme::schemeKey (s));
        showStatus ("Tema de cores: "_t + theme::schemeName (s), false);
    };

    juce::PopupMenu themes;

    for (auto s : theme::allSchemes)
        themes.addItem (theme::schemeName (s) + (s == theme::ColourScheme::whynot ? " (padrão)"_t : juce::String()), true,
                        s == theme::currentScheme(), [useScheme, s] { useScheme (s); });

    menu.addSubMenu ("Tema de cores", themes);
    menu.addSeparator();
    menu.addItem ("Sobre a WHYNOT LAB...", [this] { showAbout(); });
    menu.addItem ("Sair", [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (260));
}

void MainComponent::showAbout()
{
    // Créditos que as licenças pedem (docs/licencas.md). O do piano é da CC BY 3.0 do Salamander.
    const auto text = juce::String::fromUTF8 (
        "WHYNOT LAB, a DAW da WHYNOT.\n\n"
        "Feita com o JUCE (AGPLv3) e o Tracktion Engine (GPLv3 ou posterior). "
        "MP3 com o LAME (LGPL). ASIO pelo SDK da Steinberg (GPLv3). "
        "Esticar o áudio no tempo com o signalsmith-stretch (Signalsmith Audio, MIT).\n\n"
        "WHYNOT Piano: Salamander Grand Piano V3, gravado por Alexander Holm, licença CC BY 3.0 "
        "(creativecommons.org/licenses/by/3.0). Modificado para a WHYNOT LAB: 8 das 16 camadas de força, "
        "sem o ruído do pedal. Origem: freepats.zenvoid.org.");

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::NoIcon)
                                      .withTitle ("Sobre a WHYNOT LAB")
                                      .withMessage (text)
                                      .withButton ("Fechar")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

void MainComponent::askHeadphones()
{
    // O primeiro botão devolve 1 (Enter); o segundo e o Esc devolvem 0, e o Esc fica no lado seguro (nas caixas).
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Está de fone?"_t)
                                      .withMessage ("A trilha armada toca a sua voz de volta, com os efeitos dela: de fone, isso ajuda a cantar "
                                                    "no tempo. Nas caixas do computador, a voz volta pelo alto-falante e vira microfonia, então "
                                                    "a escuta fica desligada. Dá para mudar no botão direito do armar da trilha."_t)
                                      .withButton ("Sim, estou de fone"_t)
                                      .withButton ("Não, estou nas caixas"_t)
                                      .withAssociatedComponent (this),
                                  [safe] (int result)
                                  {
                                      if (safe != nullptr)
                                          safe->app.run ("record.setHeadphones", args ({ { "answer", result == 1 ? "yes" : "no" } }));
                                  });
}

void MainComponent::saveNewVersion()
{
    // como o FL: projeto nunca salvo pergunta onde salvar; senão grava "Nome_2" ao lado
    if (app.getModel().file == juce::File())
    {
        saveProject (true);
        return;
    }

    const auto result = app.run ("project.saveNewVersion");
    showStatus (result.ok ? "Versão nova salva: "_t + juce::File (result.value["path"].toString()).getFileName() : result.error, ! result.ok);
}

void MainComponent::showExport (ExportDialog::Format format)
{
    ExportDialog::show (app, format, this, [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::String& m, bool e)
    {
        if (safe != nullptr)
            safe->showStatus (m, e);
    });
}

void MainComponent::offerRecovery()
{
    const auto recovery = app.takeRecovery();

    if (recovery.autosave == juce::File())
        return;

    const auto name = recovery.original != juce::File() ? recovery.original.getFileNameWithoutExtension() : "Sem título"_t;
    const auto text = "A WHYNOT LAB fechou sem salvar na última vez. Há um autosave de \""_t + name + "\" de "
                      + recovery.when.formatted ("%d/%m %H:%M") + ".\n\nAbrir o autosave agora? Se escolher \"Agora não\", ele continua guardado:"_t
                      + " abra depois em Arquivo > Abrir um autosave."_t;
    juce::Component::SafePointer<MainComponent> safe (this);

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::NoIcon, "Recuperar projeto", text,
                                        "Abrir o autosave", "Agora não"_t, this,
                                        juce::ModalCallbackFunction::create ([safe, recovery] (int choice)
                                        {
                                            if (safe == nullptr)
                                                return;

                                            safe->grabKeyboardFocus();   // o aviso fechou: os atalhos voltam para cá

                                            if (choice != 1)
                                                return;

                                            const auto result = safe->app.recoverProject (recovery);
                                            safe->showStatus (result.wasOk() ? "Autosave aberto. Salve (Ctrl+S) para ele voltar a ser o projeto."_t
                                                                             : result.getErrorMessage(),
                                                              result.failed());
                                        }));
}

void MainComponent::newProject()
{
    if (auto* w = findParentComponentOfClass<MainWindow>(); w != nullptr && ! w->confirmDiscardChanges())
        return;

    runCommand ("project.new");
    showStatus ("Projeto novo com o kit WHYNOT", false);
}

void MainComponent::openProject()
{
    if (auto* w = findParentComponentOfClass<MainWindow>(); w != nullptr && ! w->confirmDiscardChanges())
        return;

    chooser = std::make_unique<juce::FileChooser> ("Abrir projeto", app.getDefaultProjectsFolder(), "*.wnl");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (file == juce::File())
                                  return;

                              const auto result = app.run ("project.open", args ({ { "path", file.getFullPathName() } }));
                              showStatus (result.ok ? "Aberto: " + file.getFileName() : result.error, ! result.ok);
                          });
}

void MainComponent::openAutosave()
{
    // a recuperação depois de uma queda só aparece uma vez; por aqui o autosave fica sempre à mão (revisão de iniciante, 22/10)
    if (auto* w = findParentComponentOfClass<MainWindow>(); w != nullptr && ! w->confirmDiscardChanges())
        return;

    chooser = std::make_unique<juce::FileChooser> ("Abrir um autosave", app.autosaveBrowseFolder(), "*.wnl");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (file == juce::File())
                                  return;

                              const auto result = app.openAutosave (file);
                              showStatus (result.wasOk() ? "Autosave aberto. Salve (Ctrl+S) para guardar no projeto."_t : result.getErrorMessage(),
                                          result.failed());
                          });
}

void MainComponent::openProjectFile (const juce::File& file)
{
    if (auto* w = findParentComponentOfClass<MainWindow>(); w != nullptr && ! w->confirmDiscardChanges())
        return;

    const auto result = app.run ("project.open", args ({ { "path", file.getFullPathName() } }));
    showStatus (result.ok ? "Aberto: " + file.getFileName() : result.error, ! result.ok);

    // aberto para trabalhar: sai da lista para a música (ou o rack, se ainda não há arranjo)
    if (result.ok)
        showView (app.getModel().get().playlistItems.empty() ? View::channelRack : View::playlist);
}

bool MainComponent::saveProject (bool asNew)
{
    app.capturePluginStates();   // o estado dos plugins abertos entra no projeto antes de salvar

    if (! asNew && app.getModel().file != juce::File())
    {
        const auto result = app.run ("project.save");
        showStatus (result.ok ? "Salvo" : result.error, ! result.ok);
        return result.ok;
    }

    // Cada projeto ganha uma pasta própria com o mesmo nome (samples e renders ficam juntos).
    const auto folder = app.getDefaultProjectsFolder();
    folder.createDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Salvar projeto", folder.getChildFile ("Meu beat.wnl"), "*.wnl");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();

                              if (file == juce::File())
                                  return;

                              // se a pasta escolhida não tem o nome do projeto, cria uma
                              if (file.getParentDirectory().getFileName() != file.getFileNameWithoutExtension())
                                  file = file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension()).getChildFile (file.getFileName());

                              const auto result = app.run ("project.save", args ({ { "path", file.withFileExtension ("wnl").getFullPathName() } }));
                              showStatus (result.ok ? "Salvo em " + file.getParentDirectory().getFullPathName() : result.error, ! result.ok);
                          });
    return false;
}

void MainComponent::showAudioSettings()
{
    juce::Component::SafePointer<MainComponent> safe (this);
    DeviceSettings::show (app, this,
                          [safe] { return safe != nullptr && safe->typingKeyboard; },
                          [safe] (bool on) { if (safe != nullptr) safe->setTypingKeyboard (on); });
}

} // namespace whynot
