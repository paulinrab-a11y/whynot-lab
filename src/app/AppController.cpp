#include "AppController.h"
#include "../dsp/Master.h"
#include "../engine/Exporter.h"
#include "../engine/KitGenerator.h"
#include "../engine/Mp3Encoder.h"
#include "../engine/PianoLibrary.h"
#include "../engine/PluginHost.h"
#include "../engine/WhynotFx.h"
#include "../engine/WhynotPiano.h"
#include "../engine/WhynotSampler.h"
#include "Favorites.h"
#include "../engine/UserMix.h"
#include "../engine/WhynotSynth.h"
#include "../library/ProjectLibrary.h"
#include "../model/WnlFormat.h"
#include "../record/RecordEngine.h"

namespace whynot
{

namespace
{
    struct WhynotEngineBehaviour : te::EngineBehaviour
    {
        WhynotEngineBehaviour (bool h, const PluginCatalog& c) : headless (h), catalog (c) {}

        bool autoInitialiseDeviceManager() override     { return ! headless; }
        bool shouldOpenAudioInputByDefault() override   { return false; }   // gravação chega na 3.4

        // Plugin bloqueado (travou no scan ou derrubou o app) não é aberto, nem dentro de um projeto.
        bool shouldLoadPlugin (te::ExternalPlugin& p) override   { return ! catalog.isBlocked (p.desc.fileOrIdentifier); }

        // Trilha muda não envia para os buses. O Tracktion continua processando uma trilha muda que dispara
        // um sidechain (o kick mudo ainda abaixa o 808); sem isto, o reverb dela continuaria saindo.
        bool shouldProcessAuxSendWhenTrackIsMuted (te::AuxSendPlugin&) override   { return false; }

        // O padrão do Tracktion (4 no master, 16 por trilha) não cabe 8 inserts, envios e medidores.
        te::EditLimits getEditLimits() override
        {
            te::EditLimits limits;
            limits.maxPluginsOnTrack = 64;      // volume, medidor, retorno, 10 inserts e muitos envios
            limits.maxNumMasterPlugins = 16;    // o fader, 10 inserts, o medidor e o LUFS do master
            return limits;
        }

        // Baixa latência ao gravar (gravar voz, 21/10): enquanto dura, os plugins desligados saem do grafo, e o
        // atraso deles some da escuta. Fora disso ficam no grafo (desligar e religar não remonta nada).
        bool shouldBypassedPluginsBeRemovedFromPlaybackGraph() override   { return removeBypassed; }

        const bool headless;
        const PluginCatalog& catalog;
        bool removeBypassed = false;
    };

    struct WhynotUIBehaviour : te::UIBehaviour
    {
        std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& state) override
        {
            if (auto* ws = dynamic_cast<te::Plugin::WindowState*> (&state); ws != nullptr && createWindow)
                return createWindow (ws->plugin);

            return {};
        }

        std::function<std::unique_ptr<juce::Component> (te::Plugin&)> createWindow;

        // O padrão do Tracktion não executa a tarefa; aqui ela roda até o fim.
        void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& job) override
        {
            while (job.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
            {}
        }
    };
}

namespace
{
    juce::File chooseDataFolder (const juce::File& requested)
    {
        return requested != juce::File() ? requested
                                         : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("WHYNOT LAB");
    }

    std::unique_ptr<PluginCatalog> openCatalog (const juce::File& folder)
    {
        auto c = std::make_unique<PluginCatalog> (folder.getChildFile ("plugins.xml"));
        c->load();
        return c;
    }
}

/** Scan numa thread de trabalho: cada plugin abre num processo filho; o resultado volta pela thread de mensagem. */
struct AppController::ScanThread : juce::Thread
{
    ScanThread (AppController& o, int timeout) : juce::Thread ("WHYNOT scan de plugins"), owner (o), timeoutMs (timeout), alive (o.alive) {}

    void run() override
    {
        auto progress = [this] (int done, int total, const juce::String& name)
        {
            const auto text = name.isEmpty() ? juce::String() : "Escaneando "_t + juce::String (done + 1) + " de " + juce::String (total) + ": " + name;
            const double fraction = total > 0 ? done / (double) total : 0.0;
            juce::MessageManager::callAsync ([a = alive, &o = owner, text, fraction]
            {
                if (*a && o.onScanProgress && text.isNotEmpty())
                    o.onScanProgress (text, fraction);
            });
        };

        const auto report = owner.catalog->scan (timeoutMs, progress, [this] { return threadShouldExit(); });

        juce::MessageManager::callAsync ([a = alive, &o = owner, report]
        {
            if (*a)
                o.finishScan (report);
        });
    }

    AppController& owner;
    const int timeoutMs;
    std::shared_ptr<bool> alive;
};

/** Tarefas de fundo na thread de mensagem: a prévia, as entradas MIDI ao vivo e o autosave. */
struct AppController::Housekeeping : juce::Timer
{
    explicit Housekeeping (AppController& o) : owner (o)   { startTimer (100); }

    void timerCallback() override
    {
        owner.preview.service();
        owner.routeLiveInputs();

        if (owner.recording != nullptr)
            owner.recording->service (owner.liveChannelId);   // relógio da placa x linha do tempo; notas do controlador

        owner.serviceTransport();   // a contagem passou do começo do loop: o loop liga
        owner.syncMonitoring();     // a monitoração pelo app: a placa pode ter mudado

        // desempenho (3.5): a carga da thread de áudio e as falhas de buffer que a placa e o JUCE contam
        auto& devices = owner.engine.getDeviceManager();
        owner.performanceMonitor.update (juce::Time::getMillisecondCounterHiRes() * 0.001, devices.getCpuUsage(),
                                         devices.deviceManager.getCurrentAudioDevice() != nullptr ? devices.deviceManager.getXRunCount() : -1);

        // congelar de novo: a trilha que mudou toca ao vivo e o render sai 1 s depois da última mudança
        // (fluidez, 22/10); fora de um gesto, para não renderizar no meio de um arraste
        if (! owner.registry.isInGesture() && owner.projector->isRefreezeDue())
            owner.syncProjector();

        if (++ticks >= autosaveMinutes * 60 * 10)
        {
            ticks = 0;
            owner.autosaveNow();
        }

        // a cada 10 s: os samples que saíram do projeto (canal apagado, sample trocado) saem da memória
        if (++pruneTicks >= 100)
        {
            pruneTicks = 0;
            owner.cache.prune();
        }
    }

    AppController& owner;
    int ticks = 0, pruneTicks = 0;
};

//==============================================================================
/** Durante um gesto da tela (arrastar nota, bloco ou fader), cada passo do mouse é um comando, e
    remontar o motor a cada um deixava o arraste aos trancos (pedido do usuário, 21/10). No gesto,
    a remontagem fica para no máximo a cada 30 ms; fora dele, e logo depois dele, é na hora. */
struct AppController::DeferredSync : juce::Timer
{
    explicit DeferredSync (AppController& o) : owner (o) {}

    void timerCallback() override
    {
        if (owner.syncPending)
            owner.syncProjector();

        if (! owner.registry.isInGesture())
            stopTimer();
    }

    AppController& owner;
};

//==============================================================================
/** Exportação em segundo plano: uma fila de etapas, uma por arquivo. Cada render é preparado aqui,
    na thread de mensagem (o Edit do render nasce e morre aqui), e o laço do render roda numa thread
    de trabalho, junto com o que vem depois dele (o limitador e o LAME do MP3). Um Timer acompanha. */
class AppController::ExportRunner : private juce::Timer
{
public:
    struct Step
    {
        juce::String label;
        std::function<std::unique_ptr<RenderJob> (juce::Result&)> prepare;   // thread de mensagem
        std::function<juce::Result (const std::function<bool()>& shouldStop)> after;   // thread de trabalho, depois do render
        juce::String file;                                                   // o que a etapa grava
    };

    ExportRunner (juce::String k, std::vector<Step> s, std::function<void()> cleanupFn)
        : kind (std::move (k)), steps (std::move (s)), cleanup (std::move (cleanupFn)) {}

    ~ExportRunner() override
    {
        // Fechar o app no meio: a thread do render pode estar esperando a thread de mensagem (o
        // Tracktion monta e desmonta o grafo nela), e esta aqui vai esperar a outra no join. O aviso de
        // saída faz essa espera desistir; o grafo que sobrou sai no job = nullptr, já aqui.
        stopTimer();
        cancel();
        worker.join (true);
        job = nullptr;

        if (cleanup)
            cleanup();
    }

    void start()
    {
        startTimerHz (20);
        next();
    }

    void cancel()
    {
        cancelled = true;

        if (job != nullptr)
            job->cancel();
    }

    bool isRunning() const noexcept   { return ! finished; }

    juce::var status() const
    {
        auto o = new juce::DynamicObject();
        const int n = juce::jmax (1, (int) steps.size());
        const float inStep = job != nullptr ? job->getProgress() : 0.0f;
        const float done = finished ? (ok ? 1.0f : progressAtEnd) : ((float) index + juce::jlimit (0.0f, 1.0f, inStep)) / (float) n;

        o->setProperty ("kind", kind);
        o->setProperty ("running", ! finished);
        o->setProperty ("progress", juce::jlimit (0.0, 1.0, (double) done));
        o->setProperty ("step", juce::jmin ((int) index + 1, n));
        o->setProperty ("steps", n);
        o->setProperty ("stage", index < steps.size() ? steps[index].label : juce::String());
        o->setProperty ("finished", finished);
        o->setProperty ("ok", finished && ok);
        o->setProperty ("cancelled", finished && cancelled.load());
        o->setProperty ("message", message);
        o->setProperty ("warning", warnings.joinIntoString (" "));   // deu certo, mas com algo a avisar (o WAV clipou)

        juce::Array<juce::var> files;
        for (size_t i = 0; i < steps.size() && i < index; ++i)
            if (steps[i].file.isNotEmpty())
                files.add (steps[i].file);
        o->setProperty ("files", files);
        return juce::var (o);
    }

private:
    void next()
    {
        if (cancelled)
            return finish (juce::Result::fail ("Exportação cancelada."_t));

        if (index >= steps.size())
            return finish (juce::Result::ok());

        auto error = juce::Result::ok();
        job = steps[index].prepare (error);

        if (job == nullptr)
            return finish (error.failed() ? error : juce::Result::fail ("Não consegui preparar o render."_t));

        workerDone = false;
        worker.start ([this]
        {
            auto r = job->run();

            if (r.wasOk())
                stepWarning = job->getWarning();   // lido pela thread de mensagem só depois do workerDone

            if (r.wasOk() && ! cancelled && steps[index].after)
                r = steps[index].after ([this] { return cancelled.load(); });

            workerResult = r;
            workerDone = true;
        });
    }

