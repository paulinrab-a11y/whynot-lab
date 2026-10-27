#pragma once

// Afinação nota a nota (frente C de 27/10): a análise de uma voz gravada (a curva do tom e as notas) e o render das
// edições com o PsolaCore. Tudo offline, numa thread de trabalho: aloca memória e não serve para a thread de áudio.
// Sem JUCE (testes em tests/notetune, compilados com o cl direto).

#include "../model/NoteTuneData.h"

#include <atomic>
#include <cstddef>
#include <vector>

namespace whynot::dsp
{

/** A curva fina do tom, um valor a cada `hopSeconds` (o quadro k fica centrado em k * hopSeconds). */
struct PitchCurve
{
    double hopSeconds = 0.005;
    std::vector<float> midi;    // nota MIDI fracionária; 0 = sem tom (silêncio, consoante, respiração)
    std::vector<float> level;   // RMS do quadro (0 a 1)

    bool voiced (size_t k) const noexcept   { return k < midi.size() && midi[k] > 0.0f; }
    double seconds() const noexcept         { return (double) midi.size() * hopSeconds; }

    /** O tom em `t` segundos, interpolado entre dois quadros com tom; 0 se não houver tom ali. */
    float at (double t) const noexcept;
};

/** Uma nota achada pela análise. */
struct NoteSegment
{
    double start = 0.0, end = 0.0;   // segundos do arquivo
    float midi = 0.0f;               // a nota média (mediana do miolo da nota)
    float driftCents = 0.0f;         // desvio lento: o quanto a nota anda em volta da média (desvio-padrão)
    float vibratoCents = 0.0f;       // profundidade do vibrato (o desvio rápido, de pico)
};

struct NoteAnalysis
{
    PitchCurve curve;
    std::vector<NoteSegment> notes;
    double sampleRate = 0.0;
    std::size_t numFrames = 0;
    bool cancelled = false;
};

struct NoteAnalysisOptions
{
    double minHz = 60.0, maxHz = 1000.0;
    double hopSeconds = 0.005;
    double minNoteSeconds = 0.06;    // mais curto que isso junta com a vizinha
    float splitSemitones = 0.75f;    // a média de 80 ms sai tanto assim da nota = nota nova
    const std::atomic<bool>* cancel = nullptr;   // ligado: a análise para e devolve vazio (a tela fechou)
};

/** Analisa `mono` (uma voz): a curva do tom (YIN a ~12 kHz, refinado na taxa cheia) e as notas. */
NoteAnalysis analyseNotes (const float* mono, std::size_t numFrames, double sampleRate, const NoteAnalysisOptions& options = {});

/** As notas da análise como notas editáveis (sem edição nenhuma). */
std::vector<TunedNote> notesFromAnalysis (const NoteAnalysis&);

/** A correção em cada quadro da curva: semitons a somar ao tom cantado e a razão da formante. */
struct Correction
{
    std::vector<float> semitones;
    std::vector<float> formant;   // razão (1 = timbre igual)
};

/** `formantFollows`: true = o timbre anda junto com o tom (como fita), mais a formante da nota; false = o timbre fica
    e só a formante da nota mexe nele. */
Correction correctionFor (const PitchCurve&, const std::vector<TunedNote>&, bool formantFollows);

/** A curva que sai depois das edições (para a tela): a curva cantada mais a correção; 0 onde não há tom. */
std::vector<float> correctedCurve (const PitchCurve&, const Correction&);

struct NoteRenderStats
{
    int regions = 0;                  // trechos refeitos pelo PSOLA
    std::size_t renderedFrames = 0;   // amostras refeitas (o resto é cópia do original)
};

/** Refaz `channels` ([canal][amostra]) com as edições: só os trechos com correção passam pelo PSOLA (com 10 ms de
    crossfade nas bordas, dentro de uma folga de 25 ms em que a correção já é zero); o resto sai igual, amostra por
    amostra. Sem nota editada, a saída é a entrada. */
std::vector<std::vector<float>> renderNotes (const std::vector<std::vector<float>>& channels, double sampleRate,
                                             const PitchCurve&, const std::vector<TunedNote>&, bool formantFollows,
                                             NoteRenderStats* stats = nullptr);

/** Frequência fundamental média (Hz) de `x` entre `from` e `to` amostras, pela YIN da análise; 0 se não houver tom.
    Para os testes medirem a nota que sai. */
double measureFundamental (const float* x, std::size_t numFrames, double sampleRate, std::size_t from, std::size_t to);

} // namespace whynot::dsp
