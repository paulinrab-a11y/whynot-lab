// Testes da máscara de mudança por comando (src/commands/ChangeScope.h), sem JUCE (frente log-mascara, 16/11):
// - a tabela: nenhum id em duas listas, todo id de `byPrefix` com prefixo, nenhuma máscara vazia;
// - o que cada domínio pede das telas (src/ui/RefreshPlan.h): arrastar nota não refaz o mixer, fader não refaz
//   o piano roll, id desconhecido refaz tudo;
// - os ids de todos os comandos de src/commands/*.cpp, lidos como texto: cada um tem decisão explícita numa das
//   listas, o changesModel do registro bate com a lista (`notModel` ou não), e nenhuma linha da tabela ficou
//   sem comando. Comando novo sem decisão = este teste falha (e o app usa change::all até alguém decidir).

#include "../../src/commands/ChangeScope.h"
#include "../../src/ui/RefreshPlan.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace whynot;
using changescope::Decision;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

//==============================================================================
static void testTable()
{
    std::printf ("A tabela\n");

    std::map<std::string, int> seen;
    std::string twice, noPrefix, emptyMask, prefixLies;

    for (const auto& rule : changescope::exceptions)
    {
        ++seen[std::string (rule.id)];

        if (rule.mask == 0)
            emptyMask += " " + std::string (rule.id);

        // excecao igual ao prefixo nao e excecao: o lugar dela e o byPrefix
        if (changescope::prefixMask (rule.id) == rule.mask)
            prefixLies += " " + std::string (rule.id);
    }

    for (const auto id : changescope::byPrefix)
    {
        ++seen[std::string (id)];

        if (changescope::prefixMask (id) == 0)
            noPrefix += " " + std::string (id);
    }

    for (const auto id : changescope::everything)   ++seen[std::string (id)];
    for (const auto id : changescope::notModel)     ++seen[std::string (id)];

    for (const auto& [id, count] : seen)
        if (count > 1)
            twice += " " + id;

    CHECK (twice.empty(), "nenhum id em duas listas%s", twice.c_str());
    CHECK (noPrefix.empty(), "todo id do byPrefix tem prefixo na tabela%s", noPrefix.c_str());
    CHECK (emptyMask.empty(), "nenhuma excecao com mascara vazia%s", emptyMask.c_str());
    CHECK (prefixLies.empty(), "toda excecao difere do prefixo dela%s", prefixLies.c_str());

    for (const auto& p : changescope::prefixes)
        if (p.prefix.empty() || p.prefix.back() != '.' || p.mask == 0 || p.mask == change::all)
            emptyMask += " " + std::string (p.prefix);

    CHECK (emptyMask.empty(), "cada prefixo termina em ponto e tem um dominio so%s", emptyMask.c_str());
}

