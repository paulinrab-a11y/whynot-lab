// Autoteste da afinação completa (frente B de 27/10):
// - o som no render: uma voz sintética desafinada (Lá 4 + 40 cents) num canal sampler sai no Lá com o preset Pop, e a
//   compensação de latência (58,5 ms do PSOLA) deixa a voz no mesmo lugar que sem o efeito;
// - a escala nota a nota pelos comandos (só o Si ligado) e o preset que não apaga as notas da escala Personalizada;
// - a nota guia: um synth tocando Dó 5 numa trilha mandada para um bus mudo manda na nota pelo sidechain, sem ser ouvido;
// - a tela: a janela do efeito e a parte de cima com a voz passando, em tune-*.png.

#include "SelfTestKit.h"
#include "../../dsp/FxCatalog.h"
#include "../../dsp/FxHost.h"
#include "../../dsp/Tune.h"
#include "../../ui/Theme.h"
#include "../../ui/fx/FxEditor.h"
#include "../../ui/fx/FxVisuals.h"

namespace whynot::selftest
{

namespace
{
    constexpr double voiceRate = 48000.0;

    /** Voz simples: fundamental forte e dois harmônicos, com um vibrato leve de ±10 cents a 5 Hz. */
    std::vector<float> voiceSamples (double hz, double seconds, double vibratoCents = 10.0)
    {
        std::vector<float> x ((size_t) (seconds * voiceRate));
        double phase = 0.0;

        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / voiceRate;
            phase += hz * std::pow (2.0, vibratoCents * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t) / 1200.0) / voiceRate;
            phase -= std::floor (phase);
            const double w = 2.0 * juce::MathConstants<double>::pi * phase;
            const double fade = juce::jlimit (0.0, 1.0, std::min (t, seconds - t) / 0.02);
            x[i] = (float) (0.3 * fade * (std::sin (w) + 0.3 * std::sin (2.0 * w) + 0.12 * std::sin (3.0 * w)));
        }

        return x;
    }

    bool writeVoice (const juce::File& file, double hz, double seconds)
    {
        const auto x = voiceSamples (hz, seconds);
        juce::AudioBuffer<float> buffer (1, (int) x.size());
        buffer.copyFrom (0, 0, x.data(), (int) x.size());
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (voiceRate).withNumChannels (1).withBitsPerSample (24));
        return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    double cents (double hz, double reference)   { return 1200.0 * std::log2 (hz / reference); }

    /** O primeiro instante (s) em que o sinal passa de 10% do pico. */
    double onsetOf (const Audio& a)
    {
        const float limit = 0.1f * juce::Decibels::decibelsToGain (peakDb (a));

        for (int i = 0; i < a.buffer.getNumSamples(); ++i)
            if (std::abs (a.buffer.getSample (0, i)) > limit)
                return i / a.sampleRate;

        return -1.0;
    }

    bool savePng (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return image.isValid() && out.openedOk() && png.writeImageToStream (image, out);
    }
}

void runTuneSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    const double sharpA = 440.0 * std::pow (2.0, 40.0 / 1200.0);   // Lá 4 + 40 cents
    const auto voiceFile = ctx.work.getChildFile ("afinacao-voz.wav");

    juce::String voiceMx, tuneId;
    float popLevel = 0.0f;   // o RMS do render com o preset Pop: a referência para ver que a guia não soma no som

    auto render = [&] (const juce::String& name)
    {
        const auto f = ctx.work.getChildFile (name);
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        return readWav (f);
    };

    auto setParam = [&] (const char* id, double value)
    {
        return app.run ("mixer.setInsertParam", args ({ { "track", voiceMx }, { "insert", tuneId }, { "param", id }, { "value", value } })).ok;
    };

    auto paramOf = [&] (const char* id)
    {
        const auto* m = model.get().findMixer (voiceMx);
        const auto* ins = m != nullptr ? m->findInsert (tuneId) : nullptr;
        return ins != nullptr && ins->params.count (id) > 0 ? ins->params.at (id) : -1.0f;
    };

    //--------------------------------------------------------------------------
    rep.section ("Afinação: o som no render e a compensação de latência"_t);
    {
        app.run ("project.new");
        app.run ("project.setKey", args ({ { "root", "A" }, { "scale", "minor" } }));
        const bool wrote = writeVoice (voiceFile, sharpA, 1.6);
        const auto added = app.run ("channel.addSampler", args ({ { "path", voiceFile.getFullPathName() }, { "name", "Voz" },
                                                                   { "rootKey", 60 }, { "detectRole", false } }));
        const auto voiceCh = added.value["channel"].toString();
        voiceMx = added.value["mixerTrack"].toString();
        addNotes (app, voiceCh, { noteVar (0, 4, 60, false) });

        const auto plain = render ("afinacao-sem.wav");
        const auto inserted = app.run ("mixer.addInsert", args ({ { "track", voiceMx }, { "type", "tune" }, { "preset", "pop" } }));
        tuneId = inserted.value["insert"].toString();
        const auto tuned = render ("afinacao-pop.wav");
        popLevel = rmsDb (tuned);

        const double before = measureHz (plain, 0.4, 1.4), after = measureHz (tuned, 0.4, 1.4);
        rep.check (wrote && added.ok && inserted.ok && plain.valid && tuned.valid && std::abs (cents (before, sharpA)) < 5.0
                       && std::abs (cents (after, 440.0)) < 6.0,
                   "voz em Lá 4 + 40 cents ("_t + juce::String (before, 1) + " Hz) sai no Lá com o preset Pop: " + juce::String (after, 1)
                       + " Hz (" + juce::String (cents (after, 440.0), 1) + " cents)");

        // A latência que o efeito informa (num host preparado à parte: sem placa de som, o plugin ao vivo não se prepara).
        // Com a compensação, a voz afinada começa junto com a seca: sem ela, chegaria 58,5 ms depois. Sobra o ataque do
        // primeiro grão do PSOLA (alguns ms), que não é atraso.
        dsp::FxHost probe (dsp::createProcessor ("tune"));
        probe.prepare (48000.0, 512);
        const double latencyMs = 1000.0 * probe.latencySamples() / 48000.0;
        const double shiftMs = 1000.0 * (onsetOf (tuned) - onsetOf (plain));
        rep.check (std::abs (latencyMs - 58.5) < 0.2 && std::abs (shiftMs) < 10.0,
                   "latência informada ao host: "_t + juce::String (latencyMs, 1) + " ms; com a compensação, a voz começa no mesmo lugar ("
                       + juce::String (shiftMs, 2) + " ms de diferença, o ataque do primeiro grão)");
    }

    //--------------------------------------------------------------------------
    rep.section ("Afinação: escala nota a nota, Lá de referência, formante e presets"_t);
    {
        // Só o Si ligado na escala Personalizada: a voz sobe 2 semitons (Si 4).
        bool set = setParam ("scale", dsp::TuneProcessor::customScale);
        const char* const notes[] = { "note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f", "note_fs", "note_g", "note_gs",
                                      "note_a", "note_as", "note_b" };

        for (int k = 0; k < 12; ++k)
            set = setParam (notes[k], k == 11 ? 1.0 : 0.0) && set;

        const auto onlyB = render ("afinacao-so-si.wav");
        const double hz = measureHz (onlyB, 0.4, 1.4);
        rep.check (set && std::abs (cents (hz, 493.88)) < 6.0,
                   "escala Personalizada só com o Si: a voz vai para "_t + juce::String (hz, 1) + " Hz (Si 4 = 493,9)");

        // O preset troca a correção, mas a escala Personalizada e as notas ficam.
        const bool loaded = app.run ("mixer.loadInsertPreset", args ({ { "track", voiceMx }, { "insert", tuneId }, { "preset", "harmony" } })).ok;
        rep.check (loaded && paramOf ("scale") == (float) dsp::TuneProcessor::customScale && paramOf ("note_b") == 1.0f
                       && paramOf ("note_a") == 0.0f && paramOf ("harmony") == 1.0f,
                   "o preset Harmonia entra sem apagar a escala Personalizada nem as notas ligadas"_t);

        // Lá em 432 Hz, cromática e sem harmonia: a voz (Lá 440 + 40 cents) fica 72 cents acima do Lá 432 e vai para o Lá#.
        app.run ("mixer.loadInsertPreset", args ({ { "track", voiceMx }, { "insert", tuneId }, { "preset", "pop" } }));
        setParam ("scale", 6);
        setParam ("reference", 432);
        const auto at432 = render ("afinacao-432.wav");
        const double hz432 = measureHz (at432, 0.4, 1.4), expected = 432.0 * std::pow (2.0, 1.0 / 12.0);
        rep.check (std::abs (cents (hz432, expected)) < 6.0,
                   "Lá de referência em 432 Hz: a voz vai para o Lá# da grade, "_t + juce::String (hz432, 1) + " Hz (esperado "
                       + juce::String (expected, 1) + ")");
        setParam ("reference", 440);
        setParam ("scale", 0);

        // Formante sem mudar a nota.
        setParam ("formant", -5);
        const auto deep = render ("afinacao-formante.wav");
        const double hzDeep = measureHz (deep, 0.4, 1.4);
        rep.check (deep.valid && std::abs (cents (hzDeep, 440.0)) < 8.0 && rmsDb (deep) > -40.0f,
                   "formante -5 st: a nota continua no Lá ("_t + juce::String (hzDeep, 1) + " Hz, "
                       + juce::String (rmsDb (deep), 1) + " dB RMS)");
        setParam ("formant", 0);
    }

    //--------------------------------------------------------------------------
    rep.section ("Afinação: nota guia pelo sidechain"_t);
    {
        // A guia: o sub do 3 OSC (um seno) tocando Dó 5, mandado para um bus com o fader no mínimo.
        const auto guide = app.run ("channel.addSynth", args ({ { "preset", "3osc-sub" } }));
        const auto guideCh = guide.value["channel"].toString(), guideMx = guide.value["mixerTrack"].toString();
        addNotes (app, guideCh, { noteVar (0, 4, 72, false) });
        const auto bus = app.run ("mixer.addTrack", args ({ { "name", "Guia muda" } }));
        const auto busId = bus.value["track"].toString();
        const bool routed = app.run ("mixer.setOutput", args ({ { "track", guideMx }, { "output", busId } })).ok
                            && app.run ("mixer.setVolume", args ({ { "track", busId }, { "db", -100 } })).ok;

        // Sem "Seguir a guia", o som não muda e a guia não se ouve.
        const bool chained = app.run ("mixer.setInsertSidechain", args ({ { "track", voiceMx }, { "insert", tuneId }, { "source", guideMx } })).ok;
        const auto quiet = render ("afinacao-guia-desligada.wav");
        const double hzQuiet = measureHz (quiet, 0.4, 1.4);

        setParam ("guide", 1);
        const auto guided = render ("afinacao-guia.wav");
        const double hzGuided = measureHz (guided, 0.4, 1.4);
        const float extra = rmsDb (quiet) - popLevel;
        rep.check (guide.ok && routed && chained && std::abs (cents (hzQuiet, 440.0)) < 6.0 && std::abs (cents (hzGuided, 523.25)) < 8.0
                       && std::abs (extra) < 0.3f,
                   "a guia (Dó 5, num bus mudo) manda na nota: "_t + juce::String (hzGuided, 1) + " Hz (Dó 5 = 523,3); sem seguir a guia, "
                       + juce::String (hzQuiet, 1) + " Hz e o mesmo nível do render sem a guia (" + juce::String (extra, 2)
                       + " dB): a guia não soma no som");

        setParam ("guide", 0);
        app.run ("mixer.setInsertSidechain", args ({ { "track", voiceMx }, { "insert", tuneId }, { "source", "" } }));
    }

    //--------------------------------------------------------------------------
    rep.section ("Afinação: a tela"_t);
    {
        setParam ("harmony", 1);
        auto* fx = app.getProjector().fxFor (voiceMx, tuneId);
        bool shot = false, shotTop = false;

        if (fx != nullptr)
        {
            FxEditor editor (app, *fx);
            shot = savePng (editor, ctx.work.getChildFile ("tune-janela.png"));

            // A parte de cima com a voz passando: sem placa de som no autoteste, o efeito ao vivo não roda sozinho;
            // aqui ele processa 1,5 s de uma voz que desliza do Lá + 40 cents ao Dó, e o visual lê os medidores a 30 Hz.
            auto& host = fx->getHost();

            if (! host.isPrepared())
                host.prepare (voiceRate, 512);

            if (const auto* m = model.get().findMixer (voiceMx))
                if (const auto* ins = m->findInsert (tuneId))
                    fx->applyInsert (*ins, model.get().bpm);

            auto visual = FxVisual::create (*fx);

            if (visual != nullptr)
            {
                std::vector<float> values;

                for (auto& p : host.info().params)
                    values.push_back (fx->getHost().getParam (host.info().indexOf (p.id)));

                visual->setValues (values);
                visual->setSize (720, FxVisual::heightFor ("tune"));
                auto a = voiceSamples (sharpA, 1.0, 25.0), b = voiceSamples (523.25 * std::pow (2.0, -30.0 / 1200.0), 1.0, 25.0);
                a.insert (a.end(), b.begin(), b.end());
                auto r = a;
                const int chunk = (int) (voiceRate / 30.0);

                for (size_t pos = 0; pos + (size_t) chunk <= a.size(); pos += (size_t) chunk)
                {
                    host.process (a.data() + pos, r.data() + pos, chunk);
                    visual->tick();
                }

                juce::Component frame;
                frame.setSize (visual->getWidth(), visual->getHeight());
                frame.setOpaque (true);
                frame.addAndMakeVisible (*visual);
                juce::Image image (juce::Image::ARGB, frame.getWidth(), frame.getHeight(), true, juce::SoftwareImageType());
                {
                    juce::Graphics g (image);
                    g.fillAll (theme::colour::panel);
                    frame.paintEntireComponent (g, false);
                }
                const auto file = ctx.work.getChildFile ("tune-visual.png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat png;
                shotTop = out.openedOk() && png.writeImageToStream (image, out);
                host.processor().reset();
            }
        }

        rep.check (shot && shotTop, "janela da afinação e a parte de cima com a voz passando (tune-janela.png, tune-visual.png)"_t);
        setParam ("harmony", 0);
    }
}

} // namespace whynot::selftest
