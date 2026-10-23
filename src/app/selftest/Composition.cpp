// Autoteste da entrega 3.2: piano roll e glide do 808, playlist e modo song, corte do áudio e BPM, headroom do kit.
// Usa ctx.beatWav (da 3.1) como clipe de áudio e deixa ctx.demoProject para a mixagem.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"

namespace whynot::selftest
{

void runCompositionSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto& workFolder = ctx.work;
    juce::ignoreUnused (rep, app, model, workFolder);
    const auto wavA = ctx.beatWav;   // o beat exportado pela 3.1

    rep.section ("Piano roll e glide do 808");
    app.run ("project.new");
    const double beat = 60.0 / 140.0;
    const auto b808 = channelId (model.get(), "808");
    const auto bassMixer = model.get().findChannel (b808)->output;
    rep.check (model.get().findChannel (b808)->mono && ! model.get().findChannel (b808)->oneShot,
               "808 do kit: mono e respeitando a duração da nota"_t);

    auto addNotes = [&] (const juce::String& ch, const juce::Array<juce::var>& notes)
    {
        return app.run ("pianoroll.addNotes", args ({ { "channel", ch }, { "notes", notes }, { "unit", "beats" } }));
    };

    rep.check (model.get().findChannel (b808)->rootKey == 36, "808 com raiz em Dó 2 (nota 36)"_t);
    const auto added = addNotes (b808, { noteVar (0, 1, 36, false), noteVar (1, 1, 43, true) });
    const auto noteIds = added.value["ids"];
    rep.check (added.ok && noteIds.size() == 2, "Dó e Sol com slide no piano roll do 808"_t);
    app.run ("mixer.setSolo", args ({ { "track", bassMixer }, { "on", true } }));

