// Testes do núcleo de tempo real (sem JUCE): glide do 808 e troca de objetos entre threads.
// Uso: whynot_audio_tests [arquivo.wav]  (grava o áudio do teste de glide para ouvir)

#include "../../src/audio/Onsets.h"
#include "../../src/audio/PianoVoice.h"
#include "../../src/audio/RtObjectSlot.h"
#include "../../src/audio/SampleVoice.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace whynot::audio;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static constexpr double pi = 3.14159265358979323846;

static double keyToHz (double key) { return 440.0 * std::exp2 ((key - 69.0) / 12.0); }

// Frequência média por cruzamentos de zero subindo, entre os frames [from, to).
static double measureHz (const std::vector<float>& x, std::size_t from, std::size_t to, double rate)
{
    std::size_t first = 0, last = 0, crossings = 0;

    for (std::size_t i = from + 1; i < to; ++i)
    {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            if (crossings == 0) first = i;
            last = i;
            ++crossings;
        }
    }

    return crossings < 2 ? 0.0 : (double) (crossings - 1) * rate / (double) (last - first);
}

static void writeWav (const std::string& path, const std::vector<float>& x, int rate)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (std::uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&] (std::uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
    const auto dataBytes = (std::uint32_t) (x.size() * 2);
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVEfmt ", 8);
    u32 (16); u16 (1); u16 (1); u32 ((std::uint32_t) rate); u32 ((std::uint32_t) rate * 2); u16 (2); u16 (16);
    f.write ("data", 4); u32 (dataBytes);

    for (float s : x)
        u16 ((std::uint16_t) (std::int16_t) std::lround (std::clamp (s, -1.0f, 1.0f) * 32767.0f));
}

static void testGlide (const char* wavPath)
{
    std::printf ("Glide do 808\n");

    constexpr double rate = 48000.0;
    constexpr int rootKey = 36; // Dó 1, ~65,4 Hz

    // "808" sintético: senoide com decaimento lento, 2 s.
    SampleData s;
    s.sampleRate = rate;
    s.rootKey = rootKey;
    s.channels.assign (1, std::vector<float> ((std::size_t) (2.0 * rate)));

    for (std::size_t i = 0; i < s.channels[0].size(); ++i)
    {
        const double t = (double) i / rate;
        s.channels[0][i] = (float) (0.9 * std::sin (2.0 * pi * keyToHz (rootKey) * t) * std::exp (-0.8 * t));
    }

    SampleVoice voice;
    voice.prepare (rate);
    VoiceParams p;
    p.glideMs = 80.0f;

    std::vector<float> out ((std::size_t) (1.2 * rate), 0.0f);
    float* chans[] = { out.data() };
    const int block = 256;
    const std::size_t slideAt = (std::size_t) (0.25 * rate);
    const std::size_t releaseAt = (std::size_t) (0.85 * rate);

    voice.noteOn (rootKey, 1.0f, false, p);

    for (std::size_t pos = 0; pos < out.size(); pos += block)
    {
        if (pos == (slideAt / block) * block)  voice.noteOn (rootKey + 7, 1.0f, true, p);   // slide uma quinta acima
        if (pos == (releaseAt / block) * block) voice.noteOff();

        float* blockChans[] = { chans[0] + pos };
        voice.render (s, blockChans, 1, (int) std::min<std::size_t> (block, out.size() - pos));
    }

    bool finite = true;
    float maxJump = 0.0f, peak = 0.0f;

    for (std::size_t i = 0; i < out.size(); ++i)
    {
        finite = finite && std::isfinite (out[i]);
        peak = std::max (peak, std::abs (out[i]));
        if (i > 0) maxJump = std::max (maxJump, std::abs (out[i] - out[i - 1]));
    }

    const double before = measureHz (out, (std::size_t) (0.05 * rate), (std::size_t) (0.24 * rate), rate);
    const double after  = measureHz (out, (std::size_t) (0.40 * rate), (std::size_t) (0.84 * rate), rate);
    const double expectBefore = keyToHz (rootKey);
    const double expectAfter  = keyToHz (rootKey + 7);

    // Maior salto possível de uma senoide de 98 Hz e amplitude 0,9 a 48 kHz ≈ 0,0115 por amostra.
    CHECK (finite, "sem NaN/Inf");
    CHECK (peak <= 1.0f, "pico %.3f <= 1", peak);
    CHECK (maxJump < 0.02f, "maior salto entre amostras %.4f < 0.02 (sem estalo no slide nem nas rampas)", maxJump);
    CHECK (std::abs (before - expectBefore) < 1.0, "antes do slide: %.2f Hz (esperado %.2f)", before, expectBefore);
    CHECK (std::abs (after - expectAfter) < 1.0, "depois do slide: %.2f Hz (esperado %.2f)", after, expectAfter);
    CHECK (std::abs (voice.currentKey() - (float) (rootKey + 7)) < 1.0e-3f || ! voice.isActive(), "glide termina exatamente na nota alvo");
    CHECK (! voice.isActive(), "voz desligou depois do release");

    const std::size_t tailStart = releaseAt + (std::size_t) (0.02 * rate);
    float tail = 0.0f;
    for (std::size_t i = tailStart; i < out.size(); ++i) tail = std::max (tail, std::abs (out[i]));
    CHECK (tail == 0.0f, "silêncio total 20 ms depois do noteOff (release de 15 ms)");

    if (wavPath != nullptr)
    {
        writeWav (wavPath, out, (int) rate);
        std::printf ("  WAV gravado em %s\n", wavPath);
    }
}

static SampleData makeSine (double rate, int rootKey, double seconds)
{
    SampleData s;
    s.sampleRate = rate;
    s.rootKey = rootKey;
    s.channels.assign (1, std::vector<float> ((std::size_t) (seconds * rate)));

    for (std::size_t i = 0; i < s.channels[0].size(); ++i)
        s.channels[0][i] = (float) (0.5 * std::sin (2.0 * pi * keyToHz (rootKey) * (double) i / rate));

    return s;
}

static void testInvalidParams()
{
    std::printf ("Parâmetros inválidos\n");

    const auto s = makeSine (48000.0, 36, 1.0);
    SampleVoice voice;
    voice.prepare (48000.0);

    VoiceParams bad;
    bad.gain = std::nanf ("");
    bad.glideMs = 1.0e12f;
    bad.attackMs = -5.0f;
    bad.releaseMs = std::numeric_limits<float>::infinity();
    bad.tuneSemis = std::nanf ("");
    bad.sustain = -3.0f;
    bad.decayMs = std::nanf ("");
    bad.pitchEnvSemis = std::numeric_limits<float>::infinity();
    bad.pitchEnvMs = 0.0f;
    bad.startFraction = 7.0f;

    std::vector<float> out (4800, 0.0f);
    float* chans[] = { out.data() };
    voice.noteOn (36, std::nanf (""), false, bad);
    voice.render (s, chans, 1, 2400);
    voice.noteOn (48, 1.0f, true, bad);
    float* rest[] = { out.data() + 2400 };
    voice.render (s, rest, 1, 2400);

    bool finite = true;
    for (float x : out) finite = finite && std::isfinite (x);

    CHECK (finite, "gain NaN, velocity NaN, afinação NaN, envelopes e início absurdos não geram NaN/Inf");
}

// Renderiza `seconds` em blocos de 256; `atBlock(pos)` roda antes de cada bloco (eventos de nota).
template <typename Events>
static std::vector<float> renderVoice (SampleVoice& voice, const SampleData& s, double rate, double seconds, Events atBlock)
{
    std::vector<float> out ((std::size_t) (seconds * rate), 0.0f);
    constexpr std::size_t block = 256;

    for (std::size_t pos = 0; pos < out.size(); pos += block)
    {
        atBlock (pos);
        float* chans[] = { out.data() + pos };
        voice.render (s, chans, 1, (int) std::min (block, out.size() - pos));
    }

    return out;
}

static float peakIn (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min (to, x.size()); ++i) p = std::max (p, std::abs (x[i]));
    return p;
}

