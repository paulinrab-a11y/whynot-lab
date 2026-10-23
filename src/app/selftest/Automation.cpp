// Autoteste da automação (entrega 3.5): o corte de um filtro que anda, medido no WAV exportado, o
// volume com curva, a volta ao valor do efeito quando a curva sai, o desfazer e o salvar/reabrir.

#include "SelfTestKit.h"
#include "../../automation/AutomationTarget.h"

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;

    bool writeNoise (const juce::File& f, double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        juce::Random random (7);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, random.nextFloat() - 0.5f);

        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    /** O brilho do ruído filtrado, em janelas de meio segundo: o ruído passa-baixas cruza o zero
        mais vezes quanto mais alto o corte, então a "frequência" medida acompanha o corte. */
    std::vector<double> brightness (const Audio& a, double from, double to)
    {
        std::vector<double> hz;

        for (double t = from; t + 0.5 <= to; t += 0.5)
            hz.push_back (measureHz (a, t, t + 0.5));

        return hz;
    }

    /** Energia acima de 2 kHz entre t0 e t1: passa-altas de 24 dB/oitava (dois biquads) e RMS em dB. */
    float energyAbove2k (const Audio& a, double t0, double t1)
    {
        if (! a.valid)
            return -200.0f;

        // biquad passa-altas (RBJ, Q = 0,707), duas vezes em série
        const double w0 = 2.0 * juce::MathConstants<double>::pi * 2000.0 / a.sampleRate, cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.7071);
        const double a0 = 1.0 + alpha, b0 = (1.0 + cw) / 2.0 / a0, b1 = -(1.0 + cw) / a0, b2 = b0, a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
        double s1[2] {}, s2[2] {};   // estados (forma direta transposta II) de cada estágio

        auto stage = [&] (double x, double* st)
        {
            const double y = b0 * x + st[0];
            st[0] = b1 * x - a1 * y + st[1];
            st[1] = b2 * x - a2 * y;
            return y;
        };

        const int from = juce::jmax (0, (int) (t0 * a.sampleRate)), to = juce::jmin (a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));
        double sum = 0.0;

        for (int i = 0; i < to; ++i)   // desde o começo, para o filtro de medida já estar assentado
        {
            const double y = stage (stage (a.buffer.getSample (0, i), s1), s2);

            if (i >= from)
                sum += y * y;
        }

        return to > from ? (float) (10.0 * std::log10 (sum / (to - from) + 1.0e-20)) : -200.0f;
    }

    float rmsDbIn (const Audio& a, double t0, double t1)
    {
        const int from = juce::jmax (0, (int) (t0 * a.sampleRate)), to = juce::jmin (a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));
        double sum = 0.0;

        for (int i = from; i < to; ++i)
            sum += (double) a.buffer.getSample (0, i) * a.buffer.getSample (0, i);

        return to > from ? (float) (10.0 * std::log10 (sum / (to - from) + 1.0e-20)) : -200.0f;
    }

    juce::String list (const std::vector<double>& v)
    {
        juce::StringArray s;

        for (auto x : v)
            s.add (juce::String (juce::roundToInt (x)));

        return s.joinIntoString (", ");
    }

    juce::var point (double t, const juce::var& v, const char* key = "value")
    {
        return args ({ { "t", t }, { key, v } });
    }
}

void runAutomationSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("automacao");
    work.createDirectory();

    rep.section ("Automação: o corte de um filtro que anda (3.5)"_t);
    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));

    // 4 compassos de ruído branco (6,86 s a 140 BPM) na playlist, com um filtro passa-baixas em 1 kHz.
    const auto noiseFile = work.getChildFile ("ruido.wav");
    writeNoise (noiseFile, 7.0);
    const auto added = app.run ("playlist.addItem", args ({ { "path", noiseFile.getFullPathName() }, { "track", 1 }, { "start", 0 },
                                                            { "length", 4 }, { "unit", "bars" } }));
    const auto* ch = model.get().findChannel (added.value["ref"].toString());
    const auto mx = ch != nullptr ? ch->output : juce::String();
    const auto ins = app.run ("mixer.addInsert", args ({ { "track", mx }, { "type", "filter" } })).value["insert"].toString();
    auto filterParams = new juce::DynamicObject();
    filterParams->setProperty ("cutoff", 1000);
    filterParams->setProperty ("slope", 1);   // 24 dB/oitava: o brilho acompanha o corte de perto
    app.run ("mixer.setInsertParam", args ({ { "track", mx }, { "insert", ins }, { "params", juce::var (filterParams) } }));
    rep.check (added.ok && ch != nullptr && ins.isNotEmpty(), "ruído na playlist e um filtro passa-baixas (1 kHz, 24 dB/oitava) na trilha dele"_t);

    const double bar = ticksToSeconds (ticksPerBar4, 140.0);
    const double songEnd = 4.0 * bar;
    auto render = [&] (const juce::String& name)
    {
        const auto file = work.getChildFile (name);
        app.run ("project.exportWav", args ({ { "path", file.getFullPathName() }, { "what", "song" } }));
        return readWav (file);
    };

    // O filtro parado em 1 kHz dá a régua: quantos cruzamentos por segundo cada Hz de corte rende.
    const auto still = render ("filtro-parado.wav");
    const auto base = brightness (still, 0.25, songEnd - 0.1);
    double baseMean = 0.0;

    for (auto x : base)
        baseMean += x / (double) juce::jmax ((size_t) 1, base.size());

    const double perHz = baseMean / 1000.0;
    rep.check (base.size() >= 12 && std::abs (base.front() / base.back() - 1.0) < 0.2 && perHz > 0.3 && perHz < 1.2,
               "sem automação o brilho fica parado (cruzamentos por segundo: "_t + list (base) + ")");

    // Os comandos (contrato do núcleo: addLane { target }, setPoints { lane, points }, removeLane { lane })
    auto target = [&] (const char* param) { return args ({ { "kind", "insert" }, { "track", mx }, { "insert", ins }, { "param", param } }); };
    rep.check (! app.run ("automation.addLane", args ({ { "target", target ("bpm") } })).ok
                   && ! app.run ("automation.addLane", args ({ { "target", target ("naoexiste") } })).ok,
               "parâmetro escondido ou que não existe é recusado"_t);

    const auto targets = app.run ("automation.targets", args ({ { "track", mx } })).value["targets"];
    bool listed = false;

    for (int i = 0; i < targets.size(); ++i)
        listed = listed || (targets[i]["target"]["param"].toString() == "cutoff" && (double) targets[i]["max"] == 20000.0
                            && targets[i]["scale"].toString() == "log");

    rep.check (targets.size() > 2 && listed, "automation.targets lista o volume, o pan e o corte do filtro (escala log)"_t);

    const auto lane = app.run ("automation.addLane", args ({ { "target", target ("cutoff") } })).value["lane"].toString();
    rep.check (lane.isNotEmpty() && app.run ("automation.addLane", args ({ { "target", target ("cutoff") } })).value["lane"].toString() == lane,
               "uma curva por parâmetro: pedir de novo devolve a mesma"_t);

    // Pontos: um por v (posição do knob) e outro por value (Hz), para conferir os dois jeitos.
    const automation::Range cutoffRange { 20.0f, 20000.0f, automation::Range::Scale::log };
    const auto set = app.run ("automation.setPoints", args ({ { "lane", lane }, { "unit", "bars" },
                                                              { "points", juce::Array<juce::var> { point (0, cutoffRange.toNormalised (200.0f), "v"),
                                                                                                   point (4, 8000) } } }));
    const auto* laneNow = model.get().findLane (lane);
    rep.check (set.ok && laneNow != nullptr && laneNow->points.size() == 2 && laneNow->points[1].t == 4 * ticksPerBar4
                   && std::abs (laneNow->points[1].v - cutoffRange.toNormalised (8000.0f)) < 1.0e-4f,
               "a curva do corte: 200 Hz no compasso 1, 8 kHz no fim do 4 (guardada como posição do knob)"_t);

    // O corte que anda
    const auto sweepAudio = render ("filtro-andando.wav");
    const auto sweep = brightness (sweepAudio, 0.25, songEnd - 0.1);
    bool rising = sweep.size() >= 12;

    for (size_t i = 1; i < sweep.size(); ++i)
        rising = rising && sweep[i] > 0.9 * sweep[i - 1];

    rep.check (rising && sweep.front() < perHz * 400.0 && sweep.back() > perHz * 3500.0,
               "o brilho sobe junto com o corte: "_t + list (sweep) + " cruzamentos por segundo");

    const float firstBar = energyAbove2k (sweepAudio, 0.0, bar), lastBar = energyAbove2k (sweepAudio, 3.0 * bar, 4.0 * bar);
    const float stillFirst = energyAbove2k (still, 0.0, bar), stillLast = energyAbove2k (still, 3.0 * bar, 4.0 * bar);
    rep.check (lastBar - firstBar > 20.0f && std::abs (stillLast - stillFirst) < 1.5f,
               "energia acima de 2 kHz: "_t + juce::String (firstBar, 1) + " dB no compasso 1, " + juce::String (lastBar, 1)
                   + " dB no 4 (com o filtro parado: " + juce::String (stillFirst, 1) + " e " + juce::String (stillLast, 1) + " dB)");

    // No meio de uma janela, o corte que a curva pede (o knob anda reto em escala log) contra o medido.
    const std::vector<automation::Point> sweepPoints { { 0, cutoffRange.toNormalised (200.0f) }, { 4 * ticksPerBar4, cutoffRange.toNormalised (8000.0f) } };
    const size_t mid = sweep.size() / 2;
    const double midSeconds = 0.25 + 0.5 * (double) mid + 0.25;
    const double asked = automation::valueAt (sweepPoints, cutoffRange, secondsToTicks (midSeconds, 140.0), 0.0f);
    const double measuredCut = sweep.size() > mid ? sweep[mid] / perHz : 0.0;
    rep.check (measuredCut > 0.7 * asked && measuredCut < 1.4 * asked,
               "em "_t + juce::String (midSeconds, 2) + " s a curva pede " + juce::String (juce::roundToInt (asked)) + " Hz e o filtro soa em ~"
                   + juce::String (juce::roundToInt (measuredCut)) + " Hz (a curva anda no knob, em escala log)");

    //--------------------------------------------------------------------------
    rep.section ("Automação: desfazer, a curva que sai e o volume"_t);
    rep.check (app.run ("edit.undo").ok && model.get().findLane (lane) != nullptr && model.get().findLane (lane)->points.empty(),
               "desfazer tira os pontos (a curva vazia não toca)"_t);
    const auto afterUndo = brightness (render ("filtro-desfeito.wav"), 0.25, songEnd - 0.1);
    rep.check (! afterUndo.empty() && std::abs (afterUndo.front() / base.front() - 1.0) < 0.2 && std::abs (afterUndo.back() / base.back() - 1.0) < 0.2,
               "sem pontos o filtro volta ao valor do efeito (1 kHz): "_t + list (afterUndo));
    rep.check (app.run ("edit.redo").ok && model.get().findLane (lane) != nullptr && model.get().findLane (lane)->points.size() == 2,
               "refazer devolve os pontos");
    rep.check (app.run ("automation.removeLane", args ({ { "lane", lane } })).ok && model.get().findLane (lane) == nullptr, "apaga a curva");

    // Volume: de -30 dB a 0 dB. A curva anda reta no fader (o mesmo do Tracktion).
    const auto volumeLane = app.run ("automation.addLane", args ({ { "target", args ({ { "kind", "mixer" }, { "track", mx }, { "param", "volume" } }) } }))
                                .value["lane"].toString();
    app.run ("automation.setPoints", args ({ { "lane", volumeLane }, { "unit", "bars" },
                                             { "points", juce::Array<juce::var> { point (0, -30), point (4, 0) } } }));
    const auto fade = render ("volume-subindo.wav");
    const automation::Range fader { -100.0f, 6.0f, automation::Range::Scale::fader };
    const std::vector<automation::Point> fadePoints { { 0, fader.toNormalised (-30.0f) }, { 4 * ticksPerBar4, fader.toNormalised (0.0f) } };
    auto expectedDb = [&] (double seconds) { return automation::valueAt (fadePoints, fader, secondsToTicks (seconds, 140.0), 0.0f); };
    const double t0 = 0.5, t1 = songEnd - 0.6;
    const double measured = rmsDbIn (fade, t1 - 0.25, t1 + 0.25) - rmsDbIn (fade, t0 - 0.25, t0 + 0.25);
    const double expected = expectedDb (t1) - expectedDb (t0);
    rep.check (std::abs (measured - expected) < 1.5,
               "o volume sobe "_t + juce::String (measured, 1) + " dB entre 0,5 s e " + juce::String (t1, 1) + " s (a curva pede "
                   + juce::String (expected, 1) + " dB)");

    //--------------------------------------------------------------------------
    rep.section ("Automação: salvar e reabrir"_t);
    const auto before = work.getChildFile ("volume-subindo.wav");
    const auto projectFile = work.getChildFile ("projeto").getChildFile ("Automação.wnl"_t);
    rep.check (app.run ("project.save", args ({ { "path", projectFile.getFullPathName() } })).ok
                   && projectFile.loadFileAsString().contains ("\"automation\""), "o .wnl guarda as curvas (campo opcional)");
    app.run ("project.new");
    app.run ("project.open", args ({ { "path", projectFile.getFullPathName() } }));
    const auto* reopened = model.get().findLane (volumeLane);
    rep.check (reopened != nullptr && reopened->points.size() == 2 && reopened->target.param == "volume"
                   && std::abs (reopened->points[0].v - fader.toNormalised (-30.0f)) < 1.0e-4f, "a curva do volume volta igual");
    const float diff = maxDiff (readWav (before), render ("volume-reaberto.wav"));
    rep.check (diff < 1.0e-6f, "o projeto reaberto soa igual (diferença máxima "_t + juce::String (diff) + ")");

    // Efeito que sai leva a curva junto (o desfazer devolve os dois).
    const auto cutLane = app.run ("automation.addLane", args ({ { "target", target ("cutoff") } })).value["lane"].toString();
    app.run ("automation.setPoints", args ({ { "lane", cutLane }, { "points", juce::Array<juce::var> { point (0, 500) } } }));
    app.run ("mixer.removeInsert", args ({ { "track", mx }, { "insert", ins } }));
    rep.check (model.get().findLane (cutLane) == nullptr && model.get().findLane (volumeLane) != nullptr,
               "remover o filtro apaga a curva dele e deixa a do volume"_t);
}

} // namespace whynot::selftest
