#include "DeviceSettings.h"
#include "Theme.h"
#include "../app/AppController.h"
#include "../record/RecordEngine.h"
#include "../record/RecordingGuide.h"

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

    constexpr int width = 580;

    juce::String decimal (double v)   { return juce::String (v, 1).replaceCharacter ('.', ','); }

    // Rótulos do seletor de áudio do JUCE (TRANS) em português; os nomes dos drivers ficam como o Windows chama.
    void installPortugueseLabels()
    {
        static bool done = false;

        if (std::exchange (done, true))
            return;

        const auto mappings = R"pt(language: Portuguese
"Audio device type:" = "Driver:"
"Output:" = "Saída:"
"Input:" = "Entrada:"
"Device:" = "Aparelho:"
"Test" = "Testar"
"Plays a test tone" = "Toca um tom de teste"
"Sample rate:" = "Taxa:"
"Audio buffer size:" = "Buffer:"
"Control Panel" = "Painel do driver"
"Opens the device's own control panel" = "Abre o painel de controle do próprio driver"
"Reset Device" = "Reiniciar aparelho"
"Active output channels:" = "Saídas ativas:"
"Active input channels:" = "Entradas ativas:"
"(no audio output channels found)" = "(nenhuma saída de áudio encontrada)"
"none" = "nenhum"
"Show advanced settings..." = "Mostrar opções avançadas..."
"Error when trying to open audio device!" = "Não consegui abrir o aparelho de áudio."
)pt"_t;
        juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (mappings, false));
    }

    /** A janela acompanha a altura dos ajustes até `limit`; daí para cima, ela rola. */
    class Scroller : public juce::Viewport
    {
    public:
        Scroller (std::unique_ptr<juce::Component> content, int maxHeight) : follower (*this), limit (maxHeight)
        {
            setScrollBarsShown (true, false);
            content->addComponentListener (&follower);
            setViewedComponent (content.release(), true);
            fit();
        }

        ~Scroller() override
        {
            if (auto* c = getViewedComponent())
                c->removeComponentListener (&follower);
        }

    private:
        // o Viewport já é um ComponentListener (do conteúdo): o nosso fica num membro à parte
        struct Follower : juce::ComponentListener
        {
            explicit Follower (Scroller& s) : owner (s) {}

            void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
            {
                if (wasResized)
                    owner.fit();
            }

            Scroller& owner;
        };

        void fit()
        {
            auto* c = getViewedComponent();
            const bool scrolls = c->getHeight() > limit;
            setSize (c->getWidth() + (scrolls ? getScrollBarThickness() : 0), juce::jmin (c->getHeight(), limit));
        }

        Follower follower;
        const int limit;
    };
}

