// Templates de projeto (frente templates, 16/11): a lógica sem JUCE de src/library/TemplateCore.h. O nome que vira
// pasta, as recusas, a lista (fábrica e do usuário), o padrão de "novo projeto", a pasta dos apagados, e a tabela dos
// templates de fábrica conferida contra os gêneros do WHYNOT Ideias e os estilos de mixagem que o app tem.

#include "../../src/library/TemplateCore.h"
#include "../../src/dsp/FxCatalog.h"
#include "../../src/ideas/Genres.h"

#include <cstdio>
#include <random>
#include <set>

using namespace whynot::templates::core;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static bool validUtf8 (std::string_view s)
{
    for (std::size_t i = 0; i < s.size();)
    {
        const auto c = (unsigned char) s[i];
        const std::size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;

        if (n == 0 || i + n > s.size())
            return false;

        for (std::size_t k = 1; k < n; ++k)
            if (((unsigned char) s[i + k] & 0xC0) != 0x80)
                return false;

        i += n;
    }

    return true;
}

static std::size_t letterCount (std::string_view s)
{
    std::size_t n = 0;

    for (auto c : s)
        n += ((unsigned char) c & 0xC0) != 0x80 ? 1u : 0u;

    return n;
}

static void testFolderName()
{
    std::printf ("O nome que vira pasta\n");

    CHECK (folderNameFor ("Meu trap") == "Meu trap", "nome simples fica igual");
    CHECK (folderNameFor ("  Meu trap  ") == "Meu trap", "os espaços das pontas saem");
    CHECK (folderNameFor ("Voz: trap/funk?") == "Voz trapfunk", "o que o Windows não aceita num nome de arquivo sai (: / ?)");
    CHECK (folderNameFor ("a\\b|c<d>e*f\"g") == "abcdefg", "barra invertida, barra reta, sinais de maior e menor, asterisco e aspas saem");
    CHECK (folderNameFor ("Beat.") == "Beat" && folderNameFor ("Beat. . ") == "Beat", "ponto e espaço no fim saem (o Windows some com eles)");
    CHECK (folderNameFor ("Beat v1.2") == "Beat v1.2", "ponto no meio fica");
    CHECK (folderNameFor ("Ação ção") == "Ação ção", "acento fica");
    CHECK (folderNameFor ("").empty() && folderNameFor ("   ").empty() && folderNameFor ("???").empty() && folderNameFor ("..").empty(),
           "vazio, só espaço, só caractere proibido e só ponto dão nome vazio");
    CHECK (folderNameFor (std::string ("a\tb\nc\x01") + "d") == "abcd", "caracteres de controle saem");
    CHECK (folderNameFor ("..\\..\\fora") == "....fora" && folderNameFor ("C:\\Windows") == "CWindows", "nenhum nome sai da pasta dos templates (sem barra nem dois-pontos)");

    const std::string longAscii (200, 'x');
    CHECK (folderNameFor (longAscii).size() == maxNameLength, "nome longo é cortado em %zu letras", maxNameLength);

    std::string longAccent;

    for (int i = 0; i < 100; ++i)
        longAccent += "ção";

    const auto cut = folderNameFor (longAccent);
    CHECK (letterCount (cut) == maxNameLength && validUtf8 (cut), "o corte conta letras, não bytes, e não parte um caractere UTF-8 (%zu bytes)", cut.size());

    // fuzz: qualquer entrada dá um nome que serve de pasta, e passar de novo não muda
    std::mt19937 rng (20261116);
    static const char pool[] = "abcXYZ 019._-()\\/:*?\"<>|#@,;^\t\n\xC3\xA7\xC3\xA3\xE2\x82\xAC";
    bool legal = true, stable = true, utf8 = true;

    for (int round = 0; round < 20000; ++round)
    {
        std::string in;
        const int n = (int) (rng() % 90);

        for (int i = 0; i < n; ++i)
            in += pool[rng() % (sizeof (pool) - 1)];

        const auto out = folderNameFor (in);
        legal = legal && out.find_first_of ("\"#@,;:<>*^|?\\/\t\n") == std::string::npos
                      && (out.empty() || (out.front() != ' ' && out.back() != ' ' && out.back() != '.')) && letterCount (out) <= maxNameLength;
        stable = stable && folderNameFor (out) == out;
        utf8 = utf8 && (! validUtf8 (in) || validUtf8 (out));
    }

    CHECK (legal, "20 mil nomes sorteados: sem caractere proibido, sem espaço nas pontas, sem ponto no fim, até %zu letras", maxNameLength);
    CHECK (stable, "passar o nome de novo pelo folderNameFor não muda nada");
    CHECK (utf8, "entrada em UTF-8 válido dá saída em UTF-8 válido");
}

