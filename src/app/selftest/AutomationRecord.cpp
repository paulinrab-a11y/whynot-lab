// Autoteste da automação completa (26/10, item 16 da pesquisa das comunidades): gravar uma passada mexendo no fader
// (com um relógio de mentira, sem placa de som) e medir a curva, a passada por cima, o loop e o punch, o desfazer de
// uma passada só; a tensão medida no WAV exportado; o VST3 de teste voltando ao valor de antes da curva; e a captura
// da raia de automação.

#include "SelfTestKit.h"
#include "../AutomationRecording.h"
#include "../../automation/AutomationTarget.h"
#include "../../model/WnlFormat.h"
#include "../../ui/AutomationLanes.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;

    bool writeNoise (const juce::File& f, double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        juce::Random random (11);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.5f * (random.nextFloat() - 0.5f));

        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    float rmsDbIn (const Audio& a, double t0, double t1)
    {
        if (! a.valid)
            return -200.0f;

        const int from = juce::jmax (0, (int) (t0 * a.sampleRate)), to = juce::jmin (a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));
        double sum = 0.0;

        for (int i = from; i < to; ++i)
            sum += (double) a.buffer.getSample (0, i) * a.buffer.getSample (0, i);

        return to > from ? (float) (10.0 * std::log10 (sum / (to - from) + 1.0e-20)) : -200.0f;
    }

    bool savePng (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();

        if (juce::FileOutputStream out (file); out.openedOk())
        {
            juce::PNGImageFormat png;
            return png.writeImageToStream (image, out) && image.getWidth() > 0;
        }

        return false;
    }

    juce::String db (double v)   { return juce::String (v, 1).replaceCharacter ('.', ','); }
}

void runAutomationRecordSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("automacao-completa");
    work.createDirectory();

    auto* recorder = app.automationRecorder();

    rep.section ("Automação completa: gravar mexendo no fader (26/10)"_t);
    rep.check (recorder != nullptr, "o app tem o gravador de automação"_t);

    if (recorder == nullptr)
        return;

    // o relógio de mentira: o tick que "está tocando" (-1 = parado), e uma medida por passo
    const auto realClock = recorder->clock;
    int now = -1;
    recorder->clock = [&now]() -> std::optional<int> { return now >= 0 ? std::optional<int> (now) : std::nullopt; };
    auto at = [&] (int tick) { now = tick; recorder->poll(); };

    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));

    const auto noiseFile = work.getChildFile ("ruido.wav");
    writeNoise (noiseFile, 7.0);
    const auto added = app.run ("playlist.addItem", args ({ { "path", noiseFile.getFullPathName() }, { "track", 1 }, { "start", 0 },
                                                            { "length", 4 }, { "unit", "bars" } }));
    const auto* ch = model.get().findChannel (added.value["ref"].toString());
    const auto mx = ch != nullptr ? ch->output : juce::String();
    app.run ("mixer.setVolume", args ({ { "track", mx }, { "db", -6.0 } }));
    rep.check (added.ok && mx.isNotEmpty(), "ruído na playlist, na trilha dele, com o fader em -6 dB"_t);

    const auto armed = app.run ("automation.setRecording", args ({ { "on", true } }));
    rep.check (armed.ok && (bool) armed.value["recording"] && app.isAutomationRecording()
                   && (bool) app.run ("automation.list").value["recording"],
               "automation.setRecording liga o modo (e o automation.list conta)"_t);

    AutomationTarget volume;
    volume.kind = "mixer";
    volume.track = mx;
    volume.param = "volume";
    const automation::Range fader { -100.0f, 6.0f, automation::Range::Scale::fader };

    // a passada: 2 compassos (7680 ticks), uma medida a cada 96 ticks (~43 ms a 140 BPM); o fader sobe de -30 a 0 dB
    // no primeiro compasso, num gesto de arrasto, como o mixer faz, e fica parado no segundo
    const auto beforePass = wnl::toJson (model.get());
    const auto undoBefore = app.getCommands().undoDescription();
    at (0);
    app.getCommands().beginGesture ("Volume"_t);   // o arrasto do fader

    for (int i = 0; i <= 80; ++i)
    {
        const int tick = i * 96;

        if (tick <= ticksPerBar4)
            app.run ("mixer.setVolume", args ({ { "track", mx }, { "db", -30.0 + 30.0 * tick / ticksPerBar4 } }));

        at (tick);
    }

    app.getCommands().endGesture();
    rep.check (recorder->isWriting(), "mexendo no fader com o play no SONG: a volta está escrevendo"_t);
    at (-1);   // parou
    rep.check (! recorder->isWriting() && recorder->lapsWritten() == 1, "o stop fecha a volta: uma passada gravada"_t);

    const auto* lane = model.get().laneFor (volume);
    const auto points = lane != nullptr ? automation::pointsOf (*lane) : std::vector<automation::Point>();
    rep.check (lane != nullptr && points.size() >= 2 && points.size() <= 16,
               "a curva do volume nasceu com "_t + juce::String ((int) points.size()) + " pontos, de 81 medidas (a redução tira o que a reta explica)");

    if (lane == nullptr)
    {
        recorder->clock = realClock;
        app.run ("automation.setRecording", args ({ { "on", false } }));
        return;
    }

    auto dbAt = [&] (double tick) { return automation::valueAt (points, fader, tick, 0.0f); };
    const double half = dbAt (ticksPerBar4 / 2), end = dbAt (ticksPerBar4 + ticksPerBar4 / 2);
    rep.check (std::abs (dbAt (0) + 30.0) < 0.6 && std::abs (half + 15.0) < 1.0 && std::abs (end) < 0.2,
               "a curva gravada: "_t + db (dbAt (0)) + " dB no começo, " + db (half) + " no meio do 1º compasso, " + db (end)
                   + " no 2º (o fader pediu -30, -15 e 0)");

    const auto* m = model.get().findMixer (mx);
    rep.check (m != nullptr && std::abs (m->volumeDb + 6.0f) < 0.01f, "o fader (fora da curva) volta ao de antes da passada: -6 dB"_t);
    rep.check (app.getCommands().undoDescription() == "Gravar automação"_t, "o desfazer diz \"Gravar automação\""_t);
    app.run ("edit.undo");
    rep.check (wnl::toJson (model.get()) == beforePass && app.getCommands().undoDescription() == undoBefore,
               "um Ctrl+Z desfaz a passada inteira: os 81 passos do fader e a curva (o projeto volta igual)");
    app.run ("edit.redo");
    rep.check (model.get().laneFor (volume) != nullptr && automation::pointsOf (*model.get().laneFor (volume)) == points, "refazer devolve a passada");

    // outro gesto no meio da volta (arrastar um bloco, uma nota) fecha a volta antes: ele não entra no passo dela
    {
        const int lapsBefore = recorder->lapsWritten();
        at (ticksPerBar4 * 5);
        app.run ("mixer.setVolume", args ({ { "track", mx }, { "db", -9.0 } }));
        at (ticksPerBar4 * 5 + 96);
        app.getCommands().beginGesture ("Mover bloco"_t);
        app.run ("transport.setBpm", args ({ { "bpm", 141.0 } }));
        app.getCommands().endGesture();
        const bool separate = recorder->lapsWritten() == lapsBefore + 1 && app.getCommands().undoDescription() == "Mover bloco"_t;
        app.run ("edit.undo");
        rep.check (separate && std::abs (model.get().bpm - 140.0) < 1.0e-9 && model.get().laneFor (volume) != nullptr
                       && std::abs (automation::valueAt (automation::pointsOf (*model.get().laneFor (volume)), fader, ticksPerBar4 * 5 + 96, 0.0f) + 9.0f) < 0.1f,
                   "um gesto de fora no meio da volta fecha a volta antes: o Ctrl+Z dele não leva a curva gravada"_t);
        app.run ("edit.undo");
        at (-1);

        // o loop volta com o fader ainda seguro: a volta fecha mesmo assim (um passo), e a próxima continua
        at (0);
        app.getCommands().beginGesture ("Volume"_t);
        app.run ("mixer.setVolume", args ({ { "track", mx }, { "db", -12.0 } }));
        at (96);
        at (1900);
        at (40);   // o loop voltou, com o mouse no fader
        const bool closed = app.getCommands().undoDescription() == "Gravar automação"_t && recorder->lapsWritten() == lapsBefore + 2;
        app.run ("mixer.setVolume", args ({ { "track", mx }, { "db", -3.0 } }));
        at (100);
        at (900);
        app.getCommands().endGesture();
        at (-1);
        rep.check (closed && recorder->lapsWritten() == lapsBefore + 3 && ! app.getCommands().isInGesture(),
                   "o loop voltando com o fader seguro fecha a volta num passo próprio (e o gesto do fader fecha certo depois)"_t);
        app.run ("edit.undo");
        app.run ("edit.undo");
        rep.check (model.get().laneFor (volume) != nullptr
                       && automation::pointsOf (*model.get().laneFor (volume)) == points,
                   "dois Ctrl+Z desfazem as duas voltas: a curva volta à da 1ª passada"_t);
    }

    // o som: a curva gravada toca no export
    const double bar = ticksToSeconds (ticksPerBar4, 140.0);
    const auto recordedFile = work.getChildFile ("gravada.wav");
    app.run ("project.exportWav", args ({ { "path", recordedFile.getFullPathName() }, { "what", "song" } }));
    const auto recorded = readWav (recordedFile);
    const double rise = rmsDbIn (recorded, 1.6 * bar, 1.9 * bar) - rmsDbIn (recorded, 0.05, 0.25);
    const double expectedRise = dbAt (secondsToTicks (1.75 * bar, 140.0)) - dbAt (secondsToTicks (0.15, 140.0));
    rep.check (std::abs (rise - expectedRise) < 2.0, "no WAV o volume sobe "_t + db (rise) + " dB do começo ao 2º compasso (a curva pede " + db (expectedRise) + ")");

    //--------------------------------------------------------------------------
    rep.section ("Automação completa: a passada por cima, o loop e o punch"_t);
    const auto first = automation::pointsOf (*model.get().laneFor (volume));
    const int lapsAtSecond = recorder->lapsWritten();
    at (ticksPerBar4 * 2);

    for (int tick = ticksPerBar4 * 2; tick <= ticksPerBar4 * 3; tick += 48)
    {
        if (tick == ticksPerBar4 * 2)
            app.run ("mixer.setVolume", args ({ { "track", mx }, { "db", -20.0 } }));

        at (tick);
    }

    at (-1);
    const auto second = automation::pointsOf (*model.get().laneFor (volume));
    const float kept = automation::positionAt (second, ticksPerBar4 / 2, 0.0f), keptBefore = automation::positionAt (first, ticksPerBar4 / 2, 0.0f);
    rep.check (std::abs (kept - keptBefore) < 1.0e-4f && std::abs (automation::valueAt (second, fader, ticksPerBar4 * 2.5, 0.0f) + 20.0f) < 0.1f
                   && recorder->lapsWritten() == lapsAtSecond + 1,
               "a 2ª passada (no 3º compasso, em -20 dB) troca só o trecho dela: o 1º compasso fica igual"_t);

    // o loop: o tick volta ao começo no meio da passada. Cada volta com algo mexido é um passo de desfazer.
    app.run ("mixer.setPan", args ({ { "track", mx }, { "pan", 0.0 } }));
    at (0);
    app.run ("mixer.setPan", args ({ { "track", mx }, { "pan", -0.5 } }));
    at (100);
    at (1900);
    at (50);    // voltou: a volta 1 fecha aqui
    app.run ("mixer.setPan", args ({ { "track", mx }, { "pan", 0.5 } }));
    at (60);
    at (900);
    at (-1);
    AutomationTarget pan = volume;
    pan.param = "pan";
    const auto* panLane = model.get().laneFor (pan);
    const auto panPoints = panLane != nullptr ? automation::pointsOf (*panLane) : std::vector<automation::Point>();
    rep.check (recorder->lapsWritten() == lapsAtSecond + 3 && panLane != nullptr
                   && std::abs (automation::valueAt (panPoints, { -1.0f, 1.0f }, 500, 0.0f) - 0.5f) < 0.01f
                   && std::abs (automation::valueAt (panPoints, { -1.0f, 1.0f }, 1500, 0.0f) + 0.5f) < 0.01f,
               "com o loop, cada volta é uma passada: a 2ª (pan à direita até 900) por cima da 1ª (à esquerda até 1900)"_t);
    app.run ("edit.undo");
    const auto* panUndone = model.get().laneFor (pan);
    rep.check (panUndone != nullptr && std::abs (automation::valueAt (automation::pointsOf (*panUndone), { -1.0f, 1.0f }, 500, 0.0f) + 0.5f) < 0.01f,
               "Ctrl+Z desfaz só a última volta: o pan volta à esquerda"_t);

    // o punch: só fica o que foi mexido entre o início e o fim
    app.run ("record.setPunch", args ({ { "on", true }, { "start", 1 }, { "end", 2 }, { "unit", "bars" } }));
    const auto beforePunch = automation::pointsOf (*model.get().laneFor (volume));
    at (0);
    app.run ("mixer.setVolume", args ({ { "track", mx }, { "db", -40.0 } }));

    for (int tick = 0; tick <= ticksPerBar4 * 3; tick += 96)
        at (tick);

    at (-1);
    app.run ("record.setPunch", args ({ { "on", false } }));
    const auto punched = automation::pointsOf (*model.get().laneFor (volume));
    rep.check (std::abs (automation::valueAt (punched, fader, ticksPerBar4 * 1.5, 0.0f) + 40.0f) < 0.1f
                   && std::abs (automation::positionAt (punched, ticksPerBar4 * 0.5, 0.0f) - automation::positionAt (beforePunch, ticksPerBar4 * 0.5, 0.0f)) < 1.0e-4f
                   && std::abs (automation::positionAt (punched, ticksPerBar4 * 2.5, 0.0f) - automation::positionAt (beforePunch, ticksPerBar4 * 2.5, 0.0f)) < 1.0e-4f,
               "com o punch no 2º compasso, a passada só escreve nele"_t);

    // pelo comando (a IA): uma passada sem mexer em nada, só os pontos
    const auto viaAi = app.run ("automation.recordPass", args ({ { "unit", "bars" },
        { "passes", juce::Array<juce::var> { args ({ { "target", args ({ { "kind", "mixer" }, { "track", mx }, { "param", "volume" } }) },
                                                     { "points", juce::Array<juce::var> { args ({ { "t", 3 }, { "value", -12 } }), args ({ { "t", 3.5 }, { "value", -12 } }),
                                                                                         args ({ { "t", 4 }, { "value", -3 } }) } } }) } } }));
    const auto afterAi = automation::pointsOf (*model.get().laneFor (volume));
    rep.check (viaAi.ok && std::abs (automation::valueAt (afterAi, fader, ticksPerBar4 * 3.25, 0.0f) + 12.0f) < 0.1f
                   && std::abs (automation::valueAt (afterAi, fader, ticksPerBar4 * 1.5, 0.0f) + 40.0f) < 0.1f,
               "automation.recordPass (para a IA) escreve do compasso 4 ao fim, por cima, e deixa o resto"_t);
    rep.check (! app.run ("automation.recordPass", args ({ { "passes", juce::Array<juce::var> { args ({ { "target", args ({ { "kind", "mixer" }, { "track", "mx_naoexiste" }, { "param", "volume" } }) },
                                                                                                        { "points", juce::Array<juce::var> { args ({ { "t", 0 }, { "v", 0.5 } }) } } }) } } })).ok
                   && ! app.run ("automation.recordPass", args ({ { "passes", juce::Array<juce::var>() } })).ok,
               "alvo que não existe ou passada vazia são recusados"_t);

    app.run ("automation.setRecording", args ({ { "on", false } }));
    rep.check (! app.isAutomationRecording(), "desligar o modo"_t);

    //--------------------------------------------------------------------------
    rep.section ("Automação completa: a tensão, medida no render"_t);
    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));
    const auto again = app.run ("playlist.addItem", args ({ { "path", noiseFile.getFullPathName() }, { "track", 1 }, { "start", 0 },
                                                            { "length", 4 }, { "unit", "bars" } }));
    const auto* ch2 = model.get().findChannel (again.value["ref"].toString());
    const auto mx2 = ch2 != nullptr ? ch2->output : juce::String();
    const auto volumeLane = app.run ("automation.addLane", args ({ { "target", args ({ { "kind", "mixer" }, { "track", mx2 }, { "param", "volume" } }) } }))
                                .value["lane"].toString();

    const std::vector<automation::Point> straight { { 0, fader.toNormalised (-40.0f) }, { 4 * ticksPerBar4, fader.toNormalised (0.0f) } };
    const auto straightJson = [&]
    {
        app.run ("automation.setPoints", args ({ { "lane", volumeLane }, { "unit", "bars" },
                                                 { "points", juce::Array<juce::var> { args ({ { "t", 0 }, { "value", -40 } }), args ({ { "t", 4 }, { "value", 0 } }) } } }));
        return wnl::toJson (model.get());
    }();
    rep.check (! straightJson.contains ("\"tension\""), "curva reta: o .wnl sai como antes, sem \"tension\"");

    const auto curved = app.run ("automation.setTension", args ({ { "lane", volumeLane }, { "t", 0 }, { "tension", 0.75 } }));
    const auto* bentLane = model.get().findLane (volumeLane);
    rep.check (curved.ok && bentLane != nullptr && std::abs (bentLane->points[0].tension - 0.75f) < 1.0e-4f && bentLane->points[1].tension == 0.0f
                   && ! app.run ("automation.setTension", args ({ { "lane", volumeLane }, { "t", 4 * ticksPerBar4 }, { "tension", 0.5 } })).ok,
               "automation.setTension curva o trecho do 1º ponto; o último ponto não tem trecho"_t);

    bentLane = model.get().findLane (volumeLane);   // o comando de cima normalizou o projeto: o ponteiro velho não vale
    const auto bent = bentLane != nullptr ? automation::pointsOf (*bentLane) : std::vector<automation::Point>();
    const auto bentFile = work.getChildFile ("tensao.wav");
    app.run ("project.exportWav", args ({ { "path", bentFile.getFullPathName() }, { "what", "song" } }));
    const auto bentAudio = readWav (bentFile);
    const double songEnd = 4.0 * bar;
    const float top = rmsDbIn (bentAudio, songEnd - 0.35, songEnd - 0.05);   // perto do fim a curva chega em 0 dB
    juce::StringArray measured;
    bool follows = true, differs = false;

    for (double frac : { 0.25, 0.5, 0.75 })
    {
        const double t = frac * songEnd;
        const double got = rmsDbIn (bentAudio, t - 0.15, t + 0.15) - top;
        const double want = automation::valueAt (bent, fader, secondsToTicks (t, 140.0), 0.0f)
                            - automation::valueAt (bent, fader, secondsToTicks (songEnd - 0.2, 140.0), 0.0f);
        const double line = automation::valueAt (straight, fader, secondsToTicks (t, 140.0), 0.0f)
                            - automation::valueAt (straight, fader, secondsToTicks (songEnd - 0.2, 140.0), 0.0f);
        follows = follows && std::abs (got - want) < 1.5;
        differs = differs || std::abs (got - line) > 4.0;
        measured.add (db (got) + " (pede " + db (want) + ", reta " + db (line) + ")");
    }

    rep.check (follows && differs, "o volume com tensão 0,75, medido no WAV em 1/4, 1/2 e 3/4: "_t + measured.joinIntoString ("; ") + " dB");

    const auto json = wnl::toJson (model.get());
    Project back;
    rep.check (json.contains ("\"tension\": 0.75") && json == wnl::toJsonReference (model.get()) && wnl::fromJson (json, back).wasOk()
                   && back.findLane (volumeLane) != nullptr && model.get().findLane (volumeLane) != nullptr
                   && back.findLane (volumeLane)->points == model.get().findLane (volumeLane)->points,
               "a tensão no .wnl: campo opcional, toJson e toJsonReference iguais, e volta igual"_t);

    app.run ("edit.undo");
    rep.check (model.get().findLane (volumeLane) != nullptr && model.get().findLane (volumeLane)->points[0].tension == 0.0f,
               "desfazer deixa o trecho reto de novo"_t);
    app.run ("edit.redo");

    //--------------------------------------------------------------------------
    rep.section ("Automação completa: o VST3 volta ao valor de antes da curva"_t);
    juce::String gainId;

    if (const auto listed = app.run ("plugins.list"); auto* list = listed.value["plugins"].getArray())
        for (auto& d : *list)
            if (d["name"].toString() == "WHYNOT Teste Ganho")
                gainId = d["id"].toString();

    const auto insAdded = gainId.isNotEmpty() ? app.run ("mixer.addPluginInsert", args ({ { "track", mx2 }, { "plugin", gainId } })) : CommandResult::fail ("sem plugin");
    const auto ins = insAdded.value["insert"].toString();
    auto* plugin = ins.isNotEmpty() ? app.getProjector().insertPlugin (mx2, ins) : nullptr;
    auto gainParam = plugin != nullptr ? plugin->getAutomatableParameterByID ("0") : nullptr;
    rep.check (insAdded.ok && gainParam != nullptr, "o WHYNOT Teste Ganho entra como efeito na trilha (parâmetro 0)"_t + (insAdded.ok ? juce::String() : ": " + insAdded.error));

    if (gainParam != nullptr)
    {
        // sem tocar, o Tracktion só põe o valor da curva no parâmetro quando o transporte anda: aqui, no começo
        auto followed = [&] { gainParam->updateToFollowCurve (te::TimePosition()); return gainParam->getCurrentValue(); };

        gainParam->setParameter (0.3f, juce::sendNotification);   // a pessoa deixou o ganho em 0,3
        AutomationTarget g;
        g.kind = "insert";
        g.track = mx2;
        g.insert = ins;
        g.param = "0";
        auto gainTarget = args ({ { "kind", "insert" }, { "track", mx2 }, { "insert", ins }, { "param", "0" } });
        const auto gainLane = app.run ("automation.addLane", args ({ { "target", gainTarget } })).value["lane"].toString();
        app.run ("automation.setPoints", args ({ { "lane", gainLane }, { "points", juce::Array<juce::var> { args ({ { "t", 0 }, { "v", 0.9 } }) } } }));
        const float during = followed();
        rep.check (std::abs (during - 0.9f) < 0.01f && app.getProjector().pluginBaseFor (g).has_value(),
                   "com a curva, o ganho vai a "_t + juce::String (during, 2) + " (e o app guarda o 0,3 de antes)");

        app.run ("automation.removeLane", args ({ { "lane", gainLane } }));
        const float after = gainParam->getCurrentValue();
        rep.check (std::abs (after - 0.3f) < 0.01f, "apagar a curva: o ganho volta a "_t + juce::String (after, 2) + " (antes ficava no 0,9 da curva)");

        app.run ("edit.undo");
        const float undone = followed();
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        const float inPat = gainParam->getCurrentValue();
        app.run ("transport.setSongMode", args ({ { "on", true } }));
        rep.check (std::abs (undone - 0.9f) < 0.01f && std::abs (inPat - 0.3f) < 0.01f && std::abs (followed() - 0.9f) < 0.01f,
                   "desfazer devolve a curva (0,9); no PAT o ganho volta a 0,3, e no SONG a curva manda de novo"_t);
        app.run ("automation.removeLane", args ({ { "lane", gainLane } }));

        // gravar mexendo no VST3 (a janela do plugin muda o parâmetro): o valor de antes da passada é o que volta
        gainParam->setParameter (0.25f, juce::sendNotification);
        app.run ("automation.setRecording", args ({ { "on", true } }));
        at (0);

        for (int i = 0; i <= 40; ++i)
        {
            gainParam->setParameter (0.25f + 0.5f * (float) i / 40.0f, juce::sendNotification);   // como a mão no knob do plugin
            at (i * 48);
        }

        at (-1);
        app.run ("automation.setRecording", args ({ { "on", false } }));
        const auto* recordedGain = model.get().laneFor (g);
        const auto gainPoints = recordedGain != nullptr ? automation::pointsOf (*recordedGain) : std::vector<automation::Point>();
        rep.check (recordedGain != nullptr && gainPoints.size() <= 4 && std::abs (automation::positionAt (gainPoints, 960, 0.0f) - 0.5f) < 0.02f,
                   "mexer no VST3 gravando escreve a curva dele ("_t + juce::String ((int) gainPoints.size()) + " pontos, 0,5 no meio)");

        if (recordedGain != nullptr)
            app.run ("automation.removeLane", args ({ { "lane", recordedGain->id } }));

        rep.check (std::abs (gainParam->getCurrentValue() - 0.25f) < 0.01f,
                   "apagar a curva gravada: o VST3 volta ao 0,25 de antes da passada ("_t + juce::String (gainParam->getCurrentValue(), 2) + ")");
    }

    //--------------------------------------------------------------------------
    rep.section ("Automação completa: a raia de automação na tela"_t);
    {
        // a curva com tensão e uma de pan com pontos, com o modo de gravar ligado (o aviso no cabeçalho)
        const auto panLaneId = app.run ("automation.addLane", args ({ { "target", args ({ { "kind", "mixer" }, { "track", mx2 }, { "param", "pan" } }) } }))
                                   .value["lane"].toString();
        app.run ("automation.setPoints", args ({ { "lane", panLaneId }, { "unit", "bars" },
                                                 { "points", juce::Array<juce::var> { args ({ { "t", 0 }, { "value", 0 } }), args ({ { "t", 1 }, { "value", -0.8 }, { "tension", -0.6 } }),
                                                                                     args ({ { "t", 2.5 }, { "value", 0.8 }, { "tension", 0.6 } }), args ({ { "t", 4 }, { "value", 0 } }) } } }));
        app.run ("automation.setRecording", args ({ { "on", true } }));

        juce::Component parent;   // fora da área de trabalho: não rouba o foco de ninguém
        AutomationLanes lanes (app);
        parent.setSize (1100, 400);
        parent.addAndMakeVisible (lanes);
        lanes.refresh (model.get());
        lanes.setView (0.0, 220.0f / (float) ticksPerBar4);
        lanes.setBounds (0, 0, 1100, lanes.getIdealHeight());
        const auto shot = work.getChildFile ("raia-automacao.png");
        rep.check (savePng (lanes, shot) && lanes.getIdealHeight() == AutomationLanes::headerHeight + 2 * AutomationLanes::laneHeight,
                   "captura da raia com a tensão e o aviso de gravar: "_t + shot.getFullPathName());

        app.run ("automation.setRecording", args ({ { "on", false } }));
        parent.removeChildComponent (&lanes);
    }

    recorder->clock = realClock;   // o relógio de mentira sai com o teste
}

} // namespace whynot::selftest
