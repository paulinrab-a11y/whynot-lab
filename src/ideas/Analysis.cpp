#include "Analysis.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace whynot::ideas
{

namespace
{
    using Histogram = std::array<double, 12>;

    double correlation (const Histogram& a, const double* b)
    {
        double ma = 0.0, mb = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            ma += a[(size_t) i];
            mb += b[i];
        }

        ma /= 12.0;
        mb /= 12.0;
        double num = 0.0, da = 0.0, db = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            const double x = a[(size_t) i] - ma, y = b[i] - mb;
            num += x * y;
            da += x * x;
            db += y * y;
        }

        return da > 0.0 && db > 0.0 ? num / std::sqrt (da * db) : 0.0;
    }

    struct Template
    {
        std::vector<int> intervals;
        double prior;   // leve preferência pelos acordes mais comuns
    };

    const std::vector<Template>& templates()
    {
        static const std::vector<Template> t = {
            { { 0, 4, 7 }, 1.0 },      { { 0, 3, 7 }, 1.0 },
            { { 0, 4, 7, 10 }, 0.98 }, { { 0, 4, 7, 11 }, 0.98 },
            { { 0, 3, 7, 10 }, 0.98 }, { { 0, 3, 6, 10 }, 0.96 },
            { { 0, 3, 6 }, 0.95 },     { { 0, 4, 8 }, 0.9 },
            { { 0, 5, 7 }, 0.93 },     { { 0, 2, 7 }, 0.92 },
            { { 0, 2, 4, 7 }, 0.95 },  { { 0, 2, 3, 7, 10 }, 0.95 },
            { { 0, 2, 4, 7, 11 }, 0.95 }, { { 0, 2, 3, 7 }, 0.94 },
        };

        return t;
    }

    struct Best
    {
        Chord chord;
        double score = -1.0;
    };

    /** `key`: acordes cujas notas estão todas no tom ganham um empurrão. Desempata linhas de uma nota
        só (Lá e Mi viram Lá menor em Lá menor, não Lá maior). */
    double scoreChord (const Histogram& w, int bassPc, int root, const Template& t, const Scale* key)
    {
        double dot = 0.0, nw = 0.0;

        for (double x : w)
            nw += x * x;

        for (int iv : t.intervals)
            dot += w[(size_t) pitchClass (root + iv)];

        if (nw <= 0.0)
            return -1.0;

        double s = dot / (std::sqrt (nw) * std::sqrt ((double) t.intervals.size())) * t.prior;
        const int rel = pitchClass (bassPc - root);

        if (key != nullptr && std::all_of (t.intervals.begin(), t.intervals.end(),
                                           [&] (int iv) { return key->contains (root + iv); }))
            s += 0.04;

        if (bassPc >= 0)
        {
            if (rel == 0)
                s += 0.08;
            else if (std::find (t.intervals.begin(), t.intervals.end(), rel) == t.intervals.end())
                s -= 0.05;
        }

        return s;
    }

    Best bestChord (const Histogram& w, int bassPc, const Scale& key)
    {
        Best best;

        for (int root = 0; root < 12; ++root)
            for (auto& t : templates())
            {
                const double s = scoreChord (w, bassPc, root, t, &key);

                if (s > best.score)
                {
                    best.score = s;
                    best.chord.root = root;
                    best.chord.intervals = t.intervals;
                    best.chord.bass = -1;
                }
            }

        if (bassPc >= 0 && bassPc != best.chord.root && best.chord.contains (bassPc))
            best.chord.bass = bassPc;

        return best;
    }

    double scoreFor (const Histogram& w, int bassPc, const Chord& c, const Scale& key)
    {
        return scoreChord (w, bassPc, c.root, { c.intervals, 1.0 }, &key);
    }
}

DetectedKey detectKey (const std::vector<GenNote>& notes)
{
    static const double major[] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minor[] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    Histogram h {};
    double total = 0.0;

    for (auto& n : notes)
    {
        const double w = std::max (1, n.length) / (double) ticksPerBeat;
        h[(size_t) pitchClass (n.key)] += w;
        total += w;
    }

    // A música costuma começar na tônica: a nota mais grave do primeiro tempo ganha peso extra.
    // Resolve casos que o perfil sozinho confunde, como "i bII" (frígio) e "i VII VI VII".
    if (! notes.empty())
    {
        int first = notes.front().start;

        for (auto& n : notes)
            first = std::min (first, n.start);

        int lowest = 1000;

        for (auto& n : notes)
            if (n.start < first + ticksPerBeat)
                lowest = std::min (lowest, n.key);

        h[(size_t) pitchClass (lowest)] += total * 0.15;
    }

    DetectedKey best;
    best.confidence = -2.0;

    for (int root = 0; root < 12; ++root)
    {
        Histogram rotated {};

        for (int i = 0; i < 12; ++i)
            rotated[(size_t) i] = h[(size_t) pitchClass (root + i)];

        const double cMaj = correlation (rotated, major);
        const double cMin = correlation (rotated, minor);

        if (cMaj > best.confidence) best = { root, true, cMaj };
        if (cMin > best.confidence) best = { root, false, cMin };
    }

    if (best.confidence < -1.0)
        best.confidence = 0.0;

    return best;
}

std::vector<DetectedChord> detectChords (const std::vector<GenNote>& notes, int totalTicks, int segment)
{
    std::vector<DetectedChord> out;
    segment = std::max (ticksPerBeat, segment);

    const auto key = detectKey (notes).scale();

    for (int t = 0; t < totalTicks; t += segment)
    {
        const int end = std::min (totalTicks, t + segment);
        Histogram w {};
        int lowest = 1000;

        for (auto& n : notes)
        {
            const int overlap = std::min (end, n.end()) - std::max (t, n.start);

            if (overlap <= 0)
                continue;

            w[(size_t) pitchClass (n.key)] += overlap / (double) ticksPerBeat * (0.5 + n.velocity / 254.0);
            lowest = std::min (lowest, n.key);
        }

        const bool empty = lowest == 1000;

        if (empty)
        {
            if (! out.empty())
                out.back().event.length = end - out.back().event.start;

            continue;
        }

        const int bassPc = pitchClass (lowest);
        auto best = bestChord (w, bassPc, key);

        // Continua o acorde anterior quando ele explica esta janela quase tão bem
        // (evita trocar de acorde por causa de uma nota de passagem).
        if (! out.empty())
        {
            const auto& prev = out.back().event.chord;
            const double prevScore = scoreFor (w, bassPc, prev, key);

            if (prevScore >= best.score - 0.04)
            {
                out.back().event.length = end - out.back().event.start;
                continue;
            }
        }

        out.push_back ({ { t, end - t, best.chord }, std::clamp (best.score, 0.0, 1.0) });
    }

    return out;
}

} // namespace whynot::ideas
