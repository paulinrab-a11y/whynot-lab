#pragma once

// Tags automáticas do navegador de sons (frente tags-sons, 16/11), sem JUCE: o tipo do som (kick, snare, 808,
// loop...), o BPM e o tom, tirados só do nome do arquivo e das duas pastas de cima. Nada aqui abre o arquivo: é
// instantâneo e roda na thread do índice. A tabela é de palavras inteiras (em português e inglês, com as siglas
// dos kits); por isso "chat" não é hat, "snap" não é snare e "Amen" não é Lá menor.
//
// Teste: tests/library/test_sound_tags.cpp (compila com o cl, sem JUCE).

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace whynot::soundtags
{

//==============================================================================
enum class Type : std::uint8_t
{
    kick, snare, clap, snap, rim, hat, openHat, perc, tom, cymbal, s808, bass, fx, vocal, loop,
    melody, chord, pad, pluck, bell, lead, keys, guitar, strings, flute,
    count
};

inline constexpr int numTypes = (int) Type::count;

/** Um bit por tipo: um som pode ter mais de um ("bassline_loop" é loop e baixo). */
using Mask = std::uint32_t;

constexpr Mask bit (Type t) noexcept   { return Mask (1) << (int) t; }

struct TypeName
{
    const char* id;      // nos comandos e no tags-sons.json
    const char* label;   // no chip (UTF-8)
};

inline constexpr std::array<TypeName, numTypes> typeNames { {
    { "kick", "Kick" },       { "snare", "Snare" },   { "clap", "Clap" },     { "snap", "Snap" },       { "rim", "Rim" },
    { "hat", "Hat" },         { "openhat", "Open hat" }, { "perc", "Perc" },  { "tom", "Tom" },         { "cymbal", "Prato" },
    { "808", "808" },         { "bass", "Baixo" },    { "fx", "FX" },         { "vocal", "Voz" },       { "loop", "Loop" },
    { "melody", "Melodia" },  { "chord", "Acorde" },  { "pad", "Pad" },       { "pluck", "Pluck" },     { "bell", "Sino" },
    { "lead", "Lead" },       { "keys", "Teclas" },   { "guitar", "Guitarra" }, { "strings", "Cordas" }, { "flute", "Flauta" },
} };

constexpr const char* typeId (Type t) noexcept      { return (int) t < numTypes ? typeNames[(std::size_t) t].id : ""; }
constexpr const char* typeLabel (Type t) noexcept   { return (int) t < numTypes ? typeNames[(std::size_t) t].label : ""; }

/** O tipo pelo id ("kick", "openhat", "808"), sem diferença de caixa; -1 = não existe. */
inline int typeFromId (std::string_view id) noexcept
{
    for (int i = 0; i < numTypes; ++i)
    {
        const std::string_view known = typeNames[(std::size_t) i].id;

        if (known.size() != id.size())
            continue;

        bool same = true;

        for (std::size_t k = 0; k < id.size() && same; ++k)
        {
            const char c = id[k] >= 'A' && id[k] <= 'Z' ? (char) (id[k] - 'A' + 'a') : id[k];
            same = c == known[k];
        }

        if (same)
            return i;
    }

    return -1;
}

/** As peças de bateria: com uma delas no nome, o "808" é a bateria eletrônica (não o baixo) e um número solto é
    o número do som (não o BPM). */
inline constexpr Mask drumMask = bit (Type::kick) | bit (Type::snare) | bit (Type::clap) | bit (Type::snap) | bit (Type::rim)
                               | bit (Type::hat) | bit (Type::openHat) | bit (Type::perc) | bit (Type::tom) | bit (Type::cymbal);

enum class Mode : std::uint8_t { none, minor, major, note };   // note = só a nota ("A#", "808 F#"): sem maior nem menor

struct Tags
{
    Mask types = 0;
    Type main = Type::count;     // o que o som é antes de tudo (count = não deu para saber)
    int bpm = 0;                 // 0 = o nome não diz
    bool bpmExplicit = false;    // veio com "bpm" escrito ("140bpm"); false = um número solto ("_140_")
    int keyRoot = -1;            // 0 = Dó ... 11 = Si; -1 = o nome não diz
    Mode mode = Mode::none;

    bool has (Type t) const noexcept   { return (types & bit (t)) != 0; }
    bool hasKey() const noexcept       { return keyRoot >= 0; }
};

//==============================================================================
namespace detail
{
    constexpr bool isUpper (char c) noexcept   { return c >= 'A' && c <= 'Z'; }
    constexpr bool isLower (char c) noexcept   { return c >= 'a' && c <= 'z'; }
    constexpr bool isDigit (char c) noexcept   { return c >= '0' && c <= '9'; }
    constexpr bool isAlpha (char c) noexcept   { return isUpper (c) || isLower (c); }
    constexpr char lower (char c) noexcept     { return isUpper (c) ? (char) (c - 'A' + 'a') : c; }

    inline std::string lowered (std::string_view s)
    {
        std::string out (s);

        for (auto& c : out)
            c = lower (c);

        return out;
    }

    /** Tira os acentos do português (UTF-8 de dois bytes: "Percussão" → "Percussao"), mantendo a caixa. Qualquer
        outro byte fora do ASCII vira espaço (separador). */
    inline std::string foldAccents (std::string_view s)
    {
        std::string out;
        out.reserve (s.size());

        for (std::size_t i = 0; i < s.size(); ++i)
        {
            const auto c = (unsigned char) s[i];

            if (c < 0x80)
            {
                out += (char) c;
                continue;
            }

            if (c == 0xC3 && i + 1 < s.size())
            {
                auto b = (unsigned char) s[++i];
                const bool small = b >= 0xA0;

                if (small)
                    b = (unsigned char) (b - 0x20);

                char base = ' ';

                if (b >= 0x80 && b <= 0x85)       base = 'A';
                else if (b == 0x87)               base = 'C';
                else if (b >= 0x88 && b <= 0x8B)  base = 'E';
                else if (b >= 0x8C && b <= 0x8F)  base = 'I';
                else if (b == 0x91)               base = 'N';
                else if (b >= 0x92 && b <= 0x96)  base = 'O';
                else if (b >= 0x99 && b <= 0x9C)  base = 'U';

                out += small && base != ' ' ? lower (base) : base;
                continue;
            }

            out += ' ';

            while (i + 1 < s.size() && ((unsigned char) s[i + 1] & 0xC0) == 0x80)   // o resto do caractere
                ++i;
        }

        return out;
    }

    /** As palavras do nome, em minúsculas. Separa em tudo que não é letra nem número, entre letra e número
        ("Kick01", "140bpm", "TR808") e nas maiúsculas do meio ("OpenHat", "HHClosed", "OHat"). */
    inline std::vector<std::string> words (std::string_view folded)
    {
        std::vector<std::string> out;
        std::string current;

        auto flush = [&]
        {
            if (! current.empty())
            {
                out.push_back (lowered (current));
                current.clear();
            }
        };

        for (std::size_t i = 0; i < folded.size(); ++i)
        {
            const char c = folded[i];

            if (! isAlpha (c) && ! isDigit (c))
            {
                flush();
                continue;
            }

            if (! current.empty())
            {
                const char prev = current.back();
                const bool letterDigit = isDigit (c) != isDigit (prev);
                const bool camel = isUpper (c) && isLower (prev);
                const bool acronymEnd = isUpper (c) && isUpper (prev) && i + 1 < folded.size() && isLower (folded[i + 1]);

                if (letterDigit || camel || acronymEnd)
                    flush();
            }

            current += c;
        }

        flush();
        return out;
    }

    inline bool allDigits (std::string_view s) noexcept
    {
        if (s.empty())
            return false;

        for (char c : s)
            if (! isDigit (c))
                return false;

        return true;
    }

    inline int toNumber (std::string_view s) noexcept
    {
        int v = 0;

        for (char c : s)
        {
            if (v > 100000)
                return v;

            v = v * 10 + (c - '0');
        }

        return v;
    }

    //==========================================================================
    struct Word
    {
        Type type;
        bool weak;   // sigla curta ("hh", "oh", "sn"): só vale se o nome não tiver uma palavra forte
    };

    /** Palavra → tipo. As fracas são as siglas de duas ou três letras e as palavras que também são outra coisa
        ("tom" é o tom da música, "sub" é o sub drop, "oh" é o que o vocal canta). */
    inline const std::unordered_map<std::string_view, Word>& wordTable()
    {
        using T = Type;
        static const std::unordered_map<std::string_view, Word> table {
            // kick
            { "kick", { T::kick, false } },     { "kik", { T::kick, false } },      { "kck", { T::kick, false } },
            { "kickdrum", { T::kick, false } }, { "bassdrum", { T::kick, false } }, { "bumbo", { T::kick, false } },
            { "bd", { T::kick, true } },        { "kd", { T::kick, true } },
            // snare
            { "snare", { T::snare, false } },   { "snr", { T::snare, false } },     { "caixa", { T::snare, false } },
            { "sn", { T::snare, true } },       { "sd", { T::snare, true } },
            // clap e snap (estalo de dedo: não é snare)
            { "clap", { T::clap, false } },     { "palma", { T::clap, false } },    { "palmas", { T::clap, false } },
            { "clp", { T::clap, true } },
            { "snap", { T::snap, false } },     { "estalo", { T::snap, false } },
            // rim
            { "rim", { T::rim, false } },       { "rimshot", { T::rim, false } },   { "sidestick", { T::rim, false } },
            { "aro", { T::rim, true } },
            // hat fechado e aberto
            { "hat", { T::hat, false } },       { "hihat", { T::hat, false } },     { "closedhat", { T::hat, false } },
            { "chimbal", { T::hat, false } },   { "chimbau", { T::hat, false } },   { "chimbais", { T::hat, false } },
            { "chh", { T::hat, false } },       { "hh", { T::hat, true } },         { "ch", { T::hat, true } },
            { "openhat", { T::openHat, false } }, { "ohat", { T::openHat, false } }, { "ohh", { T::openHat, false } },
            { "oh", { T::openHat, true } },
            // percussão
            { "perc", { T::perc, false } },     { "percussion", { T::perc, false } }, { "percussao", { T::perc, false } },
            { "shaker", { T::perc, false } },   { "conga", { T::perc, false } },    { "bongo", { T::perc, false } },
            { "cowbell", { T::perc, false } },  { "clave", { T::perc, false } },    { "tambourine", { T::perc, false } },
            { "pandeiro", { T::perc, false } }, { "agogo", { T::perc, false } },    { "triangle", { T::perc, false } },
            { "triangulo", { T::perc, false } }, { "woodblock", { T::perc, false } }, { "timbale", { T::perc, false } },
            { "cuica", { T::perc, false } },    { "tamborim", { T::perc, false } }, { "atabaque", { T::perc, false } },
            { "tambor", { T::perc, false } },   { "ganza", { T::perc, false } },    { "chocalho", { T::perc, false } },
            { "prc", { T::perc, true } },       { "tamb", { T::perc, true } },
            // tom (o tambor): fraca, porque "tom" também é o tom da música
            { "tom", { T::tom, true } },        { "floortom", { T::tom, false } },
            // pratos
            { "crash", { T::cymbal, false } },  { "ride", { T::cymbal, false } },   { "cymbal", { T::cymbal, false } },
            { "prato", { T::cymbal, false } },  { "china", { T::cymbal, false } },  { "splash", { T::cymbal, false } },
            { "cym", { T::cymbal, true } },
            // 808 e baixo
            { "808", { T::s808, false } },
            { "bass", { T::bass, false } },     { "baixo", { T::bass, false } },    { "bassline", { T::bass, false } },
            { "subbass", { T::bass, false } },  { "reese", { T::bass, false } },    { "grave", { T::bass, false } },
            { "sub", { T::bass, true } },
            // efeitos
            { "fx", { T::fx, false } },         { "sfx", { T::fx, false } },        { "efx", { T::fx, false } },
            { "effect", { T::fx, false } },     { "efeito", { T::fx, false } },     { "riser", { T::fx, false } },
            { "uplifter", { T::fx, false } },   { "downlifter", { T::fx, false } }, { "sweep", { T::fx, false } },
            { "impact", { T::fx, false } },     { "whoosh", { T::fx, false } },     { "noise", { T::fx, false } },
            { "transition", { T::fx, false } }, { "foley", { T::fx, false } },      { "glitch", { T::fx, false } },
            { "siren", { T::fx, false } },      { "sirene", { T::fx, false } },     { "laser", { T::fx, false } },
            { "gunshot", { T::fx, false } },    { "tiro", { T::fx, false } },       { "scratch", { T::fx, false } },
            { "atmos", { T::fx, false } },      { "ambience", { T::fx, false } },   { "texture", { T::fx, false } },
            { "drop", { T::fx, true } },
            // voz
            { "vocal", { T::vocal, false } },   { "vox", { T::vocal, false } },     { "voz", { T::vocal, false } },
            { "voice", { T::vocal, false } },   { "adlib", { T::vocal, false } },   { "chant", { T::vocal, false } },
            { "choir", { T::vocal, false } },   { "coro", { T::vocal, false } },    { "acapella", { T::vocal, false } },
            { "shout", { T::vocal, false } },   { "grito", { T::vocal, false } },   { "hey", { T::vocal, true } },
            { "yeah", { T::vocal, true } },
            // loop
            { "loop", { T::loop, false } },     { "breakbeat", { T::loop, false } }, { "drumloop", { T::loop, false } },
            { "toploop", { T::loop, false } },  { "lp", { T::loop, true } },
            // melódicos
            { "melody", { T::melody, false } }, { "melodia", { T::melody, false } }, { "melodies", { T::melody, false } },
            { "melodic", { T::melody, false } }, { "riff", { T::melody, false } },  { "arp", { T::melody, false } },
            { "arpeggio", { T::melody, false } }, { "mel", { T::melody, true } },   { "melo", { T::melody, true } },
            { "synth", { T::melody, true } },
            { "chord", { T::chord, false } },   { "acorde", { T::chord, false } },  { "progression", { T::chord, false } },
            { "stab", { T::chord, false } },
            { "pad", { T::pad, false } },
            { "pluck", { T::pluck, false } },
            { "bell", { T::bell, false } },     { "sino", { T::bell, false } },     { "chime", { T::bell, false } },
            { "glock", { T::bell, false } },    { "glockenspiel", { T::bell, false } },
            { "lead", { T::lead, false } },
            { "piano", { T::keys, false } },    { "keys", { T::keys, false } },     { "rhodes", { T::keys, false } },
            { "epiano", { T::keys, false } },   { "organ", { T::keys, false } },    { "orgao", { T::keys, false } },
            { "teclado", { T::keys, false } },
            { "guitar", { T::guitar, false } }, { "guitarra", { T::guitar, false } }, { "violao", { T::guitar, false } },
            { "gtr", { T::guitar, false } },
            { "strings", { T::strings, false } }, { "violin", { T::strings, false } }, { "violino", { T::strings, false } },
            { "cello", { T::strings, false } }, { "cordas", { T::strings, false } },
            { "flute", { T::flute, false } },   { "flauta", { T::flute, false } },
        };

        return table;
    }

    struct Phrase
    {
        std::string_view first, second;
        Type type;
    };

    /** Duas palavras seguidas que valem por uma (e gastam as duas: "bass drum" é kick, não baixo). */
    inline constexpr std::array phrases {
        Phrase { "bass", "drum", Type::kick },     Phrase { "kick", "drum", Type::kick },
        Phrase { "open", "hat", Type::openHat },   Phrase { "open", "hh", Type::openHat },   Phrase { "open", "hihat", Type::openHat },
        Phrase { "o", "hat", Type::openHat },      Phrase { "hat", "open", Type::openHat },  Phrase { "hh", "open", Type::openHat },
        Phrase { "chimbal", "aberto", Type::openHat },
        Phrase { "closed", "hat", Type::hat },     Phrase { "closed", "hh", Type::hat },     Phrase { "hi", "hat", Type::hat },
        Phrase { "c", "hat", Type::hat },          Phrase { "chimbal", "fechado", Type::hat },
        Phrase { "sub", "bass", Type::bass },      Phrase { "bass", "line", Type::bass },
        Phrase { "rim", "shot", Type::rim },       Phrase { "side", "stick", Type::rim },
        Phrase { "floor", "tom", Type::tom },      Phrase { "hi", "tom", Type::tom },        Phrase { "low", "tom", Type::tom },
        Phrase { "mid", "tom", Type::tom },        Phrase { "rack", "tom", Type::tom },
        Phrase { "drum", "loop", Type::loop },     Phrase { "top", "loop", Type::loop },     Phrase { "break", "beat", Type::loop },
        Phrase { "ad", "lib", Type::vocal },       Phrase { "e", "piano", Type::keys },
    };

    /** O singular de um plural simples ("kicks", "808s" já vem separado, "hats"): tira o "s" do fim. */
    inline const Word* findWord (const std::string& w)
    {
        const auto& table = wordTable();

        if (const auto it = table.find (w); it != table.end())
            return &it->second;

        if (w.size() > 3 && w.back() == 's')
            if (const auto it = table.find (std::string_view (w).substr (0, w.size() - 1)); it != table.end())
                return &it->second;

        return nullptr;
    }

    struct Found
    {
        Mask strong = 0, weak = 0;
        bool explicitLoop = false;
        bool phraseTom = false;   // "floor tom": tambor com certeza
        bool open = false;        // "open", "aberto" soltos no nome: o hat que houver é o aberto
    };

    inline Found typesIn (const std::vector<std::string>& w)
    {
        Found f;

        for (std::size_t i = 0; i < w.size(); ++i)
        {
            bool usedPhrase = false;

            if (i + 1 < w.size())
            {
                // o plural da segunda palavra também ("open hats")
                auto second = std::string_view (w[i + 1]);

                for (int attempt = 0; attempt < 2 && ! usedPhrase; ++attempt)
                {
                    for (auto& p : phrases)
                    {
                        if (p.first == w[i] && p.second == second)
                        {
                            f.strong |= bit (p.type);
                            f.explicitLoop = f.explicitLoop || p.type == Type::loop;
                            f.phraseTom = f.phraseTom || p.type == Type::tom;
                            usedPhrase = true;
                            break;
                        }
                    }

                    if (second.size() > 3 && second.back() == 's')
                        second.remove_suffix (1);
                    else
                        break;
                }
            }

            if (usedPhrase)
            {
                ++i;
                continue;
            }

            if (w[i] == "808" && i > 0 && w[i - 1] == "tr")
                continue;   // TR-808: a bateria eletrônica

            f.open = f.open || w[i] == "open" || w[i] == "opn" || w[i] == "aberto";

            if (const auto* word = findWord (w[i]))
            {
                (word->weak ? f.weak : f.strong) |= bit (word->type);
                f.explicitLoop = f.explicitLoop || (word->type == Type::loop && ! word->weak);
            }
        }

        return f;
    }

    //==========================================================================
    // O tom

    constexpr int letterValue (char c) noexcept
    {
        switch (c)
        {
            case 'C': case 'c': return 0;
            case 'D': case 'd': return 2;
            case 'E': case 'e': return 4;
            case 'F': case 'f': return 5;
            case 'G': case 'g': return 7;
            case 'A': case 'a': return 9;
            case 'B': case 'b': return 11;
            default:            return -1;
        }
    }

    inline int solfegeValue (std::string_view lowerWord) noexcept
    {
        static constexpr std::pair<std::string_view, int> names[] { { "do", 0 }, { "re", 2 }, { "mi", 4 }, { "fa", 5 },
                                                                    { "sol", 7 }, { "la", 9 }, { "si", 11 } };

        for (auto& [name, value] : names)
            if (lowerWord == name)
                return value;

        return -1;
    }

    /** 0 = menor, 1 = maior, -1 = não é. `portuguese`: aceita "menor" e "maior". */
    inline int modeWord (std::string_view lowerWord, bool portuguese) noexcept
    {
        if (lowerWord == "min" || lowerWord == "minor" || (portuguese && lowerWord == "menor"))
            return 0;

        if (lowerWord == "maj" || lowerWord == "major" || (portuguese && lowerWord == "maior"))
            return 1;

        return -1;
    }

    inline int accidentalWord (std::string_view lowerWord) noexcept
    {
        return lowerWord == "sharp" || lowerWord == "sustenido" || lowerWord == "#" ? 1
             : lowerWord == "flat" || lowerWord == "bemol" ? -1 : 0;
    }

    struct NotePart
    {
        int root = -1;
        bool upper = false, accidental = false, sharp = false;
        std::string rest;   // como veio ("m", "M", "min"...)
    };

    inline NotePart splitNote (std::string_view token)
    {
        NotePart n;

        if (token.empty() || letterValue (token[0]) < 0)
            return n;

        n.root = letterValue (token[0]);
        n.upper = isUpper (token[0]);
        std::size_t at = 1;

        if (token.size() > 1 && token[1] == '#')
        {
            n.root = (n.root + 1) % 12;
            n.accidental = n.sharp = true;
            at = 2;
        }
        else if (token.size() > 1 && token[1] == 'b')
        {
            n.root = (n.root + 11) % 12;
            n.accidental = true;
            at = 2;
        }

        n.rest = std::string (token.substr (at));
        return n;
    }

    /** As palavras para o tom: com a caixa e o "#", sem os números das pontas ("Am7" → "Am", "F#1" → "F#"). */
    inline std::vector<std::string> keyTokens (std::string_view folded)
    {
        std::vector<std::string> out;
        std::string current;

        auto flush = [&]
        {
            std::size_t from = 0, to = current.size();

            while (from < to && isDigit (current[from]))  ++from;
            while (to > from && isDigit (current[to - 1])) --to;

            if (to > from)
                out.push_back (current.substr (from, to - from));

            current.clear();
        };

        for (char c : folded)
        {
            if (isAlpha (c) || isDigit (c) || c == '#')
                current += c;
            else
                flush();
        }

        flush();
        return out;
    }

    struct Key
    {
        int root = -1;
        Mode mode = Mode::none;
    };

    inline Key keyIn (std::string_view folded, bool accentedDo)
    {
        const auto tokens = keyTokens (folded);
        bool anyLetter = false, anyLower = false, anyUpper = false;

        for (char c : folded)
        {
            anyLetter = anyLetter || isAlpha (c);
            anyLower = anyLower || isLower (c);
            anyUpper = anyUpper || isUpper (c);
        }

        const bool allCaps = anyLetter && ! anyLower, allLower = anyLetter && ! anyUpper;

        // 1) por extenso: "Cmin", "Bbmaj", "F minor", "A sharp minor", "Lá menor", "Si bemol maior"
        for (std::size_t i = 0; i < tokens.size(); ++i)
        {
            const auto& t = tokens[i];
            const auto low = lowered (t);

            // o nome da nota em português, sempre com "menor" ou "maior" depois
            {
                auto base = std::string_view (low);
                int shift = 0;

                if (base.size() > 2 && base.back() == '#')       { shift = 1; base.remove_suffix (1); }
                else if (base.size() > 2 && base.back() == 'b')  { shift = -1; base.remove_suffix (1); }

                // "Beat do Menor" (o menor é o MC): o "do" só é a nota com o acento ("Dó menor") ou o acidente
                if (const int value = solfegeValue (base); value >= 0 && (base != "do" || accentedDo || shift != 0))
                {
                    auto next = i + 1;

                    if (next < tokens.size() && shift == 0)
                        if (const int acc = accidentalWord (lowered (tokens[next])); acc != 0)
                        {
                            shift = acc;
                            ++next;
                        }

                    if (next < tokens.size())
                        if (const auto word = lowered (tokens[next]); word == "menor" || word == "maior")
                            return { (value + shift + 12) % 12, word == "maior" ? Mode::major : Mode::minor };
                }
            }

            const auto note = splitNote (t);

            if (note.root < 0)
                continue;

            if (! note.rest.empty())
            {
                if (const int mode = modeWord (lowered (note.rest), true); mode >= 0)
                    return { note.root, mode == 1 ? Mode::major : Mode::minor };

                continue;
            }

            // a letra sozinha, com o acidente e o modo nas palavras seguintes. Sem acidente, só com o modo em inglês:
            // "a menor" e "e maior" são português corrente, não tom (em português o tom vem como "Lá menor")
            int root = note.root;
            auto next = i + 1;
            bool accidental = note.accidental;

            if (next < tokens.size())
                if (const int acc = accidentalWord (lowered (tokens[next])); acc != 0)
                {
                    root = (root + acc + 12) % 12;
                    accidental = true;
                    ++next;
                }

            if (next < tokens.size())
                if (const int mode = modeWord (lowered (tokens[next]), accidental); mode >= 0)
                    return { root, mode == 1 ? Mode::major : Mode::minor };
        }

        // um número com cara de BPM no nome: num nome todo em minúsculas, "am" e "dm" passam a ser tom
        bool hasTempo = false;

        for (auto& w : words (folded))
            hasTempo = hasTempo || w == "bpm" || (allDigits (w) && w[0] != '0' && toNumber (w) >= 60 && toNumber (w) <= 200);

        // 2) as curtas: "Am", "F#m", "Bbm"; "AM" num nome todo em maiúsculas; "am" num nome todo em minúsculas com BPM
        for (auto& t : tokens)
        {
            const auto note = splitNote (t);

            if (note.root < 0)
                continue;

            if (note.rest == "m")
            {
                if (note.upper || note.sharp)
                    return { note.root, Mode::minor };

                // "em" é a preposição, "fm" é a síntese e "gm" o General MIDI
                if (allLower && hasTempo && ! note.accidental && (t == "am" || t == "bm" || t == "cm" || t == "dm"))
                    return { note.root, Mode::minor };
            }
            else if (note.rest == "M")
            {
                if (allCaps && note.upper && t != "FM" && t != "GM")
                    return { note.root, Mode::minor };
            }
        }

        // 3) só a nota, com o acidente: "A#", "Bb", "f#" (uma letra sozinha é o take ou a versão)
        for (auto& t : tokens)
        {
            const auto note = splitNote (t);

            if (note.root >= 0 && note.rest.empty() && note.accidental && (note.upper || note.sharp))
                return { note.root, Mode::note };
        }

        return {};
    }

    //==========================================================================
    // O BPM

    inline bool notTempoBefore (std::string_view w) noexcept
    {
        for (std::string_view x : { "v", "vol", "volume", "pt", "part", "parte", "take", "kit", "no", "n", "ver", "version", "pack", "tr", "x" })
            if (w == x)
                return true;

        return false;
    }

    inline bool notTempoAfter (std::string_view w) noexcept
    {
        for (std::string_view x : { "k", "khz", "hz", "bit", "bits", "db", "ms", "st", "bar", "bars", "x", "s", "th", "compassos" })
            if (w == x)
                return true;

        return false;
    }

    /** "140bpm", "140 BPM", "bpm140" (de 40 a 300); devolve 0 se não há. */
    inline int explicitBpm (const std::vector<std::string>& w) noexcept
    {
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            if (w[i] != "bpm")
                continue;

            if (i > 0 && allDigits (w[i - 1]))
                if (const int v = toNumber (w[i - 1]); v >= 40 && v <= 300)
                    return v;

            if (i + 1 < w.size() && allDigits (w[i + 1]))
                if (const int v = toNumber (w[i + 1]); v >= 40 && v <= 300)
                    return v;
        }

        return 0;
    }

    /** Um número solto com cara de andamento ("_140_"): de 60 a 200, sem zero na frente, e que não é a versão
        ("v2", "vol 90"), a taxa ("96k"), os compassos ("128 bars") nem o número de um som. */
    inline int looseBpm (const std::vector<std::string>& w) noexcept
    {
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            if (! allDigits (w[i]) || w[i][0] == '0')
                continue;

            const int v = toNumber (w[i]);

            if (v < 60 || v > 200)
                continue;

            if ((i > 0 && notTempoBefore (w[i - 1])) || (i + 1 < w.size() && notTempoAfter (w[i + 1])))
                continue;

            return v;
        }

        return 0;
    }

    /** O nome traz "Dó" com o acento (o que separa a nota do "do" de "Beat do Menor")? */
    inline bool hasAccentedDo (std::string_view name) noexcept
    {
        for (std::size_t i = 0; i + 2 < name.size(); ++i)
            if ((name[i] == 'D' || name[i] == 'd') && (unsigned char) name[i + 1] == 0xC3
                && ((unsigned char) name[i + 2] == 0xB3 || (unsigned char) name[i + 2] == 0x93))
                return true;

        return false;
    }

    inline Type mainOf (Mask types, bool explicitLoop) noexcept
    {
        if (explicitLoop && (types & bit (Type::loop)) != 0)
            return Type::loop;

        for (int i = 0; i < numTypes; ++i)
            if (i != (int) Type::loop && (types & (Mask (1) << i)) != 0)
                return (Type) i;

        return (types & bit (Type::loop)) != 0 ? Type::loop : Type::count;
    }

    /** Os tipos de um nome (de arquivo ou de pasta), já com as regras de desempate. */
    inline Mask resolve (const Found& f, bool nameHasKeyWithMode) noexcept
    {
        Mask types = f.strong != 0 ? f.strong : f.weak;

        // "Tom Am": o tom da música. O tambor só com "floor tom" e afins, ou sem tom no nome.
        if (nameHasKeyWithMode && ! f.phraseTom)
            types &= ~bit (Type::tom);

        if ((types & bit (Type::s808)) != 0)
        {
            if ((types & drumMask) != 0)
                types &= ~bit (Type::s808);   // "808 Snare", "Kick 808": a peça da bateria eletrônica
            else
                types &= ~bit (Type::bass);   // "808 Bass" é um 808
        }

        if (f.open && (types & bit (Type::hat)) != 0)
            types = (types & ~bit (Type::hat)) | bit (Type::openHat);   // "Open Hi Hat", "Hihat Open"

        if ((types & bit (Type::openHat)) != 0 && (f.strong & bit (Type::hat)) == 0)
            types &= ~bit (Type::hat);        // "OH" e "HH" juntos numa sigla: fica o aberto

        return types;
    }
}

