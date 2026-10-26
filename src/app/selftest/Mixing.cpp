// Autoteste da mixagem (entrega 3.3): efeitos nativos, estilo trap, salvar e reabrir. Dono: Mixagem.
// Começa do projeto de demonstração que a 3.2 salvou (ctx.demoProject).

#include "SelfTestKit.h"
#include "../../dsp/LoudnessMeter.h"
#include "../../model/WnlFormat.h"
#include "../../engine/UserMix.h"
#include "../../ui/fx/FxEditor.h"
#include "../../ui/fx/FxWidgets.h"
#include "../../ui/fx/InsertPanel.h"

#include <string_view>

namespace whynot::selftest
{

void runMixingSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto& workFolder = ctx.work;
    juce::ignoreUnused (rep, app, model, workFolder);

    if (ctx.demoProject.existsAsFile())
        app.run ("project.open", args ({ { "path", ctx.demoProject.getFullPathName() } }));

    {
        //======================================================================
        // Entrega 3.3: efeitos nativos e estilos de mixagem, em cima da música de demonstração.
        rep.section ("Mixagem: efeitos nativos");
        const auto listed = app.run ("fx.list");
        rep.check (listed.ok && listed.value["effects"].size() == 30 && listed.value["styles"].size() == 4,
                   "fx.list descreve 30 efeitos e 4 estilos ("_t + juce::String (listed.value["effects"].size()) + " / "
                       + juce::String (listed.value["styles"].size()) + ")");

        auto trackOf = [&model] (const juce::String& name)
        {
            for (auto& m : model.get().mixer)
                if (m.name == name)
                    return m.id;
            return juce::String();
        };

        const auto kickMx = trackOf ("Kick"), bassMx = trackOf ("808"), clapMx = trackOf ("Clap");
        const auto addedEq = app.run ("mixer.addInsert", args ({ { "track", kickMx }, { "type", "eq" }, { "preset", "kick-trap" } }));
        const auto eqId = addedEq.value["insert"].toString();
        rep.check (addedEq.ok && model.get().findMixer (kickMx)->inserts.size() == 1, "EQ com preset \"Kick trap\" no kick"_t);
        rep.check (app.getProjector().fxFor (kickMx, eqId) != nullptr, "o projetor criou o efeito no Tracktion"_t);
        rep.check (app.run ("mixer.setInsertParam", args ({ { "track", kickMx }, { "insert", eqId }, { "param", "hpSlope" }, { "value", "48 dB/oit" } })).ok
                       && model.get().findMixer (kickMx)->findInsert (eqId)->params.at ("hpSlope") == 2.0f,
                   "lista aceita o rótulo (\"48 dB/oit\")"_t);
        rep.check (! app.run ("mixer.setInsertParam", args ({ { "track", kickMx }, { "insert", eqId }, { "param", "naoExiste" }, { "value", 1 } })).ok,
                   "parâmetro inexistente é recusado"_t);

        if (auto* fx = app.getProjector().fxFor (kickMx, eqId))
            rep.check (fx->getHost().getParam (fx->getInfo().indexOf ("b3Gain")) == 2.5f, "o valor do modelo chega ao efeito vivo (realce de 2,5 dB)"_t);

        const auto wavBefore = workFolder.getChildFile ("antes_do_estilo.wav");
        app.run ("mixer.removeInsert", args ({ { "track", kickMx }, { "insert", eqId } }));
        app.run ("project.exportWav", args ({ { "path", wavBefore.getFullPathName() } }));
        const auto rawMix = readWav (wavBefore);

        auto lufs = [] (const Audio& a)
        {
            dsp::LoudnessMeter meter;
            meter.prepare (a.sampleRate);
            meter.process (a.buffer.getReadPointer (0), a.buffer.getReadPointer (a.buffer.getNumChannels() > 1 ? 1 : 0), a.buffer.getNumSamples());
            return std::pair<float, float> (meter.integrated(), meter.truePeak());
        };

        // Efeito numa trilha (em solo) e no master entram no render: -12 dB de ganho = 12 dB a menos.
        auto exportWithGain = [&] (const juce::String& target, bool solo)
        {
            if (solo)
                app.run ("mixer.setSolo", args ({ { "track", target }, { "on", true } }));

            const auto wavRef = workFolder.getChildFile ("ganho_ref.wav");
            app.run ("project.exportWav", args ({ { "path", wavRef.getFullPathName() } }));
            const auto quiet = app.run ("mixer.addInsert", args ({ { "track", target }, { "type", "stereo" } }));
            app.run ("mixer.setInsertParam", args ({ { "track", target }, { "insert", quiet.value["insert"] }, { "param", "gain" }, { "value", -12.0 } }));
            const auto wavQuiet = workFolder.getChildFile ("ganho_menos_12.wav");
            app.run ("project.exportWav", args ({ { "path", wavQuiet.getFullPathName() } }));
            app.run ("mixer.removeInsert", args ({ { "track", target }, { "insert", quiet.value["insert"] } }));

            if (solo)
                app.run ("mixer.setSolo", args ({ { "track", target }, { "on", false } }));

            return peakDb (readWav (wavQuiet)) - peakDb (readWav (wavRef));
        };

        const float kickDrop = exportWithGain (kickMx, true), masterDrop = exportWithGain (masterTrackId, false);
        rep.check (std::abs (kickDrop + 12.0f) < 0.2f, "efeito na trilha do kick entra no export ("_t + juce::String (kickDrop, 2) + " dB)");
        rep.check (std::abs (masterDrop + 12.0f) < 0.2f, "efeito no master entra no export ("_t + juce::String (masterDrop, 2) + " dB)");

        {
            // Limitador sozinho no master: empurrado 12 dB, não passa do teto.
            const auto lim = app.run ("mixer.addInsert", args ({ { "track", masterTrackId }, { "type", "limiter" } }));
            app.run ("mixer.setInsertParam", args ({ { "track", masterTrackId }, { "insert", lim.value["insert"] }, { "param", "gain" }, { "value", 12.0 } }));
            const auto wavLim = workFolder.getChildFile ("limitador.wav");
            app.run ("project.exportWav", args ({ { "path", wavLim.getFullPathName() } }));
            app.run ("mixer.removeInsert", args ({ { "track", masterTrackId }, { "insert", lim.value["insert"] } }));
            rep.check (peakDb (readWav (wavLim)) <= -0.95f, "limitador sozinho no master segura o teto ("_t + juce::String (peakDb (readWav (wavLim)), 2) + " dBFS)");
        }

        //----------------------------------------------------------------------
        rep.section ("Mixagem: estilo trap");
        const auto beforeJson = wnl::toJson (model.get());
        const auto styled = app.run ("mixer.applyStyle", args ({ { "style", "trap" } }));
        const auto& ps = model.get();
        rep.check (styled.ok, "aplica o estilo trap"_t + (styled.ok ? juce::String() : ": " + styled.error));

        bool everyTrackHasChain = true;

        for (auto& m : ps.mixer)
            everyTrackHasChain &= ! m.inserts.empty();

        rep.check (everyTrackHasChain, "toda trilha (e o master) ganhou uma cadeia");

        const auto reverbMx = trackOf ("Reverb"), delayMx = trackOf ("Delay");
        rep.check (reverbMx.isNotEmpty() && delayMx.isNotEmpty(), "buses de reverb e delay criados");
        rep.check (ps.findMixer (clapMx)->findSend (reverbMx) != nullptr, "clap manda para o reverb");
        rep.check (ps.findMixer (trackOf ("Bell"))->findSend (delayMx) != nullptr, "melodia manda para o delay");

        juce::String sidechainSource;

        for (auto& ins : ps.findMixer (bassMx)->inserts)
            if (ins.sidechain.isNotEmpty())
                sidechainSource = ins.sidechain;

        rep.check (sidechainSource == kickMx, "o compressor do 808 é disparado pelo kick (sidechain)"_t);
        rep.check (ps.findMixer (masterTrackId)->inserts.back().type == "limiter", "o master termina no limitador");
        rep.check (app.getProjector().insertProblems().size() == 0, "todos os efeitos do estilo abriram no motor");

        const auto styledBass = ps.findMixer (bassMx)->inserts;
        rep.check (! app.run ("mixer.setInsertSidechain", args ({ { "track", kickMx }, { "insert", ps.findMixer (kickMx)->inserts.back().id },
                                                                  { "source", bassMx } })).ok,
                   "sidechain que fecharia um laço (808 dispara o kick que dispara o 808) é recusado"_t);
        rep.check (! app.run ("mixer.setSend", args ({ { "track", reverbMx }, { "to", clapMx } })).ok, "envio de volta (reverb → clap) é recusado"_t);

        const auto wavTrap = workFolder.getChildFile ("trap.wav");
        app.run ("project.exportWav", args ({ { "path", wavTrap.getFullPathName() } }));
        const auto trap = readWav (wavTrap);
        const auto [lufsBefore, tpBefore] = lufs (rawMix);
        const auto [lufsTrap, tpTrap] = lufs (trap);
        rep.check (trap.valid && peakDb (trap) <= -0.95f, "master limitado: pico de amostra "_t + juce::String (peakDb (trap), 2) + " dBFS (teto -1)");
        rep.check (tpTrap < -0.5f, "pico real "_t + juce::String (tpTrap, 2) + " dBTP");
        rep.check (std::abs (lufsTrap + 7.0f) < 2.0f && lufsTrap > lufsBefore + 2.0f,
                   "trap perto do alvo de -7 LUFS: "_t + juce::String (lufsBefore, 1) + " → "_t + juce::String (lufsTrap, 1) + " LUFS integrados");

        //----------------------------------------------------------------------
        rep.section ("Mixagem: salvar, reabrir e desfazer");
        const auto mixFile = workFolder.getChildFile ("Demo 3.3").getChildFile ("Demo 3.3.wnl");
        rep.check (app.run ("project.save", args ({ { "path", mixFile.getFullPathName() } })).ok, "salva o projeto mixado");
        const auto savedText = mixFile.loadFileAsString();
        rep.check (savedText.contains ("\"schemaVersion\": " + juce::String (wnl::currentSchemaVersion)) && savedText.contains ("\"inserts\"")
                       && savedText.contains ("\"sidechain\""),
                   ".wnl no esquema atual, com inserts e sidechain");

        const auto mixedJson = wnl::toJson (model.get());
        app.run ("project.new");
        rep.check (app.run ("project.open", args ({ { "path", mixFile.getFullPathName() } })).ok && wnl::toJson (model.get()) == mixedJson,
                   "reabre com os mesmos efeitos, envios e papéis"_t);

        const auto wavTrap2 = workFolder.getChildFile ("trap_reaberto.wav");
        app.run ("project.exportWav", args ({ { "path", wavTrap2.getFullPathName() } }));
        rep.check (maxDiff (trap, readWav (wavTrap2)) < 1.0e-5f, "reaberto soa igual (diferença máx. "_t
                                                                    + juce::String (maxDiff (trap, readWav (wavTrap2)), 8) + ")");

        rep.check (app.run ("mixer.applyStyle", args ({ { "style", "funk" } })).ok, "troca para o estilo funk");
        rep.check (app.run ("edit.undo").ok && wnl::toJson (model.get()) == mixedJson, "desfazer volta ao trap inteiro, num passo só"_t);

        const auto wavFunk = workFolder.getChildFile ("funk.wav");
        app.run ("edit.redo");
        app.run ("project.exportWav", args ({ { "path", wavFunk.getFullPathName() } }));
        const auto [lufsFunk, tpFunk] = lufs (readWav (wavFunk));
        rep.check (lufsFunk > lufsTrap && std::abs (lufsFunk + 5.5f) < 2.0f,
                   "funk sai mais alto que trap, perto do alvo de -5,5 ("_t + juce::String (lufsFunk, 1) + " x " + juce::String (lufsTrap, 1) + " LUFS)");
        juce::ignoreUnused (tpFunk, tpBefore, styledBass, beforeJson);

        // Deixa o projeto de demonstração com o trap (para as capturas da documentação).
        app.run ("edit.undo");
        app.run ("project.save", args ({ { "path", mixFile.getFullPathName() } }));
    }