static float maxJump (const std::vector<float>& x)
{
    float j = 0.0f;
    for (std::size_t i = 1; i < x.size(); ++i) j = std::max (j, std::abs (x[i] - x[i - 1]));
    return j;
}

static void testTuneAndPitchEnvelope()
{
    std::printf ("Afinação e queda de tom\n");

    constexpr double rate = 48000.0;
    const auto s = makeSine (rate, 36, 2.0);
    auto at = [] (double t) { return (std::size_t) (t * rate); };

    {
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.tuneSemis = 12.0f;
        voice.noteOn (36, 1.0f, false, p);
        const auto out = renderVoice (voice, s, rate, 0.6, [] (std::size_t) {});
        const double hz = measureHz (out, at (0.05), at (0.55), rate);
        CHECK (std::abs (hz - keyToHz (48)) < 0.5, "afinação +12: %.2f Hz (esperado %.2f)", hz, keyToHz (48));
    }

    {
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.tuneSemis = 0.5f;
        voice.noteOn (36, 1.0f, false, p);
        const auto out = renderVoice (voice, s, rate, 1.0, [] (std::size_t) {});
        const double hz = measureHz (out, at (0.05), at (0.95), rate);
        CHECK (std::abs (hz - keyToHz (36.5)) < 0.1, "afinação +50 cents: %.3f Hz (esperado %.3f)", hz, keyToHz (36.5));
    }

    {
        const auto mid = makeSine (rate, 60, 1.0);
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.pitchEnvSemis = 12.0f;
        p.pitchEnvMs = 100.0f;
        voice.noteOn (60, 1.0f, false, p);
        const auto out = renderVoice (voice, mid, rate, 0.6, [] (std::size_t) {});

        const double start = measureHz (out, 0, at (0.02), rate);
        const double settled = measureHz (out, at (0.15), at (0.55), rate);
        CHECK (start > keyToHz (60) * 1.3, "começo da nota acima do tom: %.1f Hz (nota %.1f Hz)", start, keyToHz (60));
        CHECK (std::abs (settled - keyToHz (60)) < 0.5, "depois de 150 ms chega à nota: %.2f Hz (esperado %.2f)", settled, keyToHz (60));
        CHECK (maxJump (out) < 0.05f, "queda de tom sem estalo (maior salto %.4f)", maxJump (out));
    }

    {
        // Slide durante a queda: a queda continua, não recomeça.
        const auto mid = makeSine (rate, 60, 1.0);
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.pitchEnvSemis = 12.0f;
        p.pitchEnvMs = 40.0f;
        p.glideMs = 1.0f;
        voice.noteOn (60, 1.0f, false, p);
        const auto out = renderVoice (voice, mid, rate, 0.4, [&] (std::size_t pos)
        {
            if (pos == 256 * 47) voice.noteOn (67, 1.0f, true, p);   // ~250 ms
        });
        const double after = measureHz (out, at (0.26), at (0.39), rate);
        CHECK (std::abs (after - keyToHz (67)) < 1.0, "slide depois da queda vai direto à nota nova: %.2f Hz (esperado %.2f)", after, keyToHz (67));
    }
}

static void testDecaySustain()
{
    std::printf ("Decaimento e sustentação\n");

    constexpr double rate = 48000.0;
    const auto s = makeSine (rate, 36, 2.0);   // amplitude 0,5
    auto at = [] (double t) { return (std::size_t) (t * rate); };

    {
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.attackMs = 0.0f;
        p.sustain = 0.0f;
        p.decayMs = 200.0f;
        voice.noteOn (36, 1.0f, false, p);   // nota segurada: só o decaimento termina a voz
        const auto out = renderVoice (voice, s, rate, 1.0, [] (std::size_t) {});

        const float level = 20.0f * std::log10 (peakIn (out, at (0.19), at (0.21)) / 0.5f);
        CHECK (level > -63.0f && level < -57.0f, "aos 200 ms o som está a %.1f dB (esperado ~-60 dB)", level);
        CHECK (! voice.isActive(), "sem sustentação a voz termina sozinha, mesmo com a nota segurada");
        CHECK (peakIn (out, at (0.4), out.size()) == 0.0f, "silêncio total depois de 400 ms");
        CHECK (maxJump (out) < 0.01f, "decaimento sem estalo (maior salto %.4f)", maxJump (out));
    }

    {
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.sustain = 0.5f;
        p.decayMs = 100.0f;
        voice.noteOn (36, 1.0f, false, p);
        const auto out = renderVoice (voice, s, rate, 0.7, [&] (std::size_t pos)
        {
            if (pos == 256 * 117) voice.noteOff();   // ~624 ms
        });

        const float held = peakIn (out, at (0.4), at (0.5));
        CHECK (std::abs (held - 0.25f) < 0.0025f, "sustentação em 50%%: pico %.4f (esperado 0,25)", held);
        CHECK (peakIn (out, at (0.65), out.size()) == 0.0f, "release depois do sustain termina em 15 ms");
        CHECK (maxJump (out) < 0.01f, "sem estalo no decaimento nem no release (maior salto %.4f)", maxJump (out));
    }

    {
        // noteOff no meio do decaimento: a rampa de saída parte do nível atual.
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.sustain = 0.0f;
        p.decayMs = 1000.0f;
        voice.noteOn (36, 1.0f, false, p);
        const auto out = renderVoice (voice, s, rate, 0.4, [&] (std::size_t pos)
        {
            if (pos == 256 * 30) voice.noteOff();   // 160 ms
        });

        CHECK (! voice.isActive() && peakIn (out, at (0.2), out.size()) == 0.0f, "noteOff durante o decaimento: release normal");
        CHECK (maxJump (out) < 0.01f, "sem estalo ao soltar no meio do decaimento (maior salto %.4f)", maxJump (out));
    }

    {
        // Slide com outro sustain no meio do decaimento: o envelope segue o da nota que toca.
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.sustain = 0.2f;
        p.decayMs = 400.0f;
        voice.noteOn (36, 1.0f, false, p);
        VoiceParams louder = p;
        louder.sustain = 0.9f;
        const auto out = renderVoice (voice, s, rate, 1.2, [&] (std::size_t pos)
        {
            if (pos == 256 * 19) voice.noteOn (38, 1.0f, true, louder);   // ~100 ms
        });

        CHECK (maxJump (out) < 0.01f, "slide com sustain novo não salta o volume (maior salto %.4f)", maxJump (out));
        CHECK (std::abs (peakIn (out, at (1.0), at (1.15)) - 0.1f) < 0.002f, "continua indo ao sustain da nota original (pico %.4f, esperado 0,1)",
               peakIn (out, at (1.0), at (1.15)));
    }
}

// Ajuste de mínimos quadrados de uma senoide de frequência conhecida: 10·log10(sinal/resíduo).
static double sineSnrDb (const std::vector<float>& x, std::size_t from, std::size_t to, double hz, double rate)
{
    double ss = 0, sc = 0, cc = 0, xs = 0, xc = 0;

    for (std::size_t i = from; i < to; ++i)
    {
        const double s = std::sin (2.0 * pi * hz * (double) i / rate), c = std::cos (2.0 * pi * hz * (double) i / rate);
        ss += s * s; cc += c * c; sc += s * c; xs += x[i] * s; xc += x[i] * c;
    }

    const double det = ss * cc - sc * sc;
    const double a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
    double sig = 0, err = 0;

    for (std::size_t i = from; i < to; ++i)
    {
        const double fit = a * std::sin (2.0 * pi * hz * (double) i / rate) + b * std::cos (2.0 * pi * hz * (double) i / rate);
        sig += fit * fit;
        err += ((double) x[i] - fit) * ((double) x[i] - fit);
    }

    return 10.0 * std::log10 (sig / std::max (err, 1.0e-30));
}

