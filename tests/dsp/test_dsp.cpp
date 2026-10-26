// Testes dos efeitos nativos (sem JUCE): filtros, sobreamostragem, loudness, dinâmica,
// limitador, espaço, imagem, bypass do host, o catálogo de presets e estilos e os efeitos
// de voz e beat (gate, compressores vintage, multibanda, nivelador, dobrador, Grave+, limpa-voz, console).

#include "../../src/dsp/Color.h"
#include "../../src/dsp/Console.h"
#include "../../src/dsp/Denoise.h"
#include "../../src/dsp/Dynamics.h"
#include "../../src/dsp/DynamicEq.h"
#include "../../src/dsp/Enhance.h"
#include "../../src/dsp/EqProcessor.h"
#include "../../src/dsp/FxCatalog.h"
#include "../../src/dsp/FxHost.h"
#include "../../src/dsp/Gate.h"
#include "../../src/dsp/Grit.h"
#include "../../src/dsp/LoudnessMeter.h"
#include "../../src/dsp/Master.h"
#include "../../src/dsp/Modulation.h"
#include "../../src/dsp/Multiband.h"
#include "../../src/dsp/Space.h"
#include "../../src/dsp/Tune.h"
#include "../../src/dsp/Vintage.h"
#include "../../src/dsp/Vocal.h"
#include "../../src/dsp/VoicePitch.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace whynot::dsp;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

constexpr double rate = 48000.0;
constexpr int block = 512;

static std::vector<float> sine (double freq, double amplitude, int length, double phase = 0.0)
{
    std::vector<float> x ((size_t) length);

    for (int i = 0; i < length; ++i)
        x[(size_t) i] = (float) (amplitude * std::sin (2.0 * pi * freq * i / rate + phase));

    return x;
}

static double rms (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    to = to == 0 ? x.size() : to;
    double s = 0.0;

    for (size_t i = from; i < to; ++i)
        s += (double) x[i] * x[i];

    return std::sqrt (s / (double) std::max<size_t> (1, to - from));
}

static double peak (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    to = to == 0 ? x.size() : to;
    double p = 0.0;

    for (size_t i = from; i < to; ++i)
        p = std::max (p, (double) std::abs (x[i]));

    return p;
}

static double db (double g)   { return 20.0 * std::log10 (std::max (1.0e-12, g)); }

/** Amplitude de uma frequência (Goertzel), normalizada para um seno de amplitude 1 dar 1. */
static double toneAmplitude (const std::vector<float>& x, double freq, size_t from, size_t to)
{
    const double w = 2.0 * pi * freq / rate;
    const double c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;

    for (size_t i = from; i < to; ++i)
    {
        const double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
    return 2.0 * std::sqrt (re * re + im * im) / (double) (to - from);
}

/** Processa estéreo em blocos por um Processor (ou FxHost). */
template <typename P>
static void run (P& p, std::vector<float>& l, std::vector<float>& r,
                 const std::vector<float>* sideL = nullptr, const std::vector<float>* sideR = nullptr)
{
    for (size_t start = 0; start < l.size(); start += block)
    {
        const int n = (int) std::min<size_t> (block, l.size() - start);

        if constexpr (std::is_same_v<P, FxHost>)
        {
            p.process (l.data() + start, r.data() + start, n,
                       sideL != nullptr ? sideL->data() + start : nullptr,
                       sideR != nullptr ? sideR->data() + start : nullptr);
        }
        else
        {
            AudioBlock b { l.data() + start, r.data() + start, n,
                           sideL != nullptr ? sideL->data() + start : nullptr,
                           sideR != nullptr ? sideR->data() + start : nullptr };
            p.process (b);
        }
    }
}

static void setAll (Processor& p, const FxInfo& info, const Preset* preset = nullptr)
{
    const auto v = presetValues (info, preset);

    for (size_t i = 0; i < v.size(); ++i)
        p.setParam ((int) i, v[i]);
}

static std::unique_ptr<FxHost> makeHost (const char* type, const char* preset = nullptr)
{
    auto h = std::make_unique<FxHost> (createProcessor (type));
    h->prepare (rate, block);

    if (preset != nullptr)
    {
        const auto v = presetValues (h->info(), findPreset (type, preset));

        for (size_t i = 0; i < v.size(); ++i)
            h->setParam ((int) i, v[i]);
    }

    return h;
}

//==============================================================================
static void testFilters()
{
    std::printf ("Filtros\n");

    // Sino de +6 dB em 1 kHz: o ganho medido no centro tem que ser 6 dB.
    const auto bell = SvfCoeffs::make (FilterType::bell, 1000.0, 1.0, 6.0, rate);
    Svf f;
    auto x = sine (1000.0, 0.25, 48000);

    for (auto& s : x)
        s = f.process (s, bell);

    const double measured = db (rms (x, 24000) / (0.25 / std::sqrt (2.0)));
    CHECK (std::abs (measured - 6.0) < 0.05, "sino +6 dB em 1 kHz mede %.3f dB", measured);
    CHECK (std::abs (bell.magnitudeDb (1000.0, rate) - 6.0) < 0.01, "resposta analítica do sino = 6 dB");

    // Prateleiras: medido = analítico em várias frequências.
    for (auto type : { FilterType::lowShelf, FilterType::highShelf })
    {
        const auto c = SvfCoeffs::make (type, 800.0, 0.7071, -9.0, rate);
        double worst = 0.0;

        for (double freq : { 50.0, 400.0, 800.0, 2000.0, 9000.0 })
        {
            Svf s;
            auto y = sine (freq, 0.25, 48000);

            for (auto& v : y)
                v = s.process (v, c);

            const double m = db (toneAmplitude (y, freq, 24000, 48000) / 0.25);
            worst = std::max (worst, std::abs (m - c.magnitudeDb (freq, rate)));
        }

        CHECK (worst < 0.05, "prateleira %s: medido = analítico (erro máx %.3f dB)",
               type == FilterType::lowShelf ? "grave" : "aguda", worst);
    }

    // Prateleira grave de -9 dB: no grave, -9; no agudo, 0.
    const auto ls = SvfCoeffs::make (FilterType::lowShelf, 800.0, 0.7071, -9.0, rate);
    CHECK (std::abs (ls.magnitudeDb (20.0, rate) + 9.0) < 0.1 && std::abs (ls.magnitudeDb (15000.0, rate)) < 0.1,
           "prateleira grave: -9 dB embaixo, 0 dB em cima");

    // Linkwitz-Riley: grave + agudo somam plano.
    double worst = 0.0;

    for (double freq : { 40.0, 100.0, 150.0, 250.0, 1000.0 })
    {
        LinkwitzRiley4 lr;
        lr.setup (150.0, rate);
        auto y = sine (freq, 0.25, 48000);

        for (auto& v : y)
        {
            float lo, hi;
            lr.process (v, lo, hi);
            v = lo + hi;
        }

        worst = std::max (worst, std::abs (db (toneAmplitude (y, freq, 24000, 48000) / 0.25)));
    }

    CHECK (worst < 0.05, "divisor LR4 soma plano (erro máx %.3f dB)", worst);

    // Butterworth de 8ª ordem em 100 Hz: -3 dB no corte, ~-48 dB uma oitava abaixo.
    double hpAtCut = 0.0, hpOctave = 0.0;

    for (int s = 0; s < 4; ++s)
    {
        const auto c = SvfCoeffs::make (FilterType::highpass, 100.0, butterworthQ (4, s), 0.0, rate);
        hpAtCut += c.magnitudeDb (100.0, rate);
        hpOctave += c.magnitudeDb (50.0, rate);
    }

    CHECK (std::abs (hpAtCut + 3.01) < 0.1 && hpOctave < -46.0 && hpOctave > -50.0,
           "passa-altas 48 dB/oit: %.2f dB no corte, %.1f dB uma oitava abaixo", hpAtCut, hpOctave);
}

//==============================================================================
static void testOversampler()
{
    std::printf ("Sobreamostragem 4x\n");

    Oversampler4x os;
    os.prepare();
    const int latency = os.latency();
    std::vector<float> y (400, 0.0f);

    for (int i = 0; i < 400; ++i)
        y[(size_t) i] = os.process (i == 10 ? 1.0f : 0.0f, [] (float v) { return v; });

    size_t at = 0;

    for (size_t i = 0; i < y.size(); ++i)
        if (std::abs (y[i]) > std::abs (y[at]))
            at = i;

    CHECK ((int) at - 10 == latency, "impulso volta com atraso %d (latência informada %d)", (int) at - 10, latency);

    os.reset();
    auto x = sine (1000.0, 0.5, 24000);

    for (auto& v : x)
        v = os.process (v, [] (float s) { return s; });

    CHECK (std::abs (db (toneAmplitude (x, 1000.0, 4000, 24000) / 0.5)) < 0.02, "1 kHz passa sem perda");

    os.reset();
    x = sine (18000.0, 0.5, 24000);

    for (auto& v : x)
        v = os.process (v, [] (float s) { return s; });

    CHECK (std::abs (db (toneAmplitude (x, 18000.0, 4000, 24000) / 0.5)) < 0.2, "18 kHz passa (%.2f dB)",
           db (toneAmplitude (x, 18000.0, 4000, 24000) / 0.5));

    // Clip duro em 15 kHz: o 3º harmônico (45 kHz) dobraria para 3 kHz sem sobreamostragem.
    auto clipped = sine (15000.0, 1.0, 48000);
    auto naive = clipped;
    os.reset();

    for (auto& v : clipped)
        v = os.process (v, [] (float s) { return std::clamp (s * 4.0f, -1.0f, 1.0f); });

    for (auto& v : naive)
        v = std::clamp (v * 4.0f, -1.0f, 1.0f);

    const double aliasOs = db (toneAmplitude (clipped, 3000.0, 4000, 48000));
    const double aliasNaive = db (toneAmplitude (naive, 3000.0, 4000, 48000));
    CHECK (aliasOs < -45.0 && aliasNaive > -20.0, "aliasing em 3 kHz: %.1f dB com 4x, %.1f dB sem", aliasOs, aliasNaive);
}

//==============================================================================
static void testLoudness()
{
    std::printf ("Loudness (BS.1770 / EBU R128)\n");

    LoudnessMeter m;
    m.prepare (rate);

    // Seno de 1 kHz estéreo com pico de -23 dBFS por canal = -23 LUFS.
    auto l = sine (1000.0, std::pow (10.0, -23.0 / 20.0), (int) rate * 10);
    auto r = l;
    m.process (l.data(), r.data(), (int) l.size());
    CHECK (std::abs (m.integrated() + 23.0f) < 0.1f, "1 kHz a -23 dBFS mede %.2f LUFS integrado", m.integrated());
    CHECK (std::abs (m.momentary() + 23.0f) < 0.1f && std::abs (m.shortTerm() + 23.0f) < 0.1f,
           "momentâneo %.2f e curto prazo %.2f", m.momentary(), m.shortTerm());
    CHECK (std::abs (m.correlation() - 1.0f) < 0.01f, "correlação de sinal mono = 1");

    // Silêncio depois não derruba o integrado (porta absoluta).
    std::vector<float> silence ((size_t) rate * 10, 0.0f);
    m.process (silence.data(), silence.data(), (int) silence.size());
    CHECK (std::abs (m.integrated() + 23.0f) < 0.1f, "silêncio fica fora do integrado (%.2f)", m.integrated());

    // Porta relativa: 10 s a -20 e 10 s a -40 (20 dB abaixo, fora da porta de -10 LU).
    m.requestReset();
    auto loud = sine (1000.0, std::pow (10.0, -20.0 / 20.0), (int) rate * 10);
    auto quiet = sine (1000.0, std::pow (10.0, -40.0 / 20.0), (int) rate * 10);
    m.process (loud.data(), loud.data(), (int) loud.size());
    m.process (quiet.data(), quiet.data(), (int) quiet.size());
    CHECK (std::abs (m.integrated() + 20.0f) < 0.2f, "porta relativa ignora o trecho baixo (%.2f)", m.integrated());

    // Pico real: seno de fs/4 com fase de 45° tem amostras em 0,707 mas o pico real é 1,0.
    m.requestReset();
    auto tp = sine (12000.0, 1.0, 48000, pi / 4.0);
    const double samplePeak = db (peak (tp));
    m.process (tp.data(), tp.data(), (int) tp.size());
    CHECK (samplePeak < -2.9 && std::abs (m.truePeak()) < 0.3f, "pico real %.2f dBTP (pico de amostra %.2f dBFS)",
           m.truePeak(), samplePeak);

    // Fora de fase: correlação -1.
    m.requestReset();
    auto a = sine (500.0, 0.5, 48000);
    auto b = a;

    for (auto& v : b)
        v = -v;

    m.process (a.data(), b.data(), (int) a.size());
    CHECK (m.correlation() < -0.99f, "fase invertida dá correlação -1");
}

//==============================================================================
static void testDynamics()
{
    std::printf ("Dinâmica\n");

    CHECK (std::abs (compressorGainDb (-6.0f, -18.0f, 4.0f, 0.0f) + 9.0f) < 1.0e-4f, "curva 4:1: 12 dB acima viram 3 (-9 dB)");
    CHECK (compressorGainDb (-30.0f, -18.0f, 4.0f, 6.0f) == 0.0f, "abaixo do joelho não comprime");
    CHECK (compressorGainDb (-18.0f, -18.0f, 4.0f, 6.0f) < 0.0f && compressorGainDb (-18.0f, -18.0f, 4.0f, 6.0f) > -1.0f,
           "no limiar com joelho comprime um pouco");

    {
        auto host = makeHost ("comp");
        const auto& info = host->info();
        host->setParam (info.indexOf ("threshold"), -18.0f);
        host->setParam (info.indexOf ("ratio"), 4.0f);
        host->setParam (info.indexOf ("knee"), 0.0f);
        host->setParam (info.indexOf ("attack"), 1.0f);
        host->setParam (info.indexOf ("release"), 300.0f);
        host->setParam (info.indexOf ("detector"), 1.0f);   // RMS: sem ondulação no seno

        // Seno com RMS de -6 dBFS: 12 dB acima do limiar → 9 dB de redução.
        auto l = sine (1000.0, std::pow (10.0, -6.0 / 20.0) * std::sqrt (2.0), 48000);
        auto r = l;
        run (*host, l, r);
        const double out = db (rms (l, 36000));
        CHECK (std::abs (out + 15.0) < 0.5, "compressor estático: -6 dBFS RMS sai em %.2f (esperado -15)", out);
        CHECK (std::abs (host->processor().meter (CompressorProcessor::gainReduction) - 9.0f) < 0.6f,
               "medidor de redução mostra %.2f dB", host->processor().meter (CompressorProcessor::gainReduction));
    }

    {
        // Sidechain: o "kick" alto abaixa o "808"; sem kick, nada.
        auto host = makeHost ("comp", "sidechain-duck");
        auto bassL = sine (50.0, 0.1, 48000);
        auto bassR = bassL;
        std::vector<float> kick (48000, 0.0f);

        for (int i = 24000; i < 26000; ++i)
            kick[(size_t) i] = 0.8f * (float) std::sin (2.0 * pi * 60.0 * i / rate);

        run (*host, bassL, bassR, &kick, &kick);
        const double before = rms (bassL, 12000, 23000);
        const double during = rms (bassL, 24500, 25900);
        CHECK (std::abs (db (before / (0.1 / std::sqrt (2.0)))) < 0.5, "sem kick o 808 passa intacto");
        CHECK (db (during / before) < -10.0, "com o kick o 808 abaixa %.1f dB", db (during / before));
    }

    {
        // Transiente: ataque positivo aumenta o pico de um som percussivo.
        auto makeHits = []
        {
            std::vector<float> x (48000, 0.0f);

            for (int hit = 0; hit < 4; ++hit)
                for (int i = 0; i < 6000; ++i)
                    x[(size_t) (hit * 12000 + i)] = (float) (0.3 * std::exp (-i / 1500.0) * std::sin (2.0 * pi * 120.0 * i / rate));

            return x;
        };

        auto dryL = makeHits();
        auto host = makeHost ("transient");
        host->setParam (0, 100.0f);
        auto l = makeHits(), r = makeHits();
        run (*host, l, r);
        CHECK (peak (l, 12000, 18000) > peak (dryL, 12000, 18000) * 1.3, "ataque +100%% realça o pico (%.2f dB)",
               db (peak (l, 12000, 18000) / peak (dryL, 12000, 18000)));

        auto short_ = makeHost ("transient");
        short_->setParam (1, -100.0f);
        l = makeHits();
        r = makeHits();
        run (*short_, l, r);
        CHECK (rms (l, 15000, 18000) < rms (dryL, 15000, 18000) * 0.7, "sustentação -100%% encurta a cauda (%.2f dB)",
               db (rms (l, 15000, 18000) / rms (dryL, 15000, 18000)));
    }

    {
        auto host = makeHost ("deesser");
        auto l = sine (6500.0, 0.5, 24000), r = l;
        run (*host, l, r);
        CHECK (host->processor().meter (DeEsserProcessor::reduction) > 3.0f, "de-esser reduz o chiado (%.1f dB)",
               host->processor().meter (DeEsserProcessor::reduction));

        auto calm = makeHost ("deesser");
        l = sine (200.0, 0.5, 24000);
        r = l;
        run (*calm, l, r);
        CHECK (calm->processor().meter (DeEsserProcessor::reduction) < 0.5f, "de-esser não mexe no grave");
    }
}

//==============================================================================
static void testColor()
{
    std::printf ("Saturador e filtro\n");

    {
        auto host = makeHost ("sat");
        const int latency = host->latencySamples();
        host->setParam (host->info().indexOf ("mix"), 0.0f);
        host->setParam (host->info().indexOf ("autoGain"), 0.0f);
        auto in = sine (1000.0, 0.5, 24000);
        auto l = in, r = in;
        run (*host, l, r);

        double err = 0.0;

        for (size_t i = 12000; i < 24000; ++i)
            err = std::max (err, (double) std::abs (l[i] - in[i - (size_t) latency]));

        CHECK (latency > 0 && err < 0.01, "mistura 0%%: sai o original atrasado %d amostras (erro %.4f)", latency, err);
    }

    {
        auto host = makeHost ("sat");
        host->setParam (host->info().indexOf ("drive"), 24.0f);
        auto l = sine (110.0, 0.2, 96000), r = l;
        const double inRms = rms (l);
        run (*host, l, r);
        CHECK (std::abs (db (rms (l, 48000) / inRms)) < 2.0, "compensar volume: drive de 24 dB sai a %.2f dB do original",
               db (rms (l, 48000) / inRms));
        CHECK (toneAmplitude (l, 330.0, 48000, 96000) > 0.01, "a saturação cria harmônicos (3º em 330 Hz)");
    }

    {
        // Proteger graves: um seno de 40 Hz passa limpo mesmo com drive alto; os harmônicos
        // nascem dele, mas só aparecem acima da frequência protegida.
        auto host = makeHost ("sat");
        host->setParam (host->info().indexOf ("drive"), 30.0f);
        host->setParam (host->info().indexOf ("mode"), 3.0f);
        host->setParam (host->info().indexOf ("protect"), 300.0f);
        host->setParam (host->info().indexOf ("autoGain"), 0.0f);
        auto l = sine (40.0, 0.3, 96000), r = l;
        run (*host, l, r);
        const double fundamental = toneAmplitude (l, 40.0, 48000, 96000);
        const double third = toneAmplitude (l, 120.0, 48000, 96000);
        const double high = toneAmplitude (l, 600.0, 48000, 96000);   // 15º harmônico
        CHECK (std::abs (db (fundamental / 0.3)) < 0.5 && db (third / fundamental) < -20.0 && db (high / fundamental) > -40.0,
               "sub protegido: fundamental %.2f dB, 3º harmônico %.1f dB abaixo, 15º presente (%.1f dB)",
               db (fundamental / 0.3), db (third / fundamental), db (high / fundamental));
    }

    for (int m = 0; m < 5; ++m)
    {
        const float small = saturate ((SaturationMode) m, 0.001f);
        CHECK (std::abs (small / 0.001f - 1.0f) < 0.05f, "curva %d tem ganho ~1 em sinal baixo", m);
    }

    {
        auto host = makeHost ("filter");   // passa-baixas em 1 kHz
        auto l = sine (8000.0, 0.5, 24000), r = l;
        run (*host, l, r);
        CHECK (db (rms (l, 12000) / (0.5 / std::sqrt (2.0))) < -30.0, "passa-baixas em 1 kHz corta 8 kHz");
    }
}

//==============================================================================
static void testLimiter()
{
    std::printf ("Limitador\n");

    auto host = makeHost ("limiter");
    const auto& info = host->info();
    host->setParam (info.indexOf ("gain"), 12.0f);
    host->setParam (info.indexOf ("ceiling"), -1.0f);

    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 0.25f);
    std::vector<float> l (96000), r (96000);

    for (size_t i = 0; i < l.size(); ++i)
    {
        l[i] = noise (rng) + 0.5f * (float) std::sin (2.0 * pi * 60.0 * i / rate);
        r[i] = noise (rng) + 0.5f * (float) std::sin (2.0 * pi * 60.0 * i / rate);
    }

    run (*host, l, r);
    const double ceil = std::pow (10.0, -1.0 / 20.0);
    CHECK (peak (l) <= ceil + 1.0e-6 && peak (r) <= ceil + 1.0e-6, "nenhuma amostra passa do teto (%.3f dBFS)", db (std::max (peak (l), peak (r))));

    LoudnessMeter tp;
    tp.prepare (rate);
    tp.process (l.data() + 4800, r.data() + 4800, (int) l.size() - 4800);
    CHECK (tp.truePeak() < -0.7f, "pico real na saída %.2f dBTP (teto -1)", tp.truePeak());

    auto& lim = static_cast<LimiterProcessor&> (host->processor());
    CHECK (lim.meter (LimiterProcessor::gainReduction) > 3.0f, "medidor mostra a redução (%.1f dB)", lim.meter (LimiterProcessor::gainReduction));
    CHECK (lim.loudness().integrated() > -12.0f, "medidor de loudness do limitador mede (%.1f LUFS)", lim.loudness().integrated());

    // Abaixo do teto e sem ganho, o limitador só atrasa (latência informada certa).
    auto clean = makeHost ("limiter");
    std::vector<float> imp (4000, 0.0f), impR (4000, 0.0f);
    imp[100] = impR[100] = 0.5f;
    run (*clean, imp, impR);
    size_t at = 0;

    for (size_t i = 0; i < imp.size(); ++i)
        if (std::abs (imp[i]) > std::abs (imp[at]))
            at = i;

    CHECK ((int) at - 100 == clean->latencySamples(), "latência do limitador: %d amostras (informada %d)",
           (int) at - 100, clean->latencySamples());
    CHECK (std::abs (imp[at] - 0.5f) < 0.02f, "sinal abaixo do teto passa intacto (%.3f)", imp[at]);

    // Clipper: com 100% a saída fica no teto e quase sem redução no limitador.
    auto clipper = makeHost ("limiter");
    clipper->setParam (info.indexOf ("gain"), 6.0f);
    clipper->setParam (info.indexOf ("clip"), 100.0f);
    auto cl = sine (100.0, 0.9, 48000), cr = cl;
    run (*clipper, cl, cr);
    CHECK (peak (cl, 24000) <= ceil + 1.0e-6, "clipper respeita o teto");
}

