#include "Multiband.h"
#include "Dynamics.h"

namespace whynot::dsp
{

namespace
{
    const char* const soloNames[] = { "Todas", "Grave", "Médio-grave", "Médio-agudo", "Agudo" };

    const ParamInfo multibandParams[] = {
        { "x1", "Divisão 1", 40, 600, 150,     "Hz", ParamKind::continuous, true, {}, "Entre o grave e o médio-grave." },
        { "x2", "Divisão 2", 300, 5000, 1500,  "Hz", ParamKind::continuous, true, {}, "Entre o médio-grave e o médio-agudo." },
        { "x3", "Divisão 3", 2000, 16000, 6000, "Hz", ParamKind::continuous, true, {}, "Entre o médio-agudo e o agudo." },

        { "b1Thr",   "Limiar grave",   -60, 0, 0,  "dB", ParamKind::continuous, false, {}, "Acima disto a banda grave é comprimida. 0 dB = parada." },
        { "b1Ratio", "Razão grave",    1, 20, 3,   ":1", ParamKind::continuous, true },
        { "b1Gain",  "Ganho grave",    -12, 12, 0, "dB" },
        { "b2Thr",   "Limiar médio-grave", -60, 0, 0, "dB", ParamKind::continuous, false, {}, "Acima disto a banda é comprimida. 0 dB = parada." },
        { "b2Ratio", "Razão médio-grave",  1, 20, 3,  ":1", ParamKind::continuous, true },
        { "b2Gain",  "Ganho médio-grave",  -12, 12, 0, "dB" },
        { "b3Thr",   "Limiar médio-agudo", -60, 0, 0, "dB", ParamKind::continuous, false, {}, "Acima disto a banda é comprimida. 0 dB = parada." },
        { "b3Ratio", "Razão médio-agudo",  1, 20, 3,  ":1", ParamKind::continuous, true },
        { "b3Gain",  "Ganho médio-agudo",  -12, 12, 0, "dB" },
        { "b4Thr",   "Limiar agudo",   -60, 0, 0,  "dB", ParamKind::continuous, false, {}, "Acima disto a banda aguda é comprimida. 0 dB = parada." },
        { "b4Ratio", "Razão agudo",    1, 20, 3,   ":1", ParamKind::continuous, true },
        { "b4Gain",  "Ganho agudo",    -12, 12, 0, "dB" },

        { "attack",  "Ataque",  0.1f, 100, 10, "ms", ParamKind::continuous, true, {}, "O mesmo para as quatro bandas." },
        { "release", "Soltura", 10, 1000, 120, "ms", ParamKind::continuous, true, {}, "O mesmo para as quatro bandas." },
        { "solo",    "Ouvir",   0, 4, 0,       "",   ParamKind::choice,     false, soloNames, "Toca uma banda só, para achar onde está o problema." },
        { "output",  "Saída",   -12, 12, 0,    "dB" },
    };

    static_assert (std::size (multibandParams) == MultibandProcessor::count);

    constexpr float kneeDb = 6.0f;
}

const FxInfo& MultibandProcessor::staticInfo()
{
    static const FxInfo info { "multiband", "Multibanda", "Multi", "Dinâmica",
                               "Compressor de 4 bandas: segura só o grave que embola, só o médio duro da voz ou só o chiado. "
                               "Com disparo de outra trilha, a voz abre espaço só na faixa dela.",
                               multibandParams, true };
    return info;
}

std::array<float, 3> MultibandProcessor::crossovers (const float* v, double sampleRate) noexcept
{
    float c = std::min (v[x3], (float) (sampleRate * 0.45));
    float b = std::min (v[x2], c / 1.25f);
    float a = std::min (v[x1], b / 1.25f);
    b = std::max (b, a * 1.25f);
    c = std::max (c, b * 1.25f);
    return { a, b, c };
}

void MultibandProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = multibandParams[i].def;

    for (auto& f : logFreq)
        f.prepare (rate, 30.0f);

    for (auto& g : bandGain)
        g.prepare (rate, 20.0f);

    for (auto& a : audible)
        a.prepare (rate, 10.0f);

    // Janela do detector por banda: o grave precisa de mais tempo para não ondular com a onda.
    static constexpr float rmsMs[numBands] = { 20.0f, 6.0f, 3.0f, 2.0f };

    for (int b = 0; b < numBands; ++b)
        rmsCoeff[(size_t) b] = onePoleCoeff (rmsMs[b], rate);

    outGain.prepare (rate, 20.0f);
    dirty = true;
    snapToTargets();
    reset();
}

void MultibandProcessor::snapToTargets() noexcept
{
    updateTargets();

    for (auto& f : logFreq)
        f.snap (f.getTarget());

    for (auto& g : bandGain)
        g.snap (g.getTarget());

    for (auto& a : audible)
        a.snap (a.getTarget());

    outGain.snap (outGain.getTarget());
    updateFilters();
}

