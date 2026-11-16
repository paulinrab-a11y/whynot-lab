#pragma once

// Templates de projeto (frente templates, 16/11): a parte que não depende do JUCE. O nome que vira pasta, o que é
// recusado, a lista (os de fábrica primeiro, os do usuário em ordem), o padrão de "novo projeto" e a tabela dos
// templates de fábrica. Quem lê e grava disco é o Templates.cpp; o teste é tests/library/test_template_core.cpp.
//
// Os textos são UTF-8 (std::string): o Templates.cpp converte com juce::String::fromUTF8.

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace whynot::templates::core
{

/** A pasta dos templates apagados, dentro de Templates/ (o template.delete move para lá; nada some). */
inline constexpr std::string_view deletedFolderName = "apagados";

/** Quantas letras o nome de um template pode ter (a pasta e o .wnl levam o mesmo nome; o caminho do Windows é curto). */
inline constexpr std::size_t maxNameLength = 64;

//==============================================================================
// Templates de fábrica: montados por código a partir do que o app já tem (o kit sintetizado, os gêneros do
// WHYNOT Ideias para o BPM e o modo, e os estilos de mixagem). Nenhum arquivo novo, nenhum nome de produto.

struct FactoryTemplate
{
    const char* name;          // o nome na lista e no template.new
    const char* description;   // uma linha, para o menu e para a IA
    bool kit;                  // true = começa do projeto novo de fábrica (Kick, Clap, Hat, Open hat, 808 e Bell)
    const char* genre;         // id de ideas::allGenres(): o BPM padrão e o modo (menor ou maior); "" = não mexe
    const char* style;         // id de dsp::allStyles(): o estilo de mixagem aplicado em tudo; "" = sem efeitos
    bool chokeHats;            // o hat fechado corta o aberto (os dois no grupo de corte 1)
    float glideMs;             // o glide do 808 (mono, com slide); 0 = como o kit traz
    std::array<const char*, 4> lanes;   // os nomes das quatro primeiras raias da playlist; "" = fica "Raia N"
};

inline std::span<const FactoryTemplate> factoryTemplates() noexcept
{
    static constexpr FactoryTemplate all[] = {
        { "Kit WHYNOT", "O projeto novo de sempre: o kit da WHYNOT em 140 BPM, sem efeitos.",
          true, "", "", false, 0.0f, { "", "", "", "" } },
        { "Vazio", "Sem canais: só o master, um padrão vazio e as raias da playlist.",
          false, "", "", false, 0.0f, { "", "", "", "" } },
        { "Trap", "O kit, o 808 mono com glide, o hat fechado cortando o aberto, a mixagem de trap e 140 BPM.",
          true, "trap", "trap", true, 60.0f, { "Bateria", "808", "Melodia", "Voz" } },
        { "Funk", "O kit, a mixagem de funk e 130 BPM.",
          true, "funk", "funk", false, 0.0f, { "Batida", "Grave", "Melodia", "Voz" } },
        { "Boom bap", "O kit, a mixagem de rap e o andamento do lo-fi / boom bap (82 BPM).",
          true, "lofi", "rap", false, 0.0f, { "Bateria", "Baixo", "Sample", "Voz" } },
    };

    return all;
}

//==============================================================================
// Nomes

inline char lowerAscii (char c) noexcept   { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; }

inline std::string lowerAscii (std::string_view s)
{
    std::string out (s);

    for (auto& c : out)
        c = lowerAscii (c);

    return out;
}

/** O mesmo nome para o disco: o NTFS não distingue maiúsculas. Só o ASCII é comparado sem caixa ("Ação" e "ação"
    contam como dois nomes aqui; o disco recusa o segundo ao criar a pasta, e o save devolve o erro). */
inline bool sameName (std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::equal (a.begin(), a.end(), b.begin(), [] (char x, char y) { return lowerAscii (x) == lowerAscii (y); });
}

inline const FactoryTemplate* findFactory (std::string_view name) noexcept
{
    for (auto& f : factoryTemplates())
        if (sameName (f.name, name))
            return &f;

    return nullptr;
}

/** O nome que o usuário digitou → o nome da pasta (e do .wnl dentro dela). Tira o que o Windows não aceita num nome
    de arquivo (os mesmos caracteres do juce::File::createLegalFileName, mais os de controle), os espaços das pontas e
    os pontos e espaços do fim (o Windows some com eles), e corta em maxNameLength letras sem partir um caractere
    UTF-8. Pode devolver vazio: o checkName recusa. */
inline std::string folderNameFor (std::string_view name)
{
    static constexpr std::string_view illegal = "\"#@,;:<>*^|?\\/";
    std::string out;
    std::size_t letters = 0;

    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const auto c = (unsigned char) name[i];
        const bool continuation = (c & 0xC0) == 0x80;

        if (c < 0x20 || c == 0x7F || illegal.find ((char) c) != std::string_view::npos)
            continue;

        if (out.empty() && (c == ' ' || continuation))
            continue;   // espaço no começo, ou o resto de um caractere cortado

        if (! continuation && ++letters > maxNameLength)
            break;

        out += (char) c;
    }

    while (! out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();

    return out;
}

/** Nomes que o Windows reserva (CON, NUL, COM1...), com ou sem extensão, e a pasta dos apagados. */
inline bool isReservedName (std::string_view name)
{
    static constexpr std::string_view devices[] = { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                                                    "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
    const auto stem = lowerAscii (name.substr (0, name.find ('.')));

    for (auto d : devices)
        if (stem == d)
            return true;

    return sameName (name, deletedFolderName);
}

enum class NameProblem { none, empty, reserved, factory, duplicate };

/** Dá para salvar um template com este nome de pasta (já passado pelo folderNameFor)? `replace` aceita o nome de um
    template do usuário que já existe (o antigo vai para os apagados); o de um template de fábrica nunca. */
inline NameProblem checkName (std::string_view folderName, const std::vector<std::string>& userNames, bool replace)
{
    if (folderName.empty())
        return NameProblem::empty;

    if (isReservedName (folderName))
        return NameProblem::reserved;

    if (findFactory (folderName) != nullptr)
        return NameProblem::factory;

    if (! replace)
        for (auto& existing : userNames)
            if (sameName (existing, folderName))
                return NameProblem::duplicate;

    return NameProblem::none;
}

inline const char* problemText (NameProblem p) noexcept
{
    switch (p)
    {
        case NameProblem::empty:      return "Dê um nome ao template (letras ou números).";
        case NameProblem::reserved:   return "Esse nome é reservado pelo Windows ou pelo app. Escolha outro.";
        case NameProblem::factory:    return "Esse é o nome de um template de fábrica. Escolha outro.";
        case NameProblem::duplicate:  return "Já existe um template seu com esse nome. Escolha outro, ou salve com \"replace\": true para trocar (o antigo vai para a pasta apagados).";
        case NameProblem::none:       break;
    }

    return "";
}

//==============================================================================
// A lista

/** Ordem natural, sem caixa no ASCII: "Beat 2" antes de "Beat 10", "trap" junto de "Trap". */
inline int compareNatural (std::string_view a, std::string_view b) noexcept
{
    auto isDigit = [] (char c) { return c >= '0' && c <= '9'; };
    std::size_t i = 0, j = 0;

    while (i < a.size() && j < b.size())
    {
        if (isDigit (a[i]) && isDigit (b[j]))
        {
            while (i < a.size() && a[i] == '0') ++i;
            while (j < b.size() && b[j] == '0') ++j;

            const auto startA = i, startB = j;

            while (i < a.size() && isDigit (a[i])) ++i;
            while (j < b.size() && isDigit (b[j])) ++j;

            const auto lenA = i - startA, lenB = j - startB;

            if (lenA != lenB)
                return lenA < lenB ? -1 : 1;

            if (const int c = a.compare (startA, lenA, b, startB, lenB); c != 0)
                return c < 0 ? -1 : 1;

            continue;
        }

        const auto x = (unsigned char) lowerAscii (a[i]), y = (unsigned char) lowerAscii (b[j]);

        if (x != y)
            return x < y ? -1 : 1;

        ++i;
        ++j;
    }

    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return a < b ? -1 : (b < a ? 1 : 0);   // desempate estável: "trap" e "Trap" não trocam de lugar a cada chamada
}

inline void sortNames (std::vector<std::string>& names)
{
    std::sort (names.begin(), names.end(), [] (const std::string& a, const std::string& b) { return compareNatural (a, b) < 0; });
}

/** O template que o "novo projeto" usa: o guardado, se ele ainda existe (de fábrica ou do usuário), com o nome
    como está na lista; senão "" (o de fábrica de sempre). "Kit WHYNOT" também é "": é o mesmo projeto. */
inline std::string resolveDefault (std::string_view stored, const std::vector<std::string>& userNames)
{
    if (stored.empty())
        return {};

    if (const auto* f = findFactory (stored))
        return f == &factoryTemplates().front() ? std::string() : std::string (f->name);

    for (auto& name : userNames)
        if (sameName (name, stored))
            return name;

    return {};
}

struct Entry
{
    std::string name, description;
    bool factory = false;
    bool isDefault = false;   // o "novo projeto" começa deste
};

/** A lista do menu e do template.list: os de fábrica na ordem da tabela, depois os do usuário em ordem natural.
    Um do usuário com nome de fábrica (pasta criada à mão) fica de fora: o nome abre o de fábrica. Sem padrão
    guardado (ou com um que sumiu), o padrão é o primeiro de fábrica. */
inline std::vector<Entry> listAll (std::vector<std::string> userNames, std::string_view storedDefault)
{
    std::erase_if (userNames, [] (const std::string& n) { return n.empty() || findFactory (n) != nullptr || isReservedName (n); });
    sortNames (userNames);

    const auto chosen = resolveDefault (storedDefault, userNames);
    std::vector<Entry> out;

    for (auto& f : factoryTemplates())
        out.push_back ({ f.name, f.description, true, chosen.empty() ? &f == &factoryTemplates().front() : sameName (f.name, chosen) });

    for (auto& name : userNames)
        out.push_back ({ name, {}, false, ! chosen.empty() && sameName (name, chosen) });

    return out;
}

/** O nome da pasta de um template apagado: o próprio nome, ou "Nome (2)", "Nome (3)"... se já há um lá. */
inline std::string deletedNameFor (std::string_view name, const std::vector<std::string>& alreadyDeleted)
{
    auto taken = [&alreadyDeleted] (std::string_view candidate)
    {
        return std::any_of (alreadyDeleted.begin(), alreadyDeleted.end(), [candidate] (const std::string& n) { return sameName (n, candidate); });
    };

    if (! taken (name))
        return std::string (name);

    for (int n = 2;; ++n)
        if (const auto candidate = std::string (name) + " (" + std::to_string (n) + ")"; ! taken (candidate))
            return candidate;
}

} // namespace whynot::templates::core