static void testInterpolationQuality()
{
    std::printf ("Qualidade da leitura fora do tom (Hermite)\n");

    constexpr double outRate = 48000.0;

    auto render = [] (double srcRate, double toneHz, int key, float tune, double seconds)
    {
        SampleData s;
        s.sampleRate = srcRate;
        s.rootKey = 60;
        s.channels.assign (1, std::vector<float> ((std::size_t) (2.0 * srcRate)));

        for (std::size_t i = 0; i < s.channels[0].size(); ++i)
            s.channels[0][i] = (float) (0.5 * std::sin (2.0 * pi * toneHz * (double) i / srcRate));

        SampleVoice voice;
        voice.prepare (outRate);
        VoiceParams p;
        p.attackMs = 0.0f;
        p.tuneSemis = tune;
        voice.noteOn (key, 1.0f, false, p);
        std::vector<float> out ((std::size_t) (seconds * outRate), 0.0f);

        for (std::size_t pos = 0; pos < out.size(); pos += 256)
        {
            float* chans[] = { out.data() + pos };
            voice.render (s, chans, 1, (int) std::min<std::size_t> (256, out.size() - pos));
        }

        return std::make_pair (out, s);
    };

    {
        const auto [out, s] = render (44100.0, 3000.0, 67, 0.0f, 0.5);
        const double snr = sineSnrDb (out, 960, out.size() - 256, 3000.0 * std::exp2 (7.0 / 12.0), outRate);
        CHECK (snr > 55.0, "3 kHz a 44,1 kHz uma quinta acima: sinal/erro %.1f dB (linear dava 43 dB)", snr);
    }

    {
        const auto [out, s] = render (44100.0, 200.0, 72, 0.0f, 0.5);
        const double snr = sineSnrDb (out, 960, out.size() - 256, 400.0, outRate);
        // Acima do tom a leitura é a sinc da tabela (sem aliasing): o erro fica perto de -90 dB (a mesma com 128
        // ou 256 fases: vem do ganho 1 de cada linha da tabela, não da interpolação). O Hermite sozinho dava
        // 131 dB aqui, mas dobrava os agudos (revisão de 22/10). Os dois estão muito abaixo do audível.
        CHECK (snr > 85.0, "808 de 200 Hz uma oitava acima: sinal/erro %.1f dB (linear dava 90; o Hermite, 131)", snr);
    }

    {
        // No tom original e na mesma taxa a leitura é exata: frac = 0 devolve a própria amostra.
        const auto [out, s] = render (48000.0, 1000.0, 60, 0.0f, 0.1);
        bool same = true;

        for (std::size_t i = 1; i < 4000; ++i)
            same = same && out[i] == s.channels[0][i];

        CHECK (same, "no tom original, na mesma taxa, o sample sai bit a bit igual");
    }
}

static void testChokeFade()
{
    std::printf ("Corte rápido (choke)\n");

    constexpr double rate = 48000.0;
    const auto s = makeSine (rate, 36, 2.0);   // amplitude 0,5
    SampleVoice voice;
    voice.prepare (rate);
    VoiceParams p;
    p.releaseMs = 300.0f;   // o release normal é longo; o corte não usa ele
    voice.noteOn (36, 1.0f, false, p);
    const std::size_t cutAt = 256 * 20;   // ~107 ms
    const auto out = renderVoice (voice, s, rate, 0.3, [&] (std::size_t pos) { if (pos == cutAt) voice.fadeOut (3.0f); });

    const float after = peakIn (out, cutAt + (std::size_t) (0.0035 * rate), out.size());
    CHECK (after == 0.0f && ! voice.isActive(), "fadeOut de 3 ms: silêncio total 3,5 ms depois do corte (o release era de 300 ms)");
    CHECK (maxJump (out) < 0.01f, "o corte não estala (maior salto %.4f)", maxJump (out));

    SampleVoice idle;
    idle.prepare (rate);
    idle.fadeOut (3.0f);
    CHECK (! idle.isActive(), "fadeOut numa voz parada não faz nada");
}

static void testStartOffset()
{
    std::printf ("Início do sample\n");

    constexpr double rate = 48000.0;
    SampleData ramp;
    ramp.sampleRate = rate;
    ramp.rootKey = 60;
    ramp.channels.assign (1, std::vector<float> ((std::size_t) rate));

    for (std::size_t i = 0; i < ramp.channels[0].size(); ++i)
        ramp.channels[0][i] = (float) i / (float) ramp.channels[0].size();

    auto firstSample = [&] (float startFraction)
    {
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.attackMs = 0.0f;
        p.startFraction = startFraction;
        voice.noteOn (60, 1.0f, false, p);
        std::vector<float> out (64, 0.0f);
        float* chans[] = { out.data() };
        voice.render (ramp, chans, 1, 64);
        return out[0];
    };

    CHECK (std::abs (firstSample (0.0f)) < 1.0e-6f, "sem início: lê do começo (%.4f)", firstSample (0.0f));
    CHECK (std::abs (firstSample (0.5f) - 0.5f) < 1.0e-3f, "início em 50%%: primeira amostra %.4f", firstSample (0.5f));

    SampleVoice voice;
    voice.prepare (rate);
    VoiceParams p;
    p.startFraction = 0.99f;
    voice.noteOn (60, 1.0f, false, p);
    const auto out = renderVoice (voice, ramp, rate, 0.05, [] (std::size_t) {});
    bool finite = true;
    for (float x : out) finite = finite && std::isfinite (x);
    CHECK (finite && ! voice.isActive(), "início em 99%%: toca o resto e termina, sem passar do fim");
}

static void testLongGlideIsSmooth()
{
    std::printf ("Glide longo (1,5 s)\n");

    constexpr double rate = 48000.0;
    const auto s = makeSine (rate, 36, 4.0);
    SampleVoice voice;
    voice.prepare (rate);

    VoiceParams p;
    p.glideMs = 1500.0f;

    std::vector<float> scratch (256, 0.0f);
    float* chans[] = { scratch.data() };

    voice.noteOn (36, 1.0f, false, p);
    voice.render (s, chans, 1, 256);
    voice.noteOn (48, 1.0f, true, p);   // uma oitava acima, devagar

    std::vector<float> keys;

    for (int b = 0; b < 300; ++b)
    {
        voice.render (s, chans, 1, 256);
        keys.push_back (voice.currentKey());
    }

    const float expectedStep = 12.0f * 256.0f / (float) std::lround (1.5 * rate);
    float worst = 0.0f;
    bool monotonic = true;

    for (std::size_t i = 1; i < keys.size(); ++i)
    {
        const float step = keys[i] - keys[i - 1];
        monotonic = monotonic && step >= 0.0f;
        if (keys[i] < 48.0f) // ignora o último bloco, que termina o glide no meio
            worst = std::max (worst, std::abs (step - expectedStep));
    }

    CHECK (monotonic, "o tom só sobe durante o glide");
    CHECK (keys.back() == 48.0f, "termina exatamente na nota 48");
    CHECK (worst < expectedStep * 0.01f, "passos de tom por bloco iguais até o fim (desvio máx. %.6f, passo %.6f)", worst, expectedStep);
}

struct Counted
{
    static inline std::atomic<int> alive { 0 };
    int value = 0;
    explicit Counted (int v) : value (v) { ++alive; }
    ~Counted() { --alive; }
};

// Um 808 de teste: seno de 65,4 Hz (C2) que cai com constante de 0,8 s.
static SampleData makeDecaying808 (double rate, double seconds)
{
    SampleData s;
    s.sampleRate = rate;
    s.rootKey = 36;
    s.channels.assign (1, std::vector<float> ((std::size_t) (seconds * rate)));

    for (std::size_t i = 0; i < s.channels[0].size(); ++i)
    {
        const double t = (double) i / rate;
        s.channels[0][i] = (float) (0.8 * std::exp (-t / 0.8) * std::sin (2.0 * pi * keyToHz (36) * t));
    }

    return s;
}

static float maxJumpIn (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    float j = 0.0f;
    for (std::size_t i = std::max<std::size_t> (from, 1); i < std::min (to, x.size()); ++i) j = std::max (j, std::abs (x[i] - x[i - 1]));
    return j;
}

