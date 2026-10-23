#pragma once

// Contas da gravação, sem JUCE (testadas em tests/record): o relógio da placa contra a linha do tempo,
// a compensação de latência, onde o take de áudio cai na playlist e que notas o take MIDI vira.
//
// Os tempos aqui são em segundos da linha do tempo "sem dar a volta" (unlooped): com o loop ligado
// eles continuam crescendo. Quem decide o que fazer com a volta do loop é notesForPattern.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace whynot::record
{

/** Andamento único do projeto: ticks <-> segundos. */
struct Tempo
{
    double bpm = 140.0;
    int ticksPerBeat = 960;

    double secondsPerTick() const noexcept       { return 60.0 / (std::max (1.0, bpm) * ticksPerBeat); }
    double toSeconds (double ticks) const noexcept  { return ticks * secondsPerTick(); }
    double toTicks (double seconds) const noexcept  { return seconds / secondsPerTick(); }
};

/** Um ponto em comum entre o relógio da placa (amostras) e a linha do tempo, medido enquanto o
    transporte toca. Com o transporte tocando em velocidade 1, os dois andam juntos. */
struct ClockSync
{
    double streamSample = 0.0;     // posição no relógio da placa, em amostras (pode ter fração)
    double timelineSeconds = 0.0;  // a linha do tempo nesse mesmo instante
    double sampleRate = 48000.0;

    double timelineAt (double streamPosition) const noexcept
    {
        return timelineSeconds + (streamPosition - streamSample) / sampleRate;
    }
};

/** Latências, em amostras da placa, mais o ajuste manual.

    O que entra no microfone no instante em que a pessoa ouve o tempo T da música chega à gravação
    com o relógio já em T + entrada + saída + grafo (o grafo atrasa a saída quando um efeito tem
    lookahead). O take vai para trás por essa soma. O ajuste manual corrige o que o driver informa
    errado: positivo = o take vai mais para trás. */
struct Latency
{
    int inputSamples = 0;
    int outputSamples = 0;
    int graphSamples = 0;
    double adjustMs = 0.0;

    /** Quanto o take de áudio vai para trás, em segundos. */
    double audioSeconds (double sampleRate) const noexcept
    {
        return (inputSamples + outputSamples + graphSamples) / std::max (1.0, sampleRate) + adjustMs / 1000.0;
    }

    /** Quanto uma nota tocada ao vivo vai para trás: a pessoa toca junto com o que ouve, e o que
        ela ouve saiu do motor há "saída + grafo". A entrada de áudio não entra aqui. */
    double midiSeconds (double sampleRate) const noexcept
    {
        return (outputSamples + graphSamples) / std::max (1.0, sampleRate);
    }
};

constexpr double noLimit = std::numeric_limits<double>::infinity();

//==============================================================================
/** Onde começa cada bloco da entrada no relógio do motor, seja qual for a ordem dos callbacks da placa.

    O callback da gravação lê o relógio do Tracktion, que avança um bloco a cada callback dele. Se o
    Tracktion roda antes, o relógio já está no fim do bloco; se roda depois, ainda está no começo.
    Quando a placa (re)começa, os dois zeram juntos, então o primeiro bloco mostra a ordem: o relógio
    em 0 quer dizer que o Tracktion ainda não contou este bloco (roda depois). Fora de 0 é o Tracktion
    na frente, que é também o único jeito de este callback entrar com a placa já rodando.
    Thread de áudio: sem alocação nem trava. */
class BlockClock
{
public:
    /** A placa (re)começou: o próximo bloco descobre a ordem de novo. */
    void restart() noexcept   { calibrated = false; }

    /** engineNow: o relógio do motor lido agora, em amostras. Devolve o começo deste bloco. */
    int64_t blockStart (int64_t engineNow, int numSamples) noexcept
    {
        if (! calibrated)
        {
            engineFirst = engineNow != 0;
            calibrated = true;
        }

        return engineFirst ? engineNow - numSamples : engineNow;
    }

    bool isEngineFirst() const noexcept   { return engineFirst; }

private:
    bool calibrated = false, engineFirst = true;
};

/** O que fazer com um pedaço da entrada que chegou na posição `at` quando o esperado era `expected`. */
struct Stitch
{
    enum class Action
    {
        write,           // no tempo certo
        fillThenWrite,   // buraco: `silence` amostras de silêncio antes, para o resto seguir no tempo
        skipThenWrite,   // repetiu um pedaço do anterior: descarta as `skip` primeiras
        stop,            // o relógio pulou sem explicação (a placa reiniciou): o take para aqui
    };

    Action action = Action::write;
    int64_t silence = 0;
    int skip = 0;
};

/** Buraco até maxFill (uma falha de buffer) ou explicado pelo que a fila perdeu (o disco atrasou)
    vira silêncio, de qualquer tamanho: o tempo não anda errado. Pulo para trás menor que o pedaço é
    repetição. Qualquer outro pulo é a placa que recomeçou: o take termina ali, com aviso, em vez de
    seguir colado e adiantado. */
inline Stitch stitch (int64_t expected, int64_t at, int numFrames, int64_t lostNotFilled, int64_t maxFill) noexcept
{
    const int64_t jump = at - expected;

    if (jump == 0)
        return {};

    if (jump > 0 && jump <= maxFill + std::max<int64_t> (0, lostNotFilled))
        return { Stitch::Action::fillThenWrite, jump, 0 };

    if (jump < 0 && -jump < numFrames)
        return { Stitch::Action::skipThenWrite, 0, (int) -jump };

    return { Stitch::Action::stop, 0, 0 };
}

//==============================================================================
/** Onde um take de áudio fica na playlist. */
struct AudioPlacement
{
    bool valid = false;
    int startTick = 0;
    int lengthTicks = 0;
    double offsetSeconds = 0.0;   // quanto do começo do arquivo fica fora do clipe
};

/** fileStart: a primeira amostra do arquivo na linha do tempo, já compensada.
    regionStart/regionEnd: o trecho pedido (o início do play ou o punch in; o punch out), em segundos.
    O clipe começa no primeiro tick dentro do trecho em que o áudio já existe e acaba no último tick
    que ainda tem áudio. O resto da fração de tick vai para offsetSeconds: a posição é exata na amostra,
    mesmo com o início do clipe em ticks inteiros. minTicks: abaixo disso o take é curto demais. */
inline AudioPlacement placeAudio (double fileStart, double fileSeconds, double regionStart, double regionEnd,
                                  const Tempo& tempo, int minTicks)
{
    AudioPlacement p;
    const double from = std::max ({ regionStart, fileStart, 0.0 });
    const double to = std::min (regionEnd, fileStart + fileSeconds);

    if (! (to > from))
        return p;

    // tolerância de um milionésimo de tick (~0,4 ns): o punch em cima de um tick não pula para o próximo
    const int startTick = (int) std::ceil (tempo.toTicks (from) - 1.0e-6);
    const int endTick = (int) std::floor (tempo.toTicks (to) + 1.0e-6);

    if (endTick - startTick < std::max (1, minTicks))
        return p;

    p.valid = true;
    p.startTick = startTick;
    p.lengthTicks = endTick - startTick;
    p.offsetSeconds = std::max (0.0, tempo.toSeconds (startTick) - fileStart);
    return p;
}

/** As voltas de uma gravação em loop (takes e comping, 21/10): cada volta vira um take no mesmo lugar.
    O relógio da gravação anda reto (sem dar a volta): fileStart é a primeira amostra do arquivo nesse tempo,
    e na volta k a música está k voltas atrás dele. O take de cada volta fica dentro de [loopStart, loopEnd),
    cortado também por [regionStart, regionEnd) (o começo da gravação e o punch). Volta menor que minTicks
    (a que mal começou quando a pessoa parou) fica de fora. */
inline std::vector<AudioPlacement> loopPasses (double fileStart, double fileSeconds, double loopStart, double loopEnd,
                                               double regionStart, double regionEnd, const Tempo& tempo, int minTicks)
{
    std::vector<AudioPlacement> out;
    const double loopLength = loopEnd - loopStart;

    if (! (loopLength > 0.0) || ! (fileSeconds > 0.0))
        return out;

    const double from = std::max (regionStart, loopStart), to = std::min (regionEnd, loopEnd);

    for (int k = 0; k < 1000 && loopStart + k * loopLength < fileStart + fileSeconds; ++k)
        if (const auto place = placeAudio (fileStart - k * loopLength, fileSeconds, from, to, tempo, minTicks); place.valid)
            out.push_back (place);

    return out;
}

//==============================================================================
/** Onde o play e o take começam (gravar voz, 21/10). Tudo em segundos da linha do tempo. */
struct StartPlan
{
    double playFrom = 0.0;          // onde o transporte começa: a contagem fica entre playFrom e recordFrom
    double recordFrom = 0.0;        // onde o take começa a valer: a agulha ou o começo da seleção de tempo
    double passEnd = noLimit;       // o take de áudio vale até aqui nesta passada (a volta do loop)
    double countIn = 0.0;           // segundos de contagem antes de recordFrom
};

/** needle: a agulha; [selectionStart, selectionEnd): a seleção de tempo (vazia = sem seleção);
    loopEnd: onde o transporte dá a volta (noLimit = sem loop); countInBars compassos de barSeconds.
    Com seleção, o take começa nela; com o loop ligado, vale até a volta (os takes por volta são a
    fatia 3). A contagem pode começar antes do zero: o transporte toca a partir de um tempo negativo. */
inline StartPlan planStart (double needle, double selectionStart, double selectionEnd, double loopEnd,
                            int countInBars, double barSeconds)
{
    StartPlan p;
    const bool hasSelection = selectionEnd > selectionStart;
    p.recordFrom = std::max (0.0, hasSelection ? selectionStart : needle);
    p.passEnd = loopEnd > p.recordFrom ? loopEnd : noLimit;
    p.countIn = std::clamp (countInBars, 0, 2) * std::max (0.0, barSeconds);
    p.playFrom = p.recordFrom - p.countIn;
    return p;
}

/** Gravou no PAT com a playlist vazia: o padrão vai para a playlist junto com o take, do zero até
    cobrir o fim dele em voltas inteiras do padrão, para o SONG tocar o que a pessoa ouviu. */
inline int patternBlockTicks (int takeEndTicks, int patternTicks)
{
    const int pattern = std::max (1, patternTicks);
    return std::max (1, (std::max (0, takeEndTicks) + pattern - 1) / pattern) * pattern;
}

//==============================================================================
/** Uma nota tocada ao vivo, já na linha do tempo (compensada). */
struct NoteEvent
{
    double time = 0.0;
    int key = 60;
    int velocity = 100;
    bool on = true;
};

/** Uma nota pronta para o padrão, em ticks do padrão. */
struct RecordedNote
{
    int start = 0;
    int length = 0;
    int key = 60;
    int velocity = 100;

    bool operator== (const RecordedNote&) const = default;
};

/** Para onde as notas vão: o padrão atual e como a linha do tempo dá a volta. */
struct PatternTarget
{
    int patternTicks = 3840;                    // tamanho do padrão
    int loopTicks = 0;                          // 0 = sem loop; senão a linha do tempo volta ao 0 a cada loopTicks
    std::vector<std::pair<int, int>> blocks;    // modo SONG: blocos do padrão na playlist (início, duração), em ticks
};

/** Junta os "liga" e "desliga" em notas e as põe no padrão.
    - Só ficam as notas que começam dentro de [regionStart, regionEnd) (o punch); a que passa do fim é cortada nele.
    - Nota sem "desliga" até o fim da gravação acaba em stopTime.
    - Com loop, a posição dá a volta; no modo SONG ela conta a partir do bloco do padrão que está tocando
      (sem bloco ali, do começo da música), e no padrão ela dá a volta no tamanho do padrão.
    - A nota não passa do fim do padrão. */
inline std::vector<RecordedNote> notesForPattern (const std::vector<NoteEvent>& events, double stopTime,
                                                  double regionStart, double regionEnd,
                                                  const Tempo& tempo, const PatternTarget& target)
{
    struct Open { double start; int velocity; };
    std::map<int, std::vector<Open>> open;   // por nota: pode haver a mesma tecla apertada duas vezes
    struct Span { double start, end; int key, velocity; };
    std::vector<Span> spans;

    auto sorted = events;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.time < b.time; });

    for (auto& e : sorted)
    {
        if (e.on && e.velocity > 0)
        {
            open[e.key].push_back ({ e.time, e.velocity });
        }
        else if (auto it = open.find (e.key); it != open.end() && ! it->second.empty())
        {
            const auto o = it->second.front();
            it->second.erase (it->second.begin());
            spans.push_back ({ o.start, std::max (o.start, e.time), e.key, o.velocity });
        }
    }

    for (auto& [key, list] : open)
        for (auto& o : list)
            spans.push_back ({ o.start, std::max (o.start, stopTime), key, o.velocity });

    const int pattern = std::max (1, target.patternTicks);
    std::vector<RecordedNote> out;

    for (auto& s : spans)
    {
        if (s.start < regionStart || s.start >= regionEnd)
            continue;

        const double end = std::min (s.end, regionEnd);
        auto startTick = (int64_t) std::llround (tempo.toTicks (s.start));
        const auto lengthTicks = std::max<int64_t> (1, std::llround (tempo.toTicks (end)) - startTick);

        if (startTick < 0)
            continue;

        if (target.loopTicks > 0)
            startTick %= target.loopTicks;

        int64_t anchor = 0;

        for (auto& [blockStart, blockLength] : target.blocks)
            if (startTick >= blockStart && startTick < (int64_t) blockStart + blockLength)
                anchor = blockStart;

        RecordedNote n;
        n.start = (int) ((startTick - anchor) % pattern);
        n.length = (int) std::min<int64_t> (lengthTicks, pattern - n.start);
        n.key = std::clamp (s.key, 0, 127);
        n.velocity = std::clamp (s.velocity, 1, 127);
        out.push_back (n);
    }

    std::stable_sort (out.begin(), out.end(), [] (const RecordedNote& a, const RecordedNote& b)
                      { return a.start != b.start ? a.start < b.start : a.key < b.key; });
    return out;
}

} // namespace whynot::record
