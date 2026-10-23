#pragma once

#include "../model/ProjectModel.h"

#include <functional>
#include <map>
#include <optional>

namespace whynot
{

class KeyMap;
class RecordEngine;
class BeatKeyAnalyzer;
namespace library { class ProjectLibrary; }

struct CommandResult
{
    bool ok = true;
    juce::String error;
    juce::var value;

    static CommandResult success (juce::var v = {})       { return { true, {}, std::move (v) }; }
    static CommandResult fail (const juce::String& why)   { return { false, why, {} }; }
};

/** O que os comandos precisam além do modelo: transporte, arquivos, render, telas.
    A AppController implementa; nos testes headless, a versão sem janela.
*/
struct AppServices
{
    virtual ~AppServices() = default;

    virtual void play() = 0;
    /** Toca a partir de um tick da linha do tempo (a ferramenta Tocar da playlist). */
    virtual void playFrom (int /*ticks*/)   { play(); }
    virtual void stop() = 0;
    virtual bool isPlaying() const = 0;
    virtual void togglePause()   { if (isPlaying()) stop(); else play(); }   // Ctrl+Espaço: para sem voltar ao início

    // A agulha (gravar voz, 21/10): no modo SONG o play e a gravação começam dela, e o stop volta para ela.
    virtual void setPlayStart (int /*ticks*/) {}
    virtual int playStart() const   { return 0; }

    // Gravar voz, fatia 4: onde as gravações e os WAVs feitos a partir delas ficam, e o render de um trecho
    // de um projeto montado só para isso (consolidar: os blocos escolhidos, sem o mixer).
    virtual juce::File recordingsFolder() const   { return {}; }
    virtual juce::Result renderRange (const Project&, double /*fromSeconds*/, double /*toSeconds*/, const juce::File&)
    {
        return juce::Result::fail ("Render indisponível aqui."_t);
    }

    /** Renderizar no lugar (bloco 2, 23/10): o projeto como ele soa só pela saída de uma trilha do mixer, a mesma
        conta dos stems (a trilha em solo, quem dispara o sidechain dela continua processando, os buses dos envios
        dela mudos). Sem a cadeia do master quando masterChain é false. */
    virtual Project soloTrackProject (const Project& p, const juce::String& /*mixerId*/, bool /*masterChain*/) const   { return p; }

    /** A tag do produtor (exportar para vender, bloco 2): o áudio que entra no MP3 de divulgação. Preferência do
        usuário, fora do projeto; File() = sem tag. */
    virtual juce::File producerTag() const   { return {}; }
    virtual void setProducerTag (const juce::File&) {}

    virtual juce::Result newProject() = 0;
    virtual juce::Result openProject (const juce::File&) = 0;
    virtual juce::Result saveProject (const juce::File&) = 0;   // File() = salvar no arquivo atual
    virtual juce::Result exportWav (const juce::File&, int loops, bool song) = 0;   // song = playlist; senão o padrão atual

    virtual void togglePanel (const juce::String& /*panel*/) {}

    /** Duração de um arquivo de áudio, para o tamanho padrão de um clipe na playlist. */
    virtual double fileDurationSeconds (const juce::File&) const   { return 0.0; }

    //--------------------------------------------------------------------------
    // Plugins (entrega 3.3). Os padrões servem para um app sem hospedagem de plugins.
    virtual juce::Array<juce::PluginDescription> knownPlugins() const       { return {}; }
    virtual juce::Array<juce::var> blockedPlugins() const                   { return {}; }   // { file, name, reason, when }
    virtual juce::StringArray pluginFolders() const                         { return {}; }
    virtual void setPluginFolders (const juce::StringArray&)                {}
    virtual juce::var scanPlugins (bool /*wait*/, int /*timeoutSeconds*/)   { return {}; }   // relatório; "error" = falhou
    virtual bool unblockPlugin (const juce::String& /*file*/)               { return false; }

    /** Instância viva do instrumento de um canal, ou nullptr. */
    virtual juce::AudioPluginInstance* channelPlugin (const juce::String& /*channel*/)   { return nullptr; }

    /** Instância viva de um insert VST3 do mixer, ou nullptr. */
    virtual juce::AudioPluginInstance* insertPluginInstance (const juce::String& /*track*/, const juce::String& /*insert*/)   { return nullptr; }
    virtual bool showChannelPluginEditor (const juce::String& /*channel*/)               { return false; }

    /** Um parâmetro de plugin mudou: o projeto passa a ter alterações não salvas. */
    virtual void pluginStateTouched() {}

    /** Lê o estado dos plugins VST3 vivos para o modelo (sem passo de desfazer). O registro chama antes da foto
        dos comandos que removem um plugin, para o Ctrl+Z trazer o timbre de agora, e não o do último salvar. */
    virtual void captureLivePluginStates() {}