void MultibandProcessor::reset() noexcept
{
    for (auto& c : channels)
        c.reset();

    key.reset();
    grDb.fill (0.0f);
    meanSquare.fill (0.0f);

    for (int b = 0; b < numBands; ++b)
        setMeter (reduction1 + b, 0.0f);
}

void MultibandProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void MultibandProcessor::updateTargets() noexcept
{
    dirty = false;
    const auto f = crossovers (values.data(), sampleRate);

    for (int i = 0; i < 3; ++i)
        logFreq[(size_t) i].setTarget (std::log2 (f[(size_t) i]));

    const int soloBand = std::clamp ((int) values[solo], 0, numBands);

    for (int b = 0; b < numBands; ++b)
    {
        bandGain[(size_t) b].setTarget (dbToGain (values[(size_t) (b1Gain + 3 * b)]));
        audible[(size_t) b].setTarget (soloBand == 0 || soloBand == b + 1 ? 1.0f : 0.0f);
    }

    attackCoeff = onePoleCoeff (values[attack], sampleRate);
    releaseCoeff = onePoleCoeff (values[release], sampleRate);
    outGain.setTarget (dbToGain (values[output]));
}

void MultibandProcessor::updateFilters() noexcept
{
    const double f1 = std::exp2 ((double) logFreq[0].getCurrent());
    const double f2 = std::exp2 ((double) logFreq[1].getCurrent());
    const double f3 = std::exp2 ((double) logFreq[2].getCurrent());

    for (auto* s : { &channels[0], &channels[1], &key })
    {
        s->low.setup (f1, sampleRate);
        s->mid.setup (f2, sampleRate);
        s->high.setup (f3, sampleRate);
    }

    // A soma de um Linkwitz-Riley de 4ª ordem é o passa-tudo de 2ª ordem com Q = 0,7071 na mesma frequência.
    apLowCoeffs = SvfCoeffs::make (FilterType::allpass, f1, 0.70710678, 0.0, sampleRate);
    apHighCoeffs = SvfCoeffs::make (FilterType::allpass, f3, 0.70710678, 0.0, sampleRate);
}

void MultibandProcessor::split (Splitter& s, float x, float* bands, bool compensate) noexcept
{
    float lo, hi;
    s.mid.process (x, lo, hi);

    if (compensate)
    {
        lo = s.apHigh.process (lo, apHighCoeffs);
        hi = s.apLow.process (hi, apLowCoeffs);
    }

    s.low.process (lo, bands[0], bands[1]);
    s.high.process (hi, bands[2], bands[3]);
}

void MultibandProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        updateTargets();

    const bool external = block.sideLeft != nullptr && block.sideRight != nullptr;
    std::array<float, numBands> maxReduction {};

    for (int start = 0; start < block.numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, block.numSamples - start);

        if (logFreq[0].isSmoothing() || logFreq[1].isSmoothing() || logFreq[2].isSmoothing())
        {
            for (auto& f : logFreq)
                f.skip (n);

            updateFilters();
        }

        for (int j = start; j < start + n; ++j)
        {
            float bl[numBands], br[numBands], bk[numBands];
            split (channels[0], block.left[j], bl, true);
            split (channels[1], block.right[j], br, true);

            // O disparo só detecta: não precisa da compensação de fase.
            if (external)
                split (key, 0.5f * (block.sideLeft[j] + block.sideRight[j]), bk, false);

            float yl = 0.0f, yr = 0.0f;

            for (int b = 0; b < numBands; ++b)
            {
                const auto ub = (size_t) b;
                const float ms = external ? bk[b] * bk[b] : 0.5f * (bl[b] * bl[b] + br[b] * br[b]);
                meanSquare[ub] = ms + (meanSquare[ub] - ms) * rmsCoeff[ub];
                const float thr = values[(size_t) (b1Thr + 3 * b)];
                const float levelDb = 10.0f * std::log10 (std::max (1.0e-12f, 2.0f * meanSquare[ub]));
                const float target = thr >= -0.01f && levelDb < 0.0f ? 0.0f
                                   : compressorGainDb (levelDb, thr, values[(size_t) (b1Ratio + 3 * b)], kneeDb);

                grDb[ub] = target + (grDb[ub] - target) * (target < grDb[ub] ? attackCoeff : releaseCoeff);
                maxReduction[ub] = std::min (maxReduction[ub], grDb[ub]);

                const float g = (grDb[ub] > -1.0e-4f ? 1.0f : dbToGain (grDb[ub])) * bandGain[ub].next() * audible[ub].next();
                yl += bl[b] * g;
                yr += br[b] * g;
            }

            const float o = outGain.next();
            block.left[j] = yl * o;
            block.right[j] = yr * o;
        }
    }

    for (int b = 0; b < numBands; ++b)
        setMeter (reduction1 + b, -maxReduction[(size_t) b]);
}

} // namespace whynot::dsp
