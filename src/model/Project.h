#pragma once

#include <JuceHeader.h>

#include "../common/Text.h"
#include "NoteTuneData.h"

#include <map>
#include <vector>

namespace whynot
{

inline const juce::String masterTrackId { "mx_master" };

/** Tempo musical: 960 ticks por semínima; um passo do step sequencer é uma semicolcheia. */
constexpr int ticksPerBeat = 960;
constexpr int ticksPerStep = ticksPerBeat / 4;
constexpr int ticksPerBar4 = ticksPerBeat * 4;   // compasso em 4/4

/** Teto de qualquer posição ou duração na linha do tempo: 10.000 compassos (~4,7 h a 140 BPM).
    Arquivo corrompido ou comando absurdo não passa daqui, e start + length nunca estoura o int. */
constexpr int maxTimelineTicks = 10000 * ticksPerBar4;

/** Ticks <-> segundos no andamento do projeto (um BPM só). */
inline double ticksToSeconds (double ticks, double bpm) noexcept    { return ticks / ticksPerBeat * 60.0 / juce::jmax (1.0, bpm); }
inline double secondsToTicks (double seconds, double bpm) noexcept  { return seconds * juce::jmax (1.0, bpm) / 60.0 * ticksPerBeat; }

/** Segundos arredondados no microssegundo: o .wnl regravado sai com o mesmo texto. */
inline double roundToMicroseconds (double seconds) noexcept         { return std::round (seconds * 1.0e6) / 1.0e6; }

/** Ganho de um bloco de áudio (gravar voz, 21/10): de -60 a +24 dB, numa casa decimal (o .wnl regravado sai igual). */
constexpr float minClipGainDb = -60.0f, maxClipGainDb = 24.0f;
inline float clampClipGain (float db) noexcept
{
    return std::isfinite (db) ? std::round (juce::jlimit (minClipGainDb, maxClipGainDb, db) * 10.0f) / 10.0f : 0.0f;
}

/** Esticar um bloco de áudio no tempo (21/10): quanto ele dura na playlist para cada segundo do arquivo.
    De 0,25 (4 vezes mais rápido) a 4 (4 vezes mais lento); valor ruim volta para 1. Arredondado na
    sexta casa, como o offsetSeconds, para o .wnl regravado sair igual. */
constexpr double minStretch = 0.25, maxStretch = 4.0;
inline double clampStretch (double s) noexcept
{
    return std::isfinite (s) && s > 0.0 ? std::round (juce::jlimit (minStretch, maxStretch, s) * 1.0e6) / 1.0e6 : 1.0;
}

/** Um passo do step sequencer. roll = quantas batidas cabem no passo (1 = normal). */
struct Step
{
    bool on = false;
    int velocity = 100;   // 1..127
    int roll = 1;         // 1, 2, 3, 4, 6 ou 8

    // Passos mais ricos (26/10): campos opcionais no .wnl; os padrões deixam o passo igual ao de antes.
    int chance = 100;     // 1..100: a chance de o passo tocar em cada volta (sorteio fixo: o export sai sempre igual)
    int pitch = 0;        // -24..24 semitons acima ou abaixo da nota do canal
    int pan = 0;          // -100 (esquerda) .. 100 (direita): só nos canais de sample

    bool operator== (const Step&) const = default;
};

/** Um ponto da curva de uma nota (pitch bend e mod wheel, 26/10): t em ticks desde o começo da nota; v em semitons
    no pitch (de -24 a +24) ou a posição da roda no mod (de 0 a 1). As contas ficam em src/model/NoteCurves.h. */
struct CurvePoint
{
    int t = 0;
    float v = 0.0f;

    bool operator== (const CurvePoint&) const = default;
};

/** Uma nota do piano roll. slide = desliza da nota anterior até esta (808). */
struct Note
{
    int id = 0;           // único dentro da raia (canal) do padrão
    int start = 0;        // ticks desde o início do padrão
    int length = ticksPerStep;
    int key = 60;
    int velocity = 100;
    bool slide = false;
    bool muted = false;   // mudo (Alt+M, como no FL): fica no piano roll, não toca nem sai no MIDI; campo opcional
    int pan = 0;          // -100..100; por enquanto só vem dos passos (o pan do passo) e não vai para o .wnl