//==============================================================================
/** O BPM escrito num nome ("140bpm", "bpm_92") ou 0. */
inline int bpmFromName (std::string_view name)
{
    return detail::explicitBpm (detail::words (detail::foldAccents (name)));
}

/** O tom escrito num nome: a tônica (0 = Dó; -1 = não há) e o modo. */
inline std::pair<int, Mode> keyFromName (std::string_view name)
{
    const auto key = detail::keyIn (detail::foldAccents (name), detail::hasAccentedDo (name));
    return { key.root, key.mode };
}

/** Só os tipos de um nome (sem pasta). */
inline Mask typesFromName (std::string_view name)
{
    const auto folded = detail::foldAccents (name);
    const auto key = detail::keyIn (folded, detail::hasAccentedDo (name));
    return detail::resolve (detail::typesIn (detail::words (folded)), key.mode == Mode::minor || key.mode == Mode::major);
}

/** As tags de um som pelo nome do arquivo (sem a extensão) e pelas pastas de cima, da mais perto para a mais
    longe (só as duas primeiras contam). A pasta só fala quando o nome não diz o que o som é. */
inline Tags tagsFor (std::string_view nameWithoutExtension, const std::vector<std::string_view>& parentFolders)
{
    using namespace detail;
    Tags tags;
    const auto folded = foldAccents (nameWithoutExtension);
    const auto w = words (folded);
    const auto key = keyIn (folded, hasAccentedDo (nameWithoutExtension));
    const bool keyWithMode = key.mode == Mode::minor || key.mode == Mode::major;
    const auto found = typesIn (w);

    tags.keyRoot = key.root;
    tags.mode = key.mode;
    tags.types = resolve (found, keyWithMode);
    bool explicitLoop = found.explicitLoop;

    // a pasta: "Kicks/01.wav" é kick; "Loops/Hats 140.wav" é loop de hat
    Mask folderTypes = 0;

    for (std::size_t i = 0; i < parentFolders.size() && i < 2 && folderTypes == 0; ++i)
        folderTypes = resolve (typesIn (words (foldAccents (parentFolders[i]))), false);

    if ((tags.types & ~bit (Type::loop)) == 0)
    {
        tags.types |= folderTypes;

        if ((tags.types & bit (Type::s808)) != 0)
            tags.types &= ~bit (Type::bass);
    }
    else if ((folderTypes & bit (Type::loop)) != 0)
    {
        tags.types |= bit (Type::loop);
    }

    explicitLoop = explicitLoop || (folderTypes & bit (Type::loop)) != 0;

    // o BPM: com "bpm" escrito vale sempre; o número solto só num loop, num som com o tom escrito ("Dark 140 Am") ou
    // num som melódico (não numa peça de bateria, num 808, num baixo ou num efeito: lá é o número do som)
    if (const int bpm = explicitBpm (w); bpm > 0)
    {
        tags.bpm = bpm;
        tags.bpmExplicit = true;
        tags.types |= bit (Type::loop);   // one-shot não tem andamento
    }
    else if (const int loose = looseBpm (w); loose > 0)
    {
        const Mask oneShot = drumMask | bit (Type::s808) | bit (Type::bass) | bit (Type::fx);

        if ((tags.types & bit (Type::loop)) != 0 || keyWithMode || (tags.types != 0 && (tags.types & oneShot) == 0))
        {
            tags.bpm = loose;
            tags.types |= bit (Type::loop);
        }
    }

    tags.main = mainOf (tags.types, explicitLoop);
    return tags;
}