DeviceSettings::DeviceSettings (AppController& a, std::function<bool()> isOn, std::function<void (bool)> set)
    : app (a), typingIsOn (std::move (isOn)), setTyping (std::move (set))
{
    // O seletor mostra só as saídas: a entrada da placa abre quando você arma uma trilha (RecordEngine::ensureInputOpen),
    // na entrada escolhida na seção Gravação. As entradas MIDI vêm do Tracktion, abaixo.
    installPortugueseLabels();
    audio = std::make_unique<juce::AudioDeviceSelectorComponent> (app.getEngine().getDeviceManager().deviceManager,
                                                                  0, 0, 2, 2, false, false, true, false);
    audio->setSize (width, 200);         // ele mesmo acerta a altura (muda com o driver: o ASIO tem mais linhas)
    audio->addComponentListener (this);
    addAndMakeVisible (*audio);

    // Gravação: tudo passa pelos comandos record.setInput e record.setLatency (ficam em gravacao.json)
    const bool canRecord = app.recorder() != nullptr;

    recordInput.setComponentID ("recordInput");
    theme::styleComboBox (recordInput);
    recordInput.setTooltip ("A entrada da placa que a trilha armada grava: uma (mono) ou um par (estéreo)"_t);
    recordInput.onChange = [this]
    {
        const int id = recordInput.getSelectedId();

        if (id <= 0)
            return;

        const auto result = app.run ("record.setInput", args ({ { "first", (id - 1) / 2 + 1 }, { "stereo", id % 2 == 0 } }));
        recordError = result.ok ? juce::String() : result.error;
        selectRecordInput();   // se o comando recusou (gravando), a lista volta ao que vale
        refreshRecordNote();
    };
    recordInput.setEnabled (canRecord);
    addAndMakeVisible (recordInput);

    // o microfone (fatia 8): no Windows Audio a entrada é um aparelho à parte da saída; no ASIO, a própria placa
    micDevice.setComponentID ("micDevice");
    theme::styleComboBox (micDevice);
    micDevice.setTooltip ("De qual aparelho a voz vem: o microfone USB, a interface ou o microfone do computador"_t);
    micDevice.onChange = [this]
    {
        auto* rec = app.recorder();
        const int id = micDevice.getSelectedId();

        if (rec == nullptr || id <= 0)
            return;

        const auto name = id == 1 ? juce::String() : micDevice.getItemText (micDevice.getSelectedItemIndex());
        const auto result = app.run ("record.setInputDevice", args ({ { "name", name } }));
        recordError = result.ok ? juce::String() : result.error;
        rebuildMicDevices();
        rebuildRecordInputs();   // outro aparelho, outras entradas
        refreshRecordNote();
        refreshGuide();
    };
    micDevice.setEnabled (canRecord);
    addAndMakeVisible (micDevice);

    recordAdjust.setComponentID ("recordAdjust");
    recordAdjust.setRange (-500.0, 500.0, 0.5);
    recordAdjust.textFromValueFunction = [] (double v) { return decimal (v) + " ms"; };
    recordAdjust.valueFromTextFunction = [] (const juce::String& text)
    {
        return text.replaceCharacter (',', '.').retainCharacters ("-+0123456789.").getDoubleValue();
    };
    recordAdjust.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 96, theme::metric::control);
    recordAdjust.setColour (juce::Slider::textBoxBackgroundColourId, theme::colour::raised);   // como a lista de cima
    recordAdjust.setColour (juce::Slider::textBoxOutlineColourId, theme::colour::line);

    if (canRecord)
        recordAdjust.setValue (app.recorder()->settings().adjustMs, juce::dontSendNotification);

    recordAdjust.updateText();   // "0,0 ms": o Slider não refaz o texto pelo textFromValueFunction se o valor não mudou

    recordAdjust.setTooltip ("Somado ao atraso que a placa informa. Se o take gravado sai atrasado em relação ao beat, "
                             "aumente; se sai adiantado, diminua"_t);
    recordAdjust.onValueChange = [this]
    {
        app.run ("record.setLatency", args ({ { "adjustMs", recordAdjust.getValue() } }));
        refreshRecordNote();
    };
    recordAdjust.setEnabled (canRecord);
    addAndMakeVisible (recordAdjust);

    recordNote.setComponentID ("recordNote");
    recordNote.setFont (theme::font (theme::type::small));
    recordNote.setColour (juce::Label::textColourId, theme::colour::textFaint);
    recordNote.setJustificationType (juce::Justification::topLeft);
    recordNote.setBorderSize ({});
    recordNote.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (recordNote);

    rebuildMicDevices();
    rebuildRecordInputs();
    refreshRecordNote();

    // O assistente: o tipo de microfone fica no gravacao.json (record.setMic); os passos saem do record.guide
    micType.setComponentID ("micType");
    theme::styleComboBox (micType);
    micType.addItem ("Não sei"_t, 1);
    micType.addItem ("Condensador com cabo XLR (precisa de 48V)"_t, 2);
    micType.addItem ("Dinâmico com cabo XLR"_t, 3);
    micType.addItem ("Microfone USB (ele é a própria placa)"_t, 4);
    micType.setTooltip ("O tipo do microfone: o assistente diz os ajustes para ele"_t);

    if (canRecord)
    {
        const auto& mic = app.recorder()->settings().mic;
        micType.setSelectedId (mic == "condenser" ? 2 : mic == "dynamic" ? 3 : mic == "usb" ? 4 : 1, juce::dontSendNotification);
    }

    micType.onChange = [this]
    {
        const int id = micType.getSelectedId();
        app.run ("record.setMic", args ({ { "type", id == 2 ? "condenser" : id == 3 ? "dynamic" : id == 4 ? "usb" : "" } }));
        refreshGuide();
    };
    micType.setEnabled (canRecord);
    addAndMakeVisible (micType);

    guideText.setComponentID ("guideText");
    guideText.setFont (theme::font (theme::type::small));
    guideText.setColour (juce::Label::textColourId, theme::colour::textDim);
    guideText.setJustificationType (juce::Justification::topLeft);
    guideText.setBorderSize ({});
    guideText.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (guideText);

    applyGuide.setTooltip ("Grava pela entrada 1 da placa, em mono (a voz). O driver, o buffer e o 48V ficam com você."_t);
    applyGuide.onClick = [this]
    {
        app.run ("record.setInput", args ({ { "first", 1 }, { "stereo", false } }));
        selectRecordInput();
        refreshRecordNote();
        refreshGuide();
    };
    applyGuide.setEnabled (canRecord);
    addAndMakeVisible (applyGuide);

    refreshGuide();
    startTimerHz (30);

    rescanButton.setTooltip ("Procura controladores MIDI de novo (depois de conectar um pelo USB)"_t);
    rescanButton.onClick = [this]
    {
        app.getEngine().getDeviceManager().rescanMidiDeviceList();
        rebuildMidiList();
    };
    addAndMakeVisible (rescanButton);

    typing.setToggleState (typingIsOn && typingIsOn(), juce::dontSendNotification);
    typing.onClick = [this] { if (setTyping) setTyping (typing.getToggleState()); };
    addAndMakeVisible (typing);

    app.getEngine().getDeviceManager().addChangeListener (this);
    app.getEngine().getDeviceManager().deviceManager.addChangeListener (this);   // placa trocada: entradas e latência mudam
    rebuildMidiList();
    updateSize();   // sem controlador a lista não muda: o tamanho tem de sair daqui (o diálogo centra por ele)
}

