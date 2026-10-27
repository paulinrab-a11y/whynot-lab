// Autoteste da afinação nota a nota (frente C de 27/10): a voz de um bloco de áudio da playlist como notas.
// - fatia 1: a análise de uma voz sintética de notas conhecidas (notas, tempos, cents) e o cache por arquivo;
// - fatia 2: o editor sem janela (notas, escolha, captura nota-a-nota-editor.png) e o item no menu do bloco;
// - fatia 3: as edições pelos comandos e pela tela (arrastar, centralizar, vibrato, transição, cortar, juntar,
//   escala), cada uma com o desfazer;
// - fatia 4: o render não destrutivo: a nota movida medida em Hz no export, os trechos não editados iguais ao export
//   sem edição (diferença máxima medida), o original intacto, o .wnl nos dois escritores e o arquivo refeito de novo;
// - fatia 5: a formante junto e separada do tom, medida nos harmônicos, e a prévia (o Projector troca o arquivo).
// Dono: Afinação nota a nota.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"
#include "../../record/NoteTuneFiles.h"
#include "../../ui/NoteTuneEditor.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    double midiToHz (double m)               { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
    double cents (double hz, double ref)     { return hz > 0.0 && ref > 0.0 ? 1200.0 * std::log2 (hz / ref) : 1.0e9; }
    juce::String num (double v, int places = 1)   { return juce::String (v, places).replaceCharacter ('.', ','); }

    struct Sung { double start, end, midi, vibrato = 0.0; };

    /** A voz de teste: 20 harmônicos com duas formantes fixas (700 e 1200 Hz), 25 ms de subida e descida. */
    juce::AudioBuffer<float> sing (const std::vector<Sung>& notes, double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        b.clear();
        double phase = 0.0;

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            double midi = 0.0, amp = 0.0;

            for (size_t k = 0; k < notes.size(); ++k)
            {
                const auto& n = notes[k];

                if (t < n.start || t >= n.end)
                    continue;

                midi = n.midi + n.vibrato / 100.0 * std::sin (twoPi * 5.5 * (t - n.start));
                const bool before = k > 0 && std::abs (notes[k - 1].end - n.start) < 1.0e-9;
                const bool after = k + 1 < notes.size() && std::abs (notes[k + 1].start - n.end) < 1.0e-9;

                if (before && t - n.start < 0.03)
                    midi = notes[k - 1].midi + (midi - notes[k - 1].midi) * (t - n.start) / 0.03;

                amp = juce::jmin (1.0, before ? 1.0 : (t - n.start) / 0.025, after ? 1.0 : (n.end - t) / 0.025);
            }

            if (amp <= 0.0)
                continue;

            const double f0 = midiToHz (midi);
            phase = std::fmod (phase + twoPi * f0 / rate, twoPi * 1000.0);
            double s = 0.0;

            for (int h = 1; h <= 20 && f0 * h < 0.45 * rate; ++h)
            {
                const double f = f0 * h;
                const double env = 1.0 / (1.0 + std::pow ((f - 700.0) / 300.0, 2.0)) + 0.6 / (1.0 + std::pow ((f - 1200.0) / 400.0, 2.0)) + 0.02;
                s += env * std::sin (phase * h) / std::sqrt ((double) h);
            }

            b.setSample (0, i, (float) (0.2 * amp * s));
        }

        return b;
    }

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                .withSampleRate (rate)
                                                .withNumChannels (b.getNumChannels())
                                                .withBitsPerSample (32)
                                                .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    double fundamental (const Audio& a, double from, double to)
    {
        if (! a.valid)
            return 0.0;

        const auto* x = a.buffer.getReadPointer (0);
        const auto n = (std::size_t) a.buffer.getNumSamples();
        return dsp::measureFundamental (x, n, a.sampleRate, (std::size_t) (from * a.sampleRate), juce::jmin (n, (std::size_t) (to * a.sampleRate)));
    }

    double amplitudeAt (const Audio& a, double from, double to, double f)
    {
        const auto* x = a.buffer.getReadPointer (0);
        const auto s0 = (int) (from * a.sampleRate), s1 = juce::jmin (a.buffer.getNumSamples(), (int) (to * a.sampleRate));
        double re = 0.0, im = 0.0;

        for (int i = s0; i < s1; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (twoPi * (i - s0) / (double) (s1 - s0));
            re += w * x[i] * std::cos (twoPi * f * i / a.sampleRate);
            im += w * x[i] * std::sin (twoPi * f * i / a.sampleRate);
        }

        return std::sqrt (re * re + im * im);
    }

    /** A maior diferença entre dois renders fora de [from, to) segundos. */
    float maxDiffOutside (const Audio& a, const Audio& b, double from, double to)
    {
        if (! a.valid || ! b.valid)
            return 1.0e9f;

        float worst = 0.0f;
        const int n = juce::jmin (a.buffer.getNumSamples(), b.buffer.getNumSamples());

        for (int ch = 0; ch < juce::jmin (a.buffer.getNumChannels(), b.buffer.getNumChannels()); ++ch)
            for (int i = 0; i < n; ++i)
            {
                const double t = i / a.sampleRate;

                if (t < from || t >= to)
                    worst = juce::jmax (worst, std::abs (a.buffer.getSample (ch, i) - b.buffer.getSample (ch, i)));
            }

        return worst;
    }

    bool savePng (juce::Component& c, const juce::File& file)
    {
        const auto image = juce::SoftwareImageType().convert (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f));
        file.deleteFile();

        if (juce::FileOutputStream out (file); out.openedOk())
        {
            juce::PNGImageFormat png;
            return png.writeImageToStream (image, out) && image.getWidth() > 0;
        }

        return false;
    }

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, juce::Point<float> down, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, mods,
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, down, now, 1, p != down };
    }

    const Channel* channelOf (const ProjectModel& model, const juce::String& item)
    {
        const auto* i = model.get().findItem (item);
        return i != nullptr ? model.get().findChannel (i->ref) : nullptr;
    }
}

void runNoteTuneSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("nota-a-nota");
    work.createDirectory();

    // Lá 3, Dó 4 encostado, pausa, Mi 4 com vibrato de ±40 cents, Ré 4 desafinado (+30 cents) e Sol 3
    const std::vector<Sung> sung = { { 0.20, 0.70, 57.0 }, { 0.70, 1.20, 60.0 }, { 1.45, 2.15, 64.0, 40.0 }, { 2.40, 2.90, 62.3 }, { 3.10, 3.60, 55.0 } };
    const auto voiceFile = work.getChildFile ("voz-notas.wav");
    writeWav (voiceFile, sing (sung, 4.0));
    const auto originalBytes = voiceFile.loadFileAsString().hashCode64();

    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 120.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));
    const auto clip = app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", voiceFile.getFullPathName() }, { "track", 1 }, { "start", 0 } }))
                          .value["item"].toString();
    auto render = [&] (const juce::String& name)
    {
        const auto f = work.getChildFile (name);
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
        return readWav (f);
    };

    //==========================================================================
    rep.section ("Afinação nota a nota: análise da voz e cache"_t);
    {
        notetune::forgetMemoryCache();
        const auto before = notetune::cacheCounts();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto r = app.run ("audio.analyzeNotes", args ({ { "item", clip } }));
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto* found = r.value["analysis"].getArray();
        const bool five = r.ok && found != nullptr && found->size() == (int) sung.size();
        double worstCents = 0.0, worstEdge = 0.0;

        if (five)
            for (int i = 0; i < found->size(); ++i)
            {
                const auto& n = found->getReference (i);
                worstCents = juce::jmax (worstCents, std::abs (100.0 * ((double) n["midi"] - sung[(size_t) i].midi)));
                worstEdge = juce::jmax (worstEdge, std::abs ((double) n["start"] - sung[(size_t) i].start), std::abs ((double) n["end"] - sung[(size_t) i].end));
            }

        rep.check (five && worstCents < 5.0 && worstEdge < 0.04,
                   "a voz de 5 notas conhecidas vira 5 notas: a média a "_t + num (worstCents, 2) + " cents e as pontas a "
                       + num (worstEdge * 1000.0, 1) + " ms no pior caso (4 s de voz analisados em " + num (ms, 0) + " ms)");
        rep.check (five && (int) found->getReference (2)["vibratoCents"] > 20 && (int) found->getReference (0)["vibratoCents"] < 5,
                   "o vibrato aparece só no Mi ("_t + (five ? found->getReference (2)["vibratoCents"].toString() : "?") + " cents)");

        const auto computed = notetune::cacheCounts();
        notetune::forgetMemoryCache();
        app.run ("audio.analyzeNotes", args ({ { "item", clip } }));
        const auto fromDisk = notetune::cacheCounts();
        app.run ("audio.analyzeNotes", args ({ { "item", clip } }));
        const auto fromMemory = notetune::cacheCounts();
        rep.check (computed.computed == before.computed + 1 && fromDisk.disk == computed.disk + 1 && fromDisk.computed == computed.computed
                       && fromMemory.memory == fromDisk.memory + 1,
                   "cache por arquivo: a 1ª análise é feita, a 2ª sai do disco (pasta temporária) e a 3ª da memória"_t);

        const auto* ch = channelOf (model, clip);
        rep.check (ch != nullptr && ch->tunedNotes.empty() && ! wnl::toJson (model.get()).contains ("noteTune"),
                   "analisar não muda o projeto (nada no .wnl)"_t);
        rep.check (! app.run ("audio.analyzeNotes", args ({ { "item", "it_nao_existe" } })).ok, "bloco que não existe é recusado"_t);
    }

    const auto plain = render ("sem-edicao.wav");

    //==========================================================================
    rep.section ("Afinação nota a nota: edições e desfazer"_t);
    {
        const auto moved = app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 1 } }, { "to", 62 } }));
        const auto* ch = channelOf (model, clip);
        const bool edited = moved.ok && ch != nullptr && ch->tunedNotes.size() == sung.size() && std::abs (ch->tunedNotes[1].targetMidi() - 62.0f) < 0.01f
                            && ! ch->tunedNotes[0].edited();
        rep.check (edited, "arrastar o Dó para o Ré (audio.editNotes to 62): só ele editado, e o resto como estava"_t);
        app.run ("edit.undo");
        ch = channelOf (model, clip);
        rep.check (ch != nullptr && ch->tunedNotes.empty(), "Ctrl+Z desfaz a edição (o canal volta sem notas)"_t);
        app.run ("edit.redo");
        ch = channelOf (model, clip);
        rep.check (ch != nullptr && ! ch->tunedNotes.empty() && ch->tunedNotes[1].edited(), "Ctrl+Y refaz"_t);

        const bool split = app.run ("audio.splitNote", args ({ { "item", clip }, { "index", 0 }, { "at", 0.45 } })).ok;
        const int afterSplit = (int) channelOf (model, clip)->tunedNotes.size();
        const bool tooClose = ! app.run ("audio.splitNote", args ({ { "item", clip }, { "index", 0 }, { "at", 0.21 } })).ok;
        const bool merged = app.run ("audio.mergeNotes", args ({ { "item", clip }, { "index", 0 } })).ok;
        rep.check (split && afterSplit == (int) sung.size() + 1 && tooClose && merged && (int) channelOf (model, clip)->tunedNotes.size() == (int) sung.size(),
                   "cortar a nota em duas (e recusar um pedaço de menos de 30 ms) e juntar de volta"_t);

        app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 3 } }, { "center", 100 } }));
        app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 2 } }, { "flatten", 100 }, { "transitionMs", 120 } }));
        ch = channelOf (model, clip);
        rep.check (ch->tunedNotes[3].center == 1.0f && ch->tunedNotes[2].flatten == 1.0f && ch->tunedNotes[2].transitionMs == 120.0f,
                   "centralizar, achatar o vibrato e a transição vão para o canal"_t);
        rep.check (! app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 99 } }, { "center", 50 } })).ok,
                   "nota fora da lista é recusada"_t);

        // escala do projeto: Lá menor; o Ré desafinado vai para 62 e todas caem num semitom da escala
        model.data().keyRoot = 9;
        model.data().scale = "minor";
        const auto scaled = app.run ("audio.tuneToScale", args ({ { "item", clip } }));
        ch = channelOf (model, clip);
        bool allIn = scaled.ok && ch != nullptr;

        if (allIn)
            for (auto& n : ch->tunedNotes)
                allIn = allIn && std::abs (n.targetMidi() - std::round (n.targetMidi())) < 0.01f && model.get().inScale ((int) std::lround (n.targetMidi()));

        rep.check (allIn && std::abs (ch->tunedNotes[3].targetMidi() - 62.0f) < 0.01f,
                   "corrigir para a escala num clique: todas as notas num semitom de Lá menor ("_t + scaled.value["scale"].toString() + ")");
        rep.check (app.run ("audio.clearNoteTune", args ({ { "item", clip } })).ok && channelOf (model, clip)->tunedNotes.empty(),
                   "voltar ao original tira todas as notas"_t);
    }

    //==========================================================================
    rep.section ("Afinação nota a nota: render não destrutivo"_t);
    {
        app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 1 } }, { "to", 62 } }));
        const auto* ch = channelOf (model, clip);
        const auto rendered = notetune::renderedFileFor (voiceFile, ch->tunedNotes, ch->tuneFormantFollows);
        const auto tuned = render ("do-para-re.wav");
        const double hz = fundamental (tuned, 0.85, 1.10);
        const double hzOther = fundamental (tuned, 1.6, 2.0);
        rep.check (std::abs (cents (hz, midiToHz (62.0))) < 5.0 && std::abs (cents (hzOther, midiToHz (64.0))) < 10.0,
                   "no export, o Dó movido sai em Ré: "_t + num (hz, 2) + " Hz (Ré 4 = 293,66; " + num (cents (hz, midiToHz (62.0)), 2)
                       + " cents) e o Mi seguinte fica em " + num (hzOther, 1) + " Hz");

        const float outside = maxDiffOutside (plain, tuned, 0.6, 1.35);
        rep.check (outside < 1.0e-6f, "fora do Dó (0,6 a 1,35 s), o export com a edição é igual ao sem edição: diferença máxima "_t
                                          + juce::String (outside, 9));
        rep.check (rendered.existsAsFile() && rendered.getFileName().startsWith ("voz-notas_nota_") && voiceFile.loadFileAsString().hashCode64() == originalBytes,
                   "o arquivo refeito fica ao lado ("_t + rendered.getFileName() + ") e o original não muda (mesmos bytes)");

        // .wnl: os dois escritores iguais, e volta igual
        const auto json = wnl::toJson (model.get());
        Project back;
        const bool read = wnl::fromJson (json, back).wasOk();
        const auto* again = back.findChannel (ch->id);
        rep.check (read && json == wnl::toJsonReference (model.get()) && json.contains ("\"noteTune\"") && json.contains ("\"shiftCents\"")
                       && again != nullptr && again->tunedNotes == ch->tunedNotes,
                   "o .wnl guarda as notas (toJson = toJsonReference) e o projeto reaberto tem as mesmas notas, valor por valor"_t);
        rep.check (notetune::renderedFileFor (voiceFile, again->tunedNotes, again->tuneFormantFollows) == rendered,
                   "reaberto, o projeto aponta para o mesmo arquivo refeito"_t);

        // o arquivo refeito sumiu (projeto copiado sem ele): é escrito de novo, igual
        const auto bytes = rendered.loadFileAsString().hashCode64();
        rendered.deleteFile();
        const auto again2 = notetune::playedFile (voiceFile, ch->tunedNotes, ch->tuneFormantFollows);
        rep.check (again2 == rendered && rendered.existsAsFile() && rendered.loadFileAsString().hashCode64() == bytes,
                   "sem o arquivo refeito, ele é escrito de novo a partir do original, com os mesmos bytes"_t);

        // centralizar e vibrato medidos no som
        app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 3 } }, { "center", 100 } }));
        const auto centred = render ("re-centralizado.wav");
        const double off = cents (fundamental (centred, 2.55, 2.75), midiToHz (62.0)), offBefore = cents (fundamental (plain, 2.55, 2.75), midiToHz (62.0));
        rep.check (std::abs (offBefore - 30.0) < 6.0 && std::abs (off) < 6.0,
                   "centralizar 100%: o Ré desafinado vai de "_t + num (offBefore) + " para " + num (off) + " cents"_t);

        app.run ("edit.undo");
        app.run ("audio.clearNoteTune", args ({ { "item", clip } }));
        const auto cleared = render ("original-de-novo.wav");
        rep.check (maxDiff (plain, cleared) < 1.0e-6f, "voltar ao original: o export é o mesmo de antes da edição (diferença "_t
                                                           + juce::String (maxDiff (plain, cleared), 9) + ")");
    }

    //==========================================================================
    rep.section ("Afinação nota a nota: formante e prévia"_t);
    {
        // +4 st no Lá: com o timbre separado, a razão 2º/3º harmônico segue as formantes fixas; junto, a da voz original
        app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 0 } }, { "to", 61 }, { "formantFollows", false } }));
        const auto kept = render ("formante-separada.wav");
        const auto* ch = channelOf (model, clip);
        const auto keptFile = notetune::renderedFileFor (voiceFile, ch->tunedNotes, ch->tuneFormantFollows);
        app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 0 } }, { "formantFollows", true } }));
        const auto follow = render ("formante-junto.wav");
        ch = channelOf (model, clip);
        const auto followFile = notetune::renderedFileFor (voiceFile, ch->tunedNotes, ch->tuneFormantFollows);
        const double f1 = midiToHz (61.0);
        const double r0 = amplitudeAt (plain, 0.35, 0.6, 440.0) / amplitudeAt (plain, 0.35, 0.6, 660.0);
        const double rk = amplitudeAt (kept, 0.35, 0.6, 2.0 * f1) / amplitudeAt (kept, 0.35, 0.6, 3.0 * f1);
        const double rf = amplitudeAt (follow, 0.35, 0.6, 2.0 * f1) / amplitudeAt (follow, 0.35, 0.6, 3.0 * f1);
        auto env = [] (double f) { return 1.0 / (1.0 + std::pow ((f - 700.0) / 300.0, 2.0)) + 0.6 / (1.0 + std::pow ((f - 1200.0) / 400.0, 2.0)) + 0.02; };
        const double expected = (env (2.0 * f1) / std::sqrt (2.0)) / (env (3.0 * f1) / std::sqrt (3.0));
        rep.check (std::abs (rk / expected - 1.0) < 0.25 && std::abs (rf / r0 - 1.0) < 0.25 && keptFile != followFile,
                   "formante: com o timbre separado, 2º/3º harmônico "_t + num (rk, 3) + " (as formantes fixas dão " + num (expected, 3)
                       + "); junto com o tom, " + num (rf, 3) + " (a voz original tem " + num (r0, 3) + "), cada um no seu arquivo");

        app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 0 } }, { "to", 57 }, { "formant", 5 }, { "formantFollows", false } }));
        const auto formantOnly = render ("so-formante.wav");
        // 2º contra 4º harmônico do Lá (440 e 880 Hz): as formantes 5 st acima mudam bem essa razão
        const double hzKept = fundamental (formantOnly, 0.35, 0.6);
        const double r24 = amplitudeAt (plain, 0.35, 0.6, 440.0) / amplitudeAt (plain, 0.35, 0.6, 880.0);
        const double rFormant = amplitudeAt (formantOnly, 0.35, 0.6, 440.0) / amplitudeAt (formantOnly, 0.35, 0.6, 880.0);
        const double up5 = std::pow (2.0, 5.0 / 12.0);
        const double expected24 = (env (440.0 / up5) / std::sqrt (2.0)) / (env (880.0 / up5) / std::sqrt (4.0));
        rep.check (std::abs (cents (hzKept, 220.0)) < 6.0 && std::abs (rFormant / expected24 - 1.0) < 0.2 && std::abs (rFormant / r24 - 1.0) > 0.2,
                   "só a formante (+5 st) muda o timbre: 2º/4º harmônico de "_t + num (r24, 3) + " para " + num (rFormant, 3)
                       + " (as formantes 5 st acima dão " + num (expected24, 3) + ") e a nota fica em " + num (hzKept, 1) + " Hz");

        // a prévia: o Projector toca o arquivo refeito assim que a edição entra no projeto
        ch = channelOf (model, clip);
        const auto now = notetune::playedFile (voiceFile, ch->tunedNotes, ch->tuneFormantFollows);
        rep.check (now != voiceFile && now.existsAsFile() && maxDiff (formantOnly, plain) > 1.0e-3f,
                   "a prévia: com a edição no projeto, os blocos tocam o arquivo refeito ("_t + now.getFileName() + ")");
        app.run ("audio.clearNoteTune", args ({ { "item", clip } }));
        rep.check (notetune::playedFile (voiceFile, channelOf (model, clip)->tunedNotes, false) == voiceFile, "sem edição, o bloco toca o original"_t);
    }

    //==========================================================================
    rep.section ("Afinação nota a nota: o editor"_t);
    {
        bool ready = false, shot = false, dragged = false, undone = false, sliders = false, cut = false, joined = false, scale = false, follows = false;
        int shown = 0;

        {
            NoteTuneEditor editor (app, clip);   // fora da área de trabalho: nunca vai para a tela
            editor.setSize (1040, 620);
            ready = editor.waitForAnalysis (20000);
            shown = (int) editor.shownNotes().size();

            // arrastar o Dó duas linhas para cima com o mouse
            const auto r = editor.noteBounds (1);
            const auto from = r.getCentre();
            const float rowH = r.getHeight() / 0.78f;
            editor.mouseDown (mouseAt (editor, from, from));
            editor.mouseDrag (mouseAt (editor, from.translated (0.0f, -2.0f * rowH), from));
            editor.mouseUp (mouseAt (editor, from.translated (0.0f, -2.0f * rowH), from));
            const auto* ch = channelOf (model, clip);
            dragged = ch != nullptr && ch->tunedNotes.size() == sung.size() && std::abs (ch->tunedNotes[1].targetMidi() - 62.0f) < 0.01f
                      && editor.selectedNotes() == std::set<int> { 1 };

            // o controle Centralizar: arrastar não grava; soltar grava um passo
            editor.selectNotes ({ 3 });
            auto& s = editor.centerSlider();
            const auto t = s.getLocalBounds().toFloat();
            const juce::Point<float> right (t.getRight() - 2.0f, t.getBottom() - 10.0f);   // a ponta direita do trilho (100%)
            s.mouseDown (mouseAt (s, right, right));
            const bool notYet = channelOf (model, clip)->tunedNotes[3].center == 0.0f;
            s.mouseUp (mouseAt (s, right, right));
            sliders = notYet && channelOf (model, clip)->tunedNotes[3].center > 0.9f;

            shot = savePng (editor, ctx.work.getChildFile ("nota-a-nota-editor.png"));

            app.run ("edit.undo");
            app.run ("edit.undo");
            undone = channelOf (model, clip)->tunedNotes.empty() && editor.shownNotes().size() == sung.size() && ! editor.shownNotes()[1].edited();

            cut = editor.splitAt (0, 0.45) && channelOf (model, clip)->tunedNotes.size() == sung.size() + 1;
            editor.selectNotes ({ 0, 1 });
            joined = editor.mergeSelected() && channelOf (model, clip)->tunedNotes.size() == sung.size();
            scale = editor.tuneToScale() && std::abs (channelOf (model, clip)->tunedNotes[3].targetMidi() - 62.0f) < 0.01f;
            editor.setFormantFollows (true);
            follows = channelOf (model, clip)->tuneFormantFollows;
            savePng (editor, ctx.work.getChildFile ("nota-a-nota-escala.png"));
        }

        rep.check (ready && shown == (int) sung.size(), "o editor abre sem janela, analisa numa thread dele e mostra as "_t + juce::String (shown) + " notas");
        rep.check (dragged, "arrastar a nota duas linhas para cima com o mouse leva o Dó ao Ré (um comando ao soltar)"_t);
        rep.check (sliders, "o controle Centralizar só grava ao soltar (um passo de desfazer)"_t);
        rep.check (undone, "Ctrl+Z desfaz o controle e o arrasto, e a tela relê o projeto"_t);
        rep.check (cut && joined, "na tela: cortar a nota em duas e juntar as duas escolhidas"_t);
        rep.check (scale && follows, "na tela: a escala do projeto num clique e o timbre junto com o tom"_t);
        rep.check (shot, "captura: nota-a-nota-editor.png e nota-a-nota-escala.png"_t);
        app.run ("audio.clearNoteTune", args ({ { "item", clip } }));
    }

    {
        // trocar o sample do canal: as notas eram do áudio antigo e saem (achado da revisão independente)
        app.run ("audio.editNotes", args ({ { "item", clip }, { "notes", juce::Array<juce::var> { 1 } }, { "to", 62 } }));
        const auto other = work.getChildFile ("outra-voz.wav");
        writeWav (other, sing ({ { 0.1, 0.9, 50.0 } }, 1.0));
        const auto channel = channelOf (model, clip)->id;
        const bool had = ! channelOf (model, clip)->tunedNotes.empty();
        app.run ("channel.setSample", args ({ { "channel", channel }, { "path", other.getFullPathName() }, { "detectRoot", false } }));
        rep.check (had && channelOf (model, clip)->tunedNotes.empty(), "trocar o sample do canal tira as notas afinadas do áudio antigo"_t);
        app.run ("edit.undo");
    }

    {
        // um bloco invertido não abre o editor nem aceita as edições (o arquivo invertido é outro)
        app.run ("audio.reverse", args ({ { "item", clip } }));
        rep.check (! app.run ("audio.editNotes", args ({ { "item", clip }, { "to", 60 } })).ok, "bloco invertido: a afinação nota a nota é recusada"_t);
        app.run ("audio.reverse", args ({ { "item", clip } }));
    }
}

} // namespace whynot::selftest