/** O mesmo, a partir do caminho inteiro (UTF-8, com "/" ou "\\"): tira a extensão e separa as pastas. */
inline Tags tagsForPath (std::string_view path)
{
    std::vector<std::string_view> parts;
    std::size_t from = 0;

    for (std::size_t i = 0; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == '/' || path[i] == '\\')
        {
            if (i > from)
                parts.push_back (path.substr (from, i - from));

            from = i + 1;
        }
    }

    if (parts.empty())
        return {};

    auto name = parts.back();
    parts.pop_back();

    if (const auto dot = name.rfind ('.'); dot != std::string_view::npos && dot > 0 && name.size() - dot <= 6)
        name = name.substr (0, dot);

    std::vector<std::string_view> parents;

    for (auto it = parts.rbegin(); it != parts.rend() && parents.size() < 2; ++it)
        parents.push_back (*it);

    return tagsFor (name, parents);
}

//==============================================================================
/** "Am", "F#", "Cmaj": para os comandos e o teste. Vazio sem tom. */
inline std::string keyName (int root, Mode mode)
{
    static constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    if (root < 0 || mode == Mode::none)
        return {};

    std::string out = names[root % 12];
    return mode == Mode::minor ? out + "m" : mode == Mode::major ? out + "maj" : out;
}