DeviceSettings::~DeviceSettings()
{
    stopTimer();
    audio->removeComponentListener (this);
    app.getEngine().getDeviceManager().deviceManager.removeChangeListener (this);
    app.getEngine().getDeviceManager().removeChangeListener (this);
}

void DeviceSettings::componentMovedOrResized (juce::Component&, bool, bool wasResized)
{
    if (wasResized)
        updateSize();
}

int DeviceSettings::recordSectionHeight() const
{
    // a entrada, o ajuste, a nota, e o assistente: o microfone, a placa e os passos, e o medidor
    return 48 + 3 * recordRow + 60 + 16 + recordRow + guideHeight + 36 + 16;
}

int DeviceSettings::midiSectionHeight() const
{
    return 48 + juce::jmax (1, (int) midiToggles.size()) * rowHeight + 16 + 48 + rowHeight + 54 + 16;
}

void DeviceSettings::updateSize()
{
    setSize (width, audio->getHeight() + 8 + recordSectionHeight() + midiSectionHeight());
    resized();
    repaint();
}

std::unique_ptr<juce::Component> DeviceSettings::fitToHeight (std::unique_ptr<DeviceSettings> settings, int maxHeight)
{
    return std::make_unique<Scroller> (std::move (settings), juce::jmax (240, maxHeight));
}

