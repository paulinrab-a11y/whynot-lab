#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"
#include "PluginHost.h"
#include "SampleCache.h"
#include "WhynotFx.h"
#include "WhynotPiano.h"
#include "WhynotSampler.h"
#include "WhynotSynth.h"

#include <map>
#include <optional>
#include <set>

namespace whynot
{

namespace te = tracktion;

namespace dsp { class LoudnessMeter; }
class PluginCatalog;

/** Traduz o modelo da WHYNOT para um tracktion::Edit, por reconciliação:
    sync() compara com o que já foi aplicado e só mexe no que mudou.

    - canal sampler → AudioTrack com o sampler WHYNOT; canal plugin → AudioTrack com o instrumento VST3
      (criado uma vez e mantido vivo enquanto for o mesmo plugin, para não perder o estado);
      canal de áudio → AudioTrack com WaveAudioClips;
    - modo PAT: o padrão atual em loop (repetido `loops` vezes, para o render);
    - modo SONG: cada bloco da playlist vira clipes nos canais; o padrão se repete se o bloco for maior;
    - canal mono: nota com slide prolonga a anterior até 1 tick depois do seu início (legato → glide);
      nota sem slide corta a anterior;
    - trilha de mixer → AudioTrack sem clipes, com volume, pan e saída; mute/solo calculados aqui;
    - inserts → plugins antes do volume da trilha (no master, antes do medidor), na ordem do modelo;
      envios → AuxSend depois do volume (pós-fader) e AuxReturn no começo da trilha de destino;
      sidechain → Plugin::setSidechainSourceID com a trilha que dispara;
    - automação (modo SONG) → a curva do te::AutomatableParameter do alvo (volume, pan, efeito, VST3),
      com os pontos na posição do knob; fora do SONG, ou sem curva, o parâmetro volta ao valor do modelo;
    - trilha congelada (só no Edit ao vivo) → AudioTrack::setFrozen (individualFreeze): o motor renderiza a
      trilha com os canais que entram nela e os efeitos antes do fader; o conteúdo mudou, congela de novo.

    Thread de mensagem.
*/
class Projector
{
public:
    enum class Mode { followProject, pattern, song };

    Projector (te::Edit&, SampleCache&, Mode = Mode::followProject, int loops = 1, const PluginCatalog* = nullptr);
    ~Projector();

    void sync (const Project&, const juce::File& projectFolder);

    /** Uma passada do padrão atual, em segundos. */
    double patternSeconds() const noexcept   { return currentPatternSeconds; }

    /** Duração do que toca: padrão × loops (PAT) ou a música inteira (SONG). */
    double lengthSeconds() const noexcept    { return currentLengthSeconds; }

    bool isSongMode() const noexcept         { return songMode; }

    /** Gravar automação (26/10): enquanto a pessoa mexe num controle com a gravação ligada, a curva dele fica de
        fora e o controle manda no som (chaves de AutomationTarget::key()). Refaz as curvas na hora. */
    void setAutomationHold (const std::set<juce::String>& keys, const Project&);
    const std::set<juce::String>& automationHold() const noexcept   { return automationHeld; }

    /** O valor (0 a 1) que um parâmetro VST3 tinha antes da curva, e que ele volta a ter quando a curva sai
        (26/10: sem a "automação fantasma"). Não troca um valor já guardado: a primeira curva vence. */
    void rememberPluginBase (const AutomationTarget&, float value);
    std::optional<float> pluginBaseFor (const AutomationTarget&) const;

    /** O Projector está pondo valores nos parâmetros (o sync, a curva que sai): não é a pessoa mexendo. */
    bool isApplyingParameters() const noexcept   { return applyingParameters; }

    /** O app manda no loop do transporte (gravar voz, 21/10: a seleção de tempo e a contagem, que
        começa antes do loop): o sync() deixa a faixa e o liga-desliga do loop como estão. */
    void setTransportLoopManaged (bool managed) noexcept   { transportLoopManaged = managed; }

    te::LevelMeterPlugin* meterFor (const juce::String& mixerId) const;