/** A até `tolerance` (4%) do andamento pedido, da metade ou do dobro dele. */
inline bool bpmNear (double bpm, double target, double tolerance = 0.04) noexcept
{
    if (! (bpm > 0.0) || ! (target > 0.0))
        return false;

    for (const double t : { target, target * 0.5, target * 2.0 })
        if (std::abs (bpm - t) <= tolerance * t)
            return true;

    return false;
}

/** O tom do som combina com o tom pedido? O mesmo tom ou o relativo (Lá menor e Dó maior: as mesmas notas). Um
    som que só tem a nota (808 em Fá#) combina quando a nota está na escala pedida. */
inline bool keyMatches (int root, Mode mode, int wantedRoot, bool wantedMajor) noexcept
{
    if (root < 0 || wantedRoot < 0 || mode == Mode::none)
        return false;

    // tudo como o tom maior de mesmas notas: Lá menor → Dó
    const int wantedTonic = ((wantedMajor ? wantedRoot : wantedRoot + 3) % 12 + 12) % 12;

    if (mode == Mode::note)
    {
        static constexpr int steps[] { 0, 2, 4, 5, 7, 9, 11 };
        const int degree = ((root - wantedTonic) % 12 + 12) % 12;

        for (int s : steps)
            if (s == degree)
                return true;

        return false;
    }

    return ((mode == Mode::major ? root : root + 3) % 12 + 12) % 12 == wantedTonic;
}