static void testMasks()
{
    std::printf ("As mascaras e as telas\n");

    using changescope::maskFor;
    using namespace refreshplan;

    CHECK (maskFor ("pianoroll.updateNotes") == change::patterns && maskFor ("steps.set") == change::patterns,
           "notas e passos: so os padroes");
    CHECK ((screensFor (maskFor ("pianoroll.updateNotes")) & mixer) == 0 && (screensFor (maskFor ("pianoroll.updateNotes")) & pianoRoll) != 0,
           "arrastar nota refaz o piano roll e nao refaz o mixer");
    CHECK (maskFor ("mixer.setVolume") == change::mixer && (screensFor (maskFor ("mixer.setVolume")) & pianoRoll) == 0
               && (screensFor (maskFor ("mixer.setVolume")) & (mixer | rack)) == (mixer | rack),
           "fader: o mixer e o rack, sem o piano roll");
    CHECK (maskFor ("playlist.updateItem") == change::playlist && screensFor (maskFor ("playlist.updateItem")) == playlist,
           "mover bloco: so a playlist");
    CHECK (maskFor ("automation.setPoints") == change::playlist, "ponto de automacao: a playlist");
    CHECK (maskFor ("channel.setMute") == change::channels && screensFor (maskFor ("channel.setMute")) == allScreens,
           "canal: todas as telas");
    CHECK (maskFor ("transport.setSongMode") == change::transport && (screensFor (maskFor ("transport.setSongMode")) & mixer) == 0,
           "SONG/PAT: sem o mixer");

    // onde o prefixo mente
    CHECK ((maskFor ("pattern.select") & change::transport) != 0 && (maskFor ("pattern.add") & change::transport) != 0
               && (maskFor ("pattern.clone") & change::transport) != 0,
           "pattern.select, add e clone trocam o padrao atual (transporte)");
    CHECK (maskFor ("pattern.rename") == change::patterns && maskFor ("pattern.setLength") == change::patterns,
           "pattern.rename e setLength ficam nos padroes");
    CHECK (maskFor ("channel.addSampler") == change::all && maskFor ("channel.remove") == change::all
               && maskFor ("channel.addAudio") == change::all && maskFor ("channel.addSynth") == change::all
               && maskFor ("channel.addPiano") == change::all && maskFor ("channel.addPlugin") == change::all
               && maskFor ("channel.addKit") == change::all && maskFor ("channel.addSlicer") == change::all,
           "canal novo ou removido: tudo (trilha de mixer, passos, blocos)");
    CHECK (maskFor ("mixer.removeTrack") == change::all, "mixer.removeTrack: tudo (a saida dos canais, a raia que grava, as curvas)");
    CHECK ((maskFor ("mixer.applyStyle") & change::mixer) != 0 && (maskFor ("mixer.removeInsert") & change::playlist) != 0,
           "estilo e remover efeito: o mixer e as curvas de automacao");
    CHECK (maskFor ("playlist.addItem") == change::all, "playlist.addItem pode criar o canal de audio: tudo");
    CHECK ((maskFor ("playlist.setLaneInput") & change::mixer) != 0 && (maskFor ("playlist.selectMarkers") & change::transport) != 0,
           "playlist.setLaneInput mexe no mixer; selectMarkers liga o SONG");
    CHECK ((maskFor ("transport.setBpm") & change::playlist) != 0, "o BPM refaz os fades dos blocos");
    CHECK ((maskFor ("channel.rename") & change::mixer) != 0 && (maskFor ("channel.setFrozen") & change::mixer) != 0,
           "renomear e congelar canal mexem na trilha dele");

    // fora da tabela: tudo
    CHECK (maskFor ("project.open") == change::all && maskFor ("edit.undo") == change::all && maskFor ("project.setKey") == change::all
               && maskFor ("project.importFrom") == change::all && maskFor ("midi.map") == change::all,
           "project.*, desfazer e o que nao tem prefixo: tudo");
    CHECK (maskFor ("mixer.algoNovo") == change::all && maskFor ("pianoroll.algoNovo") == change::all && maskFor ("") == change::all
               && maskFor ("qualquer.coisa") == change::all,
           "id que ninguem conferiu vale tudo, mesmo com prefixo conhecido");
    CHECK (changescope::decisionFor ("mixer.algoNovo") == Decision::none && changescope::decisionFor ("mixer.setPan") == Decision::prefix
               && changescope::decisionFor ("pattern.add") == Decision::exception
               && changescope::decisionFor ("channel.remove") == Decision::everything
               && changescope::decisionFor ("transport.play") == Decision::notModel,
           "decisionFor diz de qual lista veio");

    // gesto e lote: cada comando avisa com a mascara dele, e quem escuta soma (refreshplan::onChange)
    const auto gesture = maskFor ("pianoroll.updateNotes") | maskFor ("mixer.setVolume");
    auto plan = onChange (0, maskFor ("pianoroll.updateNotes"), rack);
    plan = onChange (plan.pending, maskFor ("mixer.setVolume"), rack);
    CHECK (gesture == (change::patterns | change::mixer) && plan.pending == (screensFor (gesture) & ~rack),
           "gesto com dois dominios: as pendentes sao as da uniao");

    int precise = 0;

    for (const auto id : changescope::byPrefix)
        precise += maskFor (id) != change::all ? 1 : 0;

    for (const auto& rule : changescope::exceptions)
        precise += maskFor (rule.id) != change::all ? 1 : 0;

    std::printf ("  (%d comandos com mascara menor que tudo, %d em tudo, %d que nao mudam o modelo)\n", precise,
                 (int) std::size (changescope::everything), (int) std::size (changescope::notModel));
}