//==============================================================================
static void testSpace()
{
    std::printf ("Reverb e delay\n");

    {
        auto host = makeHost ("reverb");
        host->setParam (host->info().indexOf ("mix"), 100.0f);
        host->setParam (host->info().indexOf ("decay"), 1.0f);
        host->setParam (host->info().indexOf ("predelay"), 0.0f);
        std::vector<float> l ((size_t) rate * 3, 0.0f), r = l;
        l[100] = r[100] = 1.0f;
        run (*host, l, r);

        bool finite = true;

        for (size_t i = 0; i < l.size(); ++i)
            finite &= std::isfinite (l[i]) && std::isfinite (r[i]);

        const double early = rms (l, 2400, 7200);           // 50-150 ms
        const double late = rms (l, 48000 + 2400, 48000 + 7200);   // 1 s depois
        CHECK (finite, "reverb sem NaN nem infinito");
        CHECK (db (late / early) < -45.0 && db (late / early) > -80.0, "decaimento de 1 s: %.1f dB em 1 s (esperado ~-60)",
               db (late / early));

        double corr = 0.0, ll = 0.0, rr = 0.0;

        for (size_t i = 2400; i < 24000; ++i)
        {
            corr += (double) l[i] * r[i];
            ll += (double) l[i] * l[i];
            rr += (double) r[i] * r[i];
        }

        CHECK (std::abs (corr / std::sqrt (ll * rr)) < 0.5, "cauda estéreo (correlação %.2f)", corr / std::sqrt (ll * rr));
    }

    {
        auto host = makeHost ("reverb");
        host->setParam (host->info().indexOf ("mix"), 100.0f);
        host->setParam (host->info().indexOf ("decay"), 12.0f);
        host->setParam (host->info().indexOf ("size"), 100.0f);
        auto l = sine (300.0, 0.5, (int) rate * 8), r = l;
        run (*host, l, r);
        CHECK (std::isfinite (peak (l)) && peak (l) < 8.0, "cauda de 12 s com sinal constante continua estável (pico %.2f)", peak (l));
    }

    {
        auto host = makeHost ("delay");
        const auto& info = host->info();
        host->setParam (info.indexOf ("mix"), 100.0f);
        host->setParam (info.indexOf ("feedback"), 0.0f);
        host->setParam (info.indexOf ("mode"), 0.0f);
        host->setParam (info.indexOf ("hpf"), 20.0f);
        host->setParam (info.indexOf ("lpf"), 20000.0f);
        host->setParam (info.indexOf ("bpm"), 120.0f);   // 1/4 = 500 ms
        std::vector<float> l (48000, 0.0f), r = l;
        l[100] = r[100] = 1.0f;
        run (*host, l, r);
        size_t at = 0;

        for (size_t i = 0; i < l.size(); ++i)
            if (std::abs (l[i]) > std::abs (l[at]))
                at = i;

        CHECK (std::abs ((int) at - 100 - 24000) <= 2, "1/4 a 120 BPM ecoa em %.1f ms", (at - 100) * 1000.0 / rate);
        CHECK (std::abs (DelayProcessor::delayMs (presetValues (info, findPreset ("delay", "dotted-eighth")).data()) - 375.0) < 0.01,
               "1/8 pontuado a 120 BPM = 375 ms");
    }

    {
        // Pingue-pongue: primeiro eco na esquerda, o segundo na direita.
        auto host = makeHost ("delay");
        const auto& info = host->info();
        host->setParam (info.indexOf ("mix"), 100.0f);
        host->setParam (info.indexOf ("feedback"), 50.0f);
        host->setParam (info.indexOf ("hpf"), 20.0f);
        host->setParam (info.indexOf ("lpf"), 20000.0f);
        host->setParam (info.indexOf ("bpm"), 120.0f);
        std::vector<float> l (60000, 0.0f), r = l;
        l[100] = r[100] = 1.0f;
        run (*host, l, r);
        CHECK (std::abs (l[24100]) > 0.3f && std::abs (r[24100]) < 0.05f, "pingue-pongue: 1º eco à esquerda");
        CHECK (std::abs (r[48100]) > 0.1f && std::abs (l[48100]) < 0.05f, "pingue-pongue: 2º eco à direita");
    }
}

//==============================================================================
static void testStereo()
{
    std::printf ("Estéreo\n");

    {
        auto host = makeHost ("stereo");
        host->setParam (host->info().indexOf ("width"), 0.0f);
        auto l = sine (500.0, 0.5, 24000), r = sine (700.0, 0.5, 24000);
        run (*host, l, r);
        double diff = 0.0;

        for (size_t i = 12000; i < l.size(); ++i)
            diff = std::max (diff, (double) std::abs (l[i] - r[i]));

        CHECK (diff < 1.0e-4, "largura 0%% = mono");
    }

    {
        auto host = makeHost ("stereo", "bass-mono");
        auto l = sine (50.0, 0.5, 48000), r = l;

        for (auto& v : r)
            v = -v;   // grave só no lado (fora de fase)

        auto hl = sine (3000.0, 0.5, 48000), hr = hl;

        for (auto& v : hr)
            v = -v;

        run (*host, l, r);
        auto hostHi = makeHost ("stereo", "bass-mono");
        run (*hostHi, hl, hr);
        CHECK (db (rms (l, 24000) / (0.5 / std::sqrt (2.0))) < -30.0, "grave mono: 50 Hz fora de fase some (%.1f dB)",
               db (rms (l, 24000) / (0.5 / std::sqrt (2.0))));
        CHECK (std::abs (db (rms (hl, 24000) / (0.5 / std::sqrt (2.0)))) < 0.2, "agudo estéreo continua intacto");
    }

    {
        auto host = makeHost ("stereo");
        host->setParam (host->info().indexOf ("invertRight"), 1.0f);
        auto l = sine (500.0, 0.5, 24000), r = l;
        run (*host, l, r);
        CHECK (host->processor().meter (StereoProcessor::correlation) < -0.95f, "inverter a fase da direita: correlação -1");
    }
}

//==============================================================================
static void testHostAndEq()
{
    std::printf ("Host (bypass) e EQ\n");

    {
        // Bypass num efeito com latência: sai o seco atrasado, sem salto.
        auto host = makeHost ("sat", "808-hard");
        const int latency = host->latencySamples();
        auto in = sine (200.0, 0.3, 48000);
        auto l = in, r = in;

        for (size_t start = 0; start < l.size(); start += block)
        {
            if (start == 12288)
                host->setBypassed (true);

            host->process (l.data() + start, r.data() + start, (int) std::min<size_t> (block, l.size() - start));
        }

        double err = 0.0, jump = 0.0;

        for (size_t i = 30000; i < 48000; ++i)
            err = std::max (err, (double) std::abs (l[i] - in[i - (size_t) latency]));

        for (size_t i = 12000; i < 16000; ++i)
            jump = std::max (jump, (double) std::abs (l[i] - l[i - 1]));

        CHECK (err < 1.0e-5, "em bypass sai o original alinhado (erro %.6f)", err);
        CHECK (jump < 0.1, "troca de bypass sem estalo (maior salto %.3f)", jump);
    }

    {
        // EQ: o desenho (responseDb) bate com o som em várias frequências.
        auto host = makeHost ("eq", "vocal-trap");
        std::vector<float> v;

        for (int i = 0; i < EqProcessor::count; ++i)
            v.push_back (host->getParam (i));

        double worst = 0.0;

        for (double freq : { 60.0, 100.0, 250.0, 1000.0, 3000.0, 11000.0 })
        {
            auto h = makeHost ("eq", "vocal-trap");
            auto l = sine (freq, 0.2, 48000), r = l;
            run (*h, l, r);
            const double measured = db (toneAmplitude (l, freq, 24000, 48000) / 0.2);
            worst = std::max (worst, std::abs (measured - EqProcessor::responseDb (v.data(), freq, rate)));
        }

        CHECK (worst < 0.1, "curva desenhada = som medido (erro máx %.3f dB)", worst);
    }
}

//==============================================================================
/** Maior salto entre amostras vizinhas num trecho (estalo = salto bem maior que o do sinal). */
static double maxJump (const std::vector<float>& x, size_t from, size_t to)
{
    double j = 0.0;

    for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
        j = std::max (j, (double) std::abs (x[i] - x[i - 1]));

    return j;
}

/** Toca um seno grave, muda um parâmetro no meio e mede o maior salto em volta da troca. */
static double jumpWhenChanging (const char* type, const char* preset, const char* param, float from, float to)
{
    auto host = makeHost (type, preset);
    const int index = host->info().indexOf (param);
    host->setParam (index, from);
    auto l = sine (60.0, 0.5, 48000), r = l;

    for (size_t start = 0; start < l.size(); start += block)
    {
        if (start == 24576)
            host->setParam (index, to);

        host->process (l.data() + start, r.data() + start, (int) std::min<size_t> (block, l.size() - start));
    }

    // O "normal" é o maior salto do som parado antes e depois da troca (a cor pode mudar).
    return maxJump (l, 24576 - 4096, 24576 + 8192) / std::max (maxJump (l, 12000, 20000), maxJump (l, 38000, 47000));
}

