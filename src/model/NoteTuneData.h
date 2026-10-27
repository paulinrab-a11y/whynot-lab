#pragma once

// Afinação nota a nota (frente C de 27/10): as notas de uma voz gravada e o que a pessoa mudou em cada uma.
// Sem JUCE: o modelo (Channel::tunedNotes), o render (dsp/NoteTune) e os testes usam as mesmas contas.
//
// As notas ficam no canal de áudio, em segundos do arquivo: os pedaços de um bloco cortado tocam o mesmo arquivo
// e continuam afinados. Sem notas = o canal toca o arquivo original.

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace whynot
{

/** Uma nota da voz: onde ela fica no arquivo, a nota que a análise achou e as edições. */
struct TunedNote
{
    double start = 0.0, end = 0.0;   // segundos do arquivo
    float sourceMidi = 60.0f;        // a nota média cantada (MIDI fracionário, 69 = Lá 440 Hz), da análise
    float shift = 0.0f;              // semitons que a nota foi movida (arrastar: a nota cai num semitom inteiro)
    float center = 0.0f;             // 0 a 1: puxa a nota para o semitom e tira o desvio lento (a nota "torta")
    float flatten = 0.0f;            // 0 a 1: achata o vibrato (o desvio rápido)
    float formant = 0.0f;            // semitons de formante (-12 a +12): o timbre, à parte do tom
    float transitionMs = 40.0f;      // a correção entra em rampa no começo da nota (0 = salto; até 300 ms)

    double length() const noexcept   { return end - start; }
    bool edited() const noexcept     { return shift != 0.0f || center != 0.0f || flatten != 0.0f || formant != 0.0f; }

    /** A nota para onde ela vai (o meio dela, antes do desvio fino). */
    float targetMidi() const noexcept
    {
        const float base = sourceMidi + shift;
        return base + center * (std::round (base) - base);
    }

    bool operator== (const TunedNote&) const = default;
};

namespace notetune
{
    constexpr float maxShift = 24.0f;
    constexpr float maxFormant = 12.0f;
    constexpr float maxTransitionMs = 300.0f;
    constexpr double minNoteSeconds = 0.03;
    constexpr double maxSeconds = 5.0 * 60.0 * 60.0;   // 5 h: no .wnl os tempos vão em inteiros de 10 µs (cabe no int)

    inline double quantiseSeconds (double s) noexcept   { return (double) std::llround (s * 1.0e5) / 1.0e5; }   // 10 µs
    inline float quantiseHundredths (float v) noexcept  { return (float) ((double) std::lround ((double) v * 100.0) / 100.0); }

    inline bool anyEdited (const std::vector<TunedNote>& notes) noexcept
    {
        return std::any_of (notes.begin(), notes.end(), [] (const TunedNote& n) { return n.edited(); });
    }

    /** Deixa os valores nas faixas e as notas em ordem, sem sobreposição nem notas curtas demais. */
    inline void normalise (std::vector<TunedNote>& notes)
    {
        for (auto& n : notes)
        {
            auto finite = [] (float v, float fallback) { return std::isfinite (v) ? v : fallback; };
            n.shift = std::clamp (finite (n.shift, 0.0f), -maxShift, maxShift);
            n.center = std::clamp (finite (n.center, 0.0f), 0.0f, 1.0f);
            n.flatten = std::clamp (finite (n.flatten, 0.0f), 0.0f, 1.0f);
            n.formant = std::clamp (finite (n.formant, 0.0f), -maxFormant, maxFormant);
            n.transitionMs = std::clamp (finite (n.transitionMs, 40.0f), 0.0f, maxTransitionMs);
            n.sourceMidi = std::clamp (finite (n.sourceMidi, 60.0f), 0.0f, 127.0f);

            if (! std::isfinite (n.start)) n.start = 0.0;
            if (! std::isfinite (n.end)) n.end = n.start;
            n.start = std::clamp (n.start, 0.0, maxSeconds);
            n.end = std::clamp (n.end, n.start, maxSeconds);
        }

        // na precisão do .wnl (10 µs, centésimos): o projeto reaberto fica igual, e o arquivo refeito também
        for (auto& n : notes)
        {
            n.start = quantiseSeconds (n.start);
            n.end = quantiseSeconds (n.end);
            n.sourceMidi = quantiseHundredths (n.sourceMidi);
            n.shift = quantiseHundredths (n.shift);
            n.center = quantiseHundredths (n.center);
            n.flatten = quantiseHundredths (n.flatten);
            n.formant = quantiseHundredths (n.formant);
            n.transitionMs = std::round (n.transitionMs);
        }

        std::sort (notes.begin(), notes.end(), [] (const TunedNote& a, const TunedNote& b) { return a.start < b.start; });

        // sem sobreposição: cada nota começa depois do fim da última que ficou; as que somem (curtas demais) não
        // empurram as seguintes. Assim normalise duas vezes dá o mesmo que uma (o .wnl reaberto e o desfazer)
        std::vector<TunedNote> kept;
        double lastEnd = 0.0;

        for (auto n : notes)
        {
            if (! kept.empty())
                n.start = std::max (n.start, lastEnd);

            if (n.length() < minNoteSeconds * 0.5)
                continue;

            lastEnd = n.end;
            kept.push_back (n);
        }

        notes = std::move (kept);
    }

    /** A nota que contém `seconds`, ou -1. */
    inline int noteAt (const std::vector<TunedNote>& notes, double seconds) noexcept
    {
        for (size_t i = 0; i < notes.size(); ++i)
            if (seconds >= notes[i].start && seconds < notes[i].end)
                return (int) i;

        return -1;
    }

    /** Arrastar: a nota vai para `targetMidi` (inteiro = cai no semitom; com o Alt, fino). */
    inline void moveTo (TunedNote& n, float targetMidi) noexcept
    {
        n.shift = std::clamp (targetMidi - n.sourceMidi, -maxShift, maxShift);
    }

    /** Cortar a nota `index` em `at` (segundos); as duas metades ficam com as mesmas edições. */
    inline bool split (std::vector<TunedNote>& notes, int index, double at)
    {
        if (index < 0 || index >= (int) notes.size())
            return false;

        auto n = notes[(size_t) index];

        if (at - n.start < minNoteSeconds || n.end - at < minNoteSeconds)
            return false;

        auto right = n;
        notes[(size_t) index].end = at;
        right.start = at;
        notes.insert (notes.begin() + index + 1, right);
        return true;
    }

    /** Juntar a nota `index` com a seguinte: a nota cantada é a média pelo tamanho, e as edições são as da primeira
        (o alvo dela fica no mesmo semitom). */
    inline bool merge (std::vector<TunedNote>& notes, int index)
    {
        if (index < 0 || index + 1 >= (int) notes.size())
            return false;

        auto& a = notes[(size_t) index];
        const auto& b = notes[(size_t) index + 1];
        const double la = a.length(), lb = b.length();
        const float target = a.sourceMidi + a.shift;
        a.sourceMidi = (float) ((a.sourceMidi * la + b.sourceMidi * lb) / std::max (1.0e-9, la + lb));
        a.shift = a.shift != 0.0f ? target - a.sourceMidi : 0.0f;
        a.end = b.end;
        notes.erase (notes.begin() + index + 1);
        return true;
    }

    /** Corrigir tudo para a escala num clique: cada nota vai para a nota da escala mais perto e o desvio lento sai
        com a força `strength` (0 a 1). `inScale (nota MIDI inteira)` diz se a nota está na escala do projeto. */
    inline void snapToScale (std::vector<TunedNote>& notes, const std::function<bool (int)>& inScale, float strength)
    {
        strength = std::clamp (strength, 0.0f, 1.0f);

        for (auto& n : notes)
        {
            const int nearest = (int) std::lround (n.sourceMidi);
            int best = nearest;

            for (int d = 0; d <= 6; ++d)
            {
                // a mais perto do tom cantado; empate, a de baixo
                const int down = nearest - d, up = nearest + d;
                const bool okDown = inScale (down), okUp = inScale (up);

                if (okDown || okUp)
                {
                    best = okDown && okUp ? (std::abs ((float) down - n.sourceMidi) <= std::abs ((float) up - n.sourceMidi) ? down : up)
                                          : okDown ? down : up;
                    break;
                }
            }

            n.shift = std::clamp ((float) best - n.sourceMidi, -maxShift, maxShift) * strength;
            n.center = strength;
        }
    }
} // namespace notetune

} // namespace whynot
