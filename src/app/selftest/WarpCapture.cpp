// Autoteste do bloco 3 (pedido do usuário de 23/10, depois da pesquisa dos elogios do Ableton):
// - warp: o loop entra no BPM do projeto sem mudar o tom (audio.warpToProject), pelo nome do arquivo, no meio tempo,
//   e o automático que só age em loop fora do tempo;
// - groove: o swing de um loop de bateria vira o swing dos canais (audio.extractGroove);
// - strum nas notas que começam juntas (notetools e pianoroll.transform);
// - Capture MIDI: a última frase tocada ao vivo, no começo ou no lugar em que tocou, e o comando record.captureMidi;
// - reamostrar a música ou um trecho num áudio no fim (project.resample).

#include "SelfTestKit.h"
#include "../../model/NoteTools.h"
#include "../../record/Capture.h"

namespace whynot::selftest
{

namespace
{
    /** Um loop de bateria de teste: cliques curtos nos tempos de `hits` (segundos), em mono a 48 kHz. */
    bool writeClicks (const juce::File& file, double seconds, const std::vector<double>& hits)
    {
        const double rate = 48000.0;
        juce::AudioBuffer<float> audio (1, (int) (seconds * rate));
        audio.clear();
        juce::Random noise (11);

        for (double t : hits)
        {
            const int at = (int) (t * rate);

            for (int i = 0; i < 480 && at + i < audio.getNumSamples(); ++i)
                audio.setSample (0, at + i, 0.8f * (noise.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / 80.0f));
        }

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        juce::WavAudioFormat wav;
        auto writer = stream != nullptr ? wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                                                                        .withNumChannels (1)
                                                                                                        .withBitsPerSample (24))
                                        : nullptr;
        return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    /** Colcheias num loop de `bars` compassos em `bpm`. */
    std::vector<double> eighths (double bpm, int bars)
    {
        std::vector<double> hits;
        const double eighth = 30.0 / bpm;

        for (int i = 0; i < bars * 8; ++i)
            hits.push_back (i * eighth);

        return hits;
    }
}

void runWarpCaptureSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto folder = ctx.work.getChildFile ("Bloco3");
    folder.deleteRecursively();
    folder.createDirectory();

