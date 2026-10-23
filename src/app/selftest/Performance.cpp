// Autoteste de desempenho (entrega 3.5): o medidor de CPU e de falhas de buffer, e congelar uma
// trilha (Track::setFrozen) pelo canal. Prova que o som congelado bate com o render sem congelar
// (diferença máxima), que o plugin do canal fica desligado, que congela de novo quando o padrão
// muda, que o export sai igual e que o .wnl guarda o estado.

#include "SelfTestKit.h"
#include "../../engine/Exporter.h"
#include "../../engine/WhynotSampler.h"

namespace whynot::selftest
{

namespace
{
    /** Lê com os formatos do motor (o arquivo congelado do Tracktion não é WAV). */
    Audio readWithEngine (AppController& app, const juce::File& f)
    {
        Audio a;
        std::unique_ptr<juce::AudioFormatReader> r (app.getEngine().getAudioFileFormatManager().readFormatManager.createReaderFor (f));

        if (r == nullptr)
            return a;

        a.buffer.setSize ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&a.buffer, 0, (int) r->lengthInSamples, 0, true, true);
        a.sampleRate = r->sampleRate;
        a.valid = true;
        return a;
    }

    /** O sampler do canal está ligado? (-1 = não achei o sampler) */
    int samplerEnabled (AppController& app, const juce::String& channel)
    {
        if (auto* track = app.getProjector().trackFor (channel))
            for (auto* plugin : track->pluginList)
                if (dynamic_cast<WhynotSamplerPlugin*> (plugin) != nullptr)
                    return plugin->isEnabled() ? 1 : 0;

        return -1;
    }
}

void runPerformanceSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("desempenho");
    work.createDirectory();

    rep.section ("Desempenho: medidor de CPU e de falhas de buffer (3.5)"_t);
    const auto perf = app.run ("audio.performance");
    rep.check (perf.ok && perf.value.hasProperty ("cpu") && perf.value.hasProperty ("cpuPeak") && (int) perf.value["xruns"] == 0
                   && ! (bool) perf.value["overloaded"],
               "audio.performance responde (sem placa: CPU 0, nenhuma falha, sem aviso)"_t);
    rep.check (app.run ("audio.resetXruns").ok && app.performanceSnapshot().xruns == 0, "zera as falhas de buffer"_t);

    //==========================================================================
    rep.section ("Desempenho: congelar canal"_t);
    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", false } }));
    const auto kick = channelId (model.get(), "Kick");
    const auto hat = channelId (model.get(), "Hat");
    setGrid (app, kick, "x...x...x...x...");
    setGrid (app, hat, "x.x.x.x.x.x.x.x.", 90);
    const auto* kickChannel = model.get().findChannel (kick);
    const auto mxKick = kickChannel != nullptr ? model.get().resolveOutput (kickChannel->output) : juce::String();

    // Um EQ na trilha do Kick (+6 dB em 100 Hz): o congelado tem de incluir os efeitos antes do fader.
    const auto eq = app.run ("mixer.addInsert", args ({ { "track", mxKick }, { "type", "eq" } })).value["insert"].toString();
    auto eqParams = new juce::DynamicObject();
    eqParams->setProperty ("b1Freq", 100);
    eqParams->setProperty ("b1Gain", 6);
    app.run ("mixer.setInsertParam", args ({ { "track", mxKick }, { "insert", eq }, { "params", juce::var (eqParams) } }));

    auto render = [&] (const juce::String& name)
    {
        const auto file = work.getChildFile (name);
        app.run ("project.exportWav", args ({ { "path", file.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        return readWav (file);
    };

    const auto before = render ("antes.wav");

    rep.check (! app.run ("mixer.setFrozen", args ({ { "track", masterTrackId } })).ok, "o master não congela"_t);
    const auto frozen = app.run ("channel.setFrozen", args ({ { "channel", kick }, { "on", true } }));
    const auto* mx = model.get().findMixer (mxKick);
    const auto* frozenChannels = frozen.value["channels"].getArray();
    rep.check (frozen.ok && mx != nullptr && mx->frozen && frozen.value["track"].toString() == mxKick
                   && frozenChannels != nullptr && frozenChannels->size() == 1 && (*frozenChannels)[0].toString() == kick,
               "channel.setFrozen congela a trilha do mixer do Kick e diz quais canais congelaram (só o Kick)"_t);

    auto& projector = app.getProjector();
    rep.check (projector.isFrozen (mxKick),
               "o motor renderizou a trilha (Track::setFrozen)"_t
                   + (projector.freezeProblems()[mxKick].isNotEmpty() ? ": " + projector.freezeProblems()[mxKick] : juce::String()));
    rep.check (samplerEnabled (app, kick) == 0 && samplerEnabled (app, hat) == 1,
               "congelado, o sampler do Kick fica desligado; o do Hat continua ligado"_t);

    // A prova: o WAV congelado contra o render sem congelar, na mesma taxa. O congelado é antes do
    // fader, então a referência sai com a trilha em 0 dB, sem pan, só com os canais que entram nela.
    const auto freezeFile = projector.freezeFileFor (mxKick);
    const auto frozenAudio = readWithEngine (app, freezeFile);
    Project reference = model.get();

    for (auto& c : reference.channels)
        if (const auto out = reference.resolveOutput (c.output); out != mxKick && ! reference.reaches (out, mxKick, false))
            c.mute = true;

    for (auto& m : reference.mixer)
    {
        m.frozen = false;

        if (m.id == mxKick || m.isMaster())
        {
            m.volumeDb = 0.0f;
            m.pan = 0.0f;
            m.sends.clear();
        }
    }

    const auto referenceFile = work.getChildFile ("referencia-sem-congelar.wav");
    const auto rendered = renderToWav (app.getEngine(), app.getSampleCache(), reference, model.projectFolder(), false, 1, referenceFile,
                                       &app.getPluginCatalog(), frozenAudio.valid ? frozenAudio.sampleRate : 44100.0, 32);
    const auto referenceAudio = readWav (referenceFile);
    const int compared = juce::jmin (frozenAudio.buffer.getNumSamples(), referenceAudio.buffer.getNumSamples());
    float freezeDiff = frozenAudio.valid && referenceAudio.valid && compared > 0 ? 0.0f : 1.0e9f;

    for (int i = 0; i < compared && freezeDiff < 1.0e9f; ++i)
        freezeDiff = juce::jmax (freezeDiff, std::abs (frozenAudio.buffer.getSample (0, i) - referenceAudio.buffer.getSample (0, i)));

    const double patternSeconds = ticksToSeconds (16 * ticksPerStep, 140.0);
    rep.check (rendered.wasOk() && freezeDiff < 1.0e-4f && compared >= (int) (patternSeconds * referenceAudio.sampleRate) - 2
                   && peakDb (frozenAudio) > -30.0f,
               "o som congelado bate com o render sem congelar: diferença máxima "_t + juce::String (freezeDiff) + " em "
                   + juce::String (compared) + " amostras, pico " + juce::String (peakDb (frozenAudio), 1) + " dBFS (o EQ entra no congelado)");

    // guarda uma cópia do primeiro congelado: o arquivo do motor é o mesmo, e o conteúdo tem de mudar
    const auto firstFreeze = work.getChildFile ("congelado-1.freeze");
    freezeFile.copyFileTo (firstFreeze);
    setGrid (app, kick, "x.......x.......");
    const auto refrozen = projector.freezeFileFor (mxKick);
    rep.check (projector.isFrozen (mxKick) && refrozen.existsAsFile() && ! refrozen.hasIdenticalContentTo (firstFreeze)
                   && samplerEnabled (app, kick) == 0,
               "mudou o padrão do Kick: a trilha congela de novo, com o som novo, e o sampler volta a desligar"_t);

    setGrid (app, kick, "x...x...x...x...");
    const float exportDiff = maxDiff (before, render ("congelado.wav"));
    rep.check (exportDiff == 0.0f, "o export sai igual com a trilha congelada (diferença máxima "_t + juce::String (exportDiff) + ")");

    const auto info = app.run ("audio.performance").value["frozen"];
    rep.check (info.size() == 1 && info[0]["track"].toString() == mxKick && (bool) info[0]["frozen"],
               "audio.performance lista a trilha congelada"_t);

    // salvar e reabrir
    const auto projectFile = work.getChildFile ("projeto").getChildFile ("Congelado.wnl");
    app.run ("project.save", args ({ { "path", projectFile.getFullPathName() } }));
    rep.check (projectFile.loadFileAsString().contains ("\"frozen\": true"), "o .wnl guarda a trilha congelada (campo opcional)");
    app.run ("project.new");
    app.run ("project.open", args ({ { "path", projectFile.getFullPathName() } }));
    rep.check (model.get().findMixer (mxKick) != nullptr && model.get().findMixer (mxKick)->frozen && projector.isFrozen (mxKick),
               "reabre congelada (o motor renderiza de novo ao abrir)"_t);

    // descongelar e desfazer
    rep.check (app.run ("channel.setFrozen", args ({ { "channel", kick }, { "on", false } })).ok && ! projector.isFrozen (mxKick)
                   && samplerEnabled (app, kick) == 1,
               "descongela e o sampler do Kick volta a ligar"_t);
    rep.check (app.run ("edit.undo").ok && projector.isFrozen (mxKick), "desfazer congela de novo"_t);

    // vários canais na mesma trilha congelam juntos, e a resposta diz quais
    app.run ("channel.setOutput", args ({ { "channel", hat }, { "track", mxKick } }));
    const auto both = app.run ("channel.setFrozen", args ({ { "channel", hat }, { "on", true } }));
    const auto* bothChannels = both.value["channels"].getArray();
    rep.check (both.ok && bothChannels != nullptr && bothChannels->size() == 2 && both.value["message"].toString().isNotEmpty(),
               "o Hat na trilha do Kick: congelar um congela os dois, e a resposta lista os dois ("_t + both.value["message"].toString() + ")");

    // canal que sai direto no master não tem trilha para congelar
    app.run ("channel.setOutput", args ({ { "channel", hat }, { "track", masterTrackId } }));
    rep.check (! app.run ("channel.setFrozen", args ({ { "channel", hat } })).ok, "canal direto no master não congela (explica o porquê)"_t);
    app.run ("channel.setFrozen", args ({ { "channel", kick }, { "on", false } }));
}

} // namespace whynot::selftest
