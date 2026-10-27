#pragma once

// Ajudas comuns do autoteste (--self-test). Cada área fica num arquivo próprio em src/app/selftest/
// e recebe o SelfTestContext; a ordem e o relatório final ficam em src/app/SelfTest.cpp.

#include <JuceHeader.h>

#include "../AppController.h"

namespace whynot::selftest
{

struct Report
{
    juce::File file;
    juce::StringArray lines;
    int failures = 0;

    void check (bool ok, const juce::String& what);
    void section (const juce::String& title);

    // grava a cada linha: se o processo cair no meio, o relatório mostra até onde chegou
    void flush() const;
};

/** O que todas as áreas recebem. Os dois últimos campos passam o resultado de uma área para outra. */
struct SelfTestContext
{
    Report& rep;
    AppController& app;
    ProjectModel& model;
    juce::File work;          // pasta do autoteste (apagada a cada execução)
    juce::File data;          // dados do app de teste: catálogo de plugins, atalhos, registro de carga

    juce::File beatWav;       // 3.1 → 3.2: o beat exportado em 2 passadas a 140 BPM ("a.wav")
    juce::File demoProject;   // 3.2 → mixagem: "Demo 3.2", o beat arranjado na playlist
};

/** Um tempo a 140 BPM, o andamento do projeto novo. */
constexpr double beat140 = 60.0 / 140.0;

juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv);

struct Audio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0;
    bool valid = false;
};

Audio readWav (const juce::File&);   // WAV, MP3 e o que mais o JUCE ler
float peakDb (const Audio&);
float rmsDb (const Audio&);
float maxDiff (const Audio&, const Audio&);

/** Frequência média por cruzamentos de zero subindo, no canal 0, entre t0 e t1 segundos. */
double measureHz (const Audio&, double t0, double t1);
float peakIn (const Audio&, double t0, double t1);

/** Correlação normalizada entre `b` (inteiro) e o trecho de `a` que começa em `startSeconds`. */
double correlation (const Audio& a, double startSeconds, const Audio& b);

juce::var noteVar (double t, double len, int key, bool slide);
juce::String channelId (const Project&, const juce::String& name);

/** steps.setGrid num canal ("x...x..." com rolls 2-8); true se deu certo. */
bool setGrid (AppController&, const juce::String& channel, const char* grid, int velocity = 100);

/** pianoroll.addNotes em tempos (beats). */
CommandResult addNotes (AppController&, const juce::String& channel, const juce::Array<juce::var>& notes);

//==============================================================================
// As áreas, na ordem do relatório (src/app/SelfTest.cpp chama nesta ordem).
void runCoreSelfTest (SelfTestContext&);          // 3.1 (núcleo)
void runCompositionSelfTest (SelfTestContext&);   // 3.2 (núcleo)
void runMixingSelfTest (SelfTestContext&);        // mixagem (Mixagem)
void runPluginsSelfTest (SelfTestContext&);       // 3.3 (núcleo)
void runFinishingSelfTest (SelfTestContext&);     // 3.4 (núcleo)
void runSamplerSelfTest (SelfTestContext&);       // sampler (Sampler)
void runIdeasSelfTest (SelfTestContext&);         // WHYNOT Ideias (Ideias)
void runSynthSelfTest (SelfTestContext&);         // WHYNOT Synth (Synth)
void runDesignSelfTest (SelfTestContext&);        // galeria da base visual (Design)
void runRecordingSelfTest (SelfTestContext&);     // 3.5: gravação (Gravação e automação)
void runAutomationSelfTest (SelfTestContext&);    // 3.5: automação (Gravação e automação)
void runPerformanceSelfTest (SelfTestContext&);   // 3.5: CPU, falhas de buffer e congelar (Gravação e automação)
void runRobustnessSelfTest (SelfTestContext&);    // comandos aleatórios, só com WHYNOT_SELFTEST_FUZZ (Sampler)
void runPianoSelfTest (SelfTestContext&);         // WHYNOT Piano: biblioteca, canal, timbres e o Salamander (Sampler)
void runGridSelfTest (SelfTestContext&);          // grade que segue o zoom no piano roll e na playlist (Sampler)
void runStretchSelfTest (SelfTestContext&);       // esticar o áudio no tempo: duração, tom, fita, gesto (Esticar áudio)
void runVoiceSelfTest (SelfTestContext&);         // gravar voz para MCs: agulha, contagem, PAT e SONG (Gravação de voz)
void runSlicingSelfTest (SelfTestContext&);       // 808 do kit, laço, fatiador e kits (Fatiar e 808)
void runProducerSelfTest (SelfTestContext&);      // caça de bugs do produtor: exportar MP3/FLAC/OGG, fechar no meio, destinos, samples (Recomendações)
void runFluiditySelfTest (SelfTestContext&);     // fluidez: paint das telas, comandos, toJson, abrir e salvar num projeto grande (Fluidez)
void runBeatKeySelfTest (SelfTestContext&);       // tom do beat e as dicas do 808 no piano roll (Tom do beat)
void runLibrarySelfTest (SelfTestContext&);       // tela Projetos: biblioteca, prévia em FLAC e deck (Projetos)
void runFavoritesSelfTest (SelfTestContext&);     // favoritos: estrelas, favoritos.json e menus (Favoritos)
void runCommunitySelfTest (SelfTestContext&);     // bloco 1 da pesquisa das comunidades: piano roll do FL, MIDI, marcadores, mixer, tap (Recomendações)
void runProducerPianoSelfTest (SelfTestContext&); // pedidos do produtor no piano roll (22/10): importar MIDI, colar em outro canal, oitava, linhas das teclas (Importar MIDI)
void runRenderPackSelfTest (SelfTestContext&);    // bloco 2 da pesquisa: renderizar no lugar e exportar para vender (Recomendações)
void runWarpCaptureSelfTest (SelfTestContext&);   // bloco 3: warp, groove, strum, Capture MIDI e reamostrar (Recomendações)
void runControlSelfTest (SelfTestContext&);       // a ponte de controle da IA (Fase 4)
void runAutomationRecordSelfTest (SelfTestContext&);   // automação completa (26/10): gravar mexendo, tensão, VST3 que volta (Automação completa)
void runPitchBendSelfTest (SelfTestContext&);     // pitch bend e mod wheel desenhados na nota: curvas, comandos, render, MIDI e a raia (Pitch bend)
void runTuneSelfTest (SelfTestContext&);          // afinação completa (27/10): render, escala nota a nota, guia e a tela (Afinação)
void runVoicePlaylistSelfTest (SelfTestContext&);  // voz na playlist (27/10): corte sem buraco, ripple, fades, tom e reverso por clipe (Voz na playlist)
void runNoteTuneSelfTest (SelfTestContext&);       // afinação nota a nota (27/10): análise, editor, edições, render não destrutivo e formante (Afinação nota a nota)
void runInvalidFilesSelfTest (SelfTestContext&);  // fecha o relatório (núcleo)

} // namespace whynot::selftest