    // Pitch bend e mod wheel desenhados na nota (26/10): curvas opcionais, por t. Enquanto a nota toca, o canal recebe
    // o pitch wheel e o CC1 delas; vazias = a nota não mexe. Campos opcionais ("pitch" e "mod"; não sobem o esquema).
    std::vector<CurvePoint> pitch;
    std::vector<CurvePoint> mod;

    int end() const noexcept   { return start + length; }
    bool operator== (const Note&) const = default;
};

/** Um plugin externo (VST3) guardado no projeto: quem ele é e o estado que ele mesmo salva. */
struct PluginRef
{
    juce::String name;
    juce::String manufacturer;
    juce::String format { "VST3" };
    juce::String file;            // caminho do .vst3 onde foi salvo (em outra máquina pode ser outro)
    juce::String uid;             // PluginDescription::uniqueId, em hexadecimal
    bool instrument = false;
    juce::String state;           // base64 do getStateInformation do plugin

    bool isEmpty() const noexcept     { return name.isEmpty() && file.isEmpty(); }

    /** Mesmo plugin, mesmo que o estado ou o caminho do arquivo mudem. */
    juce::String identity() const     { return format + "|" + uid + "|" + name; }

    bool operator== (const PluginRef&) const = default;
};

/** Toda fonte de som: sampler nativo, instrumento VST3 (padrões) ou canal de áudio (clipes da playlist). */
/** Uma camada por força de um canal do sampler (bloco 4f, 23/10): outro sample, tocado a partir de uma força. */
struct SampleLayer
{
    juce::String samplePath;      // relativo à pasta do projeto, ou absoluto (como o sample do canal)
    int minVelocity = 100;        // a partir desta força (2 a 127) toca esta camada

    bool operator== (const SampleLayer&) const = default;
};

constexpr int maxSampleLayers = 3;   // o sample do canal e mais 3: suave, médio, forte e muito forte

struct Channel
{
    juce::String id;
    juce::String name;
    juce::String kind { "sampler" };   // "sampler", "plugin", "synth" ou "audio"
    juce::String colour { "#A6A6A6" };
    juce::String samplePath;      // relativo à pasta do projeto, ou absoluto
    juce::String originalPath;    // de onde o sample veio antes de ser coletado
    int rootKey = 60;
    bool oneShot = true;          // true: toca o sample inteiro; false: respeita a duração da nota (808)
    bool cutItself = false;       // nota nova corta a anterior do mesmo canal (open hat)
    int chokeGroup = 0;           // 0 = nenhum; 1 a 8: nota de um canal do grupo corta os outros (o hat fechado corta o aberto)
    float swing = 0.0f;           // 0 a 100 %: atrasa os passos ímpares do step sequencer (100 % = sensação de tercina)
    bool mono = false;            // uma nota por vez; notas com slide deslizam (glide do 808)
    float glideMs = 80.0f;        // tempo do slide
    int bendRange = 2;            // pitch bend (26/10): quantos semitons o pitch wheel inteiro dobra no instrumento VST3
                                  // (tem que ser o mesmo do plugin); o WHYNOT Synth usa o parâmetro "bend" dele.
                                  // De 1 a 24; campo opcional, 2 = o padrão do MIDI

    // Sampler: forma da nota. Os padrões deixam o som igual ao de um canal antigo.
    float rootCents = 0.0f;       // quanto o sample está fora de rootKey (-50..50), achado na detecção
    float tune = 0.0f;            // afinação do canal, em semitons (0,5 = 50 cents), de -24 a +24
    float attackMs = 1.0f;
    float releaseMs = 30.0f;      // saída quando a nota acaba (respeitando a nota); campo opcional, 30 = como antes
    float decayMs = 300.0f;       // tempo até -60 dB do caminho até `sustain`
    float sustain = 1.0f;         // 1 = sem decaimento; 0 = a nota some sozinha (decay do 808)
    float pitchDrop = 0.0f;       // a nota começa tantos semitons acima e cai até ela ("drop" do 808)
    float pitchDropMs = 60.0f;
    float start = 0.0f;           // onde a leitura começa, de 0 a 0,99 do sample

