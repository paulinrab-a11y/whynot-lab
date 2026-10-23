// Testes da detecção de nota (YIN), sem JUCE: 808 com queda de tom, timbres ricos, ruído e casos-limite.

#include "../../src/dsp/PitchDetect.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

using namespace whynot::dsp;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static constexpr double twoPi = 6.28318530717958647692;

static double keyToHz (double key) { return 440.0 * std::exp2 ((key - 69.0) / 12.0); }

using Channels = std::vector<std::vector<float>>;

static Channels mono (std::vector<float> x) { return Channels { std::move (x) }; }

// Soma de harmônicos com fase contínua; `pitchOffset(t)` em semitons sobre a nota.
template <typename Offset>
static std::vector<float> tone (double key, double seconds, double rate, const std::vector<double>& harmonics,
                                double decayPerSecond, Offset pitchOffset)
{
    std::vector<float> x ((std::size_t) (seconds * rate));
    double phase = 0.0;

    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / rate;
        double s = 0.0;

        for (std::size_t h = 0; h < harmonics.size(); ++h)
            s += harmonics[h] * std::sin (phase * (double) (h + 1));

        x[i] = (float) (0.8 * s * std::exp (-decayPerSecond * t));
        phase += twoPi * keyToHz (key + pitchOffset (t)) / rate;
    }

    return x;
}

static auto noOffset = [] (double) { return 0.0; };

static void report (const char* name, const PitchEstimate& e)
{
    std::printf ("    %s: found=%d %.2f Hz, nota %d %+.1f cents, confiança %.2f\n",
                 name, e.found ? 1 : 0, e.hz, e.key, e.cents, e.confidence);
}

static void testPureTones()
{
    std::printf ("Senoides\n");

    const auto c2 = detectSamplePitch (mono (tone (36, 1.0, 48000.0, { 1.0 }, 1.0, noOffset)), 48000.0);
    report ("Dó 2", c2);
    CHECK (c2.found && c2.key == 36 && std::abs (c2.cents) < 3.0, "Dó 2 (65,4 Hz) a 48 kHz vira a nota 36");

    const auto a4 = detectSamplePitch (mono (tone (69, 0.5, 44100.0, { 1.0 }, 0.0, noOffset)), 44100.0);
    report ("Lá 4", a4);
    CHECK (a4.found && a4.key == 69 && std::abs (a4.hz - 440.0) < 1.0, "Lá 440 a 44,1 kHz vira a nota 69");

    const auto sharp = detectSamplePitch (mono (tone (57.3, 1.0, 48000.0, { 1.0 }, 0.5, noOffset)), 48000.0);
    report ("Lá 3 +30", sharp);
    CHECK (sharp.found && sharp.key == 57 && std::abs (sharp.cents - 30.0) < 3.0, "Lá 3 30 cents acima: nota 57, +30 cents");

    const auto flat = detectSamplePitch (mono (tone (43.6, 1.0, 48000.0, { 1.0 }, 0.5, noOffset)), 48000.0);
    report ("Lá b 2 -40", flat);
    CHECK (flat.found && flat.key == 44 && std::abs (flat.cents + 40.0) < 3.0, "nota 43,6 arredonda para 44 com -40 cents");
}

static void test808()
{
    std::printf ("808\n");
    constexpr double rate = 48000.0;

    // Si 0 (30,9 Hz) que começa uma oitava acima e cai em ~60 ms, com saturação forte.
    auto x = tone (23, 1.5, rate, { 1.0 }, 1.2, [] (double t) { return 12.0 * std::exp (-t / 0.02); });

    for (auto& s : x)
        s = (float) std::tanh (3.0 * s) * 0.7f;

    const auto e = detectSamplePitch (mono (x), rate);
    report ("808 Si 0", e);
    CHECK (e.found && e.key == 23, "808 grave saturado com queda de tom: nota 23 (Si 0), não a do começo");
    CHECK (e.confidence > 0.6, "confiança alta no 808 (%.2f)", e.confidence);

    // Fá 1 com queda de 7 semitons mais lenta (150 ms): a nota do corpo vence.
    const auto f1 = detectSamplePitch (mono (tone (29, 2.0, rate, { 1.0, 0.3 }, 0.8,
                                                    [] (double t) { return 7.0 * std::exp (-t / 0.05); })), rate);
    report ("808 Fá 1", f1);
    CHECK (f1.found && f1.key == 29, "808 Fá 1 com queda lenta: nota 29");
}

