#include "PitchDetect.h"

#include <algorithm>
#include <cmath>

namespace whynot::dsp
{

namespace
{

struct Lags
{
    int minLag = 0, maxLag = 0;
    bool ok() const noexcept   { return minLag >= 2 && maxLag > minLag + 2; }
};

Lags lagsFor (double sampleRate, const PitchOptions& o)
{
    Lags l;
    const double lo = std::max (1.0, std::min (o.minHz, o.maxHz));
    const double hi = std::max (lo, std::max (o.minHz, o.maxHz));
    l.minLag = std::max (2, (int) std::floor (sampleRate / hi));
    l.maxLag = (int) std::ceil (sampleRate / lo);
    return l;
}

double rms (const float* x, std::size_t n)
{
    double sum = 0.0;

    for (std::size_t i = 0; i < n; ++i)
        sum += (double) x[i] * (double) x[i];

    return n == 0 ? 0.0 : std::sqrt (sum / (double) n);
}

} // namespace

PitchEstimate pitchFromHz (double hz, double confidence)
{
    PitchEstimate e;

    if (! std::isfinite (hz) || hz <= 0.0)
        return e;

    e.found = true;
    e.hz = hz;
    e.midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    e.key = (int) std::lround (e.midi);
    e.cents = (e.midi - (double) e.key) * 100.0;
    e.confidence = std::clamp (confidence, 0.0, 1.0);
    return e;
}

PitchEstimate detectFramePitch (const float* x, std::size_t numFrames, double sampleRate, const PitchOptions& o)
{
    if (x == nullptr || ! std::isfinite (sampleRate) || sampleRate <= 0.0)
        return {};

    auto lags = lagsFor (sampleRate, o);

    // Trecho curto: o maior período possível cai para metade do que existe.
    lags.maxLag = std::min (lags.maxLag, (int) (numFrames / 2));

    if (! lags.ok())
        return {};

    const int window = lags.maxLag;   // janela de integração = maior período procurado
    std::vector<double> d ((std::size_t) lags.maxLag + 2, 0.0);
    int lastLag = 0;   // maior τ calculado (maxLag + 1 quando cabe, para a parábola no fim da faixa)

    // Função diferença: d(τ) = Σ (x[j] − x[j+τ])²
    for (int tau = 1; tau <= lags.maxLag + 1 && window + tau <= (int) numFrames; ++tau)
    {
        lastLag = tau;
        double sum = 0.0;

        for (int j = 0; j < window; ++j)
        {
            const double diff = (double) x[j] - (double) x[j + tau];
            sum += diff * diff;
        }

        d[(std::size_t) tau] = sum;
    }

    // Diferença normalizada pela média acumulada (d'), com d'(0) = 1.
    std::vector<double> dn (d.size(), 1.0);
    double running = 0.0;

    for (std::size_t tau = 1; tau <= (std::size_t) lastLag; ++tau)
    {
        running += d[tau];
        dn[tau] = running > 0.0 ? d[tau] * (double) tau / running : 1.0;
    }

    // Primeiro vale abaixo do limiar; sem nenhum, o menor valor da faixa.
    int best = -1;

    for (int tau = lags.minLag; tau <= lags.maxLag; ++tau)
    {
        if (dn[(std::size_t) tau] < o.threshold)
        {
            while (tau + 1 <= lags.maxLag && dn[(std::size_t) tau + 1] < dn[(std::size_t) tau])
                ++tau;

            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        best = lags.minLag;

        for (int tau = lags.minLag + 1; tau <= lags.maxLag; ++tau)
            if (dn[(std::size_t) tau] < dn[(std::size_t) best])
                best = tau;
    }

    const double aperiodicity = dn[(std::size_t) best];

    if (! std::isfinite (aperiodicity) || aperiodicity > o.voicedLimit)
        return {};

    // Parábola pelos três pontos em volta do vale: período com fração de amostra.
    double period = (double) best;

    if (best > 1 && best + 1 <= lastLag)
    {
        const double a = dn[(std::size_t) best - 1], b = dn[(std::size_t) best], c = dn[(std::size_t) best + 1];
        const double denom = a - 2.0 * b + c;

        if (denom > 1.0e-12)
            period += std::clamp (0.5 * (a - c) / denom, -0.5, 0.5);
    }

    return pitchFromHz (sampleRate / period, 1.0 - aperiodicity);
}

PitchEstimate detectSamplePitch (const std::vector<std::vector<float>>& channels, double sampleRate, const PitchOptions& o)
{
    if (channels.empty() || ! std::isfinite (sampleRate) || sampleRate <= 0.0)
        return {};

    const auto lags = lagsFor (sampleRate, o);

    if (! lags.ok())
        return {};

    // Mistura dos canais em mono, com valores não finitos zerados.
    std::size_t length = 0;

    for (const auto& c : channels)
        length = std::max (length, c.size());

    std::vector<float> mono (length, 0.0f);
    const float scale = 1.0f / (float) channels.size();

    for (const auto& c : channels)
        for (std::size_t i = 0; i < c.size(); ++i)
            if (std::isfinite (c[i]))
                mono[i] += c[i] * scale;

    // Pula o ataque: começa 20 ms depois do bloco mais forte, onde o clique já passou.
    constexpr std::size_t block = 256;
    std::size_t loudest = 0;
    double loudestRms = 0.0;

    for (std::size_t start = 0; start + block <= length; start += block)
    {
        const double r = rms (mono.data() + start, block);

        if (r > loudestRms)
        {
            loudestRms = r;
            loudest = start;
        }
    }

    if (loudestRms < 1.0e-4)   // abaixo de -80 dBFS: silêncio
        return {};

    const auto frameLength = (std::size_t) lags.maxLag * 2;
    const auto hop = (std::size_t) lags.maxLag;
    std::size_t first = loudest + (std::size_t) (0.02 * sampleRate);

    if (first + frameLength > length)   // sample curto: usa o que existe
        first = length > frameLength ? length - frameLength : 0;

    struct Frame { std::size_t start; double rms; };
    std::vector<Frame> frames;
    double strongest = 0.0;

    for (std::size_t start = first; start + frameLength <= length || (frames.empty() && start == first); start += hop)
    {
        const auto n = std::min (frameLength, length - start);
        frames.push_back ({ start, rms (mono.data() + start, n) });
        strongest = std::max (strongest, frames.back().rms);

        if (start + frameLength > length)
            break;
    }

    // Só quadros com som (até -34 dB abaixo do mais forte), no máximo 24 espalhados pelo sample.
    std::vector<Frame> audible;

    for (const auto& f : frames)
        if (f.rms >= std::max (1.0e-4, strongest * 0.02))
            audible.push_back (f);

    if (audible.empty())
        return {};

    constexpr std::size_t maxFrames = 24;
    std::vector<Frame> chosen;

    if (audible.size() <= maxFrames)
        chosen = audible;
    else
        for (std::size_t i = 0; i < maxFrames; ++i)
            chosen.push_back (audible[i * (audible.size() - 1) / (maxFrames - 1)]);

    struct Voiced { double midi; double weight; };
    std::vector<Voiced> voiced;

    for (const auto& f : chosen)
    {
        const auto e = detectFramePitch (mono.data() + f.start, std::min (frameLength, length - f.start), sampleRate, o);

        if (e.found)
            voiced.push_back ({ e.midi, e.confidence });
    }

    const double voicedFraction = (double) voiced.size() / (double) chosen.size();

    if (voiced.empty() || voicedFraction < 0.5)
        return {};

    // O grupo de quadros que concorda dentro de um semitom (±50 cents) decide a nota.
    std::sort (voiced.begin(), voiced.end(), [] (const Voiced& a, const Voiced& b) { return a.midi < b.midi; });

    std::size_t bestBegin = 0, bestEnd = 0;
    double bestWeight = -1.0;

    for (std::size_t begin = 0, end = 0; begin < voiced.size(); ++begin)
    {
        end = std::max (end, begin);

        while (end < voiced.size() && voiced[end].midi - voiced[begin].midi <= 1.0)
            ++end;

        double weight = 0.0;

        for (std::size_t i = begin; i < end; ++i)
            weight += voiced[i].weight;

        if (end - begin > bestEnd - bestBegin || (end - begin == bestEnd - bestBegin && weight > bestWeight))
        {
            bestBegin = begin;
            bestEnd = end;
            bestWeight = weight;
        }
    }

    const auto count = bestEnd - bestBegin;
    const double clusterFraction = (double) count / (double) voiced.size();

    if (clusterFraction < 0.5)
        return {};

    const auto mid = bestBegin + count / 2;
    const double median = count % 2 == 1 ? voiced[mid].midi : 0.5 * (voiced[mid - 1].midi + voiced[mid].midi);
    const double meanWeight = bestWeight / (double) count;

    return pitchFromHz (440.0 * std::exp2 ((median - 69.0) / 12.0), voicedFraction * clusterFraction * meanWeight);
}

} // namespace whynot::dsp