    //--------------------------------------------------------------------------
    rep.section ("Bloco 3: warp do loop"_t);
    {
        app.run ("project.new");   // 140 BPM
        const auto loop = folder.getChildFile ("Loop 90bpm.wav");
        rep.check (writeClicks (loop, 2 * 4 * 60.0 / 90.0, eighths (90.0, 2)), "um loop de 2 compassos a 90 BPM, com o BPM no nome"_t);

        const auto added = app.run ("playlist.addItem", args ({ { "path", loop.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
        const auto itemId = added.value["item"].toString();
        const auto warped = app.run ("audio.warpToProject", args ({ { "item", itemId } }));
        const auto* item = model.get().findItem (itemId);
        rep.check (warped.ok && item != nullptr && std::abs (item->stretch - 90.0 / 140.0) < 0.001 && item->length == 2 * ticksPerBar4 && ! item->tape,
                   "warp: o loop de 90 vai para os 140 do projeto (esticar "_t + juce::String (item != nullptr ? item->stretch : 0.0, 4)
                       + "), cobre os 2 compassos e o tom fica"_t);

        const auto again = app.run ("audio.warpToProject", args ({ { "item", itemId }, { "auto", true } }));
        rep.check (again.ok && ! (bool) again.value["applied"], "o automático não mexe num loop que já está no tempo"_t);

        app.run ("edit.undo");
        rep.check (model.get().findItem (itemId) != nullptr && model.get().findItem (itemId)->stretch == 1.0, "desfazer volta o loop ao tempo do arquivo"_t);

        const auto half = folder.getChildFile ("Loop 70bpm.wav");
        writeClicks (half, 4 * 60.0 / 70.0, eighths (70.0, 1));
        const auto halfItem = app.run ("playlist.addItem", args ({ { "path", half.getFullPathName() }, { "track", 2 }, { "start", 0 } })).value["item"].toString();
        const auto halfWarp = app.run ("audio.warpToProject", args ({ { "item", halfItem } }));
        const auto* halfLoop = model.get().findItem (halfItem);
        rep.check (halfWarp.ok && std::abs ((double) halfWarp.value["bpm"] - 70.0) < 0.01 && halfLoop != nullptr
                       && std::abs (halfLoop->stretch - 0.5) < 0.001 && halfLoop->length == ticksPerBar4,
                   "o BPM do nome vale como está: o loop de 70 toca a 140 e continua com 1 compasso"_t);

        // o automático só age num loop: compassos inteiros; um áudio de 10 s a 150 BPM (6,25 compassos) só avisa
        const auto beat = folder.getChildFile ("Trap 150bpm.wav");
        writeClicks (beat, 10.0, eighths (150.0, 6));
        const auto beatItem = app.run ("playlist.addItem", args ({ { "path", beat.getFullPathName() }, { "track", 3 }, { "start", 0 } })).value["item"].toString();
        const auto beatWarp = app.run ("audio.warpToProject", args ({ { "item", beatItem }, { "auto", true } }));
        rep.check (beatWarp.ok && ! (bool) beatWarp.value["applied"] && model.get().findItem (beatItem)->stretch == 1.0,
                   "um beat que não fecha em compassos inteiros não é esticado sozinho"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Bloco 3: o swing de um loop"_t);
    {
        app.run ("project.new");
        // 2 compassos a 140, semicolcheias, com os contratempos atrasados em 50% de swing (40 ticks)
        const double step = 15.0 / 140.0, late = 40.0 * 60.0 / 140.0 / ticksPerBeat;
        std::vector<double> hits;

        for (int i = 0; i < 32; ++i)
            hits.push_back (i * step + (i % 2 == 1 ? late : 0.0));

        const auto loop = folder.getChildFile ("Groove 140bpm.wav");
        writeClicks (loop, 2 * 4 * 60.0 / 140.0, hits);
        const auto itemId = app.run ("playlist.addItem", args ({ { "path", loop.getFullPathName() }, { "track", 1 }, { "start", 0 } })).value["item"].toString();
        const auto hat = channelId (model.get(), "Hat");
        const auto groove = app.run ("audio.extractGroove", args ({ { "item", itemId }, { "channels", juce::Array<juce::var> { hat } } }));
        const float swing = groove.ok ? (float) groove.value["swing"] : -1.0f;
        const auto* hatChannel = model.get().findChannel (hat);
        rep.check (groove.ok && std::abs (swing - 50.0f) <= 12.0f && hatChannel != nullptr && std::abs (hatChannel->swing - swing) < 0.01f,
                   "o swing medido no loop: "_t + juce::String (swing, 0) + "% (feito com 50%), e o Hat ganhou o mesmo"_t
                       + (groove.ok ? juce::String() : ": " + groove.error));

        const auto straight = folder.getChildFile ("Reto 140bpm.wav");
        writeClicks (straight, 2 * 4 * 60.0 / 140.0, eighths (140.0, 2));
        const auto straightItem = app.run ("playlist.addItem", args ({ { "path", straight.getFullPathName() }, { "track", 2 }, { "start", 0 } })).value["item"].toString();
        const auto none = app.run ("audio.extractGroove", args ({ { "item", straightItem } }));
        rep.check (! none.ok, "um loop sem contratempo nas semicolcheias não inventa swing"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Bloco 3: strum"_t);
    {
        std::vector<Note> chord { { 1, 0, 960, 60, 100, false }, { 2, 0, 960, 64, 100, false }, { 3, 0, 960, 67, 100, false },
                                  { 4, 960, 240, 72, 100, false } };
        const int changed = notetools::strum (chord, {}, 40);
        rep.check (changed == 2 && chord[0].start == 0 && chord[1].start == 40 && chord[2].start == 80 && chord[2].end() == 960 && chord[3].start == 960,
                   "strum: a grave primeiro, cada uma 40 ticks depois, o fim no lugar, e a nota sozinha fica"_t);

        app.run ("project.new");
        const auto ch = channelId (model.get(), "808");
        addNotes (app, ch, { noteVar (0.0, 1.0, 48, false), noteVar (0.0, 1.0, 55, false) });
        const auto r = app.run ("pianoroll.transform", args ({ { "channel", ch }, { "op", "strum" }, { "amount", 0.5 } }));
        const auto* pat = model.get().currentPatternPtr();
        const auto* notes = pat != nullptr ? pat->notesFor (ch) : nullptr;
        rep.check (r.ok && notes != nullptr && notes->size() == 2 && (*notes)[1].start == 40, "strum pelo comando (médio = 40 ticks)"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Bloco 3: Capture MIDI"_t);
    {
        capture::Buffer buffer;
        buffer.add ({ 1000.0, 60, 100, true });
        buffer.add ({ 1250.0, 60, 0, false });
        buffer.add ({ 7000.0, 67, 90, true });   // depois de uma pausa: outra frase
        buffer.add ({ 7500.0, 67, 0, false });
        buffer.add ({ 7500.0, 69, 80, true });
        bool inTime = true;
        const auto phrase = buffer.phrase (120.0, 8000.0, inTime);
        rep.check (phrase.size() == 2 && phrase[0].key == 67 && phrase[0].start == 0 && phrase[0].length == 960 && phrase[1].start == 960
                       && phrase[1].length == 960 && ! inTime,
                   "a última frase (depois da pausa), do começo, no BPM do projeto; a nota presa vai até agora"_t);

        capture::Buffer playing;
        playing.add ({ 0.0, 60, 100, true, 480.0 });
        playing.add ({ 100.0, 60, 0, false, 600.0 });
        playing.add ({ 400.0, 62, 100, true, 1440.0 });
        playing.add ({ 500.0, 62, 0, false, 1560.0 });
        const auto played = playing.phrase (140.0, 600.0, inTime);
        rep.check (inTime && played.size() == 2 && played[0].start == 480 && played[1].start == 1440,
                   "tocada com o padrão rodando, cada nota fica onde caiu nele"_t);

        app.run ("project.new");
        const auto ch = channelId (model.get(), "808");
        app.clearCapture();
        rep.check (! app.run ("record.captureMidi", args ({ { "channel", ch } })).ok, "sem nada tocado, o Capture explica"_t);

        for (int key : { 48, 51, 55 })
        {
            app.liveNote (ch, key, 100, true);
            app.liveNote (ch, key, 0, false);
        }

        const auto captured = app.run ("record.captureMidi", args ({ { "channel", ch } }));
        const auto* pat = model.get().currentPatternPtr();
        const auto* notes = pat != nullptr ? pat->notesFor (ch) : nullptr;
        rep.check (captured.ok && (int) captured.value["notes"] == 3 && notes != nullptr && notes->size() == 3 && (*notes)[0].key == 48,
                   "o que foi tocado ao vivo (sem gravar) vira 3 notas no padrão"_t);
        rep.check (! app.run ("record.captureMidi", args ({ { "channel", ch } })).ok, "a mesma frase não entra duas vezes"_t);
        rep.check (KeyMap::findAction ("record.captureMidi") != nullptr, "o Capture MIDI está no editor de atalhos"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Bloco 3: reamostrar"_t);

    if (ctx.demoProject.existsAsFile() && app.run ("project.open", args ({ { "path", ctx.demoProject.getFullPathName() } })).ok)
    {
        const int songEnd = model.get().songEndTicks();
        const auto all = app.run ("project.resample");
        const auto* item = model.get().findItem (all.value["item"].toString());
        const auto* channel = model.get().findChannel (all.value["channel"].toString());
        const auto audio = readWav (juce::File (all.value["path"].toString()));
        rep.check (all.ok && item != nullptr && channel != nullptr && item->start >= songEnd && item->start % ticksPerBar4 == 0
                       && item->length == songEnd && channel->output == masterTrackId && audio.valid && peakDb (audio) > -30.0f,
                   "a música inteira vira um áudio depois do fim (pico "_t + juce::String (audio.valid ? peakDb (audio) : -200.0f, 1) + " dBFS)"
                       + (all.ok ? juce::String() : ": " + all.error));
        app.run ("edit.undo");

        const auto part = app.run ("project.resample", args ({ { "from", 2 }, { "to", 4 }, { "unit", "bars" } }));
        const auto* partItem = model.get().findItem (part.value["item"].toString());
        rep.check (part.ok && partItem != nullptr && partItem->length == 2 * ticksPerBar4, "um trecho de 2 compassos vira um áudio de 2 compassos"_t);
        app.run ("edit.undo");
    }
    else
    {
        rep.check (false, "sem o Demo: reamostrar não testado"_t);
    }

    app.run ("project.new");
}

} // namespace whynot::selftest
