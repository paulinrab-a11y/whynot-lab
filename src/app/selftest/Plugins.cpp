// Autoteste da entrega 3.3: scan isolado, instrumento VST3 no rack, estado no projeto, plugin ausente e queda, VST3 num insert.
// Usa os plugins de build/test_plugins (alvo whynot_test_plugins).

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"

namespace whynot::selftest
{

void runPluginsSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto& workFolder = ctx.work;
    juce::ignoreUnused (rep, app, model, workFolder);
    const auto& dataFolder = ctx.data;
    const double beat = beat140;   // um tempo a 140 BPM, o andamento do projeto novo
    auto addNotes = [&] (const juce::String& ch, const juce::Array<juce::var>& notes) { return selftest::addNotes (app, ch, notes); };

    //==========================================================================
    // Entrega 3.3
    rep.section ("Plugins VST3: scan isolado");
    {
        // build/whynot_lab_artefacts/Release/WHYNOT LAB.exe -> build/test_plugins
        const auto testPlugins = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                     .getParentDirectory().getParentDirectory().getParentDirectory().getChildFile ("test_plugins");

        if (! testPlugins.isDirectory())
        {
            rep.check (false, "plugins de teste em " + testPlugins.getFullPathName() + " (compile o alvo whynot_test_plugins)");
        }
        else
        {
            app.run ("plugins.setFolders", args ({ { "folders", juce::Array<juce::var> { testPlugins.getFullPathName() } } }));

            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto scan = app.run ("plugins.scan", args ({ { "wait", true }, { "timeoutSeconds", 6 } }));
            const double firstScan = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            rep.check (scan.ok && (int) scan.value["candidates"] == 4 && (int) scan.value["scanned"] == 4,
                       "scan abre os 4 arquivos de teste, cada um num processo separado ("_t + juce::String (firstScan, 1) + " s)");

            juce::String synthId, gainId;
            bool synthIsInstrument = false, gainIsEffect = false;

            const auto listed = app.run ("plugins.list");   // guardar: o ponteiro abaixo aponta para dentro dele

            if (auto* list = listed.value["plugins"].getArray())
            {
                for (auto& d : *list)
                {
                    if (d["name"].toString() == "WHYNOT Teste Synth") { synthId = d["id"]; synthIsInstrument = d["kind"].toString() == "instrument"; }
                    if (d["name"].toString() == "WHYNOT Teste Ganho") { gainId = d["id"]; gainIsEffect = d["kind"].toString() == "effect"; }
                }
            }

            rep.check (synthId.isNotEmpty() && synthIsInstrument, "WHYNOT Teste Synth entra como instrumento");
            rep.check (gainId.isNotEmpty() && gainIsEffect, "WHYNOT Teste Ganho entra como efeito");

            juce::String crashReason, hangReason;

            for (auto& entry : app.blockedPlugins())
            {
                if (entry["file"].toString().contains ("WHYNOT Teste Crash")) crashReason = entry["reason"];
                if (entry["file"].toString().contains ("WHYNOT Teste Trava")) hangReason = entry["reason"];
            }

            rep.check (crashReason.isNotEmpty(), "plugin que quebra ao ser lido fica bloqueado: "_t + crashReason);
            rep.check (hangReason.contains ("não respondeu"_t), "plugin que trava fica bloqueado por tempo: "_t + hangReason);

            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            const auto again = app.run ("plugins.scan", args ({ { "wait", true }, { "timeoutSeconds", 6 } }));
            const double secondScan = (juce::Time::getMillisecondCounterHiRes() - t1) / 1000.0;
            rep.check (again.ok && (int) again.value["scanned"] == 0 && (int) again.value["reused"] == 2,
                       "segundo scan vem do cache: "_t + again.value["scanned"].toString() + " abertos de novo, "
                           + again.value["reused"].toString() + " do cache, bloqueados pulados (" + juce::String (secondScan, 2) + " s)");

            //------------------------------------------------------------------
            rep.section ("Plugins VST3: instrumento no channel rack");
            app.run ("project.new");
            rep.check (! app.run ("channel.addPlugin", args ({ { "plugin", "WHYNOT Teste Ganho" } })).ok, "efeito não entra como canal do rack"_t);

            const auto addedSynth = app.run ("channel.addPlugin", args ({ { "plugin", synthId } }));
            const auto synthCh = addedSynth.value["channel"].toString();
            const auto synthMixer = addedSynth.value["mixerTrack"].toString();
            rep.check (addedSynth.ok && app.channelPlugin (synthCh) != nullptr && app.getProjector().sampleProblems()[synthCh].isEmpty(),
                       "o synth abre no canal novo"_t + (addedSynth.ok ? juce::String() : ": " + addedSynth.error));

            addNotes (synthCh, { noteVar (0, 1, 69, false), noteVar (2, 1, 76, false) });
            app.run ("mixer.setSolo", args ({ { "track", synthMixer }, { "on", true } }));

            const auto wavSynth = workFolder.getChildFile ("synth.wav");
            app.run ("project.exportWav", args ({ { "path", wavSynth.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            const auto synth = readWav (wavSynth);
            const double hzA = measureHz (synth, 0.05, beat - 0.05), hzE = measureHz (synth, 2 * beat + 0.05, 3 * beat - 0.05);
            rep.check (std::abs (hzA - 440.0) < 2.0, "o synth toca o A4 do piano roll: "_t + juce::String (hzA, 1) + " Hz (440,0 Hz)");
            rep.check (std::abs (hzE - 659.26) < 3.0, "e o E5: "_t + juce::String (hzE, 1) + " Hz (659,3 Hz)");

            const auto setParam = app.run ("plugin.setParameter", args ({ { "channel", synthCh }, { "parameter", "Volume" }, { "value", 0.25 } }));
            rep.check (setParam.ok && setParam.value["text"].toString().contains ("-18"),
                       "parâmetro Volume do plugin em -18 dB ("_t + (setParam.ok ? setParam.value["text"].toString() : setParam.error) + ")");

            const auto wavQuiet = workFolder.getChildFile ("synth_baixo.wav");
            app.run ("project.exportWav", args ({ { "path", wavQuiet.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            const auto quiet = readWav (wavQuiet);
            const float drop = peakDb (quiet) - peakDb (synth);
            rep.check (std::abs (drop + 12.0f) < 0.5f, "o export sai 12 dB mais baixo: "_t + juce::String (drop, 2) + " dB");

            //------------------------------------------------------------------
            rep.section ("Plugins VST3: estado salvo no projeto");
            const auto pluginProject = workFolder.getChildFile ("Plugin Teste").getChildFile ("Plugin Teste.wnl");
            rep.check (app.run ("project.save", args ({ { "path", pluginProject.getFullPathName() } })).ok, "salva o projeto com o plugin");
            const auto savedState = model.get().findChannel (synthCh) != nullptr ? model.get().findChannel (synthCh)->plugin.state : juce::String();
            rep.check (savedState.length() > 20, "estado do plugin gravado no .wnl ("_t + juce::String (savedState.length()) + " caracteres)");

            app.run ("project.new");
            rep.check (app.run ("project.open", args ({ { "path", pluginProject.getFullPathName() } })).ok, "reabre o projeto");
            const auto wavReopen = workFolder.getChildFile ("synth_reaberto.wav");
            app.run ("project.exportWav", args ({ { "path", wavReopen.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            const float reopenDiff = maxDiff (quiet, readWav (wavReopen));
            rep.check (reopenDiff < 1.0e-4f, "reaberto soa igual, com o volume do plugin que foi salvo (diferença máx. "_t
                                                 + juce::String (reopenDiff, 6) + ")");

            //------------------------------------------------------------------
            rep.section ("Plugins VST3: plugin ausente e recuperação"_t);
            {
                // outro computador, sem o plugin: o projeto abre e o canal diz o que falta
                auto text = pluginProject.loadFileAsString().replace ("WHYNOT Teste Synth", "Plugin Que Nao Existe");
                const auto synthDesc = app.getPluginCatalog().find (synthId);
                const auto uidField = "\"uid\": \"" + juce::String::toHexString (synthDesc ? synthDesc->uniqueId : 0) + "\"";
                text = text.replace (uidField, "\"uid\": \"1234abcd\"");
                const auto missingFile = workFolder.getChildFile ("Plugin Teste").getChildFile ("Sem Plugin.wnl");
                missingFile.replaceWithText (text);

                const auto opened = app.run ("project.open", args ({ { "path", missingFile.getFullPathName() } }));
                juce::String problem;

                for (auto& c : model.get().channels)
                    if (c.isPlugin())
                        problem = app.getProjector().sampleProblems()[c.id];

                rep.check (opened.ok && problem.contains ("não encontrado"_t), "projeto com plugin que falta abre e avisa: "_t + problem);
            }

            {
                // o app caiu enquanto abria o plugin: na próxima abertura ele vem bloqueado
                const auto marker = dataFolder.getChildFile ("carregando-plugin.txt");
                const auto gainDesc = app.getPluginCatalog().find (gainId);
                const auto gainFile = gainDesc ? gainDesc->fileOrIdentifier : juce::String ("(ganho ausente)");
                marker.replaceWithText (gainFile + "\nWHYNOT Teste Ganho");

                juce::String warning;
                bool blocked = false;

                {
                    AppController restarted (true, dataFolder);
                    warning = restarted.takeStartupWarning();
                    blocked = restarted.getPluginCatalog().isBlocked (gainFile);
                }

                rep.check (blocked && ! marker.existsAsFile(), "plugin que derrubou o app fica bloqueado na abertura seguinte");
                rep.check (warning.contains ("WHYNOT Teste Ganho"), "e o app avisa: " + warning);

                app.getPluginCatalog().load();   // o catálogo em disco agora tem o bloqueio
                rep.check (app.unblockPlugin (gainFile), "desbloquear devolve o plugin");
                app.run ("plugins.scan", args ({ { "wait", true }, { "timeoutSeconds", 6 } }));
                rep.check (app.getPluginCatalog().find (gainId).has_value(), "e um scan novo traz ele de volta");
            }

            //------------------------------------------------------------------
            rep.section ("Plugins VST3: efeito num insert do mixer");
            {
                app.run ("project.new");
                const auto ch = app.run ("channel.addPlugin", args ({ { "plugin", synthId } }));
                const auto chId = ch.value["channel"].toString(), mx = ch.value["mixerTrack"].toString();
                addNotes (chId, { noteVar (0, 1, 69, false), noteVar (2, 1, 76, false) });
                app.run ("mixer.setSolo", args ({ { "track", mx }, { "on", true } }));

                auto render = [&] (const juce::String& name)
                {
                    const auto f = workFolder.getChildFile (name);
                    app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
                    return readWav (f);
                };

                const auto dry = render ("insert_seco.wav");
                rep.check (! app.run ("mixer.addPluginInsert", args ({ { "track", mx }, { "plugin", synthId } })).ok,
                           "instrumento não entra num insert"_t);

                const auto addedInsert = app.run ("mixer.addPluginInsert", args ({ { "track", mx }, { "plugin", gainId } }));
                const auto insertId = addedInsert.value["insert"].toString();
                rep.check (addedInsert.ok && app.insertPluginInstance (mx, insertId) != nullptr
                               && app.getProjector().insertProblems()[insertId].isEmpty(),
                           "WHYNOT Teste Ganho abre no insert da trilha do synth"_t
                               + (addedInsert.ok ? " " + app.getProjector().insertProblems()[insertId] : ": " + addedInsert.error));

                const auto setGain = app.run ("plugin.setParameter", args ({ { "track", mx }, { "insert", insertId },
                                                                             { "parameter", "Ganho" }, { "value", 12.0 / 36.0 } }));
                rep.check (setGain.ok && setGain.value["text"].toString().contains ("-12"),
                           "ganho do insert em -12 dB ("_t + (setGain.ok ? setGain.value["text"].toString() : setGain.error) + ")");

                const auto wet = render ("insert_ganho.wav");
                const float insertDrop = peakDb (wet) - peakDb (dry);
                rep.check (std::abs (insertDrop + 12.0f) < 0.5f, "com o insert, o export sai 12 dB mais baixo: "_t + juce::String (insertDrop, 2) + " dB");

                // O ganho foi mexido no plugin vivo (como na janela dele), sem salvar. Remover e dar Ctrl+Z trazia o
                // plugin com o estado do último salvar: o timbre de agora sumia (revisão profissional, 22/10).
                app.run ("mixer.removeInsert", args ({ { "track", mx }, { "insert", insertId } }));
                app.run ("edit.undo");
                const float undoDrop = peakDb (render ("insert_desfeito.wav")) - peakDb (dry);
                rep.check (app.insertPluginInstance (mx, insertId) != nullptr && std::abs (undoDrop + 12.0f) < 0.5f,
                           "remover o insert e dar Ctrl+Z volta o plugin com o ajuste de agora (-12 dB), e não o do último salvar: "_t
                               + juce::String (undoDrop, 2) + " dB");

                app.run ("mixer.setInsertBypass", args ({ { "track", mx }, { "insert", insertId }, { "on", true } }));
                const float bypassDrop = peakDb (render ("insert_bypass.wav")) - peakDb (dry);
                rep.check (std::abs (bypassDrop) < 0.1f, "em bypass, volta ao som seco: "_t + juce::String (bypassDrop, 2) + " dB");
                app.run ("mixer.setInsertBypass", args ({ { "track", mx }, { "insert", insertId }, { "on", false } }));

                const auto insertProject = workFolder.getChildFile ("Insert Teste").getChildFile ("Insert Teste.wnl");
                app.run ("project.save", args ({ { "path", insertProject.getFullPathName() } }));
                const auto* saved = model.get().findMixer (mx) != nullptr ? model.get().findMixer (mx)->findInsert (insertId) : nullptr;
                rep.check (saved != nullptr && saved->kind == "vst3" && saved->plugin.state.length() > 20,
                           "insert VST3 salvo com o estado ("_t + juce::String (saved != nullptr ? saved->plugin.state.length() : 0) + " caracteres)");

                app.run ("project.new");
                app.run ("project.open", args ({ { "path", insertProject.getFullPathName() } }));
                const float reopenedDrop = peakDb (render ("insert_reaberto.wav")) - peakDb (dry);
                rep.check (std::abs (reopenedDrop + 12.0f) < 0.5f, "reaberto, o insert continua em -12 dB: "_t + juce::String (reopenedDrop, 2) + " dB");
            }
        }
    }
}

} // namespace whynot::selftest