void DeviceSettings::show (AppController& app, juce::Component* parent, std::function<bool()> isOn, std::function<void (bool)> set)
{
    // A janela cresce com o driver e os controladores: numa tela baixa ela rola em vez de passar da borda
    int maxHeight = 900;
    const auto& displays = juce::Desktop::getInstance().getDisplays();

    if (const auto* display = parent != nullptr ? displays.getDisplayForRect (parent->getScreenBounds()) : displays.getPrimaryDisplay())
        maxHeight = juce::roundToInt (display->userBounds.getHeight()) - 60;   // a barra de título e uma folga

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (fitToHeight (std::make_unique<DeviceSettings> (app, std::move (isOn), std::move (set)), maxHeight).release());
    o.dialogTitle = "Áudio e MIDI"_t;
    o.dialogBackgroundColour = theme::colour::panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.escapeKeyTriggersCloseButton = true;
    o.componentToCentreAround = parent;
    o.launchAsync();
}

void DeviceSettings::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildMidiList();       // controlador conectado ou desconectado
    rebuildMicDevices();     // outro driver: outros aparelhos de entrada
    rebuildRecordInputs();   // placa trocada: outras entradas e outra latência
    refreshRecordNote();
    refreshGuide();          // e outra placa para reconhecer
}

void DeviceSettings::refreshGuide()
{
    auto* rec = app.recorder();

    if (rec == nullptr)
        return;

    const auto g = rec->guide();
    const auto device = g["device"].toString();
    const auto kind = g["kind"].toString();
    juce::String text;

    // o nome que aparece é o que a placa informa (nunca um nome de produto nosso)
    if (device.isEmpty())
        text << "Placa: nenhuma aberta.\n"_t;
    else
        text << "Placa: " << device << (kind == "interface" ? " (interface de áudio USB"_t + (g["phantom"] ? juce::String (", com 48V"_t) : juce::String())
                                                                   + (g["directMonitor"] ? juce::String (" e monitoração direta"_t) : juce::String()) + ")"
                                     : kind == "usbMic" ? juce::String (" (microfone USB)")
                                     : kind == "asioWrapper" ? juce::String (" (ASIO genérico)"_t)
                                     : kind == "builtIn" ? juce::String (" (a placa do computador)") : juce::String (" (não reconhecida)"_t)) << ".\n";

    if (auto* steps = g["steps"].getArray())
        for (auto& step : *steps)
            text << "- " << step.toString() << "\n";

    guideText.setText (text.trimEnd(), juce::dontSendNotification);
    guideText.setColour (juce::Label::textColourId, (bool) g["warnDirectMonitor"] ? theme::colour::meterHigh : theme::colour::textDim);

    // A área acompanha o texto: a interface com o condensador e o aviso de eco quebra em umas 12 linhas, e o Label
    // cortaria o fim (o aviso de eco é o último). Mede na largura do texto (a da janela menos as margens) e sobra uma linha.
    // mede na fonte em que o Label vai sair (a do getLabelFont: o tema respeita o setFont desde o 0d202df)
    const auto measureFont = guideText.getLookAndFeel().getLabelFont (guideText);

    juce::AttributedString measured;
    measured.setText (guideText.getText());
    measured.setFont (measureFont);
    juce::TextLayout layout;
    layout.createLayout (measured, (float) (width - 2 * (theme::metric::pad + 4)));
    const int needed = juce::jmax (56, (int) std::ceil (layout.getHeight() + measureFont.getHeight()) + 8);

    if (needed != guideHeight)
    {
        guideHeight = needed;

        if (getWidth() > 0)   // no construtor, o updateSize do fim já usa a altura nova
            updateSize();
    }
}

void DeviceSettings::timerCallback()
{
    // o medidor com alvo: o pico da entrada escolhida (gravando ou não), com queda suave e o pico segurado 1,5 s
    auto* rec = app.recorder();

    if (rec == nullptr)
        return;

    const float peak = rec->takeInputPeak (1);
    const float db = peak > 1.0e-5f ? 20.0f * std::log10 (peak) : -100.0f;
    meterDb = juce::jmax (db, meterDb - 1.5f);   // ~45 dB por segundo de queda

    if (db >= meterHoldDb || --meterHoldTicks <= 0)
    {
        meterHoldDb = db >= meterHoldDb ? db : meterDb;
        meterHoldTicks = 45;
    }

    repaint (meterArea.expanded (0, 20));
}