static void testLoopPoints()
{
    std::printf ("Laço com pontos de loop (nota mais longa que o sample)\n");

    constexpr double rate = 48000.0;
    const auto s = makeDecaying808 (rate, 1.0);
    const auto& src = s.channels[0];

    // O maior salto do próprio sample no trecho do laço: a régua do "sem estalo".
    auto naturalJump = [&] (double from, double to)
    {
        return maxJumpIn (src, (std::size_t) (from * rate), (std::size_t) (to * rate));
    };

    auto looped = [&] (float start, float end, float xfadeMs, int key, double seconds, std::size_t releaseAt, bool oneShot)
    {
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.loop = true;
        p.loopStart = start;
        p.loopEnd = end;
        p.loopXfadeMs = xfadeMs;
        p.releaseMs = 30.0f;
        voice.noteOn (key, 1.0f, false, p);
        const auto out = renderVoice (voice, s, rate, seconds, [&] (std::size_t pos)
        {
            if (pos == releaseAt)
            {
                if (oneShot) voice.leaveLoop();
                else         voice.noteOff();
            }
        });
        return std::make_pair (out, voice.isActive());
    };

    {
        // Laço de 0,5 a 0,9 do sample com 20 ms de crossfade, nota de 3 s num sample de 1 s.
        const std::size_t releaseAt = 256 * (std::size_t) (3.0 * rate / 256);
        const auto [out, stillActive] = looped (0.5f, 0.9f, 20.0f, 36, 3.3, releaseAt, false);
        bool finite = true;
        for (float x : out) finite = finite && std::isfinite (x);

        const float late = peakIn (out, (std::size_t) (2.4 * rate), (std::size_t) (2.6 * rate));
        const float natural = naturalJump (0.45, 0.9);
        const float jump = maxJumpIn (out, (std::size_t) (0.9 * rate), releaseAt);
        CHECK (finite, "sem NaN/Inf");
        CHECK (late > 0.15f, "aos 2,5 s (sample de 1 s) ainda soa: pico %.3f", late);
        CHECK (jump <= natural * 1.25f, "as voltas do laço não estalam: maior salto %.5f, o do próprio sample %.5f", jump, natural);
        CHECK (! stillActive && peakIn (out, releaseAt + (std::size_t) (0.035 * rate), out.size()) == 0.0f,
               "a nota solta sai pelo release (30 ms), dentro do laço, e a voz desliga");

        const double hz = measureHz (out, (std::size_t) (1.2 * rate), (std::size_t) (2.8 * rate), rate);
        CHECK (std::abs (hz - keyToHz (36)) < 0.5, "o tom no laço é o do sample: %.2f Hz (%.2f)", hz, keyToHz (36));
    }

    {
        // Pontos fora de fase (o laço não tem períodos inteiros): o crossfade evita o estalo...
        const auto [out, active] = looped (0.5f, 0.8037f, 30.0f, 36, 2.5, (std::size_t) -1, false);
        const float jump = maxJumpIn (out, (std::size_t) (0.8 * rate), out.size());
        const float natural = naturalJump (0.45, 0.81);
        CHECK (active && jump <= natural * 1.3f, "laço fora de fase com crossfade: maior salto %.5f (sample %.5f)", jump, natural);

        // ...que sem crossfade aparece, e a régua do teste pega.
        const auto [hard, active2] = looped (0.5f, 0.8037f, 0.0f, 36, 2.5, (std::size_t) -1, false);
        const float hardJump = maxJumpIn (hard, (std::size_t) (0.8 * rate), hard.size());
        CHECK (active2 && hardJump > natural * 3.0f, "o mesmo laço sem crossfade salta %.4f (a régua acha o estalo)", hardJump);
    }

    {
        // Uma quinta acima: o laço segue o tom e continua sem estalo.
        const auto [out, active] = looped (0.5f, 0.9f, 20.0f, 43, 3.0, (std::size_t) -1, false);
        const double hz = measureHz (out, (std::size_t) (1.0 * rate), (std::size_t) (2.9 * rate), rate);
        const float jump = maxJumpIn (out, (std::size_t) (0.6 * rate), out.size());
        const float natural = naturalJump (0.45, 0.9) * (float) std::exp2 (7.0 / 12.0);   // lido mais rápido, salta mais
        CHECK (active && std::abs (hz - keyToHz (43)) < 0.8, "laço numa quinta acima: %.2f Hz (%.2f)", hz, keyToHz (43));
        CHECK (jump <= natural * 1.3f, "e sem estalo (maior salto %.5f, régua %.5f)", jump, natural);
    }

    {
        // Canal one-shot: soltar a nota sai do laço e o sample toca até o fim, sem salto.
        const std::size_t releaseAt = 256 * (std::size_t) (1.5 * rate / 256);
        const auto [out, stillActive] = looped (0.5f, 0.9f, 20.0f, 36, 2.5, releaseAt, true);
        int lastSound = 0;
        for (std::size_t i = 0; i < out.size(); ++i) if (out[i] != 0.0f) lastSound = (int) i;
        const double endsAt = lastSound / rate;
        // Até 1 ms antes do fim: o fim do arquivo corta seco, como sempre num one-shot (o sample de teste não acaba em silêncio).
        const float jump = maxJumpIn (out, (std::size_t) (0.9 * rate), (std::size_t) lastSound - 48);
        const float natural = naturalJump (0.45, 1.0);
        std::size_t at = 0; float big = 0.0f;
        for (std::size_t i = 1; i < out.size(); ++i) if (std::abs (out[i] - out[i - 1]) > big) { big = std::abs (out[i] - out[i - 1]); at = i; }
        std::printf ("  (maior salto em %.4f s; fim em %.4f s)\n", at / rate, endsAt);
        CHECK (! stillActive && endsAt > 1.5 && endsAt < 1.5 + 0.55, "solto no laço, toca o resto do sample e termina (em %.3f s)", endsAt);
        CHECK (jump <= natural * 1.25f, "sair do laço não salta (maior salto %.5f, sample %.5f)", jump, natural);
    }

    {
        // Laço curto demais (menos de 16 amostras) ou que o início já passou: a nota acaba no fim do sample.
        const auto [tiny, tinyActive] = looped (0.5f, 0.5001f, 20.0f, 36, 1.3, (std::size_t) -1, false);
        CHECK (! tinyActive && peakIn (tiny, (std::size_t) (1.01 * rate), tiny.size()) == 0.0f, "laço de 4 amostras fica desligado");

        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.loop = true;
        p.loopStart = 0.2f;
        p.loopEnd = 0.4f;
        p.startFraction = 0.6f;
        voice.noteOn (36, 1.0f, false, p);
        renderVoice (voice, s, rate, 1.0, [] (std::size_t) {});
        CHECK (! voice.isActive(), "início depois do fim do laço: toca até o fim, sem laço");

        VoiceParams bad;
        bad.loop = true;
        bad.loopStart = std::numeric_limits<float>::quiet_NaN();
        bad.loopEnd = std::numeric_limits<float>::infinity();
        bad.loopXfadeMs = -5.0f;
        SampleVoice v2;
        v2.prepare (rate);
        v2.noteOn (36, 1.0f, false, bad);
        const auto out = renderVoice (v2, s, rate, 0.5, [] (std::size_t) {});
        bool finite = true;
        for (float x : out) finite = finite && std::isfinite (x);
        CHECK (finite, "pontos NaN e infinitos não geram NaN/Inf");
    }

    {
        // Um slide no meio do laço continua o laço (o 808 que desliza segurado).
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.loop = true;
        p.loopStart = 0.5f;
        p.loopEnd = 0.9f;
        voice.noteOn (36, 1.0f, false, p);
        const auto out = renderVoice (voice, s, rate, 2.5, [&] (std::size_t pos) { if (pos == 256 * 234) voice.noteOn (41, 1.0f, true, p); });
        const double hz = measureHz (out, (std::size_t) (1.6 * rate), (std::size_t) (2.4 * rate), rate);
        CHECK (voice.isLooping() && std::abs (hz - keyToHz (41)) < 0.6, "slide segurado no laço: %.2f Hz (%.2f), ainda no laço", hz, keyToHz (41));
    }
}