static void testSmoothChanges()
{
    std::printf ("Mudanças sem estalo e render sem rampa\n");

    {
        // Valores definidos antes do prepare (como no render): a 1ª amostra já sai certa.
        FxHost host (createProcessor ("stereo"));
        host.setParam (host.info().indexOf ("gain"), -12.0f);
        host.prepare (rate, block);
        auto l = sine (1000.0, 0.5, 2048), r = l;
        const auto in = l;
        run (host, l, r);
        double worst = 0.0;

        for (size_t i = 0; i < 512; ++i)
            worst = std::max (worst, (double) std::abs (l[i] - in[i] * std::pow (10.0, -12.0 / 20.0)));

        CHECK (worst < 1.0e-4, "render começa sem rampa (erro %.6f na 1ª amostra)", worst);
    }

    {
        FxHost host (createProcessor ("limiter"));
        host.setParam (host.info().indexOf ("ceiling"), -3.0f);
        host.setParam (host.info().indexOf ("gain"), 12.0f);
        host.prepare (rate, block);
        auto l = sine (100.0, 0.9, 4800), r = l;
        run (host, l, r);
        CHECK (peak (l) <= std::pow (10.0, -3.0 / 20.0) + 1.0e-6, "teto escolhido vale desde o começo do render");
    }

    { const double j = jumpWhenChanging ("sat", "808-trap", "protect", 100.0f, 0.0f); CHECK (j < 1.5, "desligar proteger graves não estala (salto %.2fx o normal)", j); }
    { const double j = jumpWhenChanging ("sat", nullptr, "mode", 0.0f, 4.0f); CHECK (j < 1.5, "trocar a curva do saturador não estala (salto %.2fx o normal)", j); }
    { const double j = jumpWhenChanging ("stereo", "master-mono-low", "monoBass", 100.0f, 0.0f); CHECK (j < 1.5, "desligar grave mono não estala (salto %.2fx o normal)", j); }
    { const double j = jumpWhenChanging ("eq", "808-trap", "hpOn", 1.0f, 0.0f); CHECK (j < 1.5, "desligar o passa-altas do EQ não estala (salto %.2fx o normal)", j); }
    { const double j = jumpWhenChanging ("eq", "vocal-trap", "hpSlope", 1.0f, 2.0f); CHECK (j < 1.5, "trocar a inclinação do EQ não estala (salto %.2fx o normal)", j); }
    { const double j = jumpWhenChanging ("filter", "intro-muffled", "slope", 1.0f, 0.0f); CHECK (j < 1.5, "trocar a inclinação do filtro não estala (salto %.2fx o normal)", j); }
    { const double j = jumpWhenChanging ("filter", "lofi", "mode", 0.0f, 1.0f); CHECK (j < 1.5, "trocar o tipo do filtro não estala (salto %.2fx o normal)", j); }
    { const double j = jumpWhenChanging ("limiter", "master-trap", "clip", 50.0f, 0.0f); CHECK (j < 1.5, "desligar o clipper não estala (salto %.2fx o normal)", j); }

    {
        // NaN na entrada (de um plugin de terceiros): o efeito não trava.
        auto host = makeHost ("reverb", "plate-vocal");
        auto l = sine (300.0, 0.5, 24000), r = l;
        l[5000] = std::numeric_limits<float>::quiet_NaN();
        r[5001] = std::numeric_limits<float>::infinity();
        run (*host, l, r);
        bool finite = true;

        for (size_t i = 0; i < l.size(); ++i)
            finite &= std::isfinite (l[i]) && std::isfinite (r[i]);

        CHECK (finite && rms (l, 12000) > 0.01, "NaN na entrada vira silêncio e o reverb continua tocando");
    }

    {
        // Bypass total zera o delay aos poucos: ao voltar, nada do eco antigo.
        auto host = makeHost ("delay", "throw");
        host->setParam (host->info().indexOf ("bpm"), 120.0f);
        auto l = sine (500.0, 0.5, 4800), r = l;
        l.resize (96000, 0.0f);
        r.resize (96000, 0.0f);

        for (size_t start = 0; start < l.size(); start += block)
        {
            if (start == 5120)  host->setBypassed (true);
            if (start == 60416) host->setBypassed (false);
            host->process (l.data() + start, r.data() + start, (int) std::min<size_t> (block, l.size() - start));
        }

        size_t at = 61440;

        for (size_t i = 61440; i < l.size(); ++i)
            if (std::abs (l[i]) > std::abs (l[at]))
                at = i;

        CHECK (peak (l, 61440) < 1.0e-6, "delay volta do bypass sem eco velho (limpeza em partes; resto %.6f em %d)", peak (l, 61440), (int) at);
    }
}

//==============================================================================
static void testCatalog()
{
    std::printf ("Catálogo, presets e estilos\n");

    CHECK (allEffects().size() == 30, "30 efeitos nativos (%d)", (int) allEffects().size());

    // O menu do mixer abre uma seção por categoria nova: cada categoria vem junta na lista.
    {
        std::vector<std::string> seen;
        bool grouped = true;

        for (size_t i = 0; i < allEffects().size(); ++i)
        {
            const std::string cat = allEffects()[i]->category;

            if (i > 0 && cat != allEffects()[i - 1]->category && std::find (seen.begin(), seen.end(), cat) != seen.end())
                grouped = false;

            if (std::find (seen.begin(), seen.end(), cat) == seen.end())
                seen.push_back (cat);
        }

        CHECK (grouped, "efeitos agrupados por categoria (%d categorias)", (int) seen.size());
    }

    bool effectsOk = true;

    for (auto* e : allEffects())
    {
        auto p = createProcessor (e->type);
        effectsOk &= p != nullptr && &p->info() == e;

        for (auto& param : e->params)
            effectsOk &= param.def >= param.min && param.def <= param.max
                         && (param.kind != ParamKind::choice || (int) param.choices.size() == (int) param.max + 1);
    }

    CHECK (effectsOk, "todo efeito cria o processador certo e tem padrões dentro da faixa");

    bool presetsOk = true;

    for (auto& preset : allPresets())
    {
        const auto* info = findEffect (preset.type);
        presetsOk &= info != nullptr;

        if (info == nullptr)
        {
            std::printf ("    preset de efeito desconhecido: %s\n", preset.type);
            continue;
        }

        for (auto& v : preset.values)
        {
            const int i = info->indexOf (v.param);

            if (i < 0 || info->params[(size_t) i].clamp (v.value) != v.value)
            {
                presetsOk = false;
                std::printf ("    preset %s/%s: parâmetro %s inválido\n", preset.type, preset.id, v.param);
            }
        }
    }

    CHECK (presetsOk, "%d presets válidos (parâmetros existem e valores dentro da faixa)", (int) allPresets().size());

    bool stylesOk = true;

    auto checkChain = [&stylesOk] (const MixStyle& s, std::span<const ChainItem> chain)
    {
        stylesOk &= chain.size() <= 8;

        for (auto& item : chain)
            if (findEffect (item.type) == nullptr || (item.preset != nullptr && findPreset (item.type, item.preset) == nullptr)
                || (item.sidechainRole != nullptr && findRole (item.sidechainRole) == nullptr))
            {
                stylesOk = false;
                std::printf ("    estilo %s: %s/%s inválido\n", s.id, item.type, item.preset != nullptr ? item.preset : "-");
            }
    };

    for (auto& s : allStyles())
    {
        for (auto& r : s.roles)
        {
            stylesOk &= findRole (r.role) != nullptr;
            checkChain (s, r.chain);
        }

        checkChain (s, s.master);
        checkChain (s, s.reverbBus);
        checkChain (s, s.delayBus);
        stylesOk &= s.recipeFor ("vocal") != nullptr && s.recipeFor ("bass") != nullptr;
    }

    CHECK (allStyles().size() == 4 && stylesOk, "estilos trap, funk, rap e pop completos e válidos");

    CHECK (std::string (guessRole ("Kick")) == "kick" && std::string (guessRole ("808")) == "bass"
           && std::string (guessRole ("Clap")) == "snare" && std::string (guessRole ("Open hat")) == "hats"
           && std::string (guessRole ("Bell")) == "melody", "papéis do kit WHYNOT adivinhados pelo nome");
    CHECK (std::string (guessRole ("Voz principal")) == "vocal" && std::string (guessRole ("Backing vocal")) == "backing"
           && std::string (guessRole ("Ad-libs")) == "backing" && std::string (guessRole ("BD 2")) == "kick"
           && std::string (guessRole ("Reverb")) == "reverbBus" && std::string (guessRole ("Primo")) == "melody",
           "papéis de nomes comuns (voz, backing, ad-libs, siglas)");
    CHECK (std::string (guessRole ("Beat")) == "beat" && std::string (guessRole ("Instrumental final")) == "beat"
           && std::string (guessRole ("Type beat 140")) == "beat" && std::string (guessRole ("Base do funk")) == "beat"
           && std::string (guessRole ("808 base")) == "bass" && std::string (guessRole ("Beatbox voz")) == "vocal",
           "beat pronto de 2 pistas tem papel próprio (Beat, Instrumental, Type beat, Base)");
    {
        bool beatOk = true;

        for (auto& st : allStyles())
        {
            const auto* recipe = st.recipeFor ("beat");
            beatOk &= recipe != nullptr && recipe->reverbSendDb == noSend && recipe->delaySendDb == noSend;

            if (recipe != nullptr)
            {
                bool keyedByVoice = false;

                for (auto& item : recipe->chain)
                    keyedByVoice |= std::string_view (item.type) == "multiband" && item.sidechainRole != nullptr
                                    && std::string_view (item.sidechainRole) == "vocal";

                beatOk &= keyedByVoice;
            }
        }

        CHECK (beatOk, "todo estilo tem a receita do beat pronto, com a multibanda disparada pela voz e sem envios");
    }

    // Todo efeito, com cada preset, processa ruído sem NaN e sem explodir.
    bool stable = true;

    for (auto* e : allEffects())
    {
        std::vector<const Preset*> list = presetsFor (e->type);
        list.push_back (nullptr);

        for (auto* preset : list)
        {
            auto p = createProcessor (e->type);
            p->prepare (rate, block);
            setAll (*p, *e, preset);

            std::mt19937 rng (3);
            std::normal_distribution<float> noise (0.0f, 0.2f);
            std::vector<float> l (24000), r (24000);

            for (size_t i = 0; i < l.size(); ++i)
            {
                l[i] = noise (rng);
                r[i] = noise (rng);
            }

            run (*p, l, r, &r, &l);
            const double pk = std::max (peak (l), peak (r));

            if (! std::isfinite (pk) || pk > 30.0)
            {
                stable = false;
                std::printf ("    %s/%s instável (pico %.2f)\n", e->type, preset != nullptr ? preset->id : "padrão", pk);
            }
        }
    }

    CHECK (stable, "todos os efeitos e presets estáveis com ruído");
}

//==============================================================================
// Afinação: voz sintética (pulso com formantes de "a") desafinada que tem que sair afinada.

/** Voz sintética: harmônicos até 5 kHz moldados por três formantes (700, 1220, 2600 Hz). */
static std::vector<float> syntheticVoice (double (*hzAt) (double), double seconds, double amplitude = 0.3)
{
    const int n = (int) (seconds * rate);
    std::vector<float> x ((size_t) n);
    double phase = 0.0, peakValue = 0.0;
    const double formants[3][2] = { { 700.0, 130.0 }, { 1220.0, 150.0 }, { 2600.0, 250.0 } };

    for (int i = 0; i < n; ++i)
    {
        const double f0 = hzAt (i / rate);
        phase += f0 / rate;
        phase -= std::floor (phase);
        double s = 0.0;
        const int harmonics = std::min (60, (int) (5000.0 / f0));

        for (int k = 1; k <= harmonics; ++k)
        {
            double env = 0.15;

            for (auto& f : formants)
            {
                const double q = (k * f0 - f[0]) / f[1];
                env += 1.0 / (1.0 + q * q);
            }

            s += env / std::sqrt ((double) k) * std::sin (2.0 * pi * k * phase);
        }

        x[(size_t) i] = (float) s;
        peakValue = std::max (peakValue, std::abs (s));
    }

    for (auto& v : x)
        v = (float) (v / peakValue * amplitude);

    return x;
}

/** Tom por autocorrelação normalizada (independente do detector do efeito), em Hz. */
static double measurePitch (const std::vector<float>& x, double from, double to)
{
    const int a = (int) (from * rate), b = std::min ((int) x.size(), (int) (to * rate));
    const int lagMin = (int) (rate / 1000.0), lagMax = (int) (rate / 60.0);
    const int n = b - a - lagMax;

    if (n <= 0)
        return 0.0;

    std::vector<double> r ((size_t) lagMax + 2, 0.0);
    double best = 0.0;

    for (int lag = lagMin; lag <= lagMax + 1; ++lag)
    {
        double num = 0.0, e1 = 0.0, e2 = 0.0;

        for (int j = a; j < a + n; ++j)
        {
            num += (double) x[(size_t) j] * x[(size_t) (j + lag)];
            e1 += (double) x[(size_t) j] * x[(size_t) j];
            e2 += (double) x[(size_t) (j + lag)] * x[(size_t) (j + lag)];
        }

        r[(size_t) lag] = num / std::sqrt (std::max (1.0e-12, e1 * e2));
        best = std::max (best, r[(size_t) lag]);
    }

    // O primeiro pico perto do máximo (evita escolher o dobro do período).
    for (int lag = lagMin + 1; lag <= lagMax; ++lag)
    {
        const double v = r[(size_t) lag];

        if (v > 0.9 * best && v >= r[(size_t) lag - 1] && v >= r[(size_t) lag + 1])
        {
            const double y0 = r[(size_t) lag - 1], y1 = v, y2 = r[(size_t) lag + 1];
            const double den = y0 - 2.0 * y1 + y2;
            const double t = lag + (std::abs (den) > 1.0e-12 ? 0.5 * (y0 - y2) / den : 0.0);
            return rate / t;
        }
    }

    return 0.0;
}

static double cents (double hz, double reference)   { return 1200.0 * std::log2 (hz / reference); }

struct TuneRun
{
    std::vector<float> left, right;
    int latency = 0;
};

static TuneRun runTune (const std::vector<float>& x, std::initializer_list<std::pair<int, float>> params)
{
    TuneProcessor t;
    t.prepare (rate, block);

    for (auto [index, value] : params)
        t.setParam (index, value);

    t.snapToTargets();
    TuneRun out { x, x, t.latencySamples() };

    for (size_t pos = 0; pos < x.size(); pos += block)
    {
        AudioBlock b { out.left.data() + pos, out.right.data() + pos, (int) std::min<size_t> (block, x.size() - pos) };
        t.process (b);
    }

    return out;
}

static double hzSharpA (double)    { return 440.0 * std::pow (2.0, 40.0 / 1200.0); }   // Lá 4 + 40 cents
static double hzFlatA (double)     { return 440.0 * std::pow (2.0, -35.0 / 1200.0); }  // Lá 4 - 35 cents
static double hzSharpCs (double)   { return 554.365 * std::pow (2.0, 30.0 / 1200.0); }  // Dó# 5 + 30 cents
static double hzGlide (double t)   { return 430.0 + 10.0 * t; }                         // 430 a 450 Hz em 2 s
static double hzBetween (double)   { return 452.0; }                                    // entre Lá e Lá#
static double hzSharpThenTuned (double t) { return t < 0.6 ? hzSharpA (t) : 440.0; }       // desafinado, depois afinado

