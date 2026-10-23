// Testes do WHYNOT Synth (sem JUCE): parâmetros, tabelas, aliasing, envelopes, roubo de voz sem
// clique, glide e legato, NaN/desnormais, nível dos presets e orçamento de CPU.
//
//   whynot_synth_tests                      roda tudo
//   whynot_synth_tests --levels <arquivo>   mede os presets e grava o ganho de cada um (calibração)
//   whynot_synth_tests --cpu                CPU de cada preset com polifonia cheia (48 kHz, blocos de 256)

#ifdef _WIN32
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

#include "../../src/synth/Synth.h"
#include "../../src/synth/SynthMeasure.h"
#include "../../src/synth/SynthPresets.h"
#include "../../src/dsp/LoudnessMeter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace whynot;
using namespace whynot::synth;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

constexpr double rate = 48000.0;
constexpr double pi = 3.14159265358979323846;

struct Audio
{
    std::vector<float> l, r;
    size_t size() const   { return l.size(); }
};

struct NoteSpec
{
    double start, length;
    int key;
    float velocity = 0.8f;
};

static void setp (Synth& s, const char* id, float value)
{
    const int i = indexOf (id);

    if (i < 0)
    {
        std::printf ("  parâmetro desconhecido no teste: %s\n", id);
        ++failures;
        return;
    }

    s.setParam (i, value);
}

/** Toca as notas e devolve o áudio, em blocos de `block` amostras com eventos no ponto exato. */
static Audio render (Synth& s, const std::vector<NoteSpec>& notes, double seconds, int block = 256)
{
    struct Ev { long long at; Event e; };
    std::vector<Ev> evs;

    for (auto& n : notes)
    {
        evs.push_back ({ (long long) std::llround (n.start * rate), { 0, Event::Type::noteOn, n.key, n.velocity } });
        evs.push_back ({ (long long) std::llround ((n.start + n.length) * rate), { 0, Event::Type::noteOff, n.key, 0.0f } });
    }

    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.at < b.at; });

    const size_t total = (size_t) std::llround (seconds * rate);
    Audio a;
    a.l.assign (total, 0.0f);
    a.r.assign (total, 0.0f);
    std::vector<Event> blockEvents;
    size_t next = 0;

    for (size_t start = 0; start < total; start += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, total - start);
        blockEvents.clear();

        while (next < evs.size() && evs[next].at < (long long) (start + (size_t) n))
        {
            auto e = evs[next].e;
            e.sampleOffset = (int) std::max<long long> (0, evs[next].at - (long long) start);
            blockEvents.push_back (e);
            ++next;
        }

        s.process (a.l.data() + start, a.r.data() + start, n, blockEvents.data(), (int) blockEvents.size());
    }

    return a;
}

static double rms (const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min (to, x.size());
    double sum = 0.0;

    for (size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];

    return to > from ? std::sqrt (sum / (double) (to - from)) : 0.0;
}

static double peak (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    to = to == 0 ? x.size() : std::min (to, x.size());
    double p = 0.0;

    for (size_t i = from; i < to; ++i)
        p = std::max (p, (double) std::abs (x[i]));

    return p;
}

static double db (double g)   { return 20.0 * std::log10 (std::max (1.0e-12, g)); }

static size_t at (double seconds)   { return (size_t) std::llround (seconds * rate); }

/** Frequência média por cruzamentos de zero subindo entre t0 e t1 segundos. */
static double measureHz (const std::vector<float>& x, double t0, double t1)
{
    const size_t from = std::max<size_t> (1, at (t0)), to = std::min (x.size(), at (t1));
    long first = -1, last = -1;
    int crossings = 0;

    for (size_t i = from; i < to; ++i)
    {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            // posição fracionária do cruzamento
            const double frac = x[i - 1] / (double) (x[i - 1] - x[i]);
            const long pos = (long) i;
            (void) frac;

            if (crossings == 0) first = pos;
            last = pos;
            ++crossings;
        }
    }

    return crossings < 2 ? 0.0 : (crossings - 1) * rate / (double) (last - first);
}

static bool allFinite (const Audio& a)
{
    for (size_t i = 0; i < a.size(); ++i)
        if (! std::isfinite (a.l[i]) || ! std::isfinite (a.r[i]))
            return false;

    return true;
}

static int countDenormals (const Audio& a)
{
    int n = 0;

    for (size_t i = 0; i < a.size(); ++i)
        for (float x : { a.l[i], a.r[i] })
            if (x != 0.0f && std::abs (x) < 1.17549435e-38f)
                ++n;

    return n;
}

//==============================================================================
// FFT para medir aliasing

