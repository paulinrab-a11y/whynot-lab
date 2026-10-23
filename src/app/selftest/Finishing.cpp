// Autoteste da entrega 3.4: MP3, MIDI, stems, versões e autosave, queda, atalhos, desfazer do FL, placa de som e prévia.

#include "SelfTestKit.h"
#include "../../app/KeyMap.h"
#include "../../app/Session.h"
#include "../../engine/PreviewPlayer.h"
#include "../../model/WnlFormat.h"
#include "../../record/RecordEngine.h"
#include "../../ui/DeviceSettings.h"

namespace whynot::selftest
{

void runFinishingSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto& workFolder = ctx.work;
    juce::ignoreUnused (rep, app, model, workFolder);
    const auto& dataFolder = ctx.data;
    auto grid = [&] (const juce::String& ch, const char* g, int vel = 100) { return setGrid (app, ch, g, vel); };

    //==========================================================================
    // Entrega 3.4

    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", false } }));
    const auto kick34 = channelId (model.get(), "Kick"), hat34 = channelId (model.get(), "Hat"), bass34 = channelId (model.get(), "808");
    grid (kick34, "x.........x.....");
    grid (hat34,  "x.x.x.x.x.x.x.x.", 90);
    grid (bass34, "x.......x.......", 110);

    [[maybe_unused]] const double seconds34 = 2 * 16 * 15.0 / 140.0;   // 2 passadas do padrão; só o MP3 usa (sem o LAME, fica sem uso)
    const auto mixWav = workFolder.getChildFile ("34_mix.wav");
    app.run ("project.exportWav", args ({ { "path", mixWav.getFullPathName() }, { "loops", 2 }, { "what", "pattern" } }));
    const auto mix = readWav (mixWav);

    rep.section ("Exportar MP3");
    const auto mp3File = workFolder.getChildFile ("34_beat.mp3");
    const auto mp3 = app.run ("project.exportMp3", args ({ { "path", mp3File.getFullPathName() }, { "loops", 2 },
                                                          { "what", "pattern" }, { "bitrate", 320 } }));
   #if WHYNOT_HAS_LAME
    rep.check (mp3.ok, "exporta MP3 320 kbps" + (mp3.ok ? juce::String() : ": " + mp3.error));
    juce::MemoryBlock head;
    mp3File.loadFileAsData (head);
    rep.check (head.getSize() > 10 && std::memcmp (head.getData(), "ID3", 3) == 0, "começa com a tag ID3v2 (título e programa)"_t);

    const auto decoded = readWav (mp3File);   // o leitor de MP3 do JUCE
    const double mp3Seconds = decoded.valid ? decoded.buffer.getNumSamples() / decoded.sampleRate : 0.0;
    rep.check (decoded.valid && decoded.sampleRate == 44100.0 && decoded.buffer.getNumChannels() == 2, "decodifica de volta: estéreo, 44,1 kHz"_t);
    rep.check (std::abs (mp3Seconds - seconds34) < 0.1,
               "duração "_t + juce::String (mp3Seconds, 3) + " s (o WAV tem " + juce::String (seconds34, 3) + " s)");
    rep.check (std::abs (peakDb (decoded) - peakDb (mix)) < 1.5f,
               "pico do MP3 "_t + juce::String (peakDb (decoded), 1) + " dBFS, do WAV " + juce::String (peakDb (mix), 1) + " dBFS");
    rep.check (std::abs (rmsDb (decoded) - rmsDb (mix)) < 0.5f,
               "nível RMS do MP3 "_t + juce::String (rmsDb (decoded), 2) + " dB, do WAV " + juce::String (rmsDb (mix), 2) + " dB");
    const double expectedBytes = 320000.0 / 8.0 * seconds34;
    const double bytes = (double) mp3File.getSize();
    rep.check (bytes > 0.85 * expectedBytes && bytes < 1.25 * expectedBytes,
               "tamanho de 320 kbps: "_t + juce::String (bytes / 1024.0, 1) + " KB (esperado ~" + juce::String (expectedBytes / 1024.0, 1) + " KB)");

    const auto vbrFile = workFolder.getChildFile ("34_vbr.mp3");
    const auto vbrResult = app.run ("project.exportMp3", args ({ { "path", vbrFile.getFullPathName() }, { "loops", 2 },
                                                                { "what", "pattern" }, { "vbr", true } }));
    rep.check (vbrResult.ok && readWav (vbrFile).valid, "MP3 de taxa variável (VBR) exporta e decodifica"_t);

