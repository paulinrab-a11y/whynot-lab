#pragma once

// O miolo da paleta de comandos (src/ui/CommandPalette.*), sem JUCE: dobrar o texto (sem acento, sem caixa),
// pontuar o que foi digitado contra o título e o id de cada entrada, e ordenar. Fica separado para ter teste
// próprio (tests/ui/test_command_search.cpp), compilado só com o `cl`.
//
// A busca é difusa: as letras digitadas têm de aparecer na ordem, não coladas ("exwav" acha "Exportar WAV").
// Começo de palavra e letras seguidas valem mais, então "expo" põe "Exportar WAV" antes de um título em que as
// mesmas letras só aparecem espalhadas. Várias palavras ("exportar mp3"): todas têm de casar, no título ou no id.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace whynot::cmdsearch
{

/** Uma linha da paleta: o id ("export.wav") e o título em português ("Exportar WAV"), os dois em UTF-8. */
struct Entry
{
    std::string id, title;
};

namespace detail
{
    constexpr int noMatch = -1000000;

    inline bool isAsciiUpper (char c) noexcept   { return c >= 'A' && c <= 'Z'; }
    inline bool isAsciiLower (char c) noexcept   { return c >= 'a' && c <= 'z'; }
    inline bool isAsciiDigit (char c) noexcept   { return c >= '0' && c <= '9'; }
    inline char asciiLower (char c) noexcept     { return isAsciiUpper (c) ? (char) (c - 'A' + 'a') : c; }
    inline char asciiUpper (char c) noexcept     { return isAsciiLower (c) ? (char) (c - 'a' + 'A') : c; }

    // letra, número ou um byte de um caractere fora do ASCII (que sobrou sem dobrar): faz parte de uma palavra
    inline bool isWordChar (char c) noexcept
    {
        return isAsciiUpper (c) || isAsciiLower (c) || isAsciiDigit (c) || (unsigned char) c >= 0x80;
    }

    /** Lê um caractere UTF-8 a partir de `i` e avança. Byte fora do padrão sai sozinho, como veio. */
    inline char32_t decode (std::string_view s, std::size_t& i) noexcept
    {
        const auto b0 = (unsigned char) s[i];
        int extra = 0;
        char32_t cp = b0;

        if (b0 >= 0xC2 && b0 <= 0xDF)        { extra = 1; cp = b0 & 0x1Fu; }
        else if (b0 >= 0xE0 && b0 <= 0xEF)   { extra = 2; cp = b0 & 0x0Fu; }
        else if (b0 >= 0xF0 && b0 <= 0xF4)   { extra = 3; cp = b0 & 0x07u; }

        if (i + (std::size_t) extra >= s.size() && extra > 0)
            extra = -1;   // cortado no meio

        for (int k = 1; k <= extra; ++k)
            if (((unsigned char) s[i + (std::size_t) k] & 0xC0u) != 0x80u)
                extra = -1;

        if (extra <= 0)
        {
            ++i;
            return b0 < 0x80u ? (char32_t) b0 : (char32_t) (0xDC00u + b0);   // 0xDC00 + byte = "byte solto" (volta igual no encode)
        }

        for (int k = 1; k <= extra; ++k)
            cp = (cp << 6) | ((unsigned char) s[i + (std::size_t) k] & 0x3Fu);

        i += (std::size_t) extra + 1;
        return cp;
    }

    inline void encode (char32_t cp, std::string& out)
    {
        if (cp >= 0xDC80u && cp <= 0xDCFFu)   out += (char) (cp - 0xDC00u);   // o byte solto do decode
        else if (cp < 0x80u)                  out += (char) cp;
        else if (cp < 0x800u)                 { out += (char) (0xC0u | (cp >> 6)); out += (char) (0x80u | (cp & 0x3Fu)); }
        else if (cp < 0x10000u)               { out += (char) (0xE0u | (cp >> 12)); out += (char) (0x80u | ((cp >> 6) & 0x3Fu)); out += (char) (0x80u | (cp & 0x3Fu)); }
        else                                  { out += (char) (0xF0u | (cp >> 18)); out += (char) (0x80u | ((cp >> 12) & 0x3Fu));
                                                out += (char) (0x80u | ((cp >> 6) & 0x3Fu)); out += (char) (0x80u | (cp & 0x3Fu)); }
    }

    /** A letra sem acento de U+00C0 a U+00FF (À a ÿ), em minúscula; nullptr = não é letra acentuada (× ÷ Þ þ). */
    inline const char* latin1Base (char32_t cp) noexcept
    {
        static constexpr const char* base[32] = { "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
                                                  "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", nullptr, "ss" };

        if (cp == 0xFFu)
            return "y";   // ÿ (na fileira de cima, a mesma casa é o ß)

        return base[(cp - 0xC0u) & 31u];
    }
}

/** O texto sem acento e, com `lower`, em minúsculas: "Padrão" e "PADRAO" viram "padrao". Só mexe no que o
    português usa (ASCII e o Latin-1, mais os acentos soltos U+0300 a U+036F); o resto passa como está. */
inline std::string fold (std::string_view text, bool lower = true)
{
    std::string out;
    out.reserve (text.size());

    for (std::size_t i = 0; i < text.size();)
    {
        const auto cp = detail::decode (text, i);

        if (cp < 0x80u)
        {
            out += lower ? detail::asciiLower ((char) cp) : (char) cp;
        }
        else if (cp >= 0x300u && cp <= 0x36Fu)
        {
            // acento solto (texto decomposto): some
        }
        else if (cp >= 0xC0u && cp <= 0xFFu && detail::latin1Base (cp) != nullptr)
        {
            const bool upper = ! lower && cp < 0xDFu;   // À a Þ são as maiúsculas; ß (0xDF) já é minúscula

            for (const char* p = detail::latin1Base (cp); *p != 0; ++p)
                out += upper ? detail::asciiUpper (*p) : *p;
        }
        else
        {
            detail::encode (cp, out);
        }
    }

    return out;
}

/** Um texto pronto para a busca: dobrado e com a marca de onde cada palavra começa (depois de espaço, ponto ou
    outro sinal, e na maiúscula no meio de um id: "togglePlay" tem duas palavras). */
struct Folded
{
    std::string text;
    std::vector<char> starts;   // 1 = começo de palavra
};

inline Folded prepare (std::string_view raw)
{
    const auto kept = fold (raw, false);
    Folded f;
    f.text.resize (kept.size());
    f.starts.assign (kept.size(), 0);

    for (std::size_t i = 0; i < kept.size(); ++i)
    {
        const char c = kept[i];
        const char before = i > 0 ? kept[i - 1] : ' ';
        f.starts[i] = (char) (detail::isWordChar (c)
                              && (! detail::isWordChar (before) || (detail::isAsciiLower (before) && detail::isAsciiUpper (c))));
        f.text[i] = detail::asciiLower (c);
    }

    return f;
}

/** Uma palavra da busca (já dobrada) contra um texto: detail::noMatch se as letras não aparecem na ordem; senão,
    quanto maior, melhor. Cada letra vale 16; no começo de uma palavra, mais 20; colada na anterior, mais 24 (um
    trecho inteiro ganha de letras soltas em começos de palavra); pular letras no meio custa 2 mais 1 por letra, e
    as do começo do texto custam até 8. */
inline int scoreToken (std::string_view token, const Folded& in)
{
    const auto n = token.size(), m = in.text.size();

    if (n == 0)
        return 0;

    if (n > m)
        return detail::noMatch;

    constexpr int none = detail::noMatch;
    std::vector<int> previous (m, none), current (m, none);

    for (std::size_t i = 0; i < n; ++i)
    {
        int gapped = none;   // o melhor da letra anterior com pelo menos uma letra pulada até aqui

        for (std::size_t j = 0; j < m; ++j)
        {
            int best = none;

            if (i == 0)
            {
                best = -(int) std::min<std::size_t> (j, 8);
            }
            else
            {
                if (j > 0 && previous[j - 1] > none)
                    best = previous[j - 1] + 24;

                best = std::max (best, gapped);
            }

            current[j] = token[i] == in.text[j] && best > none ? best + 16 + (in.starts[j] != 0 ? 20 : 0) : none;

            // para a casa seguinte: quem parou em j - 1 pula uma letra; quem já vinha pulando, mais uma
            if (gapped > none)
                gapped -= 1;

            if (i > 0 && j > 0 && previous[j - 1] > none)
                gapped = std::max (gapped, previous[j - 1] - 3);
        }

        previous.swap (current);
        std::fill (current.begin(), current.end(), none);
    }

    return *std::max_element (previous.begin(), previous.end());
}

/** As palavras da busca, dobradas: "  Exportar  MP3 " vira { "exportar", "mp3" }. */
inline std::vector<std::string> tokens (std::string_view query)
{
    const auto folded = fold (query);
    std::vector<std::string> list;
    std::string word;

    for (const char c : folded)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            if (! word.empty())
                list.push_back (std::move (word));

            word.clear();
        }
        else
        {
            word += c;
        }
    }

    if (! word.empty())
        list.push_back (std::move (word));

    return list;
}