    void timerCallback() override
    {
        if (finished || ! workerDone)
            return;

        worker.join (false);
        job = nullptr;   // o Edit do render sai na thread de mensagem

        if (stepWarning.isNotEmpty())
            warnings.addIfNotAlreadyThere (stepWarning);

        stepWarning = {};

        if (cancelled)
            return finish (juce::Result::fail ("Exportação cancelada."_t));

        if (workerResult.failed())
            return finish (workerResult);

        ++index;
        next();
    }

    void finish (const juce::Result& r)
    {
        stopTimer();
        progressAtEnd = ((float) index) / (float) juce::jmax (1, (int) steps.size());
        finished = true;
        ok = r.wasOk();
        message = r.wasOk() ? juce::String() : r.getErrorMessage();

        if (cleanup)
            cleanup();

        cleanup = nullptr;
    }

    const juce::String kind;
    std::vector<Step> steps;
    std::function<void()> cleanup;
    size_t index = 0;
    std::unique_ptr<RenderJob> job;
    RenderThread worker;   // o join (true) interrompe as esperas do Tracktion pela thread de mensagem
    std::atomic<bool> workerDone { false }, cancelled { false };
    juce::Result workerResult = juce::Result::ok();
    bool finished = false, ok = false;
    float progressAtEnd = 0.0f;
    juce::String message;
    juce::String stepWarning;    // escrito pela thread do render antes do workerDone
    juce::StringArray warnings;  // thread de mensagem
};

AppController::AppController (bool isHeadless, const juce::File& requestedDataFolder)
    : dataFolder (chooseDataFolder (requestedDataFolder)),
      catalog (openCatalog (dataFolder)),
      engine (std::make_unique<te::PropertyStorage> ("WHYNOT LAB"),
              std::make_unique<WhynotUIBehaviour>(),
              std::make_unique<WhynotEngineBehaviour> (isHeadless, *catalog)),
      cache (engine.getAudioFileFormatManager().readFormatManager),
      registry (model, *this),
      headless (isHeadless),
      keys (dataFolder.getChildFile ("atalhos.json"))
{
    keys.load();

    engine.getPluginManager().createBuiltInType<WhynotSamplerPlugin>();
    engine.getPluginManager().createBuiltInType<WhynotSynthPlugin>();
    engine.getPluginManager().createBuiltInType<WhynotPianoPlugin>();
    WhynotFxPlugin::registerAll (engine);
    Favorites::get().setFolder (dataFolder);   // favoritos.json; migra o synth-favoritos.txt antigo
    piano::setDataFolder (dataFolder);
    usermix::setDataFolder (dataFolder);   // cadeias e presets do artista (Mixagem, 21/10)

    if (! headless)
        pianoPrefetch = std::make_unique<piano::Prefetch>();

    recoverFromPluginCrash();
    catalog->fillKnownList (engine.getPluginManager().knownPluginList);
    installPluginLoader();

    edit = te::Edit::createSingleTrackEdit (engine);
    projector = std::make_unique<Projector> (*edit, cache, Projector::Mode::followProject, 1, catalog.get());
    projector->setTransportLoopManaged (true);   // o loop do transporte é o applyTransportLoop (seleção de tempo e contagem)

    // A contagem toca a partir de um tempo negativo quando a agulha está no começo: o transporte do
    // Tracktion só aceita posições até "- contagem - 0,5 s" com o modo de contagem dele ligado.
    edit->setCountInMode (te::Edit::CountIn::twoBar);

    // Com janela, congelar de novo espera 1 s sem mudança (o render roda na thread da tela). Sem janela (autoteste,
    // modo da IA) continua na hora, no próprio comando.
    if (! headless)
        projector->setRefreezeDelay (1.0);

    if (! headless)
        edit->getTransport().ensureContextAllocated();

    recording = std::make_unique<RecordEngine> (engine, *edit, dataFolder, headless);

    // Capture MIDI (bloco 3): as notas do controlador também ficam guardadas, com a hora em que chegaram
    recording->onLiveNote = [this] (int key, int velocity, bool on, double ageMs)
    {
        rememberLiveNote (key, velocity, on, juce::Time::getMillisecondCounterHiRes() - juce::jmax (0.0, ageMs));
    };

    // tom do beat: uma thread de trabalho só para isso, também sem janela (o audio.detectKey da IA)
    beatKeys = std::make_unique<BeatKeyAnalyzer>();
    beatKeys->onAnalysed = [this] (const juce::File& f) { if (onBeatKey) onBeatKey (f); };

    registerCoreCommands (registry);
    registerCompositionCommands (registry);
    registerPluginCommands (registry);
    registerMixCommands (registry);
    registerExportCommands (registry);
    registerDeviceCommands (registry);
    registerIdeasCommands (registry);
    registerSynthCommands (registry);
    registerPianoCommands (registry);
    registerRecordCommands (registry);
    registerAutomationCommands (registry);
    registerPerformanceCommands (registry);
    registerSliceCommands (registry);
    registerBeatCommands (registry);
    registerLibraryCommands (registry);
    registerFavoritesCommands (registry);

    // Prévia do navegador de sons: depende do player daqui, então o comando mora aqui.
    registry.add ({ "browser.preview", "Ouvir prévia"_t,
                    params::schema ({ { "path", "string", true, "Arquivo de áudio (WAV, AIFF, FLAC, OGG, MP3); toca até 30 s"_t } }), false,
                    [this] (CommandContext&, const juce::var& a)
                    {
                        const juce::File file (params::string (a, "path"));

                        if (! file.existsAsFile())
                            return CommandResult::fail ("Arquivo não encontrado: "_t + file.getFullPathName());

                        if (previewFile (file))
                            return CommandResult::success();

                        return CommandResult::fail (headless ? "Sem placa de som no modo sem janela."_t
                                                             : "Não consegui abrir esse arquivo de áudio (ou a placa de som está fechada)."_t);
                    } });

    registry.add ({ "browser.stopPreview", "Parar prévia"_t, params::schema ({}), false,
                    [this] (CommandContext&, const juce::var&) { stopPreview(); return CommandResult::success(); } });

    model.addListener (this);
    newProject();

    if (! headless)
    {
        session = std::make_unique<Session> (dataFolder);
        engine.getDeviceManager().deviceManager.addAudioCallback (&preview);
        housekeeping = std::make_unique<Housekeeping> (*this);
    }

    // Tela Projetos (21/10, 3.3): as prévias renderizam num Edit próprio e o deck toca por fora do Edit,
    // como a prévia do navegador. Sem janela não há placa nem render a cada salvar.
    projectLibrary = std::make_unique<library::ProjectLibrary> (library::ProjectLibrary::Host {
        engine, cache, catalog.get(), dataFolder,
        [this] { return isPlaying(); },
        [this] { return isExportingInBackground(); },
        [this] { stop(); },
        headless ? nullptr : &engine.getDeviceManager().deviceManager });
    projectLibrary->renderOnSave = ! headless;

    if (! headless)
        projectLibrary->setRoots ({ getDefaultProjectsFolder() });
}

AppController::~AppController()
{
    beatKeys = nullptr;       // cancela a análise de tom em andamento e espera a thread sair
    projectLibrary = nullptr;   // cancela a prévia renderizando e tira o deck da placa, antes do motor
    exportRunner = nullptr;   // cancela e espera o render em andamento, antes de o motor sair
    deferredSync = nullptr;
    *alive = false;
    housekeeping = nullptr;

    if (! headless)
        engine.getDeviceManager().deviceManager.removeAudioCallback (&preview);

    if (scanThread != nullptr)
        scanThread->stopThread (5000);   // o scan mata o processo filho ao ser cancelado

    model.removeListener (this);
    recording = nullptr;   // descarta uma gravação em andamento e sai da placa antes do Edit
    edit->getTransport().stop (false, true);
    projector = nullptr;
    edit = nullptr;
}

juce::File AppController::getKitFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("WHYNOT LAB").getChildFile ("Kit");
}

juce::File AppController::getDefaultProjectsFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("WHYNOT LAB");
}

void AppController::projectChanged (const ProjectModel&)
{
    // A comparação A/B vale para o mixer de quando ligou: qualquer mudança nele a desliga.
    if (auditionOn && model.get().mixer != auditionMixer)
    {
        auditionOn = false;
        projector->setAudition (false, 0.0f);
    }

    ++editsSinceAutosave;

    if (registry.isInGesture())
    {
        syncPending = true;

        if (deferredSync == nullptr)
            deferredSync = std::make_unique<DeferredSync> (*this);

        if (! deferredSync->isTimerRunning())
            deferredSync->startTimer (30);

        return;
    }

    syncProjector();
}

void AppController::syncProjector()
{
    syncPending = false;
    projector->sync (model.get(), model.projectFolder());
    applyTransportLoop();
    syncMonitoring();
    requestBeatKeys();
}

void AppController::syncMonitoring()
{
    if (recording != nullptr)
        recording->syncMonitoring (model.get(), [this] (const juce::String& id) { return projector->mixerTrackFor (id); });
}

void AppController::applyLowLatency()
{
    // Baixa latência ao gravar: gravando com a monitoração, os efeitos com atraso das trilhas que
    // monitoram e do master saem do que se ouve. O take é seco de qualquer jeito. Voltam ao parar.
    std::set<juce::String> cut;
    juce::StringArray names;

    if (recording != nullptr && recording->isRecording() && recording->settings().lowLatency && ! recording->monitoredTracks().isEmpty())
    {
        auto addTrack = [&] (const juce::String& id)
        {
            if (const auto* m = model.get().findMixer (id))
            {
                for (auto& ins : m->inserts)
                {
                    if (auto* plugin = projector->insertPlugin (id, ins.id); plugin != nullptr && plugin->getLatencySeconds() > 0.0)
                    {
                        cut.insert (ins.id);
                        names.add (plugin->getName());
                    }
                }
            }
        };

        for (auto& id : recording->monitoredTracks())
            addTrack (id);

        addTrack (masterTrackId);
    }

    if (cut == projector->getLowLatencyOff())
        return;

    if (auto* behaviour = dynamic_cast<WhynotEngineBehaviour*> (&engine.getEngineBehaviour()))
        behaviour->removeBypassed = ! cut.empty();

    projector->setLowLatencyOff (cut);
    syncProjector();
    edit->restartPlayback();

    if (! cut.empty())
        recording->setMessage ("Baixa latência: fora da escuta enquanto grava: "_t + names.joinIntoString (", ") + ".");
}

void AppController::requestBeatKeys()
{
    // beat novo no projeto (canal de áudio, bloco na playlist ou sample longo): analisa sozinho, em segundo plano
    if (beatKeys == nullptr)
        return;

    for (auto& beat : beatKeys->beatsOf (model))
    {
        const auto path = beat.file.getFullPathName();

        if (! requestedBeats.contains (path))
        {
            requestedBeats.add (path);
            beatKeys->request (beat.file);
        }
    }
}

void AppController::finishBeatAnalysis (int timeoutMs)
{
    if (beatKeys != nullptr)
        beatKeys->finishAll (timeoutMs);
}