static void testTune()
{
    std::printf ("Afinação\n");
    using P = TuneProcessor;

    // O medidor do teste confere a própria entrada.
    const auto sharp = syntheticVoice (hzSharpA, 1.6);
    CHECK (std::abs (cents (measurePitch (sharp, 0.5, 0.6), 440.0) - 40.0) < 3.0,
           "medidor do teste: entrada a %.1f cents do Lá", cents (measurePitch (sharp, 0.5, 0.6), 440.0));

    auto worstCents = [] (const std::vector<float>& y, double target, double from, double to, double step)
    {
        double worst = 0.0;

        for (double t = from; t + step <= to + 1.0e-9; t += step)
            worst = std::max (worst, std::abs (cents (measurePitch (y, t, t + step), target)));

        return worst;
    };

    {
        const auto out = runTune (sharp, { { P::key, 9.0f }, { P::scale, 0.0f }, { P::speed, 0.0f } });
        const double w = worstCents (out.left, 440.0, 0.4, 1.5, 0.1);
        CHECK (w < 8.0, "Lá + 40 cents vira Lá (pior janela: %.1f cents)", w);
        CHECK (out.left == out.right, "entrada igual nos dois lados sai igual nos dois lados");
        CHECK (out.latency > 0 && out.latency < (int) (0.02 * rate), "latência fixa de %.1f ms", 1000.0 * out.latency / rate);
    }

    {
        const auto out = runTune (syntheticVoice (hzFlatA, 1.6), { { P::key, 9.0f }, { P::scale, 0.0f }, { P::speed, 0.0f } });
        const double w = worstCents (out.left, 440.0, 0.4, 1.5, 0.1);
        CHECK (w < 8.0, "Lá - 35 cents vira Lá (pior janela: %.1f cents)", w);
    }

    {
        // Dó# não está em Dó maior: vai para a vizinha mais perto (Ré, a 70 cents; Dó fica a 130).
        const auto out = runTune (syntheticVoice (hzSharpCs, 1.6), { { P::key, 0.0f }, { P::scale, 1.0f }, { P::speed, 0.0f } });
        const double w = worstCents (out.left, 587.33, 0.4, 1.5, 0.1);
        CHECK (w < 8.0, "Dó# + 30 cents em Dó maior vira Ré (pior janela: %.1f cents)", w);
    }

    {
        const auto out = runTune (syntheticVoice (hzBetween, 1.6), { { P::scale, 6.0f }, { P::speed, 0.0f } });
        const double w = worstCents (out.left, 440.0, 0.4, 1.5, 0.1);
        CHECK (w < 8.0, "cromática: 452 Hz vai para o semitom mais perto, Lá (pior janela: %.1f cents)", w);
    }

    {
        // Glissando de -39 a +39 cents: a saída fica parada no Lá.
        const auto in = syntheticVoice (hzGlide, 2.0);
        const auto out = runTune (in, { { P::key, 9.0f }, { P::scale, 0.0f }, { P::speed, 0.0f } });
        const double w = worstCents (out.left, 440.0, 0.3, 1.9, 0.1);
        CHECK (w < 10.0, "glissando 430-450 Hz sai parado no Lá (pior janela: %.1f cents)", w);

        // Sem estalo nas emendas: a segunda diferença da saída fica perto da da entrada.
        auto roughness = [] (const std::vector<float>& y, size_t from)
        {
            double m = 0.0;

            for (size_t i = from + 2; i < y.size(); ++i)
                m = std::max (m, (double) std::abs (y[i] - 2.0f * y[i - 1] + y[i - 2]));

            return m;
        };

        const double rIn = roughness (in, (size_t) (0.1 * rate)), rOut = roughness (out.left, (size_t) (0.1 * rate));
        CHECK (rOut < rIn * 2.0, "emendas sem estalo (aspereza %.3f contra %.3f da entrada)", rOut, rIn);
    }

    {
        // Velocidade lenta: começa perto do tom de entrada e chega no alvo depois.
        const auto out = runTune (sharp, { { P::key, 9.0f }, { P::scale, 0.0f }, { P::speed, 300.0f } });
        const double early = cents (measurePitch (out.left, 0.12, 0.2), 440.0);
        const double late = cents (measurePitch (out.left, 1.3, 1.5), 440.0);
        CHECK (early > 15.0 && std::abs (late) < 6.0, "velocidade 300 ms: %.1f cents no começo, %.1f no fim", early, late);
    }

    {
        // Força 0 e mistura 0: a saída é a entrada atrasada, igual.
        const auto noForce = runTune (sharp, { { P::amount, 0.0f } });
        const auto noMix = runTune (sharp, { { P::mix, 0.0f }, { P::speed, 0.0f } });
        double d1 = 0.0, d2 = 0.0;

        for (size_t i = (size_t) noForce.latency; i < sharp.size(); ++i)
        {
            d1 = std::max (d1, (double) std::abs (noForce.left[i] - sharp[i - (size_t) noForce.latency]));
            d2 = std::max (d2, (double) std::abs (noMix.left[i] - sharp[i - (size_t) noMix.latency]));
        }

        CHECK (d1 < 1.0e-6 && d2 < 1.0e-6, "força 0 e mistura 0 = entrada atrasada (diferença %.1e e %.1e)", d1, d2);
    }

    {
        // Ruído (consoante) e silêncio: passam sem mudar o nível e sem NaN.
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> u (-0.1f, 0.1f);
        std::vector<float> noise ((size_t) rate);

        for (auto& v : noise)
            v = u (rng);

        const auto out = runTune (noise, { { P::speed, 0.0f } });
        bool finite = true;

        for (auto v : out.left)
            finite = finite && std::isfinite (v);

        const double inDb = 20.0 * std::log10 (rms (noise, (size_t) (0.1 * rate))), outDb = 20.0 * std::log10 (rms (out.left, (size_t) (0.1 * rate)));
        CHECK (finite && std::abs (outDb - inDb) < 1.0, "ruído passa sem NaN e no mesmo nível (%.2f dB de diferença)", outDb - inDb);

        const auto silent = runTune (std::vector<float> ((size_t) (0.5 * rate), 0.0f), {});
        CHECK (peak (silent.left) == 0.0, "silêncio continua silêncio");
    }

    {
        // Com Mistura 50%, depois que a correção acaba a voz corrigida volta a alinhar com a original:
        // a saída fica igual à entrada atrasada (sem o pente de um atraso fixo).
        const auto in = syntheticVoice (hzSharpThenTuned, 2.0);
        const auto out = runTune (in, { { P::key, 9.0f }, { P::scale, 0.0f }, { P::speed, 0.0f }, { P::mix, 50.0f } });
        double d = 0.0;

        for (size_t i = (size_t) (1.6 * rate); i < in.size(); ++i)
            d = std::max (d, (double) std::abs (out.left[i] - in[i - (size_t) out.latency]));

        CHECK (d < 1.0e-3, "mistura 50%%: sem correção, volta a alinhar com o seco (diferença %.1e)", d);
    }

    {
        // NaN na entrada: a saída nunca fica NaN e a detecção volta a funcionar.
        auto in = sharp;
        in[(size_t) (0.5 * rate)] = std::numeric_limits<float>::quiet_NaN();
        const auto out = runTune (in, { { P::key, 9.0f }, { P::scale, 0.0f }, { P::speed, 0.0f } });
        bool finite = true;

        for (auto v : out.left)
            finite = finite && std::isfinite (v);

        const double w = std::abs (cents (measurePitch (out.left, 1.3, 1.5), 440.0));
        CHECK (finite && w < 8.0, "NaN na entrada: saída sempre finita e continua afinando (%.1f cents)", w);
    }

    CHECK (TuneProcessor::nearestInScale (61.2f, 0, TuneProcessor::scaleMask (1)) == 62
           && TuneProcessor::nearestInScale (60.4f, 0, TuneProcessor::scaleMask (1)) == 60
           && TuneProcessor::scaleMask (6) == 0xFFF,
           "escalas: nota mais perto em Dó maior e cromática com as 12");
}

//==============================================================================
// Pico real antes do MP3: a função de export limita a -1 dBTP e não mexe no que já está abaixo.

static void testTruePeakOffline()
{
    std::printf ("Pico real antes do MP3\n");

    // Seno quase em Nyquist/4 com fase que cria pico entre amostras, empurrado acima de 0 dBFS.
    const int n = (int) (2.0 * rate);
    std::vector<float> l ((size_t) n), r ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        l[(size_t) i] = (float) (1.4 * std::sin (2.0 * pi * 11025.0 * t + 0.785) * (0.5 + 0.5 * std::sin (2.0 * pi * 3.0 * t)));
        r[(size_t) i] = (float) (0.9 * std::sin (2.0 * pi * 220.0 * t));
    }

    LoudnessMeter before;
    before.prepare (rate);
    before.process (l.data(), r.data(), n);

    float* io[2] = { l.data(), r.data() };
    const float reduction = limitTruePeakOffline (io, 2, n, rate, -1.0f);

    LoudnessMeter after;
    after.prepare (rate);
    after.process (l.data(), r.data(), n);

    CHECK (before.truePeak() > 2.0f && after.truePeak() <= -0.9f,
           "pico real %.2f dBTP vira %.2f dBTP (teto -1)", before.truePeak(), after.truePeak());
    CHECK (reduction > 2.0f, "a função conta a redução aplicada (%.1f dB)", reduction);

    // Abaixo do teto: sai igual, alinhado (sem a latência do limitador).
    std::vector<float> q ((size_t) n), original;

    for (int i = 0; i < n; ++i)
        q[(size_t) i] = (float) (0.5 * std::sin (2.0 * pi * 440.0 * i / rate));

    original = q;
    float* mono[1] = { q.data() };
    const float none = limitTruePeakOffline (mono, 1, n, rate, -1.0f);
    double diff = 0.0;

    for (int i = 0; i < n; ++i)
        diff = std::max (diff, (double) std::abs (q[(size_t) i] - original[(size_t) i]));

    CHECK (none < 0.01f && diff < 5.0e-4, "áudio abaixo do teto passa alinhado e praticamente igual (diferença %.1e, -%.0f dB; redução %.3f dB)",
           diff, -20.0 * std::log10 (std::max (1.0e-12, diff / 0.5)), none);
}

//==============================================================================
// Efeitos novos de 21/10 (noite): gate, compressores óptico e FET, EQ vintage, multibanda,
// nivelador, dobrador, Grave+, limpa-voz, console e o humanizar da afinação.

/** Atraso que alinha melhor y com x (correlação cruzada em 0..maxLag). */
static int bestLag (const std::vector<float>& x, const std::vector<float>& y, int maxLag, size_t from, size_t to)
{
    int best = 0;
    double bestValue = -1.0e30;

    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double acc = 0.0;

        for (size_t i = from; i < to; ++i)
            acc += (double) x[i] * y[i + (size_t) lag];

        if (acc > bestValue)
        {
            bestValue = acc;
            best = lag;
        }
    }

    return best;
}

/** Amplitude de `freq` em ~1 s a partir de `from`, com um número inteiro de ciclos (erro abaixo de 0,001 dB). */
static double amplitudeAt (const std::vector<float>& x, double freq, size_t from)
{
    const double cycles = std::max (1.0, std::floor (freq * 1.0));
    const size_t length = (size_t) std::lround (cycles * rate / freq);
    return toneAmplitude (x, freq, from, std::min (x.size(), from + length));
}

static std::vector<float> whiteNoise (int length, double rmsLevel, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> n (0.0f, (float) rmsLevel);
    std::vector<float> x ((size_t) length);

    for (auto& v : x)
        v = n (rng);

    return x;
}

static void setByName (FxHost& h, const char* id, float v)
{
    const int i = h.info().indexOf (id);

    if (i < 0)
        std::printf ("    parâmetro desconhecido: %s\n", id);

    h.setParam (i, v);
}

static void testGate()
{
    std::printf ("Gate e expansor\n");

    // Rajadas de 1 kHz (-12 dBFS de pico) sobre ruído a -60 dBFS RMS: 0,5 s ligada, 0,5 s desligada.
    const int n = 4 * 48000;
    auto noise = whiteNoise (n, std::pow (10.0, -60.0 / 20.0), 7);
    std::vector<float> x ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        const bool on = (i / 24000) % 2 == 0;
        x[(size_t) i] = noise[(size_t) i] + (on ? (float) (0.25 * std::sin (2.0 * pi * 1000.0 * i / rate)) : 0.0f);
    }

    auto host = makeHost ("gate");
    const int lat = host->latencySamples();
    CHECK (lat == (int) std::lround (GateProcessor::lookaheadSeconds * rate), "antecipação de 1,5 ms informada: %d amostras", lat);

    auto l = x, r = x;
    run (*host, l, r);

    // No silêncio (depois da soltura), o ruído desce a faixa inteira (30 dB).
    const double inNoise = rms (x, 24000 + 14400, 48000);
    const double outNoise = rms (l, 24000 + 14400 + (size_t) lat, 48000);
    CHECK (db (outNoise / inNoise) < -28.0, "o gate fecha no silêncio: o ruído cai %.1f dB (faixa 30)", db (outNoise / inNoise));

    // Na rajada, a saída é a entrada atrasada pela antecipação, sem mudar o nível.
    double worst = 0.0;

    for (size_t i = 48000 + 4800; i < 48000 + 24000 - 4800; ++i)
        worst = std::max (worst, (double) std::abs (l[i + (size_t) lat] - x[i]));

    CHECK (worst < 1.0e-6, "aberto, sai a entrada atrasada em %d amostras (diferença máx. %.1e)", lat, worst);

    // O começo da rajada não é comido: a antecipação abre antes de o som chegar.
    const double onset = rms (l, 48000 + (size_t) lat, 48000 + (size_t) lat + 48);
    const double onsetIn = rms (x, 48000, 48000 + 48);
    CHECK (db (onset / onsetIn) > -1.0, "o primeiro milissegundo da rajada passa (%.2f dB)", db (onset / onsetIn));

    // Expansor 2:1: 10 dB abaixo do limiar = 10 dB a menos.
    {
        auto e = makeHost ("gate");
        setByName (*e, "mode", 1.0f);
        setByName (*e, "threshold", -30.0f);
        setByName (*e, "ratio", 2.0f);
        setByName (*e, "range", 40.0f);
        auto el = sine (1000.0, std::pow (10.0, -40.0 / 20.0), 48000), er = el;
        const auto original = el;
        run (*e, el, er);
        const double change = db (rms (el, 24000) / rms (original, 24000));
        CHECK (std::abs (change + 10.0) < 1.0, "expansor 2:1 a 10 dB abaixo do limiar: %.2f dB (esperado -10)", change);
    }

    // Histerese: um som oscilando 1 dB em volta do limiar não fica abrindo e fechando.
    {
        auto h = makeHost ("gate");
        setByName (*h, "threshold", -30.0f);
        setByName (*h, "hold", 0.0f);
        std::vector<float> wobble (48000 * 2);

        for (size_t i = 0; i < wobble.size(); ++i)
        {
            const double levelDb = -30.0 + 1.0 * std::sin (2.0 * pi * 3.0 * i / rate);
            wobble[i] = (float) (std::pow (10.0, levelDb / 20.0) * std::sin (2.0 * pi * 500.0 * i / rate));
        }

        int transitions = 0;
        float previous = 0.0f;
        auto other = wobble;

        for (size_t start = 0; start < wobble.size(); start += 256)
        {
            h->process (wobble.data() + start, other.data() + start, 256);
            const float open = h->processor().meter (GateProcessor::openState);
            transitions += start > 0 && open != previous ? 1 : 0;
            previous = open;
        }

        CHECK (transitions <= 1, "com histerese de 4 dB, oscilar ±1 dB no limiar não faz o gate trepidar (%d trocas)", transitions);
    }

    // Disparo externo: o ruído só passa quando a outra trilha toca.
    {
        auto h = makeHost ("gate");
        auto ml = whiteNoise (48000, 0.1, 3), mr = ml;
        const auto original = ml;
        std::vector<float> key (48000, 0.0f);

        for (int i = 24000; i < 48000; ++i)
            key[(size_t) i] = 0.5f * (float) std::sin (2.0 * pi * 100.0 * i / rate);

        run (*h, ml, mr, &key, &key);
        CHECK (db (rms (ml, 12000, 23000) / rms (original, 12000, 23000)) < -28.0
                   && std::abs (db (rms (ml, 30000, 47000) / rms (original, 30000 - (size_t) lat, 47000 - (size_t) lat))) < 0.5,
               "disparo externo: fechado sem a outra trilha, aberto com ela");
    }
}

