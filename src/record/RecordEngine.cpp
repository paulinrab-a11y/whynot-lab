#include "RecordEngine.h"
#include "RecordingGuide.h"

namespace whynot
{

/** A entrada da placa para o gravador (thread de áudio).

    A posição de cada bloco é a do relógio do Tracktion, a mesma que ele usa para tocar
    (EditPlaybackContext::globalStreamTimeToEditTime). Como o relógio dele avança no callback dele, a
    leitura depende de quem roda antes; o record::BlockClock descobre a ordem quando a placa começa e
    acerta o começo do bloco nos dois casos (revisão do Synth, 16/10). */
struct RecordEngine::DeviceInput : juce::AudioIODeviceCallback
{
    DeviceInput (record::InputRecorder& r, te::DeviceManager& d) : recorder (r), devices (d) {}

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        const juce::ScopedNoDenormals noDenormals;   // o pico compara amostras da placa, que podem vir denormais

        // a saída daqui é somada à do Tracktion: fica em silêncio (a monitoração é a da placa)
        for (int i = 0; i < numOutputs; ++i)
            if (outputs[i] != nullptr)
                juce::FloatVectorOperations::clear (outputs[i], numSamples);

        // O relógio do Tracktion é um std::atomic<double> com a soma de bloco / taxa: no pior caso o erro
        // chega a ~0,01 amostra em uma hora, longe da meia amostra em que blocos seguidos discordariam.
        const double rate = sampleRate.load (std::memory_order_relaxed);

        if (restarted.exchange (false, std::memory_order_acquire))
            clock.restart();

        const auto engineNow = (int64_t) std::llround (devices.getCurrentStreamTime() * rate);
        recorder.pushInput (inputs, numInputs, numSamples, clock.blockStart (engineNow, numSamples));
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        // a placa (re)começou: o relógio do Tracktion volta a 0 e a ordem dos callbacks é medida de novo
        if (device != nullptr)
            sampleRate.store (device->getCurrentSampleRate(), std::memory_order_relaxed);

        restarts.fetch_add (1, std::memory_order_relaxed);
        restarted.store (true, std::memory_order_release);
    }

    void audioDeviceStopped() override {}

    record::InputRecorder& recorder;
    te::DeviceManager& devices;
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool> restarted { true };
    std::atomic<int> restarts { 0 };
    record::BlockClock clock;   // só a thread de áudio
};

/** As notas de um controlador MIDI ligado (thread do MIDI do aparelho). O Tracktion entrega cada
    mensagem já carimbada no relógio da placa, mais a latência de saída; a fila sem trava leva a nota
    para a thread de mensagem. Um aparelho por fila: um produtor só. */
struct RecordEngine::MidiTap : te::InputDeviceInstance::Consumer
{
    struct Event
    {
        double stamp;
        int key, velocity;
        bool on;
    };

    /** Um knob ou fader (CC) para o MIDI learn (bloco 4d). */
    struct Control
    {
        int channel, cc, value;
    };

    /** O último valor de cada canal e CC, com a marca de novo (valor + 128): a thread de mensagem travada um
        instante não perde o valor final de um knob girado (uma fila cheia perderia). */
    std::array<std::atomic<int>, 16 * 120> latest {};
    std::atomic<bool> anyControl { false };

    MidiTap (te::InputDeviceInstance& i, std::function<void()> wakeUp) : instance (&i), wake (std::move (wakeUp))   { i.addConsumer (this); }

    ~MidiTap() override
    {
        if (auto* i = instance.get())
            i->removeConsumer (this);
    }

    void handleIncomingMidiMessage (const juce::MidiMessage& m, te::MPESourceID) override
    {
        if (m.isNoteOnOrOff() && ! events.push ({ m.getTimeStamp(), m.getNoteNumber(), m.getVelocity(), m.isNoteOn() }))
            dropped.fetch_add (1, std::memory_order_relaxed);   // fila cheia: conta, para o aviso

        // o CC vai para o MIDI learn; os de modo (120 a 127: all notes off, reset...) não são knobs
        if (m.isController() && m.getControllerNumber() < 120 && m.getChannel() >= 1 && m.getChannel() <= 16)
        {
            latest[(size_t) ((m.getChannel() - 1) * 120 + m.getControllerNumber())].store (128 + m.getControllerValue(), std::memory_order_relaxed);
            anyControl.store (true, std::memory_order_release);

            if (wake)
                wake();
        }
    }

    juce::WeakReference<te::InputDeviceInstance> instance;
    std::function<void()> wake;   // fixo desde o construtor: a thread do MIDI só chama
    audio::SpscQueue<Event, 512> events;
    std::atomic<int> dropped { 0 };
};

/** Leva os CCs para a thread de mensagem logo que chegam (o service roda a cada 100 ms: lento para um knob). */
struct RecordEngine::ControlWake : juce::AsyncUpdater
{
    explicit ControlWake (RecordEngine& o) : owner (o) {}
    void handleAsyncUpdate() override   { owner.drainControls(); }
    RecordEngine& owner;
};