    /** O loudness do que sai do master (LUFS e pico real), medido no fim da cadeia, depois do medidor de
        pico (bloco 1, 22/10: o LUFS sempre à vista no mixer). Os valores são atomics: a tela lê direto. */
    dsp::LoudnessMeter* masterLoudness() const;

    te::AudioTrack* trackFor (const juce::String& channelId) const;

    /** A trilha do motor de uma trilha do mixer (a monitoração liga a entrada da placa nela), ou nullptr. */
    te::AudioTrack* mixerTrackFor (const juce::String& mixerId) const;

    /** Baixa latência ao gravar (gravar voz, 21/10): estes inserts saem do que se ouve (o plugin fica
        desligado; com o motor tirando os desligados do grafo, o atraso deles some). Vale no próximo sync(). */
    void setLowLatencyOff (std::set<juce::String> insertIds)   { lowLatencyOff = std::move (insertIds); }
    const std::set<juce::String>& getLowLatencyOff() const noexcept   { return lowLatencyOff; }

    /** O fader e o pan de uma trilha do mixer no motor (no master, o que vem antes da cadeia dele), ou nullptr.
        Pan pela lei de potência constante com o centro em 0 dB (ver applyFaderAndPan no .cpp). */
    te::VolumeAndPanPlugin* volumeFor (const juce::String& mixerId) const;

    /** O plugin vivo de um insert (nativo ou VST3), ou nullptr. */
    te::Plugin* insertPlugin (const juce::String& mixerId, const juce::String& insertId) const;

    /** O efeito nativo de um insert (para os medidores e o analisador da interface), ou nullptr. */
    WhynotFxPlugin* fxFor (const juce::String& mixerId, const juce::String& insertId) const;

    /** Comparação A/B da mixagem, fora do modelo e do desfazer: todos os inserts em bypass e os envios
        desligados, e/ou um ganho no fim do master (depois do medidor). Vale a partir do próximo sync(). */
    void setAudition (bool bypassInserts, float trimDb) noexcept   { auditionBypass = bypassInserts; auditionTrimDb = trimDb; }
    bool isAuditionBypass() const noexcept                         { return auditionBypass; }

    /** Inserts que não puderam ser criados (tipo desconhecido, plugin ausente), com o motivo. */
    const juce::StringPairArray& insertProblems() const noexcept   { return fxProblems; }


    /** O instrumento VST3 vivo de um canal kind "plugin" (ou nullptr). */
    te::Plugin* instrumentFor (const juce::String& channelId) const;

    /** O WHYNOT Synth vivo de um canal kind "synth" (ou nullptr). */
    WhynotSynthPlugin* synthFor (const juce::String& channelId) const;

    /** O WHYNOT Piano vivo de um canal com instrument "piano" (ou nullptr). */
    WhynotPianoPlugin* pianoFor (const juce::String& channelId) const;

    /** Canais com problema (sample ou plugin não encontrado, plugin bloqueado ou com erro), com o motivo. */
    const juce::StringPairArray& sampleProblems() const noexcept   { return problems; }

    /** Congelar (3.5): a trilha do mixer está congelada agora (o motor renderizou o som dela)? */
    bool isFrozen (const juce::String& mixerId) const;

    /** O WAV do som congelado de uma trilha (vazio se não está congelada). */
    juce::File freezeFileFor (const juce::String& mixerId) const;

    /** Trilhas marcadas para congelar que o motor recusou, com o motivo. */
    const juce::StringPairArray& freezeProblems() const noexcept   { return freezeIssues; }

    /** Os clipes de MP3, FLAC e OGG do Edit passam a ser lidos direto do arquivo, sem o "proxy" em WAV do
        Tracktion (caça de bugs do produtor, 21/10). O proxy só começa num Timer da thread de mensagem, e
        um render que roda nela (exportar sem "background", o --render, congelar uma trilha, abrir um
        projeto com trilha congelada) esperava por ele para sempre. O WAV e o AIFF nunca usam proxy. */
    static void readCompressedClipsDirectly (te::Edit&);

