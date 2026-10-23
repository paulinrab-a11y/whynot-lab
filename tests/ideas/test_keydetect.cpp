// Testes do tom do beat (sem JUCE): áudio sintético em tom conhecido, com acordes em dente de serra,
// baixo, bumbo, chimbal e ruído, afinado fora de 440. Mede a taxa de acerto do tom e dos acordes por compasso.
//
//   whynot_key_tests            roda tudo e sai com o número de falhas
//   whynot_key_tests --wav <arquivo.wav> <bpm> [início em s]   analisa um WAV (16 ou 24 bits, PCM ou float)

#include "../../src/ideas/KeyDetect.h"
#include "../../src/ideas/Theory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace whynot::ideas;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

namespace
{
    constexpr double pi = 3.14159265358979323846;

    struct ChordSpec
    {
        int root;
        bool minor;
    };

    struct Beat
    {
        double sampleRate = 44100.0;
        double bpm = 140.0;
        int bars = 8;
        double cents = 0.0;
        bool kick = true, hats = true, bass = true, pads = true;
        double noise = 0.03;
        unsigned seed = 1;
        double leadIn = 0.0;          // silêncio antes do primeiro compasso
        bool stabs = false;           // acordes curtos (staccato) em vez de pad
        double padLevel = 0.12, bassLevel = 0.35, kickLevel = 0.7, kickDecay = 0.12;
        int melodyRoot = -1;          // >= 0: melodia de colcheias na escala deste tom
        bool melodyMajor = false;
        std::vector<ChordSpec> chords;
    };

