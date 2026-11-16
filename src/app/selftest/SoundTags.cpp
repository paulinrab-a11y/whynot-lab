// Autoteste das tags dos sons (16/11): o navegador filtra pelo tipo lido no nome e na pasta, soma com a busca e com
// as #tags do usuário; a correção do usuário vale mais que a regra; as tags ficam no tags-sons.json da pasta de dados
// do autoteste (nunca no %APPDATA% de quem roda); arquivo estragado não derruba; os comandos da IA leem e marcam.
// Os arquivos do índice não precisam existir: o tipo sai do caminho, sem ler o áudio.

#include "SelfTestKit.h"

#include "../../library/SoundTags.h"
#include "../../ui/SampleBrowser.h"

namespace whynot::selftest
{

void runSoundTagsSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& store = soundtags::Store::get();
    using soundtags::bit;
    using soundtags::Type;

    rep.section ("Tags dos sons: tipo pelo nome e pela pasta, tags suas e o filtro do navegador (16/11)"_t);

    const auto dir = ctx.work.getChildFile ("tags-sons");
    const auto kick1 = dir.getChildFile ("Kick 01.wav"),          hardKick = dir.getChildFile ("Hard Kick.wav"),
               snare = dir.getChildFile ("Snare Fat.wav"),        closed = dir.getChildFile ("HH closed.wav"),
               open = dir.getChildFile ("OH 2.wav"),              bass = dir.getChildFile ("808 Long Am.wav"),
               chat = dir.getChildFile ("chat noise.wav"),        pad = dir.getChildFile ("Pad Dark 120bpm Am.wav"),
               boom = dir.getChildFile ("Kicks").getChildFile ("Boom.wav");
    const juce::Array<juce::File> files { kick1, hardKick, snare, closed, open, bass, chat, pad, boom };

    rep.check (store.file().isAChildOf (ctx.data), "as tags do autoteste ficam na pasta de dados dele: "_t + store.file().getFullPathName());

    juce::Component host;
    host.setSize (272, 420);
    SampleBrowser browser (app);
    host.addAndMakeVisible (browser);
    browser.setBounds (host.getLocalBounds());
    browser.setIndexForTest (files);

    auto shown = [&browser] (std::initializer_list<juce::File> want, std::initializer_list<juce::File> notWant)
    {
        for (auto& f : want)
            if (browser.rowOf (f) < 0)
                return false;

        for (auto& f : notWant)
            if (browser.rowOf (f) >= 0)
                return false;

        return true;
    };

    browser.setTypeFilter (bit (Type::kick));
    rep.check (shown ({ kick1, hardKick, boom }, { snare, closed, chat, pad, bass }),
               "tipo Kick: os kicks pelo nome e o da pasta Kicks; snare, hat, 808 e pad ficam de fora"_t);

    browser.setTypeFilter (bit (Type::hat) | bit (Type::openHat));
    rep.check (shown ({ closed, open }, { chat, kick1 }),
               "Hat ou Open hat: \"HH closed\" e \"OH 2\" entram; \"chat noise\" não é hat"_t);

    browser.setTypeFilter (bit (Type::s808));
    rep.check (shown ({ bass }, { kick1, pad }), "tipo 808: o \"808 Long Am\""_t);

    // a correção do usuário vale mais que a regra
    store.setTypes ({ chat }, bit (Type::hat), false);
    browser.setTypeFilter (bit (Type::hat));
    rep.check (shown ({ chat, closed }, {}), "corrigir o tipo do \"chat noise\" para Hat põe ele no filtro de hat"_t);
    store.setTypes ({ chat }, 0, true);
    browser.setTypeFilter (bit (Type::hat));
    rep.check (shown ({ closed }, { chat }), "voltar ao automático tira de novo"_t);

    // as #tags do usuário, sozinhas e com a busca por nome
    store.change ({ kick1, snare }, juce::StringArray ("escuro"), {});
    browser.setTypeFilter (0);
    browser.setSearchForTest ("#escuro");
    rep.check (shown ({ kick1, snare }, { hardKick, closed }), "#escuro: só os sons marcados"_t);
    browser.setSearchForTest ("kick #escuro");
    rep.check (shown ({ kick1 }, { snare, hardKick }), "\"kick #escuro\": nome e tag juntos"_t);
    browser.setSearchForTest ("#escuro");
    browser.setTypeFilter (bit (Type::snare));
    rep.check (shown ({ snare }, { kick1 }), "#escuro com o tipo Snare: os dois filtros somam"_t);
    browser.setTypeFilter (0);
    browser.setSearchForTest ({});

    // grava e relê
    store.setFolder (ctx.data);
    rep.check (store.file().existsAsFile() && store.of (kick1).labels.contains ("escuro") && store.allLabels().contains ("escuro"),
               "as tags ficam no tags-sons.json e voltam ao reler"_t);

    // os comandos da IA
    const auto read = app.run ("browser.tags", args ({ { "path", bass.getFullPathName() } }));
    bool is808 = false;

    if (auto* types = read.value["types"].getArray())
        for (auto& t : *types)
            is808 = is808 || t.toString() == "808";

    rep.check (read.ok && is808 && read.value["key"].toString().startsWith ("A"),
               "browser.tags: o 808, com o tom do nome ("_t + read.value["key"].toString() + ")");
    rep.check ((int) app.run ("browser.tags", args ({ { "path", pad.getFullPathName() } })).value["bpm"] == 120,
               "browser.tags: o BPM do nome do pad (120)"_t);

    juce::Array<juce::var> paths { hardKick.getFullPathName() }, add { "teste" };
    const auto marked = app.run ("browser.setTags", args ({ { "paths", paths }, { "add", add } }));
    rep.check (marked.ok && store.of (hardKick).labels.contains ("teste"), "browser.setTags marca a tag"_t);

    juce::Array<juce::var> wrong { "nao-existe" };
    rep.check (! app.run ("browser.setTags", args ({ { "paths", paths }, { "types", wrong } })).ok,
               "browser.setTags recusa tipo desconhecido"_t);

    // arquivo estragado: não derruba, guarda ao lado e começa vazio
    store.file().replaceWithText ("{{{ isto não é json");
    store.setFolder (ctx.data);
    const auto kept = store.file().getSiblingFile ("tags-sons.estragado.json");
    rep.check (store.loadProblem().isNotEmpty() && kept.existsAsFile() && store.allLabels().isEmpty(),
               "arquivo estragado: guardado em tags-sons.estragado.json e as tags começam vazias ("_t + store.loadProblem() + ")");
    kept.deleteFile();

    // uma biblioteca grande: 5.000 sons filtrados pelo tipo
    {
        juce::Array<juce::File> many;
        const char* names[] = { "Kick", "Snare", "Clap", "HiHat", "Open Hat", "Perc", "808", "Loop 140bpm", "Bell", "FX Riser" };

        for (int i = 0; i < 5000; ++i)
            many.add (dir.getChildFile (juce::String (names[i % 10]) + " " + juce::String (i) + ".wav"));

        browser.setIndexForTest (many);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        browser.setTypeFilter (bit (Type::kick));
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        rep.check (browser.rowOf (many[0]) >= 0 && browser.rowOf (many[1]) < 0 && ms < 500.0,
                   "5.000 sons filtrados pelo tipo em "_t + juce::String (ms, 1) + " ms"_t);
    }

    browser.setTypeFilter (0);
    store.file().deleteFile();
    store.setFolder (ctx.data);
}

} // namespace whynot::selftest