void DeviceSettings::rebuildRecordInputs()
{
    recordInput.clear (juce::dontSendNotification);
    auto* rec = app.recorder();

    if (rec == nullptr)
        return;

    juce::StringArray names;

    if (auto* device = app.getEngine().getDeviceManager().deviceManager.getCurrentAudioDevice())
        names = device->getInputChannelNames();

    // O WASAPI só conta as entradas depois de abrir o aparelho de entrada, e ele abre quando você arma uma trilha:
    // até lá, a lista mostra as duas primeiras. Os nomes genéricos do driver ("Input channel 1") ficam de fora.
    const auto& s = rec->settings();
    const int count = juce::jlimit (2, 64, juce::jmax (names.size(), s.firstInput + (s.stereo ? 2 : 1)));

    auto nameOf = [&names] (int i)
    {
        const auto n = names[i].trim();
        return n.isEmpty() || n.startsWithIgnoreCase ("Input channel") ? juce::String() : " (" + n + ")";
    };

    recordInput.addSectionHeading ("Mono");

    for (int i = 0; i < count; ++i)
        recordInput.addItem ("Entrada " + juce::String (i + 1) + nameOf (i), 2 * i + 1);

    recordInput.addSectionHeading ("Estéreo"_t);

    for (int i = 0; i + 1 < count; ++i)
        recordInput.addItem ("Entradas " + juce::String (i + 1) + " e " + juce::String (i + 2), 2 * i + 2);

    selectRecordInput();
}

void DeviceSettings::rebuildMicDevices()
{
    micDevice.clear (juce::dontSendNotification);
    auto* rec = app.recorder();

    if (rec == nullptr)
        return;

    const auto d = rec->inputDevices();

    if (! d.separate)
    {
        micDevice.addItem ("A própria placa (no ASIO, a entrada vem dela)"_t, 1);
        micDevice.setSelectedId (1, juce::dontSendNotification);
        micDevice.setEnabled (false);
        return;
    }

    micDevice.addItem ("O padrão do Windows"_t + (d.defaultName.isNotEmpty() ? " (" + d.defaultName + ")" : juce::String()), 1);

    for (int i = 0; i < d.names.size(); ++i)
        micDevice.addItem (d.names[i], i + 2);

    const int chosen = d.names.indexOf (rec->settings().inputDevice);
    micDevice.setSelectedId (rec->settings().inputDevice.isEmpty() || chosen < 0 ? 1 : chosen + 2, juce::dontSendNotification);
    micDevice.setEnabled (! rec->isRecording());
}

void DeviceSettings::selectRecordInput()
{
    if (auto* rec = app.recorder())
        recordInput.setSelectedId (2 * rec->settings().firstInput + (rec->settings().stereo ? 2 : 1), juce::dontSendNotification);
}

void DeviceSettings::refreshRecordNote()
{
    auto* rec = app.recorder();

    if (rec == nullptr)
    {
        recordNote.setText ("A gravação não está disponível."_t, juce::dontSendNotification);
        return;
    }

    if (recordError.isNotEmpty())
    {
        recordNote.setText (recordError, juce::dontSendNotification);
        return;
    }

    const double rate = rec->sampleRate();
    const auto l = rec->latency();
    auto ofSamples = [rate] (int samples) { return decimal (samples * 1000.0 / rate); };

    juce::String text;
    text << "O take de áudio recua "_t << decimal (l.audioSeconds (rate) * 1000.0) << " ms: entrada " << ofSamples (l.inputSamples)
         << " + saída "_t << ofSamples (l.outputSamples) << " + motor " << ofSamples (l.graphSamples) << " + ajuste " << decimal (l.adjustMs) << ".\n"
         << "As notas MIDI recuam " << decimal (l.midiSeconds (rate) * 1000.0) << " ms.\n";

    const auto& s = rec->settings();
    auto* device = app.getEngine().getDeviceManager().deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        text << "A placa de som está fechada: escolha uma acima."_t;
    }
    else
    {
        const auto active = device->getActiveInputChannels();
        const bool open = active[s.firstInput] && (! s.stereo || active[s.firstInput + 1]);
        text << (open ? "A entrada está aberta."_t : "A entrada abre quando você arma uma trilha do mixer, no aparelho de entrada padrão do driver."_t);
    }

    recordNote.setText (text, juce::dontSendNotification);
}