//==============================================================================
void AppController::play()
{
    auto& transport = edit->getTransport();
    const bool willRecord = recording != nullptr && recording->isRecordMode() && ! recording->isRecording();

    // Gravar áudio no PAT: o take é um clipe da playlist, que só toca no SONG. Com algo na playlist, o
    // app passa para o SONG antes (é um passo de desfazer à parte). Com a playlist vazia não há o que
    // tocar no SONG: grava o padrão como ele toca, e o padrão vai junto para a playlist ao guardar.
    const bool toSong = willRecord && ! model.get().songMode && ! model.get().playlistItems.empty()
                        && std::any_of (model.get().mixer.begin(), model.get().mixer.end(), [] (const MixerTrack& m) { return m.armed; });

    if (toSong)
    {
        auto on = new juce::DynamicObject();
        on->setProperty ("on", true);
        registry.run ("transport.setSongMode", juce::var (on));
    }

    // A agulha (ou a seleção de tempo) no SONG; o começo do padrão no PAT. Gravando com contagem, o
    // transporte começa N compassos antes, mais meio tempo: o clique sai só no primeiro tempo cheio.
    auto plan = startPlan (willRecord ? recording->settings().countInBars : 0);
    double halfBeat = plan.countIn > 0.0 ? 30.0 / juce::jmax (1.0, model.get().bpm) : 0.0;
    double transportStart = plan.playFrom - halfBeat;

    // REC ligado: grava junto. Se não der (nada armado, placa sem entrada), toca assim mesmo, sem a
    // contagem; o motivo fica em record.state e na barra de status.
    if (willRecord && beginRecording ({ plan.recordFrom, transportStart, plan.passEnd, transportLoopRange().getStart().inSeconds(), false }).failed())
    {
        plan = startPlan (0);
        halfBeat = 0.0;
        transportStart = plan.playFrom;
    }
    else if (toSong)
    {
        recording->setMessage ("Passei para o SONG: o take de áudio é um clipe da playlist."_t);   // depois do begin, que limpa
    }

    applyLowLatency();

    loopHoldUntil.reset();

    if (transportStart < transportLoopRange().getStart().inSeconds() - 1.0e-9)
        loopHoldUntil = transportLoopRange().getStart().inSeconds();

    applyTransportLoop (true);
    transport.setPosition (te::TimePosition::fromSeconds (transportStart));

    // Sem janela não há placa: o play do Tracktion montaria um contexto de reprodução (threads e grafo)
    // que nada consome, como o construtor já evita. A gravação simulada do autoteste começa igual.
    if (headless)
        return;

    transport.play (false);

    // O play do Tracktion limpa a faixa do clique: ela vale a partir daqui. Com o metrônomo desligado,
    // o clique toca só na contagem; o último tempo dela fica meio tempo antes do take.
    if (plan.countIn > 0.0 && recording != nullptr && recording->isRecording())
        edit->setClickTrackRange ({ te::TimePosition::fromSeconds (plan.playFrom - 0.001),
                                    te::TimePosition::fromSeconds (plan.recordFrom - halfBeat) });
}

void AppController::playFrom (int ticks)
{
    // a ferramenta Tocar da playlist: começa onde a pessoa clicou (a agulha fica onde estava)
    auto& transport = edit->getTransport();
    loopHoldUntil.reset();
    applyTransportLoop();
    transport.setPosition (te::TimePosition::fromSeconds (ticksToSeconds (juce::jmax (0, ticks), model.get().bpm)));

    if (! headless)
        transport.play (false);
}

void AppController::stop()
{
    finishRecording();   // antes de parar: com o transporte ainda tocando dá para medir o relógio, se faltava

    auto& transport = edit->getTransport();
    transport.stop (false, false);
    loopHoldUntil.reset();
    applyTransportLoop();

    // volta para onde o play começa: a seleção de tempo ou a agulha (no PAT, o começo do padrão), como o
    // FL volta para a marca de posição
    transport.setPosition (te::TimePosition::fromSeconds (startPlan (0).recordFrom));
}

void AppController::setPlayStart (int ticks)
{
    needleTicks = juce::jlimit (0, maxTimelineTicks, ticks);

    // parado, o transporte já fica na agulha (a posição na barra e o playhead mostram); tocando, pula para ela
    auto& transport = edit->getTransport();

    if (model.get().songMode || transport.isPlaying())
        transport.setPosition (te::TimePosition::fromSeconds (ticksToSeconds (needleTicks, model.get().bpm)));
}

int AppController::countInBeatsLeft() const
{
    if (recording == nullptr || ! recording->isRecording())
        return 0;

    // um número por clique: 4, 3, 2, 1 (o meio tempo antes do primeiro clique já mostra o 4)
    const auto& p = model.get();
    const double left = (recording->recordFromSeconds() - edit->getTransport().getPosition().inSeconds()) * p.bpm / 60.0;
    const int beats = recording->settings().countInBars * juce::jmax (1, p.timeSigNum);
    return left > 0.0 ? juce::jlimit (1, juce::jmax (1, beats), (int) std::ceil (left - 1.0e-9)) : 0;
}

void AppController::setTimeSelection (juce::Range<int> r)
{
    timeSelection = r;
    applyTransportLoop();   // no SONG, a seleção é o loop
}

te::TimeRange AppController::transportLoopRange() const
{
    const auto& p = model.get();
    const auto selection = songSelection();

    if (selection.isEmpty())
        return { te::TimePosition(), te::TimePosition::fromSeconds (projector->lengthSeconds()) };

    return { te::TimePosition::fromSeconds (ticksToSeconds (selection.getStart(), p.bpm)),
             te::TimePosition::fromSeconds (ticksToSeconds (selection.getEnd(), p.bpm)) };
}

juce::Range<int> AppController::songSelection() const
{
    return model.get().songMode && timeSelection.getLength() > 0 ? timeSelection : juce::Range<int>();
}

record::StartPlan AppController::startPlan (int countInBars) const
{
    const auto& p = model.get();
    const double barSeconds = ticksToSeconds (juce::jmax (1, p.timeSigNum) * ticksPerBar4 / juce::jmax (1, p.timeSigDen), p.bpm);

    // PAT: o padrão em loop, do começo; o take dura o que a pessoa gravar (o padrão vai junto para a playlist)
    if (! p.songMode)
        return record::planStart (0.0, 0.0, 0.0, record::noLimit, countInBars, barSeconds);

    // Gravando no SONG sem seleção, a música passa do fim em vez de voltar (applyTransportLoop), como
    // no FL: o take segue. Com seleção e o loop ligado, o take vale até a volta.
    const auto selection = songSelection();
    const double selStart = ticksToSeconds (selection.getStart(), p.bpm), selEnd = ticksToSeconds (selection.getEnd(), p.bpm);
    const double loopEnd = p.loop && ! selection.isEmpty() ? selEnd : record::noLimit;
    return record::planStart (ticksToSeconds (needleTicks, p.bpm), selStart, selEnd, loopEnd, countInBars, barSeconds);
}

void AppController::applyTransportLoop (bool starting)
{
    // O loop: a seleção de tempo no SONG, como no FL; sem ela, o padrão (PAT) ou a música inteira.
    auto& transport = edit->getTransport();
    const auto range = transportLoopRange();
    bool loopOn = model.get().loop;

    // gravando no SONG sem seleção de tempo: a música passa do fim, para o take seguir (como no FL)
    if (recording != nullptr && recording->isRecording() && model.get().songMode && songSelection().isEmpty())
        loopOn = false;

    // A contagem começa antes do loop: ligado, o Tracktion pularia para dentro dele. Liga quando a
    // agulha entrar no loop (serviceTransport, a cada 100 ms).
    if (loopHoldUntil.has_value())
    {
        if (starting || (transport.isPlaying() && transport.getPosition().inSeconds() < *loopHoldUntil))
            loopOn = false;
        else
            loopHoldUntil.reset();
    }

    if (transport.getLoopRange() != range)
        transport.setLoopRange (range);

    if (transport.looping.get() != loopOn)
        transport.looping = loopOn;
}

void AppController::serviceTransport()
{
    if (loopHoldUntil.has_value()
        && (! edit->getTransport().isPlaying() || edit->getTransport().getPosition().inSeconds() >= *loopHoldUntil))
        applyTransportLoop();
}

bool AppController::isPlaying() const
{
    return edit->getTransport().isPlaying();
}

void AppController::togglePause()
{
    // como o Ctrl+Espaço do FL: para onde está e continua dali
    auto& transport = edit->getTransport();

    if (transport.isPlaying())
    {
        finishRecording();
        transport.stop (false, false);
    }
    else
    {
        // continua de onde parou, sem contagem: o take vale daqui até a volta do loop
        if (recording != nullptr && recording->isRecordMode() && ! recording->isRecording())
        {
            const double from = transport.getPosition().inSeconds();
            const double loopEnd = transport.looping.get() && ! songSelection().isEmpty() ? transportLoopRange().getEnd().inSeconds() : record::noLimit;
            beginRecording ({ from, from, model.get().songMode && loopEnd > from ? loopEnd : record::noLimit,
                              transportLoopRange().getStart().inSeconds(), false });
            applyTransportLoop();
            applyLowLatency();
        }

        if (! headless)
            transport.play (false);
    }
}

double AppController::positionInSteps() const
{
    const auto stepSeconds = 15.0 / model.get().bpm;   // semicolcheia
    return stepSeconds > 0.0 ? edit->getTransport().getPosition().inSeconds() / stepSeconds : 0.0;
}

//==============================================================================
juce::Result AppController::newProject()
{
    needleTicks = 0;
    timeSelection = {};
    stop();
    stopAudition();

    juce::String error;
    const auto kit = KitGenerator::ensureKit (getKitFolder(), error);

    if (kit.isEmpty())
        return juce::Result::fail (error);

    Project fresh;
    wnl::normalise (fresh);
    model.file = juce::File();
    model.replace (fresh);

    for (auto& sound : kit)
    {
        auto params = new juce::DynamicObject();
        params->setProperty ("path", sound.file.getFullPathName());
        params->setProperty ("name", sound.name);
        params->setProperty ("rootKey", sound.rootKey);
        // 808: mono com glide e respeitando a duração da nota; open hat corta a si mesmo
        params->setProperty ("cutItself", sound.name == "Open hat");
        params->setProperty ("mono", sound.name == "808");
        params->setProperty ("oneShot", sound.name != "808");
        registry.run ("channel.addSampler", juce::var (params));
    }

    registry.clearHistory();
    model.dirty = false;
    pluginsTouched = false;
    cache.prune();   // os samples do projeto anterior que ninguém mais usa saem da memória
    return juce::Result::ok();
}

