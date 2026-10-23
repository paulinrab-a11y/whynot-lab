// Autoteste da frente "Fatiar e 808" (pedidos de 21/10 à tarde, frente C): o 808 do kit nasce
// como o nativo, a nota mais longa que o sample, o fatiador e os kits. Dono: Sampler.

#include "SelfTestKit.h"
#include "../../engine/Mp3Encoder.h"
#include "../../engine/SampleAnalysis.h"
#include "../../model/WnlFormat.h"
#include "../../ui/ChannelRack.h"
#include "../../ui/SamplerPanel.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    constexpr double testRate = 48000.0;

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate = testRate)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                       .withNumChannels (audio.getNumChannels()).withBitsPerSample (24));
        return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    /** Um 808 de teste: seno com uma queda de tom curta no começo e um decaimento longo. */
    juce::AudioBuffer<float> make808 (double hz, double seconds, double decaySeconds = 0.9)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * testRate));
        double phase = 0.0;

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / testRate;
            const double drop = 12.0 * std::exp (-t / 0.012);   // +12 st que caem em ~30 ms
            phase += juce::MathConstants<double>::twoPi * hz * std::pow (2.0, drop / 12.0) / testRate;
            const double env = std::min (1.0, t / 0.002) * std::exp (-t / decaySeconds);
            b.setSample (0, i, (float) (0.8 * env * std::sin (phase)));
        }

        return b;
    }

    /** Um kick de teste: varredura de 150 a 50 Hz e decaimento curto. */
    juce::AudioBuffer<float> makeKick()
    {
        juce::AudioBuffer<float> b (1, (int) (0.4 * testRate));
        double phase = 0.0;

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / testRate;
            phase += juce::MathConstants<double>::twoPi * (50.0 + 100.0 * std::exp (-t / 0.03)) / testRate;
            b.setSample (0, i, (float) (0.9 * std::min (1.0, t / 0.001) * std::exp (-t / 0.07) * std::sin (phase)));
        }

        return b;
    }

    /** Ruído com decaimento rápido (palma, chimbal). */
    juce::AudioBuffer<float> makeNoise (double seconds, double decaySeconds, juce::int64 seed)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * testRate));
        juce::Random random (seed);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (float) (0.6 * std::exp (-(i / testRate) / decaySeconds) * (random.nextFloat() * 2.0f - 1.0f)));

        return b;
    }

    /** Maior salto entre duas amostras seguidas, no canal 0, entre t0 e t1 segundos. */
    float maxJump (const Audio& a, double t0, double t1)
    {
        float jump = 0.0f;
        const int from = juce::jmax (1, (int) (t0 * a.sampleRate)), to = juce::jmin (a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));

        for (int i = from; i < to; ++i)
            jump = juce::jmax (jump, std::abs (a.buffer.getSample (0, i) - a.buffer.getSample (0, i - 1)));

        return jump;
    }

    /** Um beat de teste a 120 BPM, 2 compassos (4 s): kick nos tempos 1 e 3, caixa no 2 e no 4, chimbal
        nos contratempos de colcheia. 16 batidas, uma a cada 0,25 s, com silêncio entre elas. */
    juce::AudioBuffer<float> makeBeat()
    {
        juce::AudioBuffer<float> b (1, (int) (4.0 * testRate));
        b.clear();
        juce::Random random (120);

        for (int hit = 0; hit < 16; ++hit)
        {
            const int start = juce::roundToInt (0.25 * hit * testRate);
            const int kind = hit % 2 == 1 ? 2 : (hit % 4 == 0 ? 0 : 1);   // 0 kick, 1 caixa, 2 chimbal
            double phase = 0.0;

            for (int j = 0; j < (int) (0.2 * testRate) && start + j < b.getNumSamples(); ++j)
            {
                const double t = j / testRate;
                float v;

                if (kind == 0)
                {
                    phase += juce::MathConstants<double>::twoPi * (50.0 + 100.0 * std::exp (-t / 0.03)) / testRate;
                    v = (float) (0.9 * std::exp (-t / 0.05) * std::sin (phase));
                }
                else
                {
                    v = (float) ((kind == 1 ? 0.6 * std::exp (-t / 0.04) : 0.25 * std::exp (-t / 0.015)) * (random.nextFloat() * 2.0 - 1.0));
                }

                b.addSample (0, start + j, v);
            }
        }

        return b;
    }

    /** Correlação entre `a` de t0 a t0 + len e `b` de u0 a u0 + len (segundos), no canal 0. */
    double correlate (const Audio& a, double t0, const Audio& b, double u0, double len)
    {
        const int n = juce::roundToInt (len * a.sampleRate);
        const int ia = juce::roundToInt (t0 * a.sampleRate), ib = juce::roundToInt (u0 * b.sampleRate);
        double ab = 0, aa = 0, bb = 0;

        for (int i = 0; i < n && ia + i < a.buffer.getNumSamples() && ib + i < b.buffer.getNumSamples(); ++i)
        {
            const double x = a.buffer.getSample (0, ia + i), y = b.buffer.getSample (0, ib + i);
            ab += x * y; aa += x * x; bb += y * y;
        }

        return aa > 0 && bb > 0 ? ab / std::sqrt (aa * bb) : 0.0;
    }

    /** Primeira amostra depois de `from` segundos que passa de `level`, em segundos (-1 se não há). */
    double onsetAfter (const Audio& a, double from, float level = 0.01f)
    {
        for (int i = juce::jmax (0, (int) (from * a.sampleRate)); i < a.buffer.getNumSamples(); ++i)
            if (std::abs (a.buffer.getSample (0, i)) > level)
                return i / a.sampleRate;

        return -1.0;
    }
}

void runSlicingSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto kit = ctx.work.getChildFile ("kit_fatiar");
    kit.deleteRecursively();

    const auto dark = kit.getChildFile ("808s").getChildFile ("Dark.wav");            // 808 pela pasta
    const auto plain = kit.getChildFile ("Sons").getChildFile ("Escuro 01.wav");       // 808 só pelo som
    const auto punch = kit.getChildFile ("Kicks").getChildFile ("Punch.wav");          // kick pela pasta
    const auto shortHit = kit.getChildFile ("Sons").getChildFile ("Batida Curta.wav"); // kick sem nome: curto, não é 808
    const auto clap = kit.getChildFile ("808 Clap.wav");                               // "808" no nome, mas é palma

    //--------------------------------------------------------------------------
    rep.section ("Fatiar e 808: o 808 do kit nasce como o nativo"_t);
    rep.check (writeWav (dark, make808 (49.0, 1.6)) && writeWav (plain, make808 (49.0, 1.6)) && writeWav (punch, makeKick())
                   && writeWav (shortHit, makeKick()) && writeWav (clap, makeNoise (0.25, 0.05, 808)),
               "kit de teste: dois 808 (G1), dois kicks e uma palma"_t);

    app.run ("project.new");

    auto findByName = [&] (const juce::String& name) -> const Channel*
    {
        for (auto& c : model.get().channels)
            if (c.name == name)
                return &c;

        return nullptr;
    };

    {
        // Arrastar para o channel rack (o mesmo caminho do Explorer): o rack fica fora da tela.
        juce::Component host;
        ChannelRack rack (app);
        host.setSize (900, 500);
        host.addAndMakeVisible (rack);
        rack.setBounds (host.getLocalBounds());
        rack.refresh (model.get());
        rack.filesDropped (juce::StringArray { dark.getFullPathName() }, 400, 480);

        const auto* c = findByName ("Dark");
        rep.check (c != nullptr && c->mono && ! c->oneShot && c->glideMs > 0.0f && ! c->cutItself,
                   "arrastado para o rack, o 808 da pasta 808s nasce mono, com glide de "_t
                       + (c != nullptr ? juce::String (c->glideMs, 0) : juce::String ("?")) + " ms e respeitando a duração da nota");
        host.removeChildComponent (&rack);
    }

    const auto byAnalysis = app.run ("channel.addSampler", args ({ { "path", plain.getFullPathName() } }));
    const auto bassId = byAnalysis.value["channel"].toString();
    {
        const auto* c = model.get().findChannel (bassId);
        rep.check (byAnalysis.ok && byAnalysis.value["role"].toString() == "808" && c != nullptr && c->mono && ! c->oneShot,
                   "um 808 sem nome que diga (\"Escuro 01\") vira 808 pelo som: grave, longo e sustentado"_t);
        rep.check (c != nullptr && c->rootKey == 31, "e a nota dele cai em G1 (nota "_t + (c != nullptr ? juce::String (c->rootKey) : "?") + ")");
    }

    {
        const auto r = app.run ("channel.addSampler", args ({ { "path", punch.getFullPathName() } }));
        const auto* c = model.get().findChannel (r.value["channel"].toString());
        rep.check (r.ok && r.value["role"].toString() == "kick" && c != nullptr && c->oneShot && ! c->mono,
                   "o kick da pasta Kicks continua one-shot, sem mono"_t);
    }
    {
        const auto r = app.run ("channel.addSampler", args ({ { "path", shortHit.getFullPathName() } }));
        const auto* c = model.get().findChannel (r.value["channel"].toString());
        rep.check (r.ok && r.value["role"].toString().isEmpty() && c != nullptr && c->oneShot && ! c->mono,
                   "um kick sem nome que diga é grave, mas curto: continua one-shot"_t);
    }
    {
        const auto r = app.run ("channel.addSampler", args ({ { "path", clap.getFullPathName() } }));
        const auto* c = model.get().findChannel (r.value["channel"].toString());
        rep.check (r.ok && r.value["role"].toString() == "clap" && c != nullptr && c->oneShot && ! c->mono,
                   "\"808 Clap\" é palma, não baixo: one-shot"_t);
    }
    {
        const auto r = app.run ("channel.addSampler", args ({ { "path", plain.getFullPathName() }, { "name", "808 fixo" }, { "mono", false }, { "oneShot", true } }));
        const auto* c = model.get().findChannel (r.value["channel"].toString());
        rep.check (r.ok && c != nullptr && ! c->mono && c->oneShot, "mono e oneShot dados no comando vencem o papel"_t);
    }

    // O glide se ouve: G1 desliza para D2 (73,4 Hz) sem reiniciar o sample.
    {
        const auto* c = model.get().findChannel (bassId);
        app.run ("mixer.setSolo", args ({ { "track", c != nullptr ? c->output : juce::String() }, { "on", true } }));
        addNotes (app, bassId, { noteVar (0.0, 1.0, 31, false), noteVar (1.0, 1.0, 38, true) });

        const auto f = ctx.work.getChildFile ("fatiar_glide.wav");
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        const auto out = readWav (f);

        const double before = measureHz (out, 0.15, 0.40);
        const double after = measureHz (out, beat140 + 0.15, beat140 + 0.40);
        rep.check (std::abs (before - 49.0) < 1.0 && std::abs (after - 73.4) < 1.5,
                   "slide de G1 para D2: "_t + juce::String (before, 1) + " Hz e depois " + juce::String (after, 1) + " Hz (49,0 e 73,4)");

        // Sem reiniciar: o volume continua caindo (um sample novo voltaria ao pico) e nada estala.
        const float head = peakIn (out, 0.03, 0.15), slid = peakIn (out, beat140 + 0.01, beat140 + 0.15);
        rep.check (head > 0.1f && slid < head * 0.85f, "o slide continua o mesmo som, sem voltar ao ataque (pico "_t
                                                           + juce::String (slid / juce::jmax (head, 1.0e-6f), 2) + " do começo)");
        const float jump = maxJump (out, beat140 - 0.02, beat140 + 0.15);
        rep.check (jump < 0.05f, "e sem estalo na passagem (maior salto "_t + juce::String (jump, 4) + ")");
    }

    //--------------------------------------------------------------------------
    // Nota de 3 s num 808 de 1,6 s: sem laço o som acaba no fim do arquivo; com o laço achado
    // sozinho, continua até a nota acabar, sem estalo nas voltas.
    rep.section ("Fatiar e 808: nota mais longa que o sample (laço)"_t);
    {
        app.run ("project.new");
        // A raiz dada (G1, sem cents): o .wnl guarda os cents com uma casa, e a comparação depois de reabrir é amostra a amostra.
        const auto added = app.run ("channel.addSampler", args ({ { "path", dark.getFullPathName() }, { "name", "808 longo" }, { "rootKey", 31 } }));
        const auto id = added.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));
        app.run ("pattern.setLength", args ({ { "steps", 32 } }));   // 2 compassos: 3,43 s a 140 BPM
        addNotes (app, id, { noteVar (0.0, 7.0, 31, false) });        // 7 tempos = 3,0 s

        int renders = 0;
        auto render = [&]
        {
            const auto f = ctx.work.getChildFile ("fatiar_laco_" + juce::String (++renders) + ".wav");
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            return readWav (f);
        };

        const double noteEnd = 7.0 * beat140, sampleEnd = 1.6;
        const auto plainOut = render();
        rep.check (plainOut.valid && peakIn (plainOut, 0.1, 0.5) > 0.1f && peakIn (plainOut, sampleEnd + 0.05, noteEnd - 0.05) < 1.0e-4f,
                   "sem laço, a nota de 3 s fica muda depois do fim do sample (1,6 s)"_t);

        const auto found = app.run ("channel.setSampler", args ({ { "channel", id }, { "loopAuto", true } }));
        const double a = (double) found.value["loopStart"] * sampleEnd, b = (double) found.value["loopEnd"] * sampleEnd;
        const double xfade = (double) found.value["loopXfadeMs"] / 1000.0;
        rep.check (found.ok && (bool) found.value["loop"] && a > 0.12 && b > a + 0.05 && b < sampleEnd,
                   "Achar laço: de "_t + juce::String (a, 3) + " s a " + juce::String (b, 3) + " s, crossfade de "
                       + juce::String (xfade * 1000.0, 1) + " ms (depois do ataque, dentro do sample)");

        const auto looped = render();
        const float late = peakIn (looped, noteEnd - 0.5, noteEnd - 0.05);
        rep.check (late > 0.05f, "com laço, a nota soa até o fim: de 2,5 a 2,95 s o pico é "_t
                                     + juce::String (juce::Decibels::gainToDecibels (late), 1) + " dBFS");
        rep.check (peakIn (looped, noteEnd + 0.05, noteEnd + 0.3) < 1.0e-4f, "e para quando a nota acaba (release de 30 ms)"_t);

        // Salto medido: o maior salto entre amostras nas voltas do laço contra o do próprio 808 no trecho do laço.
        const float natural = maxJump (plainOut, a - 0.02, b);
        const float jump = maxJump (looped, b - xfade - 0.01, noteEnd - 0.02);
        rep.check (natural > 0.0f && jump <= natural * 1.25f, "o laço não estala: maior salto nas voltas "_t + juce::String (jump, 5)
                                                                   + ", o do próprio sample no trecho " + juce::String (natural, 5));
        rep.check (std::abs (measureHz (looped, 2.0, 2.9) - 49.0) < 1.0, "o tom no laço continua G1: "_t
                                                                             + juce::String (measureHz (looped, 2.0, 2.9), 2) + " Hz");
        float headDiff = 0.0f;
        for (int ch = 0; ch < juce::jmin (plainOut.buffer.getNumChannels(), looped.buffer.getNumChannels()); ++ch)
            for (int i = 0; i < juce::jmin (looped.buffer.getNumSamples(), (int) ((b - xfade - 0.005) * looped.sampleRate)); ++i)
                headDiff = juce::jmax (headDiff, std::abs (plainOut.buffer.getSample (ch, i) - looped.buffer.getSample (ch, i)));
        rep.check (headDiff < 1.0e-6f, "até o crossfade da primeira volta, o som é o mesmo de sem laço (diferença "_t
                                           + juce::String (headDiff, 7) + ")");

        // .wnl e desfazer
        const auto file = ctx.work.getChildFile ("Laco Teste").getChildFile ("Laco Teste.wnl");
        const auto* before = model.get().findChannel (id);
        const auto saved = before != nullptr ? *before : Channel {};
        rep.check (app.run ("project.save", args ({ { "path", file.getFullPathName() } })).ok
                       && file.loadFileAsString().contains ("\"loopStartPpm\""), "o laço vai para o .wnl (as pontas em milionésimos)"_t);
        app.run ("project.new");
        app.run ("project.open", args ({ { "path", file.getFullPathName() } }));
        const auto* back = model.get().findChannel (id);
        rep.check (back != nullptr && back->loop && back->loopStart == saved.loopStart && back->loopEnd == saved.loopEnd
                       && back->loopXfadeMs == saved.loopXfadeMs,
                   "e volta igual ao reabrir"_t);
        rep.check (maxDiff (looped, render()) < 1.0e-4f, "o canal reaberto soa igual"_t);

        app.run ("channel.setSampler", args ({ { "channel", id }, { "loop", false } }));
        app.run ("edit.undo");
        rep.check (model.get().findChannel (id) != nullptr && model.get().findChannel (id)->loop, "desfazer volta o laço"_t);

        // O painel com o grupo LAÇO e a onda, aberto fora da tela.
        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1200, 700);
        auto& box = SamplerPanel::show (app, id, { 40, 40, 160, 24 }, nullptr, &host);
        const auto shot = ctx.work.getChildFile ("fatiar-laco-painel.png");
        const auto image = juce::SoftwareImageType().convert (box.createComponentSnapshot (box.getLocalBounds(), true, 1.0f));
        shot.deleteFile();
        bool written = false;

        if (juce::FileOutputStream out (shot); out.openedOk())
        {
            juce::PNGImageFormat png;
            written = png.writeImageToStream (image, out);
        }

        rep.check (written && image.getWidth() >= 800, "captura do painel com o laço em "_t + shot.getFullPathName()
                                                          + " (" + juce::String (image.getWidth()) + " px)");
        box.dismiss();

        for (int i = 0; i < 40 && host.getNumChildComponents() > 0; ++i)   // o CallOutBox fecha por mensagem
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        host.setLookAndFeel (nullptr);
    }

    //--------------------------------------------------------------------------
    // O fatiador: um beat de 16 batidas vira 16 fatias, uma por tecla a partir do C4, e as notas
    // no piano roll tocam o beat de novo no andamento do projeto.
    rep.section ("Fatiar e 808: o fatiador (fatias nas teclas e no piano roll)"_t);
    const auto beatFile = kit.getChildFile ("Beat Teste 120bpm.wav");
    rep.check (writeWav (beatFile, makeBeat()), "beat de teste: 16 batidas a 120 BPM, 2 compassos"_t);
    const auto original = readWav (beatFile);
    {
        app.run ("project.new");
        app.run ("transport.setBpm", args ({ { "bpm", 120 } }));
        const auto sliced = app.run ("channel.addSlicer", args ({ { "path", beatFile.getFullPathName() } }));
        const auto id = sliced.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", sliced.value["mixerTrack"].toString() }, { "on", true } }));

        rep.check (sliced.ok && (int) sliced.value["slices"] == 16 && (int) sliced.value["firstKey"] == 60 && (int) sliced.value["lastKey"] == 75
                       && std::abs ((double) sliced.value["bpm"] - 120.0) < 0.01,
                   "channel.addSlicer: "_t + sliced.value["slices"].toString() + " fatias nas teclas C4 a " + Project::keyName ((int) sliced.value["lastKey"])
                       + ", " + sliced.value["bpm"].toString() + " BPM (do nome do arquivo)");

        const auto* ch = model.get().findChannel (id);
        double worst = 0.0;

        if (ch != nullptr && ch->slices.size() == 16)
            for (size_t i = 0; i < 16; ++i)
                worst = juce::jmax (worst, std::abs (ch->slices[i] * (original.buffer.getNumSamples() - 1) / testRate - 0.25 * (double) i) * 1000.0);

        rep.check (ch != nullptr && ch->slices.size() == 16 && worst <= 3.0, "cada fatia começa na batida: a mais longe a "_t + juce::String (worst, 2) + " ms");

        const auto* pat = model.get().currentPatternPtr();
        const auto* notes = pat != nullptr ? pat->notesFor (id) : nullptr;
        bool keysOk = notes != nullptr && notes->size() == 16;

        for (size_t i = 0; keysOk && i < 16; ++i)
            keysOk = (*notes)[i].key == 60 + (int) i && std::abs ((*notes)[i].start - (int) i * ticksPerBeat / 2) <= 2;

        rep.check (keysOk && pat->lengthSteps >= 32, "no piano roll: uma nota por fatia, da grave para a aguda, cada uma no tempo da batida (padrão com "_t
                                                        + juce::String (pat != nullptr ? pat->lengthSteps : 0) + " passos)");

        auto render = [&] (const char* name)
        {
            const auto f = ctx.work.getChildFile (name);
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            return readWav (f);
        };

        const auto same = render ("fatiar_beat_120.wav");
        const double corr = correlate (same, 0.0, original, 0.0, 3.9);
        rep.check (corr > 0.99, "as notas tocam o beat de novo a 120 BPM: correlação "_t + juce::String (corr, 4) + " com o original");

        // A tecla certa: só a nota F4 (fatia 5, o chimbal de 1,25 s).
        app.run ("pianoroll.clear", args ({ { "channel", id } }));
        addNotes (app, id, { noteVar (0.0, 0.5, 65, false) });
        const auto one = render ("fatiar_tecla_65.wav");
        const double corrOne = correlate (one, 0.0, original, 1.25, 0.2);
        rep.check (corrOne > 0.99 && peakIn (one, 0.26, 1.0) < 1.0e-4f, "a tecla F4 toca só a fatia 5 (correlação "_t + juce::String (corrOne, 4)
                                                                            + "), e para no fim dela");

        app.run ("pianoroll.clear", args ({ { "channel", id } }));
        addNotes (app, id, { noteVar (0.0, 0.5, 59, false), noteVar (1.0, 0.5, 76, false) });
        rep.check (peakDb (render ("fatiar_fora.wav")) < -100.0f, "teclas fora das fatias (B3, E5) não tocam nada"_t);

        // No andamento do projeto: a 140 BPM a fatia 4 (tempo 3) entra em 2 tempos de 140.
        app.run ("channel.slicesToPianoRoll", args ({ { "channel", id } }));
        app.run ("transport.setBpm", args ({ { "bpm", 140 } }));
        const auto faster = render ("fatiar_beat_140.wav");
        const double at = onsetAfter (faster, 2.0 * beat140 - 0.01);
        rep.check (std::abs (at - 2.0 * beat140) < 0.002, "a 140 BPM as fatias seguem o projeto: a fatia 4 entra em "_t + juce::String (at, 4)
                                                             + " s (" + juce::String (2.0 * beat140, 4) + ")");

        const auto grid = app.run ("channel.slice", args ({ { "channel", id }, { "mode", "grid" }, { "division", "1/16" } }));
        rep.check (grid.ok && (int) grid.value["slices"] == 32, "pela grade de 1/16: "_t + grid.value["slices"].toString() + " fatias (32)");

        const auto json = wnl::toJson (model.get());
        Project back;
        rep.check (wnl::fromJson (json, back).wasOk() && back.findChannel (id) != nullptr && back.findChannel (id)->slices == model.get().findChannel (id)->slices
                       && back.findChannel (id)->sliceKey == 60,
                   "as fatias vão e voltam do .wnl"_t);

        app.run ("edit.undo");
        rep.check (model.get().findChannel (id) != nullptr && model.get().findChannel (id)->slices.size() == 16, "desfazer volta as 16 fatias"_t);

        const auto none = app.run ("channel.slice", args ({ { "channel", id }, { "mode", "none" } }));
        rep.check (none.ok && model.get().findChannel (id)->slices.empty(), "\"none\" tira as fatias: o sample inteiro em qualquer tecla"_t);
        app.run ("edit.undo");

        // O painel mostra as fatias na onda.
        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1200, 700);
        auto& box = SamplerPanel::show (app, id, { 40, 40, 160, 24 }, nullptr, &host);
        const auto shot = ctx.work.getChildFile ("fatiar-fatias-painel.png");
        const auto image = juce::SoftwareImageType().convert (box.createComponentSnapshot (box.getLocalBounds(), true, 1.0f));
        shot.deleteFile();
        bool written = false;

        if (juce::FileOutputStream out (shot); out.openedOk())
        {
            juce::PNGImageFormat png;
            written = png.writeImageToStream (image, out);
        }

        rep.check (written, "captura do painel com as fatias em "_t + shot.getFullPathName());
        box.dismiss();

        for (int i = 0; i < 40 && host.getNumChildComponents() > 0; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        host.setLookAndFeel (nullptr);
    }

    //--------------------------------------------------------------------------
    rep.section ("Fatiar e 808: picotar o clipe de áudio da playlist"_t);
    {
        app.run ("project.new");
        app.run ("playlist.clear");
        app.run ("transport.setBpm", args ({ { "bpm", 120 } }));

        // Uma cópia sem número no nome: com "120bpm" no nome, o Tracktion deduz o andamento do clipe e
        // o export da música trava (bug anterior a esta frente, avisado ao núcleo em 21/10 19:15).
        const auto clipFile = kit.getChildFile ("Beat Teste Clipe.wav");
        beatFile.copyFileTo (clipFile);
        const auto added = app.run ("playlist.addItem", args ({ { "path", clipFile.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
        const auto itemId = added.value["item"].toString();

        auto renderSong = [&] (const char* name)
        {
            const auto f = ctx.work.getChildFile (name);
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
            return readWav (f);
        };

        const auto whole = renderSong ("fatiar_clipe_inteiro.wav");
        const auto chop = app.run ("playlist.chopItem", args ({ { "item", itemId }, { "mode", "attack" } }));
        const int pieces = chop.ok ? chop.value["items"].size() : 0;
        bool startsOk = pieces == 16;

        for (int i = 0; startsOk && i < 16; ++i)
        {
            const auto* it = model.get().findItem (chop.value["items"][i].toString());
            startsOk = it != nullptr && std::abs (it->start - i * ticksPerBeat / 2) <= 2
                       && std::abs (it->offsetSeconds - 0.25 * i) < 0.004;
        }

        rep.check (startsOk, "\"Pelas batidas\": o clipe virou "_t + juce::String (pieces) + " blocos, cada um na sua batida (16)");

        const auto chopped = renderSong ("fatiar_clipe_picotado.wav");
        const float diff = maxDiff (whole, chopped);
        rep.check (diff < 0.01f, "picotado, soa como antes (maior diferença "_t + juce::String (diff, 5) + ")");

        app.run ("edit.undo");
        const auto bars = app.run ("playlist.chopItem", args ({ { "item", itemId }, { "mode", "bar" } }));
        rep.check (bars.ok && bars.value["items"].size() == 2, "\"A cada compasso\": 2 blocos num clipe de 2 compassos"_t);
    }

    //--------------------------------------------------------------------------
    // Uma pasta de kit inteira no rack, com o papel de cada som, e um MP3 de kit sem o atraso do codificador.
    rep.section ("Fatiar e 808: kits (pasta inteira e MP3 sem atraso)"_t);
    {
        app.run ("project.new");
        const auto kit2 = ctx.work.getChildFile ("Kit Trap 808");   // o nome da pasta do kit não decide o papel
        kit2.deleteRecursively();
        const bool written = writeWav (kit2.getChildFile ("Kicks/Kick Hard.wav"), makeKick())
                          && writeWav (kit2.getChildFile ("Snares/Snare Tight.wav"), makeNoise (0.3, 0.06, 1))
                          && writeWav (kit2.getChildFile ("Hats/Hat 01.wav"), makeNoise (0.1, 0.02, 2))
                          && writeWav (kit2.getChildFile ("Hats/Open Hat 01.wav"), makeNoise (0.6, 0.2, 3))
                          && writeWav (kit2.getChildFile ("808s/Zay.wav"), make808 (49.0, 1.5))
                          && writeWav (kit2.getChildFile ("Perc/Shaker.wav"), makeNoise (0.15, 0.03, 4))
                          && writeWav (kit2.getChildFile ("Sons/Escuro 02.wav"), make808 (55.0, 1.4))
                          && kit2.getChildFile ("Leia-me.txt").replaceWithText ("não é som");
        rep.check (written, "kit de teste em pastas: Kicks, Snares, Hats, 808s, Perc e Sons"_t);

        const int before = (int) model.get().channels.size();
        {
            juce::Component host;
            ChannelRack rack (app);
            host.setSize (900, 700);
            host.addAndMakeVisible (rack);
            rack.setBounds (host.getLocalBounds());
            rack.refresh (model.get());
            rep.check (rack.isInterestedInFileDrag ({ kit2.getFullPathName() }), "o rack aceita soltar uma pasta"_t);
            rack.filesDropped ({ kit2.getFullPathName() }, 400, 680);
            rack.refresh (model.get());

            const auto shot = ctx.work.getChildFile ("fatiar-kit-rack.png");
            const auto image = juce::SoftwareImageType().convert (rack.createComponentSnapshot (rack.getLocalBounds(), true, 1.0f));
            shot.deleteFile();

            if (juce::FileOutputStream out (shot); out.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (image, out);
            }

            host.removeChildComponent (&rack);
        }

        const auto& chans = model.get().channels;
        juce::StringArray names;

        for (int i = before; i < (int) chans.size(); ++i)
            names.add (chans[(size_t) i].name);

        rep.check (names.joinIntoString (", ") == "Kick Hard, Snare Tight, Hat 01, Open Hat 01, Shaker, Zay, Escuro 02",
                   "a pasta virou "_t + juce::String (names.size()) + " canais, na ordem da bateria: " + names.joinIntoString (", "));

        auto find = [&] (const juce::String& name) -> const Channel*
        {
            for (int i = before; i < (int) chans.size(); ++i)
                if (chans[(size_t) i].name == name)
                    return &chans[(size_t) i];

            return nullptr;
        };

        const auto* zay = find ("Zay");
        const auto* dark2 = find ("Escuro 02");
        const auto* hat = find ("Hat 01");
        const auto* open = find ("Open Hat 01");
        const auto* kick = find ("Kick Hard");
        rep.check (zay != nullptr && dark2 != nullptr && zay->mono && ! zay->oneShot && dark2->mono && ! dark2->oneShot
                       && zay->rootKey == 31 && dark2->rootKey == 33,
                   "os dois 808 (pela pasta e pelo som) nascem mono, com glide e respeitando a nota, em G1 e A1"_t);
        rep.check (hat != nullptr && open != nullptr && kick != nullptr && hat->chokeGroup == 1 && open->chokeGroup == 1 && open->cutItself
                       && kick->chokeGroup == 0 && kick->oneShot && kick->rootKey == 60,
                   "chimbal fechado e aberto no grupo de corte 1 (o aberto corta a si mesmo); o kick fica one-shot em C4"_t);
        rep.check (true, "captura do rack com o kit em "_t + ctx.work.getChildFile ("fatiar-kit-rack.png").getFullPathName());

        // O MP3 do kit: o one-shot entra na hora, como o WAV (o atraso do codificador sai na leitura).
        if (mp3EncoderAvailable())
        {
            const auto kickWav = kit2.getChildFile ("Kicks/Kick Hard.wav");
            const auto kickMp3 = ctx.work.getChildFile ("kit_mp3").getChildFile ("Kick Hard.mp3");
            kickMp3.getParentDirectory().createDirectory();
            const auto encoded = encodeMp3 (kickWav, kickMp3, Mp3Settings {});
            const int priming = mp3PrimingFrames (kickMp3);
            rep.check (encoded.wasOk() && priming > 0, "MP3 do kick feito pelo LAME: a etiqueta diz "_t + juce::String (priming)
                                                           + " amostras de atraso (" + juce::String (priming / testRate * 1000.0, 1) + " ms)");

            auto onsetOf = [&] (const juce::File& file, const juce::String& name)
            {
                app.run ("project.new");
                const auto added = app.run ("channel.addSampler", args ({ { "path", file.getFullPathName() }, { "name", name }, { "detectRoot", false } }));
                const auto chId = added.value["channel"].toString();
                app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));
                setGrid (app, chId, "x...............");
                const auto f = ctx.work.getChildFile ("fatiar_onset_" + name + ".wav");
                app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
                return readWav (f);
            };

            const auto fromWav = onsetOf (kickWav, "wav"), fromMp3 = onsetOf (kickMp3, "mp3");
            const double delayMs = (onsetAfter (fromMp3, 0.0) - onsetAfter (fromWav, 0.0)) * 1000.0;
            rep.check (onsetAfter (fromWav, 0.0) >= 0.0 && std::abs (delayMs) < 1.0,
                       "o one-shot em MP3 entra junto com o WAV: "_t + juce::String (delayMs, 2) + " ms de diferença (sem o corte seriam "
                           + juce::String (priming / testRate * 1000.0, 1) + " ms)");
            const double corr = correlate (fromMp3, 0.0, fromWav, 0.0, 0.1);
            rep.check (corr > 0.98, "e o ataque é o mesmo: correlação "_t + juce::String (corr, 4) + " nos primeiros 100 ms");
        }
        else
        {
            rep.check (false, "app compilado sem o LAME: o teste do MP3 de kit não rodou"_t);
        }
    }

    //--------------------------------------------------------------------------
    // Revisão de 22/10 (fatia 5): o sampler sem aliasing acima do tom, o slide com outra força sem
    // estalo, o release no knob, a bateria sem a rampa de ataque e o beat em MP3 na playlist no tempo.
    rep.section ("Fatiar e 808: o sampler mais limpo (revisão de 22/10)"_t);
    {
        auto renderPattern = [&] (const juce::String& name)
        {
            const auto f = ctx.work.getChildFile (name);
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            return readWav (f);
        };

        auto rmsIn = [] (const Audio& a, double t0, double t1)
        {
            double sum = 0.0;
            int n = 0;

            for (int i = juce::jmax (0, (int) (t0 * a.sampleRate)); i < juce::jmin (a.buffer.getNumSamples(), (int) (t1 * a.sampleRate)); ++i, ++n)
                sum += (double) a.buffer.getSample (0, i) * a.buffer.getSample (0, i);

            return n > 0 ? std::sqrt (sum / n) : 0.0;
        };

        auto soloSampler = [&] (const juce::File& file, const juce::String& name)
        {
            app.run ("project.new");
            const auto added = app.run ("channel.addSampler", args ({ { "path", file.getFullPathName() }, { "name", name }, { "detectRoot", false } }));
            app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));
            return added.value["channel"].toString();
        };

        // 1. Um agudo de 18 kHz sete semitons acima (27 kHz, acima do Nyquist) não pode dobrar para baixo.
        {
            const auto file = ctx.work.getChildFile ("fatiar_agudo_18k.wav");
            juce::AudioBuffer<float> tone (1, (int) (1.0 * testRate));

            for (int i = 0; i < tone.getNumSamples(); ++i)
                tone.setSample (0, i, (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 18000.0 * i / testRate)));

            writeWav (file, tone);
            const auto id = soloSampler (file, "Agudo");
            addNotes (app, id, { noteVar (0.0, 2.0, 60, false) });
            const double root = rmsIn (renderPattern ("fatiar_agudo_raiz.wav"), 0.1, 0.8);
            app.run ("pianoroll.clear", args ({ { "channel", id } }));
            addNotes (app, id, { noteVar (0.0, 2.0, 67, false) });
            const double up = rmsIn (renderPattern ("fatiar_agudo_quinta.wav"), 0.1, 0.8);
            const double db = 20.0 * std::log10 (juce::jmax (up, 1.0e-12) / juce::jmax (root, 1.0e-12));
            rep.check (root > 0.01 && db < -60.0, "18 kHz sete semitons acima não dobra: sobra "_t + juce::String (db, 1)
                                                       + " dB do som no tom original (antes, só o Hermite: -2 dB)");
        }

        // 2. Um sample de 96 kHz com 30 kHz (inaudível) tocado no tom original numa saída de 48: some.
        {
            const auto file = ctx.work.getChildFile ("fatiar_96k.wav");
            juce::AudioBuffer<float> tone (1, (int) (1.0 * 96000.0));

            for (int i = 0; i < tone.getNumSamples(); ++i)
                tone.setSample (0, i, (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 30000.0 * i / 96000.0)
                                                + 0.2 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 96000.0)));

            writeWav (file, tone, 96000.0);
            const auto id = soloSampler (file, "Noventa e seis");
            addNotes (app, id, { noteVar (0.0, 2.0, 60, false) });
            const auto out = renderPattern ("fatiar_96k_render.wav");
            const double hz = measureHz (out, 0.1, 0.8);
            const double level = rmsIn (out, 0.1, 0.8);
            const double expected = 0.2 / std::sqrt (2.0) * 0.5012 * (110.0 / 127.0);   // a trilha a -6 dB e a força 110 da nota
            rep.check (std::abs (hz - 1000.0) < 1.0 && std::abs (20.0 * std::log10 (level / expected)) < 0.5,
                       "sample de 96 kHz a 48: só o 1 kHz sai ("_t + juce::String (hz, 1) + " Hz, no nível dele); o 30 kHz não dobra para 18 kHz");
        }

        // 3. O release no knob: um 808 respeitando a nota soa 400 ms depois do fim dela com release de 400 ms.
        {
            const auto id = soloSampler (dark, "808 release");
            addNotes (app, id, { noteVar (0.0, 1.0, 31, false) });
            const double end = beat140;
            const double shortTail = rmsIn (renderPattern ("fatiar_release_30.wav"), end + 0.10, end + 0.20);
            rep.check (app.run ("channel.setSampler", args ({ { "channel", id }, { "releaseMs", 400.0 } })).ok, "release de 400 ms"_t);
            const double longTail = rmsIn (renderPattern ("fatiar_release_400.wav"), end + 0.10, end + 0.20);
            rep.check (shortTail < 1.0e-4 && longTail > 0.01, "com 30 ms o 808 some logo depois da nota; com 400 ms ainda soa ("_t
                                                                 + juce::String (20.0 * std::log10 (longTail), 1) + " dBFS de 100 a 200 ms depois)");

            const auto json = wnl::toJson (model.get());
            Project back;
            rep.check (json.contains ("\"releaseMs\": 400.0") && wnl::fromJson (json, back).wasOk() && back.findChannel (id) != nullptr
                           && back.findChannel (id)->releaseMs == 400.0f,
                       "o release vai e volta do .wnl (opcional: só grava quando muda)"_t);
        }

        // 4. A bateria nasce sem a rampa de ataque (o clique do kick é o ataque dele); o 808, com ela.
        {
            app.run ("project.new");
            const auto kickAdded = app.run ("channel.addSampler", args ({ { "path", punch.getFullPathName() } }));
            const auto bassAdded = app.run ("channel.addSampler", args ({ { "path", dark.getFullPathName() } }));
            const auto* k = model.get().findChannel (kickAdded.value["channel"].toString());
            const auto* b = model.get().findChannel (bassAdded.value["channel"].toString());
            rep.check (k != nullptr && b != nullptr && k->attackMs == 0.0f && b->attackMs == 1.0f,
                       "o kick nasce com ataque 0 ms; o 808, com a rampa de 1 ms"_t);
        }

        // 5. O slide de um 808 com outra força não estala (a força chega numa rampa de 5 ms).
        {
            const auto id = soloSampler (dark, "808 forca");
            auto note = [] (double t, double len, int key, bool slide, int vel)
            {
                auto v = noteVar (t, len, key, slide);
                v.getDynamicObject()->setProperty ("vel", vel);
                return v;
            };
            app.run ("channel.setSampler", args ({ { "channel", id }, { "rootKey", 31 }, { "rootCents", 0.0 } }));
            addNotes (app, id, { note (0.0, 1.0, 31, false, 127), note (1.0, 1.0, 33, true, 38) });
            const auto out = renderPattern ("fatiar_slide_forca.wav");
            const float jump = maxJump (out, beat140 - 0.02, beat140 + 0.10);
            const float natural = maxJump (out, beat140 - 0.12, beat140 - 0.02);
            const float before = peakIn (out, beat140 - 0.10, beat140 - 0.01), after = peakIn (out, beat140 + 0.03, beat140 + 0.12);
            rep.check (jump <= natural * 1.1f + 1.0e-4f && after < before * 0.4f,
                       "slide de força 127 para 38: maior salto "_t + juce::String (jump, 5) + " (antes do slide, "
                           + juce::String (natural, 5) + "); o volume cai para " + juce::String (after / juce::jmax (before, 1.0e-6f), 2));
        }

        // 6. O beat em MP3 na playlist cai no tempo do WAV (o atraso do codificador entra no corte).
        if (mp3EncoderAvailable())
        {
            const auto wavBeat = kit.getChildFile ("Beat Teste Clipe.wav");
            const auto mp3Beat = ctx.work.getChildFile ("kit_mp3").getChildFile ("Beat Teste Clipe.mp3");
            const auto encoded = encodeMp3 (wavBeat, mp3Beat, Mp3Settings {});

            auto renderClip = [&] (const juce::File& file, const juce::String& name)
            {
                app.run ("project.new");
                app.run ("playlist.clear");
                app.run ("transport.setBpm", args ({ { "bpm", 120 } }));
                app.run ("playlist.addItem", args ({ { "path", file.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
                const auto f = ctx.work.getChildFile (name);
                app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
                return readWav (f);
            };

            // O mesmo beat com etiqueta ID3 (título e artista), como quase todo MP3 de verdade.
            const auto mp3Tagged = ctx.work.getChildFile ("kit_mp3").getChildFile ("Beat Teste Clipe ID3.mp3");
            Mp3Settings tagged;
            tagged.title = "Beat de teste";
            tagged.artist = "WHYNOT";
            const auto encodedTagged = encodeMp3 (wavBeat, mp3Tagged, tagged);

            // O atraso que sobra: o deslocamento com a maior correlação, de -60 a +60 ms.
            auto lagOf = [] (const Audio& reference, const Audio& other, double& best)
            {
                int bestLag = 0;
                best = -2.0;

                for (int lag = -2880; lag <= 2880; ++lag)
                {
                    double ab = 0.0, aa = 0.0, bb = 0.0;

                    for (int i = 4800; i < 4800 + 48000; i += 4)
                    {
                        const double x = reference.buffer.getSample (0, i), y = other.buffer.getSample (0, juce::jlimit (0, other.buffer.getNumSamples() - 1, i + lag));
                        ab += x * y; aa += x * x; bb += y * y;
                    }

                    if (const double c = aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0; c > best)
                    {
                        best = c;
                        bestLag = lag;
                    }
                }

                return bestLag;
            };

            const auto fromWav = renderClip (wavBeat, "fatiar_clipe_wav.wav"), fromMp3 = renderClip (mp3Beat, "fatiar_clipe_mp3.wav");
            const auto fromTagged = renderClip (mp3Tagged, "fatiar_clipe_mp3_id3.wav");
            double best = 0.0, bestTagged = 0.0;
            const int bestLag = lagOf (fromWav, fromMp3, best), taggedLag = lagOf (fromWav, fromTagged, bestTagged);

            rep.check (encoded.wasOk() && std::abs (bestLag) <= 2 && best > 0.98,
                       "beat em MP3 na playlist: "_t + juce::String (bestLag) + " amostras do WAV (correlação " + juce::String (best, 4)
                           + "); a etiqueta pediu " + juce::String (mp3EncoderDelaySeconds (mp3Beat) * 1000.0, 2) + " ms de corte");
            rep.check (encodedTagged.wasOk() && std::abs (taggedLag) <= 2 && bestTagged > 0.98,
                       "o mesmo beat com ID3 (título e artista): "_t + juce::String (taggedLag) + " amostras do WAV (correlação "
                           + juce::String (bestTagged, 4) + "; corte de " + juce::String (mp3EncoderDelaySeconds (mp3Tagged) * 1000.0, 2) + " ms)");

            // E no sampler: o one-shot em MP3 com ID3 entra junto com o WAV.
            auto samplerOnset = [&] (const juce::File& file, const juce::String& name)
            {
                app.run ("project.new");
                const auto added = app.run ("channel.addSampler", args ({ { "path", file.getFullPathName() }, { "name", name }, { "detectRoot", false } }));
                app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));
                setGrid (app, added.value["channel"].toString(), "x...............");
                const auto f = ctx.work.getChildFile ("fatiar_onset_" + name + ".wav");
                app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
                return readWav (f);
            };

            const auto kickWav = ctx.work.getChildFile ("Kit Trap 808").getChildFile ("Kicks/Kick Hard.wav");
            const auto kickTagged = ctx.work.getChildFile ("kit_mp3").getChildFile ("Kick ID3.mp3");
            encodeMp3 (kickWav, kickTagged, tagged);
            const auto kickA = samplerOnset (kickWav, "wav5"), kickB = samplerOnset (kickTagged, "id3");
            const double kickMs = (onsetAfter (kickB, 0.0) - onsetAfter (kickA, 0.0)) * 1000.0;
            rep.check (std::abs (kickMs) < 1.0 && correlate (kickB, 0.0, kickA, 0.0, 0.1) > 0.98,
                       "o one-shot em MP3 com ID3 entra junto com o WAV: "_t + juce::String (kickMs, 2) + " ms de diferença");
        }
    }

    app.run ("project.new");
}

} // namespace whynot::selftest