    // Sampler: laço (os pontos de loop). Enquanto a nota soa, a leitura volta do fim para o começo
    // do laço, com crossfade: a nota mais longa que o sample continua soando (o 808 esticado).
    // Campos opcionais (não sobem o esquema): um app sem eles toca até o fim do sample.
    // No .wnl as pontas vão em milionésimos inteiros ("loopStartPpm"): a mesma precisão do modelo.
    bool loop = false;
    float loopStart = 0.0f;       // fração do sample, de 0 a 1 (com 6 casas)
    float loopEnd = 1.0f;         // fração do sample, depois de loopStart
    float loopXfadeMs = 20.0f;    // crossfade antes do fim do laço, de 0 a 500 ms

    // Sampler fatiado (o fatiador): cada fatia toca numa tecla, da grave para a aguda a partir de
    // sliceKey, no tom original e só até a fatia seguinte. Campos opcionais (não sobem o esquema):
    // um app sem eles toca o sample inteiro em qualquer tecla. No .wnl, em milionésimos ("slicesPpm").
    std::vector<float> slices;    // começo de cada fatia, em fração do sample com 6 casas, em ordem (a primeira é 0; até 128)
    int sliceKey = 60;            // tecla da primeira fatia (C4)
    float sliceBpm = 0.0f;        // andamento do beat fatiado (0 = não se sabe): põe as fatias no piano roll

    // Camadas por força (bloco 4f, como os pads do Drum Rack): até maxSampleLayers samples a mais, cada um a partir de
    // uma força, em ordem. A nota mais fraca que a primeira camada toca o sample do canal. Num canal fatiado, só o sample
    // do canal toca. Campo opcional ("layers" no sampler; não sobe o esquema).
    std::vector<SampleLayer> layers;

    bool mute = false;
    juce::String output;          // id da trilha de mixer
    bool armed = false;           // gravação (3.5): as notas tocadas ao vivo neste canal viram notas do padrão atual

    PluginRef plugin;             // kind "plugin": o instrumento

    /** Atraso dos passos ímpares (os 16 avos do contratempo), em ticks. */
    int swingTicks() const noexcept  { return juce::roundToInt (swing / 100.0f * (float) ticksPerStep / 3.0f); }
    // kind "synth": o WHYNOT Synth (src/synth). Valores por id de parâmetro (synth::allParams);
    // ausente = padrão. Mono e glide do synth são os campos mono e glideMs acima.
    std::map<juce::String, float> synthParams;
    juce::String synthPreset;     // último preset carregado (só informativo)

    // Instrumento interno no lugar do sample (só kind "sampler"): "" = o sample; "piano" = o WHYNOT Piano.
    // Campos opcionais (não sobem o esquema): um app sem eles abre o canal como um sampler sem sample.
    juce::String instrument;
    juce::String instrumentPreset;   // piano: "grand", "suave" ou "lofi" (audio::pianoPresets)

    // Afinação nota a nota (frente C de 27/10), só no kind "audio": as notas da voz e o que mudou em cada uma, em
    // segundos do arquivo (NoteTuneData.h). Com alguma nota editada, os blocos do canal tocam o arquivo refeito ao lado
    // do original ("Voz_nota_<código>.wav"); o original nunca muda. Campos opcionais ("noteTune"; não sobem o esquema).
    std::vector<TunedNote> tunedNotes;
    bool tuneFormantFollows = false;   // true: o timbre anda junto com o tom (como fita); false: fica (o padrão)

    bool isAudio() const noexcept    { return kind == "audio"; }
    bool isPlugin() const noexcept   { return kind == "plugin"; }
    bool isSynth() const noexcept    { return kind == "synth"; }
    bool isPiano() const noexcept    { return kind == "sampler" && instrument == "piano"; }
    bool isSliced() const noexcept   { return kind == "sampler" && ! slices.empty(); }
    bool operator== (const Channel&) const = default;
};

struct Pattern
{
    juce::String id;
    juce::String name;
    int lengthSteps = 16;
    std::map<juce::String, std::vector<Step>> steps;   // por id de canal; cada vetor tem lengthSteps itens
    std::map<juce::String, std::vector<Note>> notes;   // por id de canal; ordenadas por início

