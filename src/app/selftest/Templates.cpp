// Autoteste dos templates de projeto (16/11): os de fábrica abrem com o que prometem; salvar o projeto aberto como
// template e abrir um projeto novo dele dá o mesmo som; o template não muda quando o projeto novo muda; "só a
// estrutura" tira as notas; o "novo projeto" passa a usar o template escolhido e volta ao de fábrica; apagar leva
// para Templates/apagados. Os templates ficam em <dados do autoteste>/Templates, nunca no %APPDATA% de quem roda.

#include "SelfTestKit.h"

#include "../../library/Templates.h"

namespace whynot::selftest
{

void runTemplatesSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("templates");
    work.createDirectory();

    auto render = [&] (const juce::String& name)
    {
        const auto f = work.getChildFile (name);
        f.deleteFile();
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
        return readWav (f);
    };
    auto names = [] (const juce::var& list)
    {
        juce::StringArray out;

        if (auto* a = list.getArray())
            for (auto& t : *a)
                out.add (t["name"].toString());

        return out;
    };
    auto hasInserts = [&]
    {
        for (auto& m : model.get().mixer)
            if (! m.inserts.empty())
                return true;

        return false;
    };

    rep.section ("Templates de projeto: de fábrica, salvar, novo a partir dele, padrão e apagar (16/11)"_t);

    rep.check (templates::folder().isAChildOf (ctx.data),
               "os templates do autoteste ficam na pasta de dados dele: "_t + templates::folder().getFullPathName());

    const auto listed = app.run ("template.list");
    const auto factory = names (listed.value["templates"]);
    rep.check (listed.ok && factory.contains ("Kit WHYNOT") && factory.contains ("Vazio") && factory.contains ("Trap")
                   && factory.contains ("Funk") && factory.contains ("Boom bap"),
               "template.list traz os de fábrica: "_t + factory.joinIntoString (", "));

    // os de fábrica
    {
        const auto empty = app.run ("template.new", args ({ { "name", "Vazio" } }));
        rep.check (empty.ok && model.get().channels.empty() && model.file == juce::File(),
                   "\"Vazio\": nenhum canal, projeto sem arquivo ("_t + juce::String ((int) model.get().channels.size()) + ")");

        const auto trap = app.run ("template.new", args ({ { "name", "Trap" } }));
        const auto bass = channelId (model.get(), "808");
        const auto* ch808 = model.get().findChannel (bass);
        rep.check (trap.ok && model.get().channels.size() >= 4 && std::abs (model.get().bpm - 140.0) < 0.01
                       && ch808 != nullptr && ch808->mono && hasInserts()
                       && model.get().playlistTracks.size() > 1 && model.get().playlistTracks[0].name == "Bateria",
                   "\"Trap\": o kit, 140 BPM, o 808 mono, a mixagem com efeitos e as raias nomeadas ("_t
                       + juce::String ((int) model.get().channels.size()) + " canais, "_t + juce::String (model.get().bpm) + " BPM)");
        rep.check (! app.getCommands().canUndo(), "abrir um template limpa o desfazer, como o novo projeto"_t);

        const auto boom = app.run ("template.new", args ({ { "name", "Boom bap" } }));
        rep.check (boom.ok && model.get().bpm < 100.0 && hasInserts(),
                   "\"Boom bap\": o andamento lento do gênero ("_t + juce::String (model.get().bpm) + " BPM) e a mixagem"_t);

        rep.check (! app.run ("template.new", args ({ { "name", "Nao existe" } })).ok && model.get().bpm < 100.0,
                   "template que não existe: recusa e o projeto aberto fica como estava"_t);
    }

    // salvar o projeto aberto e abrir um novo a partir dele
    app.run ("template.new", args ({ { "name", "Trap" } }));
    app.run ("transport.setBpm", args ({ { "bpm", 150 } }));
    setGrid (app, channelId (model.get(), "Kick"), "x...x...x...x...");
    setGrid (app, channelId (model.get(), "Hat"), "..x...x...x...x.");
    app.run ("playlist.addItem", args ({ { "kind", "pattern" }, { "ref", model.get().currentPattern }, { "track", 1 }, { "start", 0 } }));
    const auto original = render ("original.wav");

