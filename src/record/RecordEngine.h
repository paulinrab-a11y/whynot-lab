#pragma once

#include <JuceHeader.h>

#include "InputRecorder.h"
#include "TakeMath.h"
#include "../model/Project.h"

#include <map>
#include <optional>

namespace whynot
{

namespace te = tracktion;

/** A gravação de áudio e MIDI (entrega 3.5), na thread de mensagem.

    - Áudio: a trilha de mixer armada recebe a entrada da placa. Cada take vira um canal de áudio
      novo (o canal de áudio tem um arquivo só), que sai por essa trilha, com um clipe na playlist.
    - MIDI: as notas tocadas ao vivo nos canais armados (sem canal armado, no canal ao vivo) viram
      notas do padrão atual, somadas às que já estavam.
    - Punch: só fica o que caiu entre punchIn e punchOut.
    - Latência: o take vai para trás pela entrada + saída da placa + o atraso do grafo + o ajuste manual;
      as notas, pela saída + o grafo (ver TakeMath.h).

    Os tempos passam pelo relógio da placa (amostras desde que ela abriu). Com janela, o relógio é o do
    DeviceManager do Tracktion, e o ponto em comum com a linha do tempo é medido enquanto o transporte
    toca. Sem janela (autoteste), não há placa: a taxa e as latências são simuladas, o relógio começa em
    0 junto com o play, e quem empurra a entrada (input().pushInput) e as notas é o próprio teste.

    O que foi gravado sai de finish() como os parâmetros do comando record.commitTake: guardar o take no
    modelo é um passo de desfazer como qualquer outro.
*/
class RecordEngine
{
public:
    RecordEngine (te::Engine&, te::Edit&, const juce::File& dataFolder, bool headless);
    ~RecordEngine();

    //==========================================================================
    // Ajustes da máquina (não do projeto): ficam em gravacao.json, na pasta de dados do app.
    struct Settings
    {
        double adjustMs = 0.0;   // ajuste manual da compensação; positivo = o take vai mais para trás
        int firstInput = 0;      // 0 = entrada 1 da placa
        bool stereo = false;     // false = mono (a entrada firstInput); true = o par firstInput e firstInput + 1
        int countInBars = 0;     // contagem antes de gravar: 0, 1 ou 2 compassos (Ctrl+P liga e desliga)
        int countInLength = 1;   // o tamanho que o Ctrl+P liga (o último escolhido: 1 ou 2)
        bool lowLatency = false; // gravando com a monitoração: os efeitos com atraso saem do que se ouve (o take é seco)
        juce::String mic;        // o assistente (fatia 7): "" = não sei, "condenser", "dynamic" ou "usb"
        juce::String inputDevice;   // o microfone no Windows Audio (o aparelho de entrada): "" = o padrão do Windows
        int headphones = 0;         // a escuta pelo app (fatia 8): 0 = ainda não perguntei; 1 = de fone; 2 = nas caixas
    };

    const Settings& settings() const noexcept       { return config; }
    void setAdjustMs (double ms);
    void setInput (int firstInput, bool stereo);

    /** Contagem antes de gravar: 0 desliga; 1 ou 2 compassos. */
    void setCountIn (int bars);

    /** Baixa latência ao gravar (ver AppController::applyLowLatency). */
    void setLowLatency (bool on);

    /** O tipo de microfone do assistente: "", "condenser", "dynamic" ou "usb". */
    void setMic (const juce::String& type);

    /** O microfone (o aparelho de entrada) nos drivers que separam entrada e saída, como o Windows Audio:
        "" = o padrão do Windows. No ASIO não vale: a entrada é a própria placa. Com a entrada já aberta, ela
        reabre no aparelho novo. */
    juce::Result setInputDevice (const juce::String& name);

    /** Os aparelhos de entrada do driver aberto, para a tela e a IA. separate = false no ASIO (a entrada é a
        própria placa); current = o que grava agora (o escolhido, se está ligado; senão o padrão do Windows). */
    struct InputDevices
    {
        bool separate = false;
        juce::StringArray names;
        juce::String defaultName, current;
    };

    InputDevices inputDevices() const;

    /** A escuta "Quando armada" só vale de fone (fatia 8): 1 = de fone, 2 = nas caixas, 0 = perguntar de novo. */
    void setHeadphones (int answer);