// Um beat de teste a 120 BPM, 2 compassos (4 s): kick nos tempos 1 e 3, caixa no 2 e no 4,
// chimbal nos contratempos de colcheia. 16 batidas, uma a cada 0,25 s, com silêncio entre elas.
static std::vector<float> makeTestBeat (double rate, std::vector<double>& hits)
{
    std::vector<float> x ((std::size_t) (4.0 * rate), 0.0f);
    std::uint32_t noise = 12345u;
    auto white = [&noise] { noise = noise * 1664525u + 1013904223u; return (float) ((double) (noise >> 8) / 8388608.0 - 1.0); };
    hits.clear();

    for (int i = 0; i < 16; ++i)
    {
        const double t0 = 0.25 * i;
        const auto start = (std::size_t) std::lround (t0 * rate);
        hits.push_back ((double) start);
        const int kind = i % 2 == 1 ? 2 : (i % 4 == 0 ? 0 : 1);   // 0 kick, 1 caixa, 2 chimbal
        double phase = 0.0;

        for (std::size_t j = 0; j < (std::size_t) (0.2 * rate) && start + j < x.size(); ++j)
        {
            const double t = (double) j / rate;
            float v = 0.0f;

            if (kind == 0)
            {
                phase += 2.0 * pi * (50.0 + 100.0 * std::exp (-t / 0.03)) / rate;
                v = (float) (0.9 * std::exp (-t / 0.05) * std::sin (phase));
            }
            else if (kind == 1)
            {
                v = (float) (0.6 * std::exp (-t / 0.04)) * white();
            }
            else
            {
                v = (float) (0.25 * std::exp (-t / 0.015)) * white();
            }

            x[start + j] += v;
        }
    }

    return x;
}

static void testSlicesAndOnsets()
{
    std::printf ("Fatias (região) e ataques\n");

    constexpr double rate = 48000.0;

    {
        // Fatia de 0,25 a 0,50 de um seno de 1 s: começa no começo dela, acaba no fim dela, sem estalo.
        const auto s = makeSine (rate, 60, 1.0);
        const auto& src = s.channels[0];
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.attackMs = 0.0f;
        p.regionStart = 0.25f;
        p.regionEnd = 0.5f;
        p.loop = true;          // a fatia ignora o laço
        p.loopStart = 0.3f;
        p.loopEnd = 0.4f;
        voice.noteOn (60, 1.0f, false, p);
        const auto out = renderVoice (voice, s, rate, 0.4, [] (std::size_t) {});
        const auto first = (std::size_t) std::lround (0.25 * (double) (src.size() - 1));
        int lastSound = -1;
        for (std::size_t i = 0; i < out.size(); ++i) if (out[i] != 0.0f) lastSound = (int) i;
        const double length = (lastSound + 1) / rate;
        bool finite = true;
        for (float v : out) finite = finite && std::isfinite (v);

        CHECK (std::abs (out[1] - src[first + 1]) < 6.0e-3f, "a fatia começa no começo dela (%.4f, fonte %.4f; 0,25 amostra antes)", out[1], src[first + 1]);
        CHECK (std::abs (length - 0.25) < 0.001 && ! voice.isActive(), "e acaba no fim dela, sem laço: %.4f s (0,2500)", length);
        const float natural = (float) (0.5 * 2.0 * pi * keyToHz (60) / rate);   // o maior salto do próprio seno
        CHECK (finite && maxJump (out) <= natural * 1.02f, "sai pelo fade de 4 ms, sem estalo (maior salto %.4f, o do seno %.4f)", maxJump (out), natural);

        // Uma oitava acima, a mesma fatia dura a metade e o fade ainda chega a zero no fim.
        SampleVoice up;
        up.prepare (rate);
        up.noteOn (72, 1.0f, false, p);
        const auto outUp = renderVoice (up, s, rate, 0.3, [] (std::size_t) {});
        int lastUp = -1;
        for (std::size_t i = 0; i < outUp.size(); ++i) if (outUp[i] != 0.0f) lastUp = (int) i;
        const float tail = std::abs (outUp[(std::size_t) std::max (0, lastUp)]);
        CHECK (std::abs ((lastUp + 1) / rate - 0.125) < 0.001 && tail < 0.01f, "uma oitava acima: %.4f s (0,1250), última amostra %.4f",
               (lastUp + 1) / rate, tail);
    }

    {
        std::vector<double> hits;
        const auto beat = makeTestBeat (rate, hits);
        const auto medium = detectOnsets (beat.data(), beat.size(), rate, Sensitivity::medium);
        const auto dull = detectOnsets (beat.data(), beat.size(), rate, Sensitivity::dull);
        const auto sharp = detectOnsets (beat.data(), beat.size(), rate, Sensitivity::sharp);

        // A batida do começo é o começo da primeira fatia: os ataques são as outras 15.
        double worst = 0.0;
        bool all = medium.size() == hits.size() - 1;

        for (std::size_t i = 0; all && i < medium.size(); ++i)
            worst = std::max (worst, std::abs (medium[i] - hits[i + 1]) / rate * 1000.0);

        CHECK (all && worst <= 3.0, "beat de 16 batidas: %zu ataques depois do começo (médio), o mais longe a %.2f ms da batida", medium.size(), worst);
        CHECK (dull.size() <= medium.size() && medium.size() <= sharp.size(), "suave <= médio <= forte: %zu, %zu, %zu", dull.size(), medium.size(), sharp.size());

        const auto slices = sliceStarts (medium, beat.size(), 0.02 * rate);
        CHECK (slices.size() == 16 && slices.front() == 0.0f && std::is_sorted (slices.begin(), slices.end()),
               "16 fatias em ordem, a primeira no começo");

        std::vector<float> silence ((std::size_t) rate, 0.0f);
        CHECK (detectOnsets (silence.data(), silence.size(), rate, Sensitivity::sharp).empty(), "silêncio não tem ataque");

        const auto tone = makeSine (rate, 45, 1.0);
        CHECK (detectOnsets (tone.channels[0].data(), tone.channels[0].size(), rate, Sensitivity::sharp).empty(),
               "um tom constante não tem ataque");
        CHECK (detectOnsets (nullptr, 0, rate, Sensitivity::medium).empty() && detectOnsets (beat.data(), 10, rate, Sensitivity::medium).empty(),
               "entrada vazia ou curta não quebra");
    }
}

// Fatia 5 (revisão de 22/10): aliasing ao subir o tom, rampa da força no slide e a voz roubada.

static SampleData makeTone (double rate, double hz, double seconds, int rootKey = 60, float amp = 0.5f)
{
    SampleData s;
    s.sampleRate = rate;
    s.rootKey = rootKey;
    s.channels.assign (1, std::vector<float> ((std::size_t) (seconds * rate)));

    for (std::size_t i = 0; i < s.channels[0].size(); ++i)
        s.channels[0][i] = (float) (amp * std::sin (2.0 * pi * hz * (double) i / rate));

    return s;
}

static double rmsOf (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    to = std::min (to, x.size());
    for (std::size_t i = from; i < to; ++i) sum += (double) x[i] * x[i];
    return to > from ? std::sqrt (sum / (double) (to - from)) : 0.0;
}

