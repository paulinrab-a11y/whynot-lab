// Autoteste da gravação (entrega 3.5), sem microfone e sem placa: a entrada são blocos de um WAV
// conhecido empurrados no gravador, como a placa faria. Mede no WAV gravado e no WAV exportado:
// o take igual à entrada, a compensação de latência, o punch, o take repetido, o take MIDI,
// o desfazer e o salvar/reabrir.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"
#include "../../record/RecordEngine.h"

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.getParentDirectory().createDirectory();
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

    /** Empurra o WAV na entrada do gravador em blocos de `block` amostras, como a placa faria. */
    void feed (RecordEngine& rec, const Audio& in, int block)
    {
        const int total = in.buffer.getNumSamples();

        for (int pos = 0, n = 0; pos < total; pos += block, ++n)
        {
            const float* ptrs[2] { in.buffer.getReadPointer (0, pos),
                                   in.buffer.getNumChannels() > 1 ? in.buffer.getReadPointer (1, pos) : nullptr };
            rec.input().pushInput (ptrs, in.buffer.getNumChannels(), juce::jmin (block, total - pos), pos);

            if (n % 64 == 63)
                juce::Thread::sleep (1);   // o tempo real daria esse fôlego ao escritor
        }
    }

    /** Índice da amostra de maior módulo (canal 0) entre from e to. */
    int peakIndex (const Audio& a, int from, int to)
    {
        int best = -1;
        float value = 0.0f;
        const float* x = a.buffer.getReadPointer (0);

        for (int i = juce::jmax (0, from); i < juce::jmin (a.buffer.getNumSamples(), to); ++i)
        {
            if (std::abs (x[i]) > value)
            {
                value = std::abs (x[i]);
                best = i;
            }
        }

        return best;
    }

    /** Primeira (ou última) amostra com som entre from e to. */
    int firstAudible (const Audio& a, int from, int to, bool last = false)
    {
        const float* x = a.buffer.getReadPointer (0);
        from = juce::jmax (0, from);
        to = juce::jmin (a.buffer.getNumSamples(), to);

        for (int k = 0; k < to - from; ++k)
        {
            const int i = last ? to - 1 - k : from + k;

            if (std::abs (x[i]) > 1.0e-4f)
                return i;
        }

        return -1;
    }

    const PlaylistItem* itemFor (const Project& p, const juce::String& channel)
    {
        for (auto& i : p.playlistItems)
            if (i.kind == "audio" && i.ref == channel)
                return &i;

        return nullptr;
    }

    int notesWithKey (const std::vector<Note>* notes, int key, int start, int length, int velocity)
    {
        int count = 0;

        if (notes != nullptr)
            for (auto& n : *notes)
                count += (n.key == key && n.start == start && n.length == length && n.velocity == velocity) ? 1 : 0;

        return count;
    }
}

void runRecordingSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("gravacao");
    work.createDirectory();

    rep.section ("Gravação: armar e o estado (3.5)"_t);
    auto* rec = app.recorder();
    rep.check (rec != nullptr, "o app tem gravador");

    if (rec == nullptr)
        return;

    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));
    app.run ("record.setCountIn", args ({ { "bars", 0 } }));   // a contagem (gravar voz) fica no gravacao.json
    const double tick = ticksToSeconds (1, 140.0);
    const auto voz = app.run ("mixer.addTrack", args ({ { "name", "Voz" } })).value["track"].toString();

    rep.check (! app.run ("record.arm", args ({ { "track", masterTrackId } })).ok, "o master não arma"_t);
    rep.check (app.run ("record.arm", args ({ { "track", voz } })).ok && model.get().findMixer (voz) != nullptr && model.get().findMixer (voz)->armed, "arma a trilha Voz do mixer");

    const auto state = app.run ("record.state").value;
    rep.check (state["armedTracks"].size() == 1 && state["armedTracks"][0].toString() == voz && ! (bool) state["recording"],
               "record.state mostra a trilha armada, sem gravar");
    rep.check (app.getKeyMap().actionFor (juce::KeyPress ('r', juce::ModifierKeys(), 0)) == "transport.record", "R liga o REC (como no FL)");

    //==========================================================================
    rep.section ("Gravação: take de áudio a partir de um WAV conhecido"_t);

    // A entrada: 3 s de ruído baixo (determinístico) e um clique em 0,5 s. Placa simulada com 128 + 352
    // amostras de latência (10 ms de ida e volta a 48 kHz).
    constexpr int clickAt = 24000;
    const auto inputFile = work.getChildFile ("entrada-clique.wav");
    {
        juce::AudioBuffer<float> b (1, 3 * (int) rate);
        juce::Random random (35);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (random.nextFloat() - 0.5f) * 0.02f);

        b.setSample (0, clickAt, 0.9f);
        writeWav (inputFile, b);
    }

    const auto input = readWav (inputFile);
    rec->simulateDevice (rate, 128, 352);
    app.run ("record.setLatency", args ({ { "adjustMs", 0.0 } }));
    const auto mixerBefore = model.get().mixer.size();

    rep.check (app.run ("transport.record", args ({ { "on", true } })).ok && rec->isRecordMode() && ! rec->isRecording(),
               "REC ligado com o transporte parado: espera o play"_t);
    app.run ("transport.play");
    rep.check (rec->isRecording(), "REC + play começa a gravar"_t);
    feed (*rec, input, 300);   // blocos de 300: não batem com os pedaços de 256 da fila
    app.run ("transport.stop");
    rep.check (! rec->isRecording(), "stop termina a gravação"_t);

    const auto take1 = channelId (model.get(), "Voz take 1");
    const auto* ch1 = model.get().findChannel (take1);
    const auto* item1 = itemFor (model.get(), take1);
    rep.check (ch1 != nullptr && ch1->isAudio() && ch1->output == voz && model.get().mixer.size() == mixerBefore,
               "o take vira o canal de áudio \"Voz take 1\", que sai pela trilha Voz (sem trilha nova no mixer)"_t);

    const juce::File take1File (ch1 != nullptr ? ch1->samplePath : juce::String());
    const auto recorded = readWav (take1File);
    rep.check (recorded.valid && recorded.sampleRate == rate && recorded.buffer.getNumChannels() == 1
                   && recorded.buffer.getNumSamples() == input.buffer.getNumSamples(),
               "o WAV do take: 48 kHz, mono, "_t + juce::String (recorded.buffer.getNumSamples()) + " amostras (a entrada tem "
                   + juce::String (input.buffer.getNumSamples()) + ")");
    const float diff1 = maxDiff (recorded, input);
    rep.check (diff1 == 0.0f, "o take é igual à entrada, amostra por amostra (diferença máxima "_t + juce::String (diff1) + ")");

    const int expectLength = (int) std::floor ((3.0 - 0.010) / tick + 1.0e-6);
    rep.check (item1 != nullptr && item1->start == 0 && std::abs (item1->offsetSeconds - 0.010) < 1.0e-6 && item1->length == expectLength,
               "o clipe começa no tick 0 e corta os 10 ms da latência (offset "_t
                   + juce::String (item1 != nullptr ? item1->offsetSeconds * 1000.0 : -1.0, 3) + " ms, " + juce::String (item1 != nullptr ? item1->length : -1) + " ticks)");

    const auto render1File = work.getChildFile ("take1.wav");
    app.run ("project.exportWav", args ({ { "path", render1File.getFullPathName() }, { "what", "song" } }));
    const auto render1 = readWav (render1File);
    const int click1 = peakIndex (render1, 0, render1.buffer.getNumSamples());
    rep.check (click1 == clickAt - 480,
               "compensação: o clique entrou na amostra "_t + juce::String (clickAt) + " e toca na " + juce::String (click1)
                   + " (esperado " + juce::String (clickAt - 480) + ", 480 amostras antes)");

    //--------------------------------------------------------------------------
    rep.section ("Gravação: desfazer e refazer o take"_t);
    const auto channelsWithTake = model.get().channels.size();
    rep.check (app.run ("edit.undo").ok && model.get().findChannel (take1) == nullptr && itemFor (model.get(), take1) == nullptr
                   && model.get().channels.size() == channelsWithTake - 1,
               "desfazer tira o canal e o clipe do take"_t);
    rep.check (take1File.existsAsFile(), "o WAV do take continua no disco (refazer traz de volta)");
    rep.check (app.run ("edit.redo").ok && itemFor (model.get(), take1) != nullptr, "refazer devolve o take");

    //--------------------------------------------------------------------------
    rep.section ("Gravação: take novo no mesmo lugar, com ajuste manual"_t);
    const auto* item1Now = itemFor (model.get(), take1);
    const auto item1Before = item1Now != nullptr ? *item1Now : PlaylistItem();
    app.run ("record.setLatency", args ({ { "adjustMs", 2.5 } }));   // + 120 amostras
    app.run ("transport.play");
    feed (*rec, input, 512);
    app.run ("transport.stop");

    const auto take2 = channelId (model.get(), "Voz take 2");
    const auto* ch2 = model.get().findChannel (take2);
    const auto* item2 = itemFor (model.get(), take2);
    const auto* item1After = itemFor (model.get(), take1);
    rep.check (ch2 != nullptr && ch2->samplePath != take1File.getFullPathName().replaceCharacter ('\\', '/'),
               "o segundo take vira outro canal (\"Voz take 2\"), com outro arquivo"_t);
    // gravar voz (21/10, takes e comping): no mesmo lugar e pela mesma trilha, os dois viram takes de um grupo;
    // o novo toca e o antigo fica mudo, no mesmo lugar e com o mesmo áudio
    rep.check (item1After != nullptr && item2 != nullptr && item1After->start == item1Before.start && item1After->length == item1Before.length
                   && item1After->offsetSeconds == item1Before.offsetSeconds && maxDiff (readWav (take1File), input) == 0.0f
                   && item1After->takeGroup.isNotEmpty() && item1After->takeGroup == item2->takeGroup
                   && item1After->take == 1 && item2->take == 2 && item1After->muted && ! item2->muted,
               "o take 1 continua no lugar e com o mesmo áudio, agora como take 1 do grupo (mudo); o take 2 toca"_t);
    rep.check (item2 != nullptr && item2->start == 0 && std::abs (item2->offsetSeconds - 0.0125) < 1.0e-6,
               "o ajuste de 2,5 ms leva o take 2 mais para trás (offset "_t
                   + juce::String (item2 != nullptr ? item2->offsetSeconds * 1000.0 : -1.0, 3) + " ms)");

    const auto render2File = work.getChildFile ("take1e2.wav");
    app.run ("project.exportWav", args ({ { "path", render2File.getFullPathName() }, { "what", "song" } }));
    const auto render2 = readWav (render2File);
    const int clickA = peakIndex (render2, clickAt - 700, clickAt - 540);
    const float clickB = render2.valid ? std::abs (render2.buffer.getSample (0, clickAt - 480)) : 1.0f;
    rep.check (clickA == clickAt - 600 && clickB < 0.1f,
               "só o take novo toca: clique na amostra "_t + juce::String (clickA) + " (esperado " + juce::String (clickAt - 600)
                   + "), e nada na " + juce::String (clickAt - 480) + ", a do take 1 (" + juce::String (clickB, 3) + ")");

    //--------------------------------------------------------------------------
    rep.section ("Gravação: punch in/out"_t);
    app.run ("channel.setMute", args ({ { "channel", take1 }, { "on", true } }));
    app.run ("channel.setMute", args ({ { "channel", take2 }, { "on", true } }));
    app.run ("record.setLatency", args ({ { "adjustMs", 0.0 } }));
    rep.check (app.run ("record.setPunch", args ({ { "start", 1 }, { "end", 1.5 }, { "unit", "bars" } })).ok
                   && model.get().punchOn && model.get().punchIn == 3840 && model.get().punchOut == 5760,
               "punch do compasso 2 até a metade dele (ticks 3840 a 5760)"_t);

    const auto sineFile = work.getChildFile ("entrada-seno.wav");
    {
        juce::AudioBuffer<float> b (1, 4 * (int) rate);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / rate));

        writeWav (sineFile, b);
    }

    app.run ("transport.play");
    feed (*rec, readWav (sineFile), 256);
    app.run ("transport.stop");

    const auto take3 = channelId (model.get(), "Voz take 3");
    const auto* item3 = itemFor (model.get(), take3);
    const double punchInSeconds = ticksToSeconds (3840, 140.0), punchOutSeconds = ticksToSeconds (5760, 140.0);
    rep.check (item3 != nullptr && item3->start == 3840 && item3->length == 1920 && std::abs (item3->offsetSeconds - (punchInSeconds + 0.010)) < 1.0e-6,
               "o clipe do punch começa no tick 3840 e acaba no 5760 (offset "_t
                   + juce::String (item3 != nullptr ? item3->offsetSeconds : -1.0, 6) + " s)");

    const auto render3File = work.getChildFile ("punch.wav");
    app.run ("project.exportWav", args ({ { "path", render3File.getFullPathName() }, { "what", "song" } }));
    const auto render3 = readWav (render3File);
    const int firstSound = firstAudible (render3, 0, render3.buffer.getNumSamples());
    const int lastSound = firstAudible (render3, 0, render3.buffer.getNumSamples(), true);
    const int expectFirst = (int) std::ceil (punchInSeconds * rate), expectLast = (int) std::floor (punchOutSeconds * rate);
    rep.check (std::abs (firstSound - expectFirst) <= 1 && std::abs (lastSound - expectLast) <= 1,
               "som só dentro do punch: da amostra "_t + juce::String (firstSound) + " à "_t + juce::String (lastSound)
                   + " (esperado " + juce::String (expectFirst) + " a " + juce::String (expectLast) + ")");
    rep.check (peakIn (render3, punchInSeconds + 0.05, punchOutSeconds - 0.05) > 0.1f && render3.buffer.getNumSamples() > expectLast + 4000,
               "dentro do punch o seno toca; depois do punch out a música continua em silêncio"_t);
    app.run ("record.setPunch", args ({ { "on", false } }));

    //--------------------------------------------------------------------------
    rep.section ("Gravação: take MIDI no padrão atual"_t);
    app.run ("transport.setSongMode", args ({ { "on", false } }));
    app.run ("record.arm", args ({ { "track", voz }, { "on", false } }));
    const auto bass = channelId (model.get(), "808");
    app.run ("pianoroll.clear", args ({ { "channel", bass } }));
    app.run ("pianoroll.addNotes", args ({ { "channel", bass }, { "notes", juce::Array<juce::var> { args ({ { "t", 2880 }, { "len", 240 }, { "key", 30 }, { "vel", 70 } }) } } }));
    rep.check (app.run ("record.arm", args ({ { "channel", bass } })).ok && model.get().findChannel (bass) != nullptr && model.get().findChannel (bass)->armed, "arma o canal 808 (MIDI)");
    rep.check (! app.run ("record.arm", args ({ { "channel", take1 } })).ok, "canal de áudio não arma para notas"_t);

    // Saída de 10 ms: cada nota chega 10 ms depois do tick que a pessoa ouviu tocar.
    rec->simulateDevice (rate, 128, 480);
    app.run ("transport.play");
    auto note = [&] (int key, bool on, int atTick, int vel = 100)
    {
        return app.run ("record.note", args ({ { "channel", bass }, { "key", key }, { "on", on }, { "velocity", vel },
                                               { "at", ticksToSeconds (atTick, 140.0) + 0.010 } })).ok;
    };
    bool sent = note (36, true, 0, 110) && note (36, false, 480);
    sent = note (38, true, 960, 90) && note (38, false, 1200) && sent;
    sent = note (41, true, 3840 + 1920) && note (41, false, 3840 + 2400) && sent;   // segunda volta do loop do padrão
    rep.check (sent, "as notas ao vivo entram no take (record.note com o tempo de cada uma)");
    app.run ("transport.stop");

    const auto* bassNotes = model.get().currentPatternPtr()->notesFor (bass);
    rep.check (notesWithKey (bassNotes, 36, 0, 480, 110) == 1 && notesWithKey (bassNotes, 38, 960, 240, 90) == 1,
               "notas nos ticks certos: Dó no 0 (480 ticks), Ré no 960 (240 ticks), com a força tocada"_t);
    rep.check (notesWithKey (bassNotes, 41, 1920, 480, 100) == 1, "a nota da segunda volta do loop cai no tick 1920 do padrão"_t);
    rep.check (notesWithKey (bassNotes, 30, 2880, 240, 70) == 1 && bassNotes != nullptr && bassNotes->size() == 4,
               "a nota que já estava no padrão continua (gravação soma, não apaga)"_t);

    rep.check (app.run ("edit.undo").ok && model.get().currentPatternPtr()->notesFor (bass) != nullptr
                   && model.get().currentPatternPtr()->notesFor (bass)->size() == 1,
               "desfazer tira as notas gravadas e deixa a que já estava"_t);
    app.run ("edit.redo");

    //--------------------------------------------------------------------------
    rep.section ("Gravação: salvar e reabrir"_t);
    app.run ("transport.record", args ({ { "on", false } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));
    app.run ("record.setPunch", args ({ { "start", 3840 }, { "end", 7680 }, { "on", false } }));
    const auto beforeSave = work.getChildFile ("antes-de-salvar.wav");
    app.run ("project.exportWav", args ({ { "path", beforeSave.getFullPathName() }, { "what", "song" } }));
    const auto saved = model.get();

    const auto projectFile = work.getChildFile ("projeto").getChildFile ("Gravação.wnl"_t);
    rep.check (app.run ("project.save", args ({ { "path", projectFile.getFullPathName() } })).ok, "salva o projeto com os takes");
    const auto json = projectFile.loadFileAsString();
    rep.check (json.contains ("\"armed\": true") && json.contains ("\"punch\""), "o .wnl guarda o canal armado e o punch (campos opcionais)");

    app.run ("project.new");
    rep.check (app.run ("project.open", args ({ { "path", projectFile.getFullPathName() } })).ok, "reabre");

    const auto& reopened = model.get();
    bool takesKept = true;

    for (auto name : { "Voz take 1", "Voz take 2", "Voz take 3" })
    {
        const auto* before = itemFor (saved, channelId (saved, name));
        const auto* after = itemFor (reopened, channelId (reopened, name));
        const auto* ch = reopened.findChannel (channelId (reopened, name));
        takesKept = takesKept && before != nullptr && after != nullptr && after->start == before->start && after->length == before->length
                    && std::abs (after->offsetSeconds - before->offsetSeconds) < 1.0e-6
                    && ch != nullptr && ch->samplePath.startsWith ("samples/") && model.resolveSample (ch->samplePath).existsAsFile();
    }

    rep.check (takesKept, "os 3 takes voltam no mesmo lugar, com os WAVs copiados para samples/");
    rep.check (reopened.findChannel (bass) != nullptr && reopened.findChannel (bass)->armed && reopened.punchIn == 3840 && reopened.punchOut == 7680,
               "o arme do 808 e o punch voltam"_t);

    const auto afterOpen = work.getChildFile ("depois-de-reabrir.wav");
    app.run ("project.exportWav", args ({ { "path", afterOpen.getFullPathName() }, { "what", "song" } }));
    const float reopenDiff = maxDiff (readWav (beforeSave), readWav (afterOpen));
    rep.check (reopenDiff < 1.0e-6f, "o projeto reaberto soa igual (diferença máxima "_t + juce::String (reopenDiff) + ")");

    //--------------------------------------------------------------------------
    rep.section ("Gravação: falha de buffer no meio do take"_t);
    {
        record::InputRecorder gapRecorder;
        const auto gapFile = work.getChildFile ("buraco.wav");
        rep.check (gapRecorder.start (gapFile, rate, 0, 1).wasOk(), "abre um gravador avulso");

        juce::AudioBuffer<float> block (1, 256);

        for (int i = 0; i < 256; ++i)
            block.setSample (0, i, 0.5f);

        const float* ptr[1] { block.getReadPointer (0) };
        gapRecorder.pushInput (ptr, 1, 256, 1000);
        gapRecorder.pushInput (ptr, 1, 256, 1000 + 256 + 100);   // a placa pulou 100 amostras
        const auto take = gapRecorder.stop (false);
        const auto gapAudio = readWav (gapFile);

        rep.check (take.gaps == 1 && take.firstStreamSample == 1000 && take.numSamples == 612 && gapAudio.buffer.getNumSamples() == 612,
                   "o buraco vira 100 amostras de silêncio e o resto continua no tempo ("_t + juce::String (take.numSamples) + " amostras)");
        rep.check (gapAudio.valid && peakIn (gapAudio, 260.0 / rate, 350.0 / rate) == 0.0f && gapAudio.buffer.getSample (0, 356) == 0.5f,
                   "silêncio no buraco, o áudio de depois começa na amostra 356"_t);

        // A placa reiniciou no meio (o relógio pulou 10 s sem a fila ter perdido nada): o take para ali,
        // em vez de seguir colado e adiantado.
        record::InputRecorder restartRecorder;
        const auto restartFile = work.getChildFile ("placa-reiniciou.wav");
        restartRecorder.start (restartFile, rate, 0, 1);
        restartRecorder.pushInput (ptr, 1, 256, 1000);
        restartRecorder.pushInput (ptr, 1, 256, 1000 + 256 + 480000);
        restartRecorder.pushInput (ptr, 1, 256, 0);
        const auto cut = restartRecorder.stop (false);
        rep.check (cut.truncated && cut.numSamples == 256 && readWav (restartFile).buffer.getNumSamples() == 256,
                   "a placa reiniciou no meio: o take para no último bloco certo (256 amostras), sem adiantar o resto"_t);
    }

    app.run ("transport.record", args ({ { "on", false } }));
}

} // namespace whynot::selftest