    const auto saved = app.run ("template.save", args ({ { "name", "Meu ponto" } }));
    const auto file = templates::fileFor ("Meu ponto");
    rep.check (saved.ok && file.existsAsFile() && file.isAChildOf (templates::folder()),
               "template.save grava em Templates/Meu ponto: "_t + (saved.ok ? file.getFullPathName() : saved.error));
    rep.check (! app.run ("template.save", args ({ { "name", "Meu ponto" } })).ok,
               "nome repetido é recusado (só com replace)"_t);
    rep.check (! app.run ("template.save", args ({ { "name", "Trap" } })).ok && ! app.run ("template.save", args ({ { "name", "  " } })).ok,
               "nome de fábrica e nome vazio são recusados"_t);

    const auto before = file.loadFileAsString();
    app.run ("template.new", args ({ { "name", "Vazio" } }));
    const auto opened = app.run ("template.new", args ({ { "name", "Meu ponto" } }));
    const auto copy = render ("do-template.wav");
    rep.check (opened.ok && std::abs (model.get().bpm - 150.0) < 0.01 && model.file == juce::File()
                   && original.valid && copy.valid && peakDb (original) > -60.0f && maxDiff (original, copy) < 1.0e-5f,
               "novo a partir do template soa igual ao projeto que o gerou (diferença máxima "_t
                   + juce::String (maxDiff (original, copy), 6) + ", pico "_t + juce::String (peakDb (copy), 1) + " dB), sem arquivo"_t);

    // o projeto novo é uma cópia: salvar em outro lugar não mexe no template
    setGrid (app, channelId (model.get(), "Kick"), "xxxxxxxxxxxxxxxx");
    const auto elsewhere = work.getChildFile ("Outro").getChildFile ("Outro.wnl");
    app.run ("project.save", args ({ { "path", elsewhere.getFullPathName() } }));
    rep.check (elsewhere.existsAsFile() && file.loadFileAsString() == before,
               "mudar e salvar o projeto novo em outro lugar não muda o template"_t);

    // só a estrutura
    app.run ("template.new", args ({ { "name", "Meu ponto" } }));
    const auto structure = app.run ("template.save", args ({ { "name", "Estrutura" }, { "clearNotes", true } }));
    app.run ("template.new", args ({ { "name", "Estrutura" } }));
    const auto silent = render ("estrutura.wav");
    rep.check (structure.ok && model.get().channels.size() >= 4 && std::abs (model.get().bpm - 150.0) < 0.01
                   && silent.valid && peakDb (silent) < -100.0f,
               "\"só a estrutura\": os canais e o BPM ficam, as notas e os blocos saem (pico "_t + juce::String (peakDb (silent), 1) + " dB)"_t);

    // o "novo projeto" do app
    rep.check (app.run ("template.setDefault", args ({ { "name", "Meu ponto" } })).ok, "template.setDefault escolhe o \"Meu ponto\""_t);
    app.run ("project.new");
    rep.check (std::abs (model.get().bpm - 150.0) < 0.01 && model.get().channels.size() >= 4,
               "com o padrão escolhido, o novo projeto começa do template ("_t + juce::String (model.get().bpm) + " BPM)"_t);

    app.run ("template.setDefault", args ({ { "name", "" } }));
    app.run ("project.new");
    rep.check (templates::defaultName().isEmpty() && std::abs (model.get().bpm - 140.0) < 0.01 && model.get().channels.size() >= 4,
               "setDefault \"\" volta ao novo projeto de fábrica (o kit em 140 BPM)"_t);

    // apagar
    const auto removed = app.run ("template.delete", args ({ { "name", "Meu ponto" } }));
    const auto after = names (app.run ("template.list").value["templates"]);
    rep.check (removed.ok && ! after.contains ("Meu ponto") && ! file.existsAsFile()
                   && templates::deletedFolder().getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0,
               "apagar tira da lista e leva a pasta para Templates/apagados"_t);
    rep.check (! app.run ("template.delete", args ({ { "name", "Trap" } })).ok, "os de fábrica não se apagam"_t);

    app.run ("template.delete", args ({ { "name", "Estrutura" } }));
    app.run ("project.new");
}

} // namespace whynot::selftest