void DeviceSettings::rebuildMidiList()
{
    const auto inputs = app.midiInputs();
    juce::StringArray ids;

    for (auto& in : inputs)
        ids.add (in["id"].toString());

    if (ids == midiIds && midiToggles.size() == (size_t) inputs.size())
    {
        for (int i = 0; i < inputs.size(); ++i)
            midiToggles[(size_t) i]->setToggleState ((bool) inputs[i]["enabled"], juce::dontSendNotification);

        return;
    }

    midiIds = ids;
    midiToggles.clear();

    for (auto& in : inputs)
    {
        auto t = std::make_unique<juce::ToggleButton> (in["name"].toString());
        t->setToggleState ((bool) in["enabled"], juce::dontSendNotification);
        const auto id = in["id"].toString();
        t->onClick = [this, id, raw = t.get()]
        {
            app.run ("midi.setInputEnabled", args ({ { "input", id }, { "on", raw->getToggleState() } }));
        };
        addAndMakeVisible (*t);
        midiToggles.push_back (std::move (t));
    }

    updateSize();
}

void DeviceSettings::resized()
{
    using namespace theme;
    auto r = getLocalBounds();
    audio->setBounds (r.removeFromTop (audio->getHeight()));
    r.removeFromTop (8);

    auto content = r.reduced (metric::pad + 4, 0);

    auto record = content.removeFromTop (recordSectionHeight());
    record.removeFromTop (48);   // título da gravação
    auto row = record.removeFromTop (recordRow).withTrimmedLeft (labelWidth);
    micDevice.setBounds (row.removeFromLeft (300).withSizeKeepingCentre (300, metric::control));
    row = record.removeFromTop (recordRow).withTrimmedLeft (labelWidth);
    recordInput.setBounds (row.removeFromLeft (300).withSizeKeepingCentre (300, metric::control));
    row = record.removeFromTop (recordRow).withTrimmedLeft (labelWidth);
    recordAdjust.setBounds (row.removeFromLeft (200).withSizeKeepingCentre (200, metric::control));
    recordNote.setBounds (record.removeFromTop (60).reduced (0, 4));   // três linhas: o take, as notas e a entrada
    record.removeFromTop (16);
    row = record.removeFromTop (recordRow).withTrimmedLeft (labelWidth);
    micType.setBounds (row.removeFromLeft (300).withSizeKeepingCentre (300, metric::control));
    guideText.setBounds (record.removeFromTop (guideHeight).reduced (0, 4));   // a placa e os passos
    auto meterRow = record.removeFromTop (36);
    applyGuide.setBounds (meterRow.removeFromRight (applyGuide.getIdealWidth (metric::control)).withSizeKeepingCentre (
        applyGuide.getIdealWidth (metric::control), metric::control));
    meterArea = meterRow.withTrimmedLeft (labelWidth).withTrimmedRight (16);
    meterArea = meterArea.withSizeKeepingCentre (meterArea.getWidth(), 10);

    auto header = content.removeFromTop (48);
    rescanButton.setBounds (header.removeFromRight (rescanButton.getIdealWidth (metric::control)).withSizeKeepingCentre (
        rescanButton.getIdealWidth (metric::control), metric::control));

    auto list = content.removeFromTop (juce::jmax (1, (int) midiToggles.size()) * rowHeight);

    for (auto& t : midiToggles)
        t->setBounds (list.removeFromTop (rowHeight));

    content.removeFromTop (16);
    content.removeFromTop (48);   // título do teclado
    typing.setBounds (content.removeFromTop (rowHeight));
}