//==============================================================================
RecordEngine::RecordEngine (te::Engine& e, te::Edit& ed, const juce::File& folder, bool isHeadless)
    : engine (e), edit (ed), dataFolder (folder), headless (isHeadless)
{
    controlWake = std::make_unique<ControlWake> (*this);
    loadSettings();
    recorder.setMeteredInput (config.firstInput, config.stereo ? 2 : 1);

    if (! headless)
    {
        deviceInput = std::make_unique<DeviceInput> (recorder, engine.getDeviceManager());
        engine.getDeviceManager().deviceManager.addAudioCallback (deviceInput.get());
    }
}

RecordEngine::~RecordEngine()
{
    abort();
    midiTaps.clear();   // sai dos aparelhos MIDI antes do Edit
    controlWake = nullptr;

    if (deviceInput != nullptr)
        engine.getDeviceManager().deviceManager.removeAudioCallback (deviceInput.get());
}

//==============================================================================
void RecordEngine::loadSettings()
{
    const auto json = juce::JSON::parse (settingsFile());

    if (! json.isObject())
        return;

    config.adjustMs = juce::jlimit (-500.0, 500.0, (double) json.getProperty ("adjustMs", 0.0));
    config.firstInput = juce::jlimit (0, 63, (int) json.getProperty ("firstInput", 0));
    config.stereo = (bool) json.getProperty ("stereo", false);
    config.countInBars = juce::jlimit (0, 2, (int) json.getProperty ("countIn", 0));
    config.countInLength = juce::jlimit (1, 2, (int) json.getProperty ("countInLength", 1));
    config.lowLatency = (bool) json.getProperty ("lowLatency", false);
    config.mic = json.getProperty ("mic", juce::String()).toString();
    config.inputDevice = json.getProperty ("inputDevice", juce::String()).toString();
    config.headphones = juce::jlimit (0, 2, (int) json.getProperty ("headphones", 0));

    if (config.mic != "condenser" && config.mic != "dynamic" && config.mic != "usb")
        config.mic = {};
}

void RecordEngine::saveSettings() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("adjustMs", config.adjustMs);
    o->setProperty ("firstInput", config.firstInput);
    o->setProperty ("stereo", config.stereo);
    o->setProperty ("countIn", config.countInBars);
    o->setProperty ("countInLength", config.countInLength);
    o->setProperty ("lowLatency", config.lowLatency);
    o->setProperty ("mic", config.mic);
    o->setProperty ("inputDevice", config.inputDevice);
    o->setProperty ("headphones", config.headphones);
    settingsFile().getParentDirectory().createDirectory();
    settingsFile().replaceWithText (juce::JSON::toString (juce::var (o)));
}

void RecordEngine::setAdjustMs (double ms)
{
    config.adjustMs = juce::jlimit (-500.0, 500.0, ms);
    saveSettings();
}

void RecordEngine::setInput (int firstInput, bool stereo)
{
    config.firstInput = juce::jlimit (0, 63, firstInput);
    config.stereo = stereo;
    recorder.setMeteredInput (config.firstInput, config.stereo ? 2 : 1);   // o medidor passa a medir a entrada nova
    saveSettings();
}

void RecordEngine::setMic (const juce::String& type)
{
    config.mic = type == "condenser" || type == "dynamic" || type == "usb" ? type : juce::String();
    saveSettings();
}