    double hz (double midi, double cents)   { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0 + cents / 1200.0); }

    /** Dente de serra com polyBLEP (sem serrilhado audível). */
    struct Saw
    {
        double phase = 0.0;

        float next (double inc)
        {
            phase += inc;

            if (phase >= 1.0)
                phase -= 1.0;

            double v = 2.0 * phase - 1.0;

            if (phase < inc)
            {
                const double t = phase / inc;
                v -= t + t - t * t - 1.0;
            }
            else if (phase > 1.0 - inc)
            {
                const double t = (phase - 1.0) / inc;
                v -= t * t + t + t + 1.0;
            }

            return (float) v;
        }
    };

    std::vector<float> render (const Beat& b)
    {
        const double barSeconds = 60.0 / b.bpm * 4.0;
        const auto total = (std::size_t) ((b.leadIn + barSeconds * b.bars + 0.5) * b.sampleRate);
        std::vector<float> out (total, 0.0f);
        std::mt19937 rng (b.seed);
        std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
        std::uniform_int_distribution<int> inversion (0, 2);
        const auto lead = (std::size_t) (b.leadIn * b.sampleRate);

        for (int bar = 0; bar < b.bars; ++bar)
        {
            const auto& ch = b.chords[(size_t) bar % b.chords.size()];
            const auto start = lead + (std::size_t) (bar * barSeconds * b.sampleRate);
            const auto length = (std::size_t) (barSeconds * b.sampleRate);

            // acorde: tríade perto do Dó central, com inversão sorteada
            std::vector<int> notes { ch.root, ch.root + (ch.minor ? 3 : 4), ch.root + 7 };
            int base = 57;   // Lá 3

            for (auto& n : notes)
                n = base + ((n - base) % 12 + 12) % 12;

            std::sort (notes.begin(), notes.end());

            for (int i = inversion (rng); i > 0; --i)
            {
                notes.front() += 12;
                std::sort (notes.begin(), notes.end());
            }

            if (b.pads)
            {
                for (int n : notes)
                {
                    Saw saw;
                    saw.phase = (uni (rng) + 1.0f) * 0.5f;
                    const double inc = hz (n, b.cents) / b.sampleRate;

                    for (std::size_t s = 0; s < length && start + s < total; ++s)
                    {
                        const double t = s / b.sampleRate;
                        double env = std::min (1.0, t / 0.02) * std::min (1.0, (barSeconds - t) / 0.05);

                        if (b.stabs)   // colcheias curtas
                        {
                            const double eighth = barSeconds / 8.0;
                            const double inEighth = std::fmod (t, eighth);
                            env *= inEighth < eighth * 0.45 ? std::exp (-inEighth / 0.08) : 0.0;
                        }

                        out[start + s] += (float) (b.padLevel * env) * saw.next (inc);
                    }
                }
            }

            // melodia: colcheias, nota do acorde no tempo forte e da escala no fraco, às vezes pausa
            if (b.melodyRoot >= 0)
            {
                const std::vector<int> degrees = b.melodyMajor ? std::vector<int> { 0, 2, 4, 5, 7, 9, 11 } : std::vector<int> { 0, 2, 3, 5, 7, 8, 10 };
                std::uniform_real_distribution<double> chance (0.0, 1.0);
                std::uniform_int_distribution<int> pick (0, 6), pickChord (0, 2);
                const double eighth = barSeconds / 8.0;

                for (int e = 0; e < 8; ++e)
                {
                    if (chance (rng) < 0.3)
                        continue;

                    int key = e % 2 == 0 ? notes[(size_t) pickChord (rng)] : b.melodyRoot + degrees[(size_t) pick (rng)];
                    key = 72 + ((key - 72) % 12 + 12) % 12;
                    Saw saw;
                    const double inc = hz (key, b.cents) / b.sampleRate;
                    const auto ms = start + (std::size_t) (e * eighth * b.sampleRate);

                    for (std::size_t s = 0; s < (std::size_t) (eighth * b.sampleRate) && ms + s < total; ++s)
                    {
                        const double t = s / b.sampleRate;
                        const double env = std::min (1.0, t / 0.01) * std::exp (-t / 0.25) * std::min (1.0, (eighth - t) / 0.01);
                        out[ms + s] += (float) (0.08 * env) * saw.next (inc);
                    }
                }
            }

            // baixo (808): seno grave na fundamental, com um pouco de saturação
            if (b.bass)
            {
                int bassKey = 33 + ((ch.root - 33) % 12 + 12) % 12;   // Lá 1 a Sol# 2
                const double f = hz (bassKey, b.cents);
                double ph = 0.0;

                for (std::size_t s = 0; s < length && start + s < total; ++s)
                {
                    const double t = s / b.sampleRate;
                    const double env = std::min (1.0, t / 0.005) * std::exp (-t / 1.2) * std::min (1.0, (barSeconds - t) / 0.03);
                    ph += f / b.sampleRate;
                    out[start + s] += (float) (b.bassLevel * env * std::tanh (1.5 * std::sin (2.0 * pi * ph)));
                }
            }

            // bumbo em cada tempo e chimbal nas colcheias
            for (int beat = 0; beat < 4; ++beat)
            {
                const auto ks = start + (std::size_t) (beat * barSeconds / 4.0 * b.sampleRate);

                if (b.kick)
                {
                    double ph = 0.0;

                    for (std::size_t s = 0; s < (std::size_t) (0.35 * b.sampleRate) && ks + s < total; ++s)
                    {
                        const double t = s / b.sampleRate;
                        const double f = 50.0 + 110.0 * std::exp (-t / 0.03);
                        ph += f / b.sampleRate;
                        out[ks + s] += (float) (b.kickLevel * std::exp (-t / b.kickDecay) * std::sin (2.0 * pi * ph));
                    }
                }

                if (b.hats)
                    for (int e = 0; e < 2; ++e)
                    {
                        const auto hs = ks + (std::size_t) (e * barSeconds / 8.0 * b.sampleRate);
                        float prev = 0.0f;

                        for (std::size_t s = 0; s < (std::size_t) (0.06 * b.sampleRate) && hs + s < total; ++s)
                        {
                            const float n = uni (rng);
                            out[hs + s] += (float) (0.15 * std::exp (-(s / b.sampleRate) / 0.015)) * (n - prev);   // ruído agudo
                            prev = n;
                        }
                    }
            }
        }

        for (auto& v : out)
            v += (float) b.noise * uni (rng);

        return out;
    }

    std::vector<ChordSpec> progression (int root, bool major)
    {
        if (major)   // I V vi IV
            return { { root, false }, { root + 7, false }, { root + 9, true }, { root + 5, false } };

        return { { root, true }, { root + 8, false }, { root + 3, false }, { root + 10, false } };   // i VI III VII
    }

    std::vector<ChordSpec> normalised (std::vector<ChordSpec> v)
    {
        for (auto& c : v)
            c.root = pitchClass (c.root);

        return v;
    }

    struct Score
    {
        int cases = 0, exact = 0, relative = 0, bars = 0, barsRight = 0;
        double worstTuning = 0.0, seconds = 0.0, audioSeconds = 0.0;
    };

    void runCaseWith (Score& sc, int root, bool major, Beat beat, std::vector<ChordSpec> chords, const char* label);

    void runCase (Score& sc, int root, bool major, Beat beat, bool)
    {
        runCaseWith (sc, root, major, beat, progression (root, major), "");
    }

    void runCaseWith (Score& sc, int root, bool major, Beat beat, std::vector<ChordSpec> chords, const char* label)
    {
        const bool verbose = false;
        beat.chords = normalised (chords);
        const auto audio = render (beat);
        const auto t0 = std::chrono::steady_clock::now();
        const auto a = analyzeBeat (audio.data(), audio.size(), beat.sampleRate);
        const auto barStarts = barGrid (beat.leadIn, beat.bpm, 4, 1.0, beat.bars);
        const auto bars = a.found() ? chordsForBars (a, barStarts, &a.best()) : std::vector<BarChord> {};
        sc.seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        sc.audioSeconds += audio.size() / beat.sampleRate;

        ++sc.cases;
        const KeyCandidate truth { pitchClass (root), major, 0.0, 0.0 };
        const bool exact = a.found() && a.best() == truth;
        const bool rel = a.found() && (exact || a.best() == relativeOf (truth));
        sc.exact += exact ? 1 : 0;
        sc.relative += rel ? 1 : 0;
        sc.worstTuning = std::max (sc.worstTuning, std::abs (a.tuningCents - beat.cents));
        int right = 0;

        for (std::size_t i = 0; i < bars.size(); ++i)
        {
            const auto& want = beat.chords[i % beat.chords.size()];
            right += bars[i].found && bars[i].root == want.root && bars[i].minor == want.minor ? 1 : 0;
        }

        sc.bars += beat.bars;
        sc.barsRight += right;

        if (verbose || ! exact || right < beat.bars)
        {
            std::printf ("    %s %s%s%s, %.0f BPM, %+.0f cents, semente %u: %s (%.0f%%), afinação %+.1f, acordes %d/%d",
                         exact ? "  " : "XX", keyNamePt (root, major).c_str(), *label ? " " : "", label, beat.bpm, beat.cents, beat.seed,
                         a.found() ? keyNamePt (a.best().root, a.best().major).c_str() : "nada",
                         a.found() ? 100.0 * a.best().probability : 0.0, a.tuningCents, right, beat.bars);

            if (right < beat.bars)
            {
                std::printf (" [");

                for (auto& bc : bars)
                    std::printf (" %s", bc.found ? chordName (bc.root, bc.minor, false).c_str() : "-");

                std::printf (" ]");
            }

            if (! exact && a.found())
                for (int i = 1; i < 4; ++i)
                    std::printf (" | %s %.0f%%", keyNamePt (a.keys[(size_t) i].root, a.keys[(size_t) i].major).c_str(), 100.0 * a.keys[(size_t) i].probability);

            std::printf ("\n");
        }
    }

    void report (const char* what, const Score& sc)
    {
        std::printf ("  %s: tom exato %d/%d (%.0f%%), exato ou relativo %d/%d, acordes por compasso %d/%d (%.0f%%), "
                     "pior erro de afinação %.1f cents, %.2f s para %.0f s de áudio\n",
                     what, sc.exact, sc.cases, 100.0 * sc.exact / std::max (1, sc.cases), sc.relative, sc.cases,
                     sc.barsRight, sc.bars, 100.0 * sc.barsRight / std::max (1, sc.bars), sc.worstTuning, sc.seconds, sc.audioSeconds);
    }

    //==========================================================================
    void testBasics()
    {
        std::printf ("Básico\n");
        const double sr = 44100.0;
        std::vector<float> sine ((size_t) (sr * 4.0));

        for (double cents : { 0.0, 30.0, -20.0 })
        {
            for (std::size_t i = 0; i < sine.size(); ++i)
                sine[i] = 0.5f * (float) std::sin (2.0 * pi * hz (69.0, cents) * i / sr);

            const auto a = analyzeBeat (sine.data(), sine.size(), sr);
            int top = 0;

            for (int k = 1; k < 12; ++k)
                if (a.profile[(size_t) k] > a.profile[(size_t) top])
                    top = k;

            CHECK (top == 9 && std::abs (a.tuningCents - cents) < 2.0, "seno de Lá em %+.0f cents: classe Lá, afinação %+.1f cents", cents, a.tuningCents);
        }

        std::vector<float> silence ((size_t) sr * 3, 0.0f);
        CHECK (! analyzeBeat (silence.data(), silence.size(), sr).found(), "silêncio não tem tom");

        Beat drums;
        drums.pads = false;
        drums.bass = false;
        drums.chords = { { 0, false } };
        auto drumAudio = render (drums);
        const auto d = analyzeBeat (drumAudio.data(), drumAudio.size(), drums.sampleRate);
        std::printf ("    só bateria: tonalidade %.2f, %s\n", d.tonalness, d.found() ? keyNamePt (d.best().root, d.best().major).c_str() : "sem tom");
        CHECK (! d.found() || d.best().probability < 0.5, "só bateria e ruído: sem tom, ou com confiança baixa (%.0f%%)",
               d.found() ? 100.0 * d.best().probability : 0.0);

        std::atomic<bool> cancel { true };
        const auto c = analyzeBeat (drumAudio.data(), drumAudio.size(), drums.sampleRate, {}, &cancel);
        CHECK (c.chroma.empty() && ! c.found(), "cancelar devolve vazio");

        CHECK (keyNamePt (9, false) == "Lá menor" && keyNamePt (10, true) == "Sib maior" && keyNamePt (6, false) == "Fá# menor",
               "nomes: Lá menor, Sib maior, Fá# menor");
        CHECK ((relativeOf ({ 9, false }) == KeyCandidate { 0, true } && relativeOf ({ 2, true }) == KeyCandidate { 11, false }),
               "relativos: Lá menor e Dó maior, Ré maior e Si menor");
        CHECK ((keyRelation ({ 9, false }, { 0, true }) == "relativo" && keyRelation ({ 9, false }, { 9, true }) == "paralelo"
               && keyRelation ({ 9, false }, { 4, false }) == "vizinho" && keyRelation ({ 9, false }, { 2, false }) == "vizinho"
               && keyRelation ({ 9, false }, { 3, true }).empty()),
               "relações: relativo, paralelo, vizinho e outro");

        {
            // ler o arquivo em pedaços dá o mesmo que de uma vez
            Downsampler whole (44100.0), parts (44100.0);
            whole.push (drumAudio.data(), drumAudio.size());
            whole.finish();

            for (std::size_t i = 0; i < drumAudio.size(); i += 4097)
                parts.push (drumAudio.data() + i, std::min<std::size_t> (4097, drumAudio.size() - i));

            parts.finish();
            CHECK (whole.samples() == parts.samples() && whole.samples().size() == (drumAudio.size() + 3) / 4 && std::abs (whole.outputRate() - 11025.0) < 1e-9,
                   "descer a taxa em pedaços = de uma vez (%zu amostras a %.0f Hz)", whole.samples().size(), whole.outputRate());
        }

        const auto grid = barGrid (1.0, 120.0, 4, 1.5, 2);
        CHECK (grid.size() == 3 && std::abs (grid[1] - (1.0 + 2.0 / 1.5)) < 1e-9, "grade de compassos: 120 BPM esticado 1,5x");
    }

    void testNamedKeys (Score& all)
    {
        std::printf ("Lá menor, Dó maior, Fá# menor e Ré maior (i VI III VII e I V vi IV), com bumbo, chimbal e ruído\n");
        Score sc;

        for (auto [root, major] : { std::pair { 9, false }, { 0, true }, { 6, false }, { 2, true } })
            for (double bpm : { 90.0, 140.0, 160.0 })
                for (double cents : { 0.0, 30.0 })
                    for (unsigned seed : { 1u, 2u })
                    {
                        Beat b;
                        b.bpm = bpm;
                        b.cents = cents;
                        b.seed = seed;
                        runCase (sc, root, major, b, false);
                    }

        report ("resultado", sc);
        CHECK (sc.exact * 100 >= sc.cases * 90, "tom exato em pelo menos 90%% (%d de %d)", sc.exact, sc.cases);
        CHECK (sc.relative == sc.cases, "tom exato ou o relativo em todos (%d de %d)", sc.relative, sc.cases);
        CHECK (sc.barsRight * 100 >= sc.bars * 90, "acorde do compasso certo em pelo menos 90%% (%d de %d)", sc.barsRight, sc.bars);
        CHECK (sc.worstTuning < 5.0, "afinação achada a menos de 5 cents (pior: %.1f)", sc.worstTuning);
        all.cases += sc.cases; all.exact += sc.exact; all.relative += sc.relative; all.bars += sc.bars; all.barsRight += sc.barsRight;
    }

    void testAllKeys (Score& all)
    {
        std::printf ("As 24 tonalidades a 140 BPM e +30 cents\n");
        Score sc;

        for (int root = 0; root < 12; ++root)
            for (bool major : { false, true })
            {
                Beat b;
                b.cents = 30.0;
                b.seed = (unsigned) (10 + root);
                runCase (sc, root, major, b, false);
            }

        report ("resultado", sc);
        CHECK (sc.exact * 100 >= sc.cases * 90, "tom exato em pelo menos 90%% (%d de %d)", sc.exact, sc.cases);
        CHECK (sc.relative == sc.cases, "tom exato ou o relativo em todos (%d de %d)", sc.relative, sc.cases);
        CHECK (sc.barsRight * 100 >= sc.bars * 90, "acordes por compasso em pelo menos 90%% (%d de %d)", sc.barsRight, sc.bars);
        all.cases += sc.cases; all.exact += sc.exact; all.relative += sc.relative; all.bars += sc.bars; all.barsRight += sc.barsRight;
    }

    void testHarder (Score& all)
    {
        std::printf ("Mais difícil: sem baixo, acordes curtos, ruído alto, -20 cents, 22,05 kHz, silêncio antes\n");
        Score sc;

        for (auto [root, major] : { std::pair { 9, false }, { 0, true }, { 6, false }, { 2, true }, { 4, false }, { 10, true } })
        {
            Beat noBass;
            noBass.bass = false;
            noBass.seed = 3;
            runCase (sc, root, major, noBass, false);

            Beat stabs;
            stabs.stabs = true;
            stabs.seed = 4;
            stabs.cents = -20.0;
            runCase (sc, root, major, stabs, false);

            Beat noisy;
            noisy.noise = 0.12;
            noisy.seed = 5;
            noisy.sampleRate = 22050.0;
            noisy.leadIn = 1.3;
            runCase (sc, root, major, noisy, false);
        }

        report ("resultado", sc);
        CHECK (sc.relative * 100 >= sc.cases * 90, "tom exato ou o relativo em pelo menos 90%% (%d de %d)", sc.relative, sc.cases);
        CHECK (sc.barsRight * 100 >= sc.bars * 80, "acordes por compasso em pelo menos 80%% (%d de %d)", sc.barsRight, sc.bars);
        all.cases += sc.cases; all.exact += sc.exact; all.relative += sc.relative; all.bars += sc.bars; all.barsRight += sc.barsRight;
    }

    /** Laços de trap mais perto do real: 808 alto, acordes baixos, melodia, bumbo curto e progressões que
        não são sempre a mesma (uma começa no IV). Mede, sem esperar 100%. */
    void testTrap (Score& all)
    {
        std::printf ("Trap mais perto do real: 808 alto, acordes baixos, melodia e 9 progressões\n");
        struct Prog { const char* name; bool major; std::vector<ChordSpec> rel; };
        const std::vector<Prog> progs = {
            { "i VI III VII", false, { { 0, true }, { 8, false }, { 3, false }, { 10, false } } },
            { "i iv VI V", false, { { 0, true }, { 5, true }, { 8, false }, { 7, false } } },
            { "i VII VI VII", false, { { 0, true }, { 10, false }, { 8, false }, { 10, false } } },
            { "i iv i v", false, { { 0, true }, { 5, true }, { 0, true }, { 7, true } } },
            { "i VI iv V", false, { { 0, true }, { 8, false }, { 5, true }, { 7, false } } },
            { "I V vi IV", true, { { 0, false }, { 7, false }, { 9, true }, { 5, false } } },
            { "I IV V IV", true, { { 0, false }, { 5, false }, { 7, false }, { 5, false } } },
            { "I vi IV V", true, { { 0, false }, { 9, true }, { 5, false }, { 7, false } } },
            { "IV I V vi", true, { { 5, false }, { 0, false }, { 7, false }, { 9, true } } },
        };
        Score sc;
        unsigned seed = 100;
        int perProgExact[9] = {}, perProgCases[9] = {};

        for (std::size_t pi_ = 0; pi_ < progs.size(); ++pi_)
            for (int root : { 9, 0, 6, 2, 4, 10 })
            {
                Beat b;
                static const double bpms[] = { 130.0, 145.0, 160.0 }, tunings[] = { 0.0, 30.0, -15.0 };
                b.bpm = bpms[seed % 3];
                b.cents = tunings[(seed / 3) % 3];
                b.seed = ++seed;
                b.stabs = seed % 2 == 0;
                b.padLevel = 0.05;
                b.bassLevel = 0.6;
                b.kickLevel = 0.5;
                b.kickDecay = 0.06;
                b.melodyRoot = root;
                b.melodyMajor = progs[pi_].major;
                std::vector<ChordSpec> chords;

                for (auto c : progs[pi_].rel)
                    chords.push_back ({ root + c.root, c.minor });

                const int before = sc.exact;
                runCaseWith (sc, root, progs[pi_].major, b, chords, progs[pi_].name);
                perProgExact[pi_] += sc.exact - before;
                ++perProgCases[pi_];
            }

        for (std::size_t pi_ = 0; pi_ < progs.size(); ++pi_)
            std::printf ("    %-14s tom exato %d/%d\n", progs[pi_].name, perProgExact[pi_], perProgCases[pi_]);

        report ("resultado", sc);
        CHECK (sc.exact * 100 >= sc.cases * 75, "tom exato em pelo menos 75%% (%d de %d)", sc.exact, sc.cases);
        CHECK (sc.relative * 100 >= sc.cases * 90, "tom exato ou o relativo em pelo menos 90%% (%d de %d)", sc.relative, sc.cases);
        CHECK (sc.barsRight * 100 >= sc.bars * 85, "acordes por compasso em pelo menos 85%% (%d de %d)", sc.barsRight, sc.bars);
        all.cases += sc.cases; all.exact += sc.exact; all.relative += sc.relative; all.bars += sc.bars; all.barsRight += sc.barsRight;
    }

    /** Arquivos fora do comum: 96 kHz, 8 kHz, meio segundo, NaN no meio, compassos depois do fim. */
    void testEdges()
    {
        std::printf ("Arquivos fora do comum\n");

        for (double rate : { 96000.0, 8000.0 })
        {
            Beat b;
            b.sampleRate = rate;
            b.chords = normalised (progression (9, false));
            const auto audio = render (b);
            const auto a = analyzeBeat (audio.data(), audio.size(), rate);
            const auto bars = a.found() ? chordsForBars (a, barGrid (0.0, b.bpm, 4, 1.0, b.bars), &a.best()) : std::vector<BarChord> {};
            int right = 0;

            for (std::size_t i = 0; i < bars.size(); ++i)
                right += bars[i].found && bars[i].root == b.chords[i % 4].root && bars[i].minor == b.chords[i % 4].minor ? 1 : 0;

            CHECK ((a.found() && a.best() == KeyCandidate { 9, false } && right >= 7),
                   "%.0f Hz: Lá menor e %d de 8 acordes (análise a %.0f Hz)", rate, right, a.analysisRate);
        }

        Beat shortBeat;
        shortBeat.bars = 1;
        shortBeat.chords = normalised (progression (9, false));
        auto tiny = render (shortBeat);
        tiny.resize (22050);
        const auto t = analyzeBeat (tiny.data(), tiny.size(), 44100.0);
        CHECK (t.chroma.size() > 0 && t.seconds < 0.6, "meio segundo: analisa sem cair (%s)", t.found() ? keyNamePt (t.best().root, t.best().major).c_str() : "sem tom");

        Beat b;
        b.chords = normalised (progression (2, true));
        auto broken = render (b);

        for (std::size_t i = 10000; i < broken.size(); i += 7919)
            broken[i] = i % 2 == 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();

        const auto n = analyzeBeat (broken.data(), broken.size(), b.sampleRate);
        CHECK ((n.found() && n.best() == KeyCandidate { 2, true }), "com NaN e infinito no meio: Ré maior do mesmo jeito");

        const auto after = chordsForBars (n, barGrid (1000.0, 140.0, 4, 1.0, 2), &n.best());
        CHECK (after.size() == 2 && ! after[0].found && ! after[1].found, "compassos depois do fim do que foi ouvido ficam sem acorde");
        CHECK (chordsForBars (n, { 1.0 }, nullptr).empty() && analyzeBeat (nullptr, 0, 44100.0).keys.empty(), "entrada vazia não quebra");
    }

    void testLong()
    {
        std::printf ("Beat de 3 minutos: só os primeiros 2 min 30 s, e rápido\n");
        Beat b;
        b.bpm = 80.0;
        b.bars = 60;   // 180 s
        b.chords = normalised (progression (6, false));
        const auto audio = render (b);
        const auto t0 = std::chrono::steady_clock::now();
        const auto a = analyzeBeat (audio.data(), audio.size(), b.sampleRate);
        const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (std::abs (a.seconds - 150.0) < 0.01, "analisou %.1f s de %.1f s", a.seconds, audio.size() / b.sampleRate);
        CHECK ((a.found() && a.best() == KeyCandidate { 6, false }), "Fá# menor (%.0f%%)", a.found() ? 100.0 * a.best().probability : 0.0);
        CHECK (took < 4.0, "levou %.2f s (thread de fundo; limite do teste: 4 s)", took);

        KeyDetectOptions late;
        late.startSeconds = 60.0;
        late.maxSeconds = 30.0;
        const auto l = analyzeBeat (audio.data(), audio.size(), b.sampleRate, late);
        CHECK ((std::abs (l.seconds - 30.0) < 0.01 && std::abs (l.frameTime (0) - 60.0) < 1e-9 && l.found() && l.best() == KeyCandidate { 6, false }),
               "começando em 60 s: 30 s analisados, tempo dos quadros em segundos do arquivo");
    }

    //==========================================================================
    bool readWav (const char* path, std::vector<float>& mono, double& rate)
    {
        std::ifstream f (path, std::ios::binary);
        std::vector<char> d ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());

        if (d.size() < 44 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0)
            return false;

        int channels = 0, bits = 0, format = 0;
        std::size_t pos = 12;

        auto u16 = [&] (std::size_t p) { return (int) (uint8_t) d[p] | ((int) (uint8_t) d[p + 1] << 8); };
        auto u32 = [&] (std::size_t p) { return (uint32_t) u16 (p) | ((uint32_t) u16 (p + 2) << 16); };

        while (pos + 8 <= d.size())
        {
            const uint32_t size = u32 (pos + 4);

            if (std::memcmp (d.data() + pos, "fmt ", 4) == 0)
            {
                format = u16 (pos + 8);
                channels = u16 (pos + 10);
                rate = u32 (pos + 12);
                bits = u16 (pos + 22);

                if (format == 0xFFFE)
                    format = u16 (pos + 32);
            }
            else if (std::memcmp (d.data() + pos, "data", 4) == 0 && channels > 0)
            {
                const int bytes = bits / 8;
                const std::size_t frames = std::min<std::size_t> (size, d.size() - pos - 8) / (size_t) (bytes * channels);
                mono.assign (frames, 0.0f);

                for (std::size_t i = 0; i < frames; ++i)
                    for (int c = 0; c < channels; ++c)
                    {
                        const std::size_t p = pos + 8 + (i * (size_t) channels + (size_t) c) * (size_t) bytes;
                        float v = 0.0f;

                        if (format == 3 && bits == 32)
                            std::memcpy (&v, d.data() + p, 4);
                        else if (bits == 16)
                            v = (int16_t) u16 (p) / 32768.0f;
                        else if (bits == 24)
                            v = (float) (((int32_t) ((uint32_t) u16 (p) << 8 | (uint32_t) (uint8_t) d[p + 2] << 24)) >> 8) / 8388608.0f;

                        mono[i] += v / (float) channels;
                    }

                return true;
            }

            pos += 8 + size + (size & 1);
        }

        return false;
    }

    int analyseFile (const char* path, double bpm, double start)
    {
        std::vector<float> mono;
        double rate = 0.0;

        if (! readWav (path, mono, rate))
        {
            std::printf ("não li o WAV: %s\n", path);
            return 1;
        }

        const auto a = analyzeBeat (mono.data(), mono.size(), rate);

        if (! a.found())
        {
            std::printf ("sem tom (tonalidade %.2f)\n", a.tonalness);
            return 0;
        }

        std::printf ("%s (%.0f%%), afinação %+.1f cents, tonalidade %.2f\n", keyNamePt (a.best().root, a.best().major).c_str(),
                     100.0 * a.best().probability, a.tuningCents, a.tonalness);

        for (int i = 1; i < 4; ++i)
            std::printf ("  %s (%.0f%%) %s\n", keyNamePt (a.keys[(size_t) i].root, a.keys[(size_t) i].major).c_str(),
                         100.0 * a.keys[(size_t) i].probability, keyRelation (a.best(), a.keys[(size_t) i]).c_str());

        const int bars = (int) (a.seconds / (240.0 / bpm));
        const auto chords = chordsForBars (a, barGrid (start, bpm, 4, 1.0, bars), &a.best());

        for (auto& c : chords)
            std::printf ("%s%s", c.bar % 8 == 0 ? "\n  " : " ", c.found ? chordName (c.root, c.minor, false).c_str() : "-");

        std::printf ("\n");
        return 0;
    }
}

int main (int argc, char** argv)
{
    if (argc >= 4 && std::string (argv[1]) == "--wav")
        return analyseFile (argv[2], std::atof (argv[3]), argc >= 5 ? std::atof (argv[4]) : 0.0);

    testBasics();
    Score all;
    testNamedKeys (all);
    testAllKeys (all);
    testHarder (all);
    testTrap (all);
    testEdges();
    testLong();

    std::printf ("\nTaxa de acerto no total: tom exato %d/%d (%.1f%%), exato ou relativo %d/%d (%.1f%%), acordes %d/%d (%.1f%%)\n",
                 all.exact, all.cases, 100.0 * all.exact / std::max (1, all.cases), all.relative, all.cases,
                 100.0 * all.relative / std::max (1, all.cases), all.barsRight, all.bars, 100.0 * all.barsRight / std::max (1, all.bars));
    std::printf ("\n%s: %d falha(s)\n", failures == 0 ? "TUDO OK" : "FALHOU", failures);
    return failures;
}