static double dbOf (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

/** O que o Hermite sozinho dava nessa razão (a régua do "antes"): lê o tom inteiro no passo `r`. */
static std::vector<float> hermiteOnly (const SampleData& s, double r, std::size_t n)
{
    std::vector<float> out (n, 0.0f);
    const auto& x = s.channels[0];
    const auto len = (std::int64_t) x.size();
    double pos = 0.0;

    for (std::size_t i = 0; i < n && (std::int64_t) pos + 2 < len; ++i, pos += r)
    {
        const auto i0 = (std::int64_t) pos;
        const float f = (float) (pos - (double) i0);
        const float xm1 = x[(std::size_t) std::max<std::int64_t> (0, i0 - 1)], x0 = x[(std::size_t) i0], x1 = x[(std::size_t) i0 + 1], x2 = x[(std::size_t) i0 + 2];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        out[i] = ((c3 * f + c2) * f + c1) * f + x0;
    }

    return out;
}

static std::vector<float> playHeld (const SampleData& s, double outRate, int key, double seconds, float attackMs = 0.0f)
{
    SampleVoice voice;
    voice.prepare (outRate);
    VoiceParams p;
    p.attackMs = attackMs;
    voice.noteOn (key, 1.0f, false, p);
    return renderVoice (voice, s, outRate, seconds, [] (std::size_t) {});
}

static void testAntiAliasing()
{
    std::printf ("Sem aliasing ao subir o tom (mipmaps e sinc janelada)\n");

    constexpr double rate = 48000.0;
    const std::size_t from = (std::size_t) (0.05 * rate), to = (std::size_t) (0.45 * rate);
    const double full = 0.5 / std::sqrt (2.0);   // RMS do seno de amplitude 0,5

    {
        // Um chimbal sete semitons acima: o agudo de 18 kHz vai a 27 kHz, acima do Nyquist (24 kHz). Tem que sumir.
        auto s = makeTone (rate, 18000.0, 1.0);
        buildMipmaps (s);
        const double r = std::exp2 (7.0 / 12.0);
        const double after = dbOf (rmsOf (playHeld (s, rate, 67, 0.5), from, to) / full);
        const double before = dbOf (rmsOf (hermiteOnly (s, r, to), from, to) / full);
        CHECK (after < -60.0, "18 kHz sete semitons acima (27 kHz, acima do Nyquist): sobra %.1f dB (só o Hermite: %.1f dB)", after, before);
        CHECK (before > after + 30.0, "o aliasing caiu %.1f dB", before - after);
    }
    {
        // O que cabe passa: 6 kHz sete semitons acima vira 8,99 kHz com o mesmo volume.
        auto s = makeTone (rate, 6000.0, 1.0);
        buildMipmaps (s);
        const auto out = playHeld (s, rate, 67, 0.5);
        const double level = dbOf (rmsOf (out, from, to) / full);
        const double hz = measureHz (out, from, to, rate);
        CHECK (std::abs (level) < 0.3 && std::abs (hz - 6000.0 * std::exp2 (7.0 / 12.0)) < 2.0,
               "6 kHz sete semitons acima: %.1f Hz com %.2f dB (o que cabe abaixo do Nyquist passa)", hz, level);
    }
    {
        // Um sample de 96 kHz numa saída de 48 (razão 2 no tom original): 30 kHz some, 10 kHz fica.
        auto hi = makeTone (96000.0, 30000.0, 1.0);
        auto lo = makeTone (96000.0, 10000.0, 1.0);
        buildMipmaps (hi);
        buildMipmaps (lo);
        const double gone = dbOf (rmsOf (playHeld (hi, rate, 60, 0.5), from, to) / full);
        const double kept = dbOf (rmsOf (playHeld (lo, rate, 60, 0.5), from, to) / full);
        const double before = dbOf (rmsOf (hermiteOnly (hi, 2.0, to), from, to) / full);
        CHECK (gone < -60.0 && std::abs (kept) < 0.3, "96 kHz tocado a 48: 30 kHz sobra %.1f dB (Hermite: %.1f dB), 10 kHz fica em %.2f dB",
               gone, before, kept);
    }
    {
        // O Bell duas oitavas acima (razão 4, nível 2): 8 kHz iria a 32 kHz e some; 4 kHz vai a 16 kHz e fica.
        auto hi = makeTone (rate, 8000.0, 2.0, 60);
        auto lo = makeTone (rate, 4000.0, 2.0, 60);
        buildMipmaps (hi);
        buildMipmaps (lo);
        const double gone = dbOf (rmsOf (playHeld (hi, rate, 84, 0.5), from, to) / full);
        const auto keptOut = playHeld (lo, rate, 84, 0.5);
        const double kept = dbOf (rmsOf (keptOut, from, to) / full);
        CHECK (gone < -60.0 && std::abs (kept) < 0.5 && std::abs (measureHz (keptOut, from, to, rate) - 16000.0) < 5.0,
               "duas oitavas acima: 8 kHz (32 kHz) sobra %.1f dB; 4 kHz vira 16 kHz com %.2f dB", gone, kept);
    }
    {
        // No tom original nada muda: os níveis existem, mas a leitura continua o Hermite, bit a bit.
        auto with = makeSine (rate, 48, 0.5);
        auto without = with;
        buildMipmaps (with);
        CHECK (with.mips.size() == 4 && playHeld (with, rate, 48, 0.4) == playHeld (without, rate, 48, 0.4) && playHeld (with, rate, 41, 0.4) == playHeld (without, rate, 41, 0.4),
               "no tom original e abaixo: o mesmo som, bit a bit, com ou sem os níveis (%zu níveis)", with.mips.size());

        SampleData longOne;
        longOne.sampleRate = rate;
        longOne.channels.assign (2, std::vector<float> ((std::size_t) (31.0 * rate), 0.0f));
        buildMipmaps (longOne);
        CHECK (longOne.mips.empty(), "sample de 31 s fica sem níveis (a memória quase dobraria)");
    }
    {
        // Um glide que atravessa o tom original (razão 1) não estala na troca do Hermite para a sinc.
        auto s = makeSine (rate, 48, 2.0);
        buildMipmaps (s);
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.glideMs = 300.0f;
        voice.noteOn (46, 1.0f, false, p);
        const auto out = renderVoice (voice, s, rate, 1.2, [&] (std::size_t pos) { if (pos == 256 * 40) voice.noteOn (51, 1.0f, true, p); });
        const float natural = (float) (0.5 * 2.0 * pi * keyToHz (51) / rate);
        const float jump = maxJumpIn (out, 256 * 40 - 480, out.size());   // depois do ataque da primeira nota
        CHECK (jump <= natural * 1.02f, "glide de A#2 a D#3 passando pelo tom original: maior salto %.5f (régua %.5f)", jump, natural);
    }
    {
        // O custo: uma voz sete semitons acima, 10 s de áudio.
        auto s = makeTone (rate, 1000.0, 12.0);
        buildMipmaps (s);
        SampleVoice voice;
        voice.prepare (rate);
        voice.noteOn (67, 1.0f, false, VoiceParams {});
        std::vector<float> out (512);
        float* chans[] = { out.data() };
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < (int) (10.0 * rate / 512); ++b) { std::fill (out.begin(), out.end(), 0.0f); voice.render (s, chans, 1, 512); }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        CHECK (ms < 1000.0, "custo: 10 s de uma voz acima do tom em %.1f ms (%.2f%% de um núcleo)", ms, ms / 100.0);
    }
}

static void testSlideVelocityAndSteal()
{
    std::printf ("Força no slide e voz roubada, sem estalo\n");

    constexpr double rate = 48000.0;

    {
        // Slide de força 1 para força 0,3: o volume chega numa rampa de 5 ms, sem degrau.
        const auto s = makeSine (rate, 36, 2.0);
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.glideMs = 80.0f;
        voice.noteOn (36, 1.0f, false, p);
        const std::size_t at = 256 * 40;
        const auto out = renderVoice (voice, s, rate, 0.6, [&] (std::size_t pos) { if (pos == at) voice.noteOn (38, 0.3f, true, p); });
        const float natural = (float) (0.5 * 2.0 * pi * keyToHz (38) / rate);
        const float before = peakIn (out, at - 4800, at), later = peakIn (out, at + 2400, at + 7200);
        const float jump = maxJumpIn (out, at - 480, at + 9600);   // em volta do slide (o ataque da nota fica de fora)
        CHECK (jump <= natural * 1.1f, "slide de força 1 para 0,3: maior salto %.5f (o do seno %.5f)", jump, natural);
        CHECK (std::abs (later / before - 0.3f) < 0.02f, "e o volume chega a 0,3 do de antes (%.3f)", later / before);
    }
    {
        // Voz roubada: a mesma voz recebe uma nota nova no meio do seno; o som velho sai em 3 ms.
        const auto s = makeSine (rate, 55, 2.0);
        SampleVoice voice;
        voice.prepare (rate);
        VoiceParams p;
        p.attackMs = 1.0f;
        voice.noteOn (55, 1.0f, false, p);
        const std::size_t at = 256 * 37 + 0;   // um ponto qualquer do ciclo
        const auto out = renderVoice (voice, s, rate, 0.3, [&] (std::size_t pos) { if (pos == at) voice.noteOn (55, 1.0f, false, p); });
        const float natural = (float) (0.5 * 2.0 * pi * keyToHz (55) / rate);
        CHECK (maxJump (out) <= natural * 2.05f, "voz roubada no meio do ciclo: maior salto %.5f (o do seno %.5f; velho saindo + novo entrando)", maxJump (out), natural);
        bool finite = true;
        for (float v : out) finite = finite && std::isfinite (v);
        CHECK (finite && voice.isActive(), "e a nota nova segue tocando");
    }
}

