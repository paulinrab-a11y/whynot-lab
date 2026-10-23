#include "Prompt.h"

#include "Theory.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace whynot::ideas
{

namespace
{
    struct Token
    {
        std::string lower, original;
    };

    std::vector<Token> tokenize (std::string_view text)
    {
        const auto kept = foldText (text, false);
        std::vector<Token> out;
        std::string cur;

        auto flush = [&]
        {
            if (cur.empty())
                return;

            Token t;
            t.original = cur;

            for (char c : cur)
                t.lower += (char) std::tolower ((unsigned char) c);

            out.push_back (t);
            cur.clear();
        };

        for (char c : kept)
        {
            if (std::isalnum ((unsigned char) c) || c == '#' || c == '&' || c == '-')
                cur += c;
            else
                flush();
        }

        flush();
        return out;
    }

    bool isNumber (const std::string& s)
    {
        return ! s.empty() && std::all_of (s.begin(), s.end(), [] (char c) { return std::isdigit ((unsigned char) c) != 0; });
    }

    template <size_t N>
    const char* lookup (const std::pair<const char*, const char*> (&table)[N], const std::string& word)
    {
        for (auto& [k, v] : table)
            if (word == k)
                return v;

        return nullptr;
    }

    const char* scaleWord (const std::string& w)
    {
        if (w == "menor" || w == "minor" || w == "m")        return "minor";
        if (w == "maior" || w == "major")                     return "major";
        if (w == "frigio" || w == "phrygian")                 return "phrygian";
        if (w == "dorico" || w == "dorian")                   return "dorian";
        if (w == "harmonica" || w == "harmonic")              return "harmonicMinor";
        if (w == "pentatonica" || w == "pentatonic")          return "pentatonicMinor";
        return nullptr;
    }

    std::string scaleNamePt (const std::string& s)
    {
        if (s == "major")           return "maior";
        if (s == "phrygian")        return "frígio";
        if (s == "dorian")          return "dórico";
        if (s == "harmonicMinor")   return "menor harmônica";
        if (s == "pentatonicMinor") return "pentatônica menor";
        return "menor";
    }

    /** "fa", "fa#", "sib", "solb"... → classe de altura. */
    bool ptNote (const std::string& w, int& pc, int& accidental)
    {
        static const std::pair<const char*, int> names[] = {
            { "sol", 7 }, { "do", 0 }, { "re", 2 }, { "mi", 4 }, { "fa", 5 }, { "la", 9 }, { "si", 11 },
        };

        for (auto& [name, value] : names)
        {
            const std::string n (name);

            if (w.rfind (n, 0) != 0)
                continue;

            const auto rest = w.substr (n.size());

            if (rest.empty())       { pc = value; accidental = 0;  return true; }
            if (rest == "#")        { pc = value; accidental = 1;  return true; }
            if (rest == "b")        { pc = value; accidental = -1; return true; }
        }

        return false;
    }

    /** "Am", "F#m", "Bb", "C#", "Ebmaj"... (letra maiúscula, ou minúscula com acidente ou "m"). */
    bool letterKey (const Token& t, int& pc, int& accidental, const char*& scale)
    {
        const auto& o = t.original;

        if (o.empty())
            return false;

        const char first = (char) std::toupper ((unsigned char) o[0]);

        if (first < 'A' || first > 'G')
            return false;

        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        pc = base[first - 'A'];
        accidental = 0;
        scale = nullptr;
        size_t i = 1;

        if (i < o.size() && (o[i] == '#' || o[i] == 'b'))
            accidental = o[i++] == '#' ? 1 : -1;

        const auto suffix = t.lower.substr (i);

        if (suffix == "m" || suffix == "min")   scale = "minor";
        else if (suffix == "maj")               scale = "major";
        else if (! suffix.empty())              return false;

        const bool upper = std::isupper ((unsigned char) o[0]) != 0;

        if (! upper)
        {
            // Minúscula só com acidente ou "m" — e "em" é a preposição, não Mi menor.
            if (t.lower == "em" || (accidental == 0 && scale == nullptr))
                return false;
        }

        return true;
    }
}

std::string foldText (std::string_view in, bool lower)
{
    std::string out;

    for (size_t i = 0; i < in.size(); ++i)
    {
        const auto c = (unsigned char) in[i];

        if (c == 0xC3 && i + 1 < in.size())
        {
            const auto d = (unsigned char) in[++i];
            const bool upper = d < 0xA0;
            const unsigned x = upper ? d + 0x20u : d;
            char m = 0;

            if (x >= 0xA0 && x <= 0xA5)        m = 'a';
            else if (x == 0xA7)                m = 'c';
            else if (x >= 0xA8 && x <= 0xAB)   m = 'e';
            else if (x >= 0xAC && x <= 0xAF)   m = 'i';
            else if (x == 0xB1)                m = 'n';
            else if (x >= 0xB2 && x <= 0xB6)   m = 'o';
            else if (x >= 0xB9 && x <= 0xBC)   m = 'u';

            if (m == 0)
                out += ' ';
            else
                out += (upper && ! lower) ? (char) std::toupper ((unsigned char) m) : m;

            continue;
        }

        if (c >= 0x80)
        {
            out += ' ';
            continue;
        }

        out += lower ? (char) std::tolower (c) : (char) c;
    }

    return out;
}

PromptSettings parsePrompt (std::string_view text)
{
    static const std::pair<const char*, const char*> genreWords[] = {
        { "trap", "trap" }, { "drill", "drill" }, { "funk", "funk" }, { "mandelao", "funk" }, { "baile", "funk" },
        { "automotivo", "funk" }, { "bruxaria", "funk" }, { "rap", "rap" }, { "hiphop", "rap" }, { "hip-hop", "rap" },
        { "pop", "pop" }, { "rnb", "rnb" }, { "r&b", "rnb" }, { "soul", "rnb" }, { "phonk", "phonk" },
        { "lofi", "lofi" }, { "lo-fi", "lofi" }, { "boombap", "lofi" },
    };

    static const std::pair<const char*, const char*> moodWords[] = {
        { "sombrio", "dark" }, { "sombria", "dark" }, { "dark", "dark" }, { "escuro", "dark" }, { "escura", "dark" },
        { "pesado", "dark" }, { "pesada", "dark" }, { "agressivo", "dark" }, { "agressiva", "dark" }, { "tenso", "dark" },
        { "tensa", "dark" }, { "misterioso", "dark" }, { "misteriosa", "dark" },
        { "triste", "sad" }, { "sad", "sad" }, { "melancolico", "sad" }, { "melancolica", "sad" }, { "emo", "sad" },
        { "saudade", "sad" }, { "depre", "sad" },
        { "calmo", "chill" }, { "calma", "chill" }, { "chill", "chill" }, { "relax", "chill" }, { "relaxante", "chill" },
        { "tranquilo", "chill" }, { "tranquila", "chill" }, { "suave", "chill" }, { "leve", "chill" },
        { "alegre", "happy" }, { "feliz", "happy" }, { "happy", "happy" }, { "animado", "happy" }, { "animada", "happy" },
        { "positivo", "happy" }, { "positiva", "happy" }, { "dancante", "happy" },
        { "epico", "epic" }, { "epica", "epic" }, { "epic", "epic" }, { "grandioso", "epic" }, { "grandiosa", "epic" },
        { "intenso", "epic" }, { "intensa", "epic" }, { "hino", "epic" },
    };

    static const std::pair<const char*, const char*> partWords[] = {
        { "acordes", "chords" }, { "acorde", "chords" }, { "harmonia", "chords" }, { "chords", "chords" },
        { "melodia", "melody" }, { "melody", "melody" }, { "lead", "melody" }, { "linha", "melody" },
        { "baixo", "bass" }, { "bass", "bass" }, { "baixaria", "bass" }, { "808", "808" },
        { "arpejo", "arp" }, { "arp", "arp" }, { "arpeggio", "arp" },
        { "bateria", "drums" }, { "batida", "drums" }, { "drums", "drums" }, { "beat", "drums" },
    };

    PromptSettings out;
    const auto tokens = tokenize (text);
    bool keyFound = false;

    auto lowerAt = [&] (size_t i) -> std::string { return i < tokens.size() ? tokens[i].lower : std::string(); };

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const auto& w = tokens[i].lower;
        const auto next = lowerAt (i + 1);

        // Duas palavras primeiro.
        if (! out.genre && ((w == "boom" && next == "bap") || (w == "lo" && next == "fi")))
        {
            out.genre = "lofi";
            ++i;
            continue;
        }

        if (! out.genre && w == "hip" && next == "hop")
        {
            out.genre = "rap";
            ++i;
            continue;
        }

        if (! out.mood && w == "pra" && next == "cima")
        {
            out.mood = Mood::happy;
            ++i;
            continue;
        }

        if (! out.part)
            if (auto* p = lookup (partWords, w))
            {
                Part part;

                if (parsePart (p, part))
                    out.part = part;

                continue;
            }

        if (! out.genre)
            if (auto* g = lookup (genreWords, w))
            {
                out.genre = g;
                continue;
            }

        if (! out.mood)
            if (auto* m = lookup (moodWords, w))
            {
                Mood mood;

                if (parseMood (m, mood))
                    out.mood = mood;

                continue;
            }

        if (isNumber (w) || (w.size() > 3 && w.substr (w.size() - 3) == "bpm" && isNumber (w.substr (0, w.size() - 3))))
        {
            const bool glued = ! isNumber (w);
            const int n = std::atoi (w.c_str());

            if (glued || next == "bpm")
            {
                if (n >= 40 && n <= 250)
                    out.bpm = n;

                if (! glued)
                    ++i;
            }
            else if (next.rfind ("compasso", 0) == 0 || next.rfind ("barra", 0) == 0 || next == "bars" || next == "bar")
            {
                out.bars = std::clamp (n, 1, 16);
                ++i;
            }

            continue;
        }

        if (w == "simples" || w == "simple" || w == "facil" || w == "basico" || w == "basica")
        {
            out.complexity = 0.2;
            continue;
        }

        if (w == "complexo" || w == "complexa" || w == "elaborado" || w == "elaborada" || w == "rico"
            || w == "rica" || w == "jazzy" || w == "sofisticado" || w == "sofisticada")
        {
            out.complexity = 0.85;
            continue;
        }

        if (w == "minimalista" || w == "espacado" || w == "espacada" || w == "vazio" || w == "lento" || w == "lenta")
        {
            out.density = 0.25;
            continue;
        }

        if (w == "cheio" || w == "cheia" || w == "rapido" || w == "rapida" || w == "agitado" || w == "agitada"
            || w == "frenetico" || w == "frenetica")
        {
            out.density = 0.8;
            continue;
        }

        // Tonalidade: "fá menor", "em lá", "fa# menor", "si bemol maior", "Am", "C#m"...
        if (! keyFound)
        {
            int pc = 0, accidental = 0;
            size_t j = i + 1;

            const std::string prev = i > 0 ? tokens[i - 1].lower : std::string();
            const std::string prevPrev = i > 1 ? tokens[i - 2].lower : std::string();
            const bool afterKeyWord = prev == "em" || prev == "in" || prev == "tom" || prev == "tonalidade" || prev == "key"
                                      || (prev == "de" && prevPrev == "tom");

            if (ptNote (w, pc, accidental))
            {
                if (lowerAt (j) == "sustenido")  { accidental = 1; ++j; }
                else if (lowerAt (j) == "bemol") { accidental = -1; ++j; }

                const char* scale = scaleWord (lowerAt (j));

                if (scale != nullptr && std::string (scale) == "minor" && lowerAt (j + 1) == "harmonica")
                {
                    scale = "harmonicMinor";
                    ++j;
                }

                if (scale != nullptr || accidental != 0 || afterKeyWord)
                {
                    out.root = pitchClass (pc + accidental);

                    if (scale != nullptr)
                    {
                        out.scale = scale;
                        ++j;
                    }

                    keyFound = true;
                    i = j - 1;
                    continue;
                }
            }

            const char* letterScale = nullptr;

            if (letterKey (tokens[i], pc, accidental, letterScale))
            {
                // "Em" no começo da frase é a preposição ("Em 140 bpm, um trap"); só vira Mi menor
                // depois de uma palavra de tom ("tom Em", "trap em Em").
                const bool prepositionEm = tokens[i].lower == "em" && ! afterKeyWord;
                const char* nextScale = scaleWord (lowerAt (j));

                if (! prepositionEm && (letterScale != nullptr || accidental != 0 || nextScale != nullptr || afterKeyWord))
                {
                    out.root = pitchClass (pc + accidental);

                    if (letterScale != nullptr)
                        out.scale = letterScale;
                    else if (nextScale != nullptr)
                    {
                        out.scale = nextScale;
                        ++j;
                    }

                    keyFound = true;
                    i = j - 1;
                    continue;
                }
            }
        }
    }

    // O que foi entendido, na ordem em que aparece na tela.
    if (out.part)
        out.understood.push_back ("parte: " + std::string (partName (*out.part)));

    if (out.genre)
        if (auto* g = findGenre (*out.genre))
            out.understood.push_back ("gênero: " + std::string (g->name));

    if (out.mood)
        out.understood.push_back ("clima: " + std::string (moodName (*out.mood)));

    if (out.root)
    {
        const auto scale = out.scale.value_or ("minor");
        const bool flats = Scale::fromName (*out.root, scale).prefersFlats();
        out.understood.push_back ("tom: " + pitchNamePt (*out.root, flats) + " " + scaleNamePt (scale));
    }

    if (out.bars)
        out.understood.push_back (std::to_string (*out.bars) + (*out.bars == 1 ? " compasso" : " compassos"));

    if (out.bpm)
        out.understood.push_back (std::to_string (*out.bpm) + " BPM");

    if (out.complexity)
        out.understood.push_back (*out.complexity < 0.5 ? "mais simples" : "mais elaborado");

    if (out.density)
        out.understood.push_back (*out.density < 0.5 ? "menos notas" : "mais notas");

    return out;
}

} // namespace whynot::ideas
