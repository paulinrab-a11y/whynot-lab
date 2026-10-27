// Afinação nota a nota (frente C de 27/10): a análise (curva do tom e notas), as edições do modelo e o render.
// Uma voz sintética de notas conhecidas: 20 harmônicos com duas formantes fixas, pulsando como a glote.
// Sem JUCE: cl /std:c++20 /O2 /EHsc /utf-8 test_notetune.cpp ..\..\src\dsp\NoteTune.cpp ..\..\src\dsp\VoicePitch.cpp

#include "../../src/dsp/NoteTune.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace whynot;
using namespace whynot::dsp;

namespace
{
    int failures = 0, passes = 0;

    void check (bool ok, const std::string& what)
    {
        std::printf ("%s %s\n", ok ? "ok  " : "FALHA", what.c_str());
        (ok ? passes : failures)++;
    }

    constexpr double rate = 48000.0;
    constexpr double twoPi = 6.283185307179586;

    double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
    double cents (double hz, double ref) { return 1200.0 * std::log2 (hz / ref); }

    struct SungNote
    {
        double start, end, midi;
        double vibratoCents = 0.0;   // profundidade de pico, a 5,5 Hz
    };

    /** A voz: cada nota com 25 ms de subida e descida no volume; entre notas encostadas, 30 ms de glissando. */
    std::vector<float> sing (const std::vector<SungNote>& notes, double seconds)
    {
        std::vector<float> x ((size_t) (seconds * rate), 0.0f);
        double phase = 0.0;

        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / rate;
            double midi = 0.0, amp = 0.0;

            for (size_t k = 0; k < notes.size(); ++k)
            {
                const auto& n = notes[k];

                if (t < n.start || t >= n.end)
                    continue;

                midi = n.midi + n.vibratoCents / 100.0 * std::sin (twoPi * 5.5 * (t - n.start));
                const bool joinedBefore = k > 0 && std::abs (notes[k - 1].end - n.start) < 1.0e-9;
                const bool joinedAfter = k + 1 < notes.size() && std::abs (notes[k + 1].start - n.end) < 1.0e-9;

                if (joinedBefore && t - n.start < 0.03)
                    midi = notes[k - 1].midi + (midi - notes[k - 1].midi) * (t - n.start) / 0.03;

                amp = std::min ({ 1.0, joinedBefore ? 1.0 : (t - n.start) / 0.025, joinedAfter ? 1.0 : (n.end - t) / 0.025 });
            }

            if (amp <= 0.0)
                continue;

            const double f0 = midiToHz (midi);
            phase += twoPi * f0 / rate;

            if (phase > twoPi * 1000.0)
                phase -= twoPi * 1000.0;

            double s = 0.0;

            for (int h = 1; h <= 20; ++h)
            {
                const double f = f0 * h;

                if (f > 0.45 * rate)
                    break;

                // duas formantes fixas (700 Hz e 1200 Hz): o timbre não depende da nota
                const double env = 1.0 / (1.0 + std::pow ((f - 700.0) / 300.0, 2.0)) + 0.6 / (1.0 + std::pow ((f - 1200.0) / 400.0, 2.0)) + 0.02;
                s += env * std::sin (phase * h) / std::sqrt ((double) h);
            }

            x[i] = (float) (0.2 * amp * s);
        }