    /** Abre a janela de um efeito do mixer (nativo ou VST3). false se não há interface. */
    virtual bool showInsertEditor (const juce::String& /*track*/, const juce::String& /*insert*/)   { return false; }

    //--------------------------------------------------------------------------
    // Acabamento (entrega 3.4)
    /** MP3 com o LAME: bitrate em kbps (CBR) ou VBR de qualidade 0 (melhor) a 9. */
    virtual juce::Result exportMp3 (const juce::File&, int /*loops*/, bool /*song*/, int /*bitrateKbps*/, bool /*vbr*/)
    {
        return juce::Result::fail ("Exportar MP3 não está disponível aqui.");
    }

    /** Um WAV por trilha de mixer (cada uma em solo), na pasta. tracks vazio = as que recebem som.
        masterChain = false (padrão): os efeitos do master ficam em bypass, como nos stems para mixar ou
        masterizar em outro lugar; a soma dos stems de trilhas paralelas dá a mixagem sem a cadeia do master.
        Devolve { files: [...] } ou { error }. */
    virtual juce::var exportStems (const juce::File& /*folder*/, int /*loops*/, bool /*song*/, const juce::StringArray& /*tracks*/,
                                   bool /*masterChain*/ = false)   { return {}; }

    /** Exportação sem travar a tela (pedido do usuário, 21/10): começa e volta na hora, e o render
        roda numa thread de trabalho, um arquivo por vez. Só uma de cada vez. */
    struct BackgroundExport
    {
        juce::String kind;          // wav, mp3 ou stems
        juce::File target;          // arquivo (wav, mp3) ou pasta (stems)
        int loops = 4;
        bool song = false;
        int bitrateKbps = 320;
        bool vbr = false;
        juce::StringArray tracks;   // stems: ids das trilhas; vazio = as que recebem som
        bool masterChain = false;
        int fromTicks = 0, toTicks = 0;   // só um trecho da música (SONG); to <= from = a música inteira
        double sampleRate = 48000.0;      // wav: 44100, 48000, 88200 ou 96000
        int bitDepth = 24;                // wav: 16 (com dither), 24 ou 32 (float)

        // pack (exportar para vender, bloco 2, 23/10): numa pasta com o nome do beat (BPM e tom), o WAV limpo, o MP3
        // com a tag do produtor a cada tagEverySeconds, os stems e o zip de tudo
        juce::File tag;                   // vazio = o MP3 sai sem tag
        double tagEverySeconds = 30.0;
        float tagGainDb = 0.0f;
        bool packWav = true, packMp3 = true, packStems = false, packZip = true;
    };

    virtual juce::Result startBackgroundExport (const BackgroundExport&)
    {
        return juce::Result::fail ("Exportar em segundo plano não está disponível aqui."_t);
    }

    /** { running, progress (0 a 1), stage, step, steps, finished, ok, cancelled, message, files }. */
    virtual juce::var backgroundExportStatus() const   { return {}; }
    virtual void cancelBackgroundExport() {}

    /** Salva uma versão nova ao lado da atual ("Beat_2.wnl"), como o Ctrl+N do FL. */
    virtual juce::Result saveNewVersion()   { return juce::Result::fail ("Salve o projeto uma vez antes de criar versões."); }

    /** Grava um autosave agora (se houver mudanças desde o último). Devolve o arquivo, ou File() se não precisou. */
    virtual juce::File autosaveNow()   { return {}; }

    /** Placa de som: tipos (WASAPI, ASIO...), aparelho atual, taxa e buffer. */
    virtual juce::var audioDeviceInfo() const   { return {}; }
    virtual juce::Result setAudioDevice (const juce::String& /*type*/, const juce::String& /*device*/, double /*rate*/, int /*buffer*/)
    {
        return juce::Result::fail ("Sem placa de som neste modo.");
    }

    /** Entradas MIDI (controladores): [{ name, id, enabled }]. */
    virtual juce::Array<juce::var> midiInputs() const   { return {}; }
    virtual bool setMidiInputEnabled (const juce::String& /*nameOrId*/, bool)   { return false; }

    /** Canal que recebe as notas tocadas ao vivo (controlador MIDI e teclado do computador). */
    virtual void setLiveChannel (const juce::String&) {}
    virtual juce::String liveChannel() const   { return {}; }

    /** Toca ou solta uma nota ao vivo no canal (vazio = o canal ao vivo). false se o canal não toca. */
    virtual bool liveNote (const juce::String& /*channel*/, int /*key*/, int /*velocity*/, bool /*on*/)   { return false; }