static void testSlotAcrossThreads()
{
    std::printf ("Troca de objetos entre a thread de mensagem e a de áudio\n");

    {
        RtObjectSlot<Counted> slot;
        std::atomic<bool> running { true };
        std::atomic<int> lastSeen { -1 };
        std::atomic<bool> wentBackwards { false };

        std::thread audio ([&]
        {
            int previous = -1;

            while (running.load())
            {
                if (const auto* c = slot.current())
                {
                    if (c->value < previous) wentBackwards = true;
                    previous = c->value;
                    lastSeen = c->value;
                }
            }
        });

        constexpr int total = 5000;
        int published = 0;

        while (published < total)
        {
            std::unique_ptr<const Counted> next = std::make_unique<Counted> (published);

            while (! slot.publish (next))
                std::this_thread::yield();

            ++published;
        }

        while (lastSeen.load() != total - 1)
            std::this_thread::yield();

        running = false;
        audio.join();
        slot.collectGarbage();

        CHECK (! wentBackwards.load(), "a thread de áudio nunca viu um objeto mais antigo depois de um mais novo");
        CHECK (lastSeen.load() == total - 1, "a thread de áudio chegou ao último objeto publicado (%d)", lastSeen.load());
        CHECK (Counted::alive.load() == 1, "só o objeto ativo continua vivo antes de destruir o slot (%d)", Counted::alive.load());
    }

    CHECK (Counted::alive.load() == 0, "nenhum vazamento depois de destruir o slot (%d vivos)", Counted::alive.load());
}

//==============================================================================
// WHYNOT Piano (21/10): o começo do sample na memória e o resto por um anel que uma thread enche.

// Uma zona sintética: senoide estéreo (esquerda e direita com fases diferentes) de `frames` quadros.
static PianoZone sineZone (double hz, int root, std::int64_t frames, std::int64_t headFrames, std::vector<float>& full)
{
    PianoZone z;
    z.rootKey = root;
    z.loKey = 0;
    z.hiKey = 127;
    z.sampleRate = 44100.0;
    z.totalFrames = frames;
    z.velTrack = 0.0f;   // volume cheio em qualquer força
    full.assign ((std::size_t) frames * 2, 0.0f);

    for (std::int64_t i = 0; i < frames; ++i)
    {
        full[(std::size_t) i * 2] = (float) (0.5 * std::sin (2.0 * pi * hz * (double) i / 44100.0));
        full[(std::size_t) i * 2 + 1] = (float) (0.5 * std::cos (2.0 * pi * hz * (double) i / 44100.0));
    }

    // o começo fica em 16 bits, como no app; o resto vem do "arquivo" (full), em float
    z.head.resize ((std::size_t) std::min (frames, headFrames) * 2);

    for (std::size_t i = 0; i < z.head.size(); ++i)
        z.head[i] = (std::int16_t) std::lround (full[i] * 32767.0f);

    return z;
}

// Uma passada do streamer, como a do app: planFill/finishFill copiando do "arquivo".
static void serviceStream (VoiceStream& stream, const PianoSet& set, StreamCursor& cursor, const std::vector<std::vector<float>>& files)
{
    int zone = -1;
    std::int64_t from = 0, to = 0;

    while (planFill (stream, set, cursor, 4096, zone, from, to))
    {
        for (auto f = from; f < to; ++f)
        {
            auto* d = stream.slot (f);
            d[0] = files[(std::size_t) zone][(std::size_t) f * 2];
            d[1] = files[(std::size_t) zone][(std::size_t) f * 2 + 1];
        }

        finishFill (stream, cursor, to);
    }
}