/** Todas as palavras contra uma entrada: cada uma casa no título ou no id (no empate, vale o título). Entre duas
    entradas com as mesmas letras casadas, a de título mais curto fica na frente. */
inline int scoreEntry (const std::vector<std::string>& words, const Folded& title, const Folded& id)
{
    int total = 0;

    for (auto& word : words)
    {
        const int inTitle = scoreToken (word, title);
        const int inId = scoreToken (word, id);

        if (inTitle <= detail::noMatch && inId <= detail::noMatch)
            return detail::noMatch;

        total += std::max (inTitle > detail::noMatch ? inTitle + 4 : detail::noMatch, inId);
    }

    return total - (int) (title.text.size() / 8);
}

/** A ordem da lista, em índices de `entries`.
    - Busca vazia: primeiro os recentes (`recent` = ids, o mais novo na frente) e depois o resto, na ordem em que veio.
    - Com texto: só o que casa, do melhor para o pior; no empate, fica a ordem em que veio (ordenação estável). */
inline std::vector<int> rank (const std::vector<Entry>& entries, std::string_view query, const std::vector<std::string>& recent = {})
{
    std::vector<int> order;
    order.reserve (entries.size());
    const auto words = tokens (query);

    if (words.empty())
    {
        std::vector<char> used (entries.size(), 0);

        for (auto& id : recent)
            for (std::size_t i = 0; i < entries.size(); ++i)
                if (used[i] == 0 && entries[i].id == id)
                {
                    used[i] = 1;
                    order.push_back ((int) i);
                    break;
                }

        for (std::size_t i = 0; i < entries.size(); ++i)
            if (used[i] == 0)
                order.push_back ((int) i);

        return order;
    }

    std::vector<int> scores (entries.size(), detail::noMatch);

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        scores[i] = scoreEntry (words, prepare (entries[i].title), prepare (entries[i].id));

        if (scores[i] > detail::noMatch)
            order.push_back ((int) i);
    }

    std::stable_sort (order.begin(), order.end(), [&scores] (int a, int b) { return scores[(std::size_t) a] > scores[(std::size_t) b]; });
    return order;
}

/** Põe o id na frente dos recentes (sem repetir) e corta a lista em `limit`. */
inline void touchRecent (std::vector<std::string>& recent, const std::string& id, std::size_t limit = 8)
{
    recent.erase (std::remove (recent.begin(), recent.end(), id), recent.end());
    recent.insert (recent.begin(), id);

    if (recent.size() > limit)
        recent.resize (limit);
}

} // namespace whynot::cmdsearch