    /** Muda quando a escuta precisa saber se a pessoa está de fone: a tela pergunta "Está de fone?". */
    int headphoneQuestionSerial() const noexcept     { return headphoneQuestions; }

    /** Sem janela: o driver e o aparelho de entrada simulados (o assistente e a escuta reconhecem por eles). */
    void simulateDeviceName (const juce::String& driver, const juce::String& device)   { simulatedDriver = driver; simulatedDevice = device; }

    /** O assistente de gravação (fatia 7): a placa reconhecida pelo nome que o driver dá, o tipo de microfone e
        os ajustes recomendados, para a tela e a IA. deviceType e deviceName vazios = a placa aberta agora. */
    juce::var guide (const juce::String& deviceType = {}, const juce::String& deviceName = {}) const;

    //==========================================================================
    // Monitoração pelo app (gravar voz, 21/10): a entrada da placa toca pelas trilhas que monitoram
    // (MixerTrack::monitorsInput), com os efeitos delas. O take continua seco: o gravador pega a entrada
    // no callback dele, antes de tudo.

    /** Liga a entrada escolhida nessas trilhas e desliga das outras. trackFor dá a trilha do motor de
        uma trilha do mixer. Com janela, a cada sync e a cada 100 ms (a placa pode ter mudado); sem
        janela não há placa, e só a lista fica guardada (record.state). */
    void syncMonitoring (const Project&, const std::function<te::AudioTrack* (const juce::String&)>& trackFor);

    /** As trilhas do mixer que estão tocando a entrada agora. */
    const juce::StringArray& monitoredTracks() const noexcept   { return monitoring; }

    /** Sem janela: placa simulada (taxa e latências em amostras). Com janela, vale a placa de verdade. */
    void simulateDevice (double sampleRate, int inputLatencySamples, int outputLatencySamples, int graphLatencySamples = 0);

    /** Com janela: abre a entrada da placa (a padrão do driver), se ainda estiver fechada. */
    juce::Result ensureInputOpen();

    //==========================================================================
    // Estado
    bool isRecordMode() const noexcept              { return recordMode; }
    void setRecordMode (bool on) noexcept           { recordMode = on; }
    bool isRecording() const noexcept               { return session.has_value(); }

    /** O estado para a tela e a IA (comando record.state). */
    juce::var describe (const Project&) const;

    /** Pico da entrada desde a última leitura (0 a 1+), gravando ou não. Leitor 0: o medidor da trilha armada;
        1: o do assistente de gravação. */
    float takeInputPeak (int reader = 0) noexcept   { return recorder.takePeak (reader); }

    /** A última mensagem (por que não gravou, take curto demais...). */
    const juce::String& lastMessage() const noexcept { return message; }
    void setMessage (const juce::String& text)       { message = text; ++messageCount; }

    /** Muda a cada mensagem nova (mesmo repetida): a tela mostra quando muda. */
    int messageSerial() const noexcept               { return messageCount; }

    //==========================================================================
    // Sessão (thread de mensagem)

    /** Onde a gravação começa e até onde vale (segundos da linha do tempo). */
    struct Start
    {
        double recordFrom = 0.0;            // onde o take começa a valer (a agulha, a seleção de tempo)
        double playFrom = 0.0;              // onde o transporte começa a tocar (antes de recordFrom, com a contagem)
        double passEnd = record::noLimit;   // o take de áudio vale até aqui (a volta do loop); as notas dão a volta
        double loopStart = 0.0;             // com passEnd: onde o loop recomeça; cada volta vira um take (comping)
        bool onTheFly = false;              // o transporte já está tocando: vale desde a primeira amostra que chegar
    };

    /** Começa a gravar o que estiver armado. */
    juce::Result begin (const Project&, const juce::String& liveChannel, const juce::File& takeFolder, const Start&);

    /** As entradas da placa, pelo nome que o driver dá (sem janela, duas simuladas). */
    juce::StringArray inputNames() const;

    /** Gravando: onde o take começa a valer (a contagem toca antes disso). */
    double recordFromSeconds() const noexcept       { return session.has_value() ? session->startSeconds : 0.0; }

    /** Para e devolve os parâmetros de record.commitTake, ou var() se nada ficou (o motivo em lastMessage). */
    juce::var finish (const Project&);

    /** Descarta a sessão sem guardar nada (o app está fechando). */
    void abort();