    int lengthTicks() const noexcept   { return lengthSteps * ticksPerStep; }

    const std::vector<Note>* notesFor (const juce::String& channel) const
    {
        auto it = notes.find (channel);
        return it == notes.end() || it->second.empty() ? nullptr : &it->second;
    }

    bool operator== (const Pattern&) const = default;
};

/** Um efeito num dos slots (maxInserts) de uma trilha de mixer.
    kind "whynot" = efeito nativo (type: "eq", "comp", "sat"...; ver dsp::allEffects()). */
struct Insert
{
    juce::String id;                        // "fx_" + sufixo único no projeto
    juce::String kind { "whynot" };
    juce::String type;
    bool bypass = false;
    std::map<juce::String, float> params;   // por id do parâmetro; ausente = padrão do efeito
    juce::String preset;                    // último preset carregado (só informativo)
    juce::String sidechain;                 // trilha de mixer que dispara o efeito (ex.: kick no compressor do 808)
    PluginRef plugin;                       // kind "vst3": qual plugin e o estado dele (type = nome, só para mostrar)

    bool operator== (const Insert&) const = default;
};

/** Envio para outra trilha (o bus de reverb ou de delay, por exemplo). Pós-fader (padrão): sai depois do
    volume e do pan da trilha. Pré-fader: sai antes deles, e o reverb não muda quando o fader desce. */
struct Send
{
    juce::String to;
    float levelDb = -12.0f;
    bool preFader = false;      // opcional no .wnl ("pre": true, só quando ligado; não sobe o esquema)

    bool operator== (const Send&) const = default;
};

constexpr int maxInserts = 10;   // 10 por trilha, como no FL (eram 8 até 22/10)

/** Um parâmetro que um macro move (bloco 4c, 23/10): um efeito nativo da mesma trilha e a faixa, em posição do knob
    do parâmetro (de 0 a 1, a mesma curva da automação). min maior que max inverte: o macro sobe, o parâmetro desce. */
struct MacroLink
{
    juce::String insert;            // id do efeito ("fx_...") na mesma trilha
    juce::String param;             // id do parâmetro nativo
    float min = 0.0f, max = 1.0f;

    bool operator== (const MacroLink&) const = default;
};

/** Um dos 8 macros do rack da trilha: um knob que move vários parâmetros dos efeitos dela de uma vez
    (as contas ficam em automation/Macros.h). */
struct Macro
{
    juce::String name;              // vazio = "Macro N"
    float value = 0.0f;             // 0..1
    std::vector<MacroLink> links;   // até maxMacroLinks

    bool isDefault() const noexcept   { return name.isEmpty() && value == 0.0f && links.empty(); }
    bool operator== (const Macro&) const = default;
};

constexpr int maxMacros = 8, maxMacroLinks = 16;

/** Um knob ou fader do controlador ligado a um controle do app (MIDI learn, bloco 4d, 23/10): o CC, o canal MIDI e o
    alvo. kind "mixer" (param "volume" ou "pan"), "insert" (o id de um parâmetro de efeito nativo) ou "macro" (param é o
    número, de "1" a "8"). Um CC move um alvo só, e cada alvo tem um CC só. */
struct MidiLink
{
    int channel = 0;            // 1..16; 0 = qualquer canal
    int cc = 0;                 // 0..127
    juce::String kind, track, insert, param;