/** O que o usuário corrigiu vale mais que a regra: os tipos dele no lugar dos automáticos (o BPM e o tom ficam). */
inline Tags withOverride (Tags tags, bool hasOverride, Mask overrideTypes) noexcept
{
    if (hasOverride)
    {
        tags.types = overrideTypes;
        tags.main = detail::mainOf (overrideTypes, (overrideTypes & bit (Type::loop)) != 0);
    }

    return tags;
}

/** O filtro dos chips: OU entre os tipos escolhidos; E com o BPM e o tom. 0 e -1 = sem aquele filtro. */
struct Filter
{
    Mask types = 0;
    double bpm = 0.0;
    int keyRoot = -1;
    bool keyMajor = false;

    bool any() const noexcept   { return types != 0 || bpm > 0.0 || keyRoot >= 0; }
};

inline bool passes (const Tags& tags, const Filter& f) noexcept
{
    if (f.types != 0 && (tags.types & f.types) == 0)
        return false;

    if (f.bpm > 0.0 && ! bpmNear ((double) tags.bpm, f.bpm))
        return false;

    if (f.keyRoot >= 0 && ! keyMatches (tags.keyRoot, tags.mode, f.keyRoot, f.keyMajor))
        return false;

    return true;
}

} // namespace whynot::soundtags
