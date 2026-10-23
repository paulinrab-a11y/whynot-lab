// Autoteste do WHYNOT Piano (pedido do usuário de 21/10) dentro do app headless. Dono: Sampler.
// Uma biblioteca de teste (senoides em WAV) prova o caminho inteiro sem depender do download: o canal,
// o tom por tecla, as camadas de força, o release, o streaming depois do começo que fica na memória,
// o timbre, salvar e reabrir, desfazer e trocar por um sample. Com o Salamander instalado, também
// um acorde de verdade (pico, duração e o tempo de carga). Começa com project.new.

#include "SelfTestKit.h"
#include "../../engine/PianoLibrary.h"
#include "../../engine/WhynotPiano.h"

#include <thread>

namespace whynot::selftest
{

namespace
{
    constexpr double testRate = 44100.0;

    /** Senoide estéreo em 16 bits: esquerda em seno e direita em cosseno (o "lado" fica grande). */
    bool writeStereoSine (const juce::File& f, double hz, double seconds, float amp)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * testRate));

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double ph = juce::MathConstants<double>::twoPi * hz * (double) i / testRate;
            b.setSample (0, i, amp * (float) std::sin (ph));
            b.setSample (1, i, amp * (float) std::cos (ph));
        }

        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (testRate).withNumChannels (2).withBitsPerSample (16));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    juce::var zone (const char* file, int root, int lo, int hi, int lovel, int hivel, bool release)
    {
        return args ({ { "file", file }, { "root", root }, { "lo", lo }, { "hi", hi }, { "lovel", lovel }, { "hivel", hivel },
                       { "trigger", release ? "release" : "attack" }, { "veltrack", 0.0 }, { "releaseSeconds", 0.2 },
                       { "volumeDb", 0.0 }, { "rtDecay", 0.0 }, { "keytrack", 1.0 } });
    }

    juce::var pianoNote (double t, double len, int key, int vel)
    {
        return args ({ { "t", t }, { "len", len }, { "key", key }, { "vel", vel } });
    }

    float rmsDbIn (const Audio& a, int ch, double t0, double t1)
    {
        const int from = juce::jlimit (0, a.buffer.getNumSamples(), (int) (t0 * a.sampleRate));
        const int to = juce::jlimit (from, a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));

        if (! a.valid || to <= from || ch >= a.buffer.getNumChannels())
            return -200.0f;

        return juce::Decibels::gainToDecibels (a.buffer.getRMSLevel (ch, from, to - from), -200.0f);
    }

    /** RMS do lado (E - D) sobre o do meio (E + D): 0 = mono. */
    double sideRatio (const Audio& a, double t0, double t1)
    {
        if (! a.valid || a.buffer.getNumChannels() < 2)
            return 0.0;

        const int from = (int) (t0 * a.sampleRate), to = juce::jmin (a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));
        double side = 0.0, mid = 0.0;

        for (int i = from; i < to; ++i)
        {
            const double l = a.buffer.getSample (0, i), r = a.buffer.getSample (1, i);
            side += (l - r) * (l - r);
            mid += (l + r) * (l + r);
        }

        return mid > 0.0 ? std::sqrt (side / mid) : 0.0;
    }

    juce::String db (float v)   { return juce::String (v, 1).replaceCharacter ('.', ','); }
}