    //==========================================================================
    // Comparação A/B: "sem efeitos" contra "como está", no mesmo LUFS (o volume não engana).
    rep.section ("Mixagem: comparar sem efeitos no mesmo volume"_t);
    {
        const auto demo33 = workFolder.getChildFile ("Demo 3.3").getChildFile ("Demo 3.3.wnl");

        if (demo33.existsAsFile())
            app.run ("project.open", args ({ { "path", demo33.getFullPathName() } }));

        const bool song = model.get().songMode;
        const auto cmp = app.run ("mixer.compare", args ({ { "mode", "matched" } }));
        const float without = (float) (double) cmp.value["lufsWithout"];
        const float with = (float) (double) cmp.value["lufsWith"];
        const float trim = (float) (double) cmp.value["trimDb"];
        rep.check (cmp.ok && trim < -0.5f && app.isAuditioning(),
                   "estilo trap: "_t + juce::String (with, 1) + " LUFS com efeitos, " + juce::String (without, 1)
                       + " sem; a comparação abaixa "_t + juce::String (-trim, 1) + " dB" + (cmp.ok ? juce::String() : ": " + cmp.error));

        const float heard = app.renderLoudness (model.get(), song, false, trim);
        rep.check (std::abs (heard - without) < 0.5f,
                   "como está, no volume igualado: "_t + juce::String (heard, 1) + " LUFS (sem efeitos: " + juce::String (without, 1) + ")");

        const float again = app.renderLoudness (model.get(), song, true, 0.0f);
        rep.check (std::abs (again - without) < 0.05f, "lado sem efeitos mede igual de novo ("_t + juce::String (again, 2) + " LUFS)");

        juce::String trackId;
        float volume = 0.0f;

        for (auto& m : model.get().mixer)
            if (! m.isMaster())
            {
                trackId = m.id;
                volume = m.volumeDb;
                break;
            }

        app.run ("mixer.setVolume", args ({ { "track", trackId }, { "db", volume - 1.0f } }));
        rep.check (! app.isAuditioning(), "mexer no mixer desliga a comparação"_t);
        app.run ("edit.undo");

        rep.check (app.run ("mixer.compare", args ({ { "mode", "bypass" } })).ok && app.isAuditioning(), "modo sem efeitos liga"_t);
        app.run ("project.save", args ({ { "path", demo33.getFullPathName() } }));
        rep.check (! app.isAuditioning(), "salvar desliga a comparação"_t);
        rep.check (! app.run ("mixer.compare", args ({ { "mode", "xyz" } })).ok, "modo desconhecido é recusado"_t);
    }