//==============================================================================
// O microfone (fatia 8, revisão de iniciante de 22/10): no Windows Audio a entrada é um aparelho à parte da saída,
// e o app abria sempre o padrão do Windows. Com um microfone USB ou uma interface no Windows Audio, a voz saía
// pelo microfone do notebook.
juce::String RecordEngine::chosenInputName() const
{
    if (headless)
        return config.inputDevice.isNotEmpty() ? config.inputDevice : simulatedDevice;

    auto& dm = engine.getDeviceManager().deviceManager;
    auto* type = dm.getCurrentDeviceTypeObject();

    if (type == nullptr)
        return {};

    if (! type->hasSeparateInputsAndOutputs())
        return dm.getAudioDeviceSetup().outputDeviceName;   // ASIO: a entrada é a própria placa

    const auto names = type->getDeviceNames (true);

    if (config.inputDevice.isNotEmpty() && names.contains (config.inputDevice))
        return config.inputDevice;

    const int index = type->getDefaultDeviceIndex (true);
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

RecordEngine::InputDevices RecordEngine::inputDevices() const
{
    InputDevices d;

    if (headless)
    {
        d.separate = true;
        d.names.add ("Entrada simulada");

        if (simulatedDevice.isNotEmpty())
            d.names.addIfNotAlreadyThere (simulatedDevice);

        d.defaultName = d.names[0];
        d.current = chosenInputName().isNotEmpty() ? chosenInputName() : d.defaultName;
        return d;
    }

    if (auto* type = engine.getDeviceManager().deviceManager.getCurrentDeviceTypeObject())
    {
        d.separate = type->hasSeparateInputsAndOutputs();

        if (d.separate)
        {
            type->scanForDevices();   // um microfone USB ligado agora entra na lista
            d.names = type->getDeviceNames (true);
            const int index = type->getDefaultDeviceIndex (true);
            d.defaultName = juce::isPositiveAndBelow (index, d.names.size()) ? d.names[index] : juce::String();
        }
    }

    d.current = chosenInputName();
    return d;
}

juce::Result RecordEngine::setInputDevice (const juce::String& name)
{
    if (isRecording())
        return juce::Result::fail ("Pare a gravação antes de trocar o microfone."_t);

    config.inputDevice = name.trim();
    saveSettings();

    if (headless)
        return juce::Result::ok();

    // a entrada já aberta (uma trilha armada ou escutando) reabre no aparelho novo; fechada, abre nele ao armar
    auto& dm = engine.getDeviceManager().deviceManager;
    auto* type = dm.getCurrentDeviceTypeObject();
    const auto setup = dm.getAudioDeviceSetup();

    if (type == nullptr || ! type->hasSeparateInputsAndOutputs() || setup.inputDeviceName.isEmpty()
        || setup.inputDeviceName == chosenInputName())
        return juce::Result::ok();

    return ensureInputOpen();
}

bool RecordEngine::builtInInput() const
{
    return whynot::guide::surelyBuiltIn (chosenInputName().toStdString());
}

void RecordEngine::setHeadphones (int answer)
{
    config.headphones = juce::jlimit (0, 2, answer);
    headphoneAsked = false;   // sem resposta, pergunta de novo
    quietWarned = false;
    saveSettings();
}

juce::var RecordEngine::guide (const juce::String& deviceType, const juce::String& deviceName) const
{
    juce::String type = deviceType, name = deviceName;

    if (type.isEmpty() && name.isEmpty())
    {
        if (headless)
        {
            type = simulatedDriver;
            name = chosenInputName();
        }
        else
        {
            auto& dm = engine.getDeviceManager().deviceManager;
            type = dm.getCurrentAudioDeviceType();
            name = chosenInputName();

            if (name.isEmpty())
                name = dm.getAudioDeviceSetup().outputDeviceName;
        }
    }

    namespace g = whynot::guide;   // o nome do método esconde o do namespace
    const auto device = g::recognise (type.toStdString(), name.toStdString());
    const auto mic = config.mic == "condenser" ? g::Mic::condenserXlr : config.mic == "dynamic" ? g::Mic::dynamicXlr
                   : config.mic == "usb" ? g::Mic::usb : g::Mic::unknown;
    const auto advice = g::advise (device, mic, ! monitoring.isEmpty());

    auto o = new juce::DynamicObject();
    o->setProperty ("driver", type);
    o->setProperty ("device", name);
    o->setProperty ("kind", device.kind == g::Kind::usbInterface ? "interface"
                          : device.kind == g::Kind::usbMic ? "usbMic"
                          : device.kind == g::Kind::asioWrapper ? "asioWrapper"
                          : device.kind == g::Kind::builtIn ? "builtIn" : "unknown");
    o->setProperty ("directMonitor", device.directMonitor);
    o->setProperty ("phantom", device.phantom);
    o->setProperty ("mic", config.mic);
    o->setProperty ("needsPhantom", advice.needsPhantom);
    o->setProperty ("warnDirectMonitor", advice.warnDirectMonitor);
    o->setProperty ("preferAsio", advice.preferAsio);
    juce::Array<juce::var> steps;

    for (auto& step : advice.steps)
        steps.add (juce::String::fromUTF8 (step.c_str()));

    o->setProperty ("steps", steps);
    return juce::var (o);
}

void RecordEngine::setLowLatency (bool on)
{
    config.lowLatency = on;
    saveSettings();
}

void RecordEngine::setCountIn (int bars)
{
    config.countInBars = juce::jlimit (0, 2, bars);

    if (config.countInBars > 0)
        config.countInLength = config.countInBars;

    saveSettings();
}

void RecordEngine::simulateDevice (double rate, int inputLatency, int outputLatency, int graphLatency)
{
    simulated = { rate > 0.0 ? rate : 48000.0, juce::jmax (0, inputLatency), juce::jmax (0, outputLatency), juce::jmax (0, graphLatency) };
}

double RecordEngine::sampleRate() const
{
    if (headless)
        return simulated.sampleRate;

    const double rate = engine.getDeviceManager().getSampleRate();
    return rate > 0.0 ? rate : 48000.0;
}

record::Latency RecordEngine::latency() const
{
    record::Latency l;
    l.adjustMs = config.adjustMs;

    if (headless)
    {
        l.inputSamples = simulated.input;
        l.outputSamples = simulated.output;
        l.graphSamples = simulated.graph;
        return l;
    }

    if (auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
    {
        l.inputSamples = device->getInputLatencyInSamples();
        l.outputSamples = device->getOutputLatencyInSamples();
    }

    if (auto* epc = edit.getTransport().getCurrentPlaybackContext())
        l.graphSamples = epc->getLatencySamples();

    return l;
}

juce::Result RecordEngine::ensureInputOpen()
{
    if (headless)
        return juce::Result::ok();

    auto& dm = engine.getDeviceManager().deviceManager;
    auto* device = dm.getCurrentAudioDevice();

    if (device == nullptr)
        return juce::Result::fail ("A placa de som está fechada. Escolha uma em Áudio e MIDI (F10)."_t);

    const int needed = config.firstInput + (config.stereo ? 2 : 1);
    const auto active = device->getActiveInputChannels();
    bool open = true;

    for (int ch = config.firstInput; ch < needed; ++ch)
        open = open && active[ch];

    auto setup = dm.getAudioDeviceSetup();
    auto* type = dm.getCurrentDeviceTypeObject();
    const bool separate = type != nullptr && type->hasSeparateInputsAndOutputs();

    // no Windows Audio vale o microfone escolhido (fatia 8): aberta em outro aparelho, a entrada reabre
    if (open && (! separate || chosenInputName().isEmpty() || setup.inputDeviceName == chosenInputName()))
        return juce::Result::ok();

    if (type != nullptr)
    {
        if (! separate)
        {
            setup.inputDeviceName = setup.outputDeviceName;   // ASIO: entrada e saída são o mesmo aparelho
        }
        else
        {
            type->scanForDevices();
            const auto name = chosenInputName();

            if (name.isNotEmpty())
                setup.inputDeviceName = name;

            if (config.inputDevice.isNotEmpty() && name != config.inputDevice)
                setMessage ("O microfone escolhido ("_t + config.inputDevice + ") não está ligado: gravando pelo padrão do Windows ("_t
                            + name + ").");
        }
    }

    if (setup.inputDeviceName.isEmpty())
        return juce::Result::fail ("Não achei uma entrada de áudio nesta placa."_t);

    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, needed, true);

    const auto error = dm.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        return juce::Result::fail ("Não consegui abrir a entrada de áudio: "_t + error);

    auto* reopened = dm.getCurrentAudioDevice();
    bool nowOpen = reopened != nullptr;

    for (int ch = config.firstInput; nowOpen && ch < needed; ++ch)
        nowOpen = reopened->getActiveInputChannels()[ch];

    if (! nowOpen)
        return juce::Result::fail ("A placa não tem a entrada "_t + juce::String (config.firstInput + 1)
                                   + (config.stereo ? "-" + juce::String (config.firstInput + 2) : juce::String()) + ".");

    return juce::Result::ok();
}

//==============================================================================
te::InputDeviceInstance* RecordEngine::monitorInput (bool fixLayout)
{
    // A entrada do Tracktion que corresponde à escolhida (mono: o canal sozinho; estéreo: o par). Ele
    // agrupa as entradas da placa em pares por padrão: um microfone mono numa entrada de um par tocaria
    // só num lado, então o par vira mono quando preciso (e vice-versa).
    auto& dm = engine.getDeviceManager();
    const int first = config.firstInput;
    const int count = config.stereo ? 2 : 1;
    te::WaveInputDevice* match = nullptr;

    for (auto* device : dm.getWaveInputDevices())
    {
        const auto& channels = device->getChannels();

        if ((int) channels.size() == count && channels[0].indexInDevice == first)
            match = device;
    }

    if (match == nullptr && fixLayout)
    {
        for (auto* device : dm.getWaveInputDevices())
        {
            if (device->getChannels().containsDeviceChannel (first))
            {
                dm.setDeviceNumChannels (*device, (uint32_t) count);   // a lista muda: acha na próxima volta
                break;
            }
        }

        return nullptr;
    }

    if (match == nullptr)
        return nullptr;

    if (! match->isEnabled())
        dm.setDeviceEnabled (*match, true);

    for (auto* in : edit.getAllInputDevices())
        if (in != nullptr && &in->getInputDevice() == match)
            return in;

    return nullptr;
}

void RecordEngine::syncMonitoring (const Project& p, const std::function<te::AudioTrack* (const juce::String&)>& trackFor)
{
    juce::StringArray wanted;
    bool waiting = false;   // armada em "Quando armada", mas a escuta não vale agora
    const bool builtIn = builtInInput();

    for (auto& m : p.mixer)
    {
        if (! m.monitorsInput())
            continue;

        // "Quando armada" (o padrão) só escuta de fone (fatia 8): nas caixas, ou com a placa do computador, a voz
        // volta pelo alto-falante e vira microfonia, e no Windows Audio chega atrasada. "Sempre" é escolha da pessoa.
        if (m.monitor.isEmpty() && (config.headphones != 1 || builtIn))
        {
            waiting = true;
            continue;
        }

        wanted.add (m.id);
    }

    if (waiting && builtIn)
    {
        if (! quietWarned)
            setMessage ("A escuta pelo app ficou desligada: com a placa do computador, a voz volta atrasada e, nas caixas, "
                        "vira microfonia. Para ouvir a voz com os efeitos, use um fone numa interface ou num microfone USB."_t);

        quietWarned = true;
    }
    else if (waiting && config.headphones == 0)
    {
        if (! headphoneAsked)
            ++headphoneQuestions;   // a tela pergunta "Está de fone?"

        headphoneAsked = true;
    }
    else if (waiting && config.headphones == 2)
    {
        if (! quietWarned)
            setMessage ("A escuta pelo app ficou desligada: nas caixas, a voz volta pelo alto-falante e vira microfonia. "
                        "De fone? Botão direito no armar da trilha (o ponto), \"Estou de fone\"."_t);

        quietWarned = true;
    }

    if (! wanted.isEmpty() && monitoring.isEmpty())
    {
        // começou a monitorar: abre a entrada da placa uma vez (ela reinicia), como o armar já faz
        if (const auto opened = ensureInputOpen(); opened.failed())
            setMessage (opened.getErrorMessage());
        else if (! echoWarned)
            setMessage ("Monitorando a voz pelo app. Se a placa também devolve a voz (monitoração direta), desligue uma das duas: senão, eco."_t);

        echoWarned = true;
    }

    monitoring = wanted;

    if (headless)
        return;

    juce::Array<te::EditItemID> targets;

    for (auto& id : wanted)
        if (auto* track = trackFor (id))
            targets.add (track->itemID);

    auto* in = targets.isEmpty() ? nullptr : monitorInput (true);

    // A entrada sai das trilhas que não monitoram mais, e de qualquer outra entrada da placa (a escolha
    // da entrada pode ter mudado). Só as ligações feitas aqui: o app não liga entrada de áudio em outro lugar.
    for (auto* other : edit.getAllInputDevices())
    {
        if (other == nullptr || other->getInputDevice().getDeviceType() != te::InputDevice::waveDevice)
            continue;

        for (auto old : other->getTargets())
            if (other != in || ! targets.contains (old))
                (void) other->removeTarget (old, nullptr);
    }

    if (in == nullptr)
        return;   // nada monitora, ou a placa ainda não tem a entrada (fechada, ou a lista refazendo): tenta em 100 ms

    for (auto t : targets)
        if (! in->getTargets().contains (t))
            (void) in->setTarget (t, false, nullptr);

    if (in->getInputDevice().getMonitorMode() != te::InputDevice::MonitorMode::on)
        in->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::on);
}