namespace
{
    /** Sample que não está onde o .wnl diz (caça de bugs do produtor, 21/10): projeto vindo de outro PC,
        pasta movida, kit em outro usuário do Windows, autosave com caminho absoluto. Procura pelo nome
        do arquivo, nesta ordem: a pasta samples/ do projeto, ao lado do .wnl, o caminho de origem
        (originalPath) e o kit da WHYNOT deste PC. Devolve quantos achou. */
    int relinkMissingSamples (Project& p, const juce::File& projectFolder, const juce::File& kitFolder)
    {
        int found = 0;

        for (auto& c : p.channels)
        {
            if (c.samplePath.isEmpty())
                continue;

            const auto current = juce::File::isAbsolutePath (c.samplePath) ? juce::File (c.samplePath)
                                                                           : projectFolder.getChildFile (c.samplePath);

            if (current.existsAsFile())
                continue;

            const auto name = c.samplePath.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);

            if (name.isEmpty())
                continue;

            juce::Array<juce::File> candidates { projectFolder.getChildFile ("samples").getChildFile (name),
                                                 projectFolder.getChildFile (name) };

            if (juce::File::isAbsolutePath (c.originalPath))
                candidates.add (juce::File (c.originalPath));

            candidates.add (kitFolder.getChildFile (name));

            for (auto& f : candidates)
            {
                if (! f.existsAsFile())
                    continue;

                c.samplePath = (f.isAChildOf (projectFolder) ? f.getRelativePathFrom (projectFolder) : f.getFullPathName())
                                   .replaceCharacter ('\\', '/');
                ++found;
                break;
            }
        }

        return found;
    }
}

juce::Result AppController::openProject (const juce::File& file)
{
    stopAudition();

    Project loaded;
    const auto result = wnl::load (file, loaded);

    if (result.failed())
        return result;

    needleTicks = 0;
    timeSelection = {};
    relinkMissingSamples (loaded, file.getParentDirectory(), getKitFolder());

    stop();
    model.file = file;
    model.replace (std::move (loaded));
    registry.clearHistory();
    model.dirty = false;
    pluginsTouched = false;
    cache.prune();   // os samples do projeto anterior que ninguém mais usa saem da memória

    if (projectLibrary != nullptr)
        projectLibrary->noteOpened (file);

    return juce::Result::ok();
}

juce::Result AppController::saveProject (const juce::File& requested)
{
    stopAudition();

    const auto target = requested == juce::File() ? model.file : requested.withFileExtension ("wnl");

    if (target == juce::File())
        return juce::Result::fail ("Escolha onde salvar o projeto.");

    // nome reservado, caminho longo demais, pasta sem permissão ou .wnl só de leitura: o motivo, em português
    if (auto writable = checkWritableTarget (target, true); writable.failed())
        return writable;

    capturePluginStates();
    auto copy = model.get();
    const auto previousFolder = model.projectFolder();

    if (copy.name == "Sem título"_t)
        copy.name = target.getFileNameWithoutExtension();

    const auto result = wnl::save (copy, target, true, previousFolder);

    if (result.failed())
        return result;

    // Os caminhos dos samples coletados mudaram para relativos: não é um passo de desfazer.
    model.file = target;
    model.replace (std::move (copy));
    model.dirty = false;
    pluginsTouched = false;

    if (projectLibrary != nullptr)
        projectLibrary->projectSaved (target);   // a prévia vai para a fila (decisão do usuário: a cada Ctrl+S)

    return juce::Result::ok();
}

juce::Result AppController::renderRange (const Project& p, double fromSeconds, double toSeconds, const juce::File& out)
{
    // consolidar (gravar voz, fatia 4): o projeto montado pelo comando, só o trecho, em 32 bits float
    RenderJob::Settings settings;
    settings.sampleRate = 48000.0;
    settings.bitDepth = 32;
    settings.fromSeconds = fromSeconds;
    settings.toSeconds = toSeconds;

    auto error = juce::Result::ok();
    auto job = RenderJob::prepare (engine, cache, p, model.projectFolder(), true, 1, out, catalog.get(), settings, error);
    return job == nullptr ? error : job->run();
}

juce::Result AppController::exportWav (const juce::File& file, int loops, bool song)
{
    stopAudition();

    // o render monta um Edit próprio: os plugins de lá abrem com o estado atual dos daqui
    capturePluginStates();
    return renderToWav (engine, cache, model.get(), model.projectFolder(), song, loops, file.withFileExtension ("wav"), catalog.get());
}

//==============================================================================
void AppController::installPluginLoader()
{
    // Toda instância de plugin passa por aqui. Antes de abrir, o app anota qual plugin está abrindo;
    // se ele derrubar o app, a próxima abertura acha a anotação e bloqueia o plugin.
    auto& pm = engine.getPluginManager();

    pm.createPluginInstance = [this, &pm] (const juce::PluginDescription& d, double rate, int block, juce::String& error)
        -> std::unique_ptr<juce::AudioPluginInstance>
    {
        if (catalog->isBlocked (d.fileOrIdentifier))
        {
            error = "Plugin bloqueado: "_t + catalog->blockReason (d.fileOrIdentifier);
            return {};
        }

        const auto marker = loadingMarker();
        marker.getParentDirectory().createDirectory();
        marker.replaceWithText (d.fileOrIdentifier + "\n" + d.name);

        auto instance = pm.pluginFormatManager.createPluginInstance (d, rate > 0.0 ? rate : 48000.0, block > 0 ? block : 512, error);

        marker.deleteFile();
        return instance;
    };
}

void AppController::recoverFromPluginCrash()
{
    const auto marker = loadingMarker();

    if (! marker.existsAsFile())
        return;

    const auto lines = juce::StringArray::fromLines (marker.loadFileAsString());
    marker.deleteFile();

    if (lines[0].isEmpty())
        return;

    const auto name = lines[1].isNotEmpty() ? lines[1] : juce::File (lines[0]).getFileNameWithoutExtension();
    catalog->block (lines[0], name, "derrubou o app ao abrir ("_t + juce::Time::getCurrentTime().formatted ("%d/%m/%Y %H:%M") + ")");
    catalog->save();
    startupWarning = name + " derrubou o app na última vez que foi aberto e foi bloqueado. Para tentar de novo: Plugins (F8), Bloqueados."_t;
}

void AppController::setPluginWindowFactory (std::function<std::unique_ptr<juce::Component> (te::Plugin&)> factory)
{
    if (auto* ui = dynamic_cast<WhynotUIBehaviour*> (&engine.getUIBehaviour()))
    {
        ui->createWindow = [this, factory = std::move (factory)] (te::Plugin& p) -> std::unique_ptr<juce::Component>
        {
            pluginsTouched = true;   // com a janela aberta, o usuário pode mexer no plugin
            return factory != nullptr ? factory (p) : nullptr;
        };
    }
}

bool AppController::capturePluginStates()
{
    bool changed = false, windowOpen = false;

    auto isShowing = [] (te::Plugin* p) { return p != nullptr && p->windowState != nullptr && p->windowState->isWindowShowing(); };

    for (auto& c : model.data().channels)
    {
        if (! c.isPlugin())
            continue;

        if (auto* plugin = projector->instrumentFor (c.id))
        {
            windowOpen = windowOpen || isShowing (plugin);
            const auto state = plugin_host::readState (*plugin);

            if (state.isNotEmpty() && state != c.plugin.state)
            {
                c.plugin.state = state;   // direto no modelo: estado de plugin não é passo de desfazer
                changed = true;
            }
        }
    }

    // inserts VST3 do mixer
    for (auto& m : model.data().mixer)
    {
        for (auto& ins : m.inserts)
        {
            if (ins.kind != "vst3")
                continue;

            if (auto* plugin = projector->insertPlugin (m.id, ins.id))
            {
                windowOpen = windowOpen || isShowing (plugin);
                const auto state = plugin_host::readState (*plugin);

                if (state.isNotEmpty() && state != ins.plugin.state)
                {
                    ins.plugin.state = state;
                    changed = true;
                }
            }
        }
    }

    if (changed && (pluginsTouched || windowOpen))
        model.dirty = true;

    return changed;
}

juce::Array<juce::PluginDescription> AppController::knownPlugins() const
{
    return catalog->getTypes();
}

juce::Array<juce::var> AppController::blockedPlugins() const
{
    juce::Array<juce::var> list;

    for (auto& b : catalog->getBlocked())
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("file", b.file);
        o->setProperty ("name", b.name);
        o->setProperty ("reason", b.reason);
        o->setProperty ("when", b.when.toISO8601 (true));
        list.add (juce::var (o));
    }

    return list;
}

juce::StringArray AppController::pluginFolders() const
{
    return catalog->getFolders();
}

void AppController::setPluginFolders (const juce::StringArray& folders)
{
    catalog->setFolders (folders);
    catalog->save();
}

juce::var AppController::describeReport (const PluginCatalog::ScanReport& r) const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("candidates", r.candidates);
    o->setProperty ("scanned", r.scanned);
    o->setProperty ("reused", r.reused);
    o->setProperty ("found", r.found);
    o->setProperty ("newlyBlocked", r.newlyBlocked);
    o->setProperty ("aborted", r.aborted);
    return juce::var (o);
}

juce::var AppController::scanPlugins (bool wait, int timeoutSeconds)
{
    scanTimeoutMs = juce::jlimit (2, 300, timeoutSeconds) * 1000;

    if (scanThread != nullptr)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("error", "Já tem um scan de plugins em andamento."_t);
        return juce::var (o);
    }

    if (wait)
    {
        const auto report = catalog->scan (scanTimeoutMs);
        finishScan (report);
        return describeReport (report);
    }

    scanThread = std::make_unique<ScanThread> (*this, scanTimeoutMs);
    scanThread->startThread();

    if (onScanProgress)
        onScanProgress ("Procurando plugins..."_t, 0.0);

    auto o = new juce::DynamicObject();
    o->setProperty ("started", true);
    return juce::var (o);
}

void AppController::finishScan (const PluginCatalog::ScanReport& report)
{
    if (scanThread != nullptr)
    {
        scanThread->stopThread (1000);
        scanThread = nullptr;
    }

    catalog->save();
    catalog->fillKnownList (engine.getPluginManager().knownPluginList);
    projector->sync (model.get(), model.projectFolder());   // plugins que faltavam podem abrir agora

    auto text = juce::String (report.found) + " plugins conhecidos";

    if (report.scanned > 0)
        text << ", " << report.scanned << " lidos agora";

    if (! report.newlyBlocked.isEmpty())
        text << ", " << report.newlyBlocked.size() << " bloqueados";

    if (onScanProgress)
        onScanProgress (text, -1.0);

    if (onPluginsChanged)
        onPluginsChanged();
}

bool AppController::unblockPlugin (const juce::String& file)
{
    if (! catalog->unblock (file))
        return false;

    catalog->save();
    catalog->fillKnownList (engine.getPluginManager().knownPluginList);

    if (onPluginsChanged)
        onPluginsChanged();

    return true;
}