    bool sameTarget (const MidiLink& o) const noexcept   { return kind == o.kind && track == o.track && insert == o.insert && param == o.param; }
    bool sameControl (const MidiLink& o) const noexcept  { return cc == o.cc && (channel == o.channel || channel == 0 || o.channel == 0); }
    bool operator== (const MidiLink&) const = default;
};

constexpr int maxMidiLinks = 128;

struct MixerTrack
{
    juce::String id;
    juce::String name;
    juce::String colour { "#A6A6A6" };
    float volumeDb = 0.0f;        // -100 = silêncio
    float pan = 0.0f;             // -1..1
    bool mute = false;
    bool solo = false;
    juce::String output;          // id de outra trilha; vazio = master
    juce::String autoFor;         // canal que criou esta trilha automaticamente
    juce::String role;            // papel na mixagem ("kick", "bass", "vocal"...); vazio = adivinhar pelo nome
    bool armed = false;           // gravação (3.5): a entrada de áudio vira um take (canal de áudio novo) que sai por aqui
    bool frozen = false;          // congelada (3.5): o som dela e dos canais que entram nela vira um WAV (economiza CPU);
                                  // os efeitos antes do fader param, o volume, o pan e os envios continuam vivos
    juce::String monitor;         // gravar voz (21/10): ouvir a entrada da placa por esta trilha, com os efeitos dela.
                                  // "" = quando armada (o padrão), "off" = nunca, "on" = sempre; campo opcional

    /** Esta trilha toca a entrada da placa agora (a monitoração pelo app)? */
    bool monitorsInput() const noexcept   { return ! isMaster() && (monitor == "on" || (monitor.isEmpty() && armed)); }
    std::vector<Insert> inserts;  // no máximo maxInserts, na ordem do sinal
    std::vector<Send> sends;
    std::vector<Macro> macros;    // o rack (bloco 4c): até maxMacros, na ordem dos knobs; os do fim sem nada não ficam.
                                  // Campo opcional no .wnl ("macros")

    bool isMaster() const noexcept { return id == masterTrackId; }

    Insert* findInsert (const juce::String& insertId)
    {
        for (auto& i : inserts)
            if (i.id == insertId)
                return &i;

        return nullptr;
    }

    const Insert* findInsert (const juce::String& insertId) const
    {
        for (auto& i : inserts)
            if (i.id == insertId)
                return &i;

        return nullptr;
    }

    Send* findSend (const juce::String& target)
    {
        for (auto& s : sends)
            if (s.to == target)
                return &s;

        return nullptr;
    }

    const Send* findSend (const juce::String& target) const
    {
        for (auto& s : sends)
            if (s.to == target)
                return &s;

        return nullptr;
    }

    bool operator== (const MixerTrack&) const = default;
};

/** Quantas raias a playlist pode ter. As raias só desenham (não tocam nem gastam o motor), então o teto é só uma
    proteção do arquivo: o FL vai até 500. A playlist cresce sozinha: sempre sobra uma raia vazia no fim (26/10). */
constexpr int maxPlaylistTracks = 999;

/** Raia visual da playlist (como no FL, não é ligada a canal nem ao mixer). */
struct PlaylistTrack
{
    juce::String id;
    juce::String name;
    juce::String recordTrack;   // gravar voz (21/10): a trilha do mixer que grava nesta raia (a entrada escolhida no
                                // cabeçalho, como o "Audio track" do FL); os takes dela caem aqui. Opcional
    juce::String colour;        // organizar a playlist (bloco 4, 23/10): a cor da raia, "#RRGGBB"; vazio = sem cor. Opcional
    bool muted = false;         // a raia inteira muda (o mudo de cada bloco continua valendo). Opcional
    bool solo = false;          // com alguma raia em solo, só as em solo tocam. Opcional