        return x;
    }

    /** A formante principal: o espectro (DFT direta, de 10 em 10 Hz) alisado em ±300 Hz, para ver a envoltória e não
        o harmônico mais forte; o pico dela entre 400 e 2000 Hz. */
    double formantPeak (const std::vector<float>& x, double from, double to)
    {
        const auto a = (size_t) (from * rate), b = (size_t) (to * rate);
        std::vector<double> power;

        for (double f = 100.0; f <= 2300.0; f += 10.0)
        {
            double re = 0.0, im = 0.0;

            for (size_t i = a; i < b; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (twoPi * (double) (i - a) / (double) (b - a));
                re += w * x[i] * std::cos (twoPi * f * (double) i / rate);
                im += w * x[i] * std::sin (twoPi * f * (double) i / rate);
            }

            power.push_back (re * re + im * im);
        }

        double bestHz = 0.0, best = -1.0;

        for (size_t k = 30; k + 30 < power.size(); ++k)
        {
            double sum = 0.0;

            for (size_t j = k - 30; j <= k + 30; ++j)
                sum += power[j];

            const double f = 100.0 + 10.0 * (double) k;

            if (f >= 400.0 && f <= 2000.0 && sum > best)
            {
                best = sum;
                bestHz = f;
            }
        }

        return bestHz;
    }

    /** Amplitude de `x` na frequência `f` (DFT com janela de Hann) entre `from` e `to` segundos. */
    double amplitudeAt (const std::vector<float>& x, double from, double to, double f)
    {
        const auto a = (size_t) (from * rate), b = (size_t) (to * rate);
        double re = 0.0, im = 0.0;

        for (size_t i = a; i < b; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (twoPi * (double) (i - a) / (double) (b - a));
            re += w * x[i] * std::cos (twoPi * f * (double) i / rate);
            im += w * x[i] * std::sin (twoPi * f * (double) i / rate);
        }

        return std::sqrt (re * re + im * im);
    }

    /** A envoltória da voz sintética (as duas formantes fixas). */
    double envelope (double f)
    {
        return 1.0 / (1.0 + std::pow ((f - 700.0) / 300.0, 2.0)) + 0.6 / (1.0 + std::pow ((f - 1200.0) / 400.0, 2.0)) + 0.02;
    }

    double hzAt (const std::vector<float>& x, double from, double to)
    {
        return measureFundamental (x.data(), x.size(), rate, (size_t) (from * rate), (size_t) (to * rate));
    }

    /** Desvio-padrão do tom (cents) num trecho, quadro a quadro. */
    double pitchSpreadCents (const std::vector<float>& x, double from, double to)
    {
        const auto a = analyseNotes (x.data(), x.size(), rate);
        std::vector<double> v;

        for (size_t k = 0; k < a.curve.midi.size(); ++k)
        {
            const double t = (double) k * a.curve.hopSeconds;

            if (t >= from && t < to && a.curve.voiced (k))
                v.push_back (a.curve.midi[k]);
        }

        if (v.size() < 2)
            return 1.0e9;

        double mean = 0.0, sq = 0.0;

        for (double m : v) mean += m;

        mean /= (double) v.size();

        for (double m : v) sq += (m - mean) * (m - mean);

        return 100.0 * std::sqrt (sq / (double) v.size());
    }
}