juce::AudioPluginInstance* AppController::channelPlugin (const juce::String& channel)
{
    return plugin_host::instanceOf (projector->instrumentFor (channel));
}

juce::AudioPluginInstance* AppController::insertPluginInstance (const juce::String& track, const juce::String& insert)
{
    return plugin_host::instanceOf (projector->insertPlugin (track, insert));
}

bool AppController::showChannelPluginEditor (const juce::String& channel)
{
    // WHYNOT Synth: a janela nativa dele (a mesma do Tracktion, com o editor da WHYNOT).
    if (auto* synthPlugin = projector->synthFor (channel))
    {
        if (headless || synthPlugin->windowState == nullptr)
            return false;

        synthPlugin->windowState->showWindowExplicitly();
        return true;
    }

    auto* plugin = projector->instrumentFor (channel);

    if (headless || plugin_host::instanceOf (plugin) == nullptr)
        return false;

    plugin->windowState->showWindowExplicitly();
    return true;
}

bool AppController::showInsertEditor (const juce::String& track, const juce::String& insert)
{
    auto* plugin = projector->insertPlugin (track, insert);

    if (headless || plugin == nullptr || plugin->windowState == nullptr)
        return false;

    plugin->windowState->showWindowExplicitly();
    return true;
}

void AppController::pluginStateTouched()
{
    pluginsTouched = true;
    model.dirty = true;
}

double AppController::fileDurationSeconds (const juce::File& file) const
{
    std::unique_ptr<juce::AudioFormatReader> reader (const_cast<te::Engine&> (engine).getAudioFileFormatManager()
                                                       .readFormatManager.createReaderFor (file));
    return reader != nullptr && reader->sampleRate > 0.0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
}

void AppController::previewNote (const juce::String& channel, int key, int velocity)
{
    auto* track = projector->trackFor (channel);

    if (track == nullptr || headless)
        return;

    const auto note = juce::jlimit (0, 127, key);
    track->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocity)),
                                  te::createUniqueMPESourceID());

    // a prévia dura um quarto de segundo; a referência à trilha garante que ela ainda existe
    te::AudioTrack::Ptr keep (track);
    juce::Timer::callAfterDelay (250, [keep, note]
    {
        keep->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, note), te::createUniqueMPESourceID());
    });
}


//==============================================================================
// Acabamento (entrega 3.4)
juce::String AppController::projectTitle() const
{
    const auto& name = model.get().name;
    return (name.isEmpty() || name == "Sem título"_t) && model.file != juce::File() ? model.file.getFileNameWithoutExtension() : name;
}

//==============================================================================
// Comparação A/B da mixagem
void AppController::setAudition (bool bypassInserts, float trimDb)
{
    auditionOn = bypassInserts || std::abs (trimDb) > 0.001f;
    auditionMixer = auditionOn ? model.get().mixer : std::vector<MixerTrack>();
    projector->setAudition (bypassInserts, trimDb);
    projector->sync (model.get(), model.projectFolder());
}

void AppController::stopAudition()
{
    if (auditionOn)
        setAudition (false, 0.0f);
}

float AppController::renderLoudness (const Project& p, bool song, bool bypassInserts, float trimDb)
{
    capturePluginStates();
    const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("whynot-ab", ".wav", false);
    const RenderAudition audition { bypassInserts, trimDb };
    float lufs = dsp::LoudnessMeter::silence;

    if (renderToWav (engine, cache, p, model.projectFolder(), song, 1, wav, catalog.get(), 48000.0, 32, audition).wasOk())
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav)); reader != nullptr && reader->lengthInSamples > 0)
        {
            juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
            dsp::LoudnessMeter meter;
            meter.prepare (reader->sampleRate);
            meter.process (buffer.getReadPointer (0), buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0), buffer.getNumSamples());
            lufs = meter.integrated();
        }
    }

    wav.deleteFile();
    return lufs;
}

namespace
{
    /** Relê o render (float), limita a `ceilingDb` de pico real e, se precisou, regrava em 24 bits.
        O MP3 cria picos entre as amostras ao decodificar: -1 dBTP deixa folga para eles. */
    juce::Result limitWavFile (const juce::File& wav, float ceilingDb, float& reductionDb)
    {
        reductionDb = 0.0f;
        juce::AudioBuffer<float> buffer;
        double rate = 0.0;

        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));

            if (reader == nullptr || reader->lengthInSamples <= 0)
                return juce::Result::fail ("Não consegui reler o render para o MP3."_t);

            rate = reader->sampleRate;
            buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        }

        reductionDb = dsp::limitTruePeakOffline (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                                 buffer.getNumSamples(), rate, ceilingDb);

        if (reductionDb <= 0.0f)
            return juce::Result::ok();   // nada passou do teto: o render vai como está

        wav.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream().release());

        if (stream == nullptr)
            return juce::Result::fail ("Não consegui gravar o render limitado para o MP3."_t);

        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                          .withSampleRate (rate)
                                                          .withNumChannels (buffer.getNumChannels())
                                                          .withBitsPerSample (24));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
            return juce::Result::fail ("Não consegui gravar o render limitado para o MP3."_t);

        return juce::Result::ok();
    }

    /** O MP3 de divulgação (exportar para vender, bloco 2, 23/10): a tag do produtor somada ao render, a primeira
        meio segundo depois do começo e as outras a cada `everySeconds` (no mínimo 5 s), sem passar do fim da música.
        Regrava o WAV do render em float. Thread de trabalho. */
    juce::Result mixProducerTag (const juce::File& wav, const juce::File& tagFile, double everySeconds, float gainDb,
                                 juce::AudioFormatManager& formats)
    {
        juce::AudioBuffer<float> song;
        double rate = 0.0;

        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));

            if (reader == nullptr || reader->lengthInSamples <= 0)
                return juce::Result::fail ("Não consegui reler o render para pôr a tag."_t);

            rate = reader->sampleRate;
            song.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&song, 0, song.getNumSamples(), 0, true, true);
        }

        std::unique_ptr<juce::AudioFormatReader> tagReader (formats.createReaderFor (tagFile));

        if (tagReader == nullptr || tagReader->lengthInSamples <= 0)
            return juce::Result::fail ("Não consegui ler a tag do produtor: "_t + tagFile.getFileName() + ". Use um WAV ou MP3."_t);

        if (tagReader->lengthInSamples > (juce::int64) (tagReader->sampleRate * 30.0))
            return juce::Result::fail ("A tag do produtor passa de 30 s: use só a vinheta curta."_t);

        juce::AudioBuffer<float> tag ((int) tagReader->numChannels, (int) tagReader->lengthInSamples);
        tagReader->read (&tag, 0, tag.getNumSamples(), 0, true, true);

        // na taxa do render (a tag pode vir a 48 kHz e o MP3 sai a 44,1)
        if (std::abs (tagReader->sampleRate - rate) > 0.5)
        {
            const double ratio = tagReader->sampleRate / rate;
            const int length = juce::jmax (1, (int) std::floor (tag.getNumSamples() / ratio));
            juce::AudioBuffer<float> resampled (tag.getNumChannels(), length);

            for (int ch = 0; ch < tag.getNumChannels(); ++ch)
            {
                juce::LagrangeInterpolator interpolator;
                interpolator.process (ratio, tag.getReadPointer (ch), resampled.getWritePointer (ch), length, tag.getNumSamples(), 0);
            }

            tag = std::move (resampled);
        }

        const float gain = juce::Decibels::decibelsToGain (gainDb);
        const int every = juce::jmax (1, juce::roundToInt (juce::jmax (5.0, everySeconds) * rate));
        const int length = tag.getNumSamples();
        int placed = 0;

        for (int at = juce::roundToInt (0.5 * rate); at < song.getNumSamples(); at += every)
        {
            if (placed > 0 && at + length > song.getNumSamples())
                break;   // a última não fica cortada no fim (a primeira entra mesmo num beat curto)

            const int n = juce::jmin (length, song.getNumSamples() - at);

            for (int ch = 0; ch < song.getNumChannels(); ++ch)
                song.addFrom (ch, at, tag, juce::jmin (ch, tag.getNumChannels() - 1), 0, n, gain);

            ++placed;
        }

        wav.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream().release());

        if (stream == nullptr)
            return juce::Result::fail ("Não consegui gravar o render com a tag."_t);

        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                          .withSampleRate (rate)
                                                          .withNumChannels (song.getNumChannels())
                                                          .withBitsPerSample (32));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (song, 0, song.getNumSamples()))
            return juce::Result::fail ("Não consegui gravar o render com a tag."_t);

        return juce::Result::ok();
    }

    /** O zip do pacote: a pasta inteira, com o nome dela na frente dos caminhos. Grava num arquivo de lado e troca no
        fim, para um zip pela metade nunca ficar com o nome certo. Thread de trabalho. */
    juce::Result zipFolder (const juce::File& folder, const juce::File& zip)
    {
        juce::ZipFile::Builder builder;
        int count = 0;

        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
        {
            const auto relative = entry.getFile().getRelativePathFrom (folder).replaceCharacter ('\\', '/');
            builder.addFile (entry.getFile(), 6, folder.getFileName() + "/" + relative);
            ++count;
        }

        if (count == 0)
            return juce::Result::fail ("A pasta do pacote ficou vazia: nada para o zip."_t);

        const auto temp = zip.getSiblingFile (zip.getFileNameWithoutExtension() + ".zip-parcial");
        temp.deleteFile();

        {
            juce::FileOutputStream out (temp);

            if (! out.openedOk() || ! builder.writeToStream (out, nullptr))
                return juce::Result::fail ("Não consegui gravar o zip em "_t + zip.getParentDirectory().getFullPathName());
        }

        zip.deleteFile();
        return temp.moveFileTo (zip) ? juce::Result::ok() : juce::Result::fail ("Não consegui gravar o zip: "_t + zip.getFileName());
    }
}

juce::Result AppController::exportMp3 (const juce::File& file, int loops, bool song, int bitrateKbps, bool vbr)
{
    stopAudition();

    if (! mp3EncoderAvailable())
        return encodeMp3 ({}, {}, {});   // devolve a explicação de por que não há MP3 neste build

    // o MP3 só é gravado depois do render inteiro: o destino é conferido antes, para não esperar à toa
    if (auto writable = checkWritableTarget (file, true); writable.failed())
        return writable;

    // O MP3 sai a 44,1 kHz, a taxa que todo player aceita: render nessa taxa e codificação com o LAME.
    capturePluginStates();
    const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("whynot-mp3", ".wav", false);
    // Render em float (32 bits): nada é cortado em 0 dBFS antes do limitador de pico real.
    auto result = renderToWav (engine, cache, model.get(), model.projectFolder(), song, loops, wav, catalog.get(), 44100.0, 32);
    float limitedDb = 0.0f;

    if (result.wasOk())
        result = limitWavFile (wav, -1.0f, limitedDb);

    if (result.wasOk())
    {
        Mp3Settings settings;
        settings.bitrateKbps = bitrateKbps;
        settings.vbr = vbr;
        settings.title = projectTitle();
        result = encodeMp3 (wav, file, settings);
    }

    wav.deleteFile();
    return result;
}