static void testVintage()
{
    std::printf ("Compressores óptico e FET, EQ vintage\n");

    // Óptico: redução 50 → limiar -20 dB; seno com pico de -6 dBFS (o detector lê o pico) → 14 dB acima, 3:1.
    {
        auto h = makeHost ("opto");
        setByName (*h, "reduction", 50.0f);
        auto l = sine (1000.0, std::pow (10.0, -6.0 / 20.0), 48000 * 3), r = l;
        run (*h, l, r);
        const double gr = -db (rms (l, 48000 * 2) / rms (sine (1000.0, std::pow (10.0, -6.0 / 20.0), 48000), 0));
        CHECK (std::abs (gr - 14.0 * 2.0 / 3.0) < 0.6, "óptico 3:1, 14 dB acima do limiar: reduz %.2f dB (esperado %.2f)", gr, 14.0 * 2.0 / 3.0);
    }

    // Soltura em dois estágios e memória: depois de 3 s comprimindo, sobra mais redução que depois de 0,3 s.
    auto residualAfter = [] (double compressSeconds)
    {
        auto h = makeHost ("opto");
        setByName (*h, "reduction", 60.0f);
        const int loud = (int) (compressSeconds * rate), quiet = 48000 * 2;
        std::vector<float> x ((size_t) (loud + quiet));

        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) ((i < (size_t) loud ? 0.5 : 0.005) * std::sin (2.0 * pi * 1000.0 * (double) i / rate));

        auto l = x, r = x;
        run (*h, l, r);
        // Redução que sobra 150 ms e 1,5 s depois de o som baixar (o sinal baixo não comprime nada sozinho).
        auto grAt = [&] (double t)
        {
            const size_t a = (size_t) (loud + t * rate), b = a + 480;
            return -db (rms (l, a, b) / rms (x, a, b));
        };

        return std::pair<double, double> (grAt (0.15), grAt (1.5));
    };

    const auto [shortEarly, shortLate] = residualAfter (0.3);
    const auto [longEarly, longLate] = residualAfter (3.0);
    CHECK (longEarly > 1.0 && longEarly < 0.6 * 14.4 && longLate < longEarly * 0.6,
           "óptico solta em dois estágios: 150 ms depois sobram %.1f dB, 1,5 s depois %.1f dB", longEarly, longLate);
    CHECK (longEarly > shortEarly + 0.3, "memória: depois de 3 s comprimindo sobra mais (%.1f dB) que depois de 0,3 s (%.1f dB)",
           longEarly, shortEarly);

    // FET 4:1, limiar -20, seno de -6 dBFS de pico: 14 dB acima → 10,5 dB de redução. Cor em 0.
    {
        auto h = makeHost ("fet");
        setByName (*h, "color", 0.0f);
        setByName (*h, "release", 300.0f);
        auto l = sine (1000.0, std::pow (10.0, -6.0 / 20.0), 48000 * 2), r = l;
        run (*h, l, r);
        const double gr = h->processor().meter (FetProcessor::gainReduction);
        CHECK (std::abs (gr - 10.5) < 0.6, "FET 4:1, 14 dB acima: reduz %.2f dB (esperado 10,5)", gr);
    }

    // FET "Todos": reduz mais que o 20:1 (passa do limiar e esmaga).
    {
        auto grFor = [] (float ratioChoice)
        {
            auto h = makeHost ("fet");
            setByName (*h, "ratio", ratioChoice);
            setByName (*h, "color", 0.0f);
            setByName (*h, "release", 300.0f);
            auto l = sine (1000.0, std::pow (10.0, -6.0 / 20.0), 48000), r = l;
            run (*h, l, r);
            return h->processor().meter (FetProcessor::gainReduction);
        };

        const float twenty = grFor (3.0f), all = grFor (4.0f);
        CHECK (all > twenty * 1.2f, "FET: \"Todos\" reduz %.1f dB, mais que o 20:1 (%.1f dB)", all, twenty);
    }

    // Óptico com agudo no detector: um 6 kHz comprime mais que um 200 Hz do mesmo nível.
    {
        auto grAt = [] (double freq, float emphasisValue)
        {
            auto h = makeHost ("opto");
            setByName (*h, "reduction", 50.0f);
            setByName (*h, "emphasis", emphasisValue);
            auto l = sine (freq, std::pow (10.0, -10.0 / 20.0), 48000 * 2), r = l;
            run (*h, l, r);
            return h->processor().meter (OptoProcessor::gainReduction);
        };

        const float flatLow = grAt (200.0, 0.0f), flatHigh = grAt (6000.0, 0.0f), boostedHigh = grAt (6000.0, 100.0f);
        CHECK (std::abs (flatLow - flatHigh) < 0.5f && boostedHigh > flatHigh + 3.0f,
               "óptico: sem realce 200 Hz e 6 kHz reduzem igual (%.1f e %.1f dB); com o agudo no detector, 6 kHz reduz %.1f dB",
               flatLow, flatHigh, boostedHigh);
    }

    // FET: a cor gera o 3º harmônico previsto, e a sobreamostragem segura o rebatimento.
    {
        auto h = makeHost ("fet");
        setByName (*h, "threshold", 0.0f);
        setByName (*h, "color", 100.0f);
        const double a = 0.6;
        auto l = sine (1000.0, a, 48000), r = l;
        run (*h, l, r);
        const double k = 0.15, third = toneAmplitude (l, 3000.0, 24000, 48000), fund = toneAmplitude (l, 1000.0, 24000, 48000);
        const double expected = db ((k * a * a * a / 4.0) / (a - 3.0 * k * a * a * a / 4.0));
        CHECK (std::abs (db (third / fund) - expected) < 1.0, "cor do FET: 3º harmônico a %.1f dB (previsto %.1f)", db (third / fund), expected);

        auto hi = makeHost ("fet");
        setByName (*hi, "threshold", 0.0f);
        setByName (*hi, "color", 100.0f);
        auto hl = sine (15000.0, a, 48000), hr = hl;
        run (*hi, hl, hr);
        const double alias = db (toneAmplitude (hl, 3000.0, 24000, 48000) / toneAmplitude (hl, 15000.0, 24000, 48000));
        CHECK (alias < -60.0, "15 kHz com cor: o rebatimento do 3º harmônico (3 kHz) fica em %.1f dB", alias);

        // Latência: impulso sai na posição informada.
        auto li = makeHost ("fet");
        setByName (*li, "threshold", 0.0f);
        setByName (*li, "color", 0.0f);
        std::vector<float> imp (4096, 0.0f);
        imp[1000] = 0.5f;
        auto il = imp, ir = imp;
        run (*li, il, ir);
        size_t at = 0;

        for (size_t i = 0; i < il.size(); ++i)
            if (std::abs (il[i]) > std::abs (il[at])) at = i;

        CHECK ((int) at - 1000 == li->latencySamples(), "FET: latência medida %d = informada %d", (int) at - 1000, li->latencySamples());
    }

    // EQ vintage: medido = analítico, e o "truque" (reforço e atenuação juntos) dá peso com vale acima.
    {
        auto h = makeHost ("vintageEq");
        setByName (*h, "lowFreq", 2.0f);
        setByName (*h, "lowBoost", 10.0f);
        setByName (*h, "lowAtten", 10.0f);
        setByName (*h, "highFreq", 4.0f);
        setByName (*h, "highBoost", 5.0f);
        std::vector<float> values;

        for (int i = 0; i < (int) h->info().params.size(); ++i)
            values.push_back (h->getParam (i));

        double worst = 0.0;

        for (double f : { 30.0, 60.0, 120.0, 400.0, 2000.0, 10000.0 })
        {
            auto h2 = makeHost ("vintageEq");

            for (int i = 0; i < (int) values.size(); ++i)
                h2->setParam (i, values[(size_t) i]);

            auto l = sine (f, 0.1, 48000 * 2), r = l;
            run (*h2, l, r);
            const double m = db (amplitudeAt (l, f, 48000) / 0.1);
            worst = std::max (worst, std::abs (m - VintageEqProcessor::responseDb (values.data(), f, rate)));
        }

        const double at35 = VintageEqProcessor::responseDb (values.data(), 35.0, rate);
        const double at110 = VintageEqProcessor::responseDb (values.data(), 110.0, rate);
        CHECK (worst < 0.1, "EQ vintage: medido = analítico (erro máx. %.3f dB)", worst);
        CHECK (at35 > 2.0 && at110 < -1.0, "truque do grave em 60 Hz: %+.1f dB em 35 Hz e %+.1f dB em 110 Hz", at35, at110);
    }
}

static void testMultiband()
{
    std::printf ("Multibanda\n");

    // Tudo em 0 dB: a soma das 4 bandas é plana. Medido em 31 frequências, com as divisões padrão e outras.
    auto flatness = [] (float a, float b, float c)
    {
        double worst = 0.0;

        for (int k = 0; k <= 30; ++k)
        {
            const double f = 20.0 * std::pow (1000.0, k / 30.0);
            auto h = makeHost ("multiband");
            setByName (*h, "x1", a);
            setByName (*h, "x2", b);
            setByName (*h, "x3", c);
            auto l = sine (f, 0.5, 48000 * 2), r = l;
            run (*h, l, r);
            worst = std::max (worst, std::abs (db (amplitudeAt (l, f, 48000) / 0.5)));
        }

        return worst;
    };

    const double flatDefault = flatness (150.0f, 1500.0f, 6000.0f);
    const double flatOther = flatness (80.0f, 800.0f, 9000.0f);
    CHECK (flatDefault < 0.01 && flatOther < 0.01, "soma plana com tudo em 0 dB: diferença máx. %.4f dB (padrão) e %.4f dB (80/800/9000)",
           flatDefault, flatOther);

    // Ruído branco também sai com o mesmo nível (o passa-tudo não muda a energia).
    {
        auto h = makeHost ("multiband");
        auto l = whiteNoise (48000 * 2, 0.1, 11), r = l;
        const auto original = l;
        run (*h, l, r);
        const double change = db (rms (l, 24000) / rms (original, 24000));
        CHECK (std::abs (change) < 0.05, "ruído branco sai com o mesmo nível (%.3f dB)", change);
    }

    // Só a banda 3 comprime: 3 kHz desce; 200 Hz não muda.
    {
        // Divisões em 1 e 9 kHz: os 3 kHz ficam bem dentro da banda 3 (o que vaza nas vizinhas fica abaixo de -38 dB).
        auto h = makeHost ("multiband");
        setByName (*h, "x2", 1000.0f);
        setByName (*h, "x3", 9000.0f);
        setByName (*h, "b3Thr", -30.0f);
        setByName (*h, "b3Ratio", 4.0f);
        const double amp = std::pow (10.0, -6.0 / 20.0);
        auto l = sine (3000.0, amp, 48000 * 2), r = l;
        run (*h, l, r);
        const double out3k = db (amplitudeAt (l, 3000.0, 48000));
        const float gr3 = h->processor().meter (MultibandProcessor::reduction3);
        auto h2 = makeHost ("multiband");
        setByName (*h2, "x2", 1000.0f);
        setByName (*h2, "x3", 9000.0f);
        setByName (*h2, "b3Thr", -30.0f);
        setByName (*h2, "b3Ratio", 4.0f);
        auto l2 = sine (200.0, amp, 48000 * 2), r2 = l2;
        run (*h2, l2, r2);
        const double out200 = db (amplitudeAt (l2, 200.0, 48000) / amp);
        // A banda 3 carrega os 3 kHz a -0,1 dB: 23,9 dB acima do limiar, 4:1 = 17,9 dB de redução. Na saída, o que
        // vaza nas bandas 2 e 4 (-38 dB cada, sem compressão e na mesma fase) soma ~1,4 dB por cima.
        CHECK (std::abs (gr3 - 17.9f) < 0.5f && out3k < -21.0 && out3k > -25.0,
               "banda 3 a 4:1: reduz %.1f dB (esperado 17,9); 3 kHz de -6 dB sai em %.1f dB", gr3, out3k);
        CHECK (std::abs (out200) < 0.05, "200 Hz (banda 2) não muda: %.3f dB", out200);
    }

    // Disparo externo por banda: a "voz" em 3 kHz abaixa só os 3 kHz do "beat".
    {
        auto h = makeHost ("multiband", "beat-space");
        const int n = 48000 * 2;
        std::vector<float> beat ((size_t) n), voice ((size_t) n, 0.0f);

        for (int i = 0; i < n; ++i)
        {
            beat[(size_t) i] = (float) (0.2 * std::sin (2.0 * pi * 200.0 * i / rate) + 0.2 * std::sin (2.0 * pi * 3000.0 * i / rate));
            voice[(size_t) i] = i >= n / 2 ? (float) (0.5 * std::sin (2.0 * pi * 3100.0 * i / rate)) : 0.0f;
        }

        auto l = beat, r = beat;
        run (*h, l, r, &voice, &voice);
        const double before3k = toneAmplitude (l, 3000.0, 24000, 47000), after3k = toneAmplitude (l, 3000.0, 72000, 95000);
        const double before200 = toneAmplitude (l, 200.0, 24000, 47000), after200 = toneAmplitude (l, 200.0, 72000, 95000);
        CHECK (db (after3k / before3k) < -6.0 && std::abs (db (after200 / before200)) < 0.2,
               "a voz abre espaço: 3 kHz do beat %.1f dB, 200 Hz %.2f dB", db (after3k / before3k), db (after200 / before200));
    }

    // Ouvir uma banda: só a banda 2 toca.
    {
        auto h = makeHost ("multiband");
        setByName (*h, "solo", 2.0f);
        const int n = 48000 * 2;
        std::vector<float> x ((size_t) n);

        for (int i = 0; i < n; ++i)
            x[(size_t) i] = (float) (0.2 * std::sin (2.0 * pi * 400.0 * i / rate) + 0.2 * std::sin (2.0 * pi * 9000.0 * i / rate));

        auto l = x, r = x;
        run (*h, l, r);
        CHECK (db (toneAmplitude (l, 9000.0, 48000, 96000) / toneAmplitude (l, 400.0, 48000, 96000)) < -40.0,
               "ouvir a banda 2: o agudo fica %.0f dB abaixo", db (toneAmplitude (l, 9000.0, 48000, 96000) / toneAmplitude (l, 400.0, 48000, 96000)));
    }
}

static void testLeveler()
{
    std::printf ("Nivelador de voz\n");

    // Frases alternando -30 e -10 dB RMS (2 s cada), com 0,5 s de silêncio (ruído a -80) entre elas.
    const int phrase = 48000 * 2, gap = 24000;
    std::vector<float> x;
    std::vector<std::pair<size_t, size_t>> quiet, loud;
    auto noise = whiteNoise ((phrase + gap) * 6, std::pow (10.0, -80.0 / 20.0), 5);

    for (int p = 0; p < 6; ++p)
    {
        const double level = std::pow (10.0, (p % 2 == 0 ? -30.0 : -10.0) / 20.0) * std::sqrt (2.0);
        const size_t start = x.size();

        for (int i = 0; i < phrase; ++i)
            x.push_back ((float) (level * std::sin (2.0 * pi * 400.0 * i / rate) * (0.8 + 0.2 * std::sin (2.0 * pi * 3.0 * i / rate))));

        (p % 2 == 0 ? quiet : loud).push_back ({ start + 48000, start + (size_t) phrase });

        for (int i = 0; i < gap; ++i)
            x.push_back (0.0f);
    }

    for (size_t i = 0; i < x.size(); ++i)
        x[i] += noise[i];

    auto h = makeHost ("leveler");
    setByName (*h, "range", 14.0f);
    auto l = x, r = x;
    float gainBeforeGap = 0.0f, gainAfterGap = 0.0f;

    for (size_t start = 0; start < l.size(); start += block)
    {
        const int n = (int) std::min<size_t> (block, l.size() - start);
        h->process (l.data() + start, r.data() + start, n);
        const size_t end = start + (size_t) n;

        if (end > loud[1].second - 600 && end <= loud[1].second) gainBeforeGap = h->processor().meter (LevelerProcessor::gainMeter);
        if (end > loud[1].second + (size_t) gap - 600 && end <= loud[1].second + (size_t) gap) gainAfterGap = h->processor().meter (LevelerProcessor::gainMeter);
    }

    double spreadIn = 0.0, spreadOut = 0.0;
    const double qIn = db (rms (x, quiet[2].first, quiet[2].second)), lIn = db (rms (x, loud[2].first, loud[2].second));
    const double qOut = db (rms (l, quiet[2].first, quiet[2].second)), lOut = db (rms (l, loud[2].first, loud[2].second));
    spreadIn = lIn - qIn;
    spreadOut = lOut - qOut;
    CHECK (spreadOut < 3.0 && std::abs (lOut + 18.0) < 1.5, "frases de %.1f e %.1f dB saem em %.1f e %.1f dB (alvo -18, faixa 14)",
           qIn, lIn, qOut, lOut);
    CHECK (gainAfterGap - gainBeforeGap < 2.0, "na pausa depois da frase alta o ganho quase não sobe (%.2f dB antes, %.2f dB depois): "
           "não puxa o ruído", gainBeforeGap, gainAfterGap);
    (void) spreadIn;
}

