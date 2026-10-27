#include "NoteTune.h"
#include "Svf.h"
#include "VoicePitch.h"

#include <numeric>

namespace whynot::dsp
{

namespace
{
    double hzToMidi (double hz) noexcept   { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

    /** YIN offline: o sinal todo reduzido para ~12 kHz (busca) e o original (refino do período). */
    class OfflineYin
    {
    public:
        OfflineYin (const float* x, std::size_t n, double sr, double minHz, double maxHz)
            : full (x), count (n), rate (sr), fLow (minHz), fHigh (maxHz)
        {
            decimation = std::max (1, (int) std::lround (sr / 12000.0));
            detectRate = sr / decimation;
            tauMax = (int) std::ceil (detectRate / fLow);
            window = 2 * tauMax;

            const auto aa = SvfCoeffs::make (FilterType::lowpass, std::min (4000.0, detectRate * 0.38), 0.70710678, 0.0, sr);
            Svf a, b;
            low.reserve (n / (size_t) decimation + 1);

            for (std::size_t i = 0; i < n; ++i)
            {
                // um NaN ou infinito (WAV float corrompido) ficaria preso no filtro: vira silêncio
                const float m = b.process (a.process (std::isfinite (x[i]) ? x[i] : 0.0f, aa), aa);

                if (i % (size_t) decimation == 0)
                    low.push_back (m);
            }

            diff.assign ((size_t) tauMax + 2, 1.0f);
            rawDiff.assign ((size_t) tauMax + 2, 0.0f);
        }

        struct Frame
        {
            double hz = 0.0;
            float aperiodicity = 1.0f;
            float rms = 0.0f;
        };

        /** O quadro centrado na amostra `center` (taxa cheia). */
        Frame analyse (std::size_t center)
        {
            Frame f;
            const auto span = (std::int64_t) (window + tauMax);
            const std::int64_t c = (std::int64_t) (center / (size_t) decimation);
            const std::int64_t s0 = c - span / 2;
            auto sample = [this] (std::int64_t i) { return i >= 0 && i < (std::int64_t) low.size() ? low[(size_t) i] : 0.0f; };

            double energy = 0.0;

            for (std::int64_t j = 0; j < window; ++j)
            {
                const float v = sample (s0 + j + tauMax / 2);
                energy += (double) v * v;
            }

            f.rms = (float) std::sqrt (energy / window);

            if (! (f.rms >= 1.0e-4f))   // também NaN (WAV float corrompido): sem tom
            {
                f.rms = std::isfinite (f.rms) ? f.rms : 0.0f;
                return f;
            }

            const int tauMin = std::max (2, (int) std::floor (detectRate / fHigh));
            double running = 0.0;
            diff[0] = 1.0f;

            for (int tau = 1; tau <= tauMax; ++tau)
            {
                double d = 0.0;

                for (int j = 0; j < window; ++j)
                {
                    const float e = sample (s0 + j) - sample (s0 + j + tau);
                    d += (double) e * e;
                }

                running += d;
                rawDiff[(size_t) tau] = (float) d;
                diff[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
            }

            int found = -1;

            for (int tau = tauMin; tau <= tauMax; ++tau)
            {
                if (diff[(size_t) tau] < 0.15f)
                {
                    while (tau + 1 <= tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau])
                        ++tau;

                    found = tau;
                    break;
                }
            }

            if (found < 0)
            {
                float best = 1.0e9f;

                for (int tau = tauMin; tau <= tauMax; ++tau)
                    if (diff[(size_t) tau] < best)
                    {
                        best = diff[(size_t) tau];
                        found = tau;
                    }
            }

            if (found < 0)
                return f;

            f.aperiodicity = diff[(size_t) found];

            // Refino na taxa cheia, perto do período achado: a diferença em cada atraso inteiro e a parábola no mínimo.
            const double coarse = (double) found * decimation;
            const auto lo = std::max<std::int64_t> (2, (std::int64_t) std::floor (coarse) - decimation - 1);
            const auto hi = (std::int64_t) std::ceil (coarse) + decimation + 1;
            const std::int64_t w = (std::int64_t) window * decimation;
            const std::int64_t start = (std::int64_t) center - (w + hi) / 2;
            auto at = [this] (std::int64_t i) { return i >= 0 && i < (std::int64_t) count && std::isfinite (full[(size_t) i]) ? full[(size_t) i] : 0.0f; };
            std::vector<double> d ((size_t) (hi - lo + 1), 0.0);

            for (auto tau = lo; tau <= hi; ++tau)
            {
                double sum = 0.0;

                for (std::int64_t j = 0; j < w; ++j)
                {
                    const float e = at (start + j) - at (start + j + tau);
                    sum += (double) e * e;
                }

                d[(size_t) (tau - lo)] = sum;
            }

            size_t best = 0;

            for (size_t k = 1; k < d.size(); ++k)
                if (d[k] < d[best])
                    best = k;

            double period = (double) lo + (double) best;

            if (best > 0 && best + 1 < d.size())
            {
                const double a = d[best - 1], b = d[best], cc = d[best + 1];
                const double den = a - 2.0 * b + cc;

                if (std::abs (den) > 1.0e-12)
                    period += std::clamp (0.5 * (a - cc) / den, -1.0, 1.0);
            }

            if (period >= 2.0 && std::isfinite (period))
                f.hz = rate / period;

            return f;
        }

    private:
        const float* full;
        std::size_t count;
        double rate, fLow, fHigh, detectRate = 12000.0;
        int decimation = 4, tauMax = 200, window = 400;
        std::vector<float> low, diff, rawDiff;
    };

    float median (std::vector<float> v)
    {
        if (v.empty())
            return 0.0f;

        const auto mid = v.begin() + (std::ptrdiff_t) (v.size() / 2);
        std::nth_element (v.begin(), mid, v.end());
        return *mid;
    }

    /** Média móvel de `values` só nos quadros marcados, numa janela de `radius` quadros para cada lado. */
    std::vector<float> movingAverage (const std::vector<float>& values, const std::vector<char>& valid, int radius)
    {
        const size_t n = values.size();
        std::vector<double> sum (n + 1, 0.0), cnt (n + 1, 0.0);

        for (size_t k = 0; k < n; ++k)
        {
            sum[k + 1] = sum[k] + (valid[k] ? values[k] : 0.0);
            cnt[k + 1] = cnt[k] + (valid[k] ? 1.0 : 0.0);
        }

        std::vector<float> out (n, 0.0f);

        for (size_t k = 0; k < n; ++k)
        {
            const size_t a = k >= (size_t) radius ? k - (size_t) radius : 0, b = std::min (n, k + (size_t) radius + 1);
            const double c = cnt[b] - cnt[a];
            out[k] = c > 0.0 ? (float) ((sum[b] - sum[a]) / c) : 0.0f;
        }

        return out;
    }

    /** O desvio de cada quadro da nota em volta de `centre`, separado em lento (média de 150 ms) e rápido (o resto). */
    void splitDeviation (const PitchCurve& curve, size_t from, size_t to, float centre, std::vector<float>& slow, std::vector<float>& fast)
    {
        const size_t n = to > from ? to - from : 0;
        std::vector<float> dev (n, 0.0f);
        std::vector<char> valid (n, 0);

        for (size_t k = 0; k < n; ++k)
            if (curve.voiced (from + k))
            {
                dev[k] = curve.midi[from + k] - centre;
                valid[k] = 1;
            }

        const int radius = std::max (1, (int) std::lround (0.075 / curve.hopSeconds));
        slow = movingAverage (dev, valid, radius);
        fast.assign (n, 0.0f);

        for (size_t k = 0; k < n; ++k)
            fast[k] = valid[k] ? dev[k] - slow[k] : 0.0f;
    }

    size_t frameOf (const PitchCurve& c, double seconds) noexcept
    {
        return (size_t) std::max (0.0, std::round (seconds / c.hopSeconds));
    }
}

//==============================================================================
float PitchCurve::at (double t) const noexcept
{
    if (midi.empty() || t < 0.0)
        return 0.0f;

    const double pos = t / hopSeconds;
    const auto k = (size_t) pos;

    if (k + 1 >= midi.size())
        return k < midi.size() ? midi[k] : 0.0f;

    const float a = midi[k], b = midi[k + 1], frac = (float) (pos - (double) k);

    if (a > 0.0f && b > 0.0f)
        return a + frac * (b - a);

    return frac < 0.5f ? a : b;
}

NoteAnalysis analyseNotes (const float* mono, std::size_t numFrames, double sampleRate, const NoteAnalysisOptions& options)
{
    NoteAnalysis result;
    result.sampleRate = sampleRate;
    result.numFrames = numFrames;
    auto& curve = result.curve;
    curve.hopSeconds = options.hopSeconds;

    if (mono == nullptr || numFrames == 0 || sampleRate <= 0.0)
        return result;

    const double hopSamples = options.hopSeconds * sampleRate;
    const size_t frames = (size_t) std::ceil ((double) numFrames / hopSamples);
    curve.midi.assign (frames, 0.0f);
    curve.level.assign (frames, 0.0f);
    std::vector<float> aperiodic (frames, 1.0f);

    OfflineYin yin (mono, numFrames, sampleRate, options.minHz, options.maxHz);
    float loudest = 0.0f;

    for (size_t k = 0; k < frames; ++k)
    {
        if (options.cancel != nullptr && (k & 255) == 0 && options.cancel->load())
        {
            result = {};
            result.cancelled = true;
            return result;
        }

        const auto f = yin.analyse ((size_t) std::llround ((double) k * hopSamples));
        curve.level[k] = f.rms;
        aperiodic[k] = f.aperiodicity;
        loudest = std::max (loudest, f.rms);

        if (f.hz > 0.0)
            curve.midi[k] = (float) hzToMidi (f.hz);
    }

    // Com tom: periódico o bastante e a menos de 35 dB do quadro mais alto (e acima de ~-60 dBFS).
    const float floorLevel = std::max (0.001f, loudest * 0.018f);

    for (size_t k = 0; k < frames; ++k)
        if (aperiodic[k] > 0.25f || curve.level[k] < floorLevel)
            curve.midi[k] = 0.0f;

    // Oitava errada: um quadro a ~12 semitons da mediana dos vizinhos volta para a oitava deles.
    {
        auto copy = curve.midi;

        for (size_t k = 0; k < frames; ++k)
        {
            if (copy[k] <= 0.0f)
                continue;

            std::vector<float> around;

            for (size_t j = k >= 5 ? k - 5 : 0; j < std::min (frames, k + 6); ++j)
                if (copy[j] > 0.0f)
                    around.push_back (copy[j]);

            const float m = median (around);
            const float d = copy[k] - m;

            if (std::abs (std::abs (d) - 12.0f) < 1.5f)
                curve.midi[k] = copy[k] - 12.0f * (d > 0.0f ? 1.0f : -1.0f);
        }
    }

    // Mediana de 5 quadros (só entre quadros com tom) e trechos com tom de menos de 15 ms saem.
    {
        auto copy = curve.midi;

        for (size_t k = 0; k < frames; ++k)
        {
            if (copy[k] <= 0.0f)
                continue;

            std::vector<float> around;

            for (size_t j = k >= 2 ? k - 2 : 0; j < std::min (frames, k + 3); ++j)
                if (copy[j] > 0.0f)
                    around.push_back (copy[j]);

            curve.midi[k] = median (around);
        }

        for (size_t k = 0; k < frames;)
        {
            if (curve.midi[k] <= 0.0f) { ++k; continue; }

            size_t e = k;

            while (e < frames && curve.midi[e] > 0.0f)
                ++e;

            if (e - k < 3)
                std::fill (curve.midi.begin() + (std::ptrdiff_t) k, curve.midi.begin() + (std::ptrdiff_t) e, 0.0f);

            k = e;
        }
    }

    //==========================================================================
    // As notas: trechos com tom (buracos de até 30 ms não separam) cortados onde a média de 80 ms sai da nota.
    const int bridge = std::max (1, (int) std::lround (0.03 / options.hopSeconds));
    const int avgRadius = std::max (1, (int) std::lround (0.04 / options.hopSeconds));
    const int sustain = std::max (2, (int) std::lround (0.04 / options.hopSeconds));
    const int minFrames = std::max (2, (int) std::lround (options.minNoteSeconds / options.hopSeconds));

    std::vector<char> valid (frames);

    for (size_t k = 0; k < frames; ++k)
        valid[k] = curve.midi[k] > 0.0f;

    const auto avg = movingAverage (curve.midi, valid, avgRadius);

    struct Span { size_t from, to; };   // quadros [from, to)
    std::vector<Span> spans;

    for (size_t k = 0; k < frames;)
    {
        if (! valid[k]) { ++k; continue; }

        // o trecho com tom, emendando buracos curtos
        size_t e = k, lastVoiced = k;

        while (e < frames)
        {
            if (valid[e])
                lastVoiced = e;
            else if (e - lastVoiced > (size_t) bridge)
                break;

            ++e;
        }

        const size_t runEnd = lastVoiced + 1;
        size_t noteStart = k;
        double sum = 0.0;
        int count = 0, outside = 0;

        for (size_t j = k; j < runEnd; ++j)
        {
            if (! valid[j])
                continue;

            const float mean = count > 0 ? (float) (sum / count) : curve.midi[j];

            if (count >= minFrames && std::abs (avg[j] - mean) > options.splitSemitones)
            {
                if (++outside >= sustain)
                {
                    // corta onde o tom passou da metade do caminho para fora da nota
                    size_t cut = j;

                    while (cut > noteStart + (size_t) minFrames && (! valid[cut - 1] || std::abs (curve.midi[cut - 1] - mean) > options.splitSemitones * 0.5f))
                        --cut;

                    spans.push_back ({ noteStart, cut });
                    noteStart = cut;
                    sum = 0.0;
                    count = 0;
                    outside = 0;

                    for (size_t q = cut; q <= j; ++q)
                        if (valid[q]) { sum += curve.midi[q]; ++count; }

                    continue;
                }
            }
            else
            {
                outside = 0;
            }

            sum += curve.midi[j];
            ++count;
        }

        spans.push_back ({ noteStart, runEnd });
        k = runEnd;
    }

    // Notas curtas demais: juntam com a vizinha encostada de tom mais perto; sozinhas e curtíssimas, saem.
    auto spanMedian = [&] (const Span& s)
    {
        std::vector<float> v;

        for (size_t j = s.from; j < s.to; ++j)
            if (valid[j])
                v.push_back (curve.midi[j]);

        return median (v);
    };

    for (bool changed = true; changed;)
    {
        changed = false;

        for (size_t i = 0; i < spans.size(); ++i)
        {
            if ((int) (spans[i].to - spans[i].from) >= minFrames)
                continue;

            const bool left = i > 0 && spans[i - 1].to == spans[i].from;
            const bool right = i + 1 < spans.size() && spans[i + 1].from == spans[i].to;

            if (! left && ! right)
            {
                if ((int) (spans[i].to - spans[i].from) < minFrames / 2)
                {
                    spans.erase (spans.begin() + (std::ptrdiff_t) i);
                    changed = true;
                    break;
                }

                continue;
            }

            const float m = spanMedian (spans[i]);
            const bool toLeft = left && (! right || std::abs (spanMedian (spans[i - 1]) - m) <= std::abs (spanMedian (spans[i + 1]) - m));

            if (toLeft)
                spans[i - 1].to = spans[i].to;
            else
                spans[i + 1].from = spans[i].from;

            spans.erase (spans.begin() + (std::ptrdiff_t) i);
            changed = true;
            break;
        }
    }

    for (auto& s : spans)
    {
        // a mediana do miolo (sem 15% de cada ponta: a subida e a descida até a nota)
        const size_t len = s.to - s.from, trim = len * 15 / 100;
        std::vector<float> v;

        for (size_t j = s.from + trim; j < s.to - trim; ++j)
            if (valid[j])
                v.push_back (curve.midi[j]);

        if (v.empty())
            continue;

        NoteSegment seg;
        seg.start = std::max (0.0, ((double) s.from - 0.5) * options.hopSeconds);
        seg.end = ((double) s.to - 0.5) * options.hopSeconds;
        seg.midi = median (v);

        std::vector<float> slow, fast;
        splitDeviation (curve, s.from, s.to, seg.midi, slow, fast);
        double slowSq = 0.0, fastSq = 0.0;
        int n = 0;

        for (size_t j = trim; j + trim < slow.size(); ++j)   // só o miolo: a subida até a nota não é vibrato
            if (valid[s.from + j])
            {
                slowSq += (double) slow[j] * slow[j];
                fastSq += (double) fast[j] * fast[j];
                ++n;
            }

        seg.driftCents = n > 0 ? (float) (100.0 * std::sqrt (slowSq / n)) : 0.0f;
        seg.vibratoCents = n > 0 ? (float) (100.0 * std::sqrt (2.0 * fastSq / n)) : 0.0f;
        result.notes.push_back (seg);
    }

    // notas encostadas: a fronteira é a mesma (a segunda começa onde a primeira acaba)
    for (size_t i = 1; i < result.notes.size(); ++i)
        result.notes[i].start = std::max (result.notes[i].start, result.notes[i - 1].end);

    return result;
}

std::vector<TunedNote> notesFromAnalysis (const NoteAnalysis& a)
{
    std::vector<TunedNote> notes;

    for (auto& s : a.notes)
    {
        TunedNote n;
        n.start = s.start;
        n.end = s.end;
        n.sourceMidi = s.midi;
        notes.push_back (n);
    }

    return notes;
}

//==============================================================================
Correction correctionFor (const PitchCurve& curve, const std::vector<TunedNote>& notes, bool formantFollows)
{
    const size_t frames = curve.midi.size();
    Correction c;
    c.semitones.assign (frames, 0.0f);
    c.formant.assign (frames, 1.0f);
    std::vector<float> formantSt (frames, 0.0f);

    float previousSemis = 0.0f, previousFormant = 0.0f;
    double previousEnd = -1.0;

    for (const auto& n : notes)
    {
        const size_t from = std::min (frames, frameOf (curve, n.start)), to = std::min (frames, frameOf (curve, n.end));

        if (to <= from)
            continue;

        const bool adjacent = previousEnd >= 0.0 && n.start - previousEnd < 0.03;

        if (! n.edited() && ! (adjacent && (previousSemis != 0.0f || previousFormant != 0.0f)))
        {
            previousSemis = previousFormant = 0.0f;
            previousEnd = n.end;
            continue;   // nada muda: a correção fica em zero (o trecho sai igual ao original)
        }

        std::vector<float> slow, fast;
        splitDeviation (curve, from, to, n.sourceMidi, slow, fast);
        const float centred = n.targetMidi() - n.sourceMidi;
        float held = centred;
        bool any = false;

        for (size_t k = from; k < to; ++k)
        {
            if (curve.voiced (k))
            {
                held = n.edited() ? centred - n.center * slow[k - from] - n.flatten * fast[k - from] : 0.0f;
                any = true;
            }

            c.semitones[k] = held;   // sem tom no meio da nota: segura a última correção (o PSOLA não muda o tom ali)
            formantSt[k] = n.formant;
        }

        // o começo antes do primeiro quadro com tom recebe a primeira correção
        if (any)
            for (size_t k = from; k < to && ! curve.voiced (k); ++k)
                for (size_t q = k; q < to; ++q)
                    if (curve.voiced (q)) { c.semitones[k] = c.semitones[q]; break; }

        // a transição: a correção entra em rampa a partir da da nota anterior encostada
        if (adjacent && n.transitionMs > 0.0f)
        {
            const double t = n.transitionMs * 0.001;

            for (size_t k = from; k < to; ++k)
            {
                const double x = ((double) k * curve.hopSeconds - n.start) / t;

                if (x >= 1.0)
                    break;

                const float w = (float) std::clamp (x, 0.0, 1.0);
                c.semitones[k] = previousSemis + w * (c.semitones[k] - previousSemis);
                formantSt[k] = previousFormant + w * (formantSt[k] - previousFormant);
            }
        }

        previousSemis = c.semitones[to - 1];
        previousFormant = formantSt[to - 1];
        previousEnd = n.end;
    }

    for (size_t k = 0; k < frames; ++k)
        c.formant[k] = std::exp2 (std::clamp (formantSt[k] + (formantFollows ? c.semitones[k] : 0.0f), -12.0f, 12.0f) / 12.0f);

    return c;
}

std::vector<float> correctedCurve (const PitchCurve& curve, const Correction& c)
{
    std::vector<float> out (curve.midi.size(), 0.0f);

    for (size_t k = 0; k < out.size(); ++k)
        if (curve.voiced (k))
            out[k] = curve.midi[k] + (k < c.semitones.size() ? c.semitones[k] : 0.0f);

    return out;
}

//==============================================================================
std::vector<std::vector<float>> renderNotes (const std::vector<std::vector<float>>& channels, double sampleRate,
                                             const PitchCurve& curve, const std::vector<TunedNote>& notes, bool formantFollows,
                                             NoteRenderStats* stats)
{
    auto out = channels;

    if (stats != nullptr)
        *stats = {};

    if (channels.empty() || channels[0].empty() || sampleRate <= 0.0 || curve.midi.empty() || ! notetune::anyEdited (notes))
        return out;

    const DenormalGuard denormals;   // os grãos do PSOLA decaem até números desnormais nas bordas da janela
    const auto correction = correctionFor (curve, notes, formantFollows);
    const size_t n = channels[0].size(), frames = curve.midi.size();
    const double hopSamples = curve.hopSeconds * sampleRate;

    // os quadros com correção viram trechos de amostras, com folga, emendados quando ficam perto
    struct Region { std::int64_t a, b; };
    std::vector<Region> regions;
    const auto pad = (std::int64_t) std::llround (0.025 * sampleRate);
    const auto join = (std::int64_t) std::llround (0.1 * sampleRate);

    for (size_t k = 0; k < frames; ++k)
    {
        if (std::abs (correction.semitones[k]) < 1.0e-6f && std::abs (correction.formant[k] - 1.0f) < 1.0e-6f)
            continue;

        const auto a = std::max<std::int64_t> (0, (std::int64_t) std::floor (((double) k - 1.0) * hopSamples) - pad);
        const auto b = std::min<std::int64_t> ((std::int64_t) n, (std::int64_t) std::ceil (((double) k + 1.0) * hopSamples) + pad);

        if (! regions.empty() && a <= regions.back().b + join)
            regions.back().b = std::max (regions.back().b, b);
        else
            regions.push_back ({ a, b });
    }

    auto valueAt = [&] (const std::vector<float>& v, double samplePos, float outside)
    {
        const double pos = samplePos / hopSamples;

        if (pos < 0.0)
            return v.empty() ? outside : v.front();

        const auto k = (size_t) pos;

        if (k + 1 >= v.size())
            return k < v.size() ? v[k] : outside;

        const float frac = (float) (pos - (double) k);
        return v[k] + frac * (v[k + 1] - v[k]);
    };

    const bool stereo = channels.size() > 1;
    PsolaCore core;
    core.prepare (sampleRate, 1, 2.0f);
    const int latency = core.latency();
    const double ready = 1.5 * (sampleRate / 60.0) + 2.0;   // o grão é montado tanto assim depois da posição dele
    const auto preroll = (std::int64_t) std::llround (0.15 * sampleRate);
    const auto fade = std::max<std::int64_t> (1, (std::int64_t) std::llround (0.01 * sampleRate));
    float wet[PsolaCore::maxReads] {}, wetR[PsolaCore::maxReads] {};

    for (const auto& r : regions)
    {
        core.reset();
        const auto from = std::max<std::int64_t> (0, r.a - preroll);
        const auto until = r.b + latency;

        for (auto i = from; i < until; ++i)
        {
            const double synthesisAt = (double) i - ready;
            const float semis = valueAt (correction.semitones, synthesisAt, 0.0f);
            const float formant = valueAt (correction.formant, synthesisAt, 1.0f);
            core.setShift (0, std::exp2 (semis / 12.0f), formant, false);

            // o PSOLA tem estado: um NaN do arquivo calaria o resto do trecho; ele entra como silêncio
            const bool inside = i < (std::int64_t) n;
            auto clean = [] (float v) { return std::isfinite (v) ? v : 0.0f; };
            const float l = inside ? clean (channels[0][(size_t) i]) : 0.0f;
            const float rr = inside && stereo ? clean (channels[1][(size_t) i]) : l;
            float dryL = 0.0f, dryR = 0.0f;
            core.process (l, rr, wet, wetR, dryL, dryR);

            const auto o = i - latency;

            if (o < r.a || o >= r.b)
                continue;

            // crossfade de 10 ms nas bordas (lá a correção já é zero e o PSOLA está em fase com a entrada)
            const auto edge = std::min (o - r.a, r.b - 1 - o);
            const float w = edge >= fade ? 1.0f : (float) (0.5 - 0.5 * std::cos (pi * (double) edge / (double) fade));
            const auto at = (size_t) o;
            out[0][at] = channels[0][at] + w * (wet[0] - channels[0][at]);

            if (stereo)
                out[1][at] = channels[1][at] + w * (wetR[0] - channels[1][at]);
        }

        if (stats != nullptr)
        {
            ++stats->regions;
            stats->renderedFrames += (size_t) (r.b - r.a);
        }
    }

    return out;
}

double measureFundamental (const float* x, std::size_t numFrames, double sampleRate, std::size_t from, std::size_t to)
{
    if (x == nullptr || to <= from || to > numFrames)
        return 0.0;

    OfflineYin yin (x, numFrames, sampleRate, 60.0, 1000.0);
    std::vector<float> hz;
    const auto hop = (size_t) std::max (1.0, 0.005 * sampleRate);

    for (size_t c = from; c < to; c += hop)
        if (const auto f = yin.analyse (c); f.hz > 0.0 && f.aperiodicity < 0.25f)
            hz.push_back ((float) f.hz);

    return hz.empty() ? 0.0 : (double) median (hz);
}

} // namespace whynot::dsp
