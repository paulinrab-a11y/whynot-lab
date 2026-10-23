#include "Generator.h"

#include "Random.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace whynot::ideas
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    const GenreProfile& genreOf (const Request& r)
    {
        auto* g = findGenre (r.genre);
        return g != nullptr ? *g : allGenres()[0];
    }

    Mood moodOf (const Request& r, const GenreProfile& g)
    {
        return r.mood.value_or (g.defaultMood);
    }

    int clampVelocity (double v)
    {
        return std::clamp ((int) std::lround (v), 1, 127);
    }

    int moodVelocity (Mood m)
    {
        return m == Mood::epic ? 6 : m == Mood::chill ? -7 : m == Mood::sad ? -3 : 0;
    }

    /** Peso de um modelo pela distância entre a densidade dele e a pedida. */
    double densityWeight (float itemDensity, double wanted)
    {
        const double x = itemDensity - wanted;
        return std::exp (-x * x / 0.05) + 0.02;
    }

    template <typename T>
    size_t chooseByDensity (const std::vector<T>& items, double density, Random& rng)
    {
        std::vector<double> w;

        for (auto& it : items)
            w.push_back (densityWeight (it.density, density));

        return rng.weighted (w);
    }

    Random partRandom (const Request& r)
    {
        return Random (mixSeed (mixSeed (r.seed, 100 + (uint64_t) r.part), r.variation));
    }

    /** Uma nota da classe `pc` entre low e high, a mais perto de `ref`. */
    int nearestKey (int ref, int pc, int low, int high)
    {
        int best = -1;

        for (int k = low; k <= high; ++k)
            if (pitchClass (k) == pitchClass (pc) && (best < 0 || std::abs (k - ref) < std::abs (best - ref)))
                best = k;

        if (best < 0)   // faixa menor que uma oitava sem a classe: a mais perto fora dela
        {
            best = ref - pitchClass (ref - pc);

            if (ref - best > 6)
                best += 12;
        }

        return best;
    }

    const ChordEvent* chordAt (const std::vector<ChordEvent>& chords, int t)
    {
        for (auto& e : chords)
            if (t >= e.start && t < e.end())
                return &e;

        return chords.empty() ? nullptr : (t < chords.front().start ? &chords.front() : &chords.back());
    }

    int chordIndexAt (const std::vector<ChordEvent>& chords, int t)
    {
        for (size_t i = 0; i < chords.size(); ++i)
            if (t >= chords[i].start && t < chords[i].end())
                return (int) i;

        return -1;
    }

    //==========================================================================
    struct Hit
    {
        int start = 0, length = 0;
        char symbol = 'x';
    };

    /** Os ataques de uma grade (16 ou 32 caracteres) repetida ao longo de `bars` compassos. */
    std::vector<Hit> gridHits (const char* grid, int bars)
    {
        std::vector<Hit> hits;
        const std::string_view g (grid);

        if (g.empty())
            return hits;

        const int gridSteps = (int) g.size();
        const int gridBars = std::max (1, gridSteps / 16);

        for (int bar = 0; bar < bars; bar += gridBars)
        {
            for (int p = 0; p < gridSteps; ++p)
            {
                const char c = g[(size_t) p];

                if (c == '.' || c == '-')
                    continue;

                const int absBar = bar + p / 16;

                if (absBar >= bars)
                    break;

                int len = 1;

                while (p + len < gridSteps && g[(size_t) (p + len)] == '-')
                    ++len;

                const int start = absBar * ticksPerBar + (p % 16) * ticksPerStep;
                const int end = std::min (start + len * ticksPerStep, bars * ticksPerBar);
                hits.push_back ({ start, end - start, c });
            }
        }

        return hits;
    }

    void sortNotes (std::vector<GenNote>& notes)
    {
        std::stable_sort (notes.begin(), notes.end(), [] (const GenNote& a, const GenNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.key < b.key;
        });
    }

    /** A sétima e a nona que pertencem à escala (a "diatônica"). Acorde menor sem sétima menor
        na escala (o i da menor harmônica) fica em tríade: m(maj7) é exótico demais para sair sozinho. */
    void addDiatonicExtensions (Chord& c, const Scale& scale, bool seventh, bool ninth)
    {
        if (c.intervals.size() != 3)
            return;

        if (seventh)
        {
            if (c.isDiminished())                                   c.intervals.push_back (10);
            else if (scale.contains (c.root + 10))                  c.intervals.push_back (10);
            else if (c.isMinor())                                   {}
            else if (scale.contains (c.root + 11))                  c.intervals.push_back (11);
            else                                                    c.intervals.push_back (11);
        }

        if (ninth && ! c.isDiminished() && scale.contains (c.root + 2))
            c.intervals.push_back (2);

        std::sort (c.intervals.begin(), c.intervals.end());
        c.intervals.erase (std::unique (c.intervals.begin(), c.intervals.end()), c.intervals.end());
    }

    //==========================================================================
    // Acordes
    void makeChords (const Request& r, const GenreProfile& g, const std::vector<ChordEvent>& chords,
                     Random& rng, std::vector<GenNote>& out)
    {
        const auto& rhythm = g.chordRhythms[chooseByDensity (g.chordRhythms, r.density, rng)];
        const int shift = 12 * r.octave;
        const int low = g.chordLow + shift, high = g.chordHigh + shift;
        const auto mood = moodOf (r, g);

        std::vector<std::vector<int>> voicings;
        std::vector<int> prev;

        for (auto& e : chords)
        {
            auto v = voiceChord (e.chord, prev, low, high, g.chordMaxNotes, g.spreadVoicing);
            voicings.push_back (v);
            prev = v;
        }

        for (auto& hit : gridHits (rhythm.grid, r.bars))
        {
            int s = hit.start;
            const int e = hit.start + hit.length;

            while (s < e)
            {
                const int idx = chordIndexAt (chords, s);

                if (idx < 0)
                    break;

                const auto& ev = chords[(size_t) idx];
                const int pieceEnd = std::min (e, ev.end());
                const auto& v = voicings[(size_t) idx];
                const bool barStart = s % ticksPerBar == 0;
                const int len = std::max (60, pieceEnd - s - 12);

                // Mais vozes, menos força em cada uma: o acorde soma perto do nível de uma melodia
                // (sem isso, 5 notas juntas saem uns 7 dB acima e clipam o master).
                const int voices = (int) v.size() + (g.rootBelow ? 1 : 0);
                const double perVoice = std::sqrt (3.0 / std::max (3, voices));
                // Acorde é acompanhamento: fica uns 3 dB abaixo da melodia.
                const double base = (76 + (barStart ? 8 : 0) + moodVelocity (mood) + rng.between (-4, 4)) * perVoice;

                for (size_t k = 0; k < v.size(); ++k)
                    out.push_back ({ s, len, v[k], clampVelocity (base + (k + 1 == v.size() ? 4 : 0)), false });

                if (g.rootBelow && ! v.empty())
                {
                    int bassKey = v.front() - 3;

                    while (pitchClass (bassKey) != pitchClass (ev.chord.root))
                        --bassKey;

                    if (bassKey >= 33 + shift)
                        out.push_back ({ s, len, bassKey, clampVelocity (base - 6), false });
                }

                s = pieceEnd;
            }
        }
    }

    //==========================================================================
    // Melodia
    struct Slot
    {
        int start = 0, length = 0;
        bool strong = false;
    };

    void appendCell (Cell cell, int beatStart, std::vector<Slot>& slots)
    {
        auto add = [&] (int off, int len) { slots.push_back ({ beatStart + off, len, false }); };

        switch (cell)
        {
            case Cell::quarter:             add (0, 960); break;
            case Cell::eighths:             add (0, 480); add (480, 480); break;
            case Cell::sixteenths:          add (0, 240); add (240, 240); add (480, 240); add (720, 240); break;
            case Cell::eighthTwoSixteenths: add (0, 480); add (480, 240); add (720, 240); break;
            case Cell::twoSixteenthsEighth: add (0, 240); add (240, 240); add (480, 480); break;
            case Cell::dotted:              add (0, 720); add (720, 240); break;
            case Cell::snap:                add (0, 240); add (240, 720); break;
            case Cell::offEighth:           add (480, 480); break;
            case Cell::offSixteenths:       add (240, 240); add (720, 240); break;
            case Cell::triplet:             add (0, 320); add (320, 320); add (640, 320); break;
            case Cell::rest:
            case Cell::hold:                break;
        }
    }

    std::vector<Slot> melodyRhythm (const GenreProfile& g, double density, int bars, Random& rng)
    {
        std::vector<double> w ((size_t) numCells);

        for (int i = 0; i < numCells; ++i)
            w[(size_t) i] = g.cellsSparse.w[i] * (1.0 - density) + g.cellsBusy.w[i] * density;

        std::vector<Slot> slots;

        for (int beat = 0; beat < bars * 4; ++beat)
        {
            auto cell = (Cell) rng.weighted (w);

            // O motivo começa com nota no tempo 1.
            if (beat == 0)
                for (int tries = 0; tries < 8 && (cell == Cell::rest || cell == Cell::hold
                                                  || cell == Cell::offEighth || cell == Cell::offSixteenths); ++tries)
                    cell = (Cell) rng.weighted (w);

            if (beat == 0 && (cell == Cell::rest || cell == Cell::hold || cell == Cell::offEighth || cell == Cell::offSixteenths))
                cell = Cell::quarter;

            const int beatStart = beat * ticksPerBeat;

            if (cell == Cell::hold)
            {
                if (! slots.empty() && slots.back().start + slots.back().length == beatStart)
                    slots.back().length += ticksPerBeat;

                continue;
            }

            appendCell (cell, beatStart, slots);
        }

        // Pelo menos duas notas por compasso: senão não dá para reconhecer uma melodia.
        for (int bar = 0; bar < bars; ++bar)
        {
            const int s = bar * ticksPerBar, e = s + ticksPerBar;
            const auto count = std::count_if (slots.begin(), slots.end(), [&] (const Slot& x) { return x.start >= s && x.start < e; });

            if (count < 2)
                for (int beat : { 0, 2 })
                {
                    const int t = s + beat * ticksPerBeat;
                    const bool taken = std::any_of (slots.begin(), slots.end(), [&] (const Slot& x) { return t >= x.start && t < x.start + x.length; });

                    if (! taken)
                        slots.push_back ({ t, ticksPerBeat, false });
                }
        }

        std::sort (slots.begin(), slots.end(), [] (const Slot& a, const Slot& b) { return a.start < b.start; });

        for (auto& s : slots)
        {
            const int inBeat = s.start % ticksPerBeat;
            const int beat = (s.start / ticksPerBeat) % 4;
            s.strong = (inBeat == 0 && beat % 2 == 0) || s.length >= 720;
        }

        return slots;
    }

    struct MelodyContext
    {
        const GenreProfile& g;
        const Scale& scale;
        Mood mood;
        int low, high;
    };

    /** Na faixa e na escala; fora da escala, só a tríade do acorde (a terça maior do V na
        menor, por exemplo), nunca uma extensão que brigue com a escala. */
    bool allowed (const MelodyContext& m, const Chord& chord, int key)
    {
        if (key < m.low || key > m.high)
            return false;

        if (m.scale.contains (key))
            return true;

        const int rel = pitchClass (key - chord.root);
        return chord.contains (key) && (rel == 0 || rel == 3 || rel == 4 || rel == 6 || rel == 7 || rel == 8);
    }

    /** Alvo da linha melódica (contorno) na posição x (0..1) da frase. */
    double contourTarget (const MelodyContext& m, double x)
    {
        double f = 0.5;

        switch (m.mood)
        {
            case Mood::happy:
            case Mood::epic:  f = 0.35 + 0.4 * std::sin (pi * x); break;
            case Mood::sad:   f = 0.7 - 0.4 * x; break;
            case Mood::dark:  f = 0.3 + 0.25 * std::sin (pi * x); break;
            case Mood::chill: f = 0.45 + 0.15 * std::sin (pi * x); break;
        }

        return m.low + (m.high - m.low) * f;
    }

    int choosePitch (const MelodyContext& m, const Chord& chord, const Slot& slot, int prev, int prevPrev,
                     double x, Random& rng)
    {
        const double target = contourTarget (m, x);
        const double bias = m.g.chordToneBias;
        const int maxRepeats = m.g.repeatWeight > 0.3f ? 3 : 2;
        std::vector<int> keys;
        std::vector<double> w;

        for (int k = m.low; k <= m.high; ++k)
        {
            if (! allowed (m, chord, k))
                continue;

            double weight = 1.0;
            const bool tone = chord.contains (k);

            if (slot.strong)  weight *= tone ? 1.0 + 6.0 * bias : (1.0 - bias) * 0.6;
            else              weight *= tone ? 1.0 + bias : 1.0;

            const int d = k - prev;
            const int ad = std::abs (d);

            if (ad == 0)            weight *= m.g.repeatWeight * 3.0;
            else if (ad <= 2)       weight *= 1.0;
            else if (ad <= 4)       weight *= 0.6;
            else if (ad <= 7)       weight *= m.g.leapWeight * 1.2;
            else if (ad == 12)      weight *= m.g.leapWeight * 0.8;
            else                    weight *= m.g.leapWeight * 0.15;

            weight *= std::exp (-std::abs (k - target) / 6.0);

            if ((d > 0 && target > prev) || (d < 0 && target < prev))
                weight *= 1.3;

            if (k == prev && k == prevPrev && maxRepeats <= 2)
                weight *= 0.05;

            if (m.mood == Mood::dark && pitchClass (k - m.scale.root) == 1)
                weight *= 1.5;   // a segunda menor do frígio, quando a escala tem

            if (m.mood == Mood::sad && d < 0)
                weight *= 1.2;

            keys.push_back (k);
            w.push_back (weight);
        }

        if (keys.empty())
            return std::clamp (prev, m.low, m.high);

        return keys[rng.weighted (w)];
    }

    /** Começo de frase: um som do acorde perto do centro da faixa. */
    int openingPitch (const MelodyContext& m, const Chord& chord, Random& rng)
    {
        const double centre = (m.low + m.high) * 0.5 + (m.mood == Mood::dark ? -2 : m.mood == Mood::happy ? 2 : 0);
        std::vector<int> tones;

        for (int k = m.low; k <= m.high; ++k)
            if (chord.contains (k) && allowed (m, chord, k))
                tones.push_back (k);

        if (tones.empty())
            return m.scale.snap ((m.low + m.high) / 2);

        std::sort (tones.begin(), tones.end(), [&] (int a, int b) { return std::abs (a - centre) < std::abs (b - centre); });
        return tones[(size_t) rng.between (0, std::min (2, (int) tones.size() - 1))];
    }

    /** A nota mais perto de `key` que soa bem sobre o acorde novo (para repetir um motivo). */
    int adaptToChord (const MelodyContext& m, const Chord& oldChord, const Chord& newChord, int key)
    {
        if (oldChord.contains (key) && ! newChord.contains (key))
        {
            for (int d = 0; d <= 3; ++d)
            {
                if (allowed (m, newChord, key - d) && newChord.contains (key - d)) return key - d;
                if (allowed (m, newChord, key + d) && newChord.contains (key + d)) return key + d;
            }
        }

        if (allowed (m, newChord, key))
            return key;

        return std::clamp (m.scale.snap (key), m.low, m.high);
    }

    struct MotifNote
    {
        int offset = 0, length = 0, key = 60;
        bool strong = false;
    };

    std::vector<MotifNote> newMotif (const MelodyContext& m, const std::vector<ChordEvent>& chords, int motifStart,
                                     int motifTicks, double density, int& prev, int& prevPrev, bool cadence, Random& rng)
    {
        const int motifBars = motifTicks / ticksPerBar;
        auto slots = melodyRhythm (m.g, density, motifBars, rng);
        std::vector<MotifNote> notes;
        bool first = prev < 0;

        for (size_t i = 0; i < slots.size(); ++i)
        {
            const auto& s = slots[i];
            const auto* ev = chordAt (chords, motifStart + s.start);
            const Chord chord = ev != nullptr ? ev->chord : Chord {};
            const double x = (double) s.start / motifTicks;
            int key = first ? openingPitch (m, chord, rng) : choosePitch (m, chord, s, prev, prevPrev, x, rng);
            first = false;

            // Fim de frase: repousa num som do acorde (fundamental ou terça) e segura.
            if (cadence && i + 1 == slots.size())
            {
                int best = key;
                int bestDist = 1000;

                for (int k = m.low; k <= m.high; ++k)
                {
                    const int rel = pitchClass (k - chord.root);

                    if ((rel == 0 || rel == 3 || rel == 4) && allowed (m, chord, k) && std::abs (k - prev) < bestDist)
                    {
                        best = k;
                        bestDist = std::abs (k - prev);
                    }
                }

                key = best;
            }

            notes.push_back ({ s.start, s.length, key, s.strong });
            prevPrev = prev;
            prev = key;
        }

        if (cadence && ! notes.empty())
        {
            auto& last = notes.back();
            last.length = std::max (last.length, std::min (motifTicks - last.offset, ticksPerBeat * 2));
        }

        return notes;
    }

    void makeMelody (const Request& r, const GenreProfile& g, const std::vector<ChordEvent>& chords,
                     Random& rng, std::vector<GenNote>& out)
    {
        const int shift = 12 * r.octave;
        const auto mood = moodOf (r, g);
        MelodyContext m { g, r.scale, mood, g.melodyLow + shift, g.melodyHigh + shift };

        const int motifBars = std::min (g.motifBars, r.bars);
        const int motifTicks = motifBars * ticksPerBar;
        const int totalTicks = r.bars * ticksPerBar;
        const int motifs = (r.bars + motifBars - 1) / motifBars;
        const std::string_view form (g.form);

        std::vector<MotifNote> motifA, motifB;
        int startA = 0, startB = 0;
        int prev = -1, prevPrev = -1;

        for (int i = 0; i < motifs; ++i)
        {
            char letter = form[(size_t) i % form.size()];

            if (i >= (int) form.size() && letter == 'A')
                letter = 'a';

            const int motifStart = i * motifTicks;
            const bool lastMotif = i + 1 == motifs;
            const bool isA = letter == 'A' || letter == 'a';
            auto& source = isA ? motifA : motifB;
            const int sourceStart = isA ? startA : startB;
            std::vector<MotifNote> notes;

            if (source.empty())
            {
                // Primeira vez desta letra: motivo novo. O B tem mais ou menos notas que o A.
                const double d = isA ? r.density : std::clamp (r.density + (rng.chance (0.5) ? 0.15 : -0.15), 0.0, 1.0);
                notes = newMotif (m, chords, motifStart, motifTicks, d, prev, prevPrev, lastMotif || ! isA, rng);
                source = notes;
                (isA ? startA : startB) = motifStart;
            }
            else
            {
                // Repetição: mesmo ritmo, as notas se ajustam aos acordes daqui.
                notes = source;

                for (auto& n : notes)
                {
                    const auto* oldEv = chordAt (chords, sourceStart + n.offset);
                    const auto* newEv = chordAt (chords, motifStart + n.offset);

                    if (oldEv != nullptr && newEv != nullptr)
                        n.key = adaptToChord (m, oldEv->chord, newEv->chord, n.key);
                }

                const bool vary = letter == 'a' || letter == 'b' || lastMotif;

                if (vary && notes.size() >= 2)
                {
                    // Variação: o último terço do motivo ganha notas novas.
                    const size_t from = notes.size() - std::max<size_t> (1, notes.size() / 3);
                    int p = from > 0 ? notes[from - 1].key : notes.front().key;
                    int pp = from > 1 ? notes[from - 2].key : p;

                    for (size_t k = from; k < notes.size(); ++k)
                    {
                        const auto* ev = chordAt (chords, motifStart + notes[k].offset);
                        const Chord chord = ev != nullptr ? ev->chord : Chord {};
                        Slot s { notes[k].offset, notes[k].length, notes[k].strong };
                        notes[k].key = choosePitch (m, chord, s, p, pp, (double) notes[k].offset / motifTicks, rng);
                        pp = p;
                        p = notes[k].key;
                    }
                }

                if (! notes.empty())
                {
                    prev = notes.back().key;
                    prevPrev = notes.size() > 1 ? notes[notes.size() - 2].key : prev;
                }
            }

            for (auto& n : notes)
            {
                const int start = motifStart + n.offset;

                if (start >= totalTicks)
                    continue;

                const int len = std::max (90, (int) std::lround (n.length * g.legato));
                const double vel = (n.strong ? 104 : (n.offset % ticksPerBeat) != 0 && n.length <= 240 ? 84 : 94)
                                   + moodVelocity (mood) + rng.between (-5, 5);
                out.push_back ({ start, std::min (len, totalTicks - start), n.key, clampVelocity (vel), false });
            }
        }
    }

    //==========================================================================
    // Arpejo
    void makeArp (const Request& r, const GenreProfile& g, const std::vector<ChordEvent>& chords,
                  Random& rng, std::vector<GenNote>& out)
    {
        const int shift = 12 * r.octave;
        const int low = g.melodyLow - 7 + shift, high = g.melodyLow + 14 + shift;
        int rate = g.arpTicks;

        if (r.density > 0.7 && rate == 480)        rate = 240;
        else if (r.density < 0.25 && rate == 240)  rate = 480;

        const int pattern = rng.between (0, 3);   // sobe, desce, sobe e desce, alterna grave e agudo
        const auto mood = moodOf (r, g);
        int counter = 0;

        for (auto& e : chords)
        {
            std::vector<int> pool;

            for (int k = low; k <= high; ++k)
                if (e.chord.contains (k))
                    pool.push_back (k);

            if (pool.empty())
                continue;

            std::vector<int> seq;

            switch (pattern)
            {
                case 0: seq = pool; break;
                case 1: seq.assign (pool.rbegin(), pool.rend()); break;
                case 2:
                    seq = pool;
                    for (int i = (int) pool.size() - 2; i > 0; --i)
                        seq.push_back (pool[(size_t) i]);
                    break;
                default:
                    for (size_t i = 0, j = pool.size() - 1; i <= j && j < pool.size(); ++i, --j)
                    {
                        seq.push_back (pool[i]);
                        if (i != j) seq.push_back (pool[j]);
                        if (j == 0) break;
                    }
                    break;
            }

            for (int t = e.start; t < e.end(); t += rate)
            {
                const int key = seq[(size_t) (counter++ % (int) seq.size())];
                const bool onBeat = t % ticksPerBeat == 0;
                const double vel = (onBeat ? 100 : 84) + moodVelocity (mood) + rng.between (-4, 4);
                out.push_back ({ t, std::min ((int) (rate * 0.85), e.end() - t), key, clampVelocity (vel), false });
            }
        }
    }

    //==========================================================================
    // Baixo
    void makeBass (const Request& r, const GenreProfile& g, const std::vector<ChordEvent>& chords,
                   Random& rng, std::vector<GenNote>& out)
    {
        const auto& groove = g.bassGrooves[chooseByDensity (g.bassGrooves, r.density, rng)];
        const int shift = 12 * r.octave;
        const int low = g.bassLow + shift, high = g.bassHigh + shift;
        int anchor = nearestKey (40 + shift, r.scale.root, low, high);
        const auto hits = gridHits (groove.grid, r.bars);

        for (size_t i = 0; i < hits.size(); ++i)
        {
            const auto& h = hits[i];
            const auto* ev = chordAt (chords, h.start);

            if (ev == nullptr)
                continue;

            const int root = nearestKey (anchor, ev->chord.root, low, high);
            int key = root;

            switch (h.symbol)
            {
                case 'f': key = ev->chord.has (7) ? root + 7 : root; break;
                case 'o': key = root + 12 <= high + 5 ? root + 12 : root; break;
                case 'a':
                {
                    const auto* next = chordAt (chords, h.start + h.length);
                    const int target = next != nullptr ? nearestKey (root, next->chord.root, low, high) : root;

                    if (target == root)
                        key = ev->chord.has (7) ? root + 7 : root;
                    else
                        key = r.scale.harmonic().stepFrom (target, target > root ? -1 : 1);

                    break;
                }
                default: break;
            }

            if (key > high + 5)
                key -= 12;

            anchor = root;
            const bool barStart = h.start % ticksPerBar == 0;
            const double vel = (barStart ? 110 : 100) + rng.between (-4, 4);
            out.push_back ({ h.start, std::max (60, h.length - 20), key, clampVelocity (vel), false });
        }
    }

    //==========================================================================
    // 808: segue o bumbo da batida escolhida, segura até a próxima nota e desliza.
    void make808 (const Request& r, const GenreProfile& g, const std::vector<ChordEvent>& chords, int beatIndex,
                  Random& rng, std::vector<GenNote>& out)
    {
        const auto& beat = g.beats[(size_t) beatIndex];
        const int totalTicks = r.bars * ticksPerBar;
        std::vector<int> starts;

        for (auto& h : gridHits (beat.kick, r.bars))
            if (h.symbol == 'x' || h.symbol == 'X')
                starts.push_back (h.start);

        // Pouca densidade: o 808 só nos bumbos fortes.
        if (r.density < 0.35)
        {
            std::vector<int> kept;

            for (int s : starts)
                if (s % ticksPerBar == 0 || (s % ticksPerBeat == 0) || ! rng.chance (0.6))
                    kept.push_back (s);

            starts = kept;
        }

        if (starts.empty() || starts.front() != 0)
            starts.insert (starts.begin(), 0);

        const int shift = 12 * r.octave;
        const int tonic = 31 + pitchClass (r.scale.root - 31) + shift;   // entre Sol1 e Fá#2
        std::vector<GenNote> notes;

        for (size_t i = 0; i < starts.size(); ++i)
        {
            const int s = starts[i];
            const int next = i + 1 < starts.size() ? starts[i + 1] : totalTicks;
            const auto* ev = chordAt (chords, s);
            const int rootPc = ev != nullptr ? ev->chord.root : r.scale.root;
            int key = nearestKey (tonic, rootPc, tonic - 5, tonic + 6);
            const int inBar = (s % ticksPerBar) / ticksPerStep;

            // Salto de oitava no fim do compasso, sem sair do grave (até Mi3).
            if (inBar >= 10 && rng.chance (g.octaveChance * (0.5 + r.complexity)) && key + 12 <= 52 + shift)
                key += 12;

            const double vel = (s % ticksPerBar == 0 ? 112 : 104) + rng.between (-3, 3);
            notes.push_back ({ s, next - s, key, clampVelocity (vel), false });

            // Drill: o 808 vira linha melódica, com notas de passagem no meio dos espaços longos.
            const int gapSteps = (next - s) / ticksPerStep;

            if (g.melodic808 && gapSteps >= 6 && ev != nullptr && rng.chance (0.25 + 0.5 * r.complexity))
            {
                const int mid = s + (gapSteps / 2) * ticksPerStep;
                std::vector<int> options;

                for (int iv : ev->chord.intervals)
                    if (iv != 0)
                        options.push_back (nearestKey (key, ev->chord.root + iv, key - 9, key + 9));

                if (key + 12 <= 52 + shift)
                    options.push_back (key + 12);

                if (options.empty())
                    options.push_back (key);

                int passing = options[(size_t) rng.between (0, (int) options.size() - 1)];

                if (passing > 52 + shift)
                    passing -= 12;
                else if (passing < 26 + shift)
                    passing += 12;
                notes.back().length = mid - s;
                notes.push_back ({ mid, next - mid, passing, clampVelocity (vel - 6), true });
            }
        }

        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto& n = notes[i];

            if (i > 0 && n.key != notes[i - 1].key && ! n.slide)
                n.slide = rng.chance (g.slideChance);

            const int gap = n.length;
            n.length = g.eightHold >= 0.99f ? gap : std::min (gap, std::max (ticksPerStep, (int) (gap * g.eightHold)));
            n.length = std::max (60, std::min (n.length, totalTicks - n.start));
        }

        // O primeiro não tem de onde deslizar.
        if (! notes.empty())
            notes.front().slide = false;

        out.insert (out.end(), notes.begin(), notes.end());
    }
}

