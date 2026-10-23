#pragma once

// Tom de um beat (WAV ou MP3 já decodificado), sem JUCE e sem modelo de fora: análise de sinal feita por nós.
//
// O caminho:
//   1. mistura em mono e desce a taxa para ~11 kHz (o que importa para o tom fica abaixo de 5 kHz);
//   2. STFT (Hann de 4096 pontos, passo de 512) e a separação harmônico/percussivo pela mediana
//      (Fitzgerald, 2010): o que dura no tempo é nota, o que se espalha na frequência é batida;
//   3. picos do espectro harmônico, com a frequência interpolada; a afinação de referência sai da média
//      circular dos desvios em cents (o beat pode estar uns cents fora de 440);
//   4. cromagrama por quadro (12 classes) e um cromagrama só do grave (o 808 e o baixo);
//   5. correlação do perfil do trecho com os perfis de tom maior e menor (Krumhansl-Kessler e Temperley),
//      com um empurrão para o tom cuja tríade da tônica abre o beat e cuja tônica o grave mais toca.
//
// Roda numa thread de trabalho: aloca memória. Nunca na thread de áudio.

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace whynot::ideas
{

using Chroma = std::array<float, 12>;

/** Um tom candidato: tônica (0 = Dó) e modo, a nota do modelo e a chance (as 24 somam 1). */
struct KeyCandidate
{
    int root = 0;
    bool major = false;
    double score = 0.0;         // correlação mais os empurrões (cerca de -1 a 1,3)
    double probability = 0.0;   // 0..1

    bool operator== (const KeyCandidate& o) const noexcept   { return root == o.root && major == o.major; }
};

struct KeyDetectOptions
{
    double maxSeconds = 150.0;  // analisa só o começo (2 min 30 s): o tom de um beat não muda depois
    double startSeconds = 0.0;  // de onde começa a ler (o corte do clipe, por exemplo)
};

struct BeatAnalysis
{
    double analysisRate = 0.0;      // taxa depois de descer (Hz)
    double hopSeconds = 0.0;        // tempo entre quadros
    double startSeconds = 0.0;      // o primeiro quadro, em segundos do arquivo (o centro dele)
    double seconds = 0.0;           // quanto foi analisado
    double tuningCents = 0.0;       // afinação de referência: +30 = Lá em 447,7 Hz
    double tonalness = 0.0;         // 0..1: quanto do som tem nota (só bateria fica perto de 0)

    std::vector<Chroma> chroma;     // por quadro, toda a faixa (50 Hz a 5 kHz)
    std::vector<Chroma> bass;       // por quadro, só o grave (30 a 220 Hz)
    Chroma profile {};              // perfil do trecho todo, somado e normalizado (máximo 1)
    Chroma bassProfile {};
    Chroma opening {};              // os primeiros 1,2 s com nota (o acorde que abre o beat)
    Chroma openingBass {};

    std::vector<KeyCandidate> keys; // as 24 tonalidades, da mais provável para a menos

    bool found() const noexcept     { return ! keys.empty() && tonalness >= 0.12; }
    const KeyCandidate& best() const   { return keys.front(); }

    /** O tom candidato com esta tônica e modo (sempre existe quando found()). */
    const KeyCandidate* find (int root, bool major) const noexcept;

    /** Tempo (em segundos do arquivo) do centro do quadro i. */
    double frameTime (std::size_t i) const noexcept   { return startSeconds + (double) i * hopSeconds; }
};

/** Analisa `numSamples` amostras mono a `sampleRate`. `cancel` (opcional) interrompe: devolve vazio. */
BeatAnalysis analyzeBeat (const float* mono, std::size_t numSamples, double sampleRate,
                          const KeyDetectOptions& = {}, const std::atomic<bool>* cancel = nullptr);

/** Desce a taxa aos poucos, para ler o arquivo em pedaços sem guardar ele inteiro: passa-baixas
    (seno janelado de Blackman) e um fator inteiro até perto de 11 kHz. 2 min 30 s ficam com ~6 MB. */
class Downsampler
{
public:
    explicit Downsampler (double inputRate);

    void push (const float* mono, std::size_t numSamples);
    void finish();   // depois do último push

    double outputRate() const noexcept          { return rate; }
    std::vector<float>& samples() noexcept       { return out; }
    std::size_t received() const noexcept        { return count; }

private:
    void produce (bool final);

    int factor = 1, mid = 0;
    double rate = 0.0;
    std::vector<float> h, buffer, out;
    long long bufferStart = 0, next = 0;
    std::size_t count = 0;
};

/** A análise sobre amostras que já desceram de taxa (Downsampler). `startSeconds`: onde elas começam
    no arquivo; `inputSeconds`: quanto do arquivo elas cobrem. */
BeatAnalysis analyzeDownsampled (std::vector<float> samples, double rate, double startSeconds, double inputSeconds,
                                 const std::atomic<bool>* cancel = nullptr);

/** O acorde provável de um compasso: tríade maior ou menor, com a fundamental. */
struct BarChord
{
    int bar = 0;                // 0 = o primeiro compasso pedido
    double startSeconds = 0.0;  // em segundos do arquivo
    double endSeconds = 0.0;
    bool found = false;         // false = sem nota (silêncio ou só bateria) ou fora do que foi analisado
    int root = 0;               // a fundamental: onde o 808 casa
    bool minor = false;
    double confidence = 0.0;    // 0..1

    std::vector<int> pitchClasses() const   { return { root, (root + (minor ? 3 : 4)) % 12, (root + 7) % 12 }; }
};

/** Acordes por compasso. O compasso b vai de barStarts[b] a barStarts[b + 1] (segundos do arquivo),
    então barStarts tem um a mais que o número de compassos. `key` (opcional) dá um empurrão pequeno
    para os acordes do tom. */
std::vector<BarChord> chordsForBars (const BeatAnalysis&, const std::vector<double>& barStarts,
                                     const KeyCandidate* key = nullptr);

/** Compassos de um clipe tocado a `bpm` (4/4), começando em `firstBarSeconds` do arquivo,
    `stretch` segundos na música por segundo do arquivo (1 = normal). */
std::vector<double> barGrid (double firstBarSeconds, double bpm, int beatsPerBar, double stretch, int numBars);

/** "Lá menor", "Fá# maior" (com bemóis quando a armadura pede: "Sib maior"). */
std::string keyNamePt (int root, bool major);
/** "Am", "F#", "Bb". */
std::string chordName (int root, bool minor, bool flats);
/** Bemóis para escrever as notas deste tom? */
bool keyPrefersFlats (int root, bool major) noexcept;

/** Como `alt` se relaciona com `key`: "relativo", "paralelo", "vizinho" ou "" (outro). */
std::string keyRelation (const KeyCandidate& key, const KeyCandidate& alt);

/** O relativo: Lá menor ↔ Dó maior. */
KeyCandidate relativeOf (const KeyCandidate&) noexcept;

} // namespace whynot::ideas
