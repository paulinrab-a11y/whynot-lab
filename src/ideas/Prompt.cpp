#include "Prompt.h"

#include "Theory.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace whynot::ideas
{

namespace
{
    /** A letra sem acento de um caractere "C3 xx" do UTF-8 (á, Ç, õ...), em minúscula; 0 = não é letra. */
    char latinLetter (unsigned char d, bool& upper)
    {
        upper = d < 0xA0;
        const unsigned x = upper ? d + 0x20u : d;

        if (x >= 0xA0 && x <= 0xA5)   return 'a';
        if (x == 0xA7)                return 'c';
        if (x >= 0xA8 && x <= 0xAB)   return 'e';
        if (x >= 0xAC && x <= 0xAF)   return 'i';
        if (x == 0xB1)                return 'n';
        if (x >= 0xB2 && x <= 0xB6)   return 'o';
        if (x >= 0xB9 && x <= 0xBC)   return 'u';
        return 0;
    }

    struct Token
    {
        std::string lower;      // sem acento, minúsculas: é o que as tabelas comparam
        std::string original;   // sem acento, com as maiúsculas ("Em", "F#m")
        std::string raw;        // como foi escrito, com acento: é o que volta para a tela
    };

    /** Palavras de letras, números, '#' e '&'. O hífen separa: "lo-fi" = "lo fi", "hi-hat" = "hi hat". */
    std::vector<Token> tokenize (std::string_view text)
    {
        std::vector<Token> out;
        Token cur;

        auto flush = [&]
        {
            if (! cur.original.empty())
                out.push_back (cur);

            cur = {};
        };

        for (size_t i = 0; i < text.size(); ++i)
        {
            const auto c = (unsigned char) text[i];
            size_t len = 1;
            char folded = 0;

            if (c == 0xC3 && i + 1 < text.size())
            {
                bool upper = false;
                folded = latinLetter ((unsigned char) text[i + 1], upper);

                if (folded != 0 && upper)
                    folded = (char) std::toupper ((unsigned char) folded);

                len = 2;
            }
            else if (c < 0x80)
            {
                folded = (char) c;
            }

            if (folded != 0 && (std::isalnum ((unsigned char) folded) || folded == '#' || folded == '&'))
            {
                cur.original += folded;
                cur.lower += (char) std::tolower ((unsigned char) folded);
                cur.raw.append (text.substr (i, len));
            }
            else
            {
                flush();
            }

            i += len - 1;
        }

        flush();
        return out;
    }

    bool isNumber (const std::string& s)
    {
        return ! s.empty() && std::all_of (s.begin(), s.end(), [] (char c) { return std::isdigit ((unsigned char) c) != 0; });
    }

    template <size_t N>
    bool inList (const char* const (&list)[N], const std::string& word)
    {
        for (auto* w : list)
            if (word == w)
                return true;

        return false;
    }

    //==========================================================================
    // A tabela de expressões. A mais longa que casar na posição ganha, então "linha de baixo"
    // vence "linha", "pra baixo" vence "baixo" e "brazilian phonk" vence "phonk".
    // Texto já sem acento e em minúsculas; palavras separadas por um espaço (o hífen vira espaço).
    // Erros de digitação entram aqui, um por um, só quando não há outra leitura possível.
    enum class Kind { part, genre, mood, complexity, density };

    struct Phrase
    {
        const char* text;
        Kind kind;
        const char* id;     // parte, gênero ou clima
        double amount;      // complexidade ou densidade
    };

    constexpr int maxPhraseWords = 3;

    const Phrase phrases[] = {
        // Partes
        { "linha de baixo", Kind::part, "bass", 0 },   { "linha do baixo", Kind::part, "bass", 0 },
        { "linha de 808", Kind::part, "808", 0 },      { "linha do 808", Kind::part, "808", 0 },
        { "baixo 808", Kind::part, "808", 0 },         { "808 bass", Kind::part, "808", 0 },
        { "linha melodica", Kind::part, "melody", 0 },
        { "hi hat", Kind::part, "drums", 0 },          { "hi hats", Kind::part, "drums", 0 },
        { "hihat", Kind::part, "drums", 0 },           { "hihats", Kind::part, "drums", 0 },
        { "chimbal", Kind::part, "drums", 0 },         { "chimbau", Kind::part, "drums", 0 },
        { "bumbo", Kind::part, "drums", 0 },           { "kick", Kind::part, "drums", 0 },
        { "snare", Kind::part, "drums", 0 },
        { "acordes", Kind::part, "chords", 0 },        { "acorde", Kind::part, "chords", 0 },
        { "acordis", Kind::part, "chords", 0 },        { "harmonia", Kind::part, "chords", 0 },
        { "chords", Kind::part, "chords", 0 },         { "chord", Kind::part, "chords", 0 },
        { "progressao", Kind::part, "chords", 0 },
        { "melodia", Kind::part, "melody", 0 },        { "melodias", Kind::part, "melody", 0 },
        { "melody", Kind::part, "melody", 0 },         { "lead", Kind::part, "melody", 0 },
        { "linha", Kind::part, "melody", 0 },
        { "baixo", Kind::part, "bass", 0 },            { "bass", Kind::part, "bass", 0 },
        { "bassline", Kind::part, "bass", 0 },         { "baixaria", Kind::part, "bass", 0 },
        { "808", Kind::part, "808", 0 },               { "808s", Kind::part, "808", 0 },
        { "arpejo", Kind::part, "arp", 0 },            { "arpejos", Kind::part, "arp", 0 },
        { "arp", Kind::part, "arp", 0 },               { "arpeggio", Kind::part, "arp", 0 },
        { "arpegio", Kind::part, "arp", 0 },
        { "bateria", Kind::part, "drums", 0 },         { "batera", Kind::part, "drums", 0 },
        { "batida", Kind::part, "drums", 0 },          { "drums", Kind::part, "drums", 0 },
        { "drum", Kind::part, "drums", 0 },            { "percussao", Kind::part, "drums", 0 },

        // Gêneros (os sub-estilos caem no gênero-pai: o gerador ainda não escolhe a batida pelo nome)
        { "boom bap", Kind::genre, "lofi", 0 },        { "lo fi", Kind::genre, "lofi", 0 },
        { "hip hop", Kind::genre, "rap", 0 },          { "jersey club", Kind::genre, "jersey", 0 },
        { "brazilian phonk", Kind::genre, "phonk", 0 }, { "phonk brasileiro", Kind::genre, "phonk", 0 },
        { "baile funk", Kind::genre, "funk", 0 },
        { "r and b", Kind::genre, "rnb", 0 },          { "r n b", Kind::genre, "rnb", 0 },
        { "r & b", Kind::genre, "rnb", 0 },            { "r e b", Kind::genre, "rnb", 0 },
        { "afro beat", Kind::genre, "afro", 0 },       { "afro beats", Kind::genre, "afro", 0 },
        { "trap", Kind::genre, "trap", 0 },
        { "drill", Kind::genre, "drill", 0 },          { "dril", Kind::genre, "drill", 0 },
        { "funk", Kind::genre, "funk", 0 },            { "mandelao", Kind::genre, "funk", 0 },
        { "baile", Kind::genre, "funk", 0 },           { "automotivo", Kind::genre, "funk", 0 },
        { "bruxaria", Kind::genre, "funk", 0 },        { "tamborzao", Kind::genre, "funk", 0 },
        { "rap", Kind::genre, "rap", 0 },              { "hiphop", Kind::genre, "rap", 0 },
        { "pop", Kind::genre, "pop", 0 },
        { "rnb", Kind::genre, "rnb", 0 },              { "r&b", Kind::genre, "rnb", 0 },
        { "soul", Kind::genre, "rnb", 0 },
        { "phonk", Kind::genre, "phonk", 0 },          { "drift", Kind::genre, "phonk", 0 },
        { "lofi", Kind::genre, "lofi", 0 },            { "boombap", Kind::genre, "lofi", 0 },
        { "afro", Kind::genre, "afro", 0 },            { "afrobeat", Kind::genre, "afro", 0 },
        { "afrobeats", Kind::genre, "afro", 0 },       { "amapiano", Kind::genre, "afro", 0 },
        { "reggaeton", Kind::genre, "reggaeton", 0 },  { "reggaetton", Kind::genre, "reggaeton", 0 },
        { "reggeaton", Kind::genre, "reggaeton", 0 },  { "regueton", Kind::genre, "reggaeton", 0 },
        { "dembow", Kind::genre, "reggaeton", 0 },
        { "house", Kind::genre, "house", 0 },          { "eletronica", Kind::genre, "house", 0 },
        { "plugg", Kind::genre, "plugg", 0 },          { "pluggnb", Kind::genre, "plugg", 0 },
        { "jersey", Kind::genre, "jersey", 0 },

        // Climas
        { "pra cima", Kind::mood, "happy", 0 },        { "para cima", Kind::mood, "happy", 0 },
        { "pra baixo", Kind::mood, "sad", 0 },         { "para baixo", Kind::mood, "sad", 0 },
        { "sombrio", Kind::mood, "dark", 0 },          { "sombria", Kind::mood, "dark", 0 },
        { "dark", Kind::mood, "dark", 0 },             { "escuro", Kind::mood, "dark", 0 },
        { "escura", Kind::mood, "dark", 0 },           { "pesado", Kind::mood, "dark", 0 },
        { "pesada", Kind::mood, "dark", 0 },           { "agressivo", Kind::mood, "dark", 0 },
        { "agressiva", Kind::mood, "dark", 0 },        { "tenso", Kind::mood, "dark", 0 },
        { "tensa", Kind::mood, "dark", 0 },            { "misterioso", Kind::mood, "dark", 0 },
        { "misteriosa", Kind::mood, "dark", 0 },       { "sinistro", Kind::mood, "dark", 0 },
        { "sinistra", Kind::mood, "dark", 0 },
        { "triste", Kind::mood, "sad", 0 },            { "sad", Kind::mood, "sad", 0 },
        { "melancolico", Kind::mood, "sad", 0 },       { "melancolica", Kind::mood, "sad", 0 },
        { "emo", Kind::mood, "sad", 0 },               { "saudade", Kind::mood, "sad", 0 },
        { "depre", Kind::mood, "sad", 0 },
        { "calmo", Kind::mood, "chill", 0 },           { "calma", Kind::mood, "chill", 0 },
        { "chill", Kind::mood, "chill", 0 },           { "relax", Kind::mood, "chill", 0 },
        { "relaxante", Kind::mood, "chill", 0 },       { "tranquilo", Kind::mood, "chill", 0 },
        { "tranquila", Kind::mood, "chill", 0 },       { "suave", Kind::mood, "chill", 0 },
        { "leve", Kind::mood, "chill", 0 },
        { "alegre", Kind::mood, "happy", 0 },          { "feliz", Kind::mood, "happy", 0 },
        { "happy", Kind::mood, "happy", 0 },           { "animado", Kind::mood, "happy", 0 },
        { "animada", Kind::mood, "happy", 0 },         { "positivo", Kind::mood, "happy", 0 },
        { "positiva", Kind::mood, "happy", 0 },        { "dancante", Kind::mood, "happy", 0 },
        { "epico", Kind::mood, "epic", 0 },            { "epica", Kind::mood, "epic", 0 },
        { "epic", Kind::mood, "epic", 0 },             { "grandioso", Kind::mood, "epic", 0 },
        { "grandiosa", Kind::mood, "epic", 0 },        { "intenso", Kind::mood, "epic", 0 },
        { "intensa", Kind::mood, "epic", 0 },          { "hino", Kind::mood, "epic", 0 },

        // Complexidade
        { "simples", Kind::complexity, "", 0.2 },      { "simple", Kind::complexity, "", 0.2 },
        { "facil", Kind::complexity, "", 0.2 },        { "basico", Kind::complexity, "", 0.2 },
        { "basica", Kind::complexity, "", 0.2 },
        { "complexo", Kind::complexity, "", 0.85 },    { "complexa", Kind::complexity, "", 0.85 },
        { "elaborado", Kind::complexity, "", 0.85 },   { "elaborada", Kind::complexity, "", 0.85 },
        { "rico", Kind::complexity, "", 0.85 },        { "rica", Kind::complexity, "", 0.85 },
        { "jazzy", Kind::complexity, "", 0.85 },       { "sofisticado", Kind::complexity, "", 0.85 },
        { "sofisticada", Kind::complexity, "", 0.85 },

        // Densidade
        { "poucas notas", Kind::density, "", 0.25 },   { "menos notas", Kind::density, "", 0.25 },
        { "muitas notas", Kind::density, "", 0.8 },    { "mais notas", Kind::density, "", 0.8 },
        { "minimalista", Kind::density, "", 0.25 },    { "espacado", Kind::density, "", 0.25 },
        { "espacada", Kind::density, "", 0.25 },       { "vazio", Kind::density, "", 0.25 },
        { "lento", Kind::density, "", 0.25 },          { "lenta", Kind::density, "", 0.25 },
        { "cheio", Kind::density, "", 0.8 },           { "cheia", Kind::density, "", 0.8 },
        { "rapido", Kind::density, "", 0.8 },          { "rapida", Kind::density, "", 0.8 },
        { "agitado", Kind::density, "", 0.8 },         { "agitada", Kind::density, "", 0.8 },
        { "frenetico", Kind::density, "", 0.8 },       { "frenetica", Kind::density, "", 0.8 },
    };

    /** A expressão mais longa da tabela que começa em `at`; `words` recebe quantas palavras ela tem. */
    const Phrase* matchPhrase (const std::vector<Token>& tokens, size_t at, size_t& words)
    {
        for (size_t n = (size_t) maxPhraseWords; n >= 1; --n)
        {
            if (at + n > tokens.size())
                continue;

            std::string key = tokens[at].lower;

            for (size_t k = 1; k < n; ++k)
                key += " " + tokens[at + k].lower;

            for (auto& p : phrases)
                if (key == p.text)
                {
                    words = n;
                    return &p;
                }
        }

        return nullptr;
    }

    //==========================================================================
    // Negação: depois de uma destas, a próxima expressão da tabela é tirada em vez de escolhida.
    // ("nada de" é tratado à parte, por serem duas palavras.)
    const char* const negationWords[] = { "sem", "nao", "nem", "tirar", "tira", "tire", "retirar", "retira", "retire",
                                          "remover", "remove", "remova" };

    // O que pode ficar entre a negação e o que ela nega: "não quero nenhuma bateria", "sem ser muito triste".
    const char* const negationFillers[] = { "quero", "queria", "precisa", "preciso", "usar", "use", "usa", "colocar", "coloque",
                                            "coloca", "por", "ponha", "botar", "bota", "ter", "tem", "ser", "seja", "e",
                                            "de", "da", "do", "a", "o", "as", "os", "um", "uma", "nenhum", "nenhuma",
                                            "muito", "muita", "tao" };

    // Palavras vazias: não dizem nada sobre a ideia, então não entram no "não entendi".
    // ("beat" e "instrumental" são a faixa inteira, não uma parte: ver docs/ideias.md.)
    const char* const stopWords[] = {
        "de", "do", "da", "dos", "das", "um", "uma", "uns", "umas", "com", "bem", "muito", "muita", "muitos", "muitas",
        "mais", "menos", "meio", "pouco", "pouca", "tao", "para", "pra", "pro", "por", "em", "no", "na", "nos", "nas",
        "ao", "os", "as", "ou", "que", "me", "eu", "se", "so", "ja", "ai", "la", "aqui", "esse", "essa", "este", "esta",
        "isso", "meu", "minha", "the", "an", "in", "of", "with", "and", "for",
        "quero", "queria", "preciso", "faz", "faca", "fazer", "cria", "crie", "criar", "gera", "gere", "gerar", "manda",
        "mande", "mandar", "bota", "coloca", "poe", "favor", "please", "make",
        "tipo", "estilo", "pegada", "vibe", "clima", "genero", "parte", "som", "musica", "faixa", "track", "ideia",
        "novo", "nova", "outro", "outra", "zero", "beat", "beats", "instrumental",
        "tom", "tonalidade", "key", "escala", "modo", "bpm", "andamento",
        "sem", "nao", "nem", "nada", "tirar", "tira", "tire", "retirar", "retira", "retire", "remover", "remove", "remova",
        "ser", "seja", "nenhum", "nenhuma", "como", "igual", "tambem", "mas", "sobre", "ate",
    };

    const std::pair<const char*, int> numberWords[] = {
        { "um", 1 }, { "uma", 1 }, { "dois", 2 }, { "duas", 2 }, { "tres", 3 }, { "quatro", 4 }, { "cinco", 5 },
        { "seis", 6 }, { "sete", 7 }, { "oito", 8 }, { "nove", 9 }, { "dez", 10 }, { "doze", 12 }, { "dezesseis", 16 },
    };

    /** "8" ou "oito". */
    bool numberValue (const std::string& w, int& n)
    {
        if (isNumber (w))
        {
            n = std::atoi (w.c_str());
            return true;
        }

        for (auto& [word, value] : numberWords)
            if (w == word)
            {
                n = value;
                return true;
            }

        return false;
    }

    /** "compasso(s)", "barra(s)", "bar(s)" — e "compaso(s)", que é erro comum. */
    bool isBarsWord (const std::string& w)
    {
        return w.rfind ("compasso", 0) == 0 || w.rfind ("compaso", 0) == 0 || w.rfind ("barra", 0) == 0
               || w == "bars" || w == "bar";
    }

    const char* scaleWord (const std::string& w)
    {
        if (w == "menor" || w == "minor" || w == "m")        return "minor";
        if (w == "maior" || w == "major")                     return "major";
        if (w == "frigio" || w == "frigia" || w == "phrygian") return "phrygian";
        if (w == "dorico" || w == "dorica" || w == "dorian")  return "dorian";
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

    /** "sustenido", "sharp", "#" solto = +1; "bemol", "flat" = -1; outra coisa = 0. */
    int accidentalWord (const std::string& w)
    {
        if (w == "sustenido" || w == "sharp" || w == "#")   return 1;
        if (w == "bemol" || w == "flat")                    return -1;
        return 0;
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

    /** "Am", "F#m", "Bb", "C#", "Ebmaj"... (letra maiúscula, ou minúscula com acidente ou "m").
        `accidentalFollows`: a próxima palavra é "sharp"/"bemol"..., o que também vale para minúscula ("f sharp"). */
    bool letterKey (const Token& t, bool accidentalFollows, int& pc, int& accidental, const char*& scale)
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
            if (t.lower == "em" || (accidental == 0 && scale == nullptr && ! accidentalFollows))
                return false;
        }

        return true;
    }

    void addUnique (std::vector<std::string>& list, const std::string& item)
    {
        if (std::find (list.begin(), list.end(), item) == list.end())
            list.push_back (item);
    }

    std::string joinList (const std::vector<std::string>& list)
    {
        std::string s;

        for (auto& item : list)
            s += (s.empty() ? "" : ", ") + item;

        return s;
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
            bool upper = false;
            const char m = latinLetter ((unsigned char) in[++i], upper);

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
    PromptSettings out;
    const auto tokens = tokenize (text);
    bool keyFound = false;
    bool negating = false;                 // uma negação está esperando o que negar
    size_t negatedEnd = (size_t) -1;       // onde acabou a última expressão negada ("sem X ou Y")

    auto lowerAt = [&] (size_t i) -> std::string { return i < tokens.size() ? tokens[i].lower : std::string(); };

    auto rawText = [&] (size_t from, size_t count)
    {
        std::string s;

        for (size_t k = from; k < from + count && k < tokens.size(); ++k)
            s += (s.empty() ? "" : " ") + tokens[k].raw;

        return s;
    };

    auto setBpm = [&] (int n, const std::string& raw)
    {
        if (n >= 40 && n <= 250)
            out.bpm = n;
        else
            addUnique (out.ignored, raw);
    };

    // Uma tonalidade lida a partir de `j`: "menor", "menor harmônica", "harmonic minor", "frígio"...
    auto readScale = [&] (size_t& j) -> const char*
    {
        const char* scale = scaleWord (lowerAt (j));

        if (scale == nullptr)
            return nullptr;

        ++j;
        const auto after = scaleWord (lowerAt (j));
        const bool minorHere = std::strcmp (scale, "minor") == 0;
        const bool minorAfter = after != nullptr && std::strcmp (after, "minor") == 0;

        if (minorHere && after != nullptr && (std::strcmp (after, "harmonicMinor") == 0 || std::strcmp (after, "pentatonicMinor") == 0))
        {
            scale = after;      // "menor harmônica", "menor pentatônica"
            ++j;
        }
        else if (! minorHere && minorAfter && lowerAt (j) != "m"
                 && (std::strcmp (scale, "harmonicMinor") == 0 || std::strcmp (scale, "pentatonicMinor") == 0))
        {
            ++j;                // "harmonic minor", "pentatônica menor"
        }

        return scale;
    };

    for (size_t i = 0; i < tokens.size();)
    {
        const auto& w = tokens[i].lower;
        const auto next = lowerAt (i + 1);
        const auto next2 = lowerAt (i + 2);
        const std::string prev = i > 0 ? tokens[i - 1].lower : std::string();
        const std::string prevPrev = i > 1 ? tokens[i - 2].lower : std::string();

        // 1. Negação: "sem X", "nada de X", "não quero X", "tirar X", "sem X nem Y", "sem X ou Y".
        if (inList (negationWords, w) || (w == "nada" && next == "de") || (w == "ou" && i == negatedEnd))
        {
            negating = true;
            i += (w == "nada") ? 2 : 1;
            continue;
        }

        if (negating && inList (negationFillers, w))
        {
            ++i;
            continue;
        }

        // 2. Expressões da tabela, a mais longa primeiro.
        size_t words = 0;

        if (auto* p = matchPhrase (tokens, i, words))
        {
            const auto raw = rawText (i, words);
            i += words;

            if (negating)
            {
                addUnique (out.negated, raw);
                negating = false;
                negatedEnd = i;
                continue;
            }

            // A primeira citada vale; outra diferente depois dela fica de fora (e é avisada).
            switch (p->kind)
            {
                case Kind::part:
                {
                    Part part;

                    if (parsePart (p->id, part))
                    {
                        if (! out.part)              out.part = part;
                        else if (*out.part != part)  addUnique (out.ignored, raw);
                    }

                    break;
                }

                case Kind::genre:
                    if (! out.genre)                 out.genre = p->id;
                    else if (*out.genre != p->id)    addUnique (out.ignored, raw);
                    break;

                case Kind::mood:
                {
                    Mood mood;

                    if (parseMood (p->id, mood))
                    {
                        if (! out.mood)              out.mood = mood;
                        else if (*out.mood != mood)  addUnique (out.ignored, raw);
                    }

                    break;
                }

                case Kind::complexity:  out.complexity = p->amount; break;
                case Kind::density:     out.density = p->amount;    break;
            }

            continue;
        }

        negating = false;

        // 3. Números: "4 compassos", "oito barras", "140bpm", "140 bpm", "140 de bpm", "bpm 140", "a 140".
        if (w.size() > 3 && w.substr (w.size() - 3) == "bpm" && isNumber (w.substr (0, w.size() - 3)))
        {
            setBpm (std::atoi (w.c_str()), tokens[i].raw);
            ++i;
            continue;
        }

        if (w == "bpm" && (isNumber (next) || ((next == "de" || next == "a") && isNumber (next2))))
        {
            const size_t at = isNumber (next) ? i + 1 : i + 2;
            setBpm (std::atoi (tokens[at].lower.c_str()), tokens[at].raw);
            i = at + 1;
            continue;
        }

        int number = 0;

        if (numberValue (w, number))
        {
            if (isBarsWord (next))
            {
                out.bars = std::clamp (number, 1, 16);
                i += 2;
                continue;
            }

            if (isNumber (w))
            {
                if (next == "bpm")
                {
                    setBpm (number, tokens[i].raw);
                    i += 2;
                }
                else if (next == "de" && next2 == "bpm")
                {
                    setBpm (number, tokens[i].raw);
                    i += 3;
                }
                else
                {
                    // "trap a 140": só na faixa em que um andamento é plausível.
                    if (prev == "a" && number >= 60 && number <= 250)
                        out.bpm = number;
                    else
                        addUnique (out.ignored, tokens[i].raw);

                    ++i;
                }

                continue;
            }

            // Número por extenso sem "compassos" depois: segue como palavra comum ("um trap").
        }

        // 4. Tonalidade: "fá menor", "em lá", "fa# menor", "si bemol maior", "tom de lá menor",
        //    "lá menor harmônica", "ré dórico", "Am", "C#m", "F sharp minor", "Bb major"...
        if (! keyFound)
        {
            int pc = 0, accidental = 0;
            size_t j = i + 1;

            const bool afterKeyWord = prev == "em" || prev == "in" || prev == "tom" || prev == "tonalidade" || prev == "key"
                                      || (prev == "de" && (prevPrev == "tom" || prevPrev == "tonalidade"))
                                      || (prev == "of" && prevPrev == "key");

            if (ptNote (w, pc, accidental))
            {
                if (accidental == 0)
                    if (const int a = accidentalWord (lowerAt (j)))
                    {
                        accidental = a;
                        ++j;
                    }

                const char* scale = readScale (j);

                if (scale != nullptr || accidental != 0 || afterKeyWord)
                {
                    out.root = pitchClass (pc + accidental);

                    if (scale != nullptr)
                        out.scale = scale;

                    keyFound = true;
                    i = j;
                    continue;
                }

                j = i + 1;
            }

            const char* letterScale = nullptr;

            if (letterKey (tokens[i], accidentalWord (next) != 0, pc, accidental, letterScale))
            {
                if (accidental == 0 && letterScale == nullptr)
                    if (const int a = accidentalWord (lowerAt (j)))
                    {
                        accidental = a;
                        ++j;
                    }

                // "Em" no começo da frase é a preposição ("Em 140 bpm, um trap"); só vira Mi menor
                // depois de uma palavra de tom ("tom Em", "trap em Em").
                const bool prepositionEm = tokens[i].lower == "em" && ! afterKeyWord;
                size_t afterScale = j;
                const char* nextScale = letterScale == nullptr ? readScale (afterScale) : nullptr;

                if (! prepositionEm && (letterScale != nullptr || accidental != 0 || nextScale != nullptr || afterKeyWord))
                {
                    out.root = pitchClass (pc + accidental);

                    if (letterScale != nullptr)
                        out.scale = letterScale;
                    else if (nextScale != nullptr)
                    {
                        out.scale = nextScale;
                        j = afterScale;
                    }

                    keyFound = true;
                    i = j;
                    continue;
                }
            }
        }

        // 5. Sobrou: palavra vazia some; o resto vai para o "não entendi".
        if (w.size() > 1 && ! inList (stopWords, w))
            addUnique (out.ignored, tokens[i].raw);

        ++i;
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

    if (! out.negated.empty())
        out.understood.push_back ("sem: " + joinList (out.negated));

    return out;
}

} // namespace whynot::ideas