    bool operator== (const PlaylistTrack&) const = default;
};

/** A curva de um fade do bloco de áudio (PlaylistItem::fadeInCurve e fadeOutCurve). No .wnl: "convex", "concave", "s". */
enum FadeCurve : int { fadeCurveLinear = 0, fadeCurveConvex = 1, fadeCurveConcave = 2, fadeCurveS = 3 };
constexpr float maxClipPitch = 24.0f;

inline const char* fadeCurveName (int c) noexcept
{
    return c == fadeCurveConvex ? "convex" : c == fadeCurveConcave ? "concave" : c == fadeCurveS ? "s" : "linear";
}

inline int fadeCurveFromName (const juce::String& s) noexcept
{
    return s == "convex" ? fadeCurveConvex : s == "concave" ? fadeCurveConcave : s == "s" ? fadeCurveS : fadeCurveLinear;
}

/** Um bloco na playlist: um padrão (repetido se o bloco for maior que ele) ou um clipe de áudio. */
struct PlaylistItem
{
    juce::String id;
    juce::String kind { "pattern" };   // "pattern" ou "audio"
    juce::String ref;                  // id do padrão, ou do canal de áudio
    juce::String track;                // id da raia
    int start = 0;                     // ticks
    int length = ticksPerBar4;         // ticks (áudio: o arquivo toca no tempo dele, vezes o stretch; o BPM muda só quanto cabe)
    double offsetSeconds = 0.0;        // áudio: quanto do começo do arquivo foi aparado, em segundos do arquivo.
                                       // Não depende do BPM: é uma posição dentro do áudio (até o esquema 2 era em ticks)
    int patternOffset = 0;             // padrão: de onde o bloco começa dentro do padrão, em ticks (o corte e o slip
                                       // mexem nele; campo opcional, 0 = do começo, como antes)
    bool muted = false;                // mudo só neste bloco (a ferramenta Mudo da playlist); campo opcional
    double stretch = 1.0;              // áudio esticado no tempo: segundos na playlist por segundo do arquivo (2 = dura o
                                       // dobro). Não depende do BPM, como o offsetSeconds; campo opcional, 1 = normal
    bool tape = false;                 // áudio: o esticar muda o tom junto, como uma fita (senão o tom fica); opcional
    juce::String takeGroup;            // gravar voz (21/10): blocos de áudio que são takes do mesmo trecho; o que toca num
                                       // trecho é o que não está mudo, e o comping escolhe. Opcional, "" = nenhum
    int take = 0;                      // o número do take no grupo (1, 2, 3...); opcional
    float gainDb = 0.0f;               // áudio: o ganho do bloco (a alça de baixo, normalizar), de -60 a +24 dB; opcional
    double fadeIn = 0.0;               // áudio: fades nas alças do bloco, em segundos da playlist (não mudam com o BPM,
    double fadeOut = 0.0;              // como o offsetSeconds); opcionais
    // voz na playlist (27/10), todos opcionais e só no áudio:
    int fadeInCurve = 0;               // a curva de cada fade (fadeCurve*): reta, rápida, lenta ou em S
    int fadeOutCurve = 0;
    float pitch = 0.0f;                // tom do bloco em semitons (-24 a +24), sem mudar o tempo e sem canal novo
    bool reversed = false;             // o bloco toca o trecho de trás para frente (o arquivo "_invertido" ao lado), sem canal novo

    int end() const noexcept   { return start + length; }

    /** Áudio: segundos do arquivo que cabem em `seconds` da playlist (o inverso do esticar). */
    double fileSeconds (double seconds) const noexcept    { return seconds / stretch; }
    bool operator== (const PlaylistItem&) const = default;
};

/** Um marcador na régua da playlist (intro, verso, refrão...): pula a agulha e marca o trecho até o próximo. */
struct Marker
{
    juce::String id;      // "mk_" + sufixo único
    int t = 0;            // ticks da música
    juce::String name;

    bool operator== (const Marker&) const = default;
};

constexpr int maxMarkerName = 40;

/** Um ponto de automação: num tick da música, a posição do knob do parâmetro (0 a 1 na faixa dele). */
struct AutomationPoint
{
    int t = 0;          // ticks da música
    float v = 0.0f;     // 0 a 1 na faixa do parâmetro (logarítmica nas frequências, o fader no volume)
    float tension = 0.0f;   // (26/10) a curva do trecho até o próximo ponto, -1 a 1 (0 = reta); campo opcional no .wnl

    bool operator== (const AutomationPoint&) const = default;
};

/** O que uma curva de automação move.
    - kind "mixer": track + param "volume" ou "pan" (vale o master);
    - kind "insert": track + insert + param (id do parâmetro do efeito nativo, ou índice do VST3);
    - kind "channel": channel + param (índice do parâmetro do instrumento VST3 do canal). */
struct AutomationTarget
{
    juce::String kind;
    juce::String track, channel, insert, param;

    /** Uma chave só por parâmetro: uma curva por parâmetro. */
    juce::String key() const   { return kind + "/" + track + "/" + channel + "/" + insert + "/" + param; }