    /** O clipe toca o arquivo como ele é, no lugar e no tamanho dados: sem o auto-tempo e o auto-pitch que o
        Tracktion liga sozinho quando acha um andamento no nome do arquivo ("Beat 140bpm.wav") ou no pedaço
        ACID (caça de bugs do produtor, 21/10). Com eles, o render esperava um proxy esticado para sempre. */
    static void playFileAsIs (te::WaveAudioClip&, const te::ClipPosition&);
    /** Congelar de novo espera a pessoa parar de mexer (fluidez, 22/10): o conteúdo de uma trilha congelada
        mudou, ela volta a tocar ao vivo na hora, e o render novo sai `seconds` depois da última mudança.
        0 = na hora, no próprio sync (o padrão; o app com janela usa 1 s). */
    void setRefreezeDelay (double seconds) noexcept   { refreezeDelaySeconds = juce::jmax (0.0, seconds); }

    /** Há trilha esperando para congelar de novo, e o tempo dela já passou: quem chama faz um sync(). */
    bool isRefreezeDue() const;

    /** Trilhas que esperam o render novo (tocando ao vivo enquanto isso). */
    int pendingRefreezes() const noexcept   { return (int) pendingFreezes.size(); }

    /** Notas que um padrão toca num canal ao longo de `length` ticks (passos + piano roll, repetidos,
        com as regras de mono/slide aplicadas). Público para o autoteste e a interface. */
    static std::vector<Note> notesFor (const Project&, const Pattern&, const Channel&, int lengthTicks, int offsetTicks = 0,
                                       int seed = 0);

    /** Passos com chance (26/10): cada volta do padrão sorteia de novo, sempre igual para a mesma `seed`
        (na playlist, o começo do bloco; no PAT, 0). */
    static bool usesChance (const Pattern&, const juce::String& channelId);
    static bool usesChance (const Pattern&);

    /** No modo PAT ao vivo e com o loop ligado, um padrão com chance toca 8 voltas antes de repetir o sorteio. */
    static constexpr int chanceLoops = 8;
    int patternLoops (const Project& p, const Pattern& pattern) const noexcept
    {
        // só com o loop ligado: sem ele, o padrão toca uma volta e para, como antes
        return mode == Mode::followProject && p.loop && usesChance (pattern) ? loops * chanceLoops : loops;
    }

    /** A decisão de um passo com chance: o mesmo sorteio para os mesmos números. */
    static bool chanceHits (int chance, int seed, const juce::String& channelId, int stepIndex, int rollIndex, int loopIndex);

private:
    struct MidiSpec
    {
        int start = 0, length = 0;
        std::vector<Note> notes;   // relativas ao início do clipe
        std::vector<int> cuts;     // choke: CC 120 nestes ticks, relativos ao início do clipe
        float bendRange = 2.0f;    // pitch bend (26/10): o alcance do instrumento, para as curvas das notas virarem pitch wheel
        bool operator== (const MidiSpec&) const = default;
    };

    struct AudioSpec
    {
        int start = 0, length = 0;   // ticks na playlist
        double offsetSeconds = 0.0;  // corte do começo, no tempo do arquivo
        juce::String file;
        double stretch = 1.0;        // segundos na playlist por segundo do arquivo (PlaylistItem::stretch)
        bool tape = false;           // o esticar muda o tom junto (PlaylistItem::tape)
        double fadeIn = 0.0;         // crossfade no corte entre takes (takes::edges): o que entra começa isso antes
        double fadeOut = 0.0;        // e sobe; o que sai desce nos últimos fadeOut segundos dele
        float gainDb = 0.0f;         // o ganho do bloco (PlaylistItem::gainDb)
        double userFadeIn = 0.0;     // os fades das alças do bloco (PlaylistItem::fadeIn e fadeOut), em segundos
        double userFadeOut = 0.0;
        // voz na playlist (27/10): a curva dos fades das alças (FadeCurve), o crossfade automático quando dois
        // áudios se sobrepõem na raia (playlistedit::autoCrossfade, em segundos) e o tom do bloco em semitons
        int userFadeInCurve = 0, userFadeOutCurve = 0;
        double autoFadeIn = 0.0, autoFadeOut = 0.0;
        float pitch = 0.0f;
        bool operator== (const AudioSpec&) const = default;
    };