    /** Capture MIDI (bloco 3, 23/10): a última frase tocada ao vivo (o teclado e o controlador), em notas do padrão;
        `inTime` = tocada com o padrão rodando (as notas ficam onde caíram). clearCapture esquece o que foi tocado. */
    virtual std::vector<Note> capturedPhrase (double /*bpm*/, bool& inTime) const   { inTime = false; return {}; }
    virtual void clearCapture() {}

    /** Atalhos de teclado (nullptr = sem atalhos neste modo). shortcutsChanged grava e avisa a tela. */
    virtual KeyMap* keyMap()   { return nullptr; }
    virtual void shortcutsChanged() {}

    //--------------------------------------------------------------------------
    // Comparação A/B da mixagem (Mixagem): só no motor ao vivo, fora do modelo e do desfazer.
    // Desliga sozinha ao exportar, salvar, trocar de projeto ou mudar qualquer coisa no mixer.

    /** Todos os inserts em bypass (e os envios desligados) e/ou um ganho no fim do master. */
    virtual void setAudition (bool /*bypassInserts*/, float /*trimDb*/) {}
    virtual bool isAuditioning() const   { return false; }

    /** LUFS integrado de um render do projeto (1 passada), com a audição dada; -100 = sem som ou erro. */
    virtual float renderLoudness (const Project&, bool /*song*/, bool /*bypassInserts*/, float /*trimDb*/)   { return -100.0f; }

    // Gravação (entrega 3.5). Sem gravador (nullptr), os comandos record.* recusam.
    virtual RecordEngine* recorder()   { return nullptr; }

    /** Começa a gravar com o transporte já tocando (o REC apertado no meio da música). */
    virtual juce::Result startRecordingNow()   { return juce::Result::fail ("Gravação indisponível aqui."_t); }

    /** Termina a gravação em andamento e guarda o take (o transporte continua como está). */
    virtual void finishRecording() {}

    // Gravar voz (fatia 5): cancela o take em andamento e apaga o arquivo dele.
    virtual void cancelRecording() {}

    /** A escuta pelo app: refaz quem toca a entrada (o fone ou o microfone mudaram; não é mudança do modelo). */
    virtual void refreshMonitoring() {}

    //--------------------------------------------------------------------------
    // Desempenho (entrega 3.5)
    /** CPU, falhas de buffer e trilhas congeladas: { cpu, cpuPeak, xruns, secondsSinceXrun, overloaded, frozen: [...] }. */
    virtual juce::var performance()   { return {}; }
    virtual void resetXruns() {}

    //--------------------------------------------------------------------------
    // Tom do beat (21/10, noite): a análise roda numa thread de trabalho. nullptr = audio.detectKey recusa.
    virtual BeatKeyAnalyzer* beatKey()   { return nullptr; }
    // Projetos (pedidos de 21/10, item 3.3): os projetos, as prévias em áudio e o deck.
    // nullptr = sem a tela Projetos; os comandos library.* recusam.
    virtual library::ProjectLibrary* library()   { return nullptr; }
};

struct CommandContext
{
    ProjectModel& model;
    AppServices& app;
};

struct CommandInfo
{
    juce::String id;
    juce::String title;          // em português, aparece no menu de desfazer
    juce::var params;            // JSON Schema (type: object) dos parâmetros
    bool changesModel = true;    // false: transporte, arquivos, telas
    std::function<CommandResult (CommandContext&, const juce::var&)> run;
};

/** Toda ação da WHYNOT passa por aqui: a interface, os atalhos e a IA (MCP).

    Comandos que mudam o modelo viram um passo de desfazer: guardamos o projeto
    em JSON antes e depois. Entre beginGesture() e endGesture() (um arrasto de
    fader, por exemplo) tudo vira um passo só.
*/
class CommandRegistry
{
public:
    CommandRegistry (ProjectModel&, AppServices&);

    void add (CommandInfo);
    const CommandInfo* find (const juce::String& id) const;

    /** Para a IA: [{ id, title, params }] em ordem alfabética. */
    juce::var describe() const;

    CommandResult run (const juce::String& id, const juce::var& params = {});

    /** Vários comandos num passo de desfazer só, tudo ou nada (Fase 4: o command_batch da IA).
        steps = [{ id, params }]. Recusa o lote inteiro, sem rodar nada, se um id não existe, não muda o
        modelo ou é project.new/open (limpam o desfazer). Cada passo é um run() normal (normaliza e projeta
        no motor, para o passo seguinte achar a instância viva), sem empilhar desfazer; no fim entra um passo
        só, com o título. Se um passo falha, o projeto volta à foto de antes e nada entra no desfazer nem no
        refazer. Chame fora de um gesto. Devolve { results: [value de cada passo] }. */
    CommandResult runBatch (const juce::Array<juce::var>& steps, const juce::String& title);