static void testRichTimbres()
{
    std::printf ("Timbres com harmônicos\n");
    constexpr double rate = 48000.0;

    std::vector<double> saw;

    for (int h = 1; h <= 30; ++h)
        saw.push_back (1.0 / h);

    const auto s = detectSamplePitch (mono (tone (43, 1.0, rate, saw, 0.5, noOffset)), rate);
    report ("dente de serra Sol 2", s);
    CHECK (s.found && s.key == 43, "dente de serra Sol 2: nota 43, sem erro de oitava");

    // Segundo harmônico mais forte que a fundamental: o erro clássico é dar uma oitava acima.
    const auto strong2 = detectSamplePitch (mono (tone (48, 1.0, rate, { 0.4, 1.0, 0.3 }, 0.5, noOffset)), rate);
    report ("2º harmônico forte Dó 3", strong2);
    CHECK (strong2.found && strong2.key == 48, "fundamental fraca e 2º harmônico forte: continua na nota 48");

    // Sample com uma nota curta antes da principal: a mais longa decide.
    auto pickup = tone (50, 0.25, rate, { 1.0, 0.5 }, 0.0, noOffset);
    const auto body = tone (45, 1.2, rate, { 1.0, 0.5 }, 0.6, noOffset);
    pickup.insert (pickup.end(), body.begin(), body.end());
    const auto p = detectSamplePitch (mono (pickup), rate);
    report ("Ré 3 curto + Lá 2", p);
    CHECK (p.found && p.key == 45, "nota curta antes da principal: fica a principal (45)");

    // Estéreo com som só na esquerda.
    Channels stereo { tone (60, 0.8, rate, { 1.0, 0.4 }, 0.5, noOffset), std::vector<float> ((std::size_t) (0.8 * rate), 0.0f) };
    const auto st = detectSamplePitch (stereo, rate);
    CHECK (st.found && st.key == 60, "estéreo com som só num canal: nota 60");
}

static void testNoPitch()
{
    std::printf ("Sem tom e casos-limite\n");
    constexpr double rate = 48000.0;

    std::mt19937 rng (7);
    std::uniform_real_distribution<float> uni (-0.5f, 0.5f);
    std::vector<float> noise ((std::size_t) rate);

    for (auto& s : noise)
        s = uni (rng);

    const auto n = detectSamplePitch (mono (noise), rate);
    report ("ruído", n);
    CHECK (! n.found, "ruído branco não tem nota");

    // "Chimbal": ruído curto com decaimento rápido.
    std::vector<float> hat ((std::size_t) (0.12 * rate));

    for (std::size_t i = 0; i < hat.size(); ++i)
        hat[i] = uni (rng) * (float) std::exp (-40.0 * (double) i / rate);

    CHECK (! detectSamplePitch (mono (hat), rate).found, "chimbal (ruído curto) não tem nota");
    CHECK (! detectSamplePitch (mono (std::vector<float> ((std::size_t) rate, 0.0f)), rate).found, "silêncio não tem nota");
    CHECK (! detectSamplePitch ({}, rate).found, "sem canais: sem nota, sem quebrar");
    CHECK (! detectSamplePitch (mono (std::vector<float> (10, 0.3f)), rate).found, "10 amostras: sem nota, sem quebrar");
    CHECK (! detectSamplePitch (mono (tone (60, 0.5, rate, { 1.0 }, 0.0, noOffset)), 0.0).found, "taxa zero: sem nota");
    CHECK (! detectSamplePitch (mono (tone (60, 0.5, rate, { 1.0 }, 0.0, noOffset)),
                                std::numeric_limits<double>::quiet_NaN()).found, "taxa NaN: sem nota");

    // NaN e infinito no meio do sample são ignorados.
    auto dirty = tone (52, 1.0, rate, { 1.0, 0.3 }, 0.5, noOffset);
    dirty[1000] = std::numeric_limits<float>::quiet_NaN();
    dirty[20000] = std::numeric_limits<float>::infinity();
    const auto d = detectSamplePitch (mono (dirty), rate);
    CHECK (d.found && d.key == 52 && std::isfinite (d.hz), "NaN e infinito no sample: continua na nota 52");

    // Nota curta (80 ms), menor que um quadro inteiro do grave: ainda acha a nota aguda.
    const auto shortNote = detectSamplePitch (mono (tone (72, 0.08, rate, { 1.0, 0.2 }, 0.0, noOffset)), rate);
    report ("Dó 5 de 80 ms", shortNote);
    CHECK (shortNote.found && shortNote.key == 72, "Dó 5 de 80 ms: nota 72");
}

static void testSpeed()
{
    std::printf ("Tempo\n");
    constexpr double rate = 48000.0;
    const auto x = mono (tone (36, 4.0, rate, { 1.0, 0.5, 0.25 }, 0.3, noOffset));

    const auto t0 = std::chrono::steady_clock::now();
    const auto e = detectSamplePitch (x, rate);
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();

    std::printf ("    sample de 4 s: %.1f ms\n", ms);
    CHECK (e.found && e.key == 36, "sample de 4 s: nota 36");
    CHECK (ms < 500.0, "análise de um sample de 4 s em menos de 0,5 s (%.0f ms)", ms);
}

int main()
{
    testPureTones();
    test808();
    testRichTimbres();
    testNoPitch();
    testSpeed();

    std::printf ("\n%s (%d falha%s)\n", failures == 0 ? "TUDO OK" : "HOUVE FALHAS", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