    struct ChannelBinding
    {
        te::AudioTrack::Ptr track;
        WhynotSamplerPlugin* sampler = nullptr;
        WhynotSynthPlugin* synth = nullptr;   // canal synth
        WhynotPianoPlugin* piano = nullptr;   // canal sampler com instrument "piano"
        te::Plugin::Ptr instrument;        // canal plugin
        juce::String pluginIdentity;       // PluginRef::identity() do instrumento criado
        int pluginTriedVersion = -1;       // versão do catálogo na última tentativa (refaz se mudar)
        juce::String kind;                 // bindingKind(): o tipo do canal e o instrumento interno
        juce::String resolvedSample;
        juce::String resolvedLayers;   // as camadas por força (bloco 4f): caminhos e forças, para saber se mudou
        juce::String missingLayers;    // as que não abriram: o aviso volta a cada sync enquanto faltarem
        std::vector<MidiSpec> midi;
        std::vector<AudioSpec> audio;
        std::vector<te::Clip::Ptr> midiClips;    // o clipe de cada MidiSpec, na mesma ordem
        std::vector<te::Clip::Ptr> audioClips;   // o clipe de cada AudioSpec, na mesma ordem
        bool built = false;
    };

    struct InsertBinding
    {
        juce::String id, kind, type;
        te::Plugin::Ptr plugin;
        juce::String identity;        // vst3: PluginRef::identity() do plugin criado
        int triedVersion = -1;        // vst3: versão do catálogo na criação (refaz se não abriu e ela mudou)
    };

    struct MixerBinding
    {
        te::AudioTrack::Ptr track;
        std::vector<InsertBinding> inserts;
        te::Plugin::Ptr auxReturn;
        std::map<juce::String, te::Plugin::Ptr> sends;   // por id da trilha de destino
    };

    te::AudioTrack::Ptr newTrack (const juce::String& name);
    te::TimePosition toTime (int ticks) const;
    void syncTempo (const Project&);
    void syncMixer (const Project&);
    void syncChannels (const Project&, const juce::File& projectFolder);
    void syncRouting (const Project&);
    void planSolo (const Project&);
    void syncMuteSolo (const Project&);
    void syncInserts (const Project&);
    void syncInsertList (MixerBinding&, te::PluginList&, const MixerTrack&, const Project&, te::Plugin* anchor);
    void syncSends (const Project&);
    te::Plugin::Ptr createPluginFor (const Insert&);
    const MixerBinding* bindingFor (const juce::String& mixerId) const;
    void rebuildClips (ChannelBinding&, const juce::String& name, std::vector<MidiSpec>, std::vector<AudioSpec>, bool everything);
    te::Clip::Ptr insertMidiClip (te::AudioTrack&, const juce::String& name, const MidiSpec&);
    void writeMidi (te::MidiClip&, const MidiSpec&);
    std::vector<MidiSpec> midiFor (const Project&, const Channel&) const;
    void addChokeCuts (const Project&, const Channel&, std::vector<MidiSpec>&) const;
    void syncInstrument (ChannelBinding&, const Channel&);
    void syncAudition();
    void syncAutomation (const Project&);
    void syncFreeze (const Project&);

    /** O que entra no som congelado de uma trilha, e só isso: os canais que saem por ela (o instrumento e o que
        eles tocam, já projetado em clipes), as trilhas que chegam nela, os efeitos e as curvas deles. Antes era o
        JSON do projeto com todos os padrões e a playlist: um passo no chimbal renderizava de novo cada trilha
        congelada, na thread da tela (achado da revisão de 22/10). */
    struct FreezeKey
    {
        double bpm = 0.0;
        int timeSigNum = 0, timeSigDen = 0;
        std::vector<Channel> channels;
        std::vector<std::vector<MidiSpec>> midi;
        std::vector<std::vector<AudioSpec>> audio;
        std::vector<MixerTrack> mixer;
        std::vector<AutomationLane> automation;
        bool audition = false;
        bool operator== (const FreezeKey&) const = default;
    };

