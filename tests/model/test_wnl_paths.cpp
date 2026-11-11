// Regras dos caminhos que um .wnl guarda (src/model/WnlPaths.h), sem JUCE: o que o app aceita ao abrir um
// projeto de terceiros e o que ele copia para samples/ ao salvar. Mais um fuzz: nenhum texto derruba, e o que
// sai do sanitised é sempre um caminho que o app pode usar.

#include "../../src/model/WnlPaths.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace whynot::wnl::paths;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static const char* name (Kind k)
{
    switch (k)
    {
        case Kind::empty:     return "empty";
        case Kind::relative:  return "relative";
        case Kind::absolute:  return "absolute";
        case Kind::network:   return "network";
        case Kind::escapes:   return "escapes";
        case Kind::invalid:   return "invalid";
    }

    return "?";
}

static void expect (const char* path, Kind wanted)
{
    const auto got = classify (path);
    CHECK (got == wanted, "\"%s\" devia ser %s e saiu %s", path, name (wanted), name (got));
}

static void testClassify()
{
    std::printf ("Tipos de caminho\n");

    expect ("", Kind::empty);

    // o que o app grava: relativo à pasta do projeto, com / ou \, acento e espaço no meio
    expect ("samples/808_long.wav", Kind::relative);
    expect ("samples\\Kick duro.wav", Kind::relative);
    expect ("kick.wav", Kind::relative);
    expect ("samples/voz/../Refrão final.flac", Kind::relative);
    expect ("./samples/./kick.wav", Kind::relative);
    expect ("gravações/take 1.wav", Kind::relative);

    // o originalPath e o sample que ainda não foi coletado: absoluto com a letra da unidade
    expect ("C:/Users/Fulano/Música/Kick.wav", Kind::absolute);
    expect ("d:\\Samples\\808.wav", Kind::absolute);
    expect ("C:\\a\\..\\b\\x.wav", Kind::absolute);
    expect ("Z:/kit/clap.aiff", Kind::absolute);

    // rede e dispositivo: nunca, em nenhuma das grafias
    expect ("\\\\host\\share\\kick.wav", Kind::network);
    expect ("//host/share/kick.wav", Kind::network);
    expect ("\\/host/share/kick.wav", Kind::network);
    expect ("/\\host\\share\\kick.wav", Kind::network);
    expect ("\\\\?\\UNC\\host\\share\\kick.wav", Kind::network);
    expect ("\\\\?\\C:\\Users\\x\\kick.wav", Kind::network);
    expect ("\\\\.\\pipe\\coisa", Kind::network);
    expect ("//./PhysicalDrive0", Kind::network);
    expect ("\\\\192.168.0.10\\c$\\Users\\x\\Documents\\senha.txt", Kind::network);
    expect ("\\\\", Kind::network);

    // relativo que sai da pasta do projeto
    expect ("../segredo.wav", Kind::escapes);
    expect ("..\\..\\Documents\\contrato.docx", Kind::escapes);
    expect ("samples/../../fora.wav", Kind::escapes);
    expect ("a/../../b.wav", Kind::escapes);
    expect ("..", Kind::escapes);
    expect ("./../x.wav", Kind::escapes);
    expect ("../samples/kick.wav", Kind::escapes);   // sai e "volta": o meio do caminho já saiu

    // o resto que o Windows interpretaria de um jeito que ninguém pediu
    expect ("C:kick.wav", Kind::invalid);            // relativo à pasta atual da unidade C
    expect ("C:", Kind::invalid);
    expect ("\\Windows\\win.ini", Kind::invalid);    // raiz da unidade atual
    expect ("/etc/passwd", Kind::invalid);
    expect ("samples/kick.wav:oculto", Kind::invalid);
    expect ("C:/x/arquivo.txt:fluxo.wav", Kind::invalid);
    expect ("samples/CON", Kind::invalid);
    expect ("C:/x/nul.wav", Kind::invalid);
    expect ("samples/kick?.wav", Kind::invalid);
    expect ("samples/kick.wav ", Kind::invalid);
    expect ("samples/kick.wav.", Kind::invalid);
    expect ("C:/../x.wav", Kind::invalid);           // sobe acima da raiz da unidade
    expect ("http://host/kick.wav", Kind::invalid);
    expect ("file:///C:/x.wav", Kind::invalid);
    expect (".", Kind::invalid);
    expect ("samples/..", Kind::invalid);
    expect ("samples/\x01kick.wav", Kind::invalid);
    expect ("samples/\xC3\x28.wav", Kind::invalid);  // UTF-8 inválido
    CHECK (classify (std::string (2000, 'a') + ".wav") == Kind::invalid, "mais de 1024 caracteres é inválido");
    CHECK (classify (std::string_view ("a\0b.wav", 7)) == Kind::invalid, "NUL no meio do texto é inválido");

    CHECK (isUsable (Kind::empty) && isUsable (Kind::relative) && isUsable (Kind::absolute)
               && ! isUsable (Kind::network) && ! isUsable (Kind::escapes) && ! isUsable (Kind::invalid),
           "só o vazio, o relativo e o absoluto são usáveis");
}