juce::StringArray RecordEngine::inputNames() const
{
    if (headless)
        return { "Entrada 1", "Entrada 2" };

    if (auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        return device->getInputChannelNames();

    return {};
}

//==============================================================================
double RecordEngine::streamSecondsNow() const
{
    if (headless)
        return session.has_value() ? session->lastStreamSeconds : 0.0;

    return engine.getDeviceManager().getCurrentStreamTime();
}

void RecordEngine::trySync (Session& s)
{
    if (s.sync.has_value() || headless)
        return;

    auto* epc = edit.getTransport().getCurrentPlaybackContext();
    auto* playHead = epc != nullptr ? epc->getNodePlayHead() : nullptr;

    // O play do Tracktion é assíncrono: só mede depois que a agulha andou de verdade.
    if (playHead == nullptr || ! playHead->isPlaying())
        return;

    // A posição com a volta do loop: o R apertado com a música tocando depois de uma volta grava onde a
    // agulha está, não onde estaria sem o loop. Daqui em diante o take anda reto até a próxima volta,
    // que é o fim dele nesta passada (Start::passEnd).
    const double now = engine.getDeviceManager().getCurrentStreamTime();
    const double timeline = epc->globalStreamTimeToEditTime (now).inSeconds();
    s.sync = record::ClockSync { now * s.sampleRate, timeline, s.sampleRate };
    s.latency.graphSamples = epc->getLatencySamples();   // com o grafo montado para tocar
}

void RecordEngine::setSync (double streamSample, double timelineSeconds)
{
    if (session.has_value())
        session->sync = record::ClockSync { streamSample, timelineSeconds, session->sampleRate };
}

void RecordEngine::service (const juce::String& liveChannel)
{
    liveChannelId = liveChannel;
    syncMidiTaps();
    drainMidiTaps();   // fora da gravação, as notas recolhidas só são descartadas

    if (session.has_value())
        trySync (*session);
}

void RecordEngine::syncMidiTaps()
{
    if (headless)
        return;

    juce::Array<te::InputDeviceInstance*> physical;

    for (auto* in : edit.getAllInputDevices())
        if (in != nullptr && in->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
            physical.add (in);

    std::erase_if (midiTaps, [&physical] (const std::unique_ptr<MidiTap>& t)
                   { return t->instance.get() == nullptr || ! physical.contains (t->instance.get()); });

    for (auto* in : physical)
        if (std::none_of (midiTaps.begin(), midiTaps.end(), [in] (const std::unique_ptr<MidiTap>& t) { return t->instance.get() == in; }))
            midiTaps.push_back (std::make_unique<MidiTap> (*in, [wake = controlWake.get()] { wake->triggerAsyncUpdate(); }));
}

void RecordEngine::drainMidiTaps()
{
    // o carimbo do Tracktion soma a latência de saída ao relógio: volta para o relógio da placa
    const double rate = sampleRate();
    const double stampShift = latency().outputSamples / rate;

    const double now = onLiveNote ? streamSecondsNow() : 0.0;

    for (auto& tap : midiTaps)
    {
        for (MidiTap::Event e {}; tap->events.pop (e);)
        {
            if (session.has_value())
                noteEvent (liveChannelId, e.key, e.velocity, e.on, juce::jmax (0.0, e.stamp - stampShift));

            if (onLiveNote)   // o Capture MIDI guarda também fora da gravação
                onLiveNote (e.key, e.velocity, e.on, juce::jmax (0.0, (now - (e.stamp - stampShift)) * 1000.0));
        }

        if (const int dropped = tap->dropped.exchange (0, std::memory_order_relaxed); dropped > 0)
        {
            midiDroppedTotal += dropped;

            if (session.has_value())
                session->midiDropped += dropped;
        }
    }
}

void RecordEngine::drainControls()
{
    // numa rajada, só o último valor de cada CC: a tabela do tap guarda o mais novo, com a marca de novo
    std::vector<MidiTap::Control> latest;

    for (auto& tap : midiTaps)
    {
        if (! tap->anyControl.exchange (false, std::memory_order_acquire))
            continue;

        for (size_t i = 0; i < tap->latest.size(); ++i)
        {
            int v = tap->latest[i].load (std::memory_order_relaxed);

            // tira a marca só se ninguém escreveu de novo no meio (senão o mais novo fica para a próxima rodada)
            while (v >= 128 && ! tap->latest[i].compare_exchange_weak (v, v - 128, std::memory_order_relaxed)) {}

            if (v >= 128)
                latest.push_back ({ (int) i / 120 + 1, (int) i % 120, v - 128 });
        }
    }

    if (onLiveControl)
        for (auto& c : latest)
            onLiveControl (c.channel, c.cc, c.value);
}

int RecordEngine::deviceRestartCount() const noexcept
{
    return deviceInput != nullptr ? deviceInput->restarts.load (std::memory_order_relaxed) : 0;
}

//==============================================================================
juce::Result RecordEngine::begin (const Project& p, const juce::String& liveChannel, const juce::File& takeFolder, const Start& start)
{
    if (session.has_value())
        return juce::Result::fail ("Já está gravando."_t);

    liveChannelId = liveChannel;
    Session s;
    s.startSeconds = start.recordFrom;
    s.sampleRate = sampleRate();
    s.latency = latency();
    s.tempo = { p.bpm, ticksPerBeat };
    s.regionStart = start.onTheFly ? -record::noLimit : start.recordFrom;
    s.passEnd = start.passEnd;
    s.loopStart = start.loopStart;

    if (p.punchOn)
    {
        s.regionStart = juce::jmax (s.regionStart, ticksToSeconds (p.punchIn, p.bpm));
        s.regionEnd = ticksToSeconds (p.punchOut, p.bpm);
    }

    // MIDI: os canais armados; sem nada armado, o canal ao vivo (como no FL, que grava o canal escolhido)
    bool anythingArmed = false;

    for (auto& m : p.mixer)
    {
        if (m.armed)
        {
            s.audioTrack = m.id;
            anythingArmed = true;
        }
    }

    for (auto& c : p.channels)
    {
        if (c.armed && ! c.isAudio())
        {
            s.notes[c.id];
            anythingArmed = true;
        }
    }

    if (! anythingArmed)
        if (auto* live = p.findChannel (liveChannel); live != nullptr && ! live->isAudio())
            s.notes[live->id];

    if (auto* pattern = p.currentPatternPtr())
    {
        s.pattern = pattern->id;
        s.target.patternTicks = pattern->lengthTicks();

        if (p.songMode)
            for (auto& item : p.playlistItems)
                if (item.kind == "pattern" && item.ref == pattern->id)
                    s.target.blocks.push_back ({ item.start, item.length });
    }

    s.target.loopTicks = p.loop ? (p.songMode ? p.songEndTicks() : s.target.patternTicks) : 0;

    // Áudio: o take vai para um arquivo novo; o nome do canal segue o da trilha ("Voz take 2")
    juce::String audioProblem;

    if (auto* track = p.findMixer (s.audioTrack))
    {
        // O canal "Voz take 3" (o primeiro número que os canais do projeto não usam) e o arquivo "Voz_take03.wav"
        // (a trilha e o número, como os usuários do FL pedem). Se o arquivo já existe (o de um take desfeito, que o
        // refazer usa, ou o de outro projeto sem nome na mesma pasta), o novo ganha "(2)" e nada é sobrescrito.
        int n = 1;

        for (;; ++n)
        {
            const auto name = track->name + " take " + juce::String (n);

            if (std::none_of (p.channels.begin(), p.channels.end(), [&name] (const Channel& c) { return c.name == name; }))
            {
                s.takeName = name;
                break;
            }
        }

        auto result = ensureInputOpen();

        if (result.wasOk())
        {
            const auto base = juce::File::createLegalFileName (track->name.trim().replaceCharacter (' ', '_') + "_take" + juce::String (n).paddedLeft ('0', 2));
            result = recorder.start (takeFolder.getNonexistentChildFile (base, ".wav", true), s.sampleRate, config.firstInput, config.stereo ? 2 : 1);
        }

        s.audioRunning = result.wasOk();
        audioProblem = result.getErrorMessage();

        // No PAT com a playlist vazia não há para onde passar: grava o padrão como ele toca, e o
        // padrão vai para a playlist junto com o take (o app passa para o SONG ao guardar).
        s.patternToSong = s.audioRunning && ! p.songMode && p.playlistItems.empty() && p.currentPatternPtr() != nullptr;
    }

    if (! s.audioRunning && s.notes.empty())
    {
        setMessage (audioProblem.isNotEmpty() ? audioProblem
                                              : "Nada armado para gravar: para a voz, clique em Gravar voz (na playlist) ou arme uma "
                                                "trilha do mixer (o ponto); para notas, arme um canal."_t);
        return juce::Result::fail (message);
    }

    // Sem placa (autoteste): o relógio simulado começa em 0 junto com o play (o começo da contagem).
    if (headless)
        s.sync = record::ClockSync { 0.0, start.onTheFly ? start.recordFrom : start.playFrom, s.sampleRate };

    s.deviceRestarts = deviceRestartCount();

    session = std::move (s);
    message = audioProblem;   // só o MIDI grava: diz por que o áudio ficou de fora

    if (message.isNotEmpty())
        ++messageCount;

    return juce::Result::ok();
}

void RecordEngine::abort()
{
    if (! session.has_value())
        return;

    if (session->audioRunning)
        recorder.stop (! headless).file.deleteFile();

    session.reset();
}

juce::var RecordEngine::finish (const Project& p)
{
    if (! session.has_value())
        return {};

    drainMidiTaps();      // as últimas notas do controlador
    trySync (*session);   // gravação curta: o transporte ainda toca, dá para medir agora
    auto s = std::move (*session);
    session.reset();

    double stopStream = streamSecondsNow();
    message = {};
    juce::Array<juce::var> audio, midi;

    auto keepMessage = [this] (const juce::String& text) { if (message.isEmpty()) message = text; };

    if (s.audioRunning)
    {
        auto take = recorder.stop (! headless);

        if (headless && take.firstStreamSample >= 0)
            stopStream = juce::jmax (stopStream, s.lastStreamSeconds, (double) (take.firstStreamSample + take.numSamples) / s.sampleRate);

        if (take.error.isNotEmpty())
            keepMessage (take.error);

        // A placa reiniciou (troca de taxa ou de buffer) no meio: o WAV vai só até ali, no tempo certo.
        if (take.truncated || deviceRestartCount() != s.deviceRestarts)
            keepMessage ("A placa de som reiniciou durante a gravação: o take vai só até ali."_t);

        if (take.firstStreamSample < 0 || take.numSamples == 0)
        {
            take.file.deleteFile();
            keepMessage ("Não chegou áudio da placa: confira a entrada em Áudio e MIDI (F10)."_t);
        }
        else if (! s.sync.has_value())
        {
            take.file.deleteFile();
            keepMessage ("A gravação parou antes de o transporte começar a tocar."_t);
        }
        else
        {
            const double fileStart = s.sync->timelineAt ((double) take.firstStreamSample) - s.latency.audioSeconds (s.sampleRate);

            // em loop, cada volta vira um take no mesmo lugar (a volta que mal começou fica de fora)
            const bool looped = s.passEnd < record::noLimit && s.passEnd > s.loopStart;
            const auto passes = looped ? record::loopPasses (fileStart, take.seconds(), s.loopStart, s.passEnd, s.regionStart, s.regionEnd, s.tempo, ticksPerBeat)
                                       : std::vector<record::AudioPlacement>();
            const auto place = looped ? (passes.empty() ? record::AudioPlacement() : passes.front())
                                      : record::placeAudio (fileStart, take.seconds(), s.regionStart, s.regionEnd, s.tempo, ticksPerStep);

            if (! place.valid)
            {
                take.file.deleteFile();
                keepMessage ("O take ficou fora do punch, ou curto demais (menos de um passo)."_t);
            }
            else
            {
                auto o = new juce::DynamicObject();
                o->setProperty ("track", s.audioTrack);
                o->setProperty ("path", take.file.getFullPathName().replaceCharacter ('\\', '/'));
                o->setProperty ("name", s.takeName);
                o->setProperty ("start", place.startTick);
                o->setProperty ("length", place.lengthTicks);
                o->setProperty ("offsetSeconds", place.offsetSeconds);
                o->setProperty ("fileStartSeconds", fileStart);
                o->setProperty ("gaps", take.gaps);
                o->setProperty ("lostSamples", (juce::int64) take.lostSamples);

                if (looped)
                {
                    juce::Array<juce::var> list;

                    for (auto& pass : passes)
                    {
                        auto po = new juce::DynamicObject();
                        po->setProperty ("start", pass.startTick);
                        po->setProperty ("length", pass.lengthTicks);
                        po->setProperty ("offsetSeconds", pass.offsetSeconds);
                        list.add (juce::var (po));
                    }

                    o->setProperty ("passes", list);

                    if (passes.size() > 1)
                        keepMessage (juce::String ((int) passes.size()) + " takes gravados. Clique em \""_t + juce::String ((int) passes.size())
                                     + " takes\" no bloco para escolher o melhor de cada trecho."_t);
                }

                if (s.patternToSong)
                {
                    auto song = new juce::DynamicObject();
                    song->setProperty ("pattern", s.pattern);
                    song->setProperty ("length", record::patternBlockTicks (place.startTick + place.lengthTicks, s.target.patternTicks));
                    o->setProperty ("song", juce::var (song));
                    keepMessage ("Gravado no PAT: o padrão foi para a playlist junto com o take, e o modo passou para SONG."_t);
                }

                audio.add (juce::var (o));

                if (take.gaps > 0)
                    keepMessage ("A placa falhou "_t + juce::String (take.gaps) + " vez(es) durante o take; os buracos viraram silêncio."_t);
            }
        }
    }

    if (s.midiDropped > 0)
        keepMessage (juce::String (s.midiDropped) + " notas do controlador se perderam (chegaram rápido demais)."_t);

    const double midiShift = s.latency.midiSeconds (s.sampleRate);

    for (auto& [channel, events] : s.notes)
    {
        if (events.empty())
            continue;

        if (! s.sync.has_value())
        {
            keepMessage ("A gravação parou antes de o transporte começar a tocar."_t);
            continue;
        }

        std::vector<record::NoteEvent> onTimeline;

        for (auto e : events)
        {
            e.time = s.sync->timelineAt (e.time * s.sampleRate) - midiShift;
            onTimeline.push_back (e);
        }

        const double stopTime = s.sync->timelineAt (stopStream * s.sampleRate) - midiShift;
        const auto notes = record::notesForPattern (onTimeline, stopTime, s.regionStart, s.regionEnd, s.tempo, s.target);

        if (notes.empty())
        {
            keepMessage ("As notas tocadas ficaram fora do punch."_t);
            continue;
        }

        juce::Array<juce::var> list;

        for (auto& n : notes)
        {
            auto no = new juce::DynamicObject();
            no->setProperty ("t", n.start);
            no->setProperty ("len", n.length);
            no->setProperty ("key", n.key);
            no->setProperty ("vel", n.velocity);
            list.add (juce::var (no));
        }

        auto o = new juce::DynamicObject();
        o->setProperty ("channel", channel);
        o->setProperty ("pattern", s.pattern);
        o->setProperty ("notes", list);
        midi.add (juce::var (o));
    }

    juce::ignoreUnused (p);

    if (audio.isEmpty() && midi.isEmpty())
    {
        const bool notesArmed = std::any_of (p.channels.begin(), p.channels.end(), [] (const Channel& c) { return c.armed && ! c.isAudio(); });
        keepMessage (s.audioTrack.isEmpty() && ! notesArmed
                         ? "Nada foi gravado: nenhuma trilha estava armada para a voz. Para gravar voz, clique em Gravar voz, na playlist."_t
                         : "Nada foi gravado."_t);
        ++messageCount;
        return {};
    }

    if (message.isNotEmpty())
        ++messageCount;

    auto o = new juce::DynamicObject();
    o->setProperty ("audio", audio);
    o->setProperty ("midi", midi);
    return juce::var (o);
}

bool RecordEngine::noteEvent (const juce::String& channel, int key, int velocity, bool on, double streamSeconds)
{
    if (! session.has_value())
        return false;

    auto it = session->notes.find (channel);

    if (it == session->notes.end())
        return false;

    const double t = streamSeconds >= 0.0 ? streamSeconds : streamSecondsNow();
    it->second.push_back ({ t, juce::jlimit (0, 127, key), juce::jlimit (1, 127, velocity), on });
    session->lastStreamSeconds = juce::jmax (session->lastStreamSeconds, t);
    return true;
}

//==============================================================================
juce::var RecordEngine::describe (const Project& p) const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("recordMode", recordMode);
    o->setProperty ("recording", isRecording());
    o->setProperty ("seconds", recorder.writtenSeconds());
    o->setProperty ("countIn", config.countInBars);
    o->setProperty ("monitoring", juce::Array<juce::var> (monitoring.begin(), monitoring.size()));
    o->setProperty ("lowLatency", config.lowLatency);
    o->setProperty ("inputDevice", config.inputDevice);
    o->setProperty ("headphones", config.headphones == 1 ? "yes" : config.headphones == 2 ? "no" : "");

    if (isRecording())
        o->setProperty ("recordFrom", juce::roundToInt (secondsToTicks (session->startSeconds, p.bpm)));

    juce::Array<juce::var> tracks, channels;

    for (auto& m : p.mixer)
        if (m.armed)
            tracks.add (m.id);

    for (auto& c : p.channels)
        if (c.armed)
            channels.add (c.id);

    o->setProperty ("armedTracks", tracks);
    o->setProperty ("armedChannels", channels);

    auto punch = new juce::DynamicObject();
    punch->setProperty ("on", p.punchOn);
    punch->setProperty ("start", p.punchIn);
    punch->setProperty ("end", p.punchOut);
    o->setProperty ("punch", juce::var (punch));

    const double rate = sampleRate();
    const auto l = latency();
    auto ms = [rate] (int samples) { return std::round (100000.0 * samples / rate) / 100.0; };
    auto lat = new juce::DynamicObject();
    lat->setProperty ("sampleRate", rate);
    lat->setProperty ("inputMs", ms (l.inputSamples));
    lat->setProperty ("outputMs", ms (l.outputSamples));
    lat->setProperty ("graphMs", ms (l.graphSamples));
    lat->setProperty ("adjustMs", l.adjustMs);
    lat->setProperty ("audioMs", std::round (100000.0 * l.audioSeconds (rate)) / 100.0);
    lat->setProperty ("midiMs", std::round (100000.0 * l.midiSeconds (rate)) / 100.0);
    o->setProperty ("latency", juce::var (lat));

    auto in = new juce::DynamicObject();
    in->setProperty ("first", config.firstInput + 1);
    in->setProperty ("stereo", config.stereo);

    if (headless)
    {
        in->setProperty ("device", "simulada"_t);
        in->setProperty ("open", true);
    }
    else if (auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
    {
        const auto active = device->getActiveInputChannels();
        in->setProperty ("device", engine.getDeviceManager().deviceManager.getAudioDeviceSetup().inputDeviceName);
        in->setProperty ("available", device->getInputChannelNames().size());
        in->setProperty ("open", active[config.firstInput] && (! config.stereo || active[config.firstInput + 1]));
    }

    o->setProperty ("input", juce::var (in));
    o->setProperty ("midiDropped", midiDroppedTotal);
    o->setProperty ("message", message);
    return juce::var (o);
}

} // namespace whynot