namespace
{
    // Trilhas pelas quais o som de `id` passa até o master (só saídas, sem envios).
    juce::StringArray outputPath (const Project& p, juce::String id)
    {
        juce::StringArray path;

        for (int guard = 0; guard < 256 && id != masterTrackId && ! path.contains (id); ++guard)
        {
            path.add (id);
            auto* m = p.findMixer (id);

            if (m == nullptr)
                break;

            id = p.resolveOutput (m->output);
        }

        return path;
    }

    // O canal toca alguma coisa: notas ou passos em algum padrão, ou (áudio) um clipe na playlist.
    bool channelPlays (const Project& p, const Channel& c)
    {
        if (c.isAudio())
            return std::any_of (p.playlistItems.begin(), p.playlistItems.end(),
                                [&c] (const PlaylistItem& i) { return i.kind == "audio" && i.ref == c.id; });

        for (auto& pattern : p.patterns)
            if (! Projector::notesFor (p, pattern, c, pattern.lengthTicks()).empty())
                return true;

        return false;
    }

    bool receivesSound (const Project& p, const juce::String& mixerId)
    {
        for (auto& c : p.channels)
        {
            if (c.mute || ! channelPlays (p, c))
                continue;

            const auto out = p.resolveOutput (c.output);

            if (out == mixerId || p.reaches (out, mixerId, false))
                return true;
        }

        return false;
    }

    /** O projeto como ele soa só pela saída da trilha `stem` (como os stems do FL):
        - ela em solo, com o que entra nela pela saída de outras trilhas;
        - quem só chega nela por envio (a voz no bus de reverb) toca, mas a saída direta vai para
          uma trilha muda: fica só o envio;
        - os buses que recebem envio dela ficam mudos: o reverb dela sai no stem do bus.
        Assim a soma dos stems de trilhas paralelas dá a mixagem. */
    Project stemProject (const Project& p, const juce::String& stem, bool masterChain)
    {
        Project copy = p;

        // Sem a cadeia do master (padrão): quem recebe os stems faz o próprio master, e cada stem não
        // empurra o compressor e o limitador do master sozinho. O volume e o pan do master continuam.
        if (! masterChain)
            for (auto& m : copy.mixer)
                if (m.isMaster())
                    for (auto& ins : m.inserts)
                        ins.bypass = true;

        MixerTrack sink;
        sink.id = "mx_stem_mudo";
        sink.name = "(stem: mudo)";
        sink.volumeDb = -100.0f;   // silêncio
        bool needsSink = false;
        const auto stemPath = outputPath (p, stem);

        for (auto& m : copy.mixer)
            m.solo = m.id == stem;

        for (auto& m : copy.mixer)
        {
            if (m.isMaster() || stemPath.contains (m.id))
                continue;

            if (! p.reaches (m.id, stem, false))
            {
                // Não alimenta o stem (o bus que só recebe envio dele, por exemplo). Quem só dispara um
                // sidechain de algo do stem (o kick no compressor do 808) não fica mudo aqui: o solo do
                // Projector o deixa processando com a saída muda, e o stem tem o mesmo abaixar da mixagem.
                if (! p.reaches (m.id, stem, true))
                    m.mute = true;
            }
            else if (! outputPath (p, m.id).contains (stem))
            {
                m.output = sink.id;   // chega só por envio
                m.solo = true;
                needsSink = true;
            }
        }

        if (needsSink)
        {
            auto master = std::find_if (copy.mixer.begin(), copy.mixer.end(), [] (const MixerTrack& m) { return m.isMaster(); });
            copy.mixer.insert (master, sink);
        }

        return copy;
    }

    juce::var errorVar (const juce::String& text)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("error", text);
        return juce::var (o);
    }
}

Project AppController::soloTrackProject (const Project& p, const juce::String& mixerId, bool masterChain) const
{
    return stemProject (p, mixerId, masterChain);
}

// a tag do produtor (exportar para vender, bloco 2): o caminho num arquivo de texto na pasta de dados, fora do projeto
juce::File AppController::producerTag() const
{
    const auto path = dataFolder.getChildFile ("tag-do-produtor.txt").loadFileAsString().trim();
    return path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

void AppController::setProducerTag (const juce::File& file)
{
    const auto store = dataFolder.getChildFile ("tag-do-produtor.txt");

    if (file == juce::File())
        store.deleteFile();
    else
        store.replaceWithText (file.getFullPathName());
}

juce::StringArray AppController::defaultStemTracks() const
{
    juce::StringArray ids;

    for (auto& m : model.get().mixer)
        if (! m.isMaster() && ! m.mute && receivesSound (model.get(), m.id))
            ids.add (m.id);

    return ids;
}

juce::var AppController::exportStems (const juce::File& folder, int loops, bool song, const juce::StringArray& requested, bool masterChain)
{
    stopAudition();

    capturePluginStates();
    const auto& p = model.get();
    const auto ids = requested.isEmpty() ? defaultStemTracks() : requested;

    if (ids.isEmpty())
        return errorVar ("Nenhuma trilha do mixer recebe som para virar stem."_t);

    if (! folder.createDirectory())
        return errorVar ("Não consegui criar a pasta "_t + folder.getFullPathName());

    juce::Array<juce::var> files;
    juce::StringArray usedNames;

    for (auto& id : ids)
    {
        const auto* track = p.findMixer (id);

        if (track == nullptr || track->isMaster())
            continue;

        auto name = safeFileName (projectTitle() + " - " + track->name);

        for (int n = 2; usedNames.contains (name, true); ++n)
            name = safeFileName (projectTitle() + " - " + track->name + " (" + juce::String (n) + ")");

        usedNames.add (name);
        const auto out = folder.getChildFile (name + ".wav");
        const auto result = renderToWav (engine, cache, stemProject (p, id, masterChain), model.projectFolder(), song, loops, out, catalog.get());

        if (result.failed())
            return errorVar (track->name + ": " + result.getErrorMessage());

        auto f = new juce::DynamicObject();
        f->setProperty ("track", track->id);
        f->setProperty ("name", track->name);
        f->setProperty ("path", out.getFullPathName());
        files.add (juce::var (f));
    }

    auto o = new juce::DynamicObject();
    o->setProperty ("folder", folder.getFullPathName());
    o->setProperty ("files", files);
    return juce::var (o);
}

bool AppController::isExportingInBackground() const
{
    return exportRunner != nullptr && exportRunner->isRunning();
}

juce::var AppController::backgroundExportStatus() const
{
    if (exportRunner == nullptr)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("running", false);
        o->setProperty ("finished", false);
        return juce::var (o);
    }

    return exportRunner->status();
}

void AppController::cancelBackgroundExport()
{
    if (exportRunner != nullptr)
        exportRunner->cancel();
}