static void fft (std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (x[i], x[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * pi / (double) len;
        const std::complex<double> wlen (std::cos (angle), std::sin (angle));

        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);

            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

/** Energia fora dos harmônicos de f0 (aliasing e ruído) em relação aos harmônicos, em dB (até 20 kHz). */
static double aliasRatioDb (const std::vector<float>& x, size_t from, double f0)
{
    constexpr size_t n = 65536;
    std::vector<std::complex<double>> buf (n);

    for (size_t i = 0; i < n; ++i)
    {
        // Blackman-Harris de 4 termos: lóbulos laterais a -92 dB
        const double t = 2.0 * pi * (double) i / (double) (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        buf[i] = x[from + i] * w;
    }

    fft (buf);
    const double binHz = rate / (double) n;
    std::vector<bool> harmonic (n / 2, false);

    for (int h = 1; h * f0 < rate * 0.5; ++h)
    {
        const double centre = h * f0 / binHz;

        for (long k = (long) centre - 6; k <= (long) centre + 6; ++k)
            if (k >= 0 && k < (long) (n / 2))
                harmonic[(size_t) k] = true;
    }

    double signal = 0.0, alias = 0.0;

    for (size_t k = (size_t) (20.0 / binHz); k < (size_t) (20000.0 / binHz); ++k)
        (harmonic[k] ? signal : alias) += std::norm (buf[k]);

    return 10.0 * std::log10 (std::max (1.0e-30, alias) / std::max (1.0e-30, signal));
}

//==============================================================================
/** Timbre limpo para medir: uma camada, sem filtro, sem deriva, volume sem força. */
static void plainPatch (Synth& s)
{
    s.setAll (defaultValues());
    setp (s, "flt.on", 0);
    setp (s, "drift", 0);
    setp (s, "velAmp", 0);
    setp (s, "l1.level", -6);   // longe do soft clip (que geraria harmônicos acima de Nyquist)
    setp (s, "env1.attack", 0.5f);
    setp (s, "env1.sustain", 100);
    setp (s, "env1.release", 20);
}

static void testParams()
{
    std::printf ("Parametros\n");
    const auto params = allParams();
    CHECK ((int) params.size() == numParams, "%d parâmetros na ordem dos índices", (int) params.size());

    std::set<std::string> ids;
    bool defaultsOk = true, idsOk = true;

    for (int i = 0; i < numParams; ++i)
    {
        const auto& p = params[(size_t) i];
        idsOk &= ids.insert (p.id).second && indexOf (p.id) == i;
        defaultsOk &= p.def >= p.min && p.def <= p.max && p.clamp (p.def) == p.def;

        if (p.kind == ParamKind::choice)
            defaultsOk &= (int) p.choices.size() == (int) std::lround (p.max) + 1;
    }

    CHECK (idsOk, "ids únicos e achados por indexOf");
    CHECK (defaultsOk, "padrões dentro das faixas e listas com o tamanho certo");
    CHECK (indexOf ("l3.fmRatio4") == P::layer (2, lFmRatio4) && indexOf ("env2.decay") == P::env (1, eDecay)
               && indexOf ("mod8.amt") == P::slot (7, mAmt) && indexOf ("rev.mix") == P::revMix && indexOf ("bpm") == P::bpm,
           "índices com nome batem com os ids");

    const auto base = defaultValues();
    auto a = varied (base, 0.5f, 7), b = varied (base, 0.5f, 7), c = varied (base, 0.5f, 8);
    bool structure = true, inRange = true;

    for (int i = 0; i < numParams; ++i)
    {
        const auto& p = params[(size_t) i];
        inRange &= a[(size_t) i] >= p.min && a[(size_t) i] <= p.max;

        if (p.kind != ParamKind::continuous || variationRange (i) == 0.0f)
            structure &= a[(size_t) i] == base[(size_t) i];
    }

    CHECK (a == b && a != c, "Variar: mesma semente = mesmo resultado; outra semente = outro");
    CHECK (structure && inRange, "Variar não mexe em estrutura (tipos, ondas, rotas) e fica nas faixas");
}

static void testWavetables()
{
    std::printf ("Tabelas de onda\n");
    const auto& t = Wavetables::get();
    bool finite = true, bounded = true;

    for (int table = 0; table < Wavetables::numTables(); ++table)
        for (int f = 0; f < Wavetables::framesPerTable; ++f)
            for (int level = 0; level < Wavetables::numLevels; ++level)
            {
                const float* x = t.frame (table, f, level);

                for (int i = 0; i <= Wavetables::tableSize; ++i)
                {
                    finite &= std::isfinite (x[i]);
                    bounded &= std::abs (x[i]) < 1.35f;   // os níveis com menos harmônicos têm um pouco de Gibbs
                }

                finite &= x[Wavetables::tableSize] == x[0];
            }

    CHECK (finite, "%d tabelas x %d quadros x %d níveis, todas finitas e com guarda", Wavetables::numTables(),
           Wavetables::framesPerTable, Wavetables::numLevels);
    CHECK (bounded, "picos perto de 1 em todos os níveis");

    // O nível 5 (32 harmônicos) não tem nada acima do 32º.
    std::vector<std::complex<double>> buf (Wavetables::tableSize);
    const float* x = t.frame (2, 7, 5);   // Sync, último quadro (rico em agudos)

    for (int i = 0; i < Wavetables::tableSize; ++i)
        buf[(size_t) i] = x[i];

    fft (buf);
    double inside = 0.0, outside = 0.0;

    for (int k = 1; k < Wavetables::tableSize / 2; ++k)
        (k <= 32 ? inside : outside) += std::norm (buf[(size_t) k]);

    CHECK (outside < inside * 1.0e-10, "mipmap: nível de 32 harmônicos não tem energia acima deles (%.1f dB)", 10 * std::log10 (outside / inside + 1e-30));
    CHECK (Wavetables::levelFor (20.0f / 48000.0f) == 0 && Wavetables::levelFor (440.0f / 48000.0f) == 5
               && Wavetables::levelFor (4000.0f / 48000.0f) == 8,
           "nível pela altura da nota: 20 Hz = 1024 harmônicos, Lá 440 = 32, 4 kHz = 4 (todos abaixo de 22,6 kHz)");
}

static void testAliasing()
{
    std::printf ("Aliasing\n");
    Synth s;
    s.prepare (rate, 256);

    struct Case { const char* name; int type, wave; float shape; int table; };
    const Case cases[] = {
        { "serra analógica", 0, 0, 50, 0 }, { "pulso 25% analógico", 0, 1, 25, 0 }, { "triângulo analógico", 0, 2, 50, 0 },
        { "wavetable Clássica (serra)", 1, 0, 40, 0 }, { "wavetable Sync", 1, 0, 100, 2 }, { "wavetable Digital", 1, 0, 100, 13 },
    };

    for (int key : { 84, 96 })
    {
        const double f0 = 440.0 * std::pow (2.0, (key - 69) / 12.0);

        for (auto& c : cases)
        {
            plainPatch (s);
            setp (s, "l1.type", (float) c.type);
            setp (s, "l1.wave", (float) c.wave);
            setp (s, "l1.shape", c.shape);
            setp (s, "l1.table", (float) c.table);
            s.reset();
            const auto a = render (s, { { 0.0, 1.8, key } }, 1.8);
            const double ratio = aliasRatioDb (a.l, at (0.2), f0);
            // Todas as ondas de banda limitada por oitava: o que sobra é ruído de interpolação.
            const double limit = -70.0;
            CHECK (ratio < limit, "%s em %.0f Hz: aliasing %.1f dB (limite %.0f)", c.name, f0, ratio, limit);
        }
    }

    // FM: índice alto numa nota aguda não dobra (o índice cai perto de Nyquist).
    plainPatch (s);
    setp (s, "l1.type", 2);
    setp (s, "l1.fmAmt2", 100);
    setp (s, "l1.fmSustain", 100);
    setp (s, "l1.fmRatio2", 3);
    s.reset();
    const auto fm = render (s, { { 0.0, 1.8, 100 } }, 1.8);
    const double fmRatio = aliasRatioDb (fm.l, at (0.2), 440.0 * std::pow (2.0, 31 / 12.0));
    CHECK (fmRatio < -50.0, "FM com índice máximo em Mi7: o índice cai perto de Nyquist (aliasing %.1f dB)", fmRatio);

    // Sub: as mesmas ondas de banda limitada.
    plainPatch (s);
    setp (s, "l1.on", 0);
    setp (s, "sub.on", 1);
    setp (s, "sub.wave", 2);
    s.reset();
    const auto sub = render (s, { { 0.0, 1.8, 60 } }, 1.8);
    const double subRatio = aliasRatioDb (sub.l, at (0.2), 440.0 * std::pow (2.0, -9 / 12.0) * 0.5);
    CHECK (subRatio < -70.0, "sub quadrado uma oitava abaixo de Dó4: aliasing %.1f dB", subRatio);
}

static void testEnvelopes()
{
    std::printf ("Envelopes\n");
    Synth s;
    s.prepare (rate, 256);
    plainPatch (s);
    setp (s, "l1.wave", 3);   // seno
    setp (s, "env1.attack", 200);
    setp (s, "env1.decay", 300);
    setp (s, "env1.sustain", 50);
    setp (s, "env1.release", 400);
    setp (s, "env1.curve", 0);   // reta: tempos fáceis de conferir

    const auto a = render (s, { { 0.0, 1.0, 69 } }, 2.0);
    auto level = [&a] (double t) { return rms (a.l, at (t - 0.005), at (t + 0.005)) * std::sqrt (2.0); };
    const double top = level (0.2);

    CHECK (level (0.1) > top * 0.4 && level (0.1) < top * 0.6, "ataque reto de 200 ms: metade em 100 ms (%.2f de %.2f)", level (0.1), top);
    CHECK (std::abs (level (0.7) / top - 0.5) < 0.06, "sustentação em 50%% (%.2f)", level (0.7) / top);
    CHECK (level (1.2) < level (0.9) * 0.6 && level (1.45) < top * 0.03, "soltura de 400 ms: some depois de soltar");
    CHECK (s.activeVoices() == 0, "voz liberada quando o envelope termina");

    // Curva exponencial: no meio do decaimento já caiu mais que na reta.
    setp (s, "env1.curve", 100);
    setp (s, "env1.attack", 0.5f);
    setp (s, "env1.sustain", 0);
    setp (s, "env1.decay", 500);
    s.reset();
    const auto b = render (s, { { 0.0, 1.0, 69 } }, 1.0);
    auto lb = [&b] (double t) { return rms (b.l, at (t - 0.005), at (t + 0.005)) * std::sqrt (2.0); };
    CHECK (lb (0.25) < lb (0.02) * 0.35, "curva 100%%: decaimento exponencial (%.2f em 250 ms)", lb (0.25) / lb (0.02));
    CHECK (s.activeVoices() == 0, "sustentação zero: a voz termina sozinha no fim do decaimento");
}

/** Maior segunda diferença (|x[n] - 2x[n-1] + x[n-2]|): estalos aparecem como picos aqui. */
static double maxCurvature (const std::vector<float>& x, size_t from, size_t to)
{
    double m = 0.0;

    for (size_t i = std::max<size_t> (from, 2); i < std::min (to, x.size()); ++i)
        m = std::max (m, (double) std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));

    return m;
}

static void testVoiceStealing()
{
    std::printf ("Roubo de voz\n");
    Synth s;
    s.prepare (rate, 256);

    // O mesmo trecho com 2 vozes (a terceira nota rouba a primeira) e com 8 (ninguém é roubado):
    // a diferença é só a voz roubada saindo. Com fade, essa diferença é tão lisa quanto a própria
    // voz; um corte seco daria um degrau do tamanho da onda.
    const std::vector<NoteSpec> notes { { 0.0, 0.3, 60 }, { 0.1, 0.3, 64 }, { 0.2, 0.3, 67 }, { 0.25, 0.3, 72 } };

    auto renderWith = [&s, &notes] (int polyIndex, bool onlyFirst)
    {
        plainPatch (s);
        setp (s, "l1.wave", 3);
        setp (s, "env1.release", 3000);
        setp (s, "poly", (float) polyIndex);
        s.reset();
        return render (s, onlyFirst ? std::vector<NoteSpec> { notes[0] } : notes, 0.6);
    };

    const auto stolen = renderWith (1, false);
    const int voicesAfter = s.activeVoices();
    const auto roomy = renderWith (7, false);
    const auto alone = renderWith (7, true);
    std::vector<float> diff (stolen.size());

    for (size_t i = 0; i < diff.size(); ++i)
        diff[i] = stolen.l[i] - roomy.l[i];

    const double own = maxCurvature (alone.l, at (0.05), at (0.19));
    const double steal = maxCurvature (diff, at (0.195), at (0.22));

    // Um corte seco seria um degrau do tamanho da onda naquele instante.
    const double cutStep = peak (alone.l, at (0.19), at (0.21));
    CHECK (steal < own * 2.0 && steal < cutStep * 0.02,
           "terceira nota com 2 vozes: a mais antiga sai em fade de 6 ms, sem estalo (%.5f; a voz sozinha %.5f; corte seco ~%.3f)",
           steal, own, cutStep);
    CHECK (voicesAfter <= 2 + 1, "polifonia respeitada (%d vozes soando)", voicesAfter);

    // Polifonia 32 e 40 notas de uma vez: nada explode e não passa do limite.
    plainPatch (s);
    setp (s, "poly", 31);
    std::vector<NoteSpec> chord;

    for (int k = 0; k < 40; ++k)
        chord.push_back ({ 0.001 * k, 0.5, 30 + k, 0.5f });

    const auto b = render (s, chord, 0.3);
    CHECK (allFinite (b) && s.activeVoices() <= Synth::maxVoices, "40 notas juntas com polifonia 32: %d vozes, sem NaN", s.activeVoices());
}

static void testGlide()
{
    std::printf ("Mono, legato e glide\n");
    Synth s;
    s.prepare (rate, 256);
    plainPatch (s);
    setp (s, "l1.wave", 3);
    setp (s, "mono", 1);
    setp (s, "glide", 100);

    // Slide do piano roll: a nota anterior segura até depois do começo da próxima (legato).
    const auto a = render (s, { { 0.0, 0.31, 48 }, { 0.3, 0.4, 60 } }, 0.8);
    const double before = measureHz (a.l, 0.1, 0.28), after = measureHz (a.l, 0.45, 0.65), middle = measureHz (a.l, 0.33, 0.37);
    CHECK (std::abs (before - 130.81) < 1.0, "antes do slide: Dó2 (%.1f Hz)", before);
    CHECK (std::abs (after - 261.63) < 1.5, "depois do slide: Dó3 (%.1f Hz)", after);
    CHECK (middle > 150.0 && middle < 240.0, "no meio do glide de 100 ms: entre os dois (%.1f Hz)", middle);
    CHECK (std::abs (db (rms (a.l, at (0.31), at (0.34))) - db (rms (a.l, at (0.25), at (0.29)))) < 1.0,
           "legato: o envelope não recomeça (volume contínuo)");

    // Notas separadas, glide "só no slide": a segunda já começa no tom dela.
    s.reset();
    const auto b = render (s, { { 0.0, 0.3, 48 }, { 0.3, 0.3, 60 } }, 0.7);
    const double direct = measureHz (b.l, 0.31, 0.36);
    CHECK (std::abs (direct - 261.63) < 2.0, "sem slide não desliza (%.1f Hz logo no começo)", direct);

    // Glide "Sempre": desliza mesmo separado.
    setp (s, "porta", 1);
    s.reset();
    const auto c = render (s, { { 0.0, 0.3, 48 }, { 0.3, 0.3, 60 } }, 0.7);
    const double portamento = measureHz (c.l, 0.31, 0.35);
    CHECK (portamento < 230.0, "glide Sempre: começa deslizando (%.1f Hz)", portamento);

    // Soltar a nota de cima com a de baixo segurada volta deslizando para ela.
    setp (s, "porta", 0);
    s.reset();
    const auto d = render (s, { { 0.0, 1.0, 48 }, { 0.2, 0.3, 60 } }, 1.0);
    CHECK (std::abs (measureHz (d.l, 0.7, 0.95) - 130.81) < 1.0, "soltou a de cima: volta para a segurada (%.1f Hz)", measureHz (d.l, 0.7, 0.95));
}

static void testRobustness()
{
    std::printf ("NaN, desnormais e parâmetros extremos\n");
    Synth s;
    s.prepare (rate, 512);
    Random rng (1234);
    bool finite = true;
    int denormals = 0;

    for (int round = 0; round < 40; ++round)
    {
        for (int i = 0; i < numParams; ++i)
        {
            const auto& p = param (i);

            if (! p.hidden || i == P::mono)
                s.setParam (i, p.min + rng.uniform() * (p.max - p.min));
        }

        const auto a = render (s, { { 0.0, 0.2, 20 + (int) (rng.uniform() * 90) }, { 0.05, 0.1, 40 + (int) (rng.uniform() * 60) } }, 0.35, 64 + round * 37);
        finite &= allFinite (a);
        denormals += countDenormals (a);
    }

    CHECK (finite, "40 timbres aleatórios (todos os parâmetros sorteados): saída sempre finita");
    CHECK (denormals == 0, "nenhum número desnormal na saída");
    s.setParam (P::fltCutoff, std::numeric_limits<float>::quiet_NaN());
    CHECK (std::isfinite (s.getParam (P::fltCutoff)), "valor NaN vindo de fora é ignorado");

    // Cauda longa até o silêncio: termina em zero exato, sem ficar presa em desnormais.
    plainPatch (s);
    setp (s, "rev.on", 1);
    setp (s, "dly.on", 1);
    setp (s, "env1.release", 2000);
    s.reset();
    const auto tail = render (s, { { 0.0, 0.2, 60 } }, 12.0);
    CHECK (countDenormals (tail) == 0 && peak (tail.l, at (11.5)) < 1.0e-5, "cauda de reverb e delay some até o silêncio");
}

static double threadCpuSeconds();

static void testCpu()
{
    std::printf ("Orçamento de CPU (48 kHz, blocos de 256)\n");

    auto measure = [] (const char* name, int unison, double limit)
    {
        Synth s;
        s.prepare (rate, 256);
        s.setAll (defaultValues());
        const int types[] = { 0, 1, 2, 3 };

        for (int l = 0; l < 4; ++l)
        {
            const std::string pre = "l" + std::to_string (l + 1) + ".";
            setp (s, (pre + "on").c_str(), 1);
            setp (s, (pre + "type").c_str(), (float) types[l]);
            setp (s, (pre + "voices").c_str(), (float) (unison - 1));
            setp (s, (pre + "fmAlgo").c_str(), 3);
            setp (s, (pre + "fmAmt3").c_str(), 40);
            setp (s, (pre + "fmAmt4").c_str(), 30);
        }

        setp (s, "flt.cutoff", 2000);
        setp (s, "env1.sustain", 100);
        setp (s, "mod1.src", 3);
        setp (s, "mod1.dst", 14);
        setp (s, "mod1.amt", 20);
        setp (s, "sat.on", 1);
        setp (s, "rev.on", 1);

        std::vector<NoteSpec> notes;

        for (int k = 0; k < 16; ++k)
            notes.push_back ({ 0.0, 10.0, 36 + k * 3, 0.7f });

        const double t0 = threadCpuSeconds();   // CPU da thread: não sobe quando outro programa usa a máquina
        const auto a = render (s, notes, 10.0);
        const double cpu = threadCpuSeconds() - t0;
        const double factor = cpu / 10.0;
        CHECK (factor < limit && allFinite (a), "%s: %.1f%% de um núcleo (limite %.0f%%)", name, factor * 100.0, limit * 100.0);
    };

    measure ("16 vozes x 4 camadas (analógico, wavetable, FM 4 op, ruído), filtro 24 dB, saturação e reverb", 1, 0.5);
    measure ("o mesmo com unison 4 em cada camada", 4, 1.2);
}

//==============================================================================
// Presets

static bool writePresetLevels (const char* path)
{
    std::ofstream out (path, std::ios::binary);

    if (! out)
    {
        std::printf ("não consegui gravar %s\n", path);
        return false;
    }

    out << "// Gerado por whynot_synth_tests --levels: volume (dB) de cada preset, medido.\n";
    out << "// Alvo: " << levelTargetLufs << " LUFS (momentâneo máximo de uma nota), pico até " << levelPeakCeilingDb << " dBFS.\n";

    std::vector<const Preset*> all { &initPreset() };

    for (auto& p : allPresets())
        all.push_back (&p);

    for (auto* preset : all)
    {
        const auto& p = *preset;
        const float v = levelledVolume (p);
        char line[256];
        std::snprintf (line, sizeof (line), "{ \"%s\", %.2ff },\n", p.id.c_str(), v);
        out << line;
        std::printf ("%-32s %7.2f dB\n", p.id.c_str(), v);
    }

    return true;
}

static std::vector<std::string> ideiasCategories (const std::string& genre);

static void testPresets()
{
    std::printf ("Presets\n");
    const auto list = allPresets();
    std::set<std::string> ids;
    std::map<std::string, int> perCategory, perGenre;
    bool idsOk = true, tagsOk = true, levelled = true, named = true;

    for (auto& p : list)
    {
        idsOk &= ids.insert (p.id).second && findPreset (p.id) == &p && p.id != "init";
        tagsOk &= findCategory (p.category) != nullptr && p.genres != 0 && (p.genres & ~255u) == 0;
        levelled &= p.levelled;
        named &= ! p.name.empty() && ! p.description.empty() && (int) p.values.size() == numParams;
        perCategory[p.category]++;

        for (auto& g : p.genreIds())
            perGenre[g]++;
    }

    CHECK (list.size() >= 150, "%d presets (meta: 150 bem feitos, depois ~400)", (int) list.size());
    CHECK (idsOk, "ids únicos e achados por findPreset");
    CHECK (tagsOk, "toda categoria e gênero existem; todo preset tem gênero");
    CHECK (named, "todo preset tem nome, descrição e todos os valores");
    CHECK (levelled && initPreset().levelled, "todo preset (e o Init) tem volume medido (rode --levels depois de mudar presets)");

    std::string counts;

    for (auto& c : allCategories())
    {
        counts += std::string (c.id) + "=" + std::to_string (perCategory[c.id]) + " ";
        CHECK (perCategory[c.id] >= 5, "categoria %s com %d presets", c.name, perCategory[c.id]);
    }

    for (auto& g : allGenres())
        CHECK (perGenre[g.id] >= 12, "gênero %s com %d presets", g.name, perGenre[g.id]);

    // Nenhum par idêntico (preset repetido com outro nome).
    bool distinct = true;

    for (size_t i = 0; i < list.size(); ++i)
        for (size_t j = i + 1; j < list.size(); ++j)
            if (list[i].values == list[j].values)
            {
                distinct = false;
                std::printf ("  iguais: %s e %s\n", list[i].id.c_str(), list[j].id.c_str());
            }

    CHECK (distinct, "nenhum preset é cópia de outro");

    // Nível: mesma nota (por categoria) e força dão loudness parecida, sem passar do teto. Nos timbres de
    // acorde, o acorde de 4 notas também fica abaixo do teto dele (e pode deixar a nota sozinha mais baixa).
    float lo = 100.0f, hi = -100.0f, worstPeak = -100.0f, worstChord = -100.0f;
    int outOfRange = 0, chordLimitedCount = 0;

    for (auto& p : list)
    {
        const auto* category = findCategory (p.category);
        const auto r = measureLevel (p.values, category->testKey);
        const bool peakLimited = r.peakDb > levelPeakCeilingDb - 0.6f;
        bool chordOk = true, chordLimited = false;

        if (isChordCategory (p.category))
        {
            const auto rc = measureLevel (p.values, chordKeys (category->testKey));
            chordOk = rc.finite && rc.peakDb <= levelChordPeakCeilingDb + 0.2f;
            chordLimited = rc.peakDb > levelChordPeakCeilingDb - 0.6f;
            worstChord = std::max (worstChord, rc.peakDb);
            chordLimitedCount += chordLimited && r.lufs < levelTargetLufs - 1.0f ? 1 : 0;
        }

        const bool limited = peakLimited || chordLimited;
        const bool ok = r.finite && chordOk && r.peakDb <= levelPeakCeilingDb + 0.2f
                        && (std::abs (r.lufs - levelTargetLufs) <= 1.0f || (limited && r.lufs < levelTargetLufs && r.lufs > levelTargetLufs - 12.0f));

        if (! ok)
        {
            ++outOfRange;
            std::printf ("  fora: %-30s %6.1f LUFS  pico %5.1f dBFS\n", p.id.c_str(), r.lufs, r.peakDb);
        }

        lo = std::min (lo, r.lufs);
        hi = std::max (hi, r.lufs);
        worstPeak = std::max (worstPeak, r.peakDb);
    }

    CHECK (outOfRange == 0, "todos nivelados: %.1f a %.1f LUFS (alvo %.0f), pico máximo %.1f dBFS (teto %.0f)",
           lo, hi, levelTargetLufs, worstPeak, levelPeakCeilingDb);
    CHECK (worstChord <= levelChordPeakCeilingDb + 0.2f,
           "acorde de 4 notas nos timbres de acorde: pico máximo %.1f dBFS (teto %.1f); %d ficaram mais baixos por causa do acorde",
           worstChord, levelChordPeakCeilingDb, chordLimitedCount);

    // Variar: continua soando, sem NaN, perto do volume original.
    int variedBad = 0;

    for (size_t i = 0; i < list.size(); i += 7)
    {
        const auto& p = list[i];
        const auto key = findCategory (p.category)->testKey;
        const auto before = measureLevel (p.values, key);
        const auto after = measureLevel (varied (p.values, 0.4f, (std::uint32_t) i + 1), key);

        if (! after.finite || after.lufs < before.lufs - 8.0f || after.peakDb > 0.0f)
        {
            ++variedBad;
            std::printf ("  variar estragou: %s (%.1f -> %.1f LUFS, pico %.1f)\n", p.id.c_str(), before.lufs, after.lufs, after.peakDb);
        }
    }

    CHECK (variedBad == 0, "Variar mantém os presets soando e sem clipar");

    // Busca
    CHECK (! findPresets ("808", "", "").empty() && ! findPresets ("", "funk", "").empty(), "filtro por categoria e gênero");

    // O Ideias sugere som por gênero e parte (acordes, melodia, arpejo, baixo, 808): cada par que ele pede
    // tem de onde escolher.
    std::string thin;

    for (auto& g : allGenres())
        for (auto& cat : ideiasCategories (g.id))
            if (findPresets (cat, g.id, "").size() < 4)
                thin += std::string (" ") + g.id + "/" + cat;

    CHECK (thin.empty(), "todo par gênero x categoria que o Ideias pede tem 4 presets ou mais%s", thin.c_str());

    // Clean-room: nenhum nome de produto comercial no nome ou na descrição dos presets.
    const char* brands[] = { "rhodes", "wurli", "moog", "juno", "jupiter", "prophet", "dx7", "serum", "sylenth", "nexus",
                             "omnisphere", "fender", "hammond", "mellotron", "clavinet", "leslie", "roland", "korg", "yamaha",
                             "tr-808", "tr808", "tr-909", "tb-303", "oberheim", "massive", "zenology", "kontakt", "splice" };
    std::string branded;

    for (auto& p : list)
    {
        std::string text = p.name + " " + p.description;
        std::transform (text.begin(), text.end(), text.begin(), [] (char c) { return (char) std::tolower ((unsigned char) c); });

        for (auto* b : brands)
            if (text.find (b) != std::string::npos)
                branded += " " + p.id + " (" + b + ")";
    }

    CHECK (branded.empty(), "nenhum nome de produto comercial nos presets%s", branded.c_str());
    CHECK (! findPresets ("", "", "SUJO").empty() || ! findPresets ("", "", "distorcido").empty(), "busca ignora maiúsculas");
}

//==============================================================================
// CPU dos presets: tempo de CPU da própria thread (não o relógio), para a medida não depender
// do que mais está rodando na máquina.

#ifdef _WIN32
/** Ciclos da thread por segundo: o contador do QueryThreadCycleTime anda no ritmo fixo do TSC.
    Calibrado contra o relógio de alta resolução com a thread ocupada; fica o maior de 3 (uma
    preempção no meio só diminui a conta). */
static double threadCyclesPerSecond()
{
    static const double hz = []
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency (&freq);
        double best = 0.0;

        for (int k = 0; k < 3; ++k)
        {
            LARGE_INTEGER t0, t1;
            ULONG64 c0 = 0, c1 = 0;
            QueryPerformanceCounter (&t0);
            QueryThreadCycleTime (GetCurrentThread(), &c0);

            do { QueryPerformanceCounter (&t1); } while (t1.QuadPart - t0.QuadPart < freq.QuadPart / 10);

            QueryThreadCycleTime (GetCurrentThread(), &c1);
            best = std::max (best, (double) (c1 - c0) / ((double) (t1.QuadPart - t0.QuadPart) / (double) freq.QuadPart));
        }

        return best;
    }();

    return hz;
}
#endif

/** Tempo de CPU da própria thread, em segundos. O GetThreadTimes anda em degraus de ~15,6 ms;
    os ciclos da thread não têm degrau. */
static double threadCpuSeconds()
{
#ifdef _WIN32
    ULONG64 cycles = 0;

    if (QueryThreadCycleTime (GetCurrentThread(), &cycles))
        return (double) cycles / threadCyclesPerSecond();
#endif
    return (double) std::clock() / CLOCKS_PER_SEC;
}

/** Fração de um núcleo para tocar `values` com `voices` notas seguradas (48 kHz, blocos de 256). */
static double presetCpu (const std::vector<float>& values, int voices, double seconds)
{
    Synth s;
    s.setAll (values);
    s.prepare (rate, 256);

    std::vector<NoteSpec> notes;

    for (int k = 0; k < voices; ++k)
        notes.push_back ({ 0.0, seconds, 36 + (k * 5) % 48, 0.8f });

    const double t0 = threadCpuSeconds();
    render (s, notes, seconds);
    return (threadCpuSeconds() - t0) / seconds;
}

static int runCpuReport()
{
    std::printf ("CPU por preset, polifonia cheia (todas as vozes do preset seguradas), 48 kHz, blocos de 256\n");
    std::printf ("(fração de um núcleo deste PC, tempo de CPU da thread)\n\n");

    struct Row { std::string id; int voices; double cpu; };
    std::vector<Row> rows;

    for (auto& p : allPresets())
    {
        const bool mono = p.values[(size_t) P::mono] >= 0.5f;
        const int voices = mono ? 1 : (int) std::lround (p.values[(size_t) P::poly]) + 1;
        rows.push_back ({ p.id, voices, presetCpu (p.values, voices, 2.0) });
    }

    auto byCpu = [] (const Row& a, const Row& b) { return a.cpu > b.cpu; };
    std::sort (rows.begin(), rows.end(), byCpu);
    double total = 0.0;

    for (auto& r : rows)
        total += r.cpu;

    std::printf ("  média de todos: %.1f%%   mediana: %.1f%%\n\n", total / (double) rows.size() * 100.0, rows[rows.size() / 2].cpu * 100.0);

    // O relógio da thread anda em degraus de ~15,6 ms: os mais pesados são medidos de novo com 10 s de áudio.
    const size_t top = std::min<size_t> (15, rows.size());

    for (size_t i = 0; i < top; ++i)
        rows[i].cpu = presetCpu (findPreset (rows[i].id)->values, rows[i].voices, 10.0);

    std::sort (rows.begin(), rows.begin() + (std::ptrdiff_t) top, byCpu);
    std::printf ("  os %d mais pesados (10 s de áudio cada):\n", (int) top);

    for (size_t i = 0; i < top; ++i)
        std::printf ("  %-28s %2d vozes  %5.1f%%\n", rows[i].id.c_str(), rows[i].voices, rows[i].cpu * 100.0);

    // Onde vai o tempo nos 5 mais pesados: vozes (sem os efeitos) e efeitos (saturação, EQ, estéreo, delay, reverb).
    std::printf ("\n  detalhe (6 s de áudio):\n");

    for (size_t i = 0; i < std::min<size_t> (5, top); ++i)
    {
        auto dry = findPreset (rows[i].id)->values;
        dry[(size_t) P::satOn] = dry[(size_t) P::eqOn] = dry[(size_t) P::dlyOn] = dry[(size_t) P::revOn] = 0.0f;
        dry[(size_t) P::width] = 100.0f;
        const double voicesOnly = presetCpu (dry, rows[i].voices, 6.0);
        const double one = presetCpu (dry, 1, 6.0);
        std::printf ("  %-28s vozes %5.1f%% (uma voz %4.2f%%)  efeitos %5.1f%%\n", rows[i].id.c_str(),
                     voicesOnly * 100.0, one * 100.0, (rows[i].cpu - voicesOnly) * 100.0);
    }

    std::printf ("\n");

    // Pior caso possível: 4 camadas (analógico, wavetable, FM 4 op, ruído), unison 8, 32 vozes.
    auto worst = defaultValues();

    for (int l = 0; l < numLayers; ++l)
    {
        worst[(size_t) P::layer (l, lOn)] = 1;
        worst[(size_t) P::layer (l, lType)] = (float) l;
        worst[(size_t) P::layer (l, lVoices)] = 7;
        worst[(size_t) P::layer (l, lFmAlgo)] = 3;
    }

    worst[(size_t) P::env (0, eSustain)] = 100;
    worst[(size_t) P::poly] = 31;
    worst[(size_t) P::fltCutoff] = 2000;
    std::printf ("  pior caso (4 camadas, unison 8 em todas, 32 vozes): %.1f%%\n", presetCpu (worst, 32, 4.0) * 100.0);

    for (int l = 0; l < numLayers; ++l)
        worst[(size_t) P::layer (l, lVoices)] = 0;

    std::printf ("  4 camadas sem unison, 16 vozes:                    %.1f%%\n", presetCpu (worst, 16, 6.0) * 100.0);
    return 0;
}

/** O menor de `runs` medidas: a máquina é dividida com outros builds, e o menor é o mais perto do custo real. */
static double bestCpu (const std::vector<float>& values, int voices, double seconds, int runs)
{
    double best = 1.0e9;

    for (int r = 0; r < runs; ++r)
        best = std::min (best, presetCpu (values, voices, seconds));

    return best;
}

/** Banco fixo para comparar versões do motor: sempre os mesmos presets e casos, 3 medidas, fica a menor. */
static int runCpuBench()
{
    std::printf ("Banco de CPU (48 kHz, blocos de 256, polifonia cheia; fração de um núcleo, menor de 3 medidas de 3 s)\n\n");
    std::printf ("  %-24s %6s %8s %8s\n", "preset", "total", "vozes", "efeitos");

    const char* ids[] = { "coro-soul", "metais-phonk", "keys-sombrio", "pad-orgao-assombrado", "cordas-cinema",
                          "pad-phonk-escuro", "pad-voz-trap", "keys-ep-lofi", "piano-soul", "pad-soul" };

    for (auto* id : ids)
    {
        const auto* p = findPreset (id);

        if (p == nullptr)
            continue;

        const bool mono = p->values[(size_t) P::mono] >= 0.5f;
        const int voices = mono ? 1 : (int) std::lround (p->values[(size_t) P::poly]) + 1;
        auto dry = p->values;
        dry[(size_t) P::satOn] = dry[(size_t) P::eqOn] = dry[(size_t) P::dlyOn] = dry[(size_t) P::revOn] = 0.0f;
        dry[(size_t) P::width] = 100.0f;
        const double total = bestCpu (p->values, voices, 3.0, 3);
        const double voicesOnly = bestCpu (dry, voices, 3.0, 3);
        std::printf ("  %-24s %5.1f%% %7.1f%% %7.1f%%\n", id, total * 100.0, voicesOnly * 100.0, (total - voicesOnly) * 100.0);
    }

    auto worst = defaultValues();

    for (int l = 0; l < numLayers; ++l)
    {
        worst[(size_t) P::layer (l, lOn)] = 1;
        worst[(size_t) P::layer (l, lType)] = (float) l;
        worst[(size_t) P::layer (l, lVoices)] = 7;
        worst[(size_t) P::layer (l, lFmAlgo)] = 3;
    }

    worst[(size_t) P::env (0, eSustain)] = 100;
    worst[(size_t) P::poly] = 31;
    worst[(size_t) P::fltCutoff] = 2000;
    std::printf ("\n  4 camadas, unison 8 em todas, 32 vozes:  %5.1f%%\n", bestCpu (worst, 32, 3.0, 2) * 100.0);

    for (int l = 0; l < numLayers; ++l)
        worst[(size_t) P::layer (l, lVoices)] = 0;

    std::printf ("  4 camadas sem unison, 32 vozes:          %5.1f%%\n", bestCpu (worst, 32, 3.0, 3) * 100.0);
    std::printf ("  4 camadas sem unison, 16 vozes:          %5.1f%%\n", bestCpu (worst, 16, 3.0, 3) * 100.0);

    // Cada efeito sozinho (uma nota de seno, para o custo das vozes não pesar).
    auto base = defaultValues();
    base[(size_t) P::env (0, eSustain)] = 100;
    const double baseCpu = bestCpu (base, 1, 3.0, 3);
    std::printf ("\n  efeitos sozinhos (acima de uma voz simples, %.2f%%):\n", baseCpu * 100.0);

    auto effect = [&] (const char* name, std::initializer_list<std::pair<int, float>> set)
    {
        auto values = base;

        for (auto& [index, value] : set)
            values[(size_t) index] = value;

        std::printf ("  %-24s %5.2f%%\n", name, (bestCpu (values, 1, 3.0, 3) - baseCpu) * 100.0);
    };

    for (int mode = 0; mode < 6; ++mode)
    {
        char name[48];
        std::snprintf (name, sizeof (name), "saturação, tipo %d", mode);
        effect (name, { { P::satOn, 1.0f }, { P::satMode, (float) mode } });
    }

    effect ("EQ", { { P::eqOn, 1.0f }, { P::eqLow, 3.0f } });
    effect ("estéreo (largura 150%)", { { P::width, 150.0f } });
    effect ("delay", { { P::dlyOn, 1.0f } });
    effect ("reverb", { { P::revOn, 1.0f } });
    return 0;
}

/** Pares gênero × categoria que o Ideias pede (src/ideas/Genres.cpp, soundCategories: acordes,
    melodia, arpejo, baixo e 808). Cópia do mapa de lá: se ele mudar, mude aqui. */
static std::vector<std::string> ideiasCategories (const std::string& genre)
{
    auto is = [&genre] (std::initializer_list<const char*> ids)
    {
        for (auto* id : ids)
            if (genre == id)
                return true;

        return false;
    };

    std::vector<std::string> out;
    auto add = [&out] (std::initializer_list<const char*> ids)
    {
        for (auto* id : ids)
            if (std::find (out.begin(), out.end(), id) == out.end())
                out.push_back (id);
    };

    // acordes
    if (is ({ "drill" }))                   add ({ "strings", "pad", "keys" });
    else                                    add ({ "pad", "keys" });

    // melodia
    if (is ({ "trap" }))                    add ({ "bell", "pluck", "lead" });
    else if (is ({ "phonk" }))              add ({ "bell", "lead", "pluck" });
    else if (is ({ "drill" }))              add ({ "keys", "strings", "bell" });
    else if (is ({ "rnb", "lofi", "rap" })) add ({ "keys", "pluck", "lead" });
    else if (is ({ "funk" }))               add ({ "lead", "pluck", "brass" });
    else                                    add ({ "lead", "pluck", "keys" });

    add ({ "arp", "pluck", "bell", "bass", "808" });
    return out;
}

/** Matriz gênero × categoria; marca com * os pares que o Ideias pede e têm menos de `minimum`. */
static int runGenreMatrix (int minimum)
{
    std::printf ("%-10s", "");

    for (auto& c : allCategories())
        std::printf (" %7s", c.id);

    std::printf ("  total\n");
    int gaps = 0;

    for (auto& g : allGenres())
    {
        const auto wanted = ideiasCategories (g.id);
        std::printf ("%-10s", g.id);
        int total = 0;

        for (auto& c : allCategories())
        {
            const int n = (int) findPresets (c.id, g.id, "").size();
            const bool asked = std::find (wanted.begin(), wanted.end(), std::string (c.id)) != wanted.end();
            const bool gap = asked && n < minimum;
            gaps += gap ? 1 : 0;
            total += n;
            std::printf (" %6d%s", n, gap ? "*" : (asked ? "+" : " "));
        }

        std::printf ("  %d\n", total);
    }

    std::printf ("\n+ o Ideias pede este par; * pede e tem menos de %d. Faltas: %d\n", minimum, gaps);

    // Perfil de cada gênero, pelos parâmetros: o que dá para conferir sem ouvido.
    auto val = [] (const Preset& p, int index) { return p.values[(size_t) index]; };
    auto saturated = [&val] (const Preset& p) { return val (p, P::satOn) >= 0.5f; };
    auto lowpassHz = [&val] (const Preset& p)
    {
        return val (p, P::fltOn) >= 0.5f && (int) std::lround (val (p, P::fltMode)) == (int) FilterMode::lowpass ? val (p, P::fltCutoff) : 20000.0f;
    };
    // "Poeira" do lo-fi: fita, deriva, filtro fechado, afinação balançando ou chiado (camada de ruído).
    auto dusty = [&] (const Preset& p)
    {
        const bool tape = saturated (p) && (int) std::lround (val (p, P::satMode)) == 0;
        bool wobble = false, hiss = false;

        for (int s = 0; s < numSlots; ++s)
            wobble |= (int) std::lround (val (p, P::slot (s, mDst))) == (int) Dest::pitch && val (p, P::slot (s, mAmt)) != 0.0f;

        for (int l = 0; l < numLayers; ++l)
            hiss |= val (p, P::layer (l, lOn)) >= 0.5f && (int) std::lround (val (p, P::layer (l, lType))) == (int) LayerType::noise;

        return tape || val (p, P::drift) >= 20.0f || lowpassHz (p) <= 5000.0f || wobble || hiss;
    };
    auto slides = [&val] (const Preset& p) { return val (p, P::mono) >= 0.5f && val (p, P::glide) >= 30.0f; };
    auto longTail = [&val] (const Preset& p) { return val (p, P::env (0, eSustain)) >= 50.0f || val (p, P::env (0, eDecay)) >= 1200.0f; };

    std::printf ("\nPerfil por gênero (pelos parâmetros)\n");
    std::printf ("%-8s %5s %10s %8s %16s %16s\n", "gênero", "total", "saturação", "poeira", "808 que deslizam", "808 longos");

    for (auto& g : allGenres())
    {
        const auto list = findPresets ("", g.id, "");
        int sat = 0, dust = 0, n808 = 0, slide808 = 0, long808 = 0;

        for (auto* p : list)
        {
            sat += saturated (*p) ? 1 : 0;
            dust += dusty (*p) ? 1 : 0;

            if (p->category == "808")
            {
                ++n808;
                slide808 += slides (*p) ? 1 : 0;
                long808 += longTail (*p) ? 1 : 0;
            }
        }

        const auto pct = [&list] (int k) { return list.empty() ? 0 : (int) std::lround (100.0 * k / (double) list.size()); };
        std::printf ("%-8s %5d %9d%% %7d%% %10d de %-3d %10d de %-3d\n", g.id, (int) list.size(), pct (sat), pct (dust),
                     slide808, n808, long808, n808);
    }

    // Quem destoa do próprio gênero.
    std::printf ("\nPara conferir:\n");

    for (auto* p : findPresets ("", "lofi", ""))
        if (! dusty (*p))
            std::printf ("  lofi sem poeira (sem fita, deriva, filtro fechado nem afinação balançando): %s\n", p->id.c_str());

    for (auto* p : findPresets ("808", "drill", ""))
        if (! slides (*p))
            std::printf ("  808 de drill que não desliza (mono com glide): %s\n", p->id.c_str());

    for (auto* p : findPresets ("808", "trap", ""))
        if (! longTail (*p))
            std::printf ("  808 de trap curto: %s\n", p->id.c_str());

    for (auto* p : findPresets ("", "phonk", ""))
        if (! saturated (*p) && ! dusty (*p))
            std::printf ("  phonk limpo (sem saturação nem poeira): %s\n", p->id.c_str());

    // Acordes: o nível é medido com uma nota, e o Ideias toca acordes de 4 notas em pads e keys.
    // Acima de 0,8 (-1,9 dBFS) o soft clip da saída começa a curvar.
    int chordPresets = 0, inKnee = 0, overFull = 0;
    float worst = 0.0f;
    std::string worstId;

    for (auto& p : allPresets())
    {
        if (p.category != "pad" && p.category != "keys" && p.category != "strings" && p.category != "brass" && p.category != "vox")
            continue;

        Synth s;
        s.setAll (p.values);
        s.prepare (rate, 256);
        const int key = findCategory (p.category)->testKey;
        std::vector<NoteSpec> chord { { 0.0, 1.6, key }, { 0.0, 1.6, key + 3 }, { 0.0, 1.6, key + 7 }, { 0.0, 1.6, key + 10 } };
        const auto a = render (s, chord, 2.0);
        float peak = 0.0f;

        for (size_t i = 0; i < a.size(); ++i)
            peak = std::max (peak, std::max (std::abs (a.l[i]), std::abs (a.r[i])));

        ++chordPresets;
        inKnee += peak > 0.8f ? 1 : 0;
        overFull += peak > 0.98f ? 1 : 0;

        if (peak > worst)
        {
            worst = peak;
            worstId = p.id;
        }
    }

    std::printf ("\nAcorde de 4 notas (força 0,8) nos %d presets de pad, keys, cordas, metais e vox: %d passam de -1,9 dBFS "
                 "(o soft clip curva), %d chegam a 0 dBFS; o maior é %s (%.1f dBFS)\n",
                 chordPresets, inKnee, overFull, worstId.c_str(), 20.0 * std::log10 (std::max (1.0e-9f, worst)));

    // O corte com o envelope do filtro todo aberto (plucks e metais começam fechados e abrem no ataque).
    for (auto* p : findPresets ("", "pop", ""))
        if (lowpassHz (*p) * std::exp2 (std::max (0.0f, val (*p, P::fltEnv))) < 1500.0f && p->category != "808" && p->category != "bass")
            std::printf ("  pop bem escuro (passa-baixas abaixo de 1,5 kHz mesmo com o envelope): %s\n", p->id.c_str());

    return gaps;
}

/** Grava o render de cada preset (acorde de 3 notas, 0,3 s, float intercalado) em `folder`, para
    comparar duas versões do motor amostra por amostra. */
static int dumpPresets (const char* folder)
{
    for (auto& p : allPresets())
    {
        Synth s;
        s.setAll (p.values);
        s.prepare (rate, 256);
        const int key = findCategory (p.category)->testKey;
        const auto a = render (s, { { 0.0, 0.2, key, 0.8f }, { 0.0, 0.2, key + 4, 0.6f }, { 0.05, 0.2, key + 7, 0.9f } }, 0.3);
        std::ofstream out (std::string (folder) + "/" + p.id + ".raw", std::ios::binary);

        for (size_t i = 0; i < a.size(); ++i)
        {
            out.write (reinterpret_cast<const char*> (&a.l[i]), sizeof (float));
            out.write (reinterpret_cast<const char*> (&a.r[i]), sizeof (float));
        }
    }

    return 0;
}

/** WAV de 16 bits, estéreo. */
static bool writeWav (const std::string& path, const Audio& a)
{
    std::ofstream out (path, std::ios::binary);

    if (! out)
        return false;

    const auto frames = (std::uint32_t) a.size();
    const std::uint32_t dataBytes = frames * 4, sr = (std::uint32_t) rate;
    auto u32 = [&out] (std::uint32_t v) { out.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&out] (std::uint16_t v) { out.write (reinterpret_cast<const char*> (&v), 2); };

    out.write ("RIFF", 4); u32 (36 + dataBytes); out.write ("WAVE", 4);
    out.write ("fmt ", 4); u32 (16); u16 (1); u16 (2); u32 (sr); u32 (sr * 4); u16 (4); u16 (16);
    out.write ("data", 4); u32 (dataBytes);

    for (std::uint32_t i = 0; i < frames; ++i)
        for (float x : { a.l[i], a.r[i] })
            u16 ((std::uint16_t) (std::int16_t) std::lround (std::clamp (x, -1.0f, 1.0f) * 32767.0f));

    return true;
}

/** Uma demo por gênero, para ouvir: dois presets de cada categoria que o Ideias pede, cada um com uma
    frase curta (graves: linha com salto; acordes: Dó menor com sétima; o resto: melodia). Grava
    <gênero>.wav e <gênero>.txt (o que toca em cada segundo) em `folder`. */
static int writeGenreDemos (const std::string& folder)
{
    constexpr double segment = 2.4;

    for (auto& g : allGenres())
    {
        Audio all;
        std::ofstream guide (folder + "/" + g.id + ".txt", std::ios::binary);
        guide << "Demo do gênero " << g.name << " (WHYNOT Synth): dois presets por categoria que o Ideias usa.\n\n";

        for (auto& cat : ideiasCategories (g.id))
        {
            const auto list = findPresets (cat, g.id, "");

            for (size_t k = 0; k < std::min<size_t> (2, list.size()); ++k)
            {
                const auto& p = *list[k];
                Synth s;
                s.setAll (p.values);
                s.prepare (rate, 256);
                const int key = findCategory (p.category)->testKey;
                std::vector<NoteSpec> notes;

                if (cat == "808" || cat == "bass")
                    notes = { { 0.0, 0.7, key }, { 0.75, 0.2, key }, { 1.0, 0.45, key + 7 }, { 1.5, 0.45, key - 5 } };
                else if (cat == "pad" || cat == "keys" || cat == "strings" || cat == "brass")
                    notes = { { 0.0, 1.6, key }, { 0.0, 1.6, key + 3 }, { 0.0, 1.6, key + 7 }, { 0.0, 1.6, key + 10 } };
                else
                    notes = { { 0.0, 0.22, key }, { 0.25, 0.22, key + 3 }, { 0.5, 0.22, key + 7 }, { 0.75, 0.22, key + 10 },
                              { 1.0, 0.22, key + 7 }, { 1.25, 0.4, key + 3 } };

                char line[200];
                std::snprintf (line, sizeof (line), "%5.1f s  %-8s %-28s %s\n", (double) all.size() / rate, cat.c_str(),
                               p.id.c_str(), p.name.c_str());
                guide << line;

                const auto a = render (s, notes, segment);
                all.l.insert (all.l.end(), a.l.begin(), a.l.end());
                all.r.insert (all.r.end(), a.r.begin(), a.r.end());
            }
        }

        if (! writeWav (folder + "/" + g.id + ".wav", all))
        {
            std::printf ("não consegui gravar em %s\n", folder.c_str());
            return 1;
        }

        std::printf ("%s/%s.wav  (%.0f s)\n", folder.c_str(), g.id, (double) all.size() / rate);
    }

    return 0;
}

//==============================================================================
int main (int argc, char** argv)
{
    if (argc >= 3 && std::strcmp (argv[1], "--demos") == 0)
        return writeGenreDemos (argv[2]);

    if (argc >= 2 && std::strcmp (argv[1], "--cpu-bench") == 0)
        return runCpuBench();

    if (argc >= 3 && std::strcmp (argv[1], "--dump") == 0)
        return dumpPresets (argv[2]);

    if (argc >= 2 && std::strcmp (argv[1], "--genres") == 0)
        return runGenreMatrix (argc >= 3 ? std::atoi (argv[2]) : 4) == 0 ? 0 : 1;

    if (argc >= 3 && std::strcmp (argv[1], "--levels") == 0)
        return writePresetLevels (argv[2]) ? 0 : 1;

    if (argc >= 2 && std::strcmp (argv[1], "--cpu") == 0)
        return runCpuReport();

    testParams();
    testWavetables();
    testAliasing();
    testEnvelopes();
    testVoiceStealing();
    testGlide();
    testRobustness();
    testPresets();
    testCpu();

    std::printf ("\n%s\n", failures == 0 ? "TODOS OS TESTES PASSARAM" : (std::to_string (failures) + " TESTE(S) FALHARAM").c_str());
    return failures == 0 ? 0 : 1;
}
