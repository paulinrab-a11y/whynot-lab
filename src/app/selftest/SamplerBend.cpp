// Autoteste do pitch bend e do mod wheel no sampler nativo e no WHYNOT Piano (frente da noite de 27/10). Dono: Sampler.
// Tudo pelo caminho de verdade: curvas desenhadas nas notas (pianoroll.addNotes), o projetor, o plugin e o render.
// - sampler: +2 st no meio da nota = a nota 2 st acima, em Hz; o alcance do canal (12) e o vibrato do CC1;
// - bend em 0: com a curva em outra nota, o canal recebe o pitch wheel no meio e a nota sem curva sai bit a bit igual;
// - 808 (mono, slide): o slide continua o mesmo, e com bend chega à nota do slide mais o bend;
// - WHYNOT Piano: o mesmo, numa biblioteca de teste de senoides (sem depender do Salamander).

#include "SelfTestKit.h"
#include "../../engine/PianoLibrary.h"

namespace whynot::selftest
{

namespace
{
    constexpr double testRate = 48000.0;

    bool writeSine (const juce::File& f, double hz, double seconds, int channels)
    {
        juce::AudioBuffer<float> b (channels, (int) (seconds * testRate));

        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int c = 0; c < channels; ++c)
                b.setSample (c, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / testRate));

        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (testRate).withNumChannels (channels).withBitsPerSample (24));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    double hzOfKey (double key)   { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0); }

    /** O pitch wheel de +2 st em ±2 é 16383: 8191/8192 do alcance (0,02 cent abaixo). */
    double wheelTop (double range)   { return 8191.0 / 8192.0 * range; }

    /** Uma nota (em tempos) com curva de pitch ou de mod: [{ t, v }] com t em tempos desde o começo da nota. */
    juce::var curvedNote (double t, double len, int key, bool slide, const char* curve,
                          std::initializer_list<std::pair<double, double>> pts)
    {
        auto n = noteVar (t, len, key, slide);
        juce::Array<juce::var> list;

        for (auto [pt, v] : pts)
            list.add (args ({ { "t", pt }, { "v", v } }));

        n.getDynamicObject()->setProperty (curve, list);
        return n;
    }

    /** Amostras iguais, bit a bit, de 0 até `seconds` (nos dois canais). */
    bool sameUntil (const Audio& a, const Audio& b, double seconds)
    {
        if (! a.valid || ! b.valid || a.buffer.getNumChannels() != b.buffer.getNumChannels())
            return false;

        const int n = juce::jmin ((int) (seconds * a.sampleRate), a.buffer.getNumSamples(), b.buffer.getNumSamples());

        for (int c = 0; c < a.buffer.getNumChannels(); ++c)
            if (std::memcmp (a.buffer.getReadPointer (c), b.buffer.getReadPointer (c), sizeof (float) * (size_t) n) != 0)
                return false;

        return n > 0;
    }

    /** A menor e a maior frequência em janelas de 40 ms entre t0 e t1 (o vibrato aparece como a diferença). */
    std::pair<double, double> hzSpread (const Audio& a, double t0, double t1)
    {
        double lo = 1.0e9, hi = 0.0;

        for (double t = t0; t + 0.04 <= t1; t += 0.02)
        {
            const double hz = measureHz (a, t, t + 0.04);
            lo = std::min (lo, hz);
            hi = std::max (hi, hz);
        }

        return { lo, hi };
    }
}

void runSamplerBendSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const double beat = beat140;
    int renders = 0;

    auto render = [&] (const juce::String& channel, const juce::Array<juce::var>& notes)
    {
        app.run ("pianoroll.clear", args ({ { "channel", channel } }));
        app.run ("pianoroll.addNotes", args ({ { "channel", channel }, { "notes", notes }, { "unit", "beats" } }));
        const auto f = ctx.work.getChildFile ("bend_" + juce::String (++renders) + ".wav");
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        return readWav (f);
    };

    //--------------------------------------------------------------------------
    rep.section ("Pitch bend no sampler (27/10)"_t);
    {
        const auto sineFile = ctx.work.getChildFile ("bend_la2.wav");
        rep.check (writeSine (sineFile, 220.0, 6.0, 1), "WAV de teste: Lá 2 (220 Hz) de 6 s"_t);
        app.run ("project.new");
        const auto added = app.run ("channel.addSampler", args ({ { "path", sineFile.getFullPathName() }, { "name", "Seno" }, { "rootKey", 57 },
                                                                   { "oneShot", false }, { "detectRole", false } }));
        const auto ch = added.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));
        app.run ("pattern.setLength", args ({ { "steps", 32 } }));   // 8 tempos: 3,4 s

        // 4 tempos: os 2 primeiros no tom, os 2 últimos com +2 st (o salto vira uma rampa de 5 ms)
        const auto bent = render (ch, { curvedNote (0, 4, 57, false, "pitch", { { 0, 0 }, { 1.999, 0 }, { 2, 2 } }) });
        const double before = measureHz (bent, 0.1, 2 * beat - 0.05), after = measureHz (bent, 2 * beat + 0.05, 4 * beat - 0.05);
        const double target = hzOfKey (57 + wheelTop (2.0));
        rep.check (bent.valid && std::abs (before - 220.0) < 0.3 && std::abs (after - target) < 0.3,
                   "a nota com +2 st no meio: "_t + juce::String (before, 2) + " Hz e depois " + juce::String (after, 2) + " Hz ("
                       + juce::String (target, 2) + " esperado)");

        const auto plain = render (ch, { noteVar (0, 4, 59, false) });
        const double reference = measureHz (plain, 2 * beat + 0.05, 4 * beat - 0.05);
        rep.check (plain.valid && std::abs (reference - after) < 0.1,
                   "igual à nota Si 2 sem bend: "_t + juce::String (reference, 2) + " Hz contra " + juce::String (after, 2) + " Hz");

        // bend em 0: a curva numa nota depois faz o canal mandar o pitch wheel no meio no começo da primeira;
        // até a segunda começar, a saída é bit a bit a de um canal sem curva nenhuma
        const auto alone = render (ch, { noteVar (0, 3, 57, false) });
        const auto withWheel = render (ch, { noteVar (0, 3, 57, false), curvedNote (4, 3, 57, false, "pitch", { { 0, 2 } }) });
        rep.check (sameUntil (alone, withWheel, 4 * beat - 0.01), "pitch wheel no meio: a nota sem curva sai bit a bit igual à de um canal sem curva"_t);
        rep.check (std::abs (measureHz (withWheel, 4 * beat + 0.05, 7 * beat - 0.05) - target) < 0.3,
                   "a nota seguinte, com curva, dobra; a do canal sem curva não muda"_t);

        // o alcance do canal: 12 st, e +7 (a quinta)
        app.run ("channel.setBendRange", args ({ { "channel", ch }, { "semitones", 12 } }));
        const auto fifth = render (ch, { curvedNote (0, 4, 57, false, "pitch", { { 0, 7 } }) });
        const double hz5 = measureHz (fifth, 0.1, 4 * beat - 0.05);
        rep.check (fifth.valid && std::abs (hz5 - hzOfKey (64.0)) < 0.4,
                   "alcance de 12 st e +7 st: "_t + juce::String (hz5, 2) + " Hz (" + juce::String (hzOfKey (64.0), 2) + " esperado)");
        app.run ("channel.setBendRange", args ({ { "channel", ch }, { "semitones", 2 } }));

        // mod wheel: a roda toda faz um vibrato de ±0,5 st (213,7 a 226,5 Hz) em volta da nota
        const auto vib = render (ch, { curvedNote (0, 4, 57, false, "mod", { { 0, 1 } }) });
        const auto [lo, hi] = hzSpread (vib, 0.1, 4 * beat - 0.05);
        const double mean = measureHz (vib, 0.0, 2.0 / 5.5);   // dois ciclos do LFO
        rep.check (vib.valid && lo < 217.0 && lo > 211.0 && hi > 223.0 && hi < 229.0 && std::abs (mean - 220.0) < 1.0,
                   "mod wheel no máximo: vibrato de "_t + juce::String (lo, 1) + " a " + juce::String (hi, 1) + " Hz, média "
                       + juce::String (mean, 1) + " Hz (a nota)");
    }

    //--------------------------------------------------------------------------
    rep.section ("Pitch bend no sampler: o 808 com slide (27/10)"_t);
    {
        const auto subFile = ctx.work.getChildFile ("bend_808.wav");
        rep.check (writeSine (subFile, hzOfKey (36), 4.0, 1), "WAV de teste: sub de Dó 1 de 4 s"_t);
        app.run ("project.new");
        const auto added = app.run ("channel.addSampler", args ({ { "path", subFile.getFullPathName() }, { "name", "808 teste" }, { "rootKey", 36 },
                                                                   { "oneShot", false }, { "mono", true }, { "detectRole", false } }));
        const auto ch = added.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));
        app.run ("pattern.setLength", args ({ { "steps", 32 } }));

        // o slide de sempre (Dó 1 a Sol 1), sem curva: no Sol
        const auto slide = render (ch, { noteVar (0, 2, 36, false), noteVar (2, 3, 43, true) });
        const double sol = measureHz (slide, 2 * beat + 0.25, 5 * beat - 0.05);
        rep.check (slide.valid && std::abs (sol - hzOfKey (43)) < 0.2, "o slide do 808 chega ao Sol 1: "_t + juce::String (sol, 2) + " Hz");

        // a curva numa nota depois do slide: até ela, o mesmo slide, bit a bit
        const auto withWheel = render (ch, { noteVar (0, 2, 36, false), noteVar (2, 3, 43, true), curvedNote (6, 1, 36, false, "pitch", { { 0, 1 } }) });
        rep.check (sameUntil (slide, withWheel, 6 * beat - 0.01), "com o pitch wheel no meio, o slide do 808 sai bit a bit igual"_t);

        // o slide com +2 st na nota de chegada: o Lá 1
        const auto bentSlide = render (ch, { noteVar (0, 2, 36, false), curvedNote (2, 3, 43, true, "pitch", { { 0, 2 } }) });
        const double la = measureHz (bentSlide, 2 * beat + 0.25, 5 * beat - 0.05);
        rep.check (bentSlide.valid && std::abs (la - hzOfKey (43 + wheelTop (2.0))) < 0.2,
                   "slide com +2 st chega ao Lá 1: "_t + juce::String (la, 2) + " Hz (" + juce::String (hzOfKey (45), 2) + " esperado)");

        // SONG: o padrão A termina num dive de -2 st (o valor fica depois da nota); o B, sem curva, vem logo depois e
        // toca no tom dele, não no bend do A
        const auto patA = model.get().currentPatternPtr() != nullptr ? model.get().currentPatternPtr()->id : juce::String();
        app.run ("pattern.setLength", args ({ { "steps", 16 } }));
        app.run ("pianoroll.clear", args ({ { "channel", ch } }));
        app.run ("pianoroll.addNotes", args ({ { "channel", ch }, { "unit", "beats" },
                                               { "notes", juce::Array<juce::var> { curvedNote (0, 4, 36, false, "pitch", { { 0, 0 }, { 2, -2 } }) } } }));
        const auto patB = app.run ("pattern.add", args ({ { "name", "Sem curva" }, { "steps", 16 } })).value["pattern"].toString();
        app.run ("pattern.select", args ({ { "pattern", patB } }));
        app.run ("pianoroll.addNotes", args ({ { "channel", ch }, { "unit", "beats" }, { "notes", juce::Array<juce::var> { noteVar (0, 4, 36, false) } } }));
        app.run ("playlist.addItem", args ({ { "ref", patA }, { "track", 1 }, { "start", 0 }, { "unit", "beats" } }));
        app.run ("playlist.addItem", args ({ { "ref", patB }, { "track", 1 }, { "start", 4 }, { "unit", "beats" } }));
        app.run ("transport.setSongMode", args ({ { "on", true } }));
        const auto songFile = ctx.work.getChildFile ("bend_song.wav");
        app.run ("project.exportWav", args ({ { "path", songFile.getFullPathName() }, { "loops", 1 }, { "what", "song" } }));
        const auto song = readWav (songFile);
        const double dive = measureHz (song, 3 * beat, 4 * beat - 0.05), next = measureHz (song, 4 * beat + 0.1, 8 * beat - 0.1);
        rep.check (song.valid && patB.isNotEmpty() && std::abs (dive - hzOfKey (34)) < 0.2 && std::abs (next - hzOfKey (36)) < 0.2,
                   "SONG: o dive do padrão A ("_t + juce::String (dive, 2) + " Hz) não passa para o B sem curva ("
                       + juce::String (next, 2) + " Hz, " + juce::String (hzOfKey (36), 2) + " esperado)");
        app.run ("transport.setSongMode", args ({ { "on", false } }));
    }

    //--------------------------------------------------------------------------
    rep.section ("Pitch bend no WHYNOT Piano (27/10)"_t);
    {
        const auto libFolder = ctx.work.getChildFile ("piano_bend");
        const auto samples = libFolder.getChildFile ("samples");
        samples.createDirectory();
        const bool wrote = writeSine (samples.getChildFile ("la4.wav"), 440.0, 4.0, 2);
        const auto zone = args ({ { "file", "la4.wav" }, { "root", 69 }, { "lo", 60 }, { "hi", 80 }, { "lovel", 1 }, { "hivel", 127 },
                                  { "trigger", "attack" }, { "veltrack", 0.0 }, { "releaseSeconds", 0.2 }, { "volumeDb", 0.0 },
                                  { "rtDecay", 0.0 }, { "keytrack", 1.0 } });
        const auto manifest = args ({ { "format", "whynot-piano" }, { "version", 1 }, { "name", "Piano do bend" }, { "credit", "teste" },
                                      { "license", "teste" }, { "samples", "samples" }, { "zones", juce::Array<juce::var> { zone } } });
        libFolder.getChildFile ("whynot-piano.json").replaceWithText (juce::JSON::toString (manifest));
        piano::setLibraryOverride (libFolder);

        app.run ("project.new");
        const auto added = app.run ("channel.addPiano");
        const auto ch = added.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));
        app.run ("pattern.setLength", args ({ { "steps", 32 } }));
        rep.check (wrote && added.ok && app.getProjector().pianoFor (ch) != nullptr && app.getProjector().pianoFor (ch)->hasLibrary(),
                   "canal do WHYNOT Piano com a biblioteca de teste (Lá 4 em senoide)"_t);

        const auto bent = render (ch, { curvedNote (0, 4, 69, false, "pitch", { { 0, 0 }, { 1.999, 0 }, { 2, 2 } }) });
        const double before = measureHz (bent, 0.1, 2 * beat - 0.05), after = measureHz (bent, 2 * beat + 0.05, 4 * beat - 0.05);
        const double target = hzOfKey (69 + wheelTop (2.0));
        rep.check (bent.valid && std::abs (before - 440.0) < 0.5 && std::abs (after - target) < 0.5,
                   "piano: a nota com +2 st no meio: "_t + juce::String (before, 2) + " Hz e depois " + juce::String (after, 2) + " Hz ("
                       + juce::String (target, 2) + " esperado)");

        const auto plain = render (ch, { noteVar (0, 4, 71, false) });
        const double reference = measureHz (plain, 2 * beat + 0.05, 4 * beat - 0.05);
        rep.check (plain.valid && std::abs (reference - after) < 0.2,
                   "piano: igual à tecla Si 4 sem bend: "_t + juce::String (reference, 2) + " Hz contra " + juce::String (after, 2) + " Hz");

        const auto alone = render (ch, { noteVar (0, 3, 69, false) });
        const auto withWheel = render (ch, { noteVar (0, 3, 69, false), curvedNote (4, 3, 69, false, "pitch", { { 0, 2 } }) });
        rep.check (sameUntil (alone, withWheel, 4 * beat - 0.01), "piano: com o pitch wheel no meio, a nota sem curva sai bit a bit igual"_t);

        const auto vib = render (ch, { curvedNote (0, 4, 69, false, "mod", { { 0, 1 } }) });
        const auto [lo, hi] = hzSpread (vib, 0.1, 4 * beat - 0.05);
        rep.check (vib.valid && lo < 434.0 && lo > 422.0 && hi > 446.0 && hi < 458.0,
                   "piano: mod wheel no máximo faz o vibrato de "_t + juce::String (lo, 1) + " a " + juce::String (hi, 1) + " Hz");

        piano::setLibraryOverride ({});
    }

    app.run ("project.new");
}

} // namespace whynot::selftest