static void testPianoStreaming()
{
    std::printf ("WHYNOT Piano: começo na memória e o resto pelo anel\n");

    std::vector<std::vector<float>> files (3);
    PianoSet set;
    set.zones.push_back (sineZone (440.0, 69, 3 * 44100, 16384, files[0]));   // 3 s, o começo com 0,37 s
    set.zones.push_back (sineZone (220.0, 57, 2 * 44100, 4096, files[1]));    // começo curto: o anel entra cedo
    set.zones.push_back (sineZone (110.0, 45, 44100, 4096, files[2]));        // um release só da tecla 60, sem keytrack
    set.zones[2].release = true;
    set.zones[2].loKey = set.zones[2].hiKey = 60;
    set.zones[2].keyTrack = 0.0f;
    set.buildTables();

    CHECK (set.attackFor (69, 100) == 0 && set.attackFor (0, 1) == 0 && set.attackFor (128, 100) == -1,
           "a tabela acha a zona de ataque pela tecla e pela força");
    CHECK (set.releases[60][0] == 2 && set.releases[60][1] == -1 && set.releases[61][0] == -1,
           "a tabela de releases guarda só as teclas da zona");

    // 1) streamer numa thread de verdade, a voz noutra, no tom original e na mesma taxa: a saída é o sample
    {
        VoiceStream stream;
        PianoVoice voice;
        voice.prepare (44100.0);
        std::atomic<bool> running { true };
        StreamCursor cursor;

        std::thread streamer ([&]
        {
            while (running.load())
            {
                serviceStream (stream, set, cursor, files);
                std::this_thread::sleep_for (std::chrono::microseconds (500));
            }
        });

        voice.start (set, 0, 69, 1.0f, 0.0f, 1.0f, stream, 1);
        std::vector<float> left ((std::size_t) 3 * 44100, 0.0f), right (left.size(), 0.0f);
        std::uint32_t underruns = 0;
        const int block = 256;

        for (std::size_t pos = 0; pos + block <= left.size() && voice.isActive(); pos += block)
        {
            float* out[] = { left.data() + pos, right.data() + pos };
            voice.render (set, stream, out, 2, block, underruns);
            std::this_thread::sleep_for (std::chrono::microseconds (200));   // bem mais rápido que o tempo real
        }

        running = false;
        streamer.join();

        // depois da entrada de 1 ms: o começo (16 bits) e o resto (anel) batem com o sample
        float worstHead = 0.0f, worstStream = 0.0f;

        for (std::size_t i = 200; i < 16384; ++i)
            worstHead = std::max (worstHead, std::abs (left[i] - files[0][i * 2]));

        for (std::size_t i = 16384; i < 2 * 44100; ++i)
            worstStream = std::max (worstStream, std::max (std::abs (left[i] - files[0][i * 2]), std::abs (right[i] - files[0][i * 2 + 1])));

        CHECK (underruns == 0, "o anel nunca faltou com o streamer numa thread ao lado (%u faltas)", underruns);
        CHECK (worstHead < 1.0e-4f, "o começo em 16 bits sai igual ao sample (erro máx. %.2g)", (double) worstHead);
        CHECK (worstStream < 1.0e-6f, "o resto, vindo do anel, sai igual ao sample nos dois lados (erro máx. %.2g)", (double) worstStream);
    }

    // 2) sem streamer: depois do começo falta dado; a voz toca silêncio, conta as faltas e não trava
    {
        VoiceStream stream;
        PianoVoice voice;
        voice.prepare (44100.0);
        voice.start (set, 0, 69, 1.0f, 0.0f, 1.0f, stream, 7);
        std::vector<float> left (44100, 0.0f), right (44100, 0.0f);
        float* out[] = { left.data(), right.data() };
        std::uint32_t underruns = 0;
        voice.render (set, stream, out, 2, 44100, underruns);

        float lateMax = 0.0f;
        for (std::size_t i = 20000; i < 44100; ++i)
            lateMax = std::max (lateMax, std::abs (left[i]));

        CHECK (underruns > 20000 && lateMax == 0.0f, "sem o anel, depois do começo sai silêncio e as faltas são contadas (%u)", underruns);
    }

    // 3) uma oitava acima a frequência dobra (o mesmo sample lido mais rápido)
    {
        VoiceStream stream;
        PianoVoice voice;
        StreamCursor cursor;
        voice.prepare (44100.0);
        voice.start (set, 0, 81, 1.0f, 0.0f, 1.0f, stream, 3);
        std::vector<float> left (44100, 0.0f), right (44100, 0.0f);
        std::uint32_t underruns = 0;

        for (std::size_t pos = 0; pos + 512 <= left.size() && voice.isActive(); pos += 512)
        {
            serviceStream (stream, set, cursor, files);
            float* out[] = { left.data() + pos, right.data() + pos };
            voice.render (set, stream, out, 2, 512, underruns);
        }

        const double hz = measureHz (left, 1000, 40000, 44100.0);
        CHECK (std::abs (hz - 880.0) < 1.0 && underruns == 0, "uma oitava acima: %.2f Hz (esperado 880)", hz);
    }

    // 4) soltar: some em -80 dB no tempo pedido; a voz para e o anel também
    {
        VoiceStream stream;
        PianoVoice voice;
        StreamCursor cursor;
        voice.prepare (44100.0);
        voice.start (set, 0, 69, 1.0f, 0.0f, 1.0f, stream, 4);
        std::vector<float> left (44100, 0.0f), right (44100, 0.0f);
        std::uint32_t underruns = 0;
        int stoppedAt = -1;

        for (int pos = 0; pos + 64 <= 44100; pos += 64)
        {
            if (pos == 4416)
                voice.release (0.1f);   // até -60 dB em 0,1 s: -80 dB em ~0,133 s

            serviceStream (stream, set, cursor, files);
            float* out[] = { left.data() + pos, right.data() + pos };
            voice.render (set, stream, out, 2, 64, underruns);

            if (! voice.isActive() && stoppedAt < 0)
                stoppedAt = pos;
        }

        const double after = (stoppedAt - 4416) / 44100.0;
        CHECK (stoppedAt > 0 && after > 0.11 && after < 0.16 && stream.currentJob() == 0,
               "soltar com 0,1 s: a voz acaba %.3f s depois e libera o anel", after);
    }

    // 5) a mesma voz recomeça com outra zona: os dados velhos do anel não vazam para a nota nova
    {
        VoiceStream stream;
        PianoVoice voice;
        StreamCursor cursor;
        voice.prepare (44100.0);
        voice.start (set, 0, 69, 1.0f, 0.0f, 1.0f, stream, 10);
        std::vector<float> scratchL (32768, 0.0f), scratchR (32768, 0.0f);
        std::uint32_t underruns = 0;

        for (int pos = 0; pos + 512 <= 32768; pos += 512)
        {
            serviceStream (stream, set, cursor, files);
            float* out[] = { scratchL.data() + pos, scratchR.data() + pos };
            voice.render (set, stream, out, 2, 512, underruns);
        }

        // a zona 1 tem só 4096 quadros de começo, e o anel ainda guarda a zona 0 até bem depois disso
        voice.start (set, 1, 57, 1.0f, 0.0f, 1.0f, stream, 11);
        std::vector<float> left (44100, 0.0f), right (44100, 0.0f);
        underruns = 0;

        for (int pos = 0; pos + 512 <= 44100; pos += 512)
        {
            if (pos >= 8192)   // as primeiras 16 voltas sem o streamer: falta dado depois do começo
                serviceStream (stream, set, cursor, files);

            float* out[] = { left.data() + pos, right.data() + pos };
            voice.render (set, stream, out, 2, 512, underruns);
        }

        float worstHead = 0.0f, staleMax = 0.0f, worstAfter = 0.0f;

        for (std::size_t i = 200; i < 4090; ++i)
            worstHead = std::max (worstHead, std::abs (left[i] - files[1][i * 2]));

        for (std::size_t i = 4100; i < 8192; ++i)
            staleMax = std::max (staleMax, std::abs (left[i]));

        for (std::size_t i = 8704; i < 44032; ++i)   // da segunda volta com o streamer até o último bloco
            worstAfter = std::max (worstAfter, std::abs (left[i] - files[1][i * 2]));

        CHECK (worstHead < 1.0e-4f, "a nota nova começa com o sample dela (erro máx. %.2g)", (double) worstHead);
        CHECK (staleMax == 0.0f && underruns > 3000,
               "sem o streamer, o resto da nota anterior não vaza: sai silêncio (pico %.2g, %u faltas)", (double) staleMax, underruns);
        CHECK (worstAfter < 1.0e-6f, "com o streamer de volta, o anel segue da posição da voz (erro máx. %.2g)", (double) worstAfter);
    }

    // 6) release sem keytrack: toca no tom gravado em qualquer tecla da zona
    {
        VoiceStream stream;
        PianoVoice voice;
        StreamCursor cursor;
        voice.prepare (44100.0);
        voice.start (set, set.releases[60][0], 60, 1.0f, 0.0f, 1.0f, stream, 20);
        std::vector<float> left (40000, 0.0f), right (40000, 0.0f);
        std::uint32_t underruns = 0;

        for (int pos = 0; pos + 500 <= 40000; pos += 500)
        {
            serviceStream (stream, set, cursor, files);
            float* out[] = { left.data() + pos, right.data() + pos };
            voice.render (set, stream, out, 2, 500, underruns);
        }

        const double hz = measureHz (left, 1000, 40000, 44100.0);
        CHECK (std::abs (hz - 110.0) < 0.5 && underruns == 0, "release na tecla 60 sem keytrack: %.2f Hz (gravado em 110)", hz);
    }

    // 7) a 48 kHz o sample de 44,1 kHz sai no mesmo tom
    {
        VoiceStream stream;
        PianoVoice voice;
        StreamCursor cursor;
        voice.prepare (48000.0);
        voice.start (set, 0, 69, 1.0f, 0.0f, 1.0f, stream, 30);
        std::vector<float> left (48000, 0.0f), right (48000, 0.0f);
        std::uint32_t underruns = 0;

        for (int pos = 0; pos + 480 <= 48000; pos += 480)
        {
            serviceStream (stream, set, cursor, files);
            float* out[] = { left.data() + pos, right.data() + pos };
            voice.render (set, stream, out, 2, 480, underruns);
        }

        const double hz = measureHz (left, 1000, 47000, 48000.0);
        const double snr = sineSnrDb (left, 1000, 47000, 440.0, 48000.0);
        CHECK (std::abs (hz - 440.0) < 0.5 && snr > 60.0 && underruns == 0,
               "a 48 kHz: %.2f Hz (esperado 440), sinal/ruído %.1f dB", hz, snr);
    }
}

int main (int argc, char* argv[])
{
    testGlide (argc > 1 ? argv[1] : nullptr);
    testInvalidParams();
    testLongGlideIsSmooth();
    testTuneAndPitchEnvelope();
    testDecaySustain();
    testStartOffset();
    testInterpolationQuality();
    testChokeFade();
    testLoopPoints();
    testSlicesAndOnsets();
    testAntiAliasing();
    testSlideVelocityAndSteal();
    testSlotAcrossThreads();
    testPianoStreaming();

    std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d TESTE(S) FALHARAM\n", failures);
    return failures == 0 ? 0 : 1;
}