static void testCheckName()
{
    std::printf ("Recusas do nome\n");

    const std::vector<std::string> mine { "Meu trap", "Voz 2" };

    CHECK (checkName ("Novo", mine, false) == NameProblem::none, "nome novo passa");
    CHECK (checkName ("", mine, false) == NameProblem::empty && checkName ("", mine, true) == NameProblem::empty, "vazio é recusado, com ou sem replace");
    CHECK (checkName ("Meu trap", mine, false) == NameProblem::duplicate && checkName ("MEU TRAP", mine, false) == NameProblem::duplicate,
           "duplicado é recusado, sem distinguir maiúsculas (como o disco)");
    CHECK (checkName ("Meu trap", mine, true) == NameProblem::none, "com replace, o nome de um template do usuário passa");
    CHECK (checkName ("Trap", mine, false) == NameProblem::factory && checkName ("trap", mine, true) == NameProblem::factory
           && checkName ("Vazio", mine, true) == NameProblem::factory && checkName ("kit whynot", mine, true) == NameProblem::factory,
           "nome de template de fábrica é recusado sempre, mesmo com replace");
    CHECK (checkName ("apagados", mine, false) == NameProblem::reserved && checkName ("Apagados", mine, true) == NameProblem::reserved,
           "\"apagados\" é a pasta dos templates apagados: recusado");
    CHECK (checkName ("CON", mine, false) == NameProblem::reserved && checkName ("nul", mine, false) == NameProblem::reserved
           && checkName ("com1.beat", mine, false) == NameProblem::reserved && checkName ("LPT9", mine, false) == NameProblem::reserved,
           "nomes de dispositivo do Windows (CON, NUL, COM1.x, LPT9) são recusados");
    CHECK (checkName ("Console", mine, false) == NameProblem::none && checkName ("com10", mine, false) == NameProblem::none,
           "\"Console\" e \"com10\" não são nomes de dispositivo");

    bool texts = true;

    for (auto p : { NameProblem::empty, NameProblem::reserved, NameProblem::factory, NameProblem::duplicate })
        texts = texts && std::string_view (problemText (p)).size() > 10;

    CHECK (texts && std::string_view (problemText (NameProblem::none)).empty(), "cada recusa tem a mensagem dela; sem problema, sem texto");
}

static void testList()
{
    std::printf ("A lista e o padrão\n");

    std::vector<std::string> names { "beat 10", "Beat 2", "zeta", "Alfa", "beat 1" };
    sortNames (names);
    CHECK ((names == std::vector<std::string> { "Alfa", "beat 1", "Beat 2", "beat 10", "zeta" }), "ordem natural e sem caixa: Alfa, beat 1, Beat 2, beat 10, zeta");
    CHECK (compareNatural ("a", "a") == 0 && compareNatural ("a2", "a10") < 0 && compareNatural ("a10", "a2") > 0 && compareNatural ("a02", "a2") != 0
           && compareNatural ("a", "ab") < 0 && compareNatural ("Trap", "trap") == -compareNatural ("trap", "Trap"),
           "compareNatural: números pelo valor, prefixo antes, e uma ordem só para nomes que diferem na caixa");

    const auto factoryCount = factoryTemplates().size();
    const auto list = listAll ({ "zeta", "Alfa", "Trap", "", "apagados" }, "");
    bool factoryFirst = list.size() == factoryCount + 2;

    for (std::size_t i = 0; factoryFirst && i < list.size(); ++i)
        factoryFirst = list[i].factory == (i < factoryCount) && (i >= factoryCount || list[i].name == factoryTemplates()[i].name);

    CHECK (factoryFirst && list[factoryCount].name == "Alfa" && list[factoryCount + 1].name == "zeta",
           "os de fábrica na ordem da tabela, depois os do usuário em ordem; pasta com nome de fábrica, vazia ou \"apagados\" fica de fora");

    auto defaults = [] (const std::vector<Entry>& l)
    {
        std::vector<std::string> out;

        for (auto& e : l)
            if (e.isDefault)
                out.push_back (e.name);

        return out;
    };

    CHECK ((defaults (list) == std::vector<std::string> { "Kit WHYNOT" }), "sem padrão guardado, o padrão é o Kit WHYNOT (o projeto novo de fábrica)");
    CHECK ((defaults (listAll ({ "Alfa", "zeta" }, "zeta")) == std::vector<std::string> { "zeta" }), "padrão do usuário marcado, e só ele");
    CHECK ((defaults (listAll ({ "Alfa" }, "TRAP")) == std::vector<std::string> { "Trap" }), "padrão de fábrica marcado (sem distinguir maiúsculas)");
    CHECK ((defaults (listAll ({ "Alfa" }, "sumiu")) == std::vector<std::string> { "Kit WHYNOT" }), "padrão que foi apagado: volta ao de fábrica");

    CHECK (resolveDefault ("", { "Alfa" }).empty(), "resolveDefault: vazio = o de fábrica");
    CHECK (resolveDefault ("alfa", { "Alfa" }) == "Alfa", "resolveDefault: devolve o nome como está na lista");
    CHECK (resolveDefault ("funk", {}) == "Funk" && resolveDefault ("Boom bap", {}) == "Boom bap", "resolveDefault: template de fábrica vale");
    CHECK (resolveDefault ("Kit WHYNOT", {}).empty(), "resolveDefault: \"Kit WHYNOT\" é o próprio projeto de fábrica (vazio)");
    CHECK (resolveDefault ("Alfa", {}).empty() && resolveDefault ("apagados", {}).empty(), "resolveDefault: o que não existe mais vira o de fábrica");

    CHECK (deletedNameFor ("Alfa", {}) == "Alfa" && deletedNameFor ("Alfa", { "Beta" }) == "Alfa", "apagados: o nome livre fica igual");
    CHECK (deletedNameFor ("Alfa", { "alfa" }) == "Alfa (2)" && deletedNameFor ("Alfa", { "Alfa", "Alfa (2)", "Alfa (3)" }) == "Alfa (4)",
           "apagados: nome que já está lá ganha (2), (3)... e nada é sobrescrito");
}