static void testDoubler()
{
    std::printf ("Dobrador\n");

    auto voice = syntheticVoice ([] (double) { return 220.0; }, 2.0);

    {
        auto h = makeHost ("doubler");
        setByName (*h, "level", 0.0f);
        auto l = voice, r = voice;
        run (*h, l, r);
        double worst = 0.0;

        for (size_t i = 24000; i < l.size(); ++i)   // depois da rampa de 40 ms das cópias
            worst = std::max (worst, (double) std::abs (l[i] - voice[i]));

        CHECK (worst == 0.0, "cópias em 0%%: a voz passa igual, sem latência (diferença %.1e)", worst);
    }

    auto correlationOf = [] (float width)
    {
        auto h = makeHost ("doubler");
        setByName (*h, "width", width);
        setByName (*h, "level", 100.0f);
        auto l = whiteNoise (48000 * 2, 0.1, 17), r = l;
        run (*h, l, r);
        return h->processor().meter (DoublerProcessor::correlation);
    };

    const float wide = correlationOf (100.0f), centred = correlationOf (0.0f);

    {
        auto h = makeHost ("doubler", "chorus-soft");
        setByName (*h, "level", 100.0f);
        auto l = whiteNoise (48000 * 2, 0.1, 18), r = l;
        run (*h, l, r);
        CHECK (h->processor().meter (DoublerProcessor::correlation) < 0.9f && peak (l) < 2.0, "modo coro: correlação %.2f, pico %.2f",
               h->processor().meter (DoublerProcessor::correlation), peak (l));
    }
    CHECK (wide < 0.8f && centred > 0.999f, "abre o estéreo: correlação %.2f com largura 100%% e %.3f com 0%%", wide, centred);

    // A desafinação pedida vira a amplitude da modulação do atraso certa (desvio máximo de tom).
    const double a = DoublerProcessor::modulationSeconds (8.0, false);
    double maxSlope = 0.0;

    for (int i = 0; i < 200000; ++i)
    {
        const double t = i * 1.0e-4;
        const double d = 0.5 * a * (2.0 * pi * 0.31 * std::cos (2.0 * pi * 0.31 * t) + 2.0 * pi * 0.77 * std::cos (2.0 * pi * 0.77 * t));
        maxSlope = std::max (maxSlope, std::abs (d));
    }

    CHECK (std::abs (1200.0 * std::log2 (1.0 + maxSlope) - 8.0) < 0.5, "desafinação de 8 cents: o atraso varia ±%.2f ms e o tom até %.2f cents",
           a * 1000.0, 1200.0 * std::log2 (1.0 + maxSlope));
}

static void testBassPlus()
{
    std::printf ("Grave+\n");

    // 50 Hz a -12 dBFS: o 2º (100 Hz) e o 3º (150 Hz) harmônicos aparecem no nível previsto.
    const double a = 0.25;
    auto h = makeHost ("bassplus");
    auto l = sine (50.0, a, 48000 * 2), r = l;
    run (*h, l, r);

    const auto lp = SvfCoeffs::make (FilterType::lowpass, 90.0, 0.70710678, 0.0, rate);
    const auto hp = SvfCoeffs::make (FilterType::highpass, 90.0, 0.7071, 0.0, rate);
    const auto top = SvfCoeffs::make (FilterType::lowpass, 540.0, 0.7071, 0.0, rate);
    const double low = a * std::pow (std::abs (lp.response (50.0, rate)), 2.0);   // LR4 grave = Butterworth ao quadrado
    auto expected = [&] (double f, double share)
    {
        return low * 0.75 * share * std::pow (std::abs (hp.response (f, rate)), 2.0) * std::abs (top.response (f, rate));
    };

    const double h2 = amplitudeAt (l, 100.0, 48000), h3 = amplitudeAt (l, 150.0, 48000);
    CHECK (std::abs (db (h2 / expected (100.0, 0.6))) < 1.0 && std::abs (db (h3 / expected (150.0, 0.4))) < 1.0,
           "harmônicos de 50 Hz: 100 Hz a %.1f dB e 150 Hz a %.1f dB do fundamental (previstos %.1f e %.1f)",
           db (h2 / a), db (h3 / a), db (expected (100.0, 0.6) / a), db (expected (150.0, 0.4) / a));

    // A quantidade não depende do volume: 20 dB a menos na entrada, 20 dB a menos nos harmônicos.
    {
        auto q = makeHost ("bassplus");
        auto ql = sine (50.0, a * 0.1, 48000 * 2), qr = ql;
        run (*q, ql, qr);
        const double change = db (amplitudeAt (ql, 100.0, 48000) / h2);
        CHECK (std::abs (change + 20.0) < 0.5, "20 dB a menos na entrada: o 2º harmônico cai %.2f dB", change);
    }

    // Sem harmônicos e com o sub inteiro: o volume fica plano em toda a faixa.
    {
        double worst = 0.0;

        for (double f : { 30.0, 50.0, 90.0, 150.0, 1000.0, 8000.0 })
        {
            auto z = makeHost ("bassplus");
            setByName (*z, "amount", 0.0f);
            auto zl = sine (f, 0.25, 48000 * 2), zr = zl;
            run (*z, zl, zr);
            worst = std::max (worst, std::abs (db (amplitudeAt (zl, f, 48000) / 0.25)));
        }

        CHECK (worst < 0.01, "harmônicos em 0%%: volume plano (diferença máx. %.4f dB)", worst);
    }

    // Sub original em 0%: o fundamental de 40 Hz some; 1 kHz fica.
    {
        auto s = makeHost ("bassplus");
        setByName (*s, "sub", 0.0f);
        setByName (*s, "amount", 0.0f);
        auto sl = sine (40.0, 0.25, 48000 * 2), sr = sl;
        run (*s, sl, sr);
        CHECK (db (toneAmplitude (sl, 40.0, 48000, 96000) / 0.25) < -15.0, "sub em 0%%: 40 Hz cai %.1f dB",
               db (toneAmplitude (sl, 40.0, 48000, 96000) / 0.25));
    }
}

static void testDenoise()
{
    std::printf ("Limpa-voz\n");

    auto h = makeHost ("denoise");
    const int lat = h->latencySamples();
    CHECK (lat == 1024, "latência informada: %d amostras (21 ms a 48 kHz)", lat);

    // Redução 0: sai a entrada atrasada, amostra por amostra (a STFT reconstrói perfeito).
    {
        auto z = makeHost ("denoise");
        setByName (*z, "reduction", 0.0f);
        auto x = whiteNoise (48000, 0.2, 9);
        auto l = x, r = whiteNoise (48000, 0.2, 10);
        const auto xr = r;
        run (*z, l, r);
        double worst = 0.0;

        for (size_t i = 0; i + (size_t) lat < x.size(); ++i)
            worst = std::max (worst, (double) std::max (std::abs (l[i + (size_t) lat] - x[i]), std::abs (r[i + (size_t) lat] - xr[i])));

        CHECK (worst < 1.0e-5, "redução 0: entrada atrasada em %d amostras, diferença máx. %.1e (latência medida = informada)", lat, worst);
    }

    // Voz (1 s sim, 1 s não) sobre ruído branco a -50 dBFS: o ruído desce nas pausas e a voz fica.
    {
        const int n = 48000 * 8;
        auto noise = whiteNoise (n, std::pow (10.0, -50.0 / 20.0), 21);
        auto voice = syntheticVoice ([] (double t) { return 200.0 + 20.0 * std::sin (2.0 * pi * 0.5 * t); }, 8.0, 0.3);
        std::vector<float> x ((size_t) n);

        for (int i = 0; i < n; ++i)
            x[(size_t) i] = noise[(size_t) i] + ((i / 48000) % 2 == 1 ? voice[(size_t) i] : 0.0f);

        auto l = x, r = x;
        run (*h, l, r);

        const size_t L = (size_t) lat;
        double noiseIn = 0.0, noiseOut = 0.0, voiceIn = 0.0, voiceOut = 0.0;

        for (int s = 4; s < 8; ++s)
        {
            const size_t a = (size_t) s * 48000 + 19200, b = (size_t) (s + 1) * 48000 - 4800;   // 400 ms depois da troca

            if (s % 2 == 0)
            {
                noiseIn += rms (x, a, b);
                noiseOut += rms (l, a + L, b + L);
            }
            else
            {
                voiceIn += rms (x, a, b);
                voiceOut += rms (l, a + L, b + L);
            }
        }

        CHECK (db (noiseOut / noiseIn) < -9.0, "nas pausas o ruído desce %.1f dB (redução 12)", db (noiseOut / noiseIn));
        CHECK (std::abs (db (voiceOut / voiceIn)) < 1.0, "a voz fica: %.2f dB de diferença", db (voiceOut / voiceIn));
    }

    // Nota longa: 1 s de pausa e 5 s de voz sem parar. A estimativa de ruído não pode subir até a voz.
    {
        auto lh = makeHost ("denoise");
        const int n = 48000 * 6;
        auto noise = whiteNoise (n, std::pow (10.0, -50.0 / 20.0), 22);
        auto voice = syntheticVoice ([] (double t) { return 260.0 + 8.0 * std::sin (2.0 * pi * 5.0 * t); }, 6.0, 0.3);
        std::vector<float> x ((size_t) n);

        for (int i = 0; i < n; ++i)
            x[(size_t) i] = noise[(size_t) i] + (i >= 48000 ? voice[(size_t) i] : 0.0f);

        auto l = x, r = x;
        run (*lh, l, r);
        const size_t L = (size_t) lh->latencySamples();
        const double late = db (rms (l, 5 * 48000 + L, 6 * 48000 - 1) / rms (x, 5 * 48000, 6 * 48000 - 1 - L));
        CHECK (std::abs (late) < 1.0, "nota de 5 s sem pausa: no último segundo a voz continua igual (%.2f dB)", late);
    }
}

static void testConsole()
{
    std::printf ("Console\n");

    // Tudo desligado: a entrada atrasada pela sobreamostragem, sem mudar o nível.
    {
        auto h = makeHost ("console");
        const int lat = h->latencySamples();
        auto x = sine (1000.0, 0.5, 48000);
        auto l = x, r = x;
        run (*h, l, r);
        double worst = 0.0;

        for (size_t i = 4800; i + (size_t) lat < x.size(); ++i)
            worst = std::max (worst, (double) std::abs (l[i + (size_t) lat] - x[i]));

        auto nl = whiteNoise (48000, 0.1, 23), nr = nl;
        const auto noise = nl;
        auto hn = makeHost ("console");
        run (*hn, nl, nr);
        const int measured = bestLag (noise, nl, 200, 4800, 40000);
        CHECK (worst < 2.0e-3 && measured == lat,
               "tudo desligado: sai a entrada atrasada %d amostras (latência medida %d, diferença máx. %.1e)", lat, measured, worst);
    }

    // EQ: medido = analítico.
    {
        double worst = 0.0;
        std::vector<float> values;

        for (double f : { 60.0, 300.0, 1000.0, 4000.0, 12000.0 })
        {
            auto h = makeHost ("console");
            setByName (*h, "low", 6.0f);
            setByName (*h, "mid", -4.0f);
            setByName (*h, "midFreq", 1000.0f);
            setByName (*h, "high", 3.0f);
            setByName (*h, "hpf", 40.0f);
            values.clear();

            for (int i = 0; i < (int) h->info().params.size(); ++i)
                values.push_back (h->getParam (i));

            auto l = sine (f, 0.1, 48000 * 2), r = l;
            run (*h, l, r);
            worst = std::max (worst, std::abs (db (amplitudeAt (l, f, 48000) / 0.1) - ConsoleProcessor::responseDb (values.data(), f, rate)));
        }

        CHECK (worst < 0.15, "EQ do console: medido = analítico (erro máx. %.3f dB)", worst);
    }

    // Cor do console: harmônicos, sem DC (o DC da válvula sai só da diferença).
    {
        auto h = makeHost ("console");
        setByName (*h, "drive", 100.0f);
        auto l = sine (200.0, 0.7, 48000 * 2), r = l;
        run (*h, l, r);
        double mean = 0.0;

        for (size_t i = 48000; i < 96000; ++i)
            mean += l[i];

        mean /= 48000.0;
        const double second = db (amplitudeAt (l, 400.0, 48000) / amplitudeAt (l, 200.0, 48000));
        CHECK (std::abs (mean) < 1.0e-3 && second > -50.0, "cor do console em 100%%: 2º harmônico a %.1f dB, DC %.1e", second, mean);
    }

    // Gate e compressor do console.
    {
        auto g = makeHost ("console");
        setByName (*g, "gate", -45.0f);
        auto l = whiteNoise (48000, std::pow (10.0, -60.0 / 20.0), 4), r = l;
        const auto original = l;
        run (*g, l, r);
        const double drop = db (rms (l, 24000) / rms (original, 24000));
        CHECK (drop < -22.0, "gate do console fecha no ruído (%.1f dB)", drop);

        auto c = makeHost ("console");
        setByName (*c, "comp", -20.0f);
        setByName (*c, "ratio", 4.0f);
        auto cl = sine (1000.0, std::pow (10.0, -6.0 / 20.0), 48000), cr = cl;
        run (*c, cl, cr);
        CHECK (std::abs (c->processor().meter (ConsoleProcessor::compReduction) - 10.5f) < 0.8f, "compressor do console: %.2f dB (esperado 10,5)",
               c->processor().meter (ConsoleProcessor::compReduction));
    }
}

static void testTuneHumanize()
{
    std::printf ("Afinação: duro e humanizar\n");
    using P = TuneProcessor;

    // Lá 4 com vibrato de ±40 cents a 5,5 Hz, sustentado por 2 s.
    auto vib = syntheticVoice ([] (double t) { return 440.0 * std::pow (2.0, 0.4 * std::sin (2.0 * pi * 5.5 * t) / 12.0); }, 2.4);

    auto vibratoDepth = [&vib] (float humanize)
    {
        const auto out = runTune (vib, { { P::key, 9.0f }, { P::scale, 6.0f }, { P::speed, 0.0f }, { P::amount, 100.0f },
                                         { P::humanize, humanize } });
        const double shift = out.latency / rate;
        double lo = 1.0e9, hi = -1.0e9;

        for (double t = 1.0; t < 2.0; t += 0.01)
        {
            const double hz = measurePitch (out.left, t + shift, t + shift + 0.04);

            if (hz > 0.0)
            {
                lo = std::min (lo, cents (hz, 440.0));
                hi = std::max (hi, cents (hz, 440.0));
            }
        }

        return (hi - lo) * 0.5;
    };

    const double hard = vibratoDepth (0.0f), human = vibratoDepth (100.0f);
    CHECK (hard < 12.0, "duro (velocidade 0): o vibrato de ±40 cents sai com ±%.1f cents (nota reta)", hard);
    CHECK (human > 25.0, "humanizar 100%%: na nota longa o vibrato passa (±%.1f cents)", human);
}

/** Todo efeito, com os padrões: silêncio vira silêncio exato, sinal enorme e impulso não dão NaN, infinito
    nem número desnormal, e o impulso sai na latência informada (nos efeitos que têm latência e não mexem
    no som com os padrões). */
/** Trem de pulsos a f0 passando por uma ressonância (a "formante") em formantHz: uma voz bem simples. */
static std::vector<float> pulseVoice (double f0, double formantHz, double seconds)
{
    const int n = (int) (seconds * rate);
    std::vector<float> x ((size_t) n, 0.0f);
    double phase = 0.0;
    const auto res = SvfCoeffs::make (FilterType::bandpass, formantHz, 6.0, 0.0, rate);
    const auto body = SvfCoeffs::make (FilterType::lowpass, 3000.0, 0.7071, 0.0, rate);
    Svf a, b;

    for (int i = 0; i < n; ++i)
    {
        phase += f0 / rate;
        float pulse = 0.0f;

        if (phase >= 1.0)
        {
            phase -= 1.0;
            pulse = 1.0f;
        }

        x[(size_t) i] = b.process (a.process (pulse, res), body);
    }

    double pk = 0.0;

    for (auto v : x)
        pk = std::max (pk, (double) std::abs (v));

    for (auto& v : x)
        v = (float) (v / pk * 0.3);

    return x;
}