static void testSanitised()
{
    std::printf ("O que sobra de um caminho recusado\n");

    CHECK (sanitised ("samples/kick.wav") == "samples/kick.wav", "o relativo passa como veio");
    CHECK (sanitised ("C:/Users/Fulano/Música/Kick.wav") == "C:/Users/Fulano/Música/Kick.wav", "o absoluto passa como veio");
    CHECK (sanitised ("").empty(), "o vazio continua vazio");

    CHECK (sanitised ("\\\\host\\share\\kick.wav") == "kick.wav", "do caminho de rede fica o nome do arquivo");
    CHECK (sanitised ("//host/share/Meu Hat.flac") == "Meu Hat.flac", "do // também");
    CHECK (sanitised ("\\\\?\\UNC\\host\\share\\clap.wav") == "clap.wav", "do \\\\?\\UNC\\ também");
    CHECK (sanitised ("../../Documents/contrato.docx") == "contrato.docx", "do que sai da pasta fica o nome (sem a pasta de fora)");
    CHECK (sanitised ("\\\\host\\share\\").empty(), "sem nome de arquivo, fica vazio");
    CHECK (sanitised ("\\\\host\\share\\CON").empty(), "nome de dispositivo não fica");
    CHECK (sanitised ("\\\\host\\share\\x.wav:fluxo").empty(), "fluxo alternativo não fica");
    CHECK (sanitised ("C:kick.wav").empty(), "\"C:kick.wav\" não vira nada");
    CHECK (sanitised ("..").empty(), "\"..\" não vira nada");
    CHECK (safeFileName ("a/b\\c.wav") == "c.wav", "o nome é o que vem depois da última barra, / ou \\");
}

static void testAudioExtension()
{
    std::printf ("Extensão de áudio\n");

    for (const char* ok : { "kick.wav", "C:/x/Kick.WAV", "a.aif", "a.AIFF", "voz final.flac", "loop.ogg", "tag.Mp3",
                            "samples/meu.kit/hat.wav", "\\\\host\\share\\kick.wav" })
        CHECK (hasAudioExtension (ok), "\"%s\" é áudio", ok);

    for (const char* no : { "", "contrato.docx", "senha.txt", "kick", "kick.", ".wav", "kick.wav.exe", "kick.wave", "pasta.wav/arquivo",
                            "pasta.wav\\arquivo.txt", "id_rsa", "C:/Users/x/.ssh/id_rsa", "foto.png", "projeto.wnl", "kick.wav ",
                            "plugin.vst3", "kick.w" })
        CHECK (! hasAudioExtension (no), "\"%s\" não é áudio", no);
}