//==============================================================================
int chooseBeat (const Request& r)
{
    const auto& g = genreOf (r);
    Random rng (mixSeed (r.seed, 2));
    return (int) chooseByDensity (g.beats, r.density, rng);
}

std::string chordNames (const std::vector<ChordEvent>& chords, const Scale& scale)
{
    std::string s;
    const bool flats = scale.prefersFlats();

    for (auto& e : chords)
    {
        if (! s.empty())
            s += ' ';

        s += e.chord.name (flats);
    }

    return s;
}

std::vector<ChordEvent> chordTimeline (const Request& r, std::string& text, std::string& error)
{
    const auto& g = genreOf (r);
    const int bars = std::clamp (r.bars, 1, 16);
    const int totalTicks = bars * ticksPerBar;

    if (! r.chords.empty())
    {
        std::vector<ChordEvent> out;

        for (auto e : r.chords)
        {
            if (e.start >= totalTicks || e.length <= 0)
                continue;

            e.start = std::max (0, e.start);
            e.chord.root = pitchClass (e.chord.root);
            out.push_back (e);
        }

        std::stable_sort (out.begin(), out.end(), [] (const ChordEvent& a, const ChordEvent& b) { return a.start < b.start; });

        // O canal seguido pode começar depois do compasso 1 ou ter pausas: o primeiro acorde vale desde
        // o começo, cada um vai até o próximo e o último até o fim (senão acordes e arpejo ficam mudos ali).
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (i == 0)
                out[i].start = 0;

            const int end = i + 1 < out.size() ? out[i + 1].start : totalTicks;
            out[i].length = std::max (1, end - out[i].start);
        }

        out.erase (std::remove_if (out.begin(), out.end(), [] (const ChordEvent& e) { return e.length <= 0; }), out.end());
        text = chordNames (out, r.scale);
        return out;
    }

    text = r.progression;

    if (text.empty())
    {
        const auto options = progressionsFor (g.id, ! r.scale.isMajor(), moodOf (r, g));
        Random rng (mixSeed (r.seed, 1));
        std::vector<double> w;
        const auto mood = moodId (moodOf (r, g));

        for (auto* e : options)
        {
            const std::string_view moods (e->moods);
            w.push_back (moods.find (mood) != std::string_view::npos ? 3.0 : 1.0);
        }

        text = options[rng.weighted (w)]->text;
    }

    auto prog = parseProgression (text, r.scale);

    if (prog.empty())
    {
        error = "Progressão inválida: " + text;
        return {};
    }

    // Extensões automáticas: sempre sorteia as duas, para a sequência não depender do acorde.
    Random ext (mixSeed (r.seed, 4));
    // Escala de 7 notas (frígio, dórico, menor harmônica): as extensões seguem ela, para não
    // brigar com a melodia. Pentatônica e cromática usam a maior ou a menor natural.
    const auto harmonic = r.scale.degrees.size() == 7 ? r.scale : r.scale.harmonic();
    const double p7 = std::clamp ((double) g.seventhBase + g.seventhPerComplexity * r.complexity, 0.0, 1.0);
    const double p9 = std::clamp ((double) g.ninthPerComplexity * r.complexity, 0.0, 1.0);

    for (auto& pc : prog)
    {
        const bool add7 = ext.chance (p7);
        const bool add9 = ext.chance (p9);

        if (! pc.explicitExtension)
            addDiatonicExtensions (pc.chord, harmonic, add7, add9);
    }

    int weights = 0;

    for (auto& pc : prog)
        weights += pc.weight;

    int unit = ticksPerBar;

    if (weights > bars)
        unit = std::max (ticksPerBeat, (bars * ticksPerBar / weights) / ticksPerBeat * ticksPerBeat);

    std::vector<ChordEvent> out;
    size_t i = 0;

    for (int t = 0; t < totalTicks; ++i)
    {
        const auto& pc = prog[i % prog.size()];
        const int len = std::min (pc.weight * unit, totalTicks - t);
        out.push_back ({ t, len, pc.chord });
        t += len;
    }

    return out;
}