/** O harmônico mais forte de f0 (k = 1 a 30), em Hz. */
static double strongestHarmonic (const std::vector<float>& x, double f0, size_t from, size_t to)
{
    double best = 0.0, at = 0.0;

    for (int k = 1; k <= 30 && k * f0 < 8000.0; ++k)
        if (const double a = toneAmplitude (x, k * f0, from, to); a > best)
        {
            best = a;
            at = k * f0;
        }

    return at;
}

static void testVoicePitch()
{
    std::printf ("Tom da voz (PSOLA com formante)\n");

    auto runPitch = [] (const std::vector<float>& x, float st, float formantSt, float mixValue = 100.0f)
    {
        auto h = makeHost ("voicepitch");
        setByName (*h, "semitones", st);
        setByName (*h, "formant", formantSt);
        setByName (*h, "mix", mixValue);
        h->prepare (rate, block);   // os valores entram sem rampa
        auto l = x, r = x;
        run (*h, l, r);
        return std::pair<std::vector<float>, int> (l, h->latencySamples());
    };

    // Sem mudança: a entrada atrasada, exata, na latência informada.
    {
        auto x = whiteNoise (48000, 0.1, 31);
        const auto [y, lat] = runPitch (x, 0.0f, 0.0f);
        double worst = 0.0;

        for (size_t i = 0; i + (size_t) lat < x.size(); ++i)
            worst = std::max (worst, (double) std::abs (y[i + (size_t) lat] - x[i]));

        CHECK (lat == (int) std::ceil (3.0 * rate / 60.0) + 8 && worst == 0.0,
               "sem mudança: a entrada atrasada %d amostras (%.1f ms), diferença %.1e", lat, 1000.0 * lat / rate, worst);
    }

    // O tom vai para onde foi pedido.
    auto voice = syntheticVoice ([] (double) { return 220.0; }, 2.0);
    const double inHz = measurePitch (voice, 0.6, 1.4);

    for (float st : { 7.0f, -5.0f, -12.0f })
    {
        const auto [y, lat] = runPitch (voice, st, 0.0f);
        const double shift = lat / rate;
        const double hz = measurePitch (y, 0.6 + shift, 1.4 + shift);
        const double expected = inHz * std::pow (2.0, st / 12.0);
        const double level = db (rms (y, (size_t) ((0.5 + shift) * rate), (size_t) ((1.5 + shift) * rate))
                                 / rms (voice, (size_t) (0.5 * rate), (size_t) (1.5 * rate)));
        CHECK (std::abs (cents (hz, expected)) < 10.0 && std::abs (level) < 3.0,
               "%+.0f semitons: %.1f Hz vira %.1f Hz (esperado %.1f, erro %.1f cents), nível %+.1f dB", st, inHz, hz, expected,
               cents (hz, expected), level);
    }

    // A formante fica no lugar quando só o tom muda, e anda quando a formante muda.
    {
        auto pv = pulseVoice (220.0, 1000.0, 2.0);
        const auto [up, lat] = runPitch (pv, 7.0f, 0.0f);
        const size_t a = (size_t) ((0.6 * rate) + lat), b = (size_t) ((1.4 * rate) + lat);
        const double f0 = 220.0 * std::pow (2.0, 7.0 / 12.0);
        const double kept = strongestHarmonic (up, f0, a, b);

        const auto [moved, lat2] = runPitch (pv, 7.0f, 5.0f);
        const double shifted = strongestHarmonic (moved, f0, a, b);
        CHECK (std::abs (kept - 1000.0) < f0 * 0.6 && std::abs (shifted - 1000.0 * std::pow (2.0, 5.0 / 12.0)) < f0 * 0.6,
               "formante em 1 kHz: com +7 st o harmônico mais forte fica em %.0f Hz; com a formante +5 st vai para %.0f Hz (esperado ~1335)",
               kept, shifted);
        (void) lat2;
    }

    // Voz que se mexe (glissando de 200 a 260 Hz com vibrato): o tom acompanha e as emendas não estalam.
    {
        auto moving = syntheticVoice ([] (double t) { return (200.0 + 30.0 * t) * std::pow (2.0, 0.3 * std::sin (2.0 * pi * 5.0 * t) / 12.0); }, 2.0);
        const auto [y, lat] = runPitch (moving, 5.0f, 0.0f);
        const double shift = lat / rate, ratio = std::pow (2.0, 5.0 / 12.0);
        double worst = 0.0;

        for (double t = 0.4; t < 1.8; t += 0.1)
        {
            const double a = measurePitch (moving, t, t + 0.04), b = measurePitch (y, t + shift, t + shift + 0.04);

            if (a > 0.0 && b > 0.0)
                worst = std::max (worst, std::abs (cents (b, a * ratio)));
        }

        auto roughness = [] (const std::vector<float>& v, size_t from, size_t to)
        {
            double m = 0.0;

            for (size_t i = from + 2; i < to; ++i)
                m = std::max (m, (double) std::abs (v[i] - 2.0f * v[i - 1] + v[i - 2]));

            return m;
        };

        const double rIn = roughness (moving, 4800, moving.size()), rOut = roughness (y, 4800 + (size_t) lat, y.size());
        CHECK (worst < 20.0 && rOut < rIn * 2.5, "voz em movimento +5 st: o tom acompanha (pior janela %.1f cents) e sem estalo (aspereza %.3f contra %.3f)",
               worst, rOut, rIn);
    }

    // Ruído (sem tom) passa no mesmo nível, sem NaN.
    {
        auto x = whiteNoise (48000, 0.1, 32);
        const auto [y, lat] = runPitch (x, 7.0f, 3.0f);
        const double level = db (rms (y, 12000 + (size_t) lat, 40000 + (size_t) lat) / rms (x, 12000, 40000));
        bool finite = true;

        for (auto v : y)
            finite &= std::isfinite (v);

        CHECK (finite && std::abs (level) < 1.5, "ruído (sem tom) passa com %.2f dB e sem NaN", level);
    }
}

static void testHarmony()
{
    std::printf ("Harmonia\n");
    using H = HarmonyProcessor;
    const int aMinor = TuneProcessor::scaleMask (0), cMajor = TuneProcessor::scaleMask (1);

    CHECK (H::scaleStep (69, 2, 9, aMinor) == 72 && H::scaleStep (69, 4, 9, aMinor) == 76 && H::scaleStep (69, -2, 9, aMinor) == 65
               && H::scaleStep (64, 2, 0, cMajor) == 67 && H::scaleStep (71, 2, 0, cMajor) == 74 && H::scaleStep (60, 2, 0, 0xFFF) == 64,
           "graus da escala: Lá menor (Lá → Dó, Mi, Fá), Dó maior (Mi → Sol, Si → Ré), cromática (terça maior)");

    // Voz em Lá 3 (220 Hz), Lá menor: a terça acima é Dó 4 (261,6 Hz) e a quinta é Mi 4 (329,6 Hz).
    auto voice = syntheticVoice ([] (double) { return 220.0; }, 2.0);

    auto runHarmony = [&voice] (float choice, float levelValue)
    {
        auto h = makeHost ("harmony");
        setByName (*h, "key", 9.0f);
        setByName (*h, "scale", 0.0f);
        setByName (*h, "voices", choice);
        setByName (*h, "level", levelValue);
        h->prepare (rate, block);
        auto l = voice, r = voice;
        run (*h, l, r);

        for (size_t i = 0; i < l.size(); ++i)
            l[i] = 0.5f * (l[i] + r[i]);   // em mono: com duas vozes, uma vai para cada lado

        return std::pair<std::vector<float>, int> (l, h->latencySamples());
    };

    const double c4 = 261.6256, e4 = 329.6276, a3 = 220.0;
    {
        const auto [y, lat] = runHarmony (0.0f, 60.0f);
        const size_t a = (size_t) (0.8 * rate) + (size_t) lat, b = (size_t) (1.6 * rate) + (size_t) lat;
        const double third = db (toneAmplitude (y, c4, a, b) / toneAmplitude (y, a3, a, b));
        const auto [dry, lat0] = runHarmony (0.0f, 0.0f);
        const double none = db (toneAmplitude (dry, c4, a, b) / toneAmplitude (dry, a3, a, b));
        CHECK (third > -12.0 && none < -40.0, "terça acima: Dó 4 aparece a %.1f dB do Lá 3 (sem a voz nova: %.1f dB)", third, none);
        (void) lat0;
    }

    {
        const auto [y, lat] = runHarmony (2.0f, 60.0f);
        const size_t a = (size_t) (0.8 * rate) + (size_t) lat, b = (size_t) (1.6 * rate) + (size_t) lat;
        const double third = db (toneAmplitude (y, c4, a, b) / toneAmplitude (y, a3, a, b));
        const double fifth = db (toneAmplitude (y, e4, a, b) / toneAmplitude (y, a3, a, b));
        CHECK (third > -14.0 && fifth > -14.0, "terça e quinta: Dó 4 a %.1f dB e Mi 4 a %.1f dB do Lá 3", third, fifth);
    }

    // Ruído (sem tom): as vozes novas ficam caladas, sai a entrada atrasada.
    {
        auto h = makeHost ("harmony");
        auto x = whiteNoise (48000, 0.1, 41);
        auto l = x, r = x;
        run (*h, l, r);
        const int lat = h->latencySamples();
        double diff = 0.0, ref = 0.0;

        for (size_t i = 0; i + (size_t) lat < x.size(); ++i)
        {
            diff += std::pow ((double) l[i + (size_t) lat] - x[i], 2.0);
            ref += (double) x[i] * x[i];
        }

        CHECK (10.0 * std::log10 (std::max (1.0e-30, diff / ref)) < -30.0, "sem tom, as vozes novas não entram (diferença %.1f dB)",
               10.0 * std::log10 (std::max (1.0e-30, diff / ref)));
    }
}

//==============================================================================
// Os efeitos de 26/10: clipper, lo-fi, chorus, flanger, phaser e EQ dinâmico.