    bool operator== (const AutomationTarget&) const = default;
};

/** Automação (3.5): uma curva por parâmetro, em ticks da música. Toca no modo SONG, como os clipes de
    automação do FL (no PAT o parâmetro fica no valor do knob). Entre dois pontos a curva anda reta. */
struct AutomationLane
{
    juce::String id;                         // "au_" + sufixo único
    AutomationTarget target;
    std::vector<AutomationPoint> points;     // por t, um ponto por t

    bool operator== (const AutomationLane&) const = default;
};

struct Project
{
    juce::String name { "Sem título"_t };
    double bpm = 140.0;
    int timeSigNum = 4;
    int timeSigDen = 4;
    int keyRoot = 0;                   // 0 = Dó ... 11 = Si
    juce::String scale { "minor" };
    bool metronome = false;
    bool loop = true;
    bool songMode = false;             // false = toca o padrão atual (PAT); true = toca a playlist (SONG)
    juce::String currentPattern;

    // Gravação (3.5): com o punch ligado, só fica o que foi gravado entre punchIn e punchOut (ticks da música).
    bool punchOn = false;
    int punchIn = 0;
    int punchOut = ticksPerBar4;

    std::vector<Channel> channels;
    std::vector<Pattern> patterns;
    std::vector<MixerTrack> mixer;
    std::vector<PlaylistTrack> playlistTracks;
    std::vector<PlaylistItem> playlistItems;
    std::vector<Marker> markers;              // na régua da playlist, ordenados por t (campo opcional no .wnl)
    std::vector<AutomationLane> automation;   // (3.5) uma curva por alvo
    std::vector<MidiLink> midiMap;            // o MIDI learn (bloco 4d): os CCs do controlador; campo opcional no .wnl

    bool operator== (const Project&) const = default;

    const Marker* findMarker (const juce::String& id) const   { return findIn (markers, id); }
    Marker* findMarker (const juce::String& id)               { return findIn (markers, id); }

    /** Onde o trecho do marcador termina: no marcador seguinte, ou no fim da música. */
    int markerEnd (const Marker& m) const
    {
        for (auto& other : markers)
            if (other.t > m.t)
                return other.t;

        return juce::jmax (m.t + ticksPerBar4, songEndTicks());
    }

    AutomationLane* findLane (const juce::String& id)                 { return findIn (automation, id); }
    const AutomationLane* findLane (const juce::String& id) const     { return findIn (automation, id); }

    const AutomationLane* laneFor (const AutomationTarget& target) const
    {
        for (auto& l : automation)
            if (l.target.key() == target.key())
                return &l;

        return nullptr;
    }

    Channel* findChannel (const juce::String& id)                     { return findIn (channels, id); }
    const Channel* findChannel (const juce::String& id) const         { return findIn (channels, id); }
    Pattern* findPattern (const juce::String& id)                     { return findIn (patterns, id); }
    const Pattern* findPattern (const juce::String& id) const         { return findIn (patterns, id); }
    MixerTrack* findMixer (const juce::String& id)                    { return findIn (mixer, id); }
    const MixerTrack* findMixer (const juce::String& id) const        { return findIn (mixer, id); }
    PlaylistItem* findItem (const juce::String& id)                   { return findIn (playlistItems, id); }
    const PlaylistItem* findItem (const juce::String& id) const       { return findIn (playlistItems, id); }
    const PlaylistTrack* findPlaylistTrack (const juce::String& id) const { return findIn (playlistTracks, id); }

    /** A raia toca? A muda nunca; com alguma raia em solo, só as em solo (bloco 4, 23/10). Raia que não existe toca. */
    bool laneSounds (const juce::String& trackId) const
    {
        bool anySolo = false;
        const PlaylistTrack* lane = nullptr;

        for (auto& t : playlistTracks)
        {
            anySolo = anySolo || t.solo;

            if (t.id == trackId)
                lane = &t;
        }

        return lane == nullptr || (! lane->muted && (! anySolo || lane->solo));
    }

    /** O bloco toca: ele não está mudo e a raia dele toca. */
    bool itemSounds (const PlaylistItem& item) const   { return ! item.muted && laneSounds (item.track); }

    Pattern* currentPatternPtr()               { return findPattern (currentPattern); }
    const Pattern* currentPatternPtr() const   { return findPattern (currentPattern); }

