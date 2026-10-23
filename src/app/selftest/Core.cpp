// Autoteste da entrega 3.1: kit, beat por comandos, desfazer, roteamento, exportar e reabrir, mute/solo/master.
// Também os arquivos inválidos, que fecham o relatório.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"

namespace whynot::selftest
{

void runCoreSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto& workFolder = ctx.work;
    juce::ignoreUnused (rep, app, model, workFolder);

    //--------------------------------------------------------------------------
    rep.section ("Projeto novo com o kit WHYNOT");
    rep.check (model.get().channels.size() == 6, "6 canais (" + juce::String ((int) model.get().channels.size()) + ")");
    rep.check (model.get().mixer.size() == 7, "6 trilhas de mixer + master (" + juce::String ((int) model.get().mixer.size()) + ")");

    bool everyChannelHasOwnTrack = true;
    for (auto& c : model.get().channels)
        everyChannelHasOwnTrack = everyChannelHasOwnTrack && model.get().findMixer (c.output) != nullptr && model.get().findMixer (c.output)->autoFor == c.id;
    rep.check (everyChannelHasOwnTrack, "cada canal sai para a própria trilha de mixer"_t);
    rep.check (app.getProjector().sampleProblems().size() == 0, "todos os samples do kit carregaram");

    //--------------------------------------------------------------------------
    rep.section ("Beat por comandos");
    const auto& p0 = model.get();
    const auto kick = channelId (p0, "Kick"), clap = channelId (p0, "Clap"), hat = channelId (p0, "Hat"),
               open = channelId (p0, "Open hat"), bass = channelId (p0, "808");

    auto grid = [&] (const juce::String& ch, const char* g, int vel = 100)
    {
        return app.run ("steps.setGrid", args ({ { "channel", ch }, { "grid", g }, { "velocity", vel } })).ok;
    };

    rep.check (app.run ("transport.setBpm", args ({ { "bpm", 140.0 } })).ok, "BPM 140");
    rep.check (grid (kick, "x.........x....."), "kick");
    rep.check (grid (clap, "....x.......x..."), "clap");
    rep.check (grid (hat,  "x.x.x.x.x.x.3.x.", 90), "hat com roll de 3 no passo 13");
    rep.check (grid (open, "..............x."), "open hat");
    rep.check (grid (bass, "x.........x.....", 110), "808");
    rep.check (! app.run ("steps.setGrid", args ({ { "channel", "ch_inexistente" }, { "grid", "x" } })).ok, "canal inexistente é recusado"_t);

    //--------------------------------------------------------------------------
    rep.section ("Desfazer e refazer");
    const auto before = wnl::toJson (model.get());
    app.run ("steps.set", args ({ { "channel", kick }, { "index", 4 }, { "on", true } }));
    const auto after = wnl::toJson (model.get());
    rep.check (before != after, "o passo mudou o projeto");
    rep.check (app.run ("edit.undo").ok && wnl::toJson (model.get()) == before, "desfazer volta exatamente ao estado anterior");
    rep.check (app.run ("edit.redo").ok && wnl::toJson (model.get()) == after, "refazer reaplica a mudança"_t);
    app.run ("edit.undo");

    {
        CommandRegistry::ScopedGesture gesture (app.getCommands(), "Volume");
        for (int i = 0; i < 20; ++i)
            app.run ("mixer.setVolume", args ({ { "track", model.get().findChannel (kick)->output }, { "db", -0.5 * i } }));
    }
    rep.check (app.run ("edit.undo").ok && wnl::toJson (model.get()) == before, "20 ajustes de fader num gesto = um passo de desfazer");

    //--------------------------------------------------------------------------
    rep.section ("Roteamento");
    const auto kickTrack = model.get().findChannel (kick)->output;
    const auto clapTrack = model.get().findChannel (clap)->output;
    rep.check (app.run ("mixer.setOutput", args ({ { "track", kickTrack }, { "output", clapTrack } })).ok, "kick → bus do clap"_t);
    rep.check (! app.run ("mixer.setOutput", args ({ { "track", clapTrack }, { "output", kickTrack } })).ok, "laço clap → kick é recusado"_t);
    app.run ("edit.undo");
    rep.check (model.get().findMixer (kickTrack)->output.isEmpty(), "desfazer devolve o kick ao master");

