// Autoteste do BPM com o motor vivo (melhorias de 10/11). Dono: bpm.
// Mudar o BPM (ou o denominador do compasso) com o Edit montado não apaga nem recria os clipes MIDI: o Projector
// deixa cada um com o mesmo id e só escreve a posição nova, para a nota que cruza a agulha (o 808, um pad) não
// disparar de novo a cada passo do arraste.
// - no Edit do app: 140 -> 150, um passo pequeno (150 -> 150,01, que o Tracktion sozinho não reposiciona), o
//   desfazer e o compasso 6/8: os mesmos clipes MIDI, as mesmas notas nos mesmos tempos, cada clipe nos segundos certos;
// - o clipe de áudio continua indo para o lugar novo (ele é refeito, como antes);
// - num Edit de render: 140 -> 150,01 -> 150 e o WAV sai igual, amostra a amostra, ao de um projeto aberto já a 150.

#include "SelfTestKit.h"
#include "../../engine/Exporter.h"
#include "../../engine/Projector.h"

#include <map>

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;

    juce::File writeBpmTone (const juce::File& folder)
    {
        // 1 s de 220 Hz: o bloco de áudio da playlist
        juce::AudioBuffer<float> b (1, (int) rate);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / rate));

        const auto f = folder.getChildFile ("bpm-tom.wav");
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
        juce::WavAudioFormat wav;

        if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());

        return f;
    }

    struct NoteShot
    {
        int key = 0, velocity = 0;
        double start = 0.0, length = 0.0;   // tempos musicais dentro do clipe, como o Tracktion guarda
        bool operator== (const NoteShot&) const = default;
    };

    /** Um clipe MIDI do motor: o objeto, onde ele está e o que ele toca. */
    struct ClipShot
    {
        te::Clip* object = nullptr;
        double start = 0.0, end = 0.0, offset = 0.0;   // segundos
        std::vector<NoteShot> notes;
    };

    using Shots = std::map<juce::uint64, ClipShot>;   // pelo id do clipe no Edit

    /** Os clipes MIDI que o projetor pôs nas trilhas dos canais, e quantos clipes de áudio. */
    Shots midiClips (Projector& projector, const Project& p, int* audioClips = nullptr)
    {
        Shots shots;

        for (auto& c : p.channels)
            if (auto* track = projector.trackFor (c.id))
                for (auto* clip : track->getClips())
                {
                    auto* midi = dynamic_cast<te::MidiClip*> (clip);

                    if (midi == nullptr)
                    {
                        if (audioClips != nullptr)
                            ++*audioClips;

                        continue;
                    }

                    ClipShot shot;
                    shot.object = clip;
                    const auto position = clip->getPosition();
                    shot.start = position.getStart().inSeconds();
                    shot.end = position.getEnd().inSeconds();
                    shot.offset = position.getOffset().inSeconds();

                    for (auto* n : midi->getSequence().getNotes())
                        shot.notes.push_back ({ n->getNoteNumber(), n->getVelocity(), n->getStartBeat().inBeats(), n->getLengthBeats().inBeats() });

                    shots[clip->itemID.getRawID()] = std::move (shot);
                }

        return shots;
    }

    /** Os mesmos clipes (id e objeto) com as mesmas notas nos mesmos tempos musicais. */
    bool sameClips (const Shots& before, const Shots& after)
    {
        if (before.size() != after.size())
            return false;

        for (auto& [id, shot] : before)
        {
            const auto it = after.find (id);

            if (it == after.end() || it->second.object != shot.object || it->second.notes != shot.notes)
                return false;
        }

        return true;
    }

    /** Cada clipe MIDI está, em segundos, onde um bloco de padrão da playlist está em ticks (toSeconds faz a conta). */
    template <typename ToSeconds>
    bool clipsOnBlocks (const Shots& shots, const Project& p, ToSeconds&& toSeconds, double* worst = nullptr)
    {
        bool all = ! shots.empty();

        for (auto& [id, shot] : shots)
        {
            double best = 1.0e9;

            for (auto& item : p.playlistItems)
                if (item.kind == "pattern")
                    best = juce::jmin (best, std::abs (toSeconds (item.start) - shot.start) + std::abs (toSeconds (item.start + item.length) - shot.end));

            if (worst != nullptr)
                *worst = juce::jmax (*worst, best);

            all = all && best < 1.0e-9 && shot.offset == 0.0;
        }

        return all;
    }

    /** O render do Exportar (RenderJob::prepare), num Edit que já está montado. */
    bool renderEdit (te::Edit& edit, double seconds, const juce::File& out)
    {
        out.deleteFile();

        te::Renderer::Parameters params (edit);
        params.destFile = out;
        params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.bitDepth = 24;
        params.sampleRateForAudio = rate;
        params.blockSizeForAudio = 512;
        params.time = { te::TimePosition(), te::TimePosition::fromSeconds (seconds) };
        params.usePlugins = true;
        params.useMasterPlugins = true;
        params.canRenderInMono = false;
        params.checkNodesForAudio = false;
        params.ditheringEnabled = false;

        te::Edit::ScopedRenderStatus status (edit, true);
        std::atomic<float> progress { 0.0f };
        auto task = te::render_utils::createRenderTask (params, "Autoteste do BPM", &progress, nullptr);

        if (task == nullptr)
            return false;

        while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {}

        const bool ok = task->errorMessage.isEmpty();
        task = nullptr;   // o grafo sai e o arquivo fecha antes do estado de render
        return ok && out.existsAsFile();
    }
}

void runBpmLiveSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    rep.section ("BPM com o motor vivo: os clipes MIDI ficam (10/11)"_t);

    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("transport.setTimeSignature", args ({ { "numerator", 4 }, { "denominator", 4 } }));

    const auto kick = channelId (model.get(), "Kick"), hat = channelId (model.get(), "Hat"), bass = channelId (model.get(), "808");
    setGrid (app, kick, "x...x...x...x...");
    setGrid (app, hat, "x.x.x.x.x.x.3.x.", 90);
    // o 808 segura a nota por cima do tempo: é o caso que disparava de novo
    addNotes (app, bass, { noteVar (0.0, 1.5, 36, false), noteVar (2.0, 1.75, 39, false) });

    app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "unit", "bars" } }));
    app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 2 }, { "unit", "bars" } }));
    const auto audioItem = app.run ("playlist.addItem", args ({ { "track", 2 }, { "start", 1 }, { "unit", "bars" },
                                                               { "path", writeBpmTone (ctx.work).getFullPathName() } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));

    auto& projector = app.getProjector();
    auto& tempo = app.getEdit().tempoSequence;

    auto byFormula = [&] (int ticks)   { return ticks / (double) ticksPerBeat * 60.0 / model.get().bpm; };
    auto byEngine = [&] (int ticks)    { return tempo.toTime (te::BeatPosition::fromBeats (ticks / (double) ticksPerBeat)).inSeconds(); };

    /** O clipe de áudio da playlist começa nos segundos do bloco dele. */
    auto audioInPlace = [&]
    {
        const PlaylistItem* item = nullptr;

        for (auto& i : model.get().playlistItems)
            if (i.kind == "audio")
                item = &i;

        if (item == nullptr)
            return false;

        if (auto* track = projector.trackFor (item->ref))
            for (auto* clip : track->getClips())
                if (std::abs (clip->getPosition().getStart().inSeconds() - byEngine (item->start)) < 1.0e-9
                      && std::abs (clip->getPosition().getEnd().inSeconds() - byEngine (item->start + item->length)) < 1.0e-9)
                    return true;

        return false;
    };

    int audioClips = 0;
    const auto at140 = midiClips (projector, model.get(), &audioClips);
    rep.check (audioItem.ok && at140.size() >= 6 && audioClips == 1,
               "a 140 BPM, no SONG: "_t + juce::String ((int) at140.size()) + " clipes MIDI (3 canais em 2 blocos) e " + juce::String (audioClips)
                   + " de áudio"_t);
    rep.check (clipsOnBlocks (at140, model.get(), byFormula) && audioInPlace(), "cada clipe está nos segundos do bloco dele a 140 BPM"_t);

    //--------------------------------------------------------------------------
    // 140 -> 150: os mesmos clipes, as mesmas notas, os segundos novos
    app.run ("transport.setBpm", args ({ { "bpm", 150.0 } }));
    const auto at150 = midiClips (projector, model.get());
    rep.check (sameClips (at140, at150), "140 -> 150: os clipes MIDI são os mesmos objetos, com os mesmos ids e as mesmas notas nos mesmos tempos"_t);

    double worst = 0.0;
    bool moved = false;

    for (auto& [id, shot] : at150)
        if (const auto it = at140.find (id); it != at140.end() && it->second.start > 0.1)
            moved = moved || shot.start < it->second.start - 0.05;

    rep.check (clipsOnBlocks (at150, model.get(), byFormula, &worst) && clipsOnBlocks (at150, model.get(), byEngine) && moved,
               "a 150 BPM cada clipe MIDI foi para os segundos do bloco dele (maior erro: "_t + juce::String (worst * 1.0e9, 3) + " ns)");
    rep.check (audioInPlace(), "o clipe de áudio também foi para o lugar novo"_t);

    //--------------------------------------------------------------------------
    // um passo pequeno do arraste com Shift (0,01 BPM): menos de 1 ms por clipe, o Tracktion sozinho deixa onde está
    app.run ("transport.setBpm", args ({ { "bpm", 150.01 } }));
    const auto atFine = midiClips (projector, model.get());
    worst = 0.0;
    rep.check (sameClips (at140, atFine) && clipsOnBlocks (atFine, model.get(), byFormula, &worst),
               "150 -> 150,01: os mesmos clipes, nos segundos exatos (maior erro: "_t + juce::String (worst * 1.0e9, 3) + " ns)");

    // o desfazer volta por onde veio
    for (int i = 0; i < 3 && model.get().bpm != 140.0; ++i)
        app.run ("edit.undo");

    const auto undone = midiClips (projector, model.get());
    rep.check (model.get().bpm == 140.0 && sameClips (at140, undone) && clipsOnBlocks (undone, model.get(), byFormula) && audioInPlace(),
               "desfazer até os 140 BPM, os mesmos clipes, de volta aos segundos de antes"_t);

    //--------------------------------------------------------------------------
    // o compasso: o denominador muda os segundos do tempo no motor, e os clipes acompanham sem serem refeitos
    app.run ("transport.setTimeSignature", args ({ { "numerator", 6 }, { "denominator", 8 } }));
    const auto inSixEight = midiClips (projector, model.get());
    rep.check (sameClips (at140, inSixEight) && clipsOnBlocks (inSixEight, model.get(), byEngine) && audioInPlace(),
               "4/4 -> 6/8: os mesmos clipes MIDI, cada um onde o motor põe o bloco dele; o de áudio também"_t);
    app.run ("transport.setTimeSignature", args ({ { "numerator", 3 }, { "denominator", 8 } }));
    rep.check (sameClips (at140, midiClips (projector, model.get())) && audioInPlace(), "6/8 -> 3/8 (só o numerador): nada muda de lugar"_t);
    app.run ("transport.setTimeSignature", args ({ { "numerator", 4 }, { "denominator", 4 } }));
    rep.check (sameClips (at140, midiClips (projector, model.get())) && clipsOnBlocks (midiClips (projector, model.get()), model.get(), byFormula),
               "de volta a 4/4: os mesmos clipes, nos segundos de antes"_t);

    //--------------------------------------------------------------------------
    // o render: trocar o BPM com o Edit montado dá o mesmo WAV de um projeto aberto já no BPM novo
    Project p140 = model.get(), p150 = model.get(), pFine = model.get();
    p140.bpm = 140.0;
    p150.bpm = 150.0;
    pFine.bpm = 150.01;

    const auto reference = ctx.work.getChildFile ("bpm-150-aberto.wav");
    const auto exported = renderToWav (app.getEngine(), app.getSampleCache(), p150, model.projectFolder(), true, 1, reference,
                                       &app.getPluginCatalog(), rate, 24);
    const auto ref = readWav (reference);
    const float refPeak = ref.valid ? peakDb (ref) : -200.0f;
    rep.check (exported.wasOk() && ref.valid && refPeak > -40.0f,
               "a referência: o projeto a 150 BPM exportado (pico "_t + juce::String (refPeak, 1) + " dB)");

    auto renderWith = [&] (std::initializer_list<const Project*> steps, const juce::File& out, bool& keptClips)
    {
        auto edit = te::Edit::createSingleTrackEdit (app.getEngine(), te::Edit::forRendering);

        if (edit == nullptr)
            return Audio();

        bool ok = false;
        {
            Projector local (*edit, app.getSampleCache(), Projector::Mode::song, 1, &app.getPluginCatalog());
            Shots first;
            keptClips = true;

            for (auto* step : steps)
            {
                local.sync (*step, model.projectFolder());
                const auto now = midiClips (local, *step);

                if (first.empty())
                    first = now;

                keptClips = keptClips && ! now.empty() && sameClips (first, now);
            }

            Projector::readCompressedClipsDirectly (*edit);
            ok = renderEdit (*edit, local.lengthSeconds(), out);
        }   // o projetor sai antes do Edit

        return ok ? readWav (out) : Audio();
    };

    bool kept = false;
    const auto fresh = renderWith ({ &p150 }, ctx.work.getChildFile ("bpm-150-direto.wav"), kept);
    const float freshDiff = fresh.valid && ref.valid ? maxDiff (fresh, ref) : 1.0f;
    rep.check (freshDiff == 0.0f, "o render do autoteste a 150 BPM é igual ao do Exportar (diferença "_t + juce::String (freshDiff, 9) + ")");

    const auto changed = renderWith ({ &p140, &pFine, &p150 }, ctx.work.getChildFile ("bpm-140-para-150.wav"), kept);
    const float changedDiff = changed.valid && ref.valid ? maxDiff (changed, ref) : 1.0f;
    rep.check (kept, "no Edit do render, 140 -> 150,01 -> 150: os clipes MIDI são os mesmos do começo ao fim"_t);
    rep.check (changedDiff == 0.0f && changed.valid && ref.valid && changed.buffer.getNumSamples() == ref.buffer.getNumSamples(),
               "o render depois da troca é igual, amostra a amostra, ao do projeto aberto já a 150 BPM ("_t
                   + juce::String (changed.valid ? changed.buffer.getNumSamples() : 0) + " amostras, diferença "_t + juce::String (changedDiff, 9) + ")");

    app.run ("transport.setSongMode", args ({ { "on", false } }));
}

} // namespace whynot::selftest
