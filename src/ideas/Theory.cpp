#include "Theory.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace whynot::ideas
{

namespace
{
    int floorDiv (int a, int b) noexcept
    {
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    }

    int mask (const std::vector<int>& intervals) noexcept
    {
        int m = 0;

        for (int i : intervals)
            m |= 1 << pitchClass (i);

        return m;
    }

    int popcount (int x) noexcept
    {
        int n = 0;

        for (; x != 0; x &= x - 1)
            ++n;

        return n;
    }

    std::vector<int> sortedUnique (std::vector<int> v)
    {
        for (auto& x : v)
            x = pitchClass (x);

        std::sort (v.begin(), v.end());
        v.erase (std::unique (v.begin(), v.end()), v.end());
        return v;
    }
}

std::string pitchName (int pc, bool flats)
{
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flatNames[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return (flats ? flatNames : sharps)[pitchClass (pc)];
}

std::string pitchNamePt (int pc, bool flats)
{
    static const char* sharps[] = { "Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si" };
    static const char* flatNames[] = { "Dó", "Réb", "Ré", "Mib", "Mi", "Fá", "Solb", "Sol", "Láb", "Lá", "Sib", "Si" };
    return (flats ? flatNames : sharps)[pitchClass (pc)];
}

//==============================================================================
Scale Scale::fromName (int root, std::string_view name)
{
    Scale s;
    s.root = pitchClass (root);
    s.name = std::string (name);

    if (name == "major")                  s.degrees = { 0, 2, 4, 5, 7, 9, 11 };
    else if (name == "harmonicMinor")     s.degrees = { 0, 2, 3, 5, 7, 8, 11 };
    else if (name == "phrygian")          s.degrees = { 0, 1, 3, 5, 7, 8, 10 };
    else if (name == "dorian")            s.degrees = { 0, 2, 3, 5, 7, 9, 10 };
    else if (name == "pentatonicMinor")   s.degrees = { 0, 3, 5, 7, 10 };
    else if (name == "chromatic")         s.degrees = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    else
    {
        s.degrees = { 0, 2, 3, 5, 7, 8, 10 };
        s.name = "minor";
    }

    return s;
}

bool Scale::isMajor() const
{
    return name == "major";
}

bool Scale::contains (int key) const
{
    return degreeOf (key) >= 0;
}

Scale Scale::harmonic() const
{
    return fromName (root, isMajor() ? "major" : "minor");
}

bool Scale::prefersFlats() const
{
    // Tônica da relativa maior: em menor, sobe uma terça menor.
    const int majorRoot = isMajor() ? root : pitchClass (root + 3);

    switch (majorRoot)
    {
        case 5: case 10: case 3: case 8: case 1: case 6:   // F, Bb, Eb, Ab, Db, Gb
            return true;
        default:
            return false;
    }
}

int Scale::degreeOf (int key) const
{
    const int rel = pitchClass (key - root);

    for (size_t i = 0; i < degrees.size(); ++i)
        if (degrees[i] == rel)
            return (int) i;

    return -1;
}

int Scale::snap (int key) const
{
    for (int d = 0; d < 12; ++d)
    {
        if (contains (key - d)) return key - d;
        if (contains (key + d)) return key + d;
    }

    return key;
}

int Scale::stepFrom (int key, int steps) const
{
    key = snap (key);
    const int n = (int) degrees.size();
    const int rel = key - root;
    int octave = floorDiv (rel, 12);
    const int idx = degreeOf (key) + steps;

    octave += floorDiv (idx, n);
    const int wrapped = ((idx % n) + n) % n;
    return root + octave * 12 + degrees[(size_t) wrapped];
}

//==============================================================================
bool Chord::has (int interval) const
{
    const int pc = pitchClass (interval);
    return std::find (intervals.begin(), intervals.end(), pc) != intervals.end();
}

bool Chord::contains (int key) const
{
    return has (key - root);
}

std::vector<int> Chord::pitchClasses() const
{
    std::vector<int> out;

    for (int i : intervals)
        out.push_back (pitchClass (root + i));

    return out;
}

std::string Chord::suffix() const
{
    struct Entry { std::vector<int> iv; const char* suffix; };

    static const std::vector<Entry> table = {
        { { 0, 4, 7 }, "" },            { { 0, 3, 7 }, "m" },
        { { 0, 3, 6 }, "dim" },         { { 0, 4, 8 }, "aug" },
        { { 0, 2, 7 }, "sus2" },        { { 0, 5, 7 }, "sus4" },
        { { 0, 4, 7, 10 }, "7" },       { { 0, 4, 7, 11 }, "maj7" },
        { { 0, 3, 7, 10 }, "m7" },      { { 0, 3, 6, 10 }, "m7b5" },
        { { 0, 3, 6, 9 }, "dim7" },     { { 0, 3, 7, 11 }, "m(maj7)" },
        { { 0, 4, 7, 9 }, "6" },        { { 0, 3, 7, 9 }, "m6" },
        { { 0, 5, 7, 10 }, "7sus4" },   { { 0, 2, 4, 7 }, "add9" },
        { { 0, 2, 3, 7 }, "m(add9)" },  { { 0, 2, 4, 7, 10 }, "9" },
        { { 0, 2, 4, 7, 11 }, "maj9" }, { { 0, 2, 3, 7, 10 }, "m9" },
        { { 0, 7 }, "5" },              { { 0, 4, 10 }, "7" },
        { { 0, 3, 10 }, "m7" },         { { 0, 4, 11 }, "maj7" },
        { { 0, 4 }, "" },               { { 0, 3 }, "m" },
    };

    const int m = mask (intervals);
    const Entry* best = nullptr;
    int bestScore = std::numeric_limits<int>::min();

    for (auto& e : table)
    {
        const int em = mask (e.iv);

        if (em == m)
            return e.suffix;

        const int score = popcount (em & m) * 2 - popcount (em & ~m) * 3 - popcount (m & ~em);

        if (score > bestScore)
        {
            bestScore = score;
            best = &e;
        }
    }

    return best != nullptr ? best->suffix : "";
}

std::string Chord::name (bool flats) const
{
    auto n = pitchName (root, flats) + suffix();

    if (bass >= 0 && pitchClass (bass) != pitchClass (root))
        n += "/" + pitchName (bass, flats);

    return n;
}

//==============================================================================
std::optional<ProgressionChord> parseRoman (std::string_view t, const Scale& key)
{
    size_t i = 0;
    int accidental = 0;

    while (i < t.size() && (t[i] == 'b' || t[i] == '#'))
        accidental += t[i++] == 'b' ? -1 : 1;

    std::string numeral;

    while (i < t.size() && (t[i] == 'I' || t[i] == 'V' || t[i] == 'i' || t[i] == 'v'))
        numeral += t[i++];

    if (numeral.empty())
        return std::nullopt;

    const bool upper = numeral[0] == 'I' || numeral[0] == 'V';

    for (char c : numeral)
        if ((c == 'I' || c == 'V') != upper)
            return std::nullopt;   // "Iv": misturou maiúscula e minúscula

    std::string u;

    for (char c : numeral)
        u += (char) (c == 'i' ? 'I' : c == 'v' ? 'V' : c);

    static const char* numerals[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    int degree = -1;

    for (int d = 0; d < 7; ++d)
        if (u == numerals[d])
            degree = d;

    if (degree < 0)
        return std::nullopt;

    const auto h = key.harmonic();
    ProgressionChord pc;
    pc.chord.root = pitchClass (h.root + h.degrees[(size_t) degree] + accidental);
    std::vector<int> iv = upper ? std::vector<int> { 0, 4, 7 } : std::vector<int> { 0, 3, 7 };
    const auto rest = t.substr (i);
    pc.explicitExtension = ! rest.empty();

    if (rest.empty())                 {}
    else if (rest == "o")             iv = { 0, 3, 6 };
    else if (rest == "o7")            iv = { 0, 3, 6, 9 };
    else if (rest == "h7")            iv = { 0, 3, 6, 10 };
    else if (rest == "+")             iv = { 0, 4, 8 };
    else if (rest == "7")             iv.push_back (10);
    else if (rest == "maj7")          iv.push_back (11);
    else if (rest == "9")             { iv.push_back (10); iv.push_back (2); }
    else if (rest == "maj9")          { iv.push_back (11); iv.push_back (2); }
    else if (rest == "add9")          iv.push_back (2);
    else if (rest == "6")             iv.push_back (9);
    else if (rest == "sus2")          iv = { 0, 2, 7 };
    else if (rest == "sus4")          iv = { 0, 5, 7 };
    else if (rest == "7sus4")         iv = { 0, 5, 7, 10 };
    else                              return std::nullopt;

    pc.chord.intervals = sortedUnique (iv);
    return pc;
}

std::vector<ProgressionChord> parseProgression (std::string_view text, const Scale& key)
{
    std::vector<ProgressionChord> out;
    std::string token;

    auto flush = [&] () -> bool
    {
        if (token.empty())
            return true;

        int weight = 1;
        auto star = token.find ('*');
        std::string body = token;

        if (star != std::string::npos)
        {
            body = token.substr (0, star);
            weight = std::atoi (token.c_str() + star + 1);

            if (weight < 1 || weight > 16)
                return false;
        }

        auto chord = parseRoman (body, key);

        if (! chord.has_value())
            return false;

        chord->weight = weight;
        out.push_back (*chord);
        token.clear();
        return true;
    };

    for (char c : text)
    {
        if (c == ' ' || c == '-' || c == ',' || c == '|' || c == '\t')
        {
            if (! flush())
                return {};
        }
        else
        {
            token += c;
        }
    }

    if (! flush() || out.size() > 32)
        return {};

    return out;
}

//==============================================================================
std::vector<int> voiceChord (const Chord& chord, const std::vector<int>& previous, int low, int high,
                             int maxNotes, bool spread)
{
    auto iv = chord.intervals;
    maxNotes = std::max (2, maxNotes);

    // Vozes demais: a quinta sai primeiro (não muda a cor do acorde), depois a nona, a sexta e,
    // por último, a sétima: a terça e a sétima dizem que acorde é.
    for (int drop : { 7, 2, 9 })
        if ((int) iv.size() > maxNotes && iv.size() > 3)
            if (auto it = std::find (iv.begin(), iv.end(), drop); it != iv.end())
                iv.erase (it);

    while ((int) iv.size() > maxNotes)
        iv.pop_back();

    const int n = (int) iv.size();
    const double centre = (low + high) * 0.5;
    std::vector<int> best;
    double bestScore = std::numeric_limits<double>::max();

    auto distance = [] (const std::vector<int>& a, const std::vector<int>& b)
    {
        double d = 0.0;

        for (int x : a)
        {
            int m = 1000;

            for (int y : b)
                m = std::min (m, std::abs (x - y));

            d += m;
        }

        return d;
    };

    for (int inversion = 0; inversion < n; ++inversion)
    {
        for (int start = low - 12; start <= high; ++start)
        {
            if (pitchClass (start) != pitchClass (chord.root + iv[(size_t) inversion]))
                continue;

            std::vector<int> v { start };

            for (int k = 1; k < n; ++k)
            {
                const int target = pitchClass (chord.root + iv[(size_t) ((inversion + k) % n)]);
                int next = v.back() + 1;

                while (pitchClass (next) != target)
                    ++next;

                v.push_back (next);
            }

            if (spread && n >= 3)
            {
                // Drop 2: a segunda voz de cima desce uma oitava (voicing aberto de R&B e lo-fi).
                v[(size_t) (n - 2)] -= 12;
                std::sort (v.begin(), v.end());
            }

            if (v.front() < low || v.back() > high)
                continue;

            double mean = 0.0;

            for (int x : v)
                mean += x;

            mean /= n;
            double score = std::abs (mean - centre) * 0.6;

            if (! previous.empty())
                score += (distance (v, previous) + distance (previous, v)) * 0.5;
            else
                score += inversion * 0.5;   // começo: prefere a posição fundamental

            if (score < bestScore)
            {
                bestScore = score;
                best = v;
            }
        }
    }

    if (best.empty())
    {
        // Faixa estreita demais: empilha a partir da fundamental mais perto do centro.
        int root = (int) std::lround (centre);

        while (pitchClass (root) != pitchClass (chord.root))
            --root;

        for (int i : iv)
            best.push_back (root + i);
    }

    return best;
}

} // namespace whynot::ideas