// Qualquer texto: não derruba, e o sanitised só devolve o que o app pode usar (e nunca um caminho de rede).
static void testFuzz()
{
    std::printf ("Fuzz\n");
    const auto started = std::chrono::steady_clock::now();
    std::mt19937 rng (20261110u);
    auto pick = [&rng] (std::size_t n) { return (std::size_t) (rng() % (std::uint32_t) n); };

    const std::vector<std::string> seeds {
        "samples/808_long.wav", "C:/Users/Fulano/Música/Kick.wav", "\\\\host\\share\\kick.wav", "//host/share/kick.wav",
        "\\\\?\\UNC\\host\\share\\kick.wav", "../../Documents/contrato.docx", "samples/../../fora.wav", "C:kick.wav",
        "samples/kick.wav:oculto", "D:\\Beats\\kit antigo\\WHYNOT Clap.wav", "a/b/c/d/e/f.flac", "\\Windows\\win.ini"
    };
    const std::string alphabet ("\\/.:?*<>|\" \t\n\rCcNnUuLl0123$%abcdefé~-_");

    bool ok = true;
    int refused = 0;
    std::string worst;

    for (int iter = 0; iter < 400000 && ok; ++iter)
    {
        std::string text;

        if (iter % 4 == 0)
        {
            text.resize (pick (40));

            for (auto& c : text)
                c = iter % 8 == 0 ? (char) rng() : alphabet[pick (alphabet.size())];
        }
        else
        {
            text = seeds[pick (seeds.size())];
            const int edits = 1 + (int) pick (5);

            for (int e = 0; e < edits; ++e)
            {
                const auto at = pick (text.size() + 1);

                switch (pick (5))
                {
                    case 0:  text.insert (at, 1, alphabet[pick (alphabet.size())]); break;
                    case 1:  if (at < text.size()) text.erase (at, 1 + pick (3)); break;
                    case 2:  if (at < text.size()) text[at] = alphabet[pick (alphabet.size())]; break;
                    case 3:  text.insert (at, pick (2) == 0 ? "..\\" : "../"); break;
                    default: text.insert (0, pick (2) == 0 ? "\\" : "/"); break;
                }
            }

            if (pick (50) == 0)
                text.append (pick (3000), 'x');
        }

        const auto kind = classify (text);
        const auto clean = sanitised (text);
        const auto cleanKind = classify (clean);
        refused += isUsable (kind) ? 0 : 1;

        const bool sane = isUsable (cleanKind)                                        // o que sai é sempre usável
                       && (isUsable (kind) ? clean == text : cleanKind != Kind::absolute)   // o recusado nunca vira absoluto
                       && ! (clean.size() >= 2 && isSeparator (clean[0]) && isSeparator (clean[1]))
                       && (isUsable (kind) || clean.find_first_of ("/\\") == std::string::npos)   // do recusado só fica um nome
                       && clean.size() <= text.size()
                       && classify (sanitised (clean)) == cleanKind;                 // aplicar de novo não muda nada

        // um relativo aceito nunca sai da pasta: a conta dos ".." refeita aqui, sem a função
        bool inside = true;

        if (cleanKind == Kind::relative)
        {
            int depth = 0;
            std::size_t pos = 0;

            while (pos <= clean.size())
            {
                auto cut = clean.find_first_of ("/\\", pos);
                cut = cut == std::string::npos ? clean.size() : cut;
                const auto part = clean.substr (pos, cut - pos);
                depth += part == ".." ? -1 : (part.empty() || part == "." ? 0 : 1);
                inside = inside && depth >= 0;
                pos = cut + 1;
            }
        }

        if (! sane || ! inside)
        {
            ok = false;
            worst = text;
        }

        (void) hasAudioExtension (text);
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - started).count();
    CHECK (ok, "400 mil caminhos mutados: o que sai do sanitised é usável, nunca de rede, nunca sai da pasta (pior caso: \"%s\")", worst.c_str());
    CHECK (refused > 1000, "o fuzz chegou nos casos recusados (%d)", refused);
    std::printf ("  %d recusados em %lld ms\n", refused, (long long) ms);
}

int main()
{
    testClassify();
    testSanitised();
    testAudioExtension();
    testFuzz();

    std::printf ("\n%s: %d falha(s)\n", failures == 0 ? "TUDO OK" : "FALHOU", failures);
    return failures;
}