//==============================================================================
// Os comandos registrados, lidos do código como texto (a técnica de tests/control/test_control_core.cpp).
namespace scan
{
    struct Command
    {
        std::string id, file;
        bool changesModel = true;
        bool flagFound = false;
    };

    static bool readLiteral (const std::string& s, std::size_t& at, std::string& out)
    {
        while (at < s.size() && (s[at] == ' ' || s[at] == '\n' || s[at] == '\r' || s[at] == '\t'))
            ++at;

        if (at >= s.size() || s[at] != '"')
            return false;

        for (++at; at < s.size() && s[at] != '"'; ++at)
        {
            if (s[at] == '\\' && at + 1 < s.size())
                ++at;

            out += s[at];
        }

        return at < s.size();
    }

    /** O changesModel de um r.add: o último true ou false antes do lambda ("... }), true,\n [] (CommandContext&"). */
    static bool readFlag (const std::string& block, bool& changesModel)
    {
        const auto lambda = block.find ("(CommandContext");

        if (lambda == std::string::npos)
            return false;

        const auto head = block.substr (0, lambda);
        const auto yes = head.rfind ("true"), no = head.rfind ("false");

        if (yes == std::string::npos && no == std::string::npos)
            return false;

        changesModel = no == std::string::npos || (yes != std::string::npos && yes > no);
        return true;
    }

    /** Cada r.add ({ "id", ... }) de um arquivo. O r.add com o id numa variável (o channelFlag do
        CompositionCommands.cpp) vale para cada `nomeDaAjuda ("id", ...)` do arquivo. */
    static void commandsIn (const std::string& text, const std::string& file, std::vector<Command>& out, int& dynamicBlocks)
    {
        static const std::string marker = "r.add ({";

        for (auto at = text.find (marker); at != std::string::npos;)
        {
            const auto next = text.find (marker, at + marker.size());
            const auto block = text.substr (at + marker.size(), next == std::string::npos ? std::string::npos : next - at - marker.size());
            std::size_t pos = 0;
            Command c;
            c.file = file;
            c.flagFound = readFlag (block, c.changesModel);

            if (readLiteral (block, pos, c.id))
            {
                out.push_back (c);
            }
            else
            {
                ++dynamicBlocks;

                // o nome da função de ajuda: "auto channelFlag = [&r] (const char* id, ..." logo antes do r.add
                const auto assign = text.rfind (" = [&r] (const char* id", at);
                const auto nameStart = assign == std::string::npos ? std::string::npos : text.rfind (' ', assign - 1);

                if (nameStart != std::string::npos)
                {
                    const auto call = text.substr (nameStart + 1, assign - nameStart - 1) + " (\"";

                    for (auto use = text.find (call, at); use != std::string::npos; use = text.find (call, use + 1))
                    {
                        auto idAt = use + call.size() - 1;
                        Command d = c;
                        d.id.clear();

                        if (readLiteral (text, idAt, d.id))
                            out.push_back (d);
                    }
                }
            }

            at = next;
        }
    }
}