static void testEffects2610()
{
    std::printf ("Efeitos de 26/10 (clipper, lo-fi, chorus, flanger, phaser, EQ dinâmico)\n");

    // A diferença máxima entre `out` e `in` atrasado `lag` amostras, a partir de `from`.
    auto worstDiff = [] (const std::vector<float>& out, const std::vector<float>& in, int lag, size_t from)
    {
        double worst = 0.0;

        for (size_t i = from; i < out.size(); ++i)
            worst = std::max (worst, (double) std::abs (out[i] - in[i - (size_t) lag]));

        return worst;
    };

    // Clipper ------------------------------------------------------------------
    {
        auto h = makeHost ("clipper");
        const auto in = sine (200.0, 0.25, 48000);
        auto l = in, r = in;
        run (*h, l, r);
        const double d = worstDiff (l, in, h->latencySamples(), 4800);
        CHECK (h->latencySamples() > 0 && d < 2.0e-3, "clipper abaixo do teto: o som sai igual, atrasado as %d amostras informadas (diferença %.1e)",
               h->latencySamples(), d);
    }
    {
        auto h = makeHost ("clipper");
        setByName (*h, "gain", 12.0f);
        setByName (*h, "ceiling", -6.0f);
        setByName (*h, "softness", 0.0f);
        auto l = sine (100.0, 1.0, 48000), r = l;
        run (*h, l, r);
        const double pk = db (peak (l, 4800));
        CHECK (pk < -5.5 && pk > -6.6, "clipper: +12 dB contra o teto de -6 dB sai com pico de %.2f dB", pk);
        CHECK (h->processor().meter (ClipperProcessor::clipping) > 10.0f, "o medidor do clipper mostra %.1f dB cortados",
               h->processor().meter (ClipperProcessor::clipping));
    }
    {
        // 7 kHz cortado reto: o 5º harmônico (35 kHz) voltaria dobrado em 13 kHz sem a sobreamostragem.
        auto h = makeHost ("clipper");
        setByName (*h, "gain", 12.0f);
        setByName (*h, "softness", 0.0f);
        auto l = sine (7000.0, 1.0, 48000), r = l;
        run (*h, l, r);
        const double fundamental = toneAmplitude (l, 7000.0, 4800, 48000), alias = toneAmplitude (l, 13000.0, 4800, 48000);
        CHECK (db (alias / fundamental) < -40.0, "clipper sobreamostrado: o dobrado em 13 kHz fica a %.1f dB do 7 kHz", db (alias / fundamental));
    }
    CHECK (ClipperProcessor::clip (0.5f, 1.0f, 0.3f) == 0.5f && ClipperProcessor::clip (5.0f, 0.5f, 1.0f) <= 0.5f
               && ClipperProcessor::clip (-5.0f, 0.5f, 0.0f) == -0.5f,
           "a curva do clipper: passa igual abaixo da quina e nunca passa do teto");

    // Lo-fi --------------------------------------------------------------------
    {
        auto h = makeHost ("lofi");
        setByName (*h, "mix", 0.0f);
        const auto in = whiteNoise (48000, 0.1, 31);
        auto l = in, r = in;
        run (*h, l, r);
        const double d = worstDiff (l, in, h->latencySamples(), 24000);
        CHECK (h->latencySamples() == 288 && d < 1.0e-6, "lo-fi em mistura 0: o seco sai alinhado na latência de %d amostras (diferença %.1e)",
               h->latencySamples(), d);
    }
    auto lofiResidual = [&] (float bits)
    {
        auto h = makeHost ("lofi");
        setByName (*h, "bits", bits);
        setByName (*h, "rate", 48000.0f);
        setByName (*h, "wow", 0.0f);
        setByName (*h, "tone", 20000.0f);
        setByName (*h, "noise", 0.0f);
        const auto in = sine (1000.0, 0.5, 48000);
        auto l = in, r = in;
        run (*h, l, r);
        double acc = 0.0;
        int n = 0;

        for (size_t i = 24000; i < l.size(); ++i, ++n)
        {
            const double e = l[i] - in[i - (size_t) h->latencySamples()];
            acc += e * e;
        }

        return db (std::sqrt (acc / n));
    };
    const double res16 = lofiResidual (16.0f), res6 = lofiResidual (6.0f);
    CHECK (res16 < -95.0 && res6 > -50.0 && res6 < -35.0, "lo-fi: o erro dos bits é %.1f dB com 16 bits e %.1f dB com 6", res16, res6);
    {
        auto h = makeHost ("lofi");
        setByName (*h, "rate", 8000.0f);
        setByName (*h, "bits", 16.0f);
        setByName (*h, "wow", 0.0f);
        setByName (*h, "tone", 20000.0f);
        setByName (*h, "noise", 0.0f);
        auto l = sine (1000.0, 0.5, 48000), r = l;
        run (*h, l, r);
        const double image = toneAmplitude (l, 7000.0, 9600, 48000) / toneAmplitude (l, 1000.0, 9600, 48000);
        CHECK (db (image) > -30.0, "lo-fi a 8 kHz: o serrilhado aparece em 7 kHz, a %.1f dB do tom", db (image));
    }
    {
        auto h = makeHost ("lofi", "cassette");
        auto l = sine (440.0, 0.5, 48000), r = l;
        l.resize (48000 * 4, 0.0f);
        r.resize (48000 * 4, 0.0f);
        run (*h, l, r);
        bool silentAtEnd = true;

        for (size_t i = 48000 * 3; i < l.size(); ++i)
            silentAtEnd &= l[i] == 0.0f && r[i] == 0.0f;

        CHECK (silentAtEnd, "lo-fi: o chiado acompanha o som e some no silêncio (o último segundo sai zerado)");
    }

    // Chorus -------------------------------------------------------------------
    {
        auto h = makeHost ("chorus");
        setByName (*h, "mix", 0.0f);
        const auto in = whiteNoise (48000, 0.1, 32);
        auto l = in, r = in;
        run (*h, l, r);
        CHECK (worstDiff (l, in, 0, 24000) == 0.0, "chorus em mistura 0: o som passa igual, sem latência");
    }
    auto correlation = [] (const std::vector<float>& a, const std::vector<float>& b, size_t from)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;

        for (size_t i = from; i < a.size(); ++i)
        {
            ab += (double) a[i] * b[i];
            aa += (double) a[i] * a[i];
            bb += (double) b[i] * b[i];
        }

        return ab / std::sqrt (aa * bb + 1.0e-30);
    };
    {
        auto h = makeHost ("chorus", "pad-wide");
        auto l = whiteNoise (48000 * 2, 0.1, 33), r = l;
        run (*h, l, r);
        const double c = correlation (l, r, 24000);
        CHECK (c < 0.9 && peak (l) < 1.5, "chorus abre o estéreo de um som mono: correlação %.2f, pico %.2f", c, peak (l));
    }

    // Flanger ------------------------------------------------------------------
    {
        // Sem varredura nem realimentação, 1 ms de atraso a 50%: vale em 500 Hz, cheio em 1 kHz.
        auto comb = [&] (double freq)
        {
            auto h = makeHost ("flanger");
            setByName (*h, "depth", 0.0f);
            setByName (*h, "delay", 1.0f);
            setByName (*h, "feedback", 0.0f);
            setByName (*h, "mix", 50.0f);
            auto l = sine (freq, 0.5, 48000), r = l;
            run (*h, l, r);
            return db (toneAmplitude (l, freq, 9600, 48000) / 0.5);
        };
        const double notch = comb (500.0), full = comb (1000.0);
        CHECK (notch < -30.0 && std::abs (full) < 0.5, "flanger de 1 ms: %.1f dB em 500 Hz (vale) e %.2f dB em 1 kHz", notch, full);
    }
    {
        auto h = makeHost ("flanger");
        setByName (*h, "feedback", 95.0f);
        auto l = whiteNoise (48000 * 2, 0.1, 34), r = l;
        run (*h, l, r);
        CHECK (peak (l) < 10.0, "flanger com 95%% de realimentação não dispara (pico %.2f)", peak (l));
    }
    {
        auto h = makeHost ("flanger");
        setByName (*h, "mix", 0.0f);
        const auto in = whiteNoise (48000, 0.1, 35);
        auto l = in, r = in;
        run (*h, l, r);
        CHECK (worstDiff (l, in, 0, 24000) == 0.0, "flanger em mistura 0: o som passa igual, sem latência");
    }

    // Phaser -------------------------------------------------------------------
    {
        // 4 estágios parados em 1 kHz: a fase dá -180° em 414 Hz (cada estágio -45°) e a soma cancela.
        auto phaser = [&] (double freq)
        {
            auto h = makeHost ("phaser");
            setByName (*h, "stages", 0.0f);
            setByName (*h, "depth", 0.0f);
            setByName (*h, "centre", 1000.0f);
            setByName (*h, "feedback", 0.0f);
            setByName (*h, "mix", 50.0f);
            auto l = sine (freq, 0.5, 48000), r = l;
            run (*h, l, r);
            return db (toneAmplitude (l, freq, 9600, 48000) / 0.5);
        };
        const double notch = phaser (414.2), full = phaser (1000.0);
        CHECK (notch < -25.0 && std::abs (full) < 0.5, "phaser de 4 estágios em 1 kHz: %.1f dB em 414 Hz (vale) e %.2f dB em 1 kHz", notch, full);
    }
    {
        auto h = makeHost ("phaser", "deep");
        setByName (*h, "feedback", 90.0f);
        auto l = whiteNoise (48000 * 2, 0.1, 36), r = l;
        run (*h, l, r);
        CHECK (peak (l) < 10.0, "phaser de 12 estágios com 90%% de realimentação não dispara (pico %.2f)", peak (l));
    }
    {
        auto h = makeHost ("phaser");
        setByName (*h, "mix", 0.0f);
        const auto in = whiteNoise (48000, 0.1, 37);
        auto l = in, r = in;
        run (*h, l, r);
        CHECK (worstDiff (l, in, 0, 24000) == 0.0, "phaser em mistura 0: o som passa igual, sem latência");
    }

    // EQ dinâmico --------------------------------------------------------------
    auto dynEq = [&] (double freq, double amplitude, const std::vector<float>* side = nullptr, double sideAmplitude = 0.0)
    {
        auto h = makeHost ("dynEq");
        setByName (*h, "b1Freq", 220.0f);
        setByName (*h, "b1Thr", -24.0f);
        setByName (*h, "b1Range", -6.0f);
        setByName (*h, "ratio", 10.0f);
        setByName (*h, "b2On", 0.0f);
        const auto in = sine (freq, amplitude, 48000);
        auto l = in, r = in;
        std::vector<float> sl, sr;

        if (side != nullptr)
        {
            sl = *side;
            for (auto& v : sl)
                v *= (float) sideAmplitude;
            sr = sl;
        }

        run (*h, l, r, side != nullptr ? &sl : nullptr, side != nullptr ? &sr : nullptr);
        struct Result { double changeDb; double diff; float meter; };
        return Result { db (toneAmplitude (l, freq, 24000, 48000) / amplitude), worstDiff (l, in, 0, 0),
                        h->processor().meter (DynamicEqProcessor::band1Gain) };
    };
    {
        const auto loud = dynEq (220.0, 0.5), quiet = dynEq (220.0, 0.01), other = dynEq (2000.0, 0.5);
        CHECK (loud.changeDb < -5.5 && loud.changeDb > -6.5 && loud.meter < -5.5f,
               "EQ dinâmico: 220 Hz a -6 dBFS acima do limiar desce %.2f dB (a faixa é -6; o medidor diz %.1f)", loud.changeDb, loud.meter);
        CHECK (quiet.diff == 0.0, "abaixo do limiar o som passa idêntico (diferença %.1e)", quiet.diff);
        CHECK (std::abs (other.changeDb) < 0.05, "2 kHz alto não dispara a banda de 220 Hz (%.3f dB)", other.changeDb);

        const auto voice = sine (220.0, 1.0, 48000);
        const auto keyed = dynEq (220.0, 0.01, &voice, 0.5);
        CHECK (keyed.changeDb < -5.5, "com sidechain, o disparo vem da outra trilha: o 220 Hz baixo desce %.2f dB", keyed.changeDb);
    }
    // Os achados da revisão de 26/10 -----------------------------------------
    {
        // O teto vale depois da volta da sobreamostragem, também num agudo cortado reto.
        auto h = makeHost ("clipper");
        setByName (*h, "gain", 12.0f);
        setByName (*h, "softness", 0.0f);
        auto l = sine (12000.0, 1.0, 48000), r = l;
        run (*h, l, r);
        CHECK (db (peak (l, 4800)) < 2.5, "clipper: 12 kHz com +12 dB contra o teto de 0 dB passa %.2f dB do teto (o texto promete até 2 dB)",
               db (peak (l, 4800)));
    }
    {
        // O chorus não sobe o volume: grave em fase, largura 0 e só as cópias.
        auto level = [&] (float width, float voices)
        {
            auto h = makeHost ("chorus");
            setByName (*h, "width", width);
            setByName (*h, "voices", voices);
            setByName (*h, "mix", 100.0f);
            setByName (*h, "tone", 20000.0f);
            auto l = sine (60.0, 0.5, 48000 * 2), r = l;
            run (*h, l, r);
            return db (rms (l, 48000) / rms (sine (60.0, 0.5, 48000 * 2), 48000));
        };
        const double narrow = level (0.0f, 1.0f), wide = level (100.0f, 0.0f);
        CHECK (narrow < 0.5 && narrow > -4.0 && std::abs (wide) < 0.5,
               "chorus no grave não sobe o volume: %.2f dB (largura 0, 4 cópias, fora de fase entre si) e %.2f dB (largura 100)", narrow, wide);
    }
    {
        // Realimentação no máximo: o pico da ressonância fica abaixo de +14 dB (sem a compensação, +26 dB).
        auto h = makeHost ("flanger");
        setByName (*h, "depth", 0.0f);
        setByName (*h, "delay", 1.0f);
        setByName (*h, "feedback", 95.0f);
        auto l = sine (1000.0, 0.1, 48000 * 2), r = l;
        run (*h, l, r);
        const double gain = db (peak (l, 48000) / 0.1);
        CHECK (gain < 14.0, "flanger com 95%% de realimentação: a ressonância sobe %.1f dB (era +26)", gain);
    }
    {
        // Trocar os estágios do phaser tocando: nenhum salto maior que o do próprio som.
        auto h = makeHost ("phaser");
        setByName (*h, "mix", 100.0f);
        setByName (*h, "feedback", 0.0f);
        auto l = sine (300.0, 0.5, 48000), r = l;
        double worstJump = 0.0;

        for (size_t start = 0; start < l.size(); start += block)
        {
            if (start == 24064)
                h->setParam (h->info().indexOf ("stages"), 3.0f);

            h->process (l.data() + start, r.data() + start, (int) std::min<size_t> (block, l.size() - start), nullptr, nullptr);
        }

        for (size_t i = 24000; i < 26000; ++i)
            worstJump = std::max (worstJump, (double) std::abs (l[i] - l[i - 1]));

        const double sineStep = 0.5 * 2.0 * pi * 300.0 / rate;
        CHECK (worstJump < 1.2 * sineStep, "phaser: de 6 para 12 estágios tocando, o maior passo entre amostras é %.4f (o do seno, %.4f)",
               worstJump, sineStep);

        float v[PhaserProcessor::count];
        for (int i = 0; i < PhaserProcessor::count; ++i)
            v[i] = PhaserProcessor::staticInfo().params[(size_t) i].def;
        v[PhaserProcessor::centre] = 150.0f;
        v[PhaserProcessor::depth] = 100.0f;
        v[PhaserProcessor::stages] = 3.0f;
        v[PhaserProcessor::feedback] = 90.0f;
        auto p = createProcessor ("phaser");
        const double tail = p->tailSecondsFor (v);
        CHECK (tail > 1.0 && tail < 10.0, "phaser grave, 12 estágios e 90%%: a cauda informada é %.2f s", tail);
    }
    {
        // EQ dinâmico: depois de cortar, volta a deixar o som idêntico; e desligar a banda tocando não salta.
        auto h = makeHost ("dynEq");
        setByName (*h, "b1Freq", 220.0f);
        setByName (*h, "b1Thr", -24.0f);
        setByName (*h, "b1Range", -12.0f);
        setByName (*h, "b2On", 0.0f);
        auto l = sine (220.0, 0.5, 48000), r = l;
        const auto quiet = sine (220.0, 0.005, 48000, 0.0);
        l.insert (l.end(), quiet.begin(), quiet.end());
        r = l;
        const auto in = l;
        run (*h, l, r);
        double diff = 0.0;

        for (size_t i = 48000 + 24000; i < l.size(); ++i)
            diff = std::max (diff, (double) std::abs (l[i] - in[i]));

        CHECK (diff == 0.0, "EQ dinâmico: depois de cortar 12 dB, o trecho baixo volta a sair idêntico (diferença %.1e)", diff);

        auto h2 = makeHost ("dynEq");
        setByName (*h2, "b1Freq", 220.0f);
        setByName (*h2, "b1Range", -24.0f);
        setByName (*h2, "ratio", 10.0f);
        setByName (*h2, "b2On", 0.0f);
        auto a = sine (220.0, 0.5, 48000), b = a;
        double worstJump = 0.0;

        for (size_t start = 0; start < a.size(); start += block)
        {
            if (start == 24064)
                h2->setParam (h2->info().indexOf ("b1On"), 0.0f);

            h2->process (a.data() + start, b.data() + start, (int) std::min<size_t> (block, a.size() - start), nullptr, nullptr);
        }

        for (size_t i = 24000; i < 26000; ++i)
            worstJump = std::max (worstJump, (double) std::abs (a[i] - a[i - 1]));

        const double sineStep = 0.5 * 2.0 * pi * 220.0 / rate;
        CHECK (worstJump < 1.5 * sineStep, "EQ dinâmico: desligar a banda cortando 24 dB tocando dá passo máximo %.4f (o do seno, %.4f)",
               worstJump, sineStep);
    }

    CHECK (DynamicEqProcessor::gainChange (-6.0f, -24.0f, -6.0f, 3.0f) == -6.0f
               && std::abs (DynamicEqProcessor::gainChange (-21.0f, -24.0f, -6.0f, 3.0f) + 2.0f) < 1.0e-5f
               && std::abs (DynamicEqProcessor::gainChange (-21.0f, -24.0f, 6.0f, 3.0f) - 2.0f) < 1.0e-5f
               && DynamicEqProcessor::gainChange (-30.0f, -24.0f, -6.0f, 3.0f) == 0.0f,
           "a conta do EQ dinâmico: 3 dB acima em 3:1 = 2 dB, com o sinal e o teto da faixa");
}

static void testRobustness()
{
    std::printf ("Robustez de todos os efeitos (silêncio, sinal enorme, impulso)\n");

    bool silentOk = true, finiteOk = true, latencyOk = true;
    int withLatency = 0;

    auto bad = [] (float v) { return ! std::isfinite (v) || std::fpclassify (v) == FP_SUBNORMAL; };

    for (auto* e : allEffects())
    {
        // Silêncio
        {
            auto h = makeHost (e->type);
            std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
            run (*h, l, r);

            for (size_t i = 0; i < l.size(); ++i)
                if (l[i] != 0.0f || r[i] != 0.0f)
                {
                    silentOk = false;
                    std::printf ("    %s: silêncio virou %.3g na amostra %d\n", e->type, (double) l[i], (int) i);
                    break;
                }
        }

        // Enorme (+60 dBFS) e depois silêncio: nada explode e a cauda volta ao zero.
        {
            auto h = makeHost (e->type);
            auto l = sine (220.0, 1000.0, 24000), r = sine (330.0, 1000.0, 24000);
            l.resize (96000, 0.0f);
            r.resize (96000, 0.0f);
            run (*h, l, r, &l, &r);

            for (size_t i = 0; i < l.size(); ++i)
                if (bad (l[i]) || bad (r[i]))
                {
                    finiteOk = false;
                    std::printf ("    %s: sinal enorme deu %g na amostra %d\n", e->type, (double) l[i], (int) i);
                    break;
                }
        }

        // Impulso
        {
            auto h = makeHost (e->type);
            std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
            l[2000] = r[2000] = 0.5f;
            run (*h, l, r);

            for (size_t i = 0; i < l.size(); ++i)
                if (bad (l[i]) || bad (r[i]))
                {
                    finiteOk = false;
                    std::printf ("    %s: impulso deu %g na amostra %d\n", e->type, (double) l[i], (int) i);
                    break;
                }

            const std::string_view type (e->type);
            const bool neutralAtDefaults = type == "gate" || type == "denoise" || type == "console" || type == "fet" || type == "voicepitch";

            if (h->latencySamples() > 0 && neutralAtDefaults)
            {
                ++withLatency;
                size_t at = 0;

                for (size_t i = 0; i < l.size(); ++i)
                    if (std::abs (l[i]) > std::abs (l[at]))
                        at = i;

                if ((int) at - 2000 != h->latencySamples())
                {
                    latencyOk = false;
                    std::printf ("    %s: impulso saiu %d amostras depois; informado %d\n", e->type, (int) at - 2000, h->latencySamples());
                }
            }
        }
    }

    CHECK (silentOk, "silêncio entra, silêncio sai, em todos os %d efeitos", (int) allEffects().size());
    CHECK (finiteOk, "sinal de +60 dBFS e impulso: nenhum NaN, infinito ou desnormal");
    CHECK (latencyOk && withLatency == 5, "gate, FET, limpa-voz, console e tom da voz: o impulso sai na latência informada");
}

int main()
{
    const DenormalGuard guard;

    testFilters();
    testOversampler();
    testLoudness();
    testDynamics();
    testColor();
    testLimiter();
    testSpace();
    testStereo();
    testHostAndEq();
    testSmoothChanges();
    testCatalog();
    testTune();
    testTruePeakOffline();
    testGate();
    testVintage();
    testMultiband();
    testLeveler();
    testDoubler();
    testBassPlus();
    testDenoise();
    testConsole();
    testTuneHumanize();
    testVoicePitch();
    testHarmony();
    testEffects2610();
    testRobustness();

    std::printf ("\n%s (%d falha%s)\n", failures == 0 ? "TUDO OK" : "HOUVE FALHAS", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