void runPianoSelfTest (SelfTestContext& ctx)
{
    auto& app = ctx.app;
    auto& model = ctx.model;
    auto check = [&ctx] (bool ok, const juce::String& what) { ctx.rep.check (ok, what); };
    auto section = [&ctx] (const juce::String& title) { ctx.rep.section (title); };

    //--------------------------------------------------------------------------
    section ("WHYNOT Piano: biblioteca de teste");
    const auto libFolder = ctx.work.getChildFile ("piano_teste");
    const auto samples = libFolder.getChildFile ("samples");
    samples.createDirectory();

    // 3 s cada: bem mais que o começo de 0,37 s que fica na memória
    const bool wrote = writeStereoSine (samples.getChildFile ("suave.wav"), 440.0, 3.0, 0.25f)
                    && writeStereoSine (samples.getChildFile ("forte.wav"), 440.0, 3.0, 0.5f)
                    && writeStereoSine (samples.getChildFile ("release.wav"), 880.0, 0.6, 0.4f);

    juce::Array<juce::var> zones { zone ("suave.wav", 69, 60, 80, 1, 64, false), zone ("forte.wav", 69, 60, 80, 65, 127, false),
                                   zone ("release.wav", 69, 69, 69, 1, 127, true), zone ("../fora.wav", 69, 0, 127, 1, 127, false) };
    const auto manifest = args ({ { "format", "whynot-piano" }, { "version", 1 }, { "name", "Piano de teste" },
                                  { "credit", "teste" }, { "license", "teste" }, { "samples", "samples" }, { "zones", zones } });
    libFolder.getChildFile ("whynot-piano.json").replaceWithText (juce::JSON::toString (manifest));
    piano::setLibraryOverride (libFolder);

    const auto status = app.run ("piano.status");
    check (wrote && status.ok && (bool) status.value["found"] && (int) status.value["zones"] == 3 && (int) status.value["skippedZones"] == 1,
           "a biblioteca de teste abre com 3 zonas (a de caminho \"../\" fica de fora)"_t);

    //--------------------------------------------------------------------------
    section ("WHYNOT Piano: canal, tom, força e release"_t);
    app.run ("project.new");
    const auto added = app.run ("channel.addPiano");
    const auto ch = added.value["channel"].toString(), mx = added.value["mixerTrack"].toString();
    const auto* c = model.get().findChannel (ch);
    auto* plugin = app.getProjector().pianoFor (ch);

    check (added.ok && c != nullptr && c->isPiano() && c->instrumentPreset == "grand" && ! c->oneShot,
           "canal WHYNOT Piano (sampler com instrument \"piano\", timbre Cauda)"_t);
    check (plugin != nullptr && plugin->hasLibrary(), "o projetor pôs o WHYNOT Piano na trilha, com a biblioteca carregada"_t);
    check (app.getProjector().sampleProblems()[ch].isEmpty(), "o canal do piano não reclama de sample"_t);

    app.run ("pattern.setLength", args ({ { "steps", 64 } }));   // 16 tempos: 6,9 s a 140 BPM
    app.run ("mixer.setSolo", args ({ { "track", mx }, { "on", true } }));

    int renders = 0;
    auto render = [&] (const juce::Array<juce::var>& notes)
    {
        app.run ("pianoroll.clear", args ({ { "channel", ch } }));
        app.run ("pianoroll.addNotes", args ({ { "channel", ch }, { "notes", notes }, { "unit", "beats" } }));
        const auto f = ctx.work.getChildFile ("piano_" + juce::String (++renders) + ".wav");
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        return readWav (f);
    };

    const double beat = beat140;

    // uma nota longa: o começo vem da memória e o resto do anel, enchido pelo próprio render
    const auto longA = render ({ pianoNote (0, 6, 69, 100) });
    const double hz = measureHz (longA, 0.1, 2.4);
    const float early = rmsDbIn (longA, 0, 0.05, 0.3), late = rmsDbIn (longA, 0, 1.6, 2.4);
    check (std::abs (hz - 440.0) < 1.0, "Lá 4 sai em 440 Hz ("_t + juce::String (hz, 2) + " Hz)");
    check (longA.valid && early > -40.0f && std::abs (early - late) < 0.5f,
           "a nota segue igual depois do começo na memória: "_t + db (early) + " dB no início, " + db (late) + " dB em 2 s");

    const auto c5 = render ({ pianoNote (0, 4, 72, 100) });
    const double hzC5 = measureHz (c5, 0.1, 1.5);
    check (std::abs (hzC5 - 523.25) < 1.5, "Dó 5 sai do mesmo sample 3 semitons acima: "_t + juce::String (hzC5, 2) + " Hz (esperado 523,25)");

    const auto soft = render ({ pianoNote (0, 4, 69, 40) });
    const auto hard = render ({ pianoNote (0, 4, 69, 120) });
    const float layers = rmsDbIn (hard, 0, 0.1, 1.5) - rmsDbIn (soft, 0, 0.1, 1.5);
    check (std::abs (layers - 6.02f) < 0.5f, "força 40 toca a camada suave e 120 a forte: "_t + db (layers) + " dB de diferença (esperado 6)");

    // soltar: a nota some em 0,2 s e o release (880 Hz) toca sozinho
    const auto rel = render ({ pianoNote (0, 2, 69, 100) });
    const double off = 2 * beat;
    const double hzRel = measureHz (rel, off + 0.3, off + 0.5);
    const float relLevel = rmsDbIn (rel, 0, off + 0.3, off + 0.5), after = rmsDbIn (rel, 0, off + 0.8, off + 1.2);
    check (std::abs (hzRel - 880.0) < 2.0 && relLevel > -40.0f,
           "ao soltar a tecla toca o sample de release: "_t + juce::String (hzRel, 1) + " Hz, " + db (relLevel) + " dB");
    check (after < -90.0f, "depois do release o canal fica em silêncio ("_t + db (after) + " dB)");

    //--------------------------------------------------------------------------
    // O pedal e o excesso de vozes não passam pelo piano roll: aqui o MIDI vai direto ao plugin do canal
    // (sem placa de som no modo headless, nada mais chama o plugin). A tecla 70 não tem release na
    // biblioteca de teste, então o que soa depois de soltar é a nota segurada.
    section ("WHYNOT Piano: pedal, vozes e tempo real (MIDI direto no plugin)");

    if (auto* direct = app.getProjector().pianoFor (ch))
    {
        constexpr int block = 512;
        direct->initialise ({ te::TimePosition(), testRate, block });
        juce::AudioBuffer<float> buffer (2, block);
        te::MidiMessageArray midi;
        bool finite = true;

        auto run = [&] (int blocks, std::initializer_list<juce::MidiMessage> events, bool offline = true)
        {
            float peak = 0.0f;

            for (int b = 0; b < blocks; ++b)
            {
                buffer.clear();
                midi.clear();

                if (b == 0)
                    for (auto& m : events)
                        midi.addMidiMessage (m, 0.0, te::MPESourceID {});

                te::PluginRenderContext rc (&buffer, 0, block, &midi, 0.0, te::TimeRange(), true, false, offline, false);
                direct->applyToBuffer (rc);

                for (int chn = 0; chn < 2; ++chn)
                    for (int i = 0; i < block; ++i)
                        finite = finite && std::isfinite (buffer.getSample (chn, i));

                peak = buffer.getMagnitude (0, block);

                if (! offline)   // tempo real: o anel fica por conta da thread do streamer
                    std::this_thread::sleep_for (std::chrono::microseconds ((int) (1.0e6 * block / testRate)));
            }

            return peak;   // o pico do último bloco
        };

        using M = juce::MidiMessage;
        run (2, { M::controllerEvent (1, 123, 0) });

        run (10, { M::noteOn (1, 70, (juce::uint8) 100) });
        const float noPedal = run (40, { M::noteOff (1, 70) });
        check (noPedal < 1.0e-4f && direct->activeVoices() == 0, "sem o pedal, soltar a tecla abafa a nota em 0,2 s"_t);

        run (10, { M::controllerEvent (1, 64, 127), M::noteOn (1, 70, (juce::uint8) 100) });
        const float held = run (40, { M::noteOff (1, 70) });
        const float afterPedal = run (40, { M::controllerEvent (1, 64, 0) });
        check (held > 0.2f && afterPedal < 1.0e-4f,
               "com o pedal (CC64), a nota segue depois de soltar a tecla ("_t + juce::String (held, 2) + ") e some ao soltar o pedal");

        // 80 notas de uma vez, com o pedal: a polifonia fica em 64 e a sobra sai em 4 ms
        run (1, { M::controllerEvent (1, 64, 127) });
        midi.clear();
        buffer.clear();

        for (int n = 0; n < 80; ++n)
            midi.addMidiMessage (M::noteOn (1, 60 + n % 21, (juce::uint8) (40 + n)), n * 4.0 / testRate, te::MPESourceID {});

        te::PluginRenderContext crowd (&buffer, 0, block, &midi, 0.0, te::TimeRange(), true, false, true, false);
        direct->applyToBuffer (crowd);
        const int crowded = direct->activeVoices();
        run (10, {});
        const int settled = direct->activeVoices();
        check (crowded <= WhynotPianoPlugin::maxVoices && settled <= 56 && settled > 20 && finite,
               "80 notas de uma vez: "_t + juce::String (crowded) + " vozes no bloco, " + juce::String (settled) + " depois (limite 64, folga até 56)");

        const float silenced = run (3, { M::controllerEvent (1, 120, 0) });
        check (silenced < 1.0e-4f && direct->activeVoices() == 0, "All Sound Off (CC120) cala tudo na hora");
        run (1, { M::controllerEvent (1, 64, 0) });

        // tempo real: 1,5 s em blocos no ritmo da placa, com o streamer na thread dele
        const auto missedBefore = direct->underruns();
        run (1, { M::noteOn (1, 69, (juce::uint8) 100) }, false);
        const float live = run (128, {}, false);
        run (1, { M::noteOff (1, 69) }, false);
        check (live > 0.22f && direct->underruns() == missedBefore,
               "em tempo real, 1,5 s de nota sem faltar dado no anel (pico no fim "_t + juce::String (live, 2) + ", "
               + juce::String ((int) (direct->underruns() - missedBefore)) + " faltas)");
        check (direct->underruns() == 0 && finite, "nenhuma falta no anel nem valor inválido em todo o teste direto"_t);
        run (60, {});
    }
    else
    {
        check (false, "o canal do piano sumiu do projetor"_t);
    }

    //--------------------------------------------------------------------------
    section ("WHYNOT Piano: timbre, salvar, desfazer");
    const double grandSide = sideRatio (longA, 0.2, 2.0);
    check (app.run ("piano.setPreset", args ({ { "channel", ch }, { "preset", "lofi" } })).ok, "timbre Lo-fi");
    const auto lofi = render ({ pianoNote (0, 6, 69, 100) });
    const double lofiSide = sideRatio (lofi, 0.2, 2.0);
    check (grandSide > 0.9 && lofiSide < 0.45 * grandSide,
           "o Lo-fi estreita o estéreo: lado/meio "_t + juce::String (grandSide, 2) + " no Cauda, " + juce::String (lofiSide, 2) + " no Lo-fi");
    check (! app.run ("piano.setPreset", args ({ { "channel", ch }, { "preset", "nada" } })).ok, "timbre desconhecido é recusado"_t);
    check (app.run ("piano.listPresets").value["presets"].size() == 3, "três timbres na lista (Cauda, Suave, Lo-fi)"_t);

    const auto file = ctx.work.getChildFile ("piano.wnl");
    app.run ("project.save", args ({ { "path", file.getFullPathName() } }));
    const auto text = file.loadFileAsString();
    app.run ("project.new");
    app.run ("project.open", args ({ { "path", file.getFullPathName() } }));
    const auto* reopened = model.get().findChannel (ch);
    check (text.contains ("\"instrument\"") && reopened != nullptr && reopened->isPiano() && reopened->instrumentPreset == "lofi"
               && app.getProjector().pianoFor (ch) != nullptr,
           "salvar e reabrir: o canal volta como WHYNOT Piano Lo-fi"_t);

    const auto before = (int) model.get().channels.size();
    const auto second = app.run ("channel.addPiano", args ({ { "preset", "suave" } }));
    const auto* suave = model.get().findChannel (second.value["channel"].toString());
    check (second.ok && suave != nullptr && suave->name == "Piano Suave", "canal novo com o timbre Suave (\"Piano Suave\")"_t);
    app.run ("edit.undo");
    check ((int) model.get().channels.size() == before, "Ctrl+Z tira o canal do piano"_t);

    // um sample no lugar do piano: o canal vira sampler e a trilha troca de plugin
    app.run ("channel.setSample", args ({ { "channel", ch }, { "path", samples.getChildFile ("forte.wav").getFullPathName() }, { "detectRoot", false } }));
    const auto* swapped = model.get().findChannel (ch);
    check (swapped != nullptr && ! swapped->isPiano() && app.getProjector().pianoFor (ch) == nullptr,
           "trocar por um sample faz do piano um sampler"_t);

    //--------------------------------------------------------------------------
    section ("WHYNOT Piano: Salamander instalado");
    piano::setLibraryOverride ({});

    if (piano::findLibraryFolder() == juce::File())
    {
        ctx.rep.lines.add ("  -- o Salamander não está instalado nesta máquina: parte pulada"_t);
        ctx.rep.flush();
        return;
    }

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto real = app.run ("piano.status");
    const auto loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
    check (real.ok && (bool) real.value["found"] && (int) real.value["noteZones"] >= 200,
           real.value["name"].toString() + ": " + real.value["zones"].toString() + " zonas, "
           + real.value["memoryMB"].toString() + " MB na memória, carregou em " + juce::String (juce::roundToInt (loadMs)) + " ms");
    check (real.value["credit"].toString().contains ("CC BY"), "o crédito da licença CC BY está no manifesto"_t);

    app.run ("project.new");
    const auto grand = app.run ("channel.addPiano");
    const auto gch = grand.value["channel"].toString();
    app.run ("pattern.setLength", args ({ { "steps", 64 } }));
    app.run ("mixer.setSolo", args ({ { "track", grand.value["mixerTrack"].toString() }, { "on", true } }));
    app.run ("pianoroll.addNotes", args ({ { "channel", gch }, { "unit", "beats" },
                                           { "notes", juce::Array<juce::var> { pianoNote (0, 6, 60, 100), pianoNote (0, 6, 64, 100), pianoNote (0, 6, 67, 100),
                                                                               pianoNote (8, 6, 36, 127), pianoNote (8, 6, 96, 30) } } }));
    const auto chordFile = ctx.work.getChildFile ("piano_salamander.wav");
    const auto r0 = juce::Time::getMillisecondCounterHiRes();
    app.run ("project.exportWav", args ({ { "path", chordFile.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
    const auto renderMs = juce::Time::getMillisecondCounterHiRes() - r0;
    const auto chord = readWav (chordFile);
    const float peak = peakDb (chord);
    const float sustain = rmsDbIn (chord, 0, 1.5, 2.4);
    check (chord.valid && peak > -30.0f && peak < -1.0f, "acorde de Dó maior com força 100: pico "_t + db (peak) + " dBFS (soa e não clipa)");
    check (sustain > -60.0f, "o acorde continua soando depois de 1,5 s (o resto do sample veio do disco): "_t + db (sustain) + " dB");
    check (rmsDbIn (chord, 0, 8 * beat + 0.1, 8 * beat + 1.0) > -60.0f, "o grave com força 127 e o agudo com força 30 tocam"_t);
    ctx.rep.lines.add ("  -- render de 6,9 s com o Salamander em "_t + juce::String (juce::roundToInt (renderMs)) + " ms");
    ctx.rep.flush();
}

} // namespace whynot::selftest