    //--------------------------------------------------------------------------
    rep.section ("Exportar, salvar, reabrir, exportar de novo");
    const auto wavA = workFolder.getChildFile ("a.wav");
    const auto exportA = app.run ("project.exportWav", args ({ { "path", wavA.getFullPathName() }, { "loops", 2 } }));
    ctx.beatWav = wavA;   // a 3.2 usa este WAV como clipe de áudio
    rep.check (exportA.ok, "exporta WAV" + (exportA.ok ? juce::String() : ": " + exportA.error));
    const auto a = readWav (wavA);
    const double expected = 2 * 16 * 15.0 / 140.0;
    rep.check (a.valid && std::abs (a.buffer.getNumSamples() / a.sampleRate - expected) < 0.002,
               juce::String ("duração "_t) + juce::String (a.valid ? a.buffer.getNumSamples() / a.sampleRate : 0.0, 3) + " s (esperado " + juce::String (expected, 3) + " s)");
    rep.check (a.valid && a.buffer.getNumChannels() == 2 && a.sampleRate == 48000.0, "estéreo, 48 kHz"_t);
    rep.check (peakDb (a) > -12.0f && peakDb (a) < -0.3f, "pico " + juce::String (peakDb (a), 1) + " dBFS, sem clipar");

    const auto projectFile = workFolder.getChildFile ("Beat Teste").getChildFile ("Beat Teste.wnl");
    const auto save = app.run ("project.save", args ({ { "path", projectFile.getFullPathName() } }));
    rep.check (save.ok, "salva o projeto" + (save.ok ? juce::String() : ": " + save.error));
    rep.check (projectFile.getSiblingFile ("samples").getChildFile ("WHYNOT Kick.wav").existsAsFile(), "samples coletados para a pasta do projeto");
    rep.check (model.get().findChannel (kick)->samplePath == "samples/WHYNOT Kick.wav", "caminho do sample ficou relativo");
    rep.check (! model.dirty, "projeto marcado como salvo");

    const auto json = projectFile.loadFileAsString();
    Project reparsed;
    rep.check (wnl::fromJson (json, reparsed).wasOk() && reparsed == model.get(), "o .wnl gravado relê idêntico ao projeto em memória"_t);
    rep.check (wnl::toJson (reparsed) == json.trimEnd() || wnl::toJson (reparsed) + "\n" == json, "JSON estável: regravar dá o mesmo texto"_t);

    app.run ("project.new");
    rep.check (wnl::toJson (model.get()) != after, "projeto novo limpou o beat");

    const auto open1 = app.run ("project.open", args ({ { "path", projectFile.getFullPathName() } }));
    rep.check (open1.ok, "reabre o projeto" + (open1.ok ? juce::String() : ": " + open1.error));
    rep.check (app.getProjector().sampleProblems().size() == 0, "samples encontrados pela pasta do projeto");

    const auto wavB = workFolder.getChildFile ("b.wav");
    app.run ("project.exportWav", args ({ { "path", wavB.getFullPathName() }, { "loops", 2 } }));
    const auto b = readWav (wavB);
    rep.check (maxDiff (a, b) < 1.0e-6f, "WAV depois de reabrir é igual ao de antes (diferença máx. "_t + juce::String (maxDiff (a, b), 8) + ")");

    //--------------------------------------------------------------------------
    rep.section ("Mute, solo e master");
    const auto clapId = channelId (model.get(), "Clap");
    const auto clapMixer = model.get().findChannel (clapId)->output;
    juce::StringArray allChannels;

    for (auto& c : model.get().channels)
        allChannels.add (c.id);

    for (auto& id : allChannels)
        if (id != clapId)
            app.run ("channel.setMute", args ({ { "channel", id }, { "on", true } }));

    const auto wavOnlyClap = workFolder.getChildFile ("so_clap_mute.wav");
    app.run ("project.exportWav", args ({ { "path", wavOnlyClap.getFullPathName() }, { "loops", 2 } }));

    for (auto& id : allChannels)
        app.run ("channel.setMute", args ({ { "channel", id }, { "on", false } }));

    app.run ("mixer.setSolo", args ({ { "track", clapMixer }, { "on", true } }));
    const auto wavSolo = workFolder.getChildFile ("so_clap_solo.wav");
    app.run ("project.exportWav", args ({ { "path", wavSolo.getFullPathName() }, { "loops", 2 } }));
    const auto onlyClap = readWav (wavOnlyClap), solo = readWav (wavSolo);
    rep.check (peakDb (solo) > -30.0f, "solo do clap tem som (" + juce::String (peakDb (solo), 1) + " dBFS)");
    rep.check (maxDiff (onlyClap, solo) < 1.0e-6f, "solo do clap = todos os outros canais mudos");
    app.run ("mixer.setSolo", args ({ { "track", clapMixer }, { "on", false } }));