static void testFactory()
{
    std::printf ("Templates de fábrica\n");

    const auto all = factoryTemplates();
    std::set<std::string> names;
    bool unique = true, legal = true, described = true, known = true, sane = true, told = true;
    std::string report;

    for (auto& f : all)
    {
        unique = unique && names.insert (lowerAscii (f.name)).second;
        legal = legal && folderNameFor (f.name) == f.name && ! isReservedName (f.name);
        described = described && std::string_view (f.description).size() > 10;

        const std::string_view genre (f.genre), style (f.style);
        const auto* g = genre.empty() ? nullptr : whynot::ideas::findGenre (genre);
        const auto* s = style.empty() ? nullptr : whynot::dsp::findStyle (style);
        known = known && (genre.empty() || g != nullptr) && (style.empty() || s != nullptr);

        if (g != nullptr)
        {
            sane = sane && g->bpmDefault >= 20 && g->bpmDefault <= 999 && g->bpmDefault >= g->bpmLow && g->bpmDefault <= g->bpmHigh;
            told = told && std::string_view (f.description).find (std::to_string (g->bpmDefault) + " BPM") != std::string_view::npos;
        }

        // sem kit não há o que mixar, nem 808, nem hats
        sane = sane && (f.kit || (genre.empty() && style.empty() && ! f.chokeHats && f.glideMs == 0.0f));
        sane = sane && f.glideMs >= 0.0f && f.glideMs <= 2000.0f;

        report += std::string (report.empty() ? "" : ", ") + f.name + (g != nullptr ? " " + std::to_string (g->bpmDefault) + " BPM" : "");
    }

    CHECK (all.size() >= 4 && all.size() <= 6, "%zu templates de fábrica: %s", all.size(), report.c_str());
    CHECK (unique, "nomes sem repetir");
    CHECK (legal, "todo nome de fábrica serve de nome de pasta como está");
    CHECK (described, "todo template tem a linha de descrição");
    CHECK (known, "todo gênero existe em ideas::allGenres() e todo estilo em dsp::allStyles()");
    CHECK (told, "a descrição diz o BPM que o gênero dá (se o gênero mudar, o teste acusa a descrição velha)");
    CHECK (sane, "BPM do gênero dentro da faixa dele, glide de 0 a 2000 ms, e o template sem kit não pede mixagem nem 808");

    const auto* kit = findFactory ("Kit WHYNOT");
    const auto* empty = findFactory ("vazio");
    const auto* trap = findFactory ("TRAP");
    const auto* funk = findFactory ("Funk");
    const auto* boom = findFactory ("Boom bap");
    CHECK (kit == &all.front() && kit->kit && std::string_view (kit->genre).empty() && std::string_view (kit->style).empty(),
           "o primeiro é o Kit WHYNOT: o projeto novo de fábrica, sem mexer em nada");
    CHECK (empty != nullptr && ! empty->kit, "Vazio: sem o kit");
    CHECK (trap != nullptr && trap->kit && std::string_view (trap->style) == "trap" && trap->glideMs > 0.0f && trap->chokeHats
           && whynot::ideas::findGenre (trap->genre)->bpmDefault == 140,
           "Trap: kit, estilo trap, 808 com glide, hats no mesmo grupo de corte e 140 BPM");
    CHECK (funk != nullptr && funk->kit && std::string_view (funk->style) == "funk" && whynot::ideas::findGenre (funk->genre)->bpmDefault == 130,
           "Funk: kit, estilo funk e 130 BPM");
    CHECK (boom != nullptr && boom->kit && std::string_view (boom->style) == "rap" && std::string_view (boom->genre) == "lofi",
           "Boom bap: kit, estilo rap e o andamento do gênero lo-fi / boom bap (%d BPM)", whynot::ideas::findGenre ("lofi")->bpmDefault);
    CHECK (findFactory ("Trap ") == nullptr && findFactory ("") == nullptr && findFactory ("Meu trap") == nullptr, "findFactory só acha o nome inteiro");
}

int main()
{
    testFolderName();
    testCheckName();
    testList();
    testFactory();

    std::printf (failures == 0 ? "\nTUDO OK: 0 falha(s)\n" : "\n%d FALHA(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