juce::Result AppController::startBackgroundExport (const BackgroundExport& request)
{
    if (isExportingInBackground())
        return juce::Result::fail ("Já tem uma exportação em andamento."_t);

    if (request.target == juce::File())
        return juce::Result::fail (request.kind == "stems" ? "Escolha a pasta dos stems."_t : "Escolha onde salvar o arquivo."_t);

    stopAudition();

    // O que sai é o projeto deste instante: mudar o projeto durante a exportação não muda o arquivo.
    capturePluginStates();
    const Project snapshot = model.get();
    const auto folder = model.projectFolder();
    const auto loops = juce::jlimit (1, 64, request.loops);
    const bool song = request.song;
    std::vector<ExportRunner::Step> steps;

    // só um trecho (a seleção da régua): vale para a música inteira, no tempo do projeto
    RenderJob::Settings range;

    if (request.toTicks > request.fromTicks)
    {
        if (! song)
            return juce::Result::fail ("Exportar um trecho vale só para a música inteira (SONG)."_t);

        range.fromSeconds = ticksToSeconds (request.fromTicks, snapshot.bpm);
        range.toSeconds = ticksToSeconds (request.toTicks, snapshot.bpm);
    }

    // a música inteira vai até o som acabar (até 10 s além do último bloco); o padrão e o trecho, no tamanho exato
    if (song && ! (request.toTicks > request.fromTicks))
        range.tailSeconds = 10.0;

    std::function<void()> cleanup;

    if (request.kind == "wav")
    {
        const auto out = request.target.withFileExtension ("wav");
        RenderJob::Settings rs = range;
        rs.sampleRate = request.sampleRate;
        rs.bitDepth = request.bitDepth;
        steps.push_back ({ "Exportando WAV"_t,
                           [this, snapshot, folder, loops, song, out, rs] (juce::Result& error)
                           { return RenderJob::prepare (engine, cache, snapshot, folder, song, loops, out, catalog.get(), rs, error); },
                           {}, out.getFullPathName() });
    }
    else if (request.kind == "mp3")
    {
        if (! mp3EncoderAvailable())
            return encodeMp3 ({}, {}, {});   // devolve a explicação de por que não há MP3 neste build

        const auto out = request.target.withFileExtension ("mp3");

        // o MP3 só é gravado depois do render inteiro: o destino é conferido antes, para não esperar à toa
        if (auto writable = checkWritableTarget (out, true); writable.failed())
            return writable;

        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("whynot-mp3", ".wav", false);
        Mp3Settings settings;
        settings.bitrateKbps = request.bitrateKbps;
        settings.vbr = request.vbr;
        settings.title = projectTitle();

        // como no exportMp3: render em float a 44,1 kHz, pico real a -1 dBTP e o LAME
        steps.push_back ({ "Exportando MP3"_t,
                           [this, snapshot, folder, loops, song, wav, range] (juce::Result& error)
                           {
                               RenderJob::Settings rs = range;
                               rs.sampleRate = 44100.0;
                               rs.bitDepth = 32;
                               return RenderJob::prepare (engine, cache, snapshot, folder, song, loops, wav, catalog.get(), rs, error);
                           },
                           [wav, out, settings] (const std::function<bool()>& shouldStop)
                           {
                               float limitedDb = 0.0f;
                               auto result = limitWavFile (wav, -1.0f, limitedDb);

                               // Parar durante o LAME para na hora e não deixa o MP3 pela metade
                               if (result.wasOk())
                                   result = encodeMp3 (wav, out, settings, shouldStop);

                               if (result.failed() && shouldStop())
                                   out.deleteFile();

                               wav.deleteFile();
                               return result;
                           },
                           out.getFullPathName() });
        cleanup = [wav] { wav.deleteFile(); };
    }
    else if (request.kind == "stems")
    {
        const auto ids = request.tracks.isEmpty() ? defaultStemTracks() : request.tracks;

        if (ids.isEmpty())
            return juce::Result::fail ("Nenhuma trilha do mixer recebe som para virar stem."_t);

        if (! request.target.createDirectory())
            return juce::Result::fail ("Não consegui criar a pasta "_t + request.target.getFullPathName());

        juce::StringArray usedNames;
        const auto title = projectTitle();
        const bool masterChain = request.masterChain;

        for (auto& id : ids)
        {
            const auto* track = snapshot.findMixer (id);

            if (track == nullptr || track->isMaster())
                continue;

            // os mesmos nomes do exportStems
            auto name = safeFileName (title + " - " + track->name);

            for (int n = 2; usedNames.contains (name, true); ++n)
                name = safeFileName (title + " - " + track->name + " (" + juce::String (n) + ")");

            usedNames.add (name);
            const auto out = request.target.getChildFile (name + ".wav");
            const auto trackId = id;
            steps.push_back ({ "Stem: "_t + track->name,
                               [this, snapshot, folder, loops, song, out, trackId, masterChain, range] (juce::Result& error)
                               {
                                   RenderJob::Settings rs = range;
                                   rs.trimTail = false;   // os stems com a mesma cauda: todos do mesmo tamanho, alinhados
                                   return RenderJob::prepare (engine, cache, stemProject (snapshot, trackId, masterChain), folder, song, loops,
                                                              out, catalog.get(), rs, error);
                               },
                               {}, out.getFullPathName() });
        }

        if (steps.empty())
            return juce::Result::fail ("Nenhuma trilha do mixer recebe som para virar stem."_t);
    }
    else if (request.kind == "pack")
    {
        // Exportar para vender (bloco 2, 23/10): uma pasta com o nome do beat, o BPM e o tom; dentro, o WAV limpo,
        // o MP3 com a tag do produtor e os stems; ao lado, o zip de tudo. Sempre a música inteira.
        if (snapshot.playlistItems.empty())
            return juce::Result::fail ("Coloque o beat na playlist (SONG): o pacote para vender leva a música inteira."_t);

        const auto base = packBaseName (snapshot, projectTitle());
        const auto pack = request.target.getChildFile (base);

        if (! pack.createDirectory())
            return juce::Result::fail ("Não consegui criar a pasta "_t + pack.getFullPathName());

        RenderJob::Settings song44 = range;
        song44.sampleRate = 44100.0;
        song44.bitDepth = 24;

        if (request.packWav)
        {
            const auto out = pack.getChildFile (base + ".wav");
            steps.push_back ({ "Pacote: o WAV limpo"_t,
                               [this, snapshot, folder, out, song44] (juce::Result& error)
                               { return RenderJob::prepare (engine, cache, snapshot, folder, true, 1, out, catalog.get(), song44, error); },
                               {}, out.getFullPathName() });
        }

        if (request.packMp3)
        {
            if (! mp3EncoderAvailable())
                return encodeMp3 ({}, {}, {});

            const bool tagged = request.tag != juce::File();

            if (tagged && ! request.tag.existsAsFile())
                return juce::Result::fail ("A tag do produtor não está mais lá: "_t + request.tag.getFullPathName());

            const auto out = pack.getChildFile (base + (tagged ? " (com tag).mp3"_t : juce::String (".mp3")));

            if (auto writable = checkWritableTarget (out, true); writable.failed())
                return writable;

            const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("whynot-pacote", ".wav", false);
            Mp3Settings settings;
            settings.bitrateKbps = 320;
            settings.title = projectTitle();
            const auto tagFile = request.tag;
            const double every = request.tagEverySeconds;
            const float gainDb = request.tagGainDb;
            auto* readers = &engine.getAudioFileFormatManager().readFormatManager;   // lê a tag em MP3 também

            steps.push_back ({ tagged ? "Pacote: o MP3 com a tag"_t : "Pacote: o MP3"_t,
                               [this, snapshot, folder, wav, range] (juce::Result& error)
                               {
                                   RenderJob::Settings rs = range;   // como no exportMp3: float a 44,1 kHz
                                   rs.sampleRate = 44100.0;
                                   rs.bitDepth = 32;
                                   return RenderJob::prepare (engine, cache, snapshot, folder, true, 1, wav, catalog.get(), rs, error);
                               },
                               [wav, out, settings, tagFile, every, gainDb, readers] (const std::function<bool()>& shouldStop)
                               {
                                   auto result = tagFile != juce::File() ? mixProducerTag (wav, tagFile, every, gainDb, *readers) : juce::Result::ok();
                                   float limitedDb = 0.0f;

                                   if (result.wasOk())
                                       result = limitWavFile (wav, -1.0f, limitedDb);

                                   if (result.wasOk())
                                       result = encodeMp3 (wav, out, settings, shouldStop);

                                   if (result.failed() && shouldStop())
                                       out.deleteFile();

                                   wav.deleteFile();
                                   return result;
                               },
                               out.getFullPathName() });
            cleanup = [wav] { wav.deleteFile(); };
        }

        if (request.packStems)
        {
            const auto stemsFolder = pack.getChildFile ("Stems");
            juce::StringArray usedNames;

            for (auto& id : defaultStemTracks())
            {
                const auto* track = snapshot.findMixer (id);

                if (track == nullptr || track->isMaster())
                    continue;

                auto name = safeFileName (base + " - " + track->name);

                for (int n = 2; usedNames.contains (name, true); ++n)
                    name = safeFileName (base + " - " + track->name + " (" + juce::String (n) + ")");

                usedNames.add (name);
                const auto out = stemsFolder.getChildFile (name + ".wav");
                const auto trackId = id;
                steps.push_back ({ "Pacote: stem "_t + track->name,
                                   [this, snapshot, folder, out, trackId, song44, stemsFolder] (juce::Result& error)
                                   {
                                       stemsFolder.createDirectory();
                                       RenderJob::Settings rs = song44;
                                       rs.trimTail = false;   // os stems do mesmo tamanho, alinhados
                                       return RenderJob::prepare (engine, cache, stemProject (snapshot, trackId, false), folder, true, 1,
                                                                  out, catalog.get(), rs, error);
                                   },
                                   {}, out.getFullPathName() });
            }
        }

        if (steps.empty())
            return juce::Result::fail ("Escolha pelo menos um arquivo para o pacote: o WAV, o MP3 ou os stems."_t);

        // o zip vai no fim da última etapa, depois de tudo gravado
        if (request.packZip)
        {
            const auto zip = request.target.getChildFile (base + ".zip");
            auto previous = steps.back().after;
            steps.back().after = [previous, pack, zip] (const std::function<bool()>& shouldStop)
            {
                auto result = previous ? previous (shouldStop) : juce::Result::ok();

                if (result.wasOk() && ! shouldStop())
                    result = zipFolder (pack, zip);

                return result;
            };
        }
    }
    else
    {
        return juce::Result::fail ("Formato desconhecido: "_t + request.kind);
    }

    exportRunner = nullptr;   // a anterior já terminou (o status dela fica até a próxima começar)
    exportRunner = std::make_unique<ExportRunner> (request.kind, std::move (steps), std::move (cleanup));
    exportRunner->start();
    return juce::Result::ok();
}

juce::Result AppController::saveNewVersion()
{
    if (model.file == juce::File())
        return juce::Result::fail ("Salve o projeto uma vez antes de criar versões."_t);

    // "Beat" → "Beat_2"; "Beat_2" → "Beat_3" (como o FL), pulando nomes que já existem
    auto base = model.file.getFileNameWithoutExtension();
    int n = 1;
    const auto suffix = base.fromLastOccurrenceOf ("_", false, false);

    if (base.containsChar ('_') && suffix.isNotEmpty() && suffix.containsOnly ("0123456789"))
    {
        n = suffix.getIntValue();
        base = base.upToLastOccurrenceOf ("_", false, false);
    }

    juce::File next;

    do
        next = model.file.getSiblingFile (base + "_" + juce::String (++n) + ".wnl");
    while (next.exists());

    return saveProject (next);
}

juce::File AppController::autosaveFolder() const
{
    return model.file != juce::File() ? model.projectFolder().getChildFile ("autosave")
                                      : dataFolder.getChildFile ("Autosave");
}

juce::File AppController::autosaveNow()
{
    const bool pluginsChanged = capturePluginStates();

    if (! model.dirty || (editsSinceAutosave == 0 && ! pluginsChanged))
        return {};   // nada novo desde o último salvar ou autosave

    const auto folder = autosaveFolder();
    const auto base = safeFileName (model.file != juce::File() ? model.file.getFileNameWithoutExtension()
                                                               : juce::String ("Sem título"_t));
    const auto target = folder.getChildFile (base + " - autosave " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".wnl");

    // Cópia com caminhos absolutos: o autosave abre de qualquer pasta sem copiar samples.
    auto copy = model.get();

    for (auto& c : copy.channels)
        if (c.samplePath.isNotEmpty() && ! juce::File::isAbsolutePath (c.samplePath))
            c.samplePath = model.resolveSample (c.samplePath).getFullPathName().replaceCharacter ('\\', '/');

    auto saved = target;

    if (wnl::save (copy, saved, false).failed())
    {
        // Pasta do projeto sem permissão (pendrive protegido, projeto aberto de dentro de um zip, pasta
        // só de leitura): o autosave vai para a pasta de dados, em vez de não acontecer (caça de bugs, 21/10).
        const auto fallback = dataFolder.getChildFile ("Autosave");

        if (folder == fallback)
            return {};

        saved = fallback.getChildFile (target.getFileName());

        if (wnl::save (copy, saved, false).failed())
            return {};
    }

    // guarda só os mais novos deste projeto
    auto old = saved.getParentDirectory().findChildFiles (juce::File::findFiles, false, base + " - autosave *.wnl");
    std::sort (old.begin(), old.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });

    for (int i = autosavesToKeep; i < old.size(); ++i)
        old.getReference (i).deleteFile();

    if (session != nullptr)
        session->noteAutosave (saved, model.file);

    editsSinceAutosave = 0;
    return saved;
}

juce::Result AppController::recoverProject (const Session::Recovery& r)
{
    Project loaded;
    const auto result = wnl::load (r.autosave, loaded);

    if (result.failed())
        return result;

    stop();
    model.file = r.original;   // "Salvar" grava no projeto de onde o autosave veio
    model.replace (std::move (loaded));
    registry.clearHistory();
    model.dirty = true;        // o que foi recuperado ainda não está salvo no projeto
    pluginsTouched = false;
    return juce::Result::ok();
}

juce::Result AppController::openAutosave (const juce::File& autosave)
{
    if (! autosave.existsAsFile())
        return juce::Result::fail ("Autosave não encontrado: "_t + autosave.getFullPathName());

    // "<projeto> - autosave 2026-10-22 081500.wnl" dentro de <pasta do projeto>/autosave/: volta para <projeto>.wnl
    Session::Recovery r;
    r.autosave = autosave;
    r.when = autosave.getLastModificationTime();

    const auto base = autosave.getFileNameWithoutExtension().upToFirstOccurrenceOf (" - autosave ", false, false);
    const auto folder = autosave.getParentDirectory();

    if (folder.getFileName().equalsIgnoreCase ("autosave") && base.isNotEmpty())
        if (const auto original = folder.getParentDirectory().getChildFile (base + ".wnl"); original.existsAsFile())
            r.original = original;

    return recoverProject (r);
}