int main()
{
    // 1) Análise: Lá 3, Dó 4 encostado, pausa, Mi 4 com vibrato de ±40 cents, Ré 4 desafinado (+30 cents) e Sol 3.
    const std::vector<SungNote> sung = {
        { 0.20, 0.70, 57.0 },
        { 0.70, 1.20, 60.0 },
        { 1.45, 2.15, 64.0, 40.0 },
        { 2.40, 2.90, 62.3 },
        { 3.10, 3.60, 55.0 },
    };
    const auto voice = sing (sung, 4.0);

    const auto t0 = std::chrono::steady_clock::now();
    const auto analysis = analyseNotes (voice.data(), voice.size(), rate);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("análise de 4 s de voz: %.1f ms, %zu quadros, %zu notas\n", ms, analysis.curve.midi.size(), analysis.notes.size());

    for (auto& n : analysis.notes)
        std::printf ("  nota %.3f-%.3f s  midi %.3f  desvio %.1f c  vibrato %.1f c\n", n.start, n.end, n.midi, n.driftCents, n.vibratoCents);

    check (analysis.notes.size() == sung.size(), "acha as 5 notas cantadas (achou " + std::to_string (analysis.notes.size()) + ")");

    if (analysis.notes.size() == sung.size())
    {
        double worstCents = 0.0, worstEdge = 0.0;

        for (size_t i = 0; i < sung.size(); ++i)
        {
            worstCents = std::max (worstCents, std::abs (100.0 * (analysis.notes[i].midi - sung[i].midi)));
            worstEdge = std::max ({ worstEdge, std::abs (analysis.notes[i].start - sung[i].start), std::abs (analysis.notes[i].end - sung[i].end) });
        }

        check (worstCents < 5.0, "a nota média de cada uma a menos de 5 cents (pior " + std::to_string (worstCents) + ")");
        check (worstEdge < 0.04, "início e fim a menos de 40 ms (pior " + std::to_string (worstEdge * 1000.0) + " ms)");
        check (analysis.notes[2].vibratoCents > 25.0 && analysis.notes[0].vibratoCents < 5.0,
               "o vibrato aparece só no Mi (" + std::to_string (analysis.notes[2].vibratoCents) + " c contra " + std::to_string (analysis.notes[0].vibratoCents) + ")");
    }

    {
        // a curva fina: no meio do Lá, a menos de 3 cents de 220 Hz
        double worst = 0.0;

        for (double t = 0.3; t < 0.6; t += 0.01)
            worst = std::max (worst, std::abs (100.0 * (analysis.curve.at (t) - 57.0)));

        check (worst < 3.0, "a curva no meio do Lá fica a menos de 3 cents (pior " + std::to_string (worst) + ")");
        check (analysis.curve.at (1.3) == 0.0f && analysis.curve.at (3.8) == 0.0f, "na pausa e no silêncio a curva fica sem tom");
    }

    // 2) Modelo: cortar, juntar, mover e a escala.
    {
        auto notes = notesFromAnalysis (analysis);
        const size_t before = notes.size();
        check (notetune::split (notes, 0, 0.45) && notes.size() == before + 1 && notes[0].end == 0.45 && notes[1].start == 0.45,
               "cortar a nota em 0,45 s dá duas notas encostadas");
        check (! notetune::split (notes, 0, notes[0].start + 0.01), "não corta deixando um pedaço de menos de 30 ms");
        check (notetune::merge (notes, 0) && notes.size() == before && std::abs (notes[0].end - analysis.notes[0].end) < 1.0e-9,
               "juntar volta a uma nota só");
        notetune::moveTo (notes[1], 62.0f);
        check (std::abs (notes[1].targetMidi() - 62.0f) < 1.0e-4f && notes[1].edited(), "arrastar o Dó para o Ré: o alvo é 62");
        auto minor = notes;
        // Lá menor: lá si dó ré mi fá sol
        notetune::snapToScale (minor, [] (int k) { const int d = ((k - 57) % 12 + 12) % 12; return d == 0 || d == 2 || d == 3 || d == 5 || d == 7 || d == 8 || d == 10; }, 1.0f);
        bool allIn = true;

        for (auto& n : minor)
            allIn = allIn && std::abs (n.targetMidi() - std::round (n.targetMidi())) < 1.0e-3f;

        check (allIn && std::abs (minor[3].targetMidi() - 62.0f) < 1.0e-3f, "corrigir para a escala: todas num semitom, o Ré desafinado vai a 62");
        auto bad = notes;
        bad[0].shift = 99.0f;
        bad[0].center = -3.0f;
        bad[1].start = bad[0].end - 0.1;
        notetune::normalise (bad);
        check (bad[0].shift == notetune::maxShift && bad[0].center == 0.0f && bad[1].start >= bad[0].end, "normalise prende as faixas e tira a sobreposição");

        // uma nota dentro da outra e a seguinte sobreposta: normalise duas vezes dá o mesmo que uma
        std::vector<TunedNote> nested (3);
        nested[0].start = 0.0; nested[0].end = 1.0;
        nested[1].start = 0.2; nested[1].end = 0.5;
        nested[2].start = 0.4; nested[2].end = 2.0;
        notetune::normalise (nested);
        auto twice = nested;
        notetune::normalise (twice);
        check (twice == nested && nested.size() == 2 && nested[1].start == 1.0, "normalise é idempotente (nota dentro de outra some sem empurrar errado)");
    }

    // 3) Render: sem edição sai igual; o Dó movido para Ré sai em Ré; o resto igual bit a bit.
    {
        const std::vector<std::vector<float>> in { voice };
        auto notes = notesFromAnalysis (analysis);
        NoteRenderStats stats;
        const auto same = renderNotes (in, rate, analysis.curve, notes, false, &stats);
        check (same[0] == voice && stats.regions == 0, "sem edição, o render devolve a entrada, amostra por amostra");

        if (notes.size() == sung.size())
        {
            notetune::moveTo (notes[1], 62.0f);
            const auto t1 = std::chrono::steady_clock::now();
            const auto moved = renderNotes (in, rate, analysis.curve, notes, false, &stats);
            const double rms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t1).count();
            const double hz = hzAt (moved[0], 0.85, 1.10);
            const double err = cents (hz, midiToHz (62.0));
            std::printf ("render do Dó → Ré: %.1f ms, %d trecho(s), %zu amostras refeitas\n", rms, stats.regions, stats.renderedFrames);
            check (std::abs (err) < 5.0, "o Dó movido sai em Ré: " + std::to_string (hz) + " Hz (" + std::to_string (err) + " cents)");

            const double other = hzAt (moved[0], 1.6, 2.0);
            check (std::abs (cents (other, midiToHz (64.0))) < 10.0, "o Mi seguinte continua em Mi (" + std::to_string (other) + " Hz)");

            // fora do trecho refeito (o Dó com a folga e a rampa), a saída é a entrada bit a bit
            size_t changedOutside = 0, changed = 0;
            size_t firstChange = voice.size(), lastChange = 0;

            for (size_t i = 0; i < voice.size(); ++i)
                if (moved[0][i] != voice[i])
                {
                    ++changed;
                    firstChange = std::min (firstChange, i);
                    lastChange = std::max (lastChange, i);
                }

            for (size_t i = 0; i < voice.size(); ++i)
            {
                const double t = (double) i / rate;

                if ((t < notes[0].end - 0.1 || t > notes[1].end + 0.15) && moved[0][i] != voice[i])
                    ++changedOutside;
            }

            std::printf ("  amostras diferentes: %zu, de %.3f s a %.3f s\n", changed, firstChange / rate, lastChange / rate);
            check (changedOutside == 0, "fora do Dó (com 100 ms antes e 150 ms depois), a voz sai igual bit a bit");

            // nas bordas do trecho refeito o salto é pequeno: o PSOLA em fase com a entrada
            float edgeJump = 0.0f;

            for (size_t i = firstChange; i <= lastChange && i + 1 < voice.size(); ++i)
                edgeJump = std::max (edgeJump, std::abs ((moved[0][i + 1] - moved[0][i]) - (voice[i + 1] - voice[i])));

            check (edgeJump < 0.12f, "sem estalo no trecho refeito (a maior diferença de derivada " + std::to_string (edgeJump) + ")");

            // estéreo: o mesmo resultado nos dois canais
            const std::vector<std::vector<float>> st { voice, voice };
            const auto two = renderNotes (st, rate, analysis.curve, notes, false);
            check (two.size() == 2 && two[0] == two[1] && two[0] == moved[0], "estéreo: os dois canais iguais ao mono");
        }
    }

    // 4) Centralizar e achatar o vibrato.
    if (analysis.notes.size() == sung.size())
    {
        const std::vector<std::vector<float>> in { voice };
        auto notes = notesFromAnalysis (analysis);
        const double offBefore = cents (hzAt (voice, 2.55, 2.75), midiToHz (62.0));
        notes[3].center = 1.0f;
        const auto centred = renderNotes (in, rate, analysis.curve, notes, false);
        const double offAfter = cents (hzAt (centred[0], 2.55, 2.75), midiToHz (62.0));
        check (std::abs (offBefore - 30.0) < 5.0 && std::abs (offAfter) < 5.0,
               "centralizar 100%: o Ré desafinado vai de " + std::to_string (offBefore) + " para " + std::to_string (offAfter) + " cents");

        notes[3].center = 0.5f;
        const auto half = renderNotes (in, rate, analysis.curve, notes, false);
        const double offHalf = cents (hzAt (half[0], 2.55, 2.75), midiToHz (62.0));
        check (std::abs (offHalf - 15.0) < 5.0, "centralizar 50%: metade do caminho (" + std::to_string (offHalf) + " cents)");
        notes[3].center = 0.0f;

        const double vibBefore = pitchSpreadCents (voice, 1.6, 2.0);
        notes[2].flatten = 1.0f;
        const auto flat = renderNotes (in, rate, analysis.curve, notes, false);
        const double vibAfter = pitchSpreadCents (flat[0], 1.6, 2.0);
        check (vibBefore > 20.0 && vibAfter < 8.0,
               "achatar o vibrato 100%: de " + std::to_string (vibBefore) + " para " + std::to_string (vibAfter) + " cents de desvio");
    }

    // 5) Formante à parte e junto com o tom.
    if (analysis.notes.size() == sung.size())
    {
        const std::vector<std::vector<float>> in { voice };
        auto notes = notesFromAnalysis (analysis);
        const double before = formantPeak (voice, 0.35, 0.6);
        notes[0].formant = 4.0f;
        const auto up = renderNotes (in, rate, analysis.curve, notes, false);
        const double after = formantPeak (up[0], 0.35, 0.6);
        const double hzKept = hzAt (up[0], 0.35, 0.6);
        check (after > before * 1.15 && std::abs (cents (hzKept, midiToHz (57.0))) < 6.0,
               "formante +4 st: o pico vai de " + std::to_string (before) + " para " + std::to_string (after) + " Hz e a nota fica (" + std::to_string (hzKept) + " Hz)");

        notes[0].formant = 0.0f;
        notetune::moveTo (notes[0], 61.0f);
        const auto kept = renderNotes (in, rate, analysis.curve, notes, false);
        const auto follow = renderNotes (in, rate, analysis.curve, notes, true);
        // a razão entre o 2º e o 3º harmônico: com o timbre junto ela fica a da voz original (como fita); separado,
        // ela segue a envoltória fixa nos harmônicos novos
        const double f1 = midiToHz (61.0);
        const double r0 = amplitudeAt (voice, 0.35, 0.6, 440.0) / amplitudeAt (voice, 0.35, 0.6, 660.0);
        const double rk = amplitudeAt (kept[0], 0.35, 0.6, 2.0 * f1) / amplitudeAt (kept[0], 0.35, 0.6, 3.0 * f1);
        const double rf = amplitudeAt (follow[0], 0.35, 0.6, 2.0 * f1) / amplitudeAt (follow[0], 0.35, 0.6, 3.0 * f1);
        const double expectedKept = (envelope (2.0 * f1) / std::sqrt (2.0)) / (envelope (3.0 * f1) / std::sqrt (3.0));
        check (std::abs (rk / expectedKept - 1.0) < 0.25 && std::abs (rf / r0 - 1.0) < 0.25 && std::abs (rk / rf - 1.0) > 0.3,
               "+4 st no tom, 2º/3º harmônico: separado " + std::to_string (rk) + " (envoltória " + std::to_string (expectedKept)
                   + "), junto " + std::to_string (rf) + " (original " + std::to_string (r0) + ")");
    }

    // 6) Transição: com 0 ms a correção salta; com 150 ms entra devagar.
    if (analysis.notes.size() == sung.size())
    {
        auto notes = notesFromAnalysis (analysis);
        notetune::moveTo (notes[1], 62.0f);
        notes[1].transitionMs = 0.0f;
        const auto hard = correctionFor (analysis.curve, notes, false);
        notes[1].transitionMs = 150.0f;
        const auto soft = correctionFor (analysis.curve, notes, false);
        const auto k = (size_t) std::round ((notes[1].start + 0.05) / analysis.curve.hopSeconds);
        check (hard.semitones[k] > 1.8f && soft.semitones[k] > 0.4f && soft.semitones[k] < 1.2f,
               "50 ms depois do começo: correção " + std::to_string (hard.semitones[k]) + " st sem rampa e " + std::to_string (soft.semitones[k]) + " com 150 ms");
    }

    // 7) Arquivo curto, silêncio e ruído não quebram.
    {
        std::vector<float> silence ((size_t) rate, 0.0f), tiny (100, 0.1f), noise ((size_t) rate);
        unsigned s = 1;

        for (auto& v : noise) { s = s * 1664525u + 1013904223u; v = ((float) (s >> 9) / 8388608.0f - 1.0f) * 0.3f; }

        check (analyseNotes (silence.data(), silence.size(), rate).notes.empty() && analyseNotes (tiny.data(), tiny.size(), rate).notes.empty()
                   && analyseNotes (noise.data(), noise.size(), rate).notes.size() <= 1 && analyseNotes (nullptr, 0, rate).notes.empty(),
               "silêncio, 100 amostras e ruído: nenhuma nota (ou uma, no ruído)");
    }

    // 8) NaN no arquivo (WAV float corrompido): a curva e as notas ficam finitas
    {
        auto broken = voice;

        for (size_t i = 30000; i < 30100; ++i)
            broken[i] = std::nanf ("");

        const auto a = analyseNotes (broken.data(), broken.size(), rate);
        bool finite = true;

        for (float m : a.curve.midi) finite = finite && std::isfinite (m);
        for (float l : a.curve.level) finite = finite && std::isfinite (l);
        for (auto& n : a.notes) finite = finite && std::isfinite (n.midi) && std::isfinite (n.driftCents) && std::isfinite (n.vibratoCents);

        check (finite && a.notes.size() + 1 >= sung.size(), "100 amostras NaN no meio: curva e notas finitas (" + std::to_string (a.notes.size()) + " notas)");
    }

    std::printf ("\n%d ok, %d falha(s)\n", passes, failures);
    return failures;
}