static void testEveryCommandDecided()
{
    std::printf ("Todo comando de src/commands tem decisao\n");

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path folder;

    for (const auto& candidate : { fs::path (__FILE__).parent_path() / ".." / ".." / "src" / "commands", fs::path ("src") / "commands",
                                   fs::path ("..") / "src" / "commands", fs::path ("..") / ".." / "src" / "commands" })
    {
        if (fs::is_directory (candidate, ec))
        {
            folder = candidate;
            break;
        }
    }

    CHECK (! folder.empty(), "achou src/commands (rode da raiz do repositorio, ou compile com o caminho completo do teste)");

    if (folder.empty())
        return;

    std::vector<scan::Command> commands;
    int dynamicBlocks = 0;

    for (auto& entry : fs::directory_iterator (folder, ec))
    {
        if (entry.path().extension() != ".cpp")
            continue;

        std::ifstream in (entry.path(), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        scan::commandsIn (text.str(), entry.path().filename().string(), commands, dynamicBlocks);
    }

    std::set<std::string> inSource;
    std::string undecided, repeated, noFlag, wrongList;
    int model = 0;

    for (const auto& c : commands)
    {
        if (! inSource.insert (c.id).second)
            repeated += " " + c.id;

        const auto decision = changescope::decisionFor (c.id);

        if (decision == Decision::none)
        {
            undecided += " " + c.id + " (" + c.file + ")";
            continue;
        }

        if (! c.flagFound)
        {
            noFlag += " " + c.id;
            continue;
        }

        model += c.changesModel ? 1 : 0;

        // notModel e changesModel = false andam juntos: um comando que passou a mudar o modelo sai do notModel
        // (e ganha mascara), e um que deixou de mudar nao fica com mascara que ninguem usa
        if ((decision == Decision::notModel) == c.changesModel)
            wrongList += " " + c.id + (c.changesModel ? " (muda o modelo e esta no notModel)" : " (nao muda o modelo e tem mascara)");
    }

    CHECK (commands.size() >= 250, "leu os comandos registrados em src/commands (%zu, %d mudam o modelo, %d com o id numa variavel)",
           commands.size(), model, dynamicBlocks);
    CHECK (dynamicBlocks <= 1, "so o channelFlag registra com o id numa variavel (%d blocos): outro assim precisa entrar na leitura deste teste",
           dynamicBlocks);
    CHECK (inSource.count ("channel.setMono") == 1 && inSource.count ("channel.setOneShot") == 1,
           "achou os ids do channelFlag (channel.setMono e channel.setOneShot)");
    CHECK (repeated.empty(), "nenhum id registrado duas vezes%s", repeated.c_str());
    CHECK (noFlag.empty(), "leu o changesModel de cada comando%s", noFlag.c_str());
    CHECK (undecided.empty(),
           "todo comando tem decisao em src/commands/ChangeScope.h (byPrefix, exceptions, everything ou notModel)%s%s",
           undecided.empty() ? "" : " - SEM DECISAO:", undecided.c_str());
    CHECK (wrongList.empty(), "o changesModel de cada comando bate com a lista dele%s", wrongList.c_str());

    // linha da tabela sem comando: o comando saiu ou mudou de nome, e a linha ficou
    std::string stale;

    for (const auto& rule : changescope::exceptions)
        if (inSource.count (std::string (rule.id)) == 0)
            stale += " " + std::string (rule.id);

    for (const auto id : changescope::byPrefix)
        if (inSource.count (std::string (id)) == 0)
            stale += " " + std::string (id);

    for (const auto id : changescope::everything)
        if (inSource.count (std::string (id)) == 0)
            stale += " " + std::string (id);

    for (const auto id : changescope::notModel)
        if (inSource.count (std::string (id)) == 0)
            stale += " " + std::string (id);

    CHECK (stale.empty(), "toda linha da tabela e de um comando que existe%s", stale.c_str());
}

int main()
{
    testTable();
    testMasks();
    testEveryCommandDecided();

    std::printf (failures == 0 ? "\nTudo certo.\n" : "\n%d falha(s).\n", failures);
    return failures == 0 ? 0 : 1;
}