    const auto wavGlide = workFolder.getChildFile ("glide.wav");
    app.run ("project.exportWav", args ({ { "path", wavGlide.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
    const auto glide = readWav (wavGlide);
    const double hzBefore = measureHz (glide, 0.08, beat - 0.02);
    const double hzAfter = measureHz (glide, beat + 0.15, 2 * beat - 0.04);
    const float beforeSlide = peakIn (glide, beat - 0.06, beat - 0.005), afterSlide = peakIn (glide, beat + 0.005, beat + 0.06);
    rep.check (std::abs (hzBefore - 65.41) < 1.5, "antes do slide: " + juce::String (hzBefore, 2) + " Hz (Dó, 65,41 Hz)"_t);
    rep.check (std::abs (hzAfter - 98.0) < 2.0, "depois do slide: " + juce::String (hzAfter, 2) + " Hz (Sol, 98,00 Hz)"_t);
    rep.check (afterSlide <= beforeSlide * 1.05f, "com slide o 808 continua, sem reiniciar (nível "_t
                                                    + juce::String (beforeSlide, 3) + " → "_t + juce::String (afterSlide, 3) + ")");

    {
        auto update = new juce::DynamicObject();
        update->setProperty ("id", noteIds[1]);
        update->setProperty ("slide", false);
        app.run ("pianoroll.updateNotes", args ({ { "channel", b808 }, { "notes", juce::Array<juce::var> { juce::var (update) } } }));
    }

    const auto wavRetrig = workFolder.getChildFile ("sem_glide.wav");
    app.run ("project.exportWav", args ({ { "path", wavRetrig.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
    const auto retrig = readWav (wavRetrig);
    const float beforeCut = peakIn (retrig, beat - 0.06, beat - 0.005), afterCut = peakIn (retrig, beat + 0.005, beat + 0.06);
    rep.check (afterCut > beforeCut * 1.3f, "sem slide o 808 reinicia com ataque novo (nível "_t
                                              + juce::String (beforeCut, 3) + " → "_t + juce::String (afterCut, 3) + ")");
    app.run ("edit.undo");   // volta o slide

    // o sino do kit: a nota escrita é a que soa
    {
        const auto bellId = channelId (model.get(), "Bell");
        const auto bellMixer = model.get().findChannel (bellId)->output;
        app.run ("mixer.setSolo", args ({ { "track", bassMixer }, { "on", false } }));
        app.run ("mixer.setSolo", args ({ { "track", bellMixer }, { "on", true } }));
        addNotes (bellId, { noteVar (0, 1, 72, false), noteVar (2, 1, 79, false) });
        const auto wavBell = workFolder.getChildFile ("sino.wav");
        app.run ("project.exportWav", args ({ { "path", wavBell.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        const auto bellWav = readWav (wavBell);
        const double hzC5 = measureHz (bellWav, 0.05, beat - 0.02), hzG5 = measureHz (bellWav, 2 * beat + 0.05, 3 * beat - 0.02);
        rep.check (std::abs (hzC5 - 523.25) < 4.0, "Bell em C5: "_t + juce::String (hzC5, 1) + " Hz (523,3 Hz)");
        rep.check (std::abs (hzG5 - 783.99) < 6.0, "Bell em G5: "_t + juce::String (hzG5, 1) + " Hz (784,0 Hz)");
        app.run ("pianoroll.clear", args ({ { "channel", bellId } }));
        app.run ("mixer.setSolo", args ({ { "track", bellMixer }, { "on", false } }));
        app.run ("mixer.setSolo", args ({ { "track", bassMixer }, { "on", true } }));
    }

    //--------------------------------------------------------------------------
    rep.section ("Comandos do piano roll");
    auto bassNotes = [&] { auto* n = model.get().currentPatternPtr()->notesFor (b808); return n == nullptr ? std::vector<Note>() : *n; };
    rep.check (bassNotes().size() == 2 && bassNotes()[1].slide, "desfazer devolveu o slide");

    {
        auto move = new juce::DynamicObject();
        move->setProperty ("id", noteIds[1]);
        move->setProperty ("t", 2.0);
        app.run ("pianoroll.updateNotes", args ({ { "channel", b808 }, { "notes", juce::Array<juce::var> { juce::var (move) } }, { "unit", "beats" } }));
    }
    rep.check (bassNotes()[1].start == 2 * ticksPerBeat, "mover nota para o tempo 3");
    app.run ("edit.undo");
    rep.check (bassNotes()[1].start == ticksPerBeat, "desfazer devolve a nota ao tempo 2");

    app.run ("pianoroll.removeNotes", args ({ { "channel", b808 }, { "ids", juce::Array<juce::var> { noteIds[0] } } }));
    rep.check (bassNotes().size() == 1, "apagar uma nota");
    app.run ("edit.undo");
    rep.check (bassNotes().size() == 2, "desfazer devolve a nota apagada");

    addNotes (b808, { noteVar (5, 1, 36, false) });
    rep.check (model.get().currentPatternPtr()->lengthSteps == 32, "nota no compasso 2 faz o padrão crescer para 32 passos"_t);
    app.run ("edit.undo");
    rep.check (model.get().currentPatternPtr()->lengthSteps == 16, "desfazer devolve o padrão a 16 passos"_t);

    const auto hatId = channelId (model.get(), "Hat");
    app.run ("steps.setGrid", args ({ { "channel", hatId }, { "grid", "x.x.2..........." } }));
    app.run ("pianoroll.stepsToNotes", args ({ { "channel", hatId } }));
    const auto* hatNotes = model.get().currentPatternPtr()->notesFor (hatId);
    rep.check (hatNotes != nullptr && hatNotes->size() == 4, "passos viram notas no piano roll (2 + roll de 2)");
    rep.check (! model.get().currentPatternPtr()->steps.at (hatId)[0].on, "os passos convertidos saem do step sequencer");
    app.run ("edit.undo");

    app.run ("project.setKey", args ({ { "root", "F" }, { "scale", "minor" } }));
    rep.check (model.get().keyRoot == 5 && model.get().inScale (65) && ! model.get().inScale (66), "tonalidade Fá menor: Fá na escala, Fá# fora"_t);
    rep.check (! app.run ("project.setKey", args ({ { "root", "H" } })).ok, "tônica inválida é recusada"_t);

    //--------------------------------------------------------------------------
    rep.section ("Playlist e modo song");
    app.run ("mixer.setSolo", args ({ { "track", bassMixer }, { "on", false } }));
    app.run ("steps.setGrid", args ({ { "channel", channelId (model.get(), "Kick") }, { "grid", "x...x...x...x..." } }));
    rep.check (app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "unit", "bars" } })).ok, "padrão no compasso 1"_t);
    rep.check (app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 2 }, { "unit", "bars" } })).ok, "padrão no compasso 3 (o 2 fica vazio)"_t);

    const auto clipSource = wavA;   // o WAV da entrega 3.1 vira um clipe de áudio
    const auto audioItem = app.run ("playlist.addItem", args ({ { "track", 2 }, { "start", 3 }, { "unit", "bars" }, { "path", clipSource.getFullPathName() } }));
    rep.check (audioItem.ok, "clipe de áudio no compasso 4"_t + (audioItem.ok ? juce::String() : ": " + audioItem.error));
    rep.check (model.get().songEndTicks() == 5 * ticksPerBar4, "a música vai até o fim do compasso 5 (clipe de 2 compassos)"_t);

    app.run ("transport.setSongMode", args ({ { "on", true } }));
    const auto wavSong = workFolder.getChildFile ("musica.wav");
    app.run ("project.exportWav", args ({ { "path", wavSong.getFullPathName() } }));
    const auto song = readWav (wavSong);
    const double bar = 4 * beat;
    rep.check (song.valid && std::abs (song.buffer.getNumSamples() / song.sampleRate - 5 * bar) < 0.003,
               "exporta a música inteira: "_t + juce::String (song.valid ? song.buffer.getNumSamples() / song.sampleRate : 0.0, 3)
                   + " s (esperado " + juce::String (5 * bar, 3) + " s)");
    rep.check (peakIn (song, 0.0, bar) > 0.05f, "compasso 1 tem som");
    rep.check (peakIn (song, bar + 0.75, 2 * bar - 0.01) < 0.001f, "compasso 2 vazio fica em silêncio (depois das caudas)"_t);
    rep.check (peakIn (song, 2 * bar, 3 * bar) > 0.05f, "compasso 3 tem som");
    const double corr = correlation (song, 3 * bar, readWav (clipSource));
    rep.check (corr > 0.99, "o clipe de áudio toca no compasso 4, igual ao arquivo (correlação "_t + juce::String (corr, 4) + ")"_t);

    const auto songFile = workFolder.getChildFile ("Musica Teste").getChildFile ("Musica Teste.wnl");
    rep.check (app.run ("project.save", args ({ { "path", songFile.getFullPathName() } })).ok, "salva o projeto com piano roll e playlist");
    app.run ("project.new");
    rep.check (app.run ("project.open", args ({ { "path", songFile.getFullPathName() } })).ok && model.get().songMode
                   && model.get().playlistItems.size() == 3,
               "reabre com o modo song e os 3 blocos");
    const auto wavSong2 = workFolder.getChildFile ("musica_reaberta.wav");
    app.run ("project.exportWav", args ({ { "path", wavSong2.getFullPathName() } }));
    rep.check (maxDiff (song, readWav (wavSong2)) < 1.0e-6f, "a música reaberta soa igual (diferença máx. "_t
                                                             + juce::String (maxDiff (song, readWav (wavSong2)), 8) + ")"_t);

    //--------------------------------------------------------------------------
    rep.section ("Clipe de áudio aparado e mudança de BPM"_t);
    {
        // Só um clipe de áudio, aparado 0,5 s no começo e com 1 compasso. O corte é no tempo do
        // arquivo: mudar o BPM não pode mudar o ponto do arquivo onde o clipe começa.
        app.run ("project.new");
        app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
        const auto trimmed = app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "length", 1 }, { "unit", "bars" },
                                                                 { "path", clipSource.getFullPathName() }, { "offsetSeconds", 0.5 } }));
        rep.check (trimmed.ok, "clipe aparado em 0,5 s"_t + (trimmed.ok ? juce::String() : ": " + trimmed.error));
        app.run ("transport.setSongMode", args ({ { "on", true } }));

        const auto source = readWav (clipSource);
        auto renderAt = [&] (double bpm, const char* name)
        {
            app.run ("transport.setBpm", args ({ { "bpm", bpm } }));
            const auto f = workFolder.getChildFile (name);
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() } }));
            return readWav (f);
        };

        const auto at140 = renderAt (140.0, "clipe_140.wav");
        const auto at100 = renderAt (100.0, "clipe_100.wav");
        const double c140 = correlation (source, 0.5, at140), c100 = correlation (source, 0.5, at100);
        rep.check (c140 > 0.99, "a 140 BPM o clipe começa em 0,5 s do arquivo (correlação "_t + juce::String (c140, 4) + ")"_t);
        rep.check (c100 > 0.99, "a 100 BPM continua começando em 0,5 s do arquivo (correlação "_t + juce::String (c100, 4) + ")"_t);

        // o mesmo trecho, amostra por amostra, enquanto os dois renders duram (sem os últimos 50 ms,
        // onde o clipe de 140 BPM acaba e o motor pode suavizar a borda)
        auto firstSamples = [] (const Audio& x, int n)
        {
            Audio out;
            out.buffer.setSize (x.buffer.getNumChannels(), juce::jmax (0, n));
            for (int ch = 0; ch < x.buffer.getNumChannels(); ++ch)
                out.buffer.copyFrom (ch, 0, x.buffer, ch, 0, out.buffer.getNumSamples());
            out.sampleRate = x.sampleRate;
            out.valid = x.valid;
            return out;
        };
        const int common = juce::jmin (at140.buffer.getNumSamples(), at100.buffer.getNumSamples()) - (int) (0.05 * at140.sampleRate);
        const float headDiff = maxDiff (firstSamples (at100, common), firstSamples (at140, common));
        rep.check (at140.valid && at100.valid && common > 0 && headDiff < 1.0e-4f,
                   "o começo é o mesmo nos dois andamentos (diferença máx. "_t + juce::String (headDiff, 6) + ")");

        const double bar100 = 4 * 60.0 / 100.0;
        rep.check (std::abs (at100.buffer.getNumSamples() / at100.sampleRate - bar100) < 0.003,
                   "a 100 BPM o compasso dura mais e toca mais do arquivo, sem esticar ("_t + juce::String (bar100, 3) + " s)");
        rep.check (! model.get().playlistItems.empty() && model.get().playlistItems[0].offsetSeconds == 0.5,
                   "o corte guardado continua 0,5 s depois de mudar o BPM"_t);

        // .wnl antigo (esquema 2): o corte era em ticks no BPM do projeto (0,5 s a 100 BPM = 800 ticks)
        const juce::String currentTag = "\"schemaVersion\": " + juce::String (wnl::currentSchemaVersion);
        auto oldJson = wnl::toJson (model.get());
        const auto currentJson = oldJson;
        rep.check (oldJson.contains ("\"offsetSeconds\": 0.5") && oldJson.contains (currentTag),
                   "o esquema atual grava o corte em segundos (offsetSeconds)"_t);
        oldJson = oldJson.replace (currentTag, "\"schemaVersion\": 2").replace ("\"offsetSeconds\": 0.5", "\"offset\": 800");
        Project migrated;
        const bool opened = wnl::fromJson (oldJson, migrated).wasOk();
        rep.check (opened && ! migrated.playlistItems.empty() && std::abs (migrated.playlistItems[0].offsetSeconds - 0.5) < 1.0e-9,
                   "projeto do esquema 2 abre com o mesmo corte (800 ticks a 100 BPM = 0,5 s)");

        // salvar por cima de um arquivo antigo guarda o original em history/
        const auto oldFile = workFolder.getChildFile ("Esquema 2").getChildFile ("Esquema 2.wnl");
        oldFile.getParentDirectory().createDirectory();
        oldFile.replaceWithText (oldJson, false, false, "\n");   // como o app grava (LF): a cópia tem de sair idêntica
        const bool reopened = app.run ("project.open", args ({ { "path", oldFile.getFullPathName() } })).ok;
        const auto resaved = app.run ("project.save");
        const auto backup = oldFile.getSiblingFile ("history").getChildFile ("Esquema 2 (esquema 2).wnl");
        rep.check (reopened && resaved.ok && backup.existsAsFile() && backup.loadFileAsString() == oldJson
                       && oldFile.loadFileAsString().contains (currentTag),
                   "salvar converte para o esquema atual e guarda o original em history/"_t);

        // Esquema 3 (antes do canal synth): abre igual e, ao salvar no 4, o original fica em history/.
        const auto json3 = currentJson.replace (currentTag, "\"schemaVersion\": 3");
        const auto file3 = workFolder.getChildFile ("Esquema 3").getChildFile ("Esquema 3.wnl");
        file3.getParentDirectory().createDirectory();
        file3.replaceWithText (json3, false, false, "\n");
        const bool opened3 = app.run ("project.open", args ({ { "path", file3.getFullPathName() } })).ok;
        const auto sameAsBefore = opened3 && model.get().playlistItems.size() == migrated.playlistItems.size();
        const auto resaved3 = app.run ("project.save");
        const auto backup3 = file3.getSiblingFile ("history").getChildFile ("Esquema 3 (esquema 3).wnl");
        rep.check (sameAsBefore && resaved3.ok && backup3.existsAsFile() && backup3.loadFileAsString() == json3
                       && file3.loadFileAsString().contains ("\"schemaVersion\": 4"),
                   "projeto do esquema 3 abre igual, e salvar no esquema 4 guarda o original em history/"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Headroom do kit");
    {
        // O beat do teste no app: kick, clap, hats em todos os passos, 808 com slide e o sino.
        app.run ("project.new");
        const auto& pk = model.get();
        app.run ("steps.setGrid", args ({ { "channel", channelId (pk, "Kick") }, { "grid", "x.........x....." } }));
        app.run ("steps.setGrid", args ({ { "channel", channelId (pk, "Clap") }, { "grid", "....x.......x..." } }));
        app.run ("steps.setGrid", args ({ { "channel", channelId (pk, "Hat") }, { "grid", "xxxxxxxxxxxxxxxx" } }));
        app.run ("steps.setGrid", args ({ { "channel", channelId (pk, "Open hat") }, { "grid", "..............x." } }));
        addNotes (channelId (model.get(), "808"), { noteVar (0, 1.5, 36, false), noteVar (1.5, 1, 43, true), noteVar (2.5, 1.5, 36, false) });
        addNotes (channelId (model.get(), "Bell"), { noteVar (0, 0.5, 72, false), noteVar (0.5, 0.5, 70, false), noteVar (1, 0.5, 67, false),
                                                     noteVar (1.5, 0.5, 70, false), noteVar (2, 0.5, 72, false), noteVar (2.5, 0.5, 75, false),
                                                     noteVar (3, 0.5, 74, false), noteVar (3.5, 0.5, 70, false) });
        const auto wavFull = workFolder.getChildFile ("headroom.wav");
        app.run ("project.exportWav", args ({ { "path", wavFull.getFullPathName() }, { "loops", 2 }, { "what", "pattern" } }));
        const auto full = readWav (wavFull);
        rep.check (full.valid && peakDb (full) < -0.5f && peakDb (full) > -9.0f,
                   "beat completo com os faders padrão: pico "_t + juce::String (peakDb (full), 1) + " dBFS, sem clipar");

        // O critério da 3.2 inteiro por comandos: arranja o beat na playlist, exporta a música e salva
        // (o projeto fica em selftest_work/Demo 3.2 para as capturas da documentação).
        app.run ("pattern.rename", args ({ { "name", "Beat" } }));
        for (int barIndex = 0; barIndex < 4; ++barIndex)
            app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", barIndex }, { "unit", "bars" } }));
        app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 4 }, { "length", 4 }, { "unit", "bars" } }));
        app.run ("playlist.renameTrack", args ({ { "track", 1 }, { "name", "Beat" } }));
        app.run ("transport.setSongMode", args ({ { "on", true } }));

        const auto wavDemo = workFolder.getChildFile ("demo.wav");
        app.run ("project.exportWav", args ({ { "path", wavDemo.getFullPathName() } }));
        const auto demo = readWav (wavDemo);
        rep.check (demo.valid && std::abs (demo.buffer.getNumSamples() / demo.sampleRate - 8 * 4 * beat) < 0.003 && peakDb (demo) < -0.5f,
                   "música de 8 compassos exportada: "_t + juce::String (demo.valid ? demo.buffer.getNumSamples() / demo.sampleRate : 0.0, 3)
                       + " s, pico " + juce::String (peakDb (demo), 1) + " dBFS");

        const auto demoFile = workFolder.getChildFile ("Demo 3.2").getChildFile ("Demo 3.2.wnl");
        rep.check (app.run ("project.save", args ({ { "path", demoFile.getFullPathName() } })).ok, "salva o projeto de demonstração"_t);
        ctx.demoProject = demoFile;   // a mixagem começa deste projeto
    }
}

} // namespace whynot::selftest