    //==========================================================================
    // MP3 com folga para os picos entre amostras: o render vai a -1 dBTP antes do LAME.
    rep.section ("Mixagem: MP3 a -1 dBTP"_t);
   #if WHYNOT_HAS_LAME
    if (ctx.demoProject.existsAsFile())
    {
        app.run ("project.open", args ({ { "path", ctx.demoProject.getFullPathName() } }));
        app.run ("mixer.setVolume", args ({ { "track", masterTrackId }, { "db", 6.0 } }));   // empurra o beat acima do teto

        const auto hotWav = workFolder.getChildFile ("mp3_quente.wav");
        app.run ("project.exportWav", args ({ { "path", hotWav.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        const auto hotMp3 = workFolder.getChildFile ("mp3_quente.mp3");
        const auto exported = app.run ("project.exportMp3", args ({ { "path", hotMp3.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));

        auto truePeakOf = [] (const Audio& a)
        {
            dsp::LoudnessMeter m;
            m.prepare (a.sampleRate);
            m.process (a.buffer.getReadPointer (0), a.buffer.getReadPointer (a.buffer.getNumChannels() > 1 ? 1 : 0), a.buffer.getNumSamples());
            return m.truePeak();
        };

        const auto wavPeak = peakDb (readWav (hotWav));
        const auto mp3 = readWav (hotMp3);
        const float mp3Tp = mp3.valid ? truePeakOf (mp3) : 100.0f;
        rep.check (exported.ok && wavPeak > -0.1f && mp3Tp <= -0.5f,
                   "beat com o master +6 dB: o WAV encosta em "_t + juce::String (wavPeak, 1) + " dBFS; o MP3 decodificado fica em "_t
                       + juce::String (mp3Tp, 2) + " dBTP");
        app.run ("project.new");
    }
   #else
    rep.check (true, "sem LAME neste build: MP3 não testado"_t);
   #endif

    //==========================================================================
    // Afinação de voz: o tom vem do projeto, o preset não troca o tom e a latência
    // (15 ms) é compensada: o ataque continua no mesmo lugar do render.
    rep.section ("Mixagem: afinação de voz"_t);
    {
        app.run ("project.new");
        app.run ("project.setKey", args ({ { "root", "A" }, { "scale", "minor" } }));

        const auto bell = channelId (model.get(), "Bell");
        const auto bellMx = model.get().findChannel (bell) != nullptr ? model.get().findChannel (bell)->output : juce::String();
        app.run ("pianoroll.clear", args ({ { "channel", bell } }));
        juce::Array<juce::var> notes;
        notes.add (noteVar (1.0, 1.0, 69, false));
        addNotes (app, bell, notes);

        auto onset = [] (const Audio& a)
        {
            for (int i = 0; i < a.buffer.getNumSamples(); ++i)
                if (std::abs (a.buffer.getSample (0, i)) > 0.01f)
                    return i / a.sampleRate;

            return -1.0;
        };

        // Só o Bell toca (os outros canais em mudo), para medir o ataque dele.
        for (auto& ch : model.get().channels)
            if (ch.id != bell)
                app.run ("channel.setMute", args ({ { "channel", ch.id }, { "on", true } }));

        const auto wavDry = workFolder.getChildFile ("afinacao_sem.wav");
        app.run ("project.exportWav", args ({ { "path", wavDry.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        const auto dry = readWav (wavDry);

        const auto added = app.run ("mixer.addInsert", args ({ { "track", bellMx }, { "type", "tune" }, { "preset", "trap-funk" } }));
        const auto* ins = model.get().findMixer (bellMx) != nullptr && ! model.get().findMixer (bellMx)->inserts.empty()
                              ? &model.get().findMixer (bellMx)->inserts.back() : nullptr;
        rep.check (added.ok && ins != nullptr && ins->type == "tune" && ins->params.at ("key") == 9.0f && ins->params.at ("scale") == 0.0f,
                   "afinação entra no Bell com o tom do projeto (Lá menor)"_t);

        const auto wavTuned = workFolder.getChildFile ("afinacao_com.wav");
        app.run ("project.exportWav", args ({ { "path", wavTuned.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        const auto tuned = readWav (wavTuned);
        const double shiftMs = 1000.0 * (onset (tuned) - onset (dry));
        rep.check (dry.valid && tuned.valid && onset (dry) > 0.0 && std::abs (shiftMs) < 1.0,
                   "latência compensada: o ataque muda "_t + juce::String (shiftMs, 2) + " ms com a afinação"_t);
        rep.check (std::abs (peakDb (tuned) - peakDb (dry)) < 3.0f,
                   "nível parecido com e sem afinação ("_t + juce::String (peakDb (tuned), 1) + " x " + juce::String (peakDb (dry), 1) + " dBFS)");

        if (ins != nullptr)
        {
            const auto insertId = ins->id;
            // O tom do projeto muda e a afinação vai junto ("seguir o tom do projeto", 21/10); o preset não mexe nele.
            app.run ("project.setKey", args ({ { "root", "C" }, { "scale", "major" } }));
            app.run ("mixer.loadInsertPreset", args ({ { "track", bellMx }, { "insert", insertId }, { "preset", "robot" } }));
            const auto* after = model.get().findMixer (bellMx)->findInsert (insertId);
            rep.check (after != nullptr && after->params.at ("speed") == 0.0f && after->params.at ("key") == 0.0f
                           && after->params.at ("scale") == 1.0f,
                       "preset Robô troca a velocidade e mantém o tom da afinação (que seguiu o projeto para Dó maior)"_t);
        }

        for (auto& ch : model.get().channels)
            app.run ("channel.setMute", args ({ { "channel", ch.id }, { "on", false } }));
    }

    //==========================================================================
    // Efeitos de voz e beat (21/10, noite): cada efeito novo entra no Bell pelo comando, o render passa por
    // ele sem NaN, o ataque fica no lugar (a latência do gate, do FET, do limpa-voz e do console é compensada)
    // e a janela do efeito é desenhada fora da tela (fx-<tipo>.png na pasta do autoteste).
    rep.section ("Mixagem: efeitos de voz e beat"_t);

    if (ctx.demoProject.existsAsFile())
    {
        app.run ("project.open", args ({ { "path", ctx.demoProject.getFullPathName() } }));

        auto trackOf = [&model] (const juce::String& name)
        {
            for (auto& m : model.get().mixer)
                if (m.name == name)
                    return m.id;
            return juce::String();
        };

        const auto bellMx = trackOf ("Bell");
        app.run ("mixer.setSolo", args ({ { "track", bellMx }, { "on", true } }));

        auto render = [&] (const juce::String& name)
        {
            const auto f = workFolder.getChildFile ("fx_" + name + ".wav");
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            return readWav (f);
        };

        // Ataque: a primeira amostra acima de 5% do pico do próprio arquivo (o nivelador e os compressores mudam o volume).
        auto onset = [] (const Audio& a)
        {
            const float limit = 0.05f * juce::Decibels::decibelsToGain (peakDb (a));

            for (int i = 0; i < a.buffer.getNumSamples(); ++i)
                if (std::abs (a.buffer.getSample (0, i)) > limit)
                    return i / a.sampleRate;

            return -1.0;
        };

        auto finite = [] (const Audio& a)
        {
            for (int c = 0; c < a.buffer.getNumChannels(); ++c)
                for (int i = 0; i < a.buffer.getNumSamples(); ++i)
                    if (! std::isfinite (a.buffer.getSample (c, i)))
                        return false;

            return true;
        };

        auto shoot = [&] (const juce::String& trackId, const juce::String& insertId, const juce::String& name)
        {
            auto* fx = app.getProjector().fxFor (trackId, insertId);

            if (fx == nullptr)
                return false;

            FxEditor editor (app, *fx);
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
            const auto file = workFolder.getChildFile ("fx-" + name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            return image.isValid() && out.openedOk() && png.writeImageToStream (image, out);
        };

        // O atraso (em amostras, de -300 a 300) que melhor alinha `wet` com `dry` no primeiro segundo do Bell.
        auto lagOf = [] (const Audio& dryAudio, const Audio& wetAudio)
        {
            const auto* d = dryAudio.buffer.getReadPointer (0);
            const auto* w = wetAudio.buffer.getReadPointer (0);
            const int n = juce::jmin (dryAudio.buffer.getNumSamples(), wetAudio.buffer.getNumSamples());
            const int from = 300, to = juce::jmin (n - 301, (int) dryAudio.sampleRate);
            int best = 0;
            double bestValue = -1.0e30;

            for (int lag = -300; lag <= 300; ++lag)
            {
                double acc = 0.0;

                for (int i = from; i < to; ++i)
                    acc += (double) d[i] * w[i + lag];

                if (acc > bestValue)
                {
                    bestValue = acc;
                    best = lag;
                }
            }

            return best;
        };

        const auto dry = render ("seco");
        const double dryOnset = onset (dry);
        rep.check (dry.valid && dryOnset > 0.0, "Bell sozinho renderizado (ataque em "_t + juce::String (dryOnset * 1000.0, 1) + " ms)");

        // Compensação de latência: com o efeito neutro (a saída é a entrada atrasada), o render sai igual ao seco.
        {
            struct Probe { const char* type; juce::var params; };
            const Probe probes[] = { { "gate", args ({ { "range", 0.0 } }) }, { "fet", args ({ { "threshold", 0.0 }, { "color", 0.0 } }) },
                                     { "console", args ({ { "output", 0.0 } }) }, { "denoise", args ({ { "reduction", 0.0 } }) },
                                     { "voicepitch", args ({ { "semitones", 0.0 } }) }, { "harmony", args ({ { "level", 0.0 } }) },
                                     { "sat", args ({ { "mix", 0.0 } }) }, { "tune", args ({ { "amount", 0.0 }, { "mix", 0.0 } }) },
                                     // 26/10: o clipper abaixo do teto e o lo-fi sem mistura têm latência; os outros, não
                                     { "clipper", args ({ { "gain", 0.0 } }) }, { "lofi", args ({ { "mix", 0.0 } }) },
                                     { "chorus", args ({ { "mix", 0.0 } }) }, { "flanger", args ({ { "mix", 0.0 } }) },
                                     { "phaser", args ({ { "mix", 0.0 } }) }, { "dynEq", args ({ { "b1On", 0.0 }, { "b2On", 0.0 } }) } };

            for (auto& pr : probes)
            {
                const auto added = app.run ("mixer.addInsert", args ({ { "track", bellMx }, { "type", pr.type } }));
                const auto insertId = added.value["insert"].toString();
                app.run ("mixer.setInsertParam", args ({ { "track", bellMx }, { "insert", insertId }, { "params", pr.params } }));
                const auto wet = render (juce::String ("neutro_") + pr.type);
                const int lag = wet.valid ? lagOf (dry, wet) : 9999;

                // A diferença conta depois dos 2 primeiros blocos (1024 amostras): no começo de um render com
                // latência que não é múltipla do bloco, o motor solta o primeiro bloco fora do lugar (de todos
                // os efeitos com latência, o limitador do master também; está em Pedidos abertos para o núcleo).
                float diff = wet.valid && wet.buffer.getNumSamples() == dry.buffer.getNumSamples() ? 0.0f : 1.0f;
                int lastBadAtStart = -1;

                for (int c = 0; diff < 1.0f && c < dry.buffer.getNumChannels(); ++c)
                    for (int i = 0; i < dry.buffer.getNumSamples(); ++i)
                    {
                        const float d = std::abs (dry.buffer.getSample (c, i) - wet.buffer.getSample (c, i));

                        if (i >= 1024)
                            diff = std::max (diff, d);
                        else if (d > 1.0e-3f)
                            lastBadAtStart = std::max (lastBadAtStart, i);
                    }

                // O sat em mistura 0 passa o seco pelo sobreamostrador dele (filtros de fase não linear, efeito
                // de antes desta frente): sai alinhado, mas não igual bit a bit (0,5% no Bell).
                // O clipper também passa pelo sobreamostrador.
                const float tolerance = juce::String (pr.type) == "sat" || juce::String (pr.type) == "clipper" ? 1.0e-2f : 2.0e-3f;
                rep.check (added.ok && lag == 0 && diff < tolerance,
                           juce::String (pr.type) + " neutro: o render sai alinhado com o seco (atraso medido "_t + juce::String (lag)
                               + " amostras, diferença máx. " + juce::String (diff, 6) + " depois de 1024 amostras"_t
                               + (lastBadAtStart >= 0 ? "; o começo difere até a amostra "_t + juce::String (lastBadAtStart) : juce::String())
                               + ")");
                app.run ("mixer.removeInsert", args ({ { "track", bellMx }, { "insert", insertId } }));
            }
        }

        struct Case { const char* type; const char* preset; };
        const Case cases[] = { { "gate", "vocal-tight" }, { "opto", "vocal-smooth" }, { "fet", "rap-front" }, { "multiband", "vocal-harsh" },
                               { "leveler", "vocal-rap" }, { "doubler", "double-wide" }, { "bassplus", "808-phone" },
                               { "denoise", "home-light" }, { "console", "vocal-rap" }, { "vintageEq", "vocal-air" },
                               { "voicepitch", "deep" }, { "harmony", "chord" },
                               // 26/10 (o lo-fi sem a fita oscilando, para a correlação com o seco medir o alinhamento)
                               { "dynEq", "vocal-body" }, { "clipper", "drums-punch" }, { "lofi", "sampler-90" },
                               { "chorus", "soft" }, { "flanger", "subtle" }, { "phaser", "keys" } };
        int shots = 0;

        for (auto& cs : cases)
        {
            const auto added = app.run ("mixer.addInsert", args ({ { "track", bellMx }, { "type", cs.type }, { "preset", cs.preset } }));
            const auto insertId = added.value["insert"].toString();
            auto* fx = app.getProjector().fxFor (bellMx, insertId);
            const int latency = fx != nullptr ? fx->getHost().latencySamples() : -1;
            const auto wet = render (cs.type);
            const double shiftMs = 1000.0 * (onset (wet) - dryOnset);

            // O tom da voz e a harmonia mudam a nota: no lugar da correlação, o ataque (sem a compensação,
            // ele andaria os 50 ms da latência). Os outros: a correlação com o seco fica a menos de 0,25 ms.
            const bool shifts = std::string_view (cs.type) == "voicepitch" || std::string_view (cs.type) == "harmony";
            const int lag = wet.valid && ! shifts ? lagOf (dry, wet) : 0;
            const bool aligned = shifts ? std::abs (shiftMs) < 5.0 : std::abs (lag) <= 12;

            rep.check (added.ok && fx != nullptr && wet.valid && finite (wet) && aligned && peakDb (wet) < 3.0f
                           && peakDb (wet) > peakDb (dry) - 24.0f,
                       juce::String (cs.type) + " (" + cs.preset + "): render sem NaN, pico "_t + juce::String (peakDb (wet), 1) + " dBFS (seco "_t
                           + juce::String (peakDb (dry), 1) + "), ataque "_t + juce::String (shiftMs, 2) + " ms"
                           + (shifts ? juce::String() : ", alinhado com o seco a "_t + juce::String (lag) + " amostras"));
            juce::ignoreUnused (latency);

            if (shoot (bellMx, insertId, cs.type))
                ++shots;

            app.run ("mixer.removeInsert", args ({ { "track", bellMx }, { "insert", insertId } }));
        }

        rep.check (shots == (int) (sizeof (cases) / sizeof (cases[0])), "janelas dos "_t + juce::String (shots) + " efeitos novos desenhadas (fx-*.png)");

        // Afinação: o tom do projeto chega junto; escolher o tom na mão desliga o "seguir".
        {
            const auto added = app.run ("mixer.addInsert", args ({ { "track", bellMx }, { "type", "tune" }, { "preset", "hard" } }));
            const auto insertId = added.value["insert"].toString();
            auto paramOf = [&] (const char* id)
            {
                const auto* ins = model.get().findMixer (bellMx)->findInsert (insertId);
                return ins != nullptr && ins->params.count (id) > 0 ? ins->params.at (id) : -1.0f;
            };

            app.run ("project.setKey", args ({ { "root", "D" }, { "scale", "major" } }));
            const bool followed = paramOf ("key") == 2.0f && paramOf ("scale") == 1.0f && paramOf ("follow") == 1.0f;
            app.run ("mixer.setInsertParam", args ({ { "track", bellMx }, { "insert", insertId }, { "param", "key" }, { "value", 7 } }));
            app.run ("project.setKey", args ({ { "root", "A" }, { "scale", "minor" } }));
            const bool kept = paramOf ("key") == 7.0f && paramOf ("follow") == 0.0f;
            rep.check (added.ok && followed && kept, "afinação segue o tom do projeto (Ré maior); com o tom escolhido na mão, fica no Sol"_t);
            app.run ("mixer.removeInsert", args ({ { "track", bellMx }, { "insert", insertId } }));
        }

        app.run ("mixer.setSolo", args ({ { "track", bellMx }, { "on", false } }));

        // Beat pronto de 2 pistas: papel próprio e a multibanda disparada pela voz; sem voz, ela sai da cadeia.
        {
            const auto beat = app.run ("mixer.addTrack", args ({ { "name", "Beat" } }));
            const auto voice = app.run ("mixer.addTrack", args ({ { "name", "Voz" } }));
            const auto beatId = beat.value["track"].toString(), voiceId = voice.value["track"].toString();
            juce::Array<juce::var> only;
            only.add (beatId);
            app.run ("mixer.applyStyle", args ({ { "style", "trap" }, { "tracks", only } }));

            juce::String keyedBy;
            bool hasMultiband = false;

            for (auto& ins : model.get().findMixer (beatId)->inserts)
                if (ins.type == "multiband")
                {
                    hasMultiband = true;
                    keyedBy = ins.sidechain;
                }

            rep.check (beat.ok && hasMultiband && keyedBy == voiceId, "trilha \"Beat\" ganha a receita do beat, com a multibanda disparada pela voz"_t);

            app.run ("mixer.removeTrack", args ({ { "track", voiceId } }));
            app.run ("mixer.applyStyle", args ({ { "style", "trap" }, { "tracks", only } }));
            hasMultiband = false;

            for (auto& ins : model.get().findMixer (beatId)->inserts)
                hasMultiband |= ins.type == "multiband";

            rep.check (! hasMultiband && ! model.get().findMixer (beatId)->inserts.empty(),
                       "sem trilha de voz, a multibanda sai e o resto da receita fica"_t);
            rep.check (app.getProjector().insertProblems().size() == 0, "todos os efeitos novos abriram no motor"_t);
        }

        // Meus presets e minhas cadeias: ficam na pasta de dados do app e voltam em qualquer projeto.
        {
            const auto kickMx = trackOf ("Kick");
            const auto comp = app.run ("mixer.addInsert", args ({ { "track", kickMx }, { "type", "comp" }, { "preset", "kick-punch" } }));
            const auto compId = comp.value["insert"].toString();
            app.run ("mixer.setInsertParam", args ({ { "track", kickMx }, { "insert", compId }, { "param", "threshold" }, { "value", -21.0 } }));
            const auto saved = app.run ("mixer.saveInsertPreset", args ({ { "track", kickMx }, { "insert", compId }, { "name", "Meu kick" } }));
            const auto userId = saved.value["preset"].toString();

            bool listed = false;
            const auto fxList = app.run ("fx.list");

            for (auto& e : *fxList.value["effects"].getArray())
                if (e["type"].toString() == "comp")
                    for (auto& u : *e["userPresets"].getArray())
                        listed |= u["id"].toString() == userId && u["name"].toString() == "Meu kick";

            const auto used = app.run ("mixer.addInsert", args ({ { "track", bellMx }, { "type", "comp" }, { "preset", userId } }));
            const auto* usedIns = model.get().findMixer (bellMx)->findInsert (used.value["insert"].toString());
            rep.check (saved.ok && userId == "user:meu-kick" && listed && usedIns != nullptr && usedIns->params.at ("threshold") == -21.0f
                           && usedIns->preset == userId && usermix::presetsFile().existsAsFile(),
                       "meu preset: salvo em meus-presets.json, listado no fx.list e usado em outra trilha (limiar -21 dB)"_t);
            app.run ("mixer.removeInsert", args ({ { "track", bellMx }, { "insert", used.value["insert"] } }));
            app.run ("mixer.removeInsert", args ({ { "track", kickMx }, { "insert", compId } }));
            rep.check (app.run ("mixer.deleteInsertPreset", args ({ { "type", "comp" }, { "preset", userId } })).ok
                           && ! usermix::findPreset ("comp", userId).has_value(),
                       "meu preset apagado"_t);

            // Cadeia: EQ, óptico e afinação no Bell → salva → carrega no Kick, com ids novos e o tom do projeto.
            const std::pair<const char*, const char*> chainItems[] = { { "eq", "vocal-trap" }, { "opto", "vocal-smooth" }, { "tune", "hard" } };

            for (auto& [fxType, fxPreset] : chainItems)
                app.run ("mixer.addInsert", args ({ { "track", bellMx }, { "type", fxType }, { "preset", fxPreset } }));

            const auto bellChain = model.get().findMixer (bellMx)->inserts;
            const auto savedChain = app.run ("mixer.saveChain", args ({ { "track", bellMx }, { "name", "Voz teste" } }));
            const auto listedChains = app.run ("mixer.listChains");
            bool inList = false;

            for (auto& n : *listedChains.value["chains"].getArray())
                inList |= n.toString() == "Voz teste";

            app.run ("project.setKey", args ({ { "root", "E" }, { "scale", "minor" } }));
            const auto before = model.get().findMixer (kickMx)->inserts;
            const auto loaded = app.run ("mixer.loadChain", args ({ { "track", kickMx }, { "name", "Voz teste" } }));
            const auto& after = model.get().findMixer (kickMx)->inserts;
            bool same = after.size() == bellChain.size();

            for (size_t i = 0; same && i < after.size(); ++i)
            {
                same &= after[i].type == bellChain[i].type && after[i].id != bellChain[i].id && after[i].preset == bellChain[i].preset;

                for (auto& [id, v] : bellChain[i].params)
                    if (id != "key" && id != "scale")
                        same &= after[i].params.count (id) > 0 && std::abs (after[i].params.at (id) - v) < 1.0e-4f;
            }

            const bool tuneInKey = ! after.empty() && after.back().type == "tune" && after.back().params.at ("key") == 4.0f;
            rep.check (savedChain.ok && inList && usermix::chainsFolder().getChildFile ("Voz teste.wnl").existsAsFile() && loaded.ok && same && tuneInKey,
                       "minha cadeia: salva em cadeias/Voz teste.wnl e carregada no Kick (3 efeitos, ids novos, afinação em Mi)"_t);
            rep.check (app.run ("edit.undo").ok && model.get().findMixer (kickMx)->inserts == before, "Ctrl+Z tira a cadeia carregada"_t);
            rep.check (app.run ("mixer.deleteChain", args ({ { "name", "Voz teste" } })).ok && ! usermix::chainNames().contains ("Voz teste")
                           && usermix::chainsFolder().getChildFile ("apagadas").getChildFile ("Voz teste.wnl").existsAsFile(),
                       "cadeia apagada da lista (o arquivo foi para cadeias/apagadas)"_t);
        }

        app.run ("project.new");
    }
    else
    {
        rep.check (false, "sem o projeto de demonstração: efeitos de voz e beat não testados"_t);
    }

    //==========================================================================
    // Revisão do mixer (22/10): o solo e os stems mantêm o sidechain, o bus em solo toca, a lei de pan
    // (potência constante, o centro igual), o fader do master antes da cadeia, o envio pré-fader e o
    // envio de uma trilha muda.
    rep.section ("Mixagem: solo, stems, pan e master"_t);
    {
        auto trackOf = [&model] (const juce::String& name)
        {
            for (auto& m : model.get().mixer)
                if (m.name == name)
                    return m.id;
            return juce::String();
        };

        auto exportMix = [&] (const juce::String& name)
        {
            const auto file = workFolder.getChildFile ("revisao_" + name + ".wav");
            app.run ("project.exportWav", args ({ { "path", file.getFullPathName() }, { "loops", 2 }, { "what", "pattern" } }));
            return readWav (file);
        };

        // Os stems pedidos, por id da trilha (sem a cadeia do master).
        auto exportStems = [&] (const juce::String& folder, const juce::Array<juce::var>& tracks)
        {
            std::map<juce::String, Audio> out;
            auto a = args ({ { "folder", workFolder.getChildFile (folder).getFullPathName() }, { "loops", 2 }, { "what", "pattern" } });

            if (! tracks.isEmpty())
                a.getDynamicObject()->setProperty ("tracks", tracks);

            const auto r = app.run ("project.exportStems", a);

            if (auto* files = r.value["files"].getArray())
                for (auto& f : *files)
                    out[f["track"].toString()] = readWav (juce::File (f["path"].toString()));

            return out;
        };

        auto channelRmsDb = [] (const Audio& a, int channel)
        {
            if (! a.valid || channel >= a.buffer.getNumChannels() || a.buffer.getNumSamples() == 0)
                return -200.0f;

            const float rms = a.buffer.getRMSLevel (channel, 0, a.buffer.getNumSamples());
            return rms > 0.0f ? juce::Decibels::gainToDecibels (rms, -200.0f) : -200.0f;
        };

        const auto demo33 = workFolder.getChildFile ("Demo 3.3").getChildFile ("Demo 3.3.wnl");

        if (demo33.existsAsFile() && app.run ("project.open", args ({ { "path", demo33.getFullPathName() } })).ok)
        {
            const auto kickMx = trackOf ("Kick"), bassMx = trackOf ("808"), reverbMx = trackOf ("Reverb");
            juce::String duckId;

            for (auto& ins : model.get().findMixer (bassMx)->inserts)
                if (ins.sidechain == kickMx)
                    duckId = ins.id;

            rep.check (duckId.isNotEmpty(), "o estilo trap pôs o compressor do 808 disparado pelo kick"_t);

            // Sem a cadeia do master: a mixagem tem de ser a soma dos stems.
            const auto masterInserts = model.get().findMixer (masterTrackId)->inserts;

            for (auto& ins : masterInserts)
                app.run ("mixer.setInsertBypass", args ({ { "track", masterTrackId }, { "insert", ins.id }, { "on", true } }));

            // Sem o limitador, o kick do trap passa de 0 dBFS e o WAV corta o pico: com o master 12 dB abaixo
            // (o fader entra nos stems também), a soma não corta e a comparação vale.
            const float masterDb = model.get().findMixer (masterTrackId)->volumeDb;
            app.run ("mixer.setVolume", args ({ { "track", masterTrackId }, { "db", masterDb - 12.0f } }));

            const auto mix = exportMix ("mix_sem_master");
            const auto stems = exportStems ("revisao_stems", {});
            Audio sum;
            sum.buffer.setSize (mix.buffer.getNumChannels(), mix.buffer.getNumSamples());
            sum.buffer.clear();
            sum.sampleRate = mix.sampleRate;
            sum.valid = mix.valid && ! stems.empty();

            for (auto& [id, s] : stems)
            {
                sum.valid = sum.valid && s.valid && s.buffer.getNumSamples() == sum.buffer.getNumSamples();

                for (int c = 0; sum.valid && c < sum.buffer.getNumChannels(); ++c)
                    sum.buffer.addFrom (c, 0, s.buffer, c, 0, sum.buffer.getNumSamples());
            }

            Audio residual;
            residual.buffer.makeCopyOf (mix.buffer);
            residual.sampleRate = mix.sampleRate;
            residual.valid = sum.valid;

            for (int c = 0; sum.valid && c < residual.buffer.getNumChannels(); ++c)
                residual.buffer.addFrom (c, 0, sum.buffer, c, 0, residual.buffer.getNumSamples(), -1.0f);

            const float residualDb = sum.valid ? rmsDb (residual) - rmsDb (mix) : 0.0f;
            rep.check (sum.valid && residualDb < -40.0f,
                       "com o 808 abaixado pelo kick, a soma dos "_t + juce::String ((int) stems.size()) + " stems dá a mixagem (resíduo "_t
                           + juce::String (residualDb, 1) + " dB; antes o stem do 808 saía sem o abaixar)"_t);

            // O abaixar está no stem do 808: com o compressor desligado, o 808 sai mais alto. (Sem o disparo, o
            // compressor ouviria o próprio 808 e abaixaria ainda mais: a comparação é com ele desligado.)
            const auto bassStem = stems.count (bassMx) > 0 ? stems.at (bassMx) : Audio();
            app.run ("mixer.setInsertBypass", args ({ { "track", bassMx }, { "insert", duckId }, { "on", true } }));
            auto loose = exportStems ("revisao_808_sem_compressor", { bassMx });
            app.run ("edit.undo");
            const float ducking = loose.count (bassMx) > 0 ? rmsDb (loose[bassMx]) - rmsDb (bassStem) : 0.0f;
            rep.check (ducking > 1.0f, "o stem do 808 tem o abaixar do kick ("_t + juce::String (ducking, 1)
                                           + " dB a menos que com o compressor desligado)"_t);

            // 808 em solo: o kick some, mas continua abaixando o 808 (igual ao stem do 808).
            app.run ("mixer.setSolo", args ({ { "track", bassMx }, { "on", true } }));
            const auto soloBass = exportMix ("solo_808");
            app.run ("mixer.setSolo", args ({ { "track", bassMx }, { "on", false } }));

            if (model.get().findMixer (bassMx)->sends.empty())
                rep.check (maxDiff (soloBass, bassStem) < 1.0e-4f,
                           "808 em solo = o stem do 808: sem o kick e com o abaixar (diferença máx. "_t
                               + juce::String (maxDiff (soloBass, bassStem), 6) + ")");
            else
                rep.check (rmsDb (soloBass) < rmsDb (loose[bassMx]) + 6.0f, "808 em solo tem som (o 808 manda para um bus; comparação com o stem pulada)"_t);

            // Bus de reverb em solo: toca com quem manda para ele (como no FL), em vez de ficar mudo.
            if (reverbMx.isNotEmpty())
            {
                app.run ("mixer.setSolo", args ({ { "track", reverbMx }, { "on", true } }));
                const auto soloReverb = exportMix ("solo_reverb");
                app.run ("mixer.setSolo", args ({ { "track", reverbMx }, { "on", false } }));
                rep.check (peakDb (soloReverb) > -40.0f && maxDiff (soloReverb, mix) > 1.0e-3f,
                           "bus de reverb em solo toca com quem manda para ele ("_t + juce::String (peakDb (soloReverb), 1) + " dBFS; antes ficava mudo)"_t);
            }

            // Trilha muda não manda reverb, nem quando dispara um sidechain (o motor continua processando o kick).
            if (reverbMx.isNotEmpty())
            {
                const auto before = model.get();
                app.run ("mixer.setSend", args ({ { "track", kickMx }, { "to", reverbMx }, { "db", -3.5 } }));
                app.run ("mixer.setMute", args ({ { "track", kickMx }, { "on", true } }));
                const auto withSend = exportMix ("kick_mudo_com_envio");
                app.run ("mixer.removeSend", args ({ { "track", kickMx }, { "to", reverbMx } }));
                const auto withoutSend = exportMix ("kick_mudo_sem_envio");
                rep.check (maxDiff (withSend, withoutSend) < 1.0e-5f,
                           "kick mudo (fonte de sidechain) não manda reverb (diferença máx. "_t + juce::String (maxDiff (withSend, withoutSend), 7) + ")");
                app.run ("edit.undo");
                app.run ("edit.undo");
                app.run ("edit.undo");
                rep.check (model.get() == before, "Ctrl+Z volta o envio e o mute do kick"_t);
            }

            app.run ("mixer.setVolume", args ({ { "track", masterTrackId }, { "db", masterDb } }));

            for (auto& ins : masterInserts)
                app.run ("mixer.setInsertBypass", args ({ { "track", masterTrackId }, { "insert", ins.id }, { "on", false } }));

            // Fader do master antes da cadeia: +3 dB empurra o limitador, que segura o pico; o medidor vem depois.
            const auto atZero = exportMix ("master_0db");
            app.run ("mixer.setVolume", args ({ { "track", masterTrackId }, { "db", 3.0 } }));
            const auto hot = exportMix ("master_mais3db");
            app.run ("edit.undo");
            rep.check (hot.valid && peakDb (hot) <= -0.95f && rmsDb (hot) > rmsDb (atZero) + 0.5f,
                       "master +3 dB com o limitador: pico "_t + juce::String (peakDb (hot), 2) + " dBFS (teto -1; antes passava de 0 dBFS) e "_t
                           + juce::String (rmsDb (hot) - rmsDb (atZero), 1) + " dB mais alto"_t);

            auto& masterList = app.getEdit().getMasterPluginList();
            const int faderAt = masterList.indexOf (app.getProjector().volumeFor (masterTrackId));
            const int meterAt = masterList.indexOf (app.getProjector().meterFor (masterTrackId));
            rep.check (faderAt == 0 && meterAt > faderAt && std::abs (app.getEdit().getMasterVolumePlugin()->getVolumeDb()) < 0.001f,
                       "no master: o fader primeiro, a cadeia e o medidor no fim (o medidor mostra o que sai); o volume do motor em 0 dB"_t);
        }
        else
        {
            rep.check (false, "sem o Demo 3.3: solo e stems com sidechain não testados"_t);
        }

        if (ctx.demoProject.existsAsFile() && app.run ("project.open", args ({ { "path", ctx.demoProject.getFullPathName() } })).ok)
        {
            const auto bellMx = trackOf ("Bell");
            app.run ("mixer.setSolo", args ({ { "track", bellMx }, { "on", true } }));

            // Lei de pan, medida no render: o lado de perto e o de longe contra o centro.
            app.run ("mixer.setPan", args ({ { "track", bellMx }, { "pan", 0.0 } }));
            const auto center = exportMix ("pan_0");
            const float centerL = channelRmsDb (center, 0), centerR = channelRmsDb (center, 1);
            juce::String report;
            bool panOk = center.valid && center.buffer.getNumChannels() == 2 && centerL > -60.0f;

            const std::pair<float, std::pair<double, double>> cases[] = {
                { -1.0f, { 3.0103, -200.0 } }, { -0.5f, { 2.3226, -5.3329 } }, { 0.5f, { -5.3329, 2.3226 } }, { 1.0f, { -200.0, 3.0103 } } };

            for (auto& [pan, expected] : cases)
            {
                app.run ("mixer.setPan", args ({ { "track", bellMx }, { "pan", pan } }));
                const auto panned = exportMix ("pan_" + juce::String (juce::roundToInt (pan * 100)));
                const float dl = channelRmsDb (panned, 0) - centerL, dr = channelRmsDb (panned, 1) - centerR;
                auto close = [] (float got, double want) { return want < -100.0 ? got < -60.0f : std::abs (got - (float) want) < 0.05f; };
                panOk = panOk && close (dl, expected.first) && close (dr, expected.second);
                report << juce::String (juce::roundToInt (pan * 100)) << ": " << juce::String (dl, 2) << "/" << juce::String (dr, 2) << " dB  ";
            }

            rep.check (panOk, "lei de pan (E/D contra o centro): "_t + report.trim() + " (antes: +6,02 nas pontas e +3,52/-6,02 em 50)"_t);

            app.run ("mixer.setPan", args ({ { "track", bellMx }, { "pan", 0.0 } }));
            const auto again = exportMix ("pan_0_de_novo");
            rep.check (maxDiff (again, center) < 1.0e-6f, "de volta ao centro, sai igual (diferença máx. "_t + juce::String (maxDiff (again, center), 8) + ")");
            app.run ("mixer.setSolo", args ({ { "track", bellMx }, { "on", false } }));

            // Envio pré-fader: o bus recebe o mesmo com o fader e o pan da Bell mexidos; pós-fader, desce junto.
            const auto bus = app.run ("mixer.addTrack", args ({ { "name", "Bus teste" } })).value["track"].toString();
            app.run ("mixer.setSend", args ({ { "track", bellMx }, { "to", bus }, { "db", -6.0 }, { "pre", true } }));
            const bool savedPre = model.get().findMixer (bellMx)->findSend (bus) != nullptr && model.get().findMixer (bellMx)->findSend (bus)->preFader;
            auto busPre = exportStems ("revisao_bus_pre", { bus });
            app.run ("mixer.setVolume", args ({ { "track", bellMx }, { "db", -12.0 } }));
            app.run ("mixer.setPan", args ({ { "track", bellMx }, { "pan", 0.7 } }));
            auto busPreMoved = exportStems ("revisao_bus_pre_mexido", { bus });
            app.run ("mixer.setSend", args ({ { "track", bellMx }, { "to", bus }, { "pre", false } }));
            auto busPost = exportStems ("revisao_bus_pos", { bus });

            const float preDiff = busPre.count (bus) && busPreMoved.count (bus) ? maxDiff (busPre[bus], busPreMoved[bus]) : 1.0f;
            const float postDrop = busPre.count (bus) && busPost.count (bus) ? rmsDb (busPost[bus]) - rmsDb (busPre[bus]) : 0.0f;
            rep.check (savedPre && preDiff < 1.0e-5f && postDrop < -10.0f,
                       "envio pré-fader: o bus não muda com o fader e o pan da Bell (diferença máx. "_t + juce::String (preDiff, 7)
                           + "); pós-fader desce "_t + juce::String (postDrop, 1) + " dB");

            app.run ("mixer.setSend", args ({ { "track", bellMx }, { "to", bus }, { "pre", true } }));

            {
                // O painel da Bell desenhado fora da tela, com o botão PRÉ aceso (para olhar a tela).
                InsertPanel panel (app);
                panel.setTrack (bellMx);
                panel.refresh (model.get());
                panel.setSize (InsertPanel::idealWidth, panel.getIdealHeight());
                const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
                const auto shot = workFolder.getChildFile ("painel-envio-pre.png");
                shot.deleteFile();
                juce::FileOutputStream out (shot);
                juce::PNGImageFormat png;
                rep.check (image.isValid() && out.openedOk() && png.writeImageToStream (image, out),
                           "painel da Bell com o envio PRÉ desenhado (painel-envio-pre.png)"_t);
            }

            const auto preFile = workFolder.getChildFile ("Envio pre").getChildFile ("Envio pre.wnl");
            app.run ("project.save", args ({ { "path", preFile.getFullPathName() } }));
            const auto savedJson = wnl::toJson (model.get());
            app.run ("project.new");
            rep.check (preFile.loadFileAsString().contains ("\"pre\": true") && app.run ("project.open", args ({ { "path", preFile.getFullPathName() } })).ok
                           && wnl::toJson (model.get()) == savedJson,
                       ".wnl guarda o envio pré-fader (\"pre\": true) e reabre igual"_t);
        }
        else
        {
            rep.check (false, "sem o projeto de demonstração: lei de pan e envio pré-fader não testados"_t);
        }

        // Legendas dos knobs em maiúsculas com acento.
        rep.check (fxui::caps ("afinação, início e saída"_t) == "AFINAÇÃO, INÍCIO E SAÍDA"_t,
                   "legenda em maiúsculas com acento: "_t + fxui::caps ("afinação, início e saída"_t));

        app.run ("project.new");
    }
}

} // namespace whynot::selftest