Result generate (const Request& request)
{
    Request r = request;
    r.bars = std::clamp (r.bars, 1, 16);
    r.complexity = std::clamp (r.complexity, 0.0, 1.0);
    r.density = std::clamp (r.density, 0.0, 1.0);
    r.octave = std::clamp (r.octave, -2, 2);

    Result res;

    if (findGenre (r.genre) == nullptr)
    {
        res.ok = false;
        res.error = "Gênero desconhecido: " + r.genre;
        return res;
    }

    const auto& g = genreOf (r);
    res.chords = chordTimeline (r, res.progression, res.error);

    if (res.chords.empty())
    {
        res.ok = false;

        if (res.error.empty())
            res.error = "Sem acordes para seguir.";

        return res;
    }

    res.beat = chooseBeat (r);
    auto rng = partRandom (r);

    switch (r.part)
    {
        case Part::chords:       makeChords (r, g, res.chords, rng, res.notes); break;
        case Part::melody:       makeMelody (r, g, res.chords, rng, res.notes); break;
        case Part::arp:          makeArp (r, g, res.chords, rng, res.notes); break;
        case Part::bass:         makeBass (r, g, res.chords, rng, res.notes); break;
        case Part::eightOhEight: make808 (r, g, res.chords, res.beat, rng, res.notes); break;
        case Part::drums:
            res.ok = false;
            res.error = "A bateria vai para os passos do channel rack: use generateDrums.";
            return res;
    }

    sortNotes (res.notes);
    return res;
}