    // nome de projeto gigante: a tag ID3 tem teto e o buffer cresce se precisar (antes lia além do fim)
    app.run ("project.rename", args ({ { "name", juce::String::repeatedString ("a", 40000) } }));
    const auto longFile = workFolder.getChildFile ("34_nome_longo.mp3");
    const auto longMp3 = app.run ("project.exportMp3", args ({ { "path", longFile.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
    rep.check (longMp3.ok && readWav (longFile).valid, "nome de projeto com 40.000 letras: o MP3 sai e decodifica"_t);
    app.run ("edit.undo");   // volta o nome: os stems usam o nome do projeto
   #else
    rep.check (! mp3.ok, "sem o LAME no build, o MP3 é recusado com explicação"_t);
   #endif

    //--------------------------------------------------------------------------
    rep.section ("Exportar MIDI");
    const auto midFile = workFolder.getChildFile ("34_beat.mid");
    const auto mid = app.run ("project.exportMidi", args ({ { "path", midFile.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
    rep.check (mid.ok, "exporta MIDI" + (mid.ok ? juce::String() : ": " + mid.error));

    juce::MidiFile midi;
    {
        juce::FileInputStream in (midFile);
        rep.check (in.openedOk() && midi.readFrom (in) && midi.getTimeFormat() == ticksPerBeat, "relê o .mid (960 PPQ)"_t);
    }
    rep.check (midi.getNumTracks() == 4, "faixa de andamento + 3 canais com notas ("_t + juce::String (midi.getNumTracks()) + " faixas)");

    auto trackNamed = [&midi] (const juce::String& name) -> const juce::MidiMessageSequence*
    {
        for (int t = 0; t < midi.getNumTracks(); ++t)
            for (auto* e : *midi.getTrack (t))
                if (e->message.isTrackNameEvent() && e->message.getTextFromTextMetaEvent() == name)
                    return midi.getTrack (t);

        return nullptr;
    };

    auto noteOnTimes = [] (const juce::MidiMessageSequence* seq)
    {
        juce::Array<int> times;
        if (seq != nullptr)
            for (auto* e : *seq)
                if (e->message.isNoteOn())
                    times.add (juce::roundToInt (e->message.getTimeStamp()));
        return times;
    };

    const auto kickTimes = noteOnTimes (trackNamed ("Kick"));
    rep.check (kickTimes == juce::Array<int> { 0, 10 * ticksPerStep }, "kick: notas nos passos 1 e 11 (ticks "_t + juce::String (kickTimes[0]) + " e " + juce::String (kickTimes[1]) + ")");
    rep.check (noteOnTimes (trackNamed ("Hat")).size() == 8, "hat: 8 notas");

    double tempo = 0.0;
    for (auto* e : *midi.getTrack (0))
        if (e->message.isTempoMetaEvent())
            tempo = 60.0 / e->message.getTempoSecondsPerQuarterNote();
    rep.check (std::abs (tempo - 140.0) < 0.01, "andamento 140 BPM na primeira faixa ("_t + juce::String (tempo, 2) + ")");

    // slide do 808 para a mesma nota: o note-off da primeira não pode cair depois do note-on da segunda
    {
        addNotes (app, bass34, { noteVar (0, 1, 36, false), noteVar (0.5, 1, 36, true) });
        const auto slideFile = workFolder.getChildFile ("34_slide_mesma_nota.mid");
        app.run ("project.exportMidi", args ({ { "path", slideFile.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        juce::MidiFile slideMidi;
        juce::FileInputStream slideIn (slideFile);
        bool clean = slideIn.openedOk() && slideMidi.readFrom (slideIn);
        int notes36 = 0;

        for (int t = 0; clean && t < slideMidi.getNumTracks(); ++t)
        {
            juce::MidiMessageSequence seq (*slideMidi.getTrack (t));
            seq.updateMatchedPairs();
            double lastOn = -1.0, lastOff = -1.0;

            for (auto* e : seq)
            {
                if (e->message.isNoteOn() && e->message.getNoteNumber() == 36)
                {
                    ++notes36;
                    clean = clean && (lastOff <= e->message.getTimeStamp() || lastOn < 0.0);
                    lastOn = e->message.getTimeStamp();
                    lastOff = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() : lastOn;
                }
            }
        }

        rep.check (clean && notes36 >= 2, "slide para a mesma nota no MIDI: a nota velha termina antes da nova ("_t + juce::String (notes36) + " notas)");
        app.run ("edit.undo");
    }

    if (model.get().playlistItems.empty())
        rep.check (! app.run ("project.exportMidi", args ({ { "path", midFile.getFullPathName() }, { "what", "song" } })).ok,
                   "playlist vazia: o MIDI da música é recusado com explicação"_t);

    //--------------------------------------------------------------------------
    rep.section ("Exportar stems");
    const auto stemTracks = app.defaultStemTracks();
    rep.check (stemTracks.size() == 3, "3 trilhas recebem som (kick, hat, 808): "_t + juce::String (stemTracks.size()));

    const auto stems = app.run ("project.exportStems", args ({ { "folder", workFolder.getChildFile ("34_stems").getFullPathName() },
                                                              { "loops", 2 }, { "what", "pattern" } }));
    const auto* stemFiles = stems.value["files"].getArray();
    rep.check (stems.ok && stemFiles != nullptr && stemFiles->size() == 3,
               "um WAV por trilha" + (stems.ok ? juce::String() : ": " + stems.error));

    if (stemFiles != nullptr && mix.valid)
    {
        juce::AudioBuffer<float> sum (mix.buffer.getNumChannels(), mix.buffer.getNumSamples());
        sum.clear();
        bool sameShape = true;

        for (auto& f : *stemFiles)
        {
            const auto s = readWav (juce::File (f["path"].toString()));
            sameShape = sameShape && s.valid && s.buffer.getNumSamples() == sum.getNumSamples() && peakDb (s) > -60.0f;

            if (s.valid && s.buffer.getNumSamples() == sum.getNumSamples())
                for (int c = 0; c < sum.getNumChannels(); ++c)
                    sum.addFrom (c, 0, s.buffer, c, 0, sum.getNumSamples());
        }

        rep.check (sameShape, "cada stem tem a duração da mixagem e não é silêncio"_t);

        Audio residual;
        residual.buffer.makeCopyOf (mix.buffer);
        for (int c = 0; c < sum.getNumChannels(); ++c)
            residual.buffer.addFrom (c, 0, sum, c, 0, sum.getNumSamples(), -1.0f);
        rep.check (rmsDb (residual) - rmsDb (mix) < -40.0f,
                   "a soma dos stems dá a mixagem (resíduo "_t + juce::String (rmsDb (residual) - rmsDb (mix), 1) + " dB)");
    }

    // bus de reverb que só recebe envio do kick: o stem dele é só o envio; o do kick não leva o bus
    {
        const auto kickTrack34 = model.get().findChannel (kick34)->output;
        const auto kickAlone = workFolder.getChildFile ("34_stems").getChildFile (app.projectTitle() + " - Kick.wav");
        const auto bus = app.run ("mixer.addTrack", args ({ { "name", "Reverb" } }));
        const auto busId = bus.value["track"].toString();
        app.run ("mixer.setSend", args ({ { "track", kickTrack34 }, { "to", busId }, { "db", -12.0 } }));

        const auto busStems = app.run ("project.exportStems", args ({ { "folder", workFolder.getChildFile ("34_bus").getFullPathName() },
                                                                     { "tracks", juce::Array<juce::var> { busId, kickTrack34 } },
                                                                     { "loops", 2 }, { "what", "pattern" } }));
        const auto* busFiles = busStems.value["files"].getArray();

        const bool twoStems = busStems.ok && busFiles != nullptr && busFiles->size() == 2;
        rep.check (twoStems, "stems do bus e do kick" + (busStems.ok ? juce::String() : ": " + busStems.error));

        if (twoStems)
        {
            const auto busAudio = readWav (juce::File ((*busFiles)[0]["path"].toString()));
            const auto kickWithSend = readWav (juce::File ((*busFiles)[1]["path"].toString()));
            const auto kickBefore = readWav (kickAlone);
            const float drop = peakDb (busAudio) - peakDb (kickWithSend);
            rep.check (std::abs (drop + 12.0f) < 1.0f, "stem do bus = só o envio de -12 dB ("_t + juce::String (drop, 2) + " dB)");
            rep.check (maxDiff (kickWithSend, kickBefore) < 1.0e-5f, "stem do kick não leva o bus (igual ao de antes do envio)"_t);
        }

        app.run ("edit.undo");
        app.run ("edit.undo");
    }

    // Cadeia do master: por padrão os stems saem sem ela, e a soma dá a mixagem com o master em bypass.
    // Com masterChain, cada stem passa sozinho pelo limitador (+12 dB) do master.
    {
        const auto lim = app.run ("mixer.addInsert", args ({ { "track", masterTrackId }, { "type", "limiter" } }));
        const auto limId = lim.value["insert"].toString();
        app.run ("mixer.setInsertParam", args ({ { "track", masterTrackId }, { "insert", limId }, { "param", "gain" }, { "value", 12.0 } }));

        app.run ("mixer.setInsertBypass", args ({ { "track", masterTrackId }, { "insert", limId }, { "on", true } }));
        const auto refFile = workFolder.getChildFile ("34_mix_sem_master.wav");
        app.run ("project.exportWav", args ({ { "path", refFile.getFullPathName() }, { "loops", 2 }, { "what", "pattern" } }));
        const auto reference = readWav (refFile);
        app.run ("mixer.setInsertBypass", args ({ { "track", masterTrackId }, { "insert", limId }, { "on", false } }));

        auto exportAndSum = [&] (const char* folderName, bool chain, Audio& kickStem)
        {
            const auto r = app.run ("project.exportStems", args ({ { "folder", workFolder.getChildFile (folderName).getFullPathName() },
                                                                  { "loops", 2 }, { "what", "pattern" }, { "masterChain", chain } }));
            Audio sum;
            sum.buffer.setSize (reference.buffer.getNumChannels(), reference.buffer.getNumSamples());
            sum.buffer.clear();
            sum.sampleRate = reference.sampleRate;
            sum.valid = r.ok;

            if (auto* list = r.value["files"].getArray())
            {
                for (auto& f : *list)
                {
                    const auto s = readWav (juce::File (f["path"].toString()));
                    sum.valid = sum.valid && s.valid && s.buffer.getNumSamples() == sum.buffer.getNumSamples();

                    if (f["name"].toString() == "Kick")
                        kickStem = s;

                    if (s.valid && s.buffer.getNumSamples() == sum.buffer.getNumSamples())
                        for (int c = 0; c < sum.buffer.getNumChannels(); ++c)
                            sum.buffer.addFrom (c, 0, s.buffer, c, 0, sum.buffer.getNumSamples());
                }
            }

            return sum;
        };

        Audio kickDry, kickChain;
        auto sum = exportAndSum ("34_stems_sem_master", false, kickDry);
        Audio residual;
        residual.buffer.makeCopyOf (reference.buffer);
        for (int c = 0; c < residual.buffer.getNumChannels() && sum.valid; ++c)
            residual.buffer.addFrom (c, 0, sum.buffer, c, 0, residual.buffer.getNumSamples(), -1.0f);
        const float residualDb = rmsDb (residual) - rmsDb (reference);
        rep.check (sum.valid && residualDb < -40.0f,
                   "com um limitador no master, os stems saem sem a cadeia dele e a soma dá a mixagem sem o master (resíduo "_t
                       + juce::String (residualDb, 1) + " dB)");

        exportAndSum ("34_stems_com_master", true, kickChain);
        rep.check (kickDry.valid && kickChain.valid && peakDb (kickChain) > peakDb (kickDry) + 3.0f,
                   "com masterChain, o stem do kick passa pelo limitador do master ("_t + juce::String (peakDb (kickDry), 1) + " → "
                       + juce::String (peakDb (kickChain), 1) + " dBFS)");

        app.run ("mixer.removeInsert", args ({ { "track", masterTrackId }, { "insert", limId } }));
    }

    //--------------------------------------------------------------------------
    // Pedido do usuário (21/10): exportar travava a tela. WAV, MP3 e stems rodam numa thread de
    // trabalho; a janela acompanha pelo export.status e o Parar usa o export.cancel.
    rep.section ("Exportar em segundo plano"_t);
    {
        auto waitExport = [&app] (double maxSeconds)
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            juce::var st;

            do
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                st = app.run ("export.status").value;
            }
            while (! (bool) st["finished"] && juce::Time::getMillisecondCounterHiRes() - start < maxSeconds * 1000.0);

            return st;
        };

        const auto syncFile = workFolder.getChildFile ("fundo_sincrono.wav");
        const auto bgFile = workFolder.getChildFile ("fundo.wav");
        app.run ("project.exportWav", args ({ { "path", syncFile.getFullPathName() }, { "loops", 2 }, { "what", "pattern" } }));

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto started = app.run ("project.exportWav", args ({ { "path", bgFile.getFullPathName() }, { "loops", 2 },
                                                                   { "what", "pattern" }, { "background", true } }));
        const auto startMs = juce::Time::getMillisecondCounterHiRes() - t0;
        rep.check (started.ok && (bool) started.value["running"],
                   "o comando volta na hora com a exportação rodando ("_t + juce::String (startMs, 0) + " ms)");
        rep.check (! app.run ("project.exportWav", args ({ { "path", bgFile.getFullPathName() }, { "background", true } })).ok,
                   "uma segunda exportação ao mesmo tempo é recusada"_t);

        const auto done = waitExport (60.0);
        const auto a = readWav (syncFile), b = readWav (bgFile);
        rep.check ((bool) done["ok"] && (double) done["progress"] == 1.0 && done["files"].size() == 1,
                   "termina com progresso 100% e o arquivo na lista"_t + ((bool) done["ok"] ? juce::String() : ": " + done["message"].toString()));
        rep.check (a.valid && b.valid && a.buffer.getNumSamples() == b.buffer.getNumSamples() && maxDiff (a, b) == 0.0f,
                   "o WAV em segundo plano é igual, amostra por amostra, ao de antes"_t);

        // Parar no meio: o arquivo pela metade é apagado e nada sobra na pasta
        const auto cancelFolder = workFolder.getChildFile ("fundo_cancelado");
        const auto stemsStarted = app.run ("project.exportStems", args ({ { "folder", cancelFolder.getFullPathName() }, { "loops", 8 },
                                                                          { "what", "pattern" }, { "background", true } }));
        app.run ("export.cancel");
        const auto cancelled = waitExport (60.0);
        rep.check (stemsStarted.ok && (int) stemsStarted.value["steps"] >= 2 && (bool) cancelled["finished"] && (bool) cancelled["cancelled"]
                       && ! (bool) cancelled["ok"] && cancelFolder.findChildFiles (juce::File::findFiles, false, "*.wav").isEmpty(),
                   "Parar cancela os stems e apaga o arquivo pela metade"_t);

       #if WHYNOT_HAS_LAME
        const auto bgMp3 = workFolder.getChildFile ("fundo.mp3");
        const auto mp3Started = app.run ("project.exportMp3", args ({ { "path", bgMp3.getFullPathName() }, { "loops", 2 },
                                                                      { "what", "pattern" }, { "background", true } }));
        const auto mp3Done = waitExport (60.0);
        rep.check (mp3Started.ok && (bool) mp3Done["ok"] && readWav (bgMp3).valid, "MP3 em segundo plano sai e decodifica"_t);
       #endif

        const auto stemsBg = app.run ("project.exportStems", args ({ { "folder", workFolder.getChildFile ("fundo_stems").getFullPathName() },
                                                                     { "loops", 2 }, { "what", "pattern" }, { "background", true } }));
        const auto stemsDone = waitExport (120.0);
        rep.check (stemsBg.ok && (bool) stemsDone["ok"] && stemsDone["files"].size() == (int) stemsDone["steps"] && stemsDone["files"].size() >= 2,
                   juce::String (stemsDone["files"].size()) + " stems em segundo plano"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Salvar nova versão e autosave"_t);
    const auto versionFile = workFolder.getChildFile ("Versao").getChildFile ("Versao.wnl");
    app.run ("project.save", args ({ { "path", versionFile.getFullPathName() } }));
    const auto version2 = app.run ("project.saveNewVersion");
    rep.check (version2.ok, "salvar nova versão"_t + (version2.ok ? juce::String() : ": " + version2.error));
    rep.check (model.file.getFileName() == "Versao_2.wnl" && model.file.existsAsFile() && versionFile.existsAsFile(),
               "Ctrl+N do FL: grava Versao_2.wnl ao lado (agora: "_t + model.file.getFileName() + ")");
    app.run ("project.saveNewVersion");
    rep.check (model.file.getFileName() == "Versao_3.wnl", "de novo: Versao_3.wnl");

    rep.check (! app.run ("project.autosave").value["saved"], "sem mudanças, o autosave não grava nada"_t);
    app.run ("steps.set", args ({ { "channel", kick34 }, { "index", 4 }, { "on", true } }));
    const auto autosaved = app.run ("project.autosave");
    const juce::File autosaveFile (autosaved.value["path"].toString());
    rep.check ((bool) autosaved.value["saved"] && autosaveFile.existsAsFile() && autosaveFile.getParentDirectory().getFileName() == "autosave",
               "com mudanças, grava em autosave/: "_t + autosaveFile.getFileName());

    Project fromAutosave;
    const bool autosaveLoads = wnl::load (autosaveFile, fromAutosave).wasOk();
    bool absolutePaths = autosaveLoads;
    for (auto& c : fromAutosave.channels)
        absolutePaths = absolutePaths && (c.samplePath.isEmpty() || (juce::File::isAbsolutePath (c.samplePath) && juce::File (c.samplePath).existsAsFile()));
    rep.check (absolutePaths, "o autosave abre de qualquer pasta (samples com caminho absoluto)"_t);
    auto stepOn = [] (const Project& pr, const juce::String& ch, int index)
    {
        const auto* pattern = pr.currentPatternPtr();

        if (pattern == nullptr)
            return false;

        const auto it = pattern->steps.find (ch);
        return it != pattern->steps.end() && juce::isPositiveAndBelow (index, (int) it->second.size()) && it->second[(size_t) index].on;
    };
    rep.check (autosaveLoads && stepOn (fromAutosave, kick34, 4), "o autosave tem a última edição (passo 5 do kick)"_t);
    rep.check (model.dirty, "autosave não marca o projeto como salvo"_t);

    for (int i = 0; i < 12; ++i)   // autosaves velhos: só os 10 mais novos ficam
        autosaveFile.getSiblingFile ("Versao_3 - autosave 2000-01-01 0000" + juce::String (i).paddedLeft ('0', 2) + ".wnl").replaceWithText ("{}");
    app.run ("steps.set", args ({ { "channel", kick34 }, { "index", 6 }, { "on", true } }));
    app.run ("project.autosave");
    const auto kept = autosaveFile.getParentDirectory().findChildFiles (juce::File::findFiles, false, "Versao_3 - autosave *.wnl");
    bool newestKept = autosaveFile.existsAsFile();
    rep.check (kept.size() == AppController::autosavesToKeep && newestKept, "guarda os 10 autosaves mais novos ("_t + juce::String (kept.size()) + ")");

    //--------------------------------------------------------------------------
    rep.section ("Recuperar depois de uma queda");
    {
        const auto sessionData = workFolder.getChildFile ("sessao");
        sessionData.getChildFile ("sessoes").createDirectory();
        // PID 999999 não existe no Windows (os PIDs são múltiplos de 4): é uma sessão que caiu
        sessionData.getChildFile ("sessoes").getChildFile ("sessao-999999.txt")
            .replaceWithText (autosaveFile.getFullPathName() + "\n" + versionFile.getFullPathName() + "\n");

        rep.check (Session::processAlive (Session::currentPid()) && ! Session::processAlive (999999), "sabe se um processo está vivo pelo PID"_t);
        rep.check (Session::isWhynotProcess (Session::currentPid()) && ! Session::isWhynotProcess (4),
                   "PID de outro programa (reusado depois de reiniciar) não conta como WHYNOT aberta"_t);

        Session::Recovery recovery;
        {
            Session session (sessionData);
            recovery = session.takeRecovery();
            rep.check (recovery.autosave == autosaveFile && recovery.original == versionFile, "acha o autosave da sessão que caiu"_t);
            rep.check (! sessionData.getChildFile ("sessoes").getChildFile ("sessao-999999.txt").exists(), "a marca velha é apagada"_t);
            rep.check (sessionData.getChildFile ("sessoes").getChildFile ("sessao-" + juce::String (Session::currentPid()) + ".txt").existsAsFile(),
                       "esta sessão deixa a própria marca"_t);
        }
        rep.check (! sessionData.getChildFile ("sessoes").getChildFile ("sessao-" + juce::String (Session::currentPid()) + ".txt").exists(),
                   "fechar direito apaga a marca (nada a recuperar)"_t);

        app.run ("project.new");
        const auto recovered = app.recoverProject (recovery);
        rep.check (recovered.wasOk() && model.file == versionFile && model.dirty,
                   "abre o autosave; salvar volta para o projeto de onde ele veio"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Atalhos de teclado");
    {
        using KP = juce::KeyPress;
        auto& keys = app.getKeyMap();
        const auto ctrl = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier, alt = juce::ModifierKeys::altModifier;
        auto actionOf = [&keys] (int code, int mods) { return keys.actionFor (KP (code, juce::ModifierKeys (mods), 0)); };

        rep.check (keys.getPreset() == "fl", "preset padrão: estilo FL"_t);
        rep.check (actionOf ('n', ctrl) == "file.saveNewVersion", "FL: Ctrl+N salva nova versão"_t);
        rep.check (actionOf ('z', ctrl) == "edit.undo" && actionOf ('z', ctrl | alt) == "edit.undo"
                       && actionOf ('y', ctrl) == "edit.redo" && actionOf ('z', ctrl | shift) == "edit.redo",
                   "FL: Ctrl+Z e Ctrl+Alt+Z desfazem passo a passo, Ctrl+Y e Ctrl+Shift+Z refazem");
        rep.check (actionOf ('r', ctrl | shift) == "export.mp3" && actionOf ('m', ctrl | shift) == "export.midi", "FL: Ctrl+Shift+R MP3, Ctrl+Shift+M MIDI");
        rep.check (actionOf (KP::F8Key, alt) == "view.files" && actionOf (KP::F8Key, 0) == "view.plugins", "FL: Alt+F8 sons, F8 plugins");
        rep.check (actionOf ('t', ctrl) == "keyboard.typing" && actionOf (KP::spaceKey, ctrl) == "transport.togglePause", "FL: Ctrl+T teclado-piano, Ctrl+Espaço pausa"_t);

        rep.check (app.run ("shortcuts.setPreset", args ({ { "preset", "whynot" } })).ok && actionOf ('n', ctrl) == "file.new"
                       && actionOf ('y', ctrl) == "edit.redo" && actionOf ('z', ctrl) == "edit.undo",
                   "preset WHYNOT: Ctrl+N novo, Ctrl+Z desfaz, Ctrl+Y refaz");
        app.run ("shortcuts.setPreset", args ({ { "preset", "fl" } }));

        rep.check (app.run ("shortcuts.set", args ({ { "action", "export.stems" }, { "keys", juce::Array<juce::var> { "Ctrl+Shift+E" } } })).ok
                       && actionOf ('e', ctrl | shift) == "export.stems", "troca a tecla de uma ação (stems em Ctrl+Shift+E)"_t);
        app.run ("shortcuts.set", args ({ { "action", "file.open" }, { "keys", juce::Array<juce::var> { "Ctrl+Shift+E" } } }));
        rep.check (actionOf ('e', ctrl | shift) == "file.open" && keys.keysFor ("export.stems").isEmpty(), "a mesma tecla sai da outra ação"_t);
        rep.check (! app.run ("shortcuts.set", args ({ { "action", "nao.existe" }, { "keys", juce::Array<juce::var> { "F2" } } })).ok, "ação desconhecida é recusada"_t);

        KeyMap reread (dataFolder.getChildFile ("atalhos.json"));
        reread.load();
        rep.check (reread.actionFor (KP ('e', juce::ModifierKeys (ctrl | shift), 0)) == "file.open", "as trocas ficam gravadas em atalhos.json");
        {
            // atalhos.json corrompido (aninhamento demais): o app abre com o preset "fl"
            const auto broken = dataFolder.getChildFile ("atalhos-quebrado.json");
            broken.replaceWithText (juce::String::repeatedString ("[", 20000));
            KeyMap fallback (broken);
            fallback.load();
            rep.check (fallback.getPreset() == "fl" && fallback.actionFor (KP ('n', juce::ModifierKeys (ctrl), 0)) == "file.saveNewVersion",
                       "atalhos.json corrompido não derruba o app: volta ao preset FL"_t);
        }
        rep.check (KeyMap::parse ("Espaço"_t) == KP (KP::spaceKey) && KeyMap::describe (KP ('r', juce::ModifierKeys (ctrl | shift), 0)) == "Ctrl+Shift+R",
                   "lê e escreve teclas em português (Espaço, Ctrl+Shift+R)"_t);
        app.run ("shortcuts.setPreset", args ({ { "preset", "fl" } }));
    }

    //--------------------------------------------------------------------------
    rep.section ("Desfazer");
    {
        // Ctrl+Z volta vários passos (o que a pessoa apagou por engano volta), e o refazer anda de novo.
        const auto start = wnl::toJson (model.get());
        app.run ("steps.set", args ({ { "channel", kick34 }, { "index", 4 }, { "on", true } }));
        const auto one = wnl::toJson (model.get());
        app.run ("steps.set", args ({ { "channel", kick34 }, { "index", 6 }, { "on", true } }));
        app.run ("steps.set", args ({ { "channel", kick34 }, { "index", 4 }, { "on", false } }));
        const auto three = wnl::toJson (model.get());
        app.run ("edit.undo");
        app.run ("edit.undo");
        rep.check (wnl::toJson (model.get()) == one, "dois Ctrl+Z voltam dois passos");
        app.run ("edit.undo");
        rep.check (wnl::toJson (model.get()) == start, "o terceiro volta ao começo"_t);
        app.run ("edit.redo");
        app.run ("edit.redo");
        app.run ("edit.redo");
        rep.check (wnl::toJson (model.get()) == three, "três Ctrl+Y refazem tudo"_t);
        app.run ("edit.undo");
        app.run ("edit.undo");
        app.run ("edit.undo");

        // o alternar do FL continua existindo, para quem quiser pôr numa tecla
        const auto beforeToggle = wnl::toJson (model.get());
        app.run ("steps.set", args ({ { "channel", kick34 }, { "index", 8 }, { "on", true } }));
        const auto afterToggle = wnl::toJson (model.get());
        rep.check (app.run ("edit.undoToggle").ok && wnl::toJson (model.get()) == beforeToggle, "primeiro Ctrl+Z desfaz");
        rep.check (app.run ("edit.undoToggle").ok && wnl::toJson (model.get()) == afterToggle, "segundo Ctrl+Z refaz");
    }

    //--------------------------------------------------------------------------
    rep.section ("Placa de som, MIDI e prévia"_t);
    {
        const auto devices = app.run ("audio.devices");
        juce::StringArray types;
        if (auto* list = devices.value["types"].getArray())
            for (auto& t : *list)
                types.add (t["name"].toString());
       #if JUCE_ASIO
        rep.check (types.contains ("ASIO"), "ASIO aparece entre os drivers ("_t + types.joinIntoString (", ") + ")");
       #else
        rep.check (! types.isEmpty(), "drivers de áudio: "_t + types.joinIntoString (", ") + " (build sem o ASIO SDK)");
       #endif
        rep.check (app.run ("midi.inputs").ok, "lista as entradas MIDI");
        rep.check (! app.run ("live.note", args ({ { "key", 200 } })).ok, "nota ao vivo fora de 0-127 é recusada"_t);
        rep.check (app.run ("midi.setLiveChannel", args ({ { "channel", bass34 } })).ok && app.liveChannel() == bass34, "canal ao vivo = o 808");

        // prévia do navegador, sem placa de som: chama o callback de áudio à mão
        PreviewPlayer player;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const auto kickFile = model.resolveSample (model.get().findChannel (kick34)->samplePath);
        rep.check (player.play (kickFile, formats) && player.isPlaying(), "prévia abre o sample do kick"_t);

        juce::AudioBuffer<float> out (2, 512);
        const juce::AudioIODeviceCallbackContext context {};
        auto block = [&]
        {
            player.service();
            player.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, 512, context);
            return out.getMagnitude (0, 512);
        };

        float loudest = 0.0f;
        for (int i = 0; i < 20; ++i)
            loudest = juce::jmax (loudest, block());
        rep.check (loudest > 0.05f, "a prévia soa (pico "_t + juce::String (juce::Decibels::gainToDecibels (loudest), 1) + " dBFS)");

        player.stop();
        for (int i = 0; i < 4; ++i)
            block();
        rep.check (! player.isPlaying() && block() == 0.0f, "parar a prévia cala em menos de 50 ms"_t);

        // sem o áudio rodando, a fila de entrega enche e a última prévia fica pendente: parar zera o estado
        for (int i = 0; i < 9; ++i)
            player.play (kickFile, formats);
        player.stop();
        rep.check (! player.isPlaying(), "parar uma prévia que ainda nem chegou ao áudio não deixa o estado \"tocando\""_t);
        for (int i = 0; i < 4; ++i)
            block();
        rep.check (! player.play (workFolder.getChildFile ("nao-existe.wav"), formats), "arquivo que não abre é recusado"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Áudio e MIDI: entrada e latência da gravação"_t);
    if (auto* rec = app.recorder())
    {
        const auto before = rec->settings();

        // fora da área de trabalho, sem addToDesktop: não rouba o foco de quem usa o PC
        juce::Component stage;
        auto owned = std::make_unique<DeviceSettings> (app, [] { return false; }, [] (bool) {});
        auto& panel = *owned;
        stage.setSize (panel.getWidth(), panel.getHeight());
        stage.addAndMakeVisible (panel);

        auto* input = dynamic_cast<juce::ComboBox*> (panel.findChildWithID ("recordInput"));
        auto* adjust = dynamic_cast<juce::Slider*> (panel.findChildWithID ("recordAdjust"));
        auto* note = dynamic_cast<juce::Label*> (panel.findChildWithID ("recordNote"));
        rep.check (input != nullptr && adjust != nullptr && note != nullptr, "a janela Áudio e MIDI tem a seção Gravação (entrada, ajuste e compensação)"_t);

        if (input != nullptr && adjust != nullptr && note != nullptr)
        {
            rep.check (input->getNumItems() >= 3 && input->getItemText (0).startsWith ("Entrada 1"),
                       "a lista tem ao menos a entrada 1, a 2 e o par 1 e 2 ("_t + juce::String (input->getNumItems()) + " itens)");

            input->setSelectedId (2, juce::sendNotificationSync);
            rep.check (rec->settings().firstInput == 0 && rec->settings().stereo, "\"Entradas 1 e 2\" grava em estéreo pelas entradas 1 e 2"_t);
            input->setSelectedId (3, juce::sendNotificationSync);
            rep.check (rec->settings().firstInput == 1 && ! rec->settings().stereo, "\"Entrada 2\" grava em mono pela entrada 2"_t);

            adjust->setValue (12.5, juce::sendNotificationSync);
            rep.check (std::abs (rec->settings().adjustMs - 12.5) < 1e-9, "o ajuste de 12,5 ms chega ao gravador"_t);
            rep.check (adjust->getTextFromValue (12.5) == "12,5 ms" && std::abs (adjust->getValueFromText ("-3,5 ms") + 3.5) < 1e-9,
                       "o campo mostra \"12,5 ms\" e lê \"-3,5 ms\" com vírgula"_t);
            rep.check (note->getText().startsWith ("O take de áudio recua 12,5 ms"_t) && note->getText().contains ("ajuste 12,5"),
                       "a compensação mostra o ajuste: "_t + note->getText().upToFirstOccurrenceOf ("\n", false, false));
            adjust->setValue (900.0, juce::sendNotificationSync);
            rep.check (rec->settings().adjustMs == 500.0, "o ajuste para em 500 ms"_t);
            adjust->setValue (0.0, juce::sendNotificationSync);

            const auto image = juce::SoftwareImageType().convert (stage.createComponentSnapshot (stage.getLocalBounds(), true, 1.0f));
            const auto png = workFolder.getChildFile ("ajustes-audio-midi.png");
            png.deleteFile();
            juce::FileOutputStream out (png);
            rep.check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), "captura gravada em "_t + png.getFullPathName());
        }

        stage.removeChildComponent (&panel);
        const int fullHeight = panel.getHeight();
        auto scroller = DeviceSettings::fitToHeight (std::move (owned), 400);
        auto* view = dynamic_cast<juce::Viewport*> (scroller.get());
        rep.check (view != nullptr && scroller->getHeight() == 400 && view->getViewedComponent() != nullptr
                       && view->getViewedComponent()->getHeight() == fullHeight && view->canScrollVertically(),
                   "numa tela baixa a janela fica com 400 px e rola (o conteúdo tem "_t + juce::String (fullHeight) + " px)");
        scroller.reset();

        rec->setInput (before.firstInput, before.stereo);
        rec->setAdjustMs (before.adjustMs);
    }
}

} // namespace whynot::selftest