juce::File AppController::autosaveBrowseFolder() const
{
    const auto folder = autosaveFolder();
    return folder.isDirectory() ? folder : dataFolder.getChildFile ("Autosave");
}

void AppController::shortcutsChanged()
{
    keys.save();

    if (onShortcutsChanged)
        onShortcutsChanged();
}

//==============================================================================
juce::var AppController::audioDeviceInfo() const
{
    auto& dm = const_cast<te::Engine&> (engine).getDeviceManager().deviceManager;
    auto o = new juce::DynamicObject();
    juce::Array<juce::var> types;
    bool asio = false;

    for (auto* type : dm.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        auto t = new juce::DynamicObject();
        t->setProperty ("name", type->getTypeName());
        juce::Array<juce::var> names;
        for (auto& n : type->getDeviceNames (false)) names.add (n);
        t->setProperty ("devices", names);
        types.add (juce::var (t));
        asio = asio || type->getTypeName() == "ASIO";
    }

    o->setProperty ("types", types);
    o->setProperty ("asioAvailable", asio);
    o->setProperty ("type", dm.getCurrentAudioDeviceType());

    if (auto* device = dm.getCurrentAudioDevice())
    {
        const double rate = device->getCurrentSampleRate();
        o->setProperty ("device", device->getName());
        o->setProperty ("sampleRate", rate);
        o->setProperty ("bufferSize", device->getCurrentBufferSizeSamples());
        o->setProperty ("outputLatencyMs", rate > 0.0 ? 1000.0 * device->getOutputLatencyInSamples() / rate : 0.0);

        juce::Array<juce::var> rates, buffers;
        for (auto r : device->getAvailableSampleRates()) rates.add (r);
        for (auto b : device->getAvailableBufferSizes()) buffers.add (b);
        o->setProperty ("sampleRates", rates);
        o->setProperty ("bufferSizes", buffers);
    }

    return juce::var (o);
}

juce::Result AppController::setAudioDevice (const juce::String& type, const juce::String& device, double rate, int buffer)
{
    if (headless)
        return juce::Result::fail ("Sem placa de som no modo sem janela.");

    auto& dm = engine.getDeviceManager().deviceManager;

    if (type.isNotEmpty() && type != dm.getCurrentAudioDeviceType())
    {
        bool known = false;

        for (auto* t : dm.getAvailableDeviceTypes())
            known = known || t->getTypeName() == type;

        if (! known)
            return juce::Result::fail ("Tipo de driver desconhecido: "_t + type);

        dm.setCurrentAudioDeviceType (type, true);
    }

    auto setup = dm.getAudioDeviceSetup();

    if (device.isNotEmpty())
        setup.outputDeviceName = device;

    if (rate > 0.0)
        setup.sampleRate = rate;

    if (buffer > 0)
        setup.bufferSize = buffer;

    const auto error = dm.setAudioDeviceSetup (setup, true);
    return error.isEmpty() ? juce::Result::ok() : juce::Result::fail (error);
}

juce::Array<juce::var> AppController::midiInputs() const
{
    juce::Array<juce::var> list;

    for (auto& d : const_cast<te::Engine&> (engine).getDeviceManager().getMidiInDevices())
    {
        // só os aparelhos de verdade: o virtual "All MIDI Ins" do Tracktion repete as notas deles
        if (d == nullptr || d->getDeviceType() != te::InputDevice::physicalMidiDevice)
            continue;

        auto o = new juce::DynamicObject();
        o->setProperty ("name", d->getName());
        o->setProperty ("id", d->getDeviceID());
        o->setProperty ("enabled", d->isEnabled());
        o->setProperty ("virtual", d->getDeviceType() == te::InputDevice::virtualMidiDevice);
        list.add (juce::var (o));
    }

    return list;
}

bool AppController::setMidiInputEnabled (const juce::String& nameOrId, bool on)
{
    for (auto& d : engine.getDeviceManager().getMidiInDevices())
    {
        if (d != nullptr && (d->getName() == nameOrId || d->getDeviceID() == nameOrId))
        {
            d->setEnabled (on);
            liveInputCount = -1;   // a lista de entradas do Edit muda: rotear de novo
            return true;
        }
    }

    return false;
}

void AppController::setLiveChannel (const juce::String& channel)
{
    liveChannelId = channel;
    routeLiveInputs();
}

void AppController::routeLiveInputs()
{
    // As entradas MIDI tocam o canal ao vivo (como no FL: o controlador toca o canal escolhido no rack).
    if (headless)
        return;

    auto* track = liveChannelId.isEmpty() ? nullptr : projector->trackFor (liveChannelId);
    const auto target = track != nullptr ? track->itemID : te::EditItemID();
    juce::Array<te::InputDeviceInstance*> inputs;

    for (auto* in : edit->getAllInputDevices())
        if (in != nullptr && in->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
            inputs.add (in);

    if (target == liveTrack && inputs.size() == liveInputCount)
        return;

    for (auto* in : inputs)
    {
        for (auto old : in->getTargets())
            if (old != target)
                (void) in->removeTarget (old, nullptr);

        if (target.isValid() && ! in->getTargets().contains (target))
            (void) in->setTarget (target, false, nullptr);

        in->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::on);
    }

    liveTrack = target;
    liveInputCount = inputs.size();
}

void AppController::rememberLiveNote (int key, int velocity, bool on, double ms)
{
    // tocando o padrão (PAT), a nota guarda onde caiu nele: o Capture MIDI põe ela no mesmo lugar
    double patternTick = -1.0;
    const auto* pat = model.get().currentPatternPtr();

    if (isPlaying() && ! model.get().songMode && pat != nullptr && pat->lengthTicks() > 0)
        patternTick = std::fmod (juce::jmax (0.0, positionInSteps() * ticksPerStep), (double) pat->lengthTicks());

    captured.add ({ ms, juce::jlimit (0, 127, key), juce::jlimit (0, 127, velocity), on && velocity > 0, patternTick });
}

std::vector<Note> AppController::capturedPhrase (double bpm, bool& inTime) const
{
    return captured.phrase (bpm, juce::Time::getMillisecondCounterHiRes(), inTime);
}

bool AppController::liveNote (const juce::String& channel, int key, int velocity, bool on)
{
    const auto target = channel.isNotEmpty() ? channel : liveChannelId;
    rememberLiveNote (key, velocity, on, juce::Time::getMillisecondCounterHiRes());

    // gravando: a nota entra no take com o tempo de agora (o teclado-piano e o live.note passam aqui)
    const bool recorded = recording != nullptr && recording->noteEvent (target, juce::jlimit (0, 127, key), velocity, on);
    auto* track = projector->trackFor (target);

    if (track == nullptr || headless)
        return recorded;

    const auto note = juce::jlimit (0, 127, key);
    track->injectLiveMidiMessage (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocity))
                                     : juce::MidiMessage::noteOff (1, note),
                                  liveSource);
    return true;
}

//==============================================================================
// Gravação (entrega 3.5)
juce::File AppController::takeFolder() const
{
    return model.file != juce::File() ? model.projectFolder().getChildFile ("gravacoes")
                                      : dataFolder.getChildFile ("Gravacoes");
}

juce::Result AppController::beginRecording (const RecordEngine::Start& start)
{
    return recording->begin (model.get(), liveChannelId, takeFolder(), start);
}

juce::Result AppController::startRecordingNow()
{
    if (recording == nullptr)
        return juce::Result::fail ("Gravação indisponível aqui."_t);

    // R com a música tocando: vale desde a primeira amostra que chegar, até a volta do loop
    auto& transport = edit->getTransport();
    const double now = transport.getPosition().inSeconds();
    const double loopEnd = transport.looping.get() && ! songSelection().isEmpty() ? transportLoopRange().getEnd().inSeconds() : record::noLimit;
    const auto result = beginRecording ({ now, now, model.get().songMode && loopEnd > now ? loopEnd : record::noLimit,
                                          transportLoopRange().getStart().inSeconds(), true });
    applyTransportLoop();
    applyLowLatency();
    return result;
}

void AppController::cancelRecording()
{
    // cancelar o take: o arquivo é apagado, nada entra no projeto, e o transporte para
    if (recording == nullptr || ! recording->isRecording())
        return;

    recording->abort();
    recording->setMessage ("Take cancelado: o arquivo foi apagado."_t);
    stop();
    applyLowLatency();   // os efeitos que a baixa latência tirou da escuta voltam
}

void AppController::finishRecording()
{
    if (recording == nullptr || ! recording->isRecording())
        return;

    // O take vira um passo de desfazer ("Gravação"), como qualquer edição.
    const auto take = recording->finish (model.get());

    if (take.isObject())
        if (const auto result = registry.run ("record.commitTake", take); ! result.ok)
            recording->setMessage ("Não consegui guardar o take: "_t + result.error);

    applyTransportLoop();   // o loop que a gravação desligou volta
    applyLowLatency();      // e os efeitos que a baixa latência tirou da escuta
}

//==============================================================================
// Desempenho (entrega 3.5)
juce::var AppController::performance()
{
    const auto& s = performanceMonitor.snapshot();
    auto o = new juce::DynamicObject();
    o->setProperty ("cpu", std::round (s.cpu * 1000.0f) / 1000.0f);
    o->setProperty ("cpuPeak", std::round (s.cpuPeak * 1000.0f) / 1000.0f);
    o->setProperty ("xruns", s.xruns);
    o->setProperty ("secondsSinceXrun", s.secondsSinceXrun);
    o->setProperty ("overloaded", s.overloaded);

    if (auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice(); device != nullptr && ! headless)
    {
        o->setProperty ("sampleRate", device->getCurrentSampleRate());
        o->setProperty ("bufferSize", device->getCurrentBufferSizeSamples());
    }

    juce::Array<juce::var> frozen;

    for (auto& m : model.get().mixer)
    {
        if (! m.frozen)
            continue;

        auto f = new juce::DynamicObject();
        f->setProperty ("track", m.id);
        f->setProperty ("name", m.name);
        f->setProperty ("frozen", projector->isFrozen (m.id));   // false com headless = o motor ainda não renderizou ou recusou
        f->setProperty ("problem", projector->freezeProblems()[m.id]);
        frozen.add (juce::var (f));
    }

    o->setProperty ("frozen", frozen);
    return juce::var (o);
}

bool AppController::previewFile (const juce::File& file)
{
    if (headless || engine.getDeviceManager().deviceManager.getCurrentAudioDevice() == nullptr)
        return false;

    return preview.play (file, engine.getAudioFileFormatManager().readFormatManager);
}

} // namespace whynot