DrumResult generateDrums (const Request& request)
{
    Request r = request;
    r.bars = std::clamp (r.bars, 1, 16);
    r.complexity = std::clamp (r.complexity, 0.0, 1.0);
    r.density = std::clamp (r.density, 0.0, 1.0);

    DrumResult res;

    if (findGenre (r.genre) == nullptr)
    {
        res.ok = false;
        res.error = "Gênero desconhecido: " + r.genre;
        return res;
    }

    const auto& g = genreOf (r);
    res.beat = chooseBeat (r);
    res.lengthSteps = r.bars * 16;
    const auto& beat = g.beats[(size_t) res.beat];
    r.part = Part::drums;
    auto rng = partRandom (r);

    struct Lane { const char* role; const char* grid; int base; };
    const Lane lanes[] = {
        { "kick", beat.kick, 110 }, { "snare", beat.snare, 106 }, { "hat", beat.hat, 88 },
        { "openhat", beat.openhat, 90 }, { "perc", beat.perc, 84 },
    };

    for (auto& lane : lanes)
    {
        std::vector<DrumStep> steps ((size_t) res.lengthSteps);
        const std::string_view grid (lane.grid);
        const bool isHat = std::string_view (lane.role) == "hat";

        for (int s = 0; s < res.lengthSteps && ! grid.empty(); ++s)
        {
            const char c = grid[(size_t) (s % (int) grid.size())];
            auto& st = steps[(size_t) s];

            if (c == '.')
                continue;

            if (c == 'g' && r.density < 0.25)
                continue;   // pouca densidade: sem notas fantasmas

            st.on = true;
            double vel = lane.base;

            if (c == 'X')                         vel += 15;
            else if (c == 'g')                    vel = 55;
            else if (c >= '2' && c <= '8')        { st.roll = c - '0'; vel -= 6; }

            if (isHat && c != 'g')
                vel += (s % 4 == 0) ? 8 : (s % 2 == 1) ? -12 : 0;

            if (isHat || std::string_view (lane.role) == "perc")
                vel += rng.between (-4, 4);

            st.velocity = clampVelocity (vel);
        }

        // Virada no fim de cada 4 compassos: rolls no chimbal e, às vezes, uma caixa a mais.
        if (r.complexity > 0.3 && ! grid.empty())
        {
            for (int bar = 3; bar < r.bars; bar += 4)
            {
                const int base = bar * 16;

                if (isHat)
                {
                    static const char* fills[] = { "2.4.", "3.6.", "x24.", "4.4.", "2x3." };
                    const std::string_view fill = fills[rng.between (0, 4)];

                    for (int k = 0; k < 4; ++k)
                    {
                        auto& st = steps[(size_t) (base + 12 + k)];
                        const char c = fill[(size_t) k];
                        st = {};

                        if (c != '.')
                        {
                            st.on = true;
                            st.roll = c == 'x' ? 1 : c - '0';
                            st.velocity = clampVelocity (76 + k * 8 + rng.between (-3, 3));
                        }
                    }
                }
                else if (std::string_view (lane.role) == "snare" && rng.chance (r.complexity * 0.6))
                {
                    auto& st = steps[(size_t) (base + 15)];

                    if (! st.on)
                        st = { true, 92, 1 };
                }
            }
        }

        // Variar: mexe nos detalhes (chimbal, percussão, fantasmas), nunca no bumbo, que o 808 segue.
        if (r.variation > 0 && ! grid.empty() && std::string_view (lane.role) != "kick")
        {
            for (int s = 0; s < res.lengthSteps; ++s)
            {
                auto& st = steps[(size_t) s];

                if (isHat && st.on && st.roll == 1 && s % 4 == 3 && rng.chance (0.25))
                    st.roll = rng.chance (0.5) ? 2 : 3;
                else if (isHat && ! st.on && s % 2 == 1 && rng.chance (0.12))
                    st = { true, 70, 1 };
                else if (std::string_view (lane.role) == "snare" && ! st.on && s % 16 == 14 && rng.chance (0.2))
                    st = { true, 60, 1 };
            }
        }

        res.lanes.push_back ({ lane.role, std::move (steps) });
    }

    return res;
}

} // namespace whynot::ideas