    /** Destino efetivo de uma trilha: vazio ou inexistente vira master. */
    juce::String resolveOutput (const juce::String& output) const
    {
        return (output.isEmpty() || findMixer (output) == nullptr) ? masterTrackId : output;
    }

    /** Para onde o som de uma trilha de mixer vai: a saída, os envios e as trilhas
        que ela dispara por sidechain (que precisam dela processada antes). */
    std::vector<juce::String> successors (const juce::String& mixerId, bool includeSidechain = true) const
    {
        std::vector<juce::String> out;
        auto* m = findMixer (mixerId);

        if (m == nullptr || m->isMaster())
            return out;

        out.push_back (resolveOutput (m->output));

        for (auto& s : m->sends)
            out.push_back (s.to);

        if (includeSidechain)
            for (auto& other : mixer)
                for (auto& ins : other.inserts)
                    if (ins.sidechain == mixerId)
                        out.push_back (other.id);

        return out;
    }

    /** O som da trilha `from` chega em `to` por algum caminho (saída, envio ou sidechain)?
        Usado para recusar rotas que fariam o som voltar para a própria trilha. */
    bool reaches (const juce::String& from, const juce::String& to, bool includeSidechain = true) const
    {
        juce::StringArray seen;
        auto pending = successors (from, includeSidechain);

        while (! pending.empty())
        {
            const auto id = pending.back();
            pending.pop_back();

            if (id == to)
                return true;

            if (seen.contains (id))
                continue;

            seen.add (id);
            const auto next = successors (id, includeSidechain);
            pending.insert (pending.end(), next.begin(), next.end());
        }

        return false;
    }

    /** Fim da música (último bloco da playlist), em ticks; no mínimo um compasso. */
    int songEndTicks() const
    {
        int end = ticksPerBar4;

        for (auto& i : playlistItems)
            end = juce::jmax (end, i.end());

        return end;
    }

    /** A nota pertence à tonalidade do projeto? */
    bool inScale (int key) const
    {
        const int degree = ((key - keyRoot) % 12 + 12) % 12;

        for (int d : scaleDegrees (scale))
            if (d == degree)
                return true;

        return false;
    }

    static std::vector<int> scaleDegrees (const juce::String& name)
    {
        if (name == "major")           return { 0, 2, 4, 5, 7, 9, 11 };
        if (name == "minor")           return { 0, 2, 3, 5, 7, 8, 10 };
        if (name == "harmonicMinor")   return { 0, 2, 3, 5, 7, 8, 11 };
        if (name == "phrygian")        return { 0, 1, 3, 5, 7, 8, 10 };
        if (name == "dorian")          return { 0, 2, 3, 5, 7, 9, 10 };
        if (name == "pentatonicMinor") return { 0, 3, 5, 7, 10 };
        return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };   // chromatic
    }

    static juce::StringArray scaleNames()
    {
        return { "minor", "major", "harmonicMinor", "phrygian", "dorian", "pentatonicMinor", "chromatic" };
    }

    static juce::String keyName (int key)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((key % 12) + 12) % 12]) + juce::String (key / 12 - 1);
    }

private:
    template <typename T>
    static T* findIn (std::vector<T>& v, const juce::String& id)
    {
        for (auto& x : v)
            if (x.id == id)
                return &x;

        return nullptr;
    }

    template <typename T>
    static const T* findIn (const std::vector<T>& v, const juce::String& id)
    {
        for (auto& x : v)
            if (x.id == id)
                return &x;

        return nullptr;
    }
};

/** Gera um id legível e único: prefixo + 8 hexadecimais. */
inline juce::String makeId (const juce::String& prefix)
{
    return prefix + "_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()).paddedLeft ('0', 8);
}

/** Cores dos canais (borda da placa de nome): laranja, branco e cinza da marca, mais tons deles. */
inline juce::String channelColourForIndex (int index)
{
    static const char* palette[] = { "#FF751F", "#FFFFFF", "#A6A6A6", "#FFA366", "#D9D9D9", "#C25712", "#FFC299", "#6E6E6E" };
    return palette[(size_t) index % std::size (palette)];
}

} // namespace whynot