    /** Thread de mensagem, a cada 100 ms: mede o ponto em comum entre o relógio e a linha do tempo e
        recolhe as notas dos controladores MIDI (elas tocam o canal ao vivo, e é nele que entram no take). */
    void service (const juce::String& liveChannel);

    /** Teste: o ponto em comum entre o relógio simulado e a linha do tempo (o padrão é 0 = início do play). */
    void setSync (double streamSample, double timelineSeconds);

    /** Uma nota ao vivo. Entra no take se o canal estiver gravando. streamSeconds < 0 = agora.
        Devolve true se a nota entrou no take. */
    bool noteEvent (const juce::String& channel, int key, int velocity, bool on, double streamSeconds = -1.0);

    /** Capture MIDI (bloco 3, 23/10): cada nota do controlador, gravando ou não, com há quanto tempo ela chegou
        (ms). Thread de mensagem, no service(). */
    std::function<void (int key, int velocity, bool on, double ageMs)> onLiveNote;

    /** A entrada de áudio: a placa (com janela) ou o autoteste chamam pushInput. */
    record::InputRecorder& input() noexcept          { return recorder; }

    /** Taxa e latências em uso (da placa ou simuladas). */
    double sampleRate() const;
    record::Latency latency() const;

private:
    struct DeviceInput;
    struct MidiTap;

    struct Session
    {
        double startSeconds = 0.0;
        double regionStart = -record::noLimit, regionEnd = record::noLimit;   // o trecho pedido e o punch
        double passEnd = record::noLimit;   // áudio: a volta do loop; com ela, cada volta vira um take
        double loopStart = 0.0;             // onde a volta recomeça
        double sampleRate = 48000.0;
        record::Latency latency;
        std::optional<record::ClockSync> sync;
        record::Tempo tempo;

        juce::String audioTrack;        // trilha de mixer armada ("" = sem áudio)
        juce::String takeName;          // nome do canal do take ("Voz take 2")
        bool audioRunning = false;
        bool patternToSong = false;     // gravou áudio no PAT com a playlist vazia: o padrão vai junto para o SONG

        juce::String pattern;           // padrão atual quando começou
        record::PatternTarget target;
        std::map<juce::String, std::vector<record::NoteEvent>> notes;   // por canal; tempo no relógio, em segundos
        double lastStreamSeconds = 0.0; // o maior tempo visto (fim das notas sem "desliga", sem placa)
        int deviceRestarts = 0;         // quantas vezes a placa tinha (re)começado quando a gravação começou
        int midiDropped = 0;            // notas do controlador que não couberam na fila
    };

    double streamSecondsNow() const;
    void trySync (Session&);
    void syncMidiTaps();
    void drainMidiTaps();
    juce::File settingsFile() const       { return dataFolder.getChildFile ("gravacao.json"); }
    void loadSettings();
    void saveSettings() const;

    te::Engine& engine;
    te::Edit& edit;
    const juce::File dataFolder;
    const bool headless;

    Settings config;
    record::InputRecorder recorder;
    std::unique_ptr<DeviceInput> deviceInput;   // só com janela
    std::vector<std::unique_ptr<MidiTap>> midiTaps;   // um por controlador MIDI ligado
    juce::String liveChannelId;                 // o canal que os controladores tocam (vem do service)
    int midiDroppedTotal = 0;                   // desde que o app abriu (record.state)

    int deviceRestartCount() const noexcept;
    te::InputDeviceInstance* monitorInput (bool fixLayout);

    juce::StringArray monitoring;               // as trilhas que monitoram (o modelo)
    bool echoWarned = false;                    // o aviso de eco sai uma vez por sessão do app

    juce::String chosenInputName() const;       // o aparelho de entrada que vale (o escolhido, se está ligado; senão o padrão)
    bool builtInInput() const;                  // a entrada é a placa do computador (o assistente reconhece pelo nome)
    int headphoneQuestions = 0;                 // headphoneQuestionSerial
    bool headphoneAsked = false;                // a pergunta sai uma vez por sessão do app, até a resposta
    bool quietWarned = false;                   // o aviso da escuta desligada (sem fone, placa do PC) sai uma vez
    juce::String simulatedDriver, simulatedDevice;

    struct Simulated { double sampleRate = 48000.0; int input = 0, output = 0, graph = 0; } simulated;

    bool recordMode = false;
    std::optional<Session> session;
    juce::String message;
    int messageCount = 0;

    JUCE_DECLARE_NON_COPYABLE (RecordEngine)
};

} // namespace whynot
