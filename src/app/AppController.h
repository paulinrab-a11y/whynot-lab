#pragma once

#include <JuceHeader.h>

#include "KeyMap.h"
#include "Session.h"
#include "../commands/CommandRegistry.h"
#include "../engine/BeatKey.h"
#include "../engine/PreviewPlayer.h"
#include "../engine/Projector.h"
#include "../engine/SampleCache.h"
#include "../perf/PerformanceMonitor.h"
#include "../plugins/PluginCatalog.h"
#include "../record/Capture.h"
#include "../record/RecordEngine.h"

#include <optional>

namespace whynot
{

namespace te = tracktion;

namespace piano { class Prefetch; }
class AutomationRecording;

/** Dono de tudo que não é tela: motor, Edit ao vivo, modelo, comandos e arquivos.
    A interface e o modo headless conversam com o app só por aqui.
*/
class AppController : public AppServices,
                      private ProjectModel::Listener
{
public:
    /** dataFolder: onde ficam o catálogo de plugins e o registro de carga (padrão: %APPDATA%\\WHYNOT LAB). */
    explicit AppController (bool headless, const juce::File& dataFolder = {});
    ~AppController() override;

    te::Engine& getEngine() noexcept           { return engine; }
    te::Edit& getEdit() noexcept               { return *edit; }
    ProjectModel& getModel() noexcept          { return model; }
    CommandRegistry& getCommands() noexcept    { return registry; }
    Projector& getProjector() noexcept         { return *projector; }
    SampleCache& getSampleCache() noexcept     { return cache; }   // (3.5) o autoteste renderiza a referência do congelar

    // (Fase 4) a ponte da IA: o modo, e onde gravar o arquivo de controle
    bool isHeadless() const noexcept                { return headless; }
    const juce::File& getDataFolder() const noexcept   { return dataFolder; }

    /** Executa um comando e devolve o resultado (atalho para a UI). */
    CommandResult run (const juce::String& id, const juce::var& params = {})   { return registry.run (id, params); }

    /** Posição do transporte em passos do padrão atual (para o playhead). */
    double positionInSteps() const;

    juce::File getKitFolder() const;
    juce::File getDefaultProjectsFolder() const;

    // AppServices
    void play() override;
    void playFrom (int ticks) override;
    void stop() override;
    bool isPlaying() const override;
    void setPlayStart (int ticks) override;
    int playStart() const override              { return needleTicks; }
    void setPatternStart (int ticks) override;
    int patternStart() const override;
    juce::File recordingsFolder() const override   { return takeFolder(); }
    juce::Result renderRange (const Project&, double fromSeconds, double toSeconds, const juce::File&) override;
    Project soloTrackProject (const Project&, const juce::String& mixerId, bool masterChain) const override;
    juce::File producerTag() const override;
    void setProducerTag (const juce::File&) override;

    /** Gravando com contagem: quantos tempos faltam para o take começar (0 = não está contando). */
    int countInBeatsLeft() const;
    juce::Result newProject() override;
    juce::Result openProject (const juce::File&) override;
    juce::Result saveProject (const juce::File&) override;
    juce::Result exportWav (const juce::File&, int loops, bool song) override;
    double fileDurationSeconds (const juce::File&) const override;

    juce::Array<juce::PluginDescription> knownPlugins() const override;
    juce::Array<juce::var> blockedPlugins() const override;
    juce::StringArray pluginFolders() const override;
    void setPluginFolders (const juce::StringArray&) override;
    juce::var scanPlugins (bool wait, int timeoutSeconds) override;
    bool unblockPlugin (const juce::String& file) override;
    juce::AudioPluginInstance* channelPlugin (const juce::String& channel) override;
    juce::AudioPluginInstance* insertPluginInstance (const juce::String& track, const juce::String& insert) override;
    bool showChannelPluginEditor (const juce::String& channel) override;
    void pluginStateTouched() override;
    void captureLivePluginStates() override   { capturePluginStates(); }
    bool showInsertEditor (const juce::String& track, const juce::String& insert) override;

    // AppServices: acabamento (3.4)
    void togglePause() override;
    juce::Result exportMp3 (const juce::File&, int loops, bool song, int bitrateKbps, bool vbr) override;
    juce::var exportStems (const juce::File& folder, int loops, bool song, const juce::StringArray& tracks, bool masterChain = false) override;
    juce::Result startBackgroundExport (const BackgroundExport&) override;
    juce::var backgroundExportStatus() const override;
    void cancelBackgroundExport() override;
    bool isExportingInBackground() const;
    juce::Result saveNewVersion() override;
    juce::File autosaveNow() override;
    juce::var audioDeviceInfo() const override;
    juce::Result setAudioDevice (const juce::String& type, const juce::String& device, double rate, int buffer) override;
    juce::Array<juce::var> midiInputs() const override;
    bool setMidiInputEnabled (const juce::String& nameOrId, bool) override;
    void setLiveChannel (const juce::String&) override;
    juce::String liveChannel() const override   { return liveChannelId; }
    bool liveNote (const juce::String& channel, int key, int velocity, bool on) override;
    std::vector<Note> capturedPhrase (double bpm, bool& inTime) const override;
    void clearCapture() override                { captured.clear(); }
    /** Capture MIDI: guarda uma nota tocada ao vivo (o teclado-piano, o live.note e o controlador). */
    void rememberLiveNote (int key, int velocity, bool on, double ms);
    KeyMap* keyMap() override                   { return &keys; }
    void shortcutsChanged() override;

    // AppServices: comparação A/B da mixagem
    void setAudition (bool bypassInserts, float trimDb) override;
    bool isAuditioning() const override         { return auditionOn; }
    float renderLoudness (const Project&, bool song, bool bypassInserts, float trimDb) override;

    // AppServices: gravação (3.5). Com o REC ligado, o play grava e o stop guarda o take.
    RecordEngine* recorder() override           { return recording.get(); }
    juce::Result startRecordingNow() override;
    void finishRecording() override;
    void cancelRecording() override;
    void refreshMonitoring() override   { syncMonitoring(); }

    // AppServices: desempenho (3.5)
    juce::var performance() override;
    void resetXruns() override                  { performanceMonitor.resetXruns(); }

    // AppServices: tom do beat (21/10, noite). Todo beat que entra no projeto é analisado sozinho, em segundo plano.
    BeatKeyAnalyzer* beatKey() override         { return beatKeys.get(); }

    // AppServices: o BPM e o tom dos sons do navegador (bloco 4b, 23/10), com o cache em sons-analise.json
    SoundInfoCache* soundInfo() override        { return soundInfos.get(); }
    juce::Array<juce::File> soundFolders() const override;

    // AppServices: MIDI learn (bloco 4d, 23/10). O próximo CC liga ao alvo; aprender de novo troca o alvo; 20 s sem
    // CC cancela. Os links ficam no projeto (Project::midiMap).
    void learnMidi (const MidiLink& target) override;
    void cancelMidiLearn() override;
    bool isLearningMidi() const override               { return midiLearning; }
    const MidiLink& midiLearnTarget() const noexcept   { return midiLearnFor; }

    /** Um CC do controlador (thread de mensagem; o autoteste chama direto): aprendendo, liga; senão, move o alvo ligado
        a ele. Uma rajada de CCs vira um passo de desfazer, fechado 400 ms depois do último. */
    void handleMidiControl (int channel, int cc, int value);
    void finishMidiControlGesture();

    /** O alvo em palavras: "EQ · Ganho agudo (Rack)", "Volume (Kick)", "Brilho (Rack)". */
    juce::String describeMidiTarget (const MidiLink&) const;

    // AppServices: gravar automação (26/10). Com o modo ligado, mexer num controle no play do SONG escreve a curva.
    bool canRecordAutomation() const override   { return automationRecording != nullptr; }
    void setAutomationRecording (bool on) override;
    bool isAutomationRecording() const override;
    AutomationRecording* automationRecorder() noexcept   { return automationRecording.get(); }

    /** Um aviso para o pé da janela: o MIDI learn (aprendendo, ligado, parado), a automação criada pelo botão direito. */
    std::function<void (const juce::String& text, bool isError)> onNotice;
    void notice (const juce::String& text, bool isError = false)   { if (onNotice) onNotice (text, isError); }

    /** Uma análise de beat terminou (thread de mensagem): a tela mostra o aviso e as dicas do piano roll. */
    std::function<void (const juce::File&)> onBeatKey;

    /** Espera as análises de beat na fila (até timeoutMs) e entrega os avisos agora: a captura de tela usa. */
    void finishBeatAnalysis (int timeoutMs);
    // AppServices: tela Projetos (pedidos de 21/10, item 3.3)
    library::ProjectLibrary* library() override   { return projectLibrary.get(); }

    /** Medidor de CPU e de falhas de buffer, para a tela (atualizado a cada 100 ms com janela). */
    const perf::Snapshot& performanceSnapshot() const noexcept   { return performanceMonitor.snapshot(); }

    //--------------------------------------------------------------------------
    // Acabamento, para a interface
    KeyMap& getKeyMap() noexcept                { return keys; }
    std::function<void()> onShortcutsChanged;

    /** Autosave que uma sessão anterior deixou ao cair (autosave == File() = nada). Lê e limpa. */
    Session::Recovery takeRecovery()            { return session != nullptr ? session->takeRecovery() : Session::Recovery(); }

    /** Abre um autosave como o projeto atual: salvar volta para o .wnl de onde ele veio. */
    juce::Result recoverProject (const Session::Recovery&);

    /** Menu Arquivo → "Abrir um autosave": abre um autosave escolhido à mão. Se ele veio da pasta autosave/
        de um projeto, salvar volta para esse projeto; senão, o projeto fica sem arquivo (Salvar pergunta onde). */
    juce::Result openAutosave (const juce::File& autosave);

    /** Onde o "Abrir um autosave" começa: a pasta autosave/ do projeto aberto, ou a da pasta de dados. */
    juce::File autosaveBrowseFolder() const;

    /** Prévia de um arquivo de áudio do navegador (até 30 s, por cima da música). */
    bool previewFile (const juce::File&);
    void stopPreview()                          { preview.stop(); }
    bool isPreviewing() const noexcept          { return ! headless && preview.isPlaying(); }

    /** O nome que o projeto mostra (o do arquivo, se ainda se chama "Sem título"). */
    juce::String projectTitle() const;

    /** Trilhas de mixer que viram stem quando nenhuma é escolhida: as que recebem som e não estão mudas. */
    juce::StringArray defaultStemTracks() const;

    static constexpr int autosaveMinutes = 2;
    static constexpr int autosavesToKeep = 10;

    //--------------------------------------------------------------------------
    // Plugins, para a interface
    PluginCatalog& getPluginCatalog() noexcept   { return *catalog; }
    juce::StringArray defaultPluginFolders() const   { return PluginCatalog::defaultFolders(); }
    bool isScanning() const noexcept            { return scanThread != nullptr; }

    /** Grava no modelo o estado atual dos plugins vivos (sem passo de desfazer). true se algo mudou.
        Só marca o projeto como alterado se houve chance de edição (janela de plugin aberta desde o
        último salvar, ou parâmetro mudado por comando): alguns plugins devolvem bytes diferentes
        para o mesmo som, e comparar o estado sozinho acusaria alteração à toa. */
    bool capturePluginStates();

    /** Aviso para mostrar ao abrir (ex.: plugin que derrubou o app na última vez). Lê e limpa. */
    juce::String takeStartupWarning()   { return std::exchange (startupWarning, {}); }

    /** A interface registra aqui como abrir a janela de um plugin. */
    void setPluginWindowFactory (std::function<std::unique_ptr<juce::Component> (te::Plugin&)>);

    std::function<void()> onPluginsChanged;                                     // scan terminou, bloqueou, desbloqueou
    std::function<void (const juce::String& text, double progress)> onScanProgress;   // progress < 0 = parado

    /** Toca uma nota no canal agora (prévia ao desenhar no piano roll). */
    void previewNote (const juce::String& channel, int key, int velocity);
    void togglePanel (const juce::String& panel) override   { if (onTogglePanel) onTogglePanel (panel); }

    std::function<void (const juce::String&)> onTogglePanel;

    /** A seleção de tempo da playlist (estado da tela, não vai para o .wnl): a janela Exportar oferece o trecho. */
    void setTimeSelection (juce::Range<int> r) override;
    juce::Range<int> getTimeSelection() const noexcept override     { return timeSelection; }

private:
    struct ScanThread;
    struct Housekeeping;
    class ExportRunner;
    struct DeferredSync;

    void projectChanged (const ProjectModel&) override;
    void syncProjector();
    void installPluginLoader();
    void recoverFromPluginCrash();
    void finishScan (const PluginCatalog::ScanReport&);
    juce::var describeReport (const PluginCatalog::ScanReport&) const;
    juce::File loadingMarker() const   { return dataFolder.getChildFile ("carregando-plugin.txt"); }
    juce::File autosaveFolder() const;
    void routeLiveInputs();
    void stopAudition();
    juce::Result beginRecording (const RecordEngine::Start&);
    juce::File takeFolder() const;

    // Gravar voz (21/10): de onde o play começa (a agulha, a seleção de tempo), a contagem e o loop.
    juce::Range<int> songSelection() const;     // a seleção de tempo, só no modo SONG (vazia no PAT)
    te::TimeRange transportLoopRange() const;   // a seleção no SONG; sem ela, o padrão (PAT) ou a música inteira
    record::StartPlan startPlan (int countInBars, bool forTake = false) const;   // forTake: gravando (no PAT, do começo)
    void applyTransportLoop (bool starting = false);
    void serviceTransport();

    // Monitoração pelo app e baixa latência ao gravar (gravar voz, fatia 2).
    void syncMonitoring();
    void applyLowLatency();

    juce::File dataFolder;
    std::unique_ptr<PluginCatalog> catalog;
    te::Engine engine;
    SampleCache cache;
    ProjectModel model;
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<Projector> projector;
    CommandRegistry registry;
    bool auditionOn = false;
    std::vector<MixerTrack> auditionMixer;   // o mixer quando a comparação ligou: mudou, ela desliga
    const bool headless;

    std::unique_ptr<ScanThread> scanThread;
    int scanTimeoutMs = 30000;
    bool pluginsTouched = false;   // janela de plugin aberta ou parâmetro mudado desde o último salvar
    juce::String startupWarning;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    KeyMap keys;
    std::unique_ptr<Session> session;               // só com janela: o autoteste não deixa marcas
    PreviewPlayer preview;
    std::unique_ptr<Housekeeping> housekeeping;     // prévia, entradas MIDI e autosave
    int editsSinceAutosave = 0;
    juce::String liveChannelId;
    te::EditItemID liveTrack;
    int liveInputCount = -1;
    const te::MPESourceID liveSource = te::createUniqueMPESourceID();

    std::unique_ptr<RecordEngine> recording;        // gravação (3.5); sai antes do Edit
    capture::Buffer captured;                       // Capture MIDI (bloco 3): as últimas notas tocadas ao vivo
    perf::PerformanceMonitor performanceMonitor;    // desempenho (3.5)
    std::unique_ptr<ExportRunner> exportRunner;     // exportação em segundo plano; sai primeiro no destrutor
    std::unique_ptr<DeferredSync> deferredSync;     // remontagem do motor agrupada durante um arraste
    std::unique_ptr<library::ProjectLibrary> projectLibrary;   // tela Projetos: prévias e deck; sai primeiro no destrutor
    std::unique_ptr<piano::Prefetch> pianoPrefetch; // carga do WHYNOT Piano ao abrir (só com janela)
    std::unique_ptr<AutomationRecording> automationRecording;   // (26/10) gravar automação; sai antes do motor
    bool syncPending = false;
    juce::Range<int> timeSelection;
    int needleTicks = 0;                            // a agulha: o play do modo SONG começa aqui e o stop volta para cá
    int patternStartTicks = 0;                      // (29/10) o começo do play no PAT, dentro do padrão patternStartFor
    juce::String patternStartFor;
    std::optional<double> loopHoldUntil;            // a contagem começou antes do loop: ele liga quando a agulha chegar aqui (s)

    void requestBeatKeys();
    std::unique_ptr<BeatKeyAnalyzer> beatKeys;      // tom do beat (21/10, noite): a thread de análise
    std::unique_ptr<SoundInfoCache> soundInfos;     // BPM e tom do navegador de sons (bloco 4b, 23/10)

    // MIDI learn (bloco 4d): o alvo armado e o gesto aberto pelos CCs (fechado no Housekeeping)
    void serviceMidiLearn();
    void midiNotice (const juce::String& text, bool isError = false)   { if (onNotice) onNotice (text, isError); }
    bool midiLearning = false, midiGestureOpen = false, inMidiControl = false;
    MidiLink midiLearnFor;
    double midiLearnUntil = 0.0, midiGestureUntil = 0.0, midiGestureSince = 0.0;
    juce::StringArray requestedBeats;               // arquivos já pedidos (o audio.detectKey pede de novo se o arquivo mudar)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppController)
};

} // namespace whynot