    bool undo();
    bool redo();
    bool canUndo() const               { return undoManager.canUndo(); }
    bool canRedo() const               { return undoManager.canRedo(); }
    juce::String undoDescription() const   { return undoManager.getUndoDescription(); }
    juce::String redoDescription() const   { return undoManager.getRedoDescription(); }
    void clearHistory()                { undoManager.clearUndoHistory(); }

    void beginGesture (const juce::String& name);
    void endGesture();

    /** Há um gesto aberto (arrasto de fader, de nota...)? A ponte da IA espera ele acabar. */
    bool isInGesture() const noexcept   { return gestureDepth > 0; }

    struct ScopedGesture
    {
        ScopedGesture (CommandRegistry& r, const juce::String& name) : registry (r) { registry.beginGesture (name); }
        ~ScopedGesture() { registry.endGesture(); }
        CommandRegistry& registry;
    };

private:
    friend class SnapshotAction;
    void applySnapshot (const juce::String& json);

    ProjectModel& model;
    AppServices& app;
    std::map<juce::String, CommandInfo> commands;
    // Até 64 M caracteres de fotos (o getSizeInUnits é o tamanho do JSON), guardando no mínimo 30 passos.
    // Com 200 de mínimo, um projeto com 2 MB de estado de VST3 chegava a ~800 MB de RAM (revisão profissional, 22/10).
    juce::UndoManager undoManager { 64 * 1024 * 1024, 30 };
    int gestureDepth = 0;
    std::optional<Project> gestureStart;   // o projeto antes do primeiro comando do gesto
    int batchDepth = 0;   // > 0 dentro de um runBatch: o run() não empilha desfazer (o lote empilha um só)
};

/** Registra todos os comandos da entrega 3.1. */
void registerCoreCommands (CommandRegistry&);

/** Piano roll, tonalidade, modo song, canais de áudio e playlist (entrega 3.2). */
void registerCompositionCommands (CommandRegistry&);

/** Catálogo e scan de plugins, instrumento VST3 no channel rack e parâmetros (entrega 3.3). */
void registerPluginCommands (CommandRegistry&);

/** Efeitos nativos nos inserts, envios, papéis das trilhas e estilos de mixagem (entrega 3.3). */
void registerMixCommands (CommandRegistry&);

/** MP3, MIDI, stems, versões e autosave (entrega 3.4). */
void registerExportCommands (CommandRegistry&);

/** Placa de som, entradas MIDI e notas ao vivo (entrega 3.4). */
void registerDeviceCommands (CommandRegistry&);

/** WHYNOT Ideias: acordes, melodia, baixo, 808 e bateria por gênero; análise de acordes. */
void registerIdeasCommands (CommandRegistry&);
/** WHYNOT Synth: canal, presets, parâmetros, "Variar" e favoritos. */
void registerSynthCommands (CommandRegistry&);

/** WHYNOT Piano: canal, timbre e a biblioteca de samples (pedido de 21/10). */
void registerPianoCommands (CommandRegistry&);

/** Gravação de áudio e MIDI: armar, REC, punch, latência e o take (entrega 3.5). */
void registerRecordCommands (CommandRegistry&);

/** Automação: uma curva por parâmetro, em ticks (entrega 3.5). */
void registerAutomationCommands (CommandRegistry&);

/** Medidor de CPU e de falhas de buffer, e congelar trilha ou canal (entrega 3.5). */
void registerPerformanceCommands (CommandRegistry&);

/** Fatiador: fatiar um beat por ataque ou pela grade, as fatias no piano roll e picotar o clipe (21/10). */
void registerSliceCommands (CommandRegistry&);
/** Tom do beat: audio.detectKey (tom, confiança, alternativas e o acorde de cada compasso). */
void registerBeatCommands (CommandRegistry&);
/** Tela Projetos: listar, renderizar a prévia, ouvir sem abrir (pedidos de 21/10, item 3.3). */
void registerLibraryCommands (CommandRegistry&);
/** Favoritos (as estrelas): listar, marcar e alternar. Preferência do usuário, fora do projeto e do desfazer. */
void registerFavoritesCommands (CommandRegistry&);

//==============================================================================
// Ajudas para ler parâmetros com mensagens de erro claras.
namespace params
{
    juce::var schema (std::initializer_list<std::tuple<const char*, const char*, bool, juce::String>> fields);

    bool has (const juce::var& p, const char* key);
    juce::String string (const juce::var& p, const char* key, const juce::String& fallback = {});
    double number (const juce::var& p, const char* key, double fallback);
    bool boolean (const juce::var& p, const char* key, bool fallback);
}

} // namespace whynot