void DeviceSettings::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    auto r = getLocalBounds().withTrimmedTop (audio->getHeight() + 8).reduced (metric::pad + 4, 0);

    auto title = [&g] (const juce::String& text, juce::Rectangle<int> area)
    {
        g.setColour (colour::text);
        g.setFont (font (type::title, Weight::semibold));
        g.drawText (text, area, juce::Justification::centredLeft);
    };

    auto label = [&g] (const juce::String& text, juce::Rectangle<int> area)
    {
        g.setColour (colour::textDim);
        g.setFont (font (type::body));
        g.drawText (text, area, juce::Justification::centredLeft);
    };

    g.setColour (colour::line);
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);

    auto record = r.removeFromTop (recordSectionHeight());
    title ("Gravação"_t, record.removeFromTop (48));
    label ("Microfone (aparelho)", record.removeFromTop (recordRow).withWidth (labelWidth));
    label ("Entrada", record.removeFromTop (recordRow).withWidth (labelWidth));
    label ("Ajuste da latência"_t, record.removeFromTop (recordRow).withWidth (labelWidth));
    record.removeFromTop (60 + 16);
    label ("Tipo de microfone", record.removeFromTop (recordRow).withWidth (labelWidth));
    record.removeFromTop (guideHeight);
    label ("Nível da voz"_t, record.removeFromTop (36).withWidth (labelWidth));

    // o medidor com alvo: a faixa verde é o pico bom da voz (-18 a -10 dBFS); acima de -1, estourou
    if (! meterArea.isEmpty())
    {
        const auto bar = meterArea.toFloat();
        auto xOf = [&bar] (float dbValue) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (dbValue + 60.0f) / 60.0f); };
        g.setColour (colour::background);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colour::meterLow.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (-18.0f), bar.getY(), xOf (-10.0f), bar.getBottom()));
        const auto level = guide::judge (meterDb);
        g.setColour (level == guide::Level::clip ? colour::meterHigh : level == guide::Level::good ? colour::meterLow : colour::textDim);
        g.fillRoundedRectangle (bar.withRight (xOf (meterDb)), 3.0f);
        g.setColour (guide::judge (meterHoldDb) == guide::Level::clip ? colour::meterHigh : colour::text);
        g.fillRect (xOf (meterHoldDb) - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);

        const auto held = guide::judge (meterHoldDb);
        g.setColour (held == guide::Level::clip ? colour::meterHigh : colour::textFaint);
        g.setFont (font (type::small));
        g.drawText (held == guide::Level::silent ? "Nada chega: a entrada, o 48V ou o cabo"_t
                    : held == guide::Level::low ? "Baixo: suba o ganho na placa"_t
                    : held == guide::Level::good ? "No alvo (pico de -18 a -10 dBFS)"_t
                    : held == guide::Level::hot ? "Alto: sobra pouco para o grito"_t
                                                : "Estourou: abaixe o ganho na placa (o fader não conserta)"_t,
                    meterArea.translated (0, 12).withHeight (16), juce::Justification::centredLeft, false);
    }

    g.setColour (colour::line);
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);

    title ("Controladores MIDI", r.removeFromTop (48));
    auto list = r.removeFromTop (juce::jmax (1, (int) midiToggles.size()) * rowHeight);

    if (midiToggles.empty())
    {
        g.setColour (colour::textDim);
        g.setFont (font (type::body));
        g.drawText ("Nenhum controlador conectado. Conecte pelo USB e clique em Procurar de novo."_t, list, juce::Justification::centredLeft, true);
    }

    r.removeFromTop (16);
    g.setColour (colour::line);
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
    title ("Teclado do computador", r.removeFromTop (48));
    r.removeFromTop (rowHeight);

    g.setColour (colour::textFaint);
    g.setFont (font (type::small));
    g.drawFittedText ("Notas: Z S X D C V G B H N J M (oitava de baixo) e Q 2 W 3 E R 5 T 6 Y 7 U (de cima). "
                      "O controlador e o teclado tocam o canal escolhido no channel rack."_t,
                      r.removeFromTop (54), juce::Justification::topLeft, 3, 1.0f);
}

} // namespace whynot