    // O som do canal passa mesmo pela trilha de mixer? Fader da trilha em -100 dB = silêncio.
    for (auto& id : allChannels)
    {
        const auto* ch = model.get().findChannel (id);
        const auto mixerId = ch->output;
        const auto name = ch->name;
        const auto wavLevel = workFolder.getChildFile ("nivel_" + mixerId + ".wav");
        app.run ("mixer.setSolo", args ({ { "track", mixerId }, { "on", true } }));
        app.run ("project.exportWav", args ({ { "path", wavLevel.getFullPathName() }, { "loops", 1 } }));
        const float alone = peakDb (readWav (wavLevel));
        app.run ("mixer.setVolume", args ({ { "track", mixerId }, { "db", -100.0 } }));
        app.run ("project.exportWav", args ({ { "path", wavLevel.getFullPathName() }, { "loops", 1 } }));
        const float faderDown = peakDb (readWav (wavLevel));
        app.run ("mixer.setVolume", args ({ { "track", mixerId }, { "db", -6.0 } }));
        app.run ("mixer.setSolo", args ({ { "track", mixerId }, { "on", false } }));
        rep.check (faderDown < -90.0f, name + ": sozinho " + juce::String (alone, 1) + " dBFS; com o fader da trilha em -100 dB, "
                                         + juce::String (faderDown, 1) + " dBFS");
    }

    app.run ("mixer.setVolume", args ({ { "track", masterTrackId }, { "db", -100.0 } }));
    const auto wavSilent = workFolder.getChildFile ("master_mudo.wav");
    app.run ("project.exportWav", args ({ { "path", wavSilent.getFullPathName() }, { "loops", 1 } }));
    rep.check (peakDb (readWav (wavSilent)) < -90.0f, "volume do master entra no render");
    app.run ("mixer.setVolume", args ({ { "track", masterTrackId }, { "db", 0.0 } }));

    app.run ("transport.setMetronome", args ({ { "on", true } }));
    const auto wavClick = workFolder.getChildFile ("metronomo.wav");
    app.run ("project.exportWav", args ({ { "path", wavClick.getFullPathName() }, { "loops", 2 } }));
    rep.check (maxDiff (a, readWav (wavClick)) < 1.0e-6f, "metrônomo não vaza para o WAV exportado"_t);
}

void runInvalidFilesSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;

    rep.section ("Arquivos inválidos"_t);
    Project dummy;
    rep.check (wnl::fromJson ("isso não é json"_t, dummy).failed(), "texto qualquer é recusado com mensagem"_t);
    rep.check (wnl::fromJson (R"({"format":"whynot-lab-project","schemaVersion":99})", dummy).failed(), "esquema mais novo é recusado"_t);
    rep.check (wnl::fromJson (R"({"format":"outra-coisa","schemaVersion":1})", dummy).failed(), "arquivo de outro programa é recusado"_t);

    // Achados da caça a bugs do Sampler (16/10): .wnl corrompido não derruba nem trava o app.
    {
        const auto deep = juce::String (R"({"format":"whynot-lab-project","schemaVersion":3,"channels":)")
                              + juce::String::repeatedString ("[", 20000) + juce::String::repeatedString ("]", 20000) + "}";
        const auto refused = wnl::fromJson (deep, dummy);
        rep.check (refused.failed() && refused.getErrorMessage().contains ("aninhamento"),
                   "20.000 colchetes aninhados são recusados com mensagem, sem estourar a pilha"_t);

        // Salvar por cima de um arquivo assim também lê o esquema dele (wnl::keepOlderSchema).
        const auto deepFile = ctx.work.getChildFile ("aninhado.wnl");
        deepFile.replaceWithText (deep);
        rep.check (wnl::keepOlderSchema (deepFile) == juce::File(),
                   "salvar por cima de um arquivo aninhado demais não estoura a pilha nem faz cópia em history/"_t);

        Project huge;
        wnl::normalise (huge);
        PlaylistItem item;
        item.id = "it_enorme";
        item.ref = huge.currentPattern;
        item.track = huge.playlistTracks.empty() ? juce::String() : huge.playlistTracks.front().id;
        item.start = 2147483000;
        item.length = 2147483647;
        huge.playlistItems.push_back (item);
        Project reread;
        const bool opened = wnl::fromJson (wnl::toJson (huge), reread).wasOk();
        rep.check (opened && reread.playlistItems.size() == 1 && reread.playlistItems[0].end() <= 2 * maxTimelineTicks
                       && reread.playlistItems[0].end() > reread.playlistItems[0].start && reread.songEndTicks() <= 2 * maxTimelineTicks,
                   "bloco com início e duração enormes fica no teto de 10.000 compassos (fim "_t
                       + juce::String (opened && ! reread.playlistItems.empty() ? reread.playlistItems[0].end() : 0) + " ticks)");
    }
}

} // namespace whynot::selftest

