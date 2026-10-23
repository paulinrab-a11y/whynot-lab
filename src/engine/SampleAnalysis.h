#pragma once

#include <JuceHeader.h>

#include "../audio/Onsets.h"
#include "../dsp/PitchDetect.h"

namespace whynot
{

/** Nota de raiz de um arquivo de áudio: em que nota ele soa e quantos cents está fora dela.

    Lê só os primeiros segundos (o bastante para o corpo de um 808 ou de uma nota) e roda
    dsp::detectSamplePitch. Leva até ~0,1 s; roda na thread de mensagem, dentro do comando
    que troca o sample, para a nota entrar no mesmo passo de desfazer.
*/
dsp::PitchEstimate detectRootOf (const juce::File& file);

/** MP3: quantos frames do começo são atraso, pela etiqueta do LAME (no quadro "Info" ou "Xing"): o
    atraso do codificador gravado nela mais o próprio quadro da etiqueta, que o decodificador do JUCE
    entrega como silêncio (1152 amostras no MPEG 1, 576 no 2). Medido no autoteste contra o WAV: o
    LAME grava 576, e o JUCE atrasa 1728 (não tem os 529 do banco de filtros de outros decodificadores).
    -1 sem a etiqueta (outro codificador) ou se não for MP3. */
int mp3PrimingFrames (const juce::File&);

/** Quanto cortar do começo de um sample decodificado para o ataque cair no tempo. Num MP3, o
    atraso do codificador (pela etiqueta; sem ela, o silêncio do começo, até 50 ms); nos outros
    formatos, nada. `channels` é o começo do arquivo ([canal][frame]), como o leitor do JUCE devolve. */
int leadingFramesToTrim (const juce::File&, const std::vector<std::vector<float>>& channels, double sampleRate);

/** O atraso de um MP3 em segundos, pela etiqueta do LAME (0 sem ela, ou se não for MP3): o que o clipe
    da playlist pula no começo para cair no tempo como o WAV. Lê só o cabeçalho do arquivo. */
double mp3EncoderDelaySeconds (const juce::File&);

/** Corta `frames` do começo de cada canal (o atraso do MP3, por leadingFramesToTrim). */
void trimLeadingFrames (std::vector<std::vector<float>>& channels, int frames);

/** A detecção vale para preencher a raiz sozinha? (achou e com confiança suficiente) */
inline bool rootIsReliable (const dsp::PitchEstimate& e) noexcept
{
    return e.found && e.confidence >= 0.5 && e.key >= 0 && e.key <= 127;
}

//==============================================================================
/** O papel de um som de kit, como o produtor organiza as pastas (Kicks, 808s, Snares, Hats...). */
enum class SampleRole { unknown, kick, bass, snare, clap, hat, openHat, perc, cymbal, fx, vocal, loop };

/** Bateria (kick, caixa, palma, chimbais, percussão, prato): nasce sem a rampa de ataque. */
inline bool isDrumRole (SampleRole r) noexcept
{
    return r == SampleRole::kick || r == SampleRole::snare || r == SampleRole::clap || r == SampleRole::hat
        || r == SampleRole::openHat || r == SampleRole::perc || r == SampleRole::cymbal;
}

/** "kick", "808", "snare"... ("" para unknown): o que os comandos devolvem. */
const char* roleId (SampleRole) noexcept;

/** Papel pelo nome de um arquivo ou de uma pasta. As palavras de bateria vêm antes de "808"
    ("808 Clap" é palma, não baixo) e "loop" vem antes de tudo; o resto segue o dsp::guessRole
    do mixer ("808", "bass", "sub" = baixo). */
SampleRole roleFromName (const juce::String& name);

/** O que o som diz, lido dos primeiros segundos do arquivo (até 6 s, como detectRootOf). */
struct SampleProfile
{
    bool valid = false;
    double seconds = 0.0;      // duração do arquivo inteiro
    double lowShare = 0.0;     // fração da energia abaixo de ~150 Hz, no primeiro segundo depois do pico
    double bodyDb = -200.0;    // nível de 300 a 450 ms depois do pico, relativo aos 50 ms do pico
    dsp::PitchEstimate pitch;  // a nota, como detectRootOf
};

/** Uma leitura só do arquivo para a nota e o perfil (a nota sai igual à de detectRootOf). */
SampleProfile analyseSample (const juce::File&);

/** Grave, longo e sustentado: soa como um 808 ou um baixo (e não como um kick curto). */
bool soundsLikeBass (const SampleProfile&) noexcept;

/** Papel final: o nome do arquivo, o nome dado ao canal e a pasta, nesta ordem;
    sem nome que diga, um som grave e longo vira 808. */
SampleRole guessSampleRole (const juce::File&, const SampleProfile&, const juce::String& givenName = {});

//==============================================================================
/** Pontos de laço sugeridos para um sample (o "Use loop points" com os pontos já achados). */
struct LoopSuggestion
{
    bool found = false;
    float start = 0.0f, end = 1.0f;   // frações do sample, como Channel::loopStart e loopEnd
    float xfadeMs = 20.0f;
    double seconds = 0.0;             // tamanho do laço
};

/** Acha um laço no corpo do som: depois do ataque e da queda de tom, onde o volume ainda está
    perto de -12 dB do pico. As duas pontas caem em cruzamentos de zero subindo (o laço fica
    em fase); com nota, o tamanho tem períodos inteiros dela. Lê o arquivo (até 30 s). */
LoopSuggestion suggestLoop (const juce::File&);

//==============================================================================
/** Fatiar: por ataque (as batidas), pela grade (fatias iguais) ou nada (o sample inteiro). */
enum class SliceMode { attack, grid, none };

struct SliceResult
{
    bool ok = false;
    std::vector<float> starts;   // começo de cada fatia, em fração do sample (a primeira é 0); vazio em "none"
    double seconds = 0.0;        // tamanho do sample
    double bpm = 0.0;            // andamento do beat: o dado, o do nome, o do tamanho ou o do projeto
    bool truncated = false;      // mais de 128 fatias: a última leva o resto
    juce::String error;
};

/** BPM escrito no nome ("Loop 140bpm", "beat_92 BPM"), ou 0. */
double bpmFromName (const juce::String& name);

/** Andamento provável de um loop pelo tamanho: meio, 1, 2, 4, 8 ou 16 compassos em 4/4, o que
    cair entre 60 e 200 BPM mais perto de `projectBpm`; 0 se nenhum cabe. */
double bpmFromLength (double seconds, double projectBpm);

/** As fatias de um arquivo (lê até 60 s). Grade: `divisionBeats` tempos por fatia (0,25 = 1/16)
    no `bpm` dado; bpm 0 = o do nome, o do tamanho ou, por último, `projectBpm`. */
SliceResult sliceFile (const juce::File&, SliceMode, audio::Sensitivity, double divisionBeats, double bpm, double projectBpm);

/** Ataques de um trecho do arquivo, em segundos do arquivo, depois do começo do trecho: o
    "picotar" do clipe de áudio da playlist. */
std::vector<double> onsetsIn (const juce::File&, double fromSeconds, double toSeconds, audio::Sensitivity);

} // namespace whynot