    FreezeKey freezeKey (const Project&, const MixerTrack&) const;
    te::AutomatableParameter::Ptr parameterFor (const AutomationTarget&) const;

    te::Edit& edit;
    SampleCache& cache;
    const PluginCatalog* catalog;
    const Mode mode;
    const int loops;

    std::map<juce::String, ChannelBinding> channels;
    std::map<juce::String, MixerBinding> mixer;
    MixerBinding masterBinding;   // só os inserts do master
    te::Plugin::Ptr masterFader;  // VolumeAndPan no começo do master: o fader entra antes da cadeia
    te::Plugin::Ptr masterMeter;
    te::Plugin::Ptr masterTap;    // o efeito Medidor, escondido, por último: o LUFS do master (só mede)

    // Solo (revisão de 22/10): o que cada trilha faz, calculado uma vez por sync (planSolo).
    struct SoloPlan
    {
        std::map<juce::String, bool> mixerMuted, channelMuted;
        juce::StringArray toSink;        // fontes de sidechain caladas pelo solo: tocam para a trilha escondida
        juce::StringArray silentSends;   // trilhas que só tocam para disparar um sidechain: sem envios
    };

    SoloPlan soloPlan;
    te::AudioTrack::Ptr sidechainSink;   // trilha escondida com o fader em -inf, criada no 1º solo com sidechain
    juce::StringPairArray problems, fxProblems;
    std::map<juce::String, int> busNumbers;   // trilha que recebe envios → número do bus aux

    bool auditionBypass = false;
    float auditionTrimDb = 0.0f;
    std::set<juce::String> lowLatencyOff;   // inserts fora da escuta enquanto grava com a monitoração
    te::Plugin::Ptr auditionTrim;   // efeito "stereo" nativo no fim do master, criado só quando a audição usa ganho

    // Automação (3.5): as curvas que o projetor pôs, por parâmetro (tempo em segundos, valor do parâmetro)
    struct AppliedCurve
    {
        te::AutomatableParameter::Ptr param;
        te::Plugin::Ptr owner;                          // o parâmetro aponta para o plugin: ele não pode sumir antes
        std::vector<std::pair<double, float>> points;
    };

    std::map<te::AutomatableParameter*, AppliedCurve> automated;

    // (26/10) O valor de cada parâmetro VST3 antes da primeira curva: quando a curva sai, ele volta a ele.
    struct PluginBase
    {
        te::AutomatableParameter::Ptr param;
        te::Plugin::Ptr owner;
        float value = 0.0f;
    };

    std::map<te::AutomatableParameter*, PluginBase> pluginBases;
    std::set<juce::String> automationHeld;   // gravando: as curvas que a pessoa está segurando na mão
    bool applyingParameters = false;

    /** As curvas do volume e do pan de uma trilha do mixer, pela lei de pan (as duas andam juntas). */
    void faderCurves (const MixerTrack&, const AutomationLane* volumeLane, const AutomationLane* panLane,
                      std::map<te::AutomatableParameter*, AppliedCurve>& desired);

    // Congelar (3.5): por trilha congelada, o conteúdo que foi renderizado (mudou = congela de novo)
    std::map<juce::String, FreezeKey> frozenSignatures;

    struct PendingFreeze
    {
        FreezeKey key;              // o conteúdo da última mudança
        double changedAtMs = 0.0;   // quando ele mudou (getMillisecondCounterHiRes)
    };

    std::map<juce::String, PendingFreeze> pendingFreezes;   // trilhas tocando ao vivo até o render novo
    double refreezeDelaySeconds = 0.0;
    std::map<juce::String, std::vector<te::Plugin::Ptr>> frozenOff;   // plugins dos canais que o congelamento desligou
    juce::StringPairArray freezeIssues;

    void switchOffInputs (const juce::String& mixerId, te::AudioTrack&);
    void switchOnInputs (const juce::String& mixerId);

    double appliedBpm = -1.0;
    int appliedNum = -1, appliedDen = -1;
    bool tempoChanged = false;
    bool songMode = false;
    bool transportLoopManaged = false;
    double currentPatternSeconds = 0.0;
    double currentLengthSeconds = 0.0;
};

} // namespace whynot
