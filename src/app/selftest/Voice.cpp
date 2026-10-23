// Autoteste da gravação de voz para MCs (pedidos de 21/10 à tarde, frente A), sem microfone e sem placa:
// a entrada são blocos de um WAV conhecido empurrados no gravador, como a placa faria (igual ao
// Recording.cpp). Fatia 1: o take sempre se ouve (PAT passa para SONG, ou o padrão vai junto), a
// gravação começa na agulha ou na seleção de tempo, e a contagem de 1 ou 2 compassos (Ctrl+P). Fatia 2:
// a monitoração pelo app (Desligada, Quando armada, Sempre), a baixa latência e o take seco. Fatias 3 a 7: os
// takes e o comping, a edição do clipe, os arquivos e a entrada pela playlist, o beat em um gesto e o assistente.
// Dono: frente Gravação de voz.

#include "SelfTestKit.h"
#include "../../record/RecordEngine.h"
#include "../../record/RecordingGuide.h"
#include "../../ui/DeviceSettings.h"
#include "../../ui/MixerView.h"
#include "../../ui/Playlist.h"
#include "../../ui/Theme.h"
#include "../../ui/TransportBar.h"
#include <thread>

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;
    constexpr int latencySamples = 128 + 352;   // a placa simulada: 10 ms de ida e volta

    /** Se o trecho guardado não acabar em `seconds`, escreve a falha no relatório e encerra o processo, como o do
        selftest/Producer.cpp: uma exportação travada vira uma falha com nome, e não um autoteste parado para sempre
        (com a trava de build na mão). */
    class Watchdog
    {
    public:
        Watchdog (const Report& r, juce::String what, double seconds)
            : reportFile (r.file), name (std::move (what))
        {
            thread = std::thread ([this, seconds]
            {
                if (done.wait ((int) (seconds * 1000.0)))
                    return;

                reportFile.appendText ("  FALHOU: TRAVOU (mais de "_t + juce::String (seconds, 0) + " s): " + name + "\n");
                juce::Process::terminate();
            });
        }

        ~Watchdog()
        {
            done.signal();
            thread.join();
        }

    private:
        juce::File reportFile;
        juce::String name;
        juce::WaitableEvent done;
        std::thread thread;
    };

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

    /** Ruído baixo (determinístico) com um clique em clickSample (-1 = sem clique). */
    Audio voiceInput (const juce::File& f, double seconds, int clickSample)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        juce::Random random (21);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (random.nextFloat() - 0.5f) * 0.02f);

        if (juce::isPositiveAndBelow (clickSample, b.getNumSamples()))
            b.setSample (0, clickSample, 0.9f);

        writeWav (f, b);
        return readWav (f);
    }

    /** Empurra o WAV na entrada do gravador em blocos, como a placa faria. */
    void feed (RecordEngine& rec, const Audio& in, int block)
    {
        const int total = in.buffer.getNumSamples();

        for (int pos = 0, n = 0; pos < total; pos += block, ++n)
        {
            const float* ptrs[1] { in.buffer.getReadPointer (0, pos) };
            rec.input().pushInput (ptrs, 1, juce::jmin (block, total - pos), pos);

            if (n % 64 == 63)
                juce::Thread::sleep (1);   // o tempo real daria esse fôlego ao escritor
        }
    }

    int peakIndex (const Audio& a)
    {
        int best = -1;
        float value = 0.0f;

        for (int i = 0; i < a.buffer.getNumSamples(); ++i)
        {
            if (std::abs (a.buffer.getSample (0, i)) > value)
            {
                value = std::abs (a.buffer.getSample (0, i));
                best = i;
            }
        }

        return best;
    }

    const PlaylistItem* itemFor (const Project& p, const juce::String& ref)
    {
        for (auto& i : p.playlistItems)
            if (i.ref == ref)
                return &i;

        return nullptr;
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

    template <typename T>
    T* childOf (juce::Component& parent)
    {
        for (auto* c : parent.getChildren())
            if (auto* t = dynamic_cast<T*> (c))
                return t;

        return nullptr;
    }

    double transportSeconds (AppController& app)   { return app.getEdit().getTransport().getPosition().inSeconds(); }
}

void runVoiceSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("voz");
    work.createDirectory();

    auto* rec = app.recorder();

    // A escuta "Quando armada" depende da resposta do fone (fatia 8): o autoteste começa sem resposta e devolve a
    // de antes no fim (o gravacao.json é o da pasta de dados do autoteste)
    const int headphonesBefore = rec != nullptr ? rec->settings().headphones : 0;
    const int questionsBefore = rec != nullptr ? rec->headphoneQuestionSerial() : 0;
    app.run ("record.setHeadphones", args ({ { "answer", "" } }));

    rep.section ("Gravar voz: a contagem (Ctrl+P)"_t);
    rep.check (rec != nullptr, "o app tem gravador");

    if (rec == nullptr)
        return;

    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    const double tick = ticksToSeconds (1, 140.0);
    const double beat = 60.0 / 140.0;
    rec->simulateDevice (rate, 128, 352);
    app.run ("record.setLatency", args ({ { "adjustMs", 0.0 } }));

    rep.check (app.getKeyMap().actionFor (juce::KeyPress ('p', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0)) == "transport.countIn",
               "Ctrl+P liga e desliga a contagem (como no FL)");

    auto countIn = [&] (const juce::var& a) { return (int) app.run ("record.setCountIn", a).value["countIn"]; };
    const int c0 = countIn (args ({ { "bars", 0 } }));
    const int c1 = countIn ({}), c2 = countIn ({});
    const int c3 = countIn (args ({ { "bars", 2 } })), c4 = countIn ({}), c5 = countIn ({});
    rep.check (c0 == 0 && c1 == 1 && c2 == 0 && c3 == 2 && c4 == 0 && c5 == 2,
               "a contagem alterna entre desligada e o último tamanho (0, 1, 0, 2, 0, 2: "_t
                   + juce::String (c0) + ", " + juce::String (c1) + ", " + juce::String (c2) + ", " + juce::String (c3) + ", "
                   + juce::String (c4) + ", " + juce::String (c5) + ")");
    rep.check (! app.run ("record.setCountIn", args ({ { "bars", 3 } })).ok, "contagem de 3 compassos é recusada"_t);

    //==========================================================================
    rep.section ("Gravar voz: o REC no PAT passa para o SONG; o take começa na agulha, depois da contagem"_t);
    const auto voz = app.run ("mixer.addTrack", args ({ { "name", "Voz" } })).value["track"].toString();
    app.run ("record.arm", args ({ { "track", voz } }));
    app.run ("record.setCountIn", args ({ { "bars", 1 } }));

    // a playlist com um bloco mudo no lugar do beat (não soa: o clique do take fica sozinho no render)
    app.run ("playlist.addItem", args ({ { "kind", "pattern" }, { "track", 1 }, { "start", 0 }, { "length", 16 }, { "unit", "bars" }, { "muted", true } }));
    rep.check (! model.get().songMode && ! model.get().playlistItems.empty(), "projeto no PAT, com algo na playlist"_t);

    const auto moved = app.run ("transport.setPosition", args ({ { "at", 2 }, { "unit", "bars" } }));
    rep.check (moved.ok && app.playStart() == 2 * ticksPerBar4, "a agulha vai para o compasso 3 (tick "_t + juce::String (app.playStart()) + ")");

    // entrada de 6 s com um clique em 4 s: a contagem de 1 compasso mais meio tempo, a 140 BPM, é 1,93 s
    constexpr int clickAt = 4 * 48000;
    const auto input = voiceInput (work.getChildFile ("entrada-agulha.wav"), 6.0, clickAt);
    const int recordFrom = 2 * ticksPerBar4;
    const double transportStart = (recordFrom - ticksPerBar4 - ticksPerBeat / 2) * tick;   // 1,5 s

    app.run ("transport.record", args ({ { "on", true } }));
    app.run ("transport.play");
    rep.check (model.get().songMode && rec->lastMessage().contains ("SONG"),
               "o REC + play no PAT com a playlist cheia passa para o SONG, com aviso: \""_t + rec->lastMessage() + "\"");
    rep.check (rec->isRecording() && (int) app.run ("record.state").value["recordFrom"] == recordFrom,
               "gravando a partir da agulha (record.state: recordFrom "_t + app.run ("record.state").value["recordFrom"].toString() + ")");
    rep.check (std::abs (transportSeconds (app) - transportStart) < 1.0e-6,
               "o transporte começa 1 compasso e meio tempo antes da agulha ("_t + juce::String (transportSeconds (app), 4) + " s)");
    rep.check (app.countInBeatsLeft() == 4, "na contagem, faltam 4 tempos ("_t + juce::String (app.countInBeatsLeft()) + ")");

    {
        // a barra do transporte na contagem: "CONTAGEM" e o 4 no lugar da posição
        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1366, theme::metric::topBar);
        TransportBar bar (app);
        host.addAndMakeVisible (bar);
        bar.setBounds (host.getLocalBounds());
        bar.refresh (model.get());
        bar.tick();
        rep.check (savePng (bar, ctx.work.getChildFile ("voz-contagem.png")), "captura da barra do transporte na contagem"_t);
        host.removeChildComponent (&bar);
        host.setLookAndFeel (nullptr);
    }

    feed (*rec, input, 256);
    app.run ("transport.stop");
    rep.check (! rec->isRecording() && app.countInBeatsLeft() == 0, "stop termina a gravação"_t);

    const auto take1 = channelId (model.get(), "Voz take 1");
    const auto* item1 = itemFor (model.get(), take1);
    const double offset1 = (ticksPerBar4 + ticksPerBeat / 2) * tick + latencySamples / rate;
    rep.check (item1 != nullptr && item1->start == recordFrom && std::abs (item1->offsetSeconds - offset1) < 1.0e-6,
               "o clipe começa na agulha (tick "_t + juce::String (item1 != nullptr ? item1->start : -1) + "), e a contagem fica no corte do começo ("
                   + juce::String (item1 != nullptr ? item1->offsetSeconds : -1.0, 4) + " s, esperado " + juce::String (offset1, 4) + ")");
    rep.check (std::abs (transportSeconds (app) - recordFrom * tick) < 1.0e-6, "o stop volta para a agulha"_t);

    const auto render1 = work.getChildFile ("agulha.wav");
    app.run ("project.exportWav", args ({ { "path", render1.getFullPathName() }, { "what", "song" } }));
    const int expectClick1 = juce::roundToInt ((transportStart + clickAt / rate - latencySamples / rate) * rate);
    const int click1 = peakIndex (readWav (render1));
    rep.check (std::abs (click1 - expectClick1) <= 1,
               "o clique cantado em 4 s depois do play toca na amostra "_t + juce::String (click1) + " (esperado " + juce::String (expectClick1) + ")");

    //==========================================================================
    rep.section ("Gravar voz: a seleção de tempo"_t);
    app.run ("record.setCountIn", args ({ { "bars", 0 } }));
    app.setTimeSelection ({ 4 * ticksPerBar4, 6 * ticksPerBar4 });   // compassos 5 e 6 (Ctrl+arrastar na régua)
    const auto loopRange = app.getEdit().getTransport().getLoopRange();
    rep.check (std::abs (loopRange.getStart().inSeconds() - 4 * ticksPerBar4 * tick) < 1.0e-9
                   && std::abs (loopRange.getEnd().inSeconds() - 6 * ticksPerBar4 * tick) < 1.0e-9
                   && app.getEdit().getTransport().looping.get(),
               "no SONG, a seleção de tempo vira o loop do transporte"_t);

    app.run ("transport.play");
    rep.check (rec->isRecording() && (int) app.run ("record.state").value["recordFrom"] == 4 * ticksPerBar4,
               "com a seleção, a gravação começa nela, não na agulha"_t);
    feed (*rec, voiceInput (work.getChildFile ("entrada-selecao.wav"), 3.3, -1), 512);   // menos de uma volta (3,43 s)
    app.run ("transport.stop");

    const auto* item2 = itemFor (model.get(), channelId (model.get(), "Voz take 2"));
    rep.check (item2 != nullptr && item2->start == 4 * ticksPerBar4 && item2->end() <= 6 * ticksPerBar4 && item2->length > 7000,
               "o take começa na seleção e fica dentro dela ("_t + juce::String (item2 != nullptr ? item2->start : -1) + " a "
                   + juce::String (item2 != nullptr ? item2->end() : -1) + ")");
    rep.check (app.playStart() == recordFrom && std::abs (transportSeconds (app) - 4 * ticksPerBar4 * tick) < 1.0e-6,
               "a agulha não mudou, e o stop volta para o começo da seleção"_t);

    app.setTimeSelection ({});
    rep.check (std::abs (app.getEdit().getTransport().getLoopRange().getStart().inSeconds()) < 1.0e-9,
               "sem a seleção, o loop volta a ser a música inteira"_t);

    //==========================================================================
    rep.section ("Gravar voz: PAT com a playlist vazia e contagem desde o começo"_t);
    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    const auto voz2 = app.run ("mixer.addTrack", args ({ { "name", "Voz" } })).value["track"].toString();
    app.run ("record.arm", args ({ { "track", voz2 } }));
    app.run ("record.setCountIn", args ({ { "bars", 2 } }));
    const auto pattern = model.get().currentPattern;
    const bool loopWas = model.get().loop;

    constexpr int clickAt2 = 5 * 48000;
    app.run ("transport.record", args ({ { "on", true } }));
    app.run ("transport.play");
    const double preRoll = -8.5 * beat;   // 2 compassos e meio tempo antes do zero
    rep.check (! model.get().songMode && model.get().playlistItems.empty() && rec->isRecording(),
               "PAT com a playlist vazia: grava no PAT, sem passar para o SONG"_t);
    rep.check (std::abs (transportSeconds (app) - preRoll) < 1.0e-6,
               "a contagem de 2 compassos começa antes do zero ("_t + juce::String (transportSeconds (app), 4) + " s)");
    rep.check (loopWas && ! app.getEdit().getTransport().looping.get(),
               "na contagem antes do loop, o loop fica desligado (senão o Tracktion pularia para dentro dele)"_t);

    feed (*rec, voiceInput (work.getChildFile ("entrada-pat.wav"), 7.0, clickAt2), 300);
    app.run ("transport.stop");
    rep.check (app.getEdit().getTransport().looping.get() == model.get().loop, "o loop volta depois do stop"_t);

    const auto take = channelId (model.get(), "Voz take 1");
    const auto* takeItem = itemFor (model.get(), take);
    const auto* block = itemFor (model.get(), pattern);
    const int takeEnd = takeItem != nullptr ? takeItem->end() : 0;
    rep.check (model.get().songMode && takeItem != nullptr && takeItem->start == 0
                   && std::abs (takeItem->offsetSeconds - (-preRoll + latencySamples / rate)) < 1.0e-6,
               "o take vai para a playlist no tick 0, e o modo passa para o SONG"_t);
    rep.check (block != nullptr && block->kind == "pattern" && block->start == 0 && block->length == record::patternBlockTicks (takeEnd, 16 * ticksPerStep)
                   && block->length >= takeEnd && block->track != (takeItem != nullptr ? takeItem->track : juce::String()),
               "o padrão que tocava vai junto, do zero até cobrir o take em voltas inteiras ("_t
                   + juce::String (block != nullptr ? block->length : -1) + " ticks para um take até o "_t + juce::String (takeEnd) + ")");
    rep.check (rec->lastMessage().contains ("PAT"), "e avisa: \""_t + rec->lastMessage() + "\"");

    const auto render2 = work.getChildFile ("pat.wav");
    app.run ("project.exportWav", args ({ { "path", render2.getFullPathName() }, { "what", "song" } }));
    const auto audio2 = readWav (render2);
    const int expectClick2 = juce::roundToInt ((preRoll + clickAt2 / rate - latencySamples / rate) * rate);
    const int click2 = peakIndex (audio2);
    rep.check (std::abs (click2 - expectClick2) <= 1,
               "no SONG, o clique toca na amostra "_t + juce::String (click2) + " (esperado " + juce::String (expectClick2) + ")");

    rep.check (app.run ("edit.undo").ok && ! model.get().songMode && model.get().playlistItems.empty()
                   && model.get().findChannel (take) == nullptr,
               "Ctrl+Z tira o take e o padrão, e volta para o PAT"_t);
    app.run ("edit.redo");

    {
        // a playlist parada no SONG: a agulha em textDim na régua
        app.run ("transport.setPosition", args ({ { "at", 1 }, { "unit", "bars" } }));
        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1200, 300);
        Playlist list (app);
        host.addAndMakeVisible (list);
        list.setBounds (host.getLocalBounds());
        list.refresh (model.get());
        list.setNeedle (app.playStart());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);   // a forma de onda carrega em segundo plano
        rep.check (childOf<PlaylistGrid> (list) != nullptr && savePng (list, ctx.work.getChildFile ("voz-agulha.png")),
                   "captura da playlist com a agulha no compasso 2, o take e o padrão"_t);
        host.removeChildComponent (&list);
        host.setLookAndFeel (nullptr);
    }

    //==========================================================================
    rep.section ("Gravar voz: monitoração pelo app (fatia 2)"_t);
    app.run ("record.setCountIn", args ({ { "bars", 0 } }));
    auto monitored = [&] { return app.run ("record.state").value["monitoring"]; };
    auto* vozTrack = [&] { return model.get().findMixer (voz2); }();

    rep.check (vozTrack != nullptr && vozTrack->monitor.isEmpty() && vozTrack->armed && monitored().size() == 0
                   && rec->headphoneQuestionSerial() > questionsBefore,
               "sem a resposta do fone, a trilha armada ainda não escuta, e a tela pergunta \"Está de fone?\" (fatia 8)"_t);
    app.run ("record.setHeadphones", args ({ { "answer", "no" } }));
    rep.check (monitored().size() == 0 && rec->settings().headphones == 2 && rec->lastMessage().contains ("microfonia"),
               "\"Não, estou nas caixas\": a escuta fica desligada, e a barra explica (microfonia)"_t);
    app.run ("record.setHeadphones", args ({ { "answer", "yes" } }));
    rep.check (monitored().size() == 1 && monitored()[0].toString() == voz2,
               "\"Sim, estou de fone\": o padrão \"Quando armada\" vale, e a trilha armada escuta"_t);
    rec->simulateDeviceName ("Windows Audio", "Microfone (Realtek(R) Audio)");
    app.run ("record.setHeadphones", args ({ { "answer", "yes" } }));   // refaz a escuta com a placa nova
    rep.check (monitored().size() == 0 && rec->lastMessage().contains ("placa do computador"),
               "com a placa do computador, a escuta fica desligada mesmo de fone, e a barra explica"_t);
    rec->simulateDeviceName ({}, {});
    app.run ("record.setHeadphones", args ({ { "answer", "yes" } }));
    rep.check (app.run ("record.setMonitor", args ({ { "track", voz2 }, { "mode", "off" } })).ok && monitored().size() == 0,
               "\"Desligada\": armada, mas não monitora (só a monitoração direta da placa)"_t);
    app.run ("record.arm", args ({ { "track", voz2 }, { "on", false } }));
    rep.check (app.run ("record.setMonitor", args ({ { "track", voz2 }, { "mode", "on" } })).ok && monitored().size() == 1,
               "\"Sempre\": monitora mesmo desarmada"_t);
    rep.check (! app.run ("record.setMonitor", args ({ { "track", masterTrackId }, { "mode", "on" } })).ok
                   && ! app.run ("record.setMonitor", args ({ { "track", voz2 }, { "mode", "talvez" } })).ok,
               "o master e um modo desconhecido são recusados"_t);
    rep.check (app.run ("edit.undo").ok && model.get().findMixer (voz2)->monitor == "off" && app.run ("edit.redo").ok
                   && model.get().findMixer (voz2)->monitor == "on",
               "mudar a monitoração é um passo de desfazer"_t);

    const bool lowWas = rec->settings().lowLatency;
    app.run ("record.setLowLatency", args ({ { "on", ! lowWas } }));
    rep.check (rec->settings().lowLatency == ! lowWas && (bool) app.run ("record.state").value["lowLatency"] == ! lowWas,
               "a baixa latência ao gravar liga e desliga (fica no gravacao.json)"_t);
    app.run ("record.setLowLatency", args ({ { "on", lowWas } }));

    // o take é seco: um efeito na trilha que monitora não entra no arquivo
    app.run ("mixer.addInsert", args ({ { "track", voz2 }, { "type", "sat" } }));
    app.run ("record.arm", args ({ { "track", voz2 }, { "on", true } }));
    const auto dryInput = voiceInput (work.getChildFile ("entrada-seca.wav"), 2.0, 48000);
    app.run ("transport.record", args ({ { "on", true } }));
    app.run ("transport.play");
    feed (*rec, dryInput, 512);
    app.run ("transport.stop");
    const auto* dryChannel = model.get().findChannel (channelId (model.get(), "Voz take 2"));
    const float dryDiff = dryChannel != nullptr ? maxDiff (readWav (juce::File (dryChannel->samplePath)), dryInput) : 1.0f;
    rep.check (dryChannel != nullptr && dryDiff == 0.0f,
               "com a trilha monitorando e uma saturação nela, o take é igual à entrada (diferença máxima "_t + juce::String (dryDiff) + ")");

    {
        const auto savedFile = work.getChildFile ("monitor").getChildFile ("Monitor.wnl");
        rep.check (app.run ("project.save", args ({ { "path", savedFile.getFullPathName() } })).ok
                       && savedFile.loadFileAsString().contains ("\"monitor\": \"on\""),
                   "o .wnl guarda \"monitor\": \"on\" (campo opcional, o esquema não sobe)"_t);
        app.run ("project.new");
        rep.check (app.run ("project.open", args ({ { "path", savedFile.getFullPathName() } })).ok
                       && model.get().findMixer (voz2) != nullptr && model.get().findMixer (voz2)->monitor == "on",
                   "reabre com a monitoração \"Sempre\""_t);
    }

    {
        // o mixer: o fone na placa do nome quando a monitoração não é a padrão
        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1500, 520);
        MixerView mixer (app);
        host.addAndMakeVisible (mixer);
        mixer.setBounds (host.getLocalBounds());
        mixer.refresh (model.get());
        rep.check (savePng (mixer, ctx.work.getChildFile ("voz-mixer.png")), "captura do mixer com a Voz monitorando sempre"_t);
        host.removeChildComponent (&mixer);
        host.setLookAndFeel (nullptr);
    }

    //==========================================================================
    rep.section ("Gravar voz: takes em loop e comping (fatia 3)"_t);
    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("transport.setSongMode", args ({ { "on", true } }));
    app.run ("record.setCountIn", args ({ { "bars", 0 } }));
    rec->simulateDevice (rate, 128, 352);
    const auto vozT = app.run ("mixer.addTrack", args ({ { "name", "Voz" } })).value["track"].toString();
    app.run ("record.arm", args ({ { "track", vozT } }));

    // loop nos compassos 3 e 4; a entrada tem 3 voltas e meia, cada volta num tom (220, 330, 440 e 550 Hz)
    const int loopFrom = 2 * ticksPerBar4, loopTo = 4 * ticksPerBar4;
    const double loopSeconds = (loopTo - loopFrom) * tick, lat = latencySamples / rate;
    const double tones[] { 220.0, 330.0, 440.0, 550.0 };
    app.setTimeSelection ({ loopFrom, loopTo });

    Audio loopInput;
    {
        const int n = (int) ((3.5 * loopSeconds + lat) * rate);
        juce::AudioBuffer<float> b (1, n);

        for (int i = 0; i < n; ++i)
        {
            const double t = i / rate - lat;   // o tempo da música dentro das voltas (a latência põe o arquivo 10 ms antes)
            const int pass = juce::jlimit (0, 3, (int) std::floor (juce::jmax (0.0, t) / loopSeconds));
            b.setSample (0, i, (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * tones[pass] * i / rate)));
        }

        const auto f = work.getChildFile ("entrada-loop.wav");
        writeWav (f, b);
        loopInput = readWav (f);
    }

    app.run ("transport.record", args ({ { "on", true } }));
    app.run ("transport.play");
    feed (*rec, loopInput, 480);
    app.run ("transport.stop");

    const auto loopChannel = channelId (model.get(), "Voz take 1");
    juce::String group;
    int pieces = 0;

    for (auto& i : model.get().playlistItems)
        if (i.ref == loopChannel)
        {
            group = i.takeGroup;
            ++pieces;
        }

    const auto listed = app.run ("takes.list", args ({ { "group", group } })).value;
    rep.check (group.isNotEmpty() && (int) listed["takes"] == 4,
               "3 voltas e meia viram 4 takes de um grupo só, num canal e num arquivo ("_t + listed["takes"].toString() + " takes, "
                   + juce::String (pieces) + " pedaços)"_t);
    rep.check (rec->lastMessage().contains ("takes"), "e a barra avisa: \""_t + rec->lastMessage() + "\"");

    auto playingTakeAt = [&] (int tickAt)
    {
        for (auto& i : model.get().playlistItems)
            if (i.takeGroup == group && ! i.muted && i.start <= tickAt && tickAt < i.end())
                return i.take;

        return 0;
    };

    rep.check (playingTakeAt (loopFrom + 960) == 4 && playingTakeAt (loopFrom + ticksPerBar4 + 960) == 3,
               "o mais novo toca: o take 4 (meia volta) no compasso 3, e o take 3 segue no 4 (onde o 4 não chegou)"_t);

    auto hzAt = [&] (const Audio& a, int tickFrom, int tickTo)
    {
        return measureHz (a, tickFrom * tick + 0.05, tickTo * tick - 0.05);
    };

    const auto loopRender = work.getChildFile ("takes.wav");
    app.run ("project.exportWav", args ({ { "path", loopRender.getFullPathName() }, { "what", "song" } }));
    const auto loopAudio = readWav (loopRender);
    const double hz1 = hzAt (loopAudio, loopFrom, loopFrom + ticksPerBar4), hz2 = hzAt (loopAudio, loopFrom + ticksPerBar4, loopTo);
    rep.check (std::abs (hz1 - 550.0) < 3.0 && std::abs (hz2 - 440.0) < 3.0,
               "no export, o compasso 3 toca o take 4 ("_t + juce::String (hz1, 1) + " Hz) e o 4 o take 3 (" + juce::String (hz2, 1) + " Hz)");

    // o crossfade no corte do compasso 4: sem degrau (um corte seco entre os dois senos pularia até 1,0)
    {
        const int cut = (int) std::round ((loopFrom + ticksPerBar4) * tick * rate);
        float jump = 0.0f;
        const float* x = loopAudio.buffer.getReadPointer (0);

        for (int i = cut - 960; i < cut + 960 && i < loopAudio.buffer.getNumSamples(); ++i)
            if (i > 0)
                jump = juce::jmax (jump, std::abs (x[i] - x[i - 1]));

        rep.check (loopAudio.valid && jump < 0.09f,
                   "no corte entre o take 4 e o 3, crossfade de 10 ms: o maior passo entre amostras é "_t + juce::String (jump, 4)
                       + " (um seno sozinho dá até 0,036; um corte seco pularia até 1,0)");
    }

    // comping: corta todos no meio do compasso 3 e escolhe o take 1 para a primeira metade
    const int half = loopFrom + ticksPerBar4 / 2;
    rep.check (app.run ("takes.cut", args ({ { "group", group }, { "at", half } })).ok, "corta todos os takes no mesmo ponto (takes.cut)"_t);
    juce::String take1Piece;

    for (auto& i : model.get().playlistItems)
        if (i.takeGroup == group && i.take == 1 && i.start == loopFrom)
            take1Piece = i.id;

    rep.check (take1Piece.isNotEmpty() && app.run ("takes.choose", args ({ { "item", take1Piece } })).ok
                   && playingTakeAt (loopFrom + 240) == 1 && playingTakeAt (half + 240) == 4,
               "um clique escolhe o take 1 para o primeiro pedaço; o segundo continua no take 4"_t);

    const auto compRender = work.getChildFile ("comp.wav");
    app.run ("project.exportWav", args ({ { "path", compRender.getFullPathName() }, { "what", "song" } }));
    const auto compAudio = readWav (compRender);
    const double hzA = hzAt (compAudio, loopFrom, half), hzB = hzAt (compAudio, half, loopFrom + ticksPerBar4);
    rep.check (std::abs (hzA - 220.0) < 3.0 && std::abs (hzB - 550.0) < 3.0,
               "o comp soa: "_t + juce::String (hzA, 1) + " Hz (take 1) e depois " + juce::String (hzB, 1) + " Hz (take 4)");

    rep.check (app.run ("edit.undo").ok && playingTakeAt (loopFrom + 240) == 4, "Ctrl+Z desfaz a escolha"_t);
    app.run ("edit.redo");

    // gravar de novo no mesmo lugar, sem loop: vira o take 5 do grupo e toca
    app.setTimeSelection ({});
    app.run ("transport.setPosition", args ({ { "at", loopFrom } }));
    {
        juce::AudioBuffer<float> b (1, (int) (2.0 * rate));

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 660.0 * i / rate)));

        const auto f = work.getChildFile ("entrada-take5.wav");
        writeWav (f, b);
        app.run ("transport.play");
        feed (*rec, readWav (f), 512);
        app.run ("transport.stop");
    }

    rep.check ((int) app.run ("takes.list", args ({ { "group", group } })).value["takes"] == 5 && playingTakeAt (loopFrom + 960) == 5,
               "sem loop, a passada nova no mesmo lugar vira o take 5 do grupo, e toca"_t);

    {
        // a playlist com o grupo aberto: o comp em cima, uma sub-raia por take
        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1200, 420);
        Playlist list (app);
        host.addAndMakeVisible (list);
        list.setBounds (host.getLocalBounds());
        list.refresh (model.get());
        auto* grid = childOf<PlaylistGrid> (list);

        if (grid != nullptr)
            grid->setGroupOpen (group, true);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
        rep.check (grid != nullptr && grid->isGroupOpen (group) && savePng (list, ctx.work.getChildFile ("voz-takes.png")),
                   "captura da playlist com o grupo aberto (comp e 5 sub-raias)"_t);
        host.removeChildComponent (&list);
        host.setLookAndFeel (nullptr);
    }

    {
        // salvar e reabrir: os grupos e os números dos takes voltam, e soa igual
        const auto before = work.getChildFile ("takes-antes.wav");
        app.run ("project.exportWav", args ({ { "path", before.getFullPathName() }, { "what", "song" } }));
        const auto file = work.getChildFile ("takes").getChildFile ("Takes.wnl");
        app.run ("project.save", args ({ { "path", file.getFullPathName() } }));
        const auto saved = model.get();
        app.run ("project.new");
        app.run ("project.open", args ({ { "path", file.getFullPathName() } }));

        bool same = saved.playlistItems.size() == model.get().playlistItems.size();

        for (auto& i : saved.playlistItems)
            if (const auto* j = model.get().findItem (i.id); j == nullptr || j->takeGroup != i.takeGroup || j->take != i.take || j->muted != i.muted)
                same = false;

        const auto after = work.getChildFile ("takes-depois.wav");
        app.run ("project.exportWav", args ({ { "path", after.getFullPathName() }, { "what", "song" } }));
        const float diff = maxDiff (readWav (before), readWav (after));
        rep.check (same && diff < 1.0e-6f, "o .wnl guarda os grupos (takeGroup e take), e o projeto reaberto soa igual (diferença "_t
                                               + juce::String (diff) + ")");
    }

    //==========================================================================
    rep.section ("Gravar voz: edição do clipe (fatia 4)"_t);
    {
        // o comp do grupo de takes vira um WAV só, e soa igual
        const auto before = work.getChildFile ("antes-de-consolidar.wav");
        app.run ("project.exportWav", args ({ { "path", before.getFullPathName() }, { "what", "song" } }));
        juce::Array<juce::var> comp;

        for (auto& i : model.get().playlistItems)
            if (i.takeGroup == group && ! i.muted)
                comp.add (i.id);

        const auto consolidated = app.run ("audio.consolidate", args ({ { "items", comp } }));
        const auto* single = consolidated.ok ? model.get().findItem (consolidated.value["item"].toString()) : nullptr;
        int left = 0;

        for (auto& i : model.get().playlistItems)
            left += i.takeGroup == group && ! i.muted ? 1 : 0;

        const auto after = work.getChildFile ("depois-de-consolidar.wav");
        app.run ("project.exportWav", args ({ { "path", after.getFullPathName() }, { "what", "song" } }));
        const float diff = maxDiff (readWav (before), readWav (after));
        rep.check (single != nullptr && left == 0 && diff < 2.0e-3f,
                   "consolidar o comp: "_t + juce::String (comp.size()) + " pedaços viram um bloco e um WAV, e soa igual (diferença "_t
                       + juce::String (diff) + ")");
        rep.check (app.run ("edit.undo").ok && model.get().findItem (comp[0].toString()) != nullptr, "Ctrl+Z volta os pedaços do comp"_t);
    }

    {
        // um clipe conhecido: 2 s de seno a 440 Hz com pico 0,25, e um clique em 0,5 s (o inverter acha ele)
        app.run ("project.new");
        app.run ("transport.setBpm", args ({ { "bpm", 120.0 } }));
        app.run ("transport.setSongMode", args ({ { "on", true } }));
        juce::AudioBuffer<float> b (1, (int) (2.0 * rate));

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (float) (0.25 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / rate)));

        b.setSample (0, (int) (0.5 * rate), 0.6f);
        const auto clipFile = work.getChildFile ("clipe.wav");
        writeWav (clipFile, b);
        const auto added = app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", clipFile.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
        const auto clipId = added.value["item"].toString();
        static PlaylistItem missing;   // se o addItem falhar, os checks reprovam em vez de derrubar o autoteste
        auto clip = [&] { const auto* found = model.get().findItem (clipId); return found != nullptr ? found : &missing; };
        auto render = [&] (const juce::String& name)
        {
            const auto f = work.getChildFile (name);
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
            return readWav (f);
        };

        // A referência é o export sem ganho nem fade: o canal tem o nível padrão dele no caminho até o master (o seno de
        // 0,25 sai em 0,125), então o ganho, os fades e o normalizar são medidos contra ela (build 54).
        const auto plain = render ("sem-ganho.wav");
        const float plainSine = juce::jmax (1.0e-6f, peakIn (plain, 0.6, 1.4)), plainPeak = juce::jmax (1.0e-6f, peakIn (plain, 0.0, 2.0));

        rep.check (added.ok && clip() != nullptr && app.run ("audio.setGain", args ({ { "item", clipId }, { "db", 6.0 } })).ok
                       && clip()->gainDb == 6.0f, "ganho do bloco em +6 dB"_t);
        const auto loud = render ("ganho.wav");
        const float sinePeak = peakIn (loud, 0.6, 1.4);
        rep.check (plainSine > 0.01f && std::abs (sinePeak / plainSine - 1.995f) < 0.03f,
                   "no export, o seno sobe 6 dB ("_t + juce::String (sinePeak / plainSine, 3) + " vezes o export sem ganho, que dá "
                       + juce::String (plainSine, 3) + ")");

        app.run ("audio.setGain", args ({ { "item", clipId }, { "db", 0.0 } }));
        rep.check (app.run ("audio.setFades", args ({ { "item", clipId }, { "in", 0.4 }, { "out", 0.4 } })).ok
                       && std::abs (clip()->fadeIn - 0.4) < 1.0e-9 && std::abs (clip()->fadeOut - 0.4) < 1.0e-9, "fades de 0,4 s nas duas pontas"_t);
        const auto faded = render ("fades.wav");
        const float startPeak = peakIn (faded, 0.0, 0.03), midFade = peakIn (faded, 0.19, 0.21), body = peakIn (faded, 0.8, 1.2),
                    endPeak = peakIn (faded, 1.97, 2.0);
        rep.check (startPeak < 0.12f * plainSine && std::abs (midFade - 0.5f * plainSine) < 0.08f * plainSine
                       && std::abs (body - plainSine) < 0.02f * plainSine && endPeak < 0.12f * plainSine,
                   "o fade sobe reto (contra o export sem fade): "_t + juce::String (startPeak, 3) + " no começo, "_t + juce::String (midFade, 3) + " no meio do fade, "
                       + juce::String (body, 3) + " no corpo e " + juce::String (endPeak, 3) + " no fim");

        app.run ("audio.setFades", args ({ { "item", clipId }, { "in", 0.0 }, { "out", 0.0 } }));
        const auto normalized = app.run ("audio.normalize", args ({ { "item", clipId } }));
        rep.check (normalized.ok && std::abs (clip()->gainDb - (-1.0f - 20.0f * std::log10 (0.6f))) < 0.11f,
                   "normalizar: o pico do trecho (o clique, 0,6) vai a -1 dB (ganho de "_t + juce::String (clip()->gainDb, 1) + " dB)");
        const auto loudest = render ("normalizado.wav");
        const float gainSeen = 20.0f * std::log10 (juce::jmax (1.0e-6f, peakIn (loudest, 0.0, 2.0)) / plainPeak);
        rep.check (std::abs (gainSeen - clip()->gainDb) < 0.1f,
                   "no export, o pico sobe o ganho do normalizar ("_t + juce::String (gainSeen, 2) + " dB contra o export sem ganho)");

        app.run ("audio.setGain", args ({ { "item", clipId }, { "db", 0.0 } }));
        const auto originalRef = clip()->ref;
        const auto reversed = app.run ("audio.reverse", args ({ { "item", clipId } }));
        const auto backwards = render ("invertido.wav");
        const int clickAtReversed = [&]
        {
            int best = 0;
            float value = 0.0f;

            for (int i = 0; i < backwards.buffer.getNumSamples(); ++i)
                if (std::abs (backwards.buffer.getSample (0, i)) > value)
                {
                    value = std::abs (backwards.buffer.getSample (0, i));
                    best = i;
                }

            return best;
        }();
        rep.check (reversed.ok && clip()->ref != originalRef && juce::File (reversed.value["path"].toString()).existsAsFile()
                       && std::abs (clickAtReversed - (int) (1.5 * rate) + 1) <= 1,
                   "inverter: o clique de 0,5 s toca em 1,5 s (amostra "_t + juce::String (clickAtReversed) + ")");
        rep.check (app.run ("audio.reverse", args ({ { "item", clipId } })).ok && clip()->ref == originalRef,
                   "inverter de novo volta para o arquivo original (sem arquivo novo)"_t);

        {
            // as alças com o mouse: arrastar o canto de cima à esquerda faz o fade; a de baixo, o ganho
            theme::LookAndFeel laf;
            juce::Component host;
            host.setLookAndFeel (&laf);
            host.setSize (1200, 300);
            Playlist list (app);
            host.addAndMakeVisible (list);
            list.setBounds (host.getLocalBounds());
            list.refresh (model.get());

            if (auto* grid = childOf<PlaylistGrid> (list))
            {
                const float px = grid->getPxPerTick();
                const float left = 150.0f + 1.0f, top = 28.0f + 3.0f, bottom = 28.0f + 54.0f - 4.0f;
                const float centre = 150.0f + (float) (clip()->length * 0.5) * px;
                auto mouse = [&] (juce::Point<float> p, juce::Point<float> down)
                {
                    const auto now = juce::Time::getCurrentTime();
                    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                             juce::MouseInputSource::defaultTiltY, grid, grid, now, down, now, 1, p != down);
                };
                auto dragMouse = [&] (juce::Point<float> from, juce::Point<float> to)
                {
                    grid->mouseDown (mouse (from, from));
                    grid->mouseDrag (mouse ((from + to) * 0.5f, from));
                    grid->mouseDrag (mouse (to, from));
                    grid->mouseUp (mouse (to, from));
                };

                const float fadeTo = left + (float) secondsToTicks (0.5, 120.0) * px;
                dragMouse ({ left + 3.0f, top + 4.0f }, { fadeTo, top + 4.0f });
                rep.check (clip()->fadeIn > 0.4 && clip()->fadeIn < 0.6, "arrastar a alça do canto faz o fade de entrada ("_t
                                                                            + juce::String (clip()->fadeIn, 3) + " s)");
                list.refresh (model.get());
                dragMouse ({ centre, bottom - 2.0f }, { centre, bottom - 32.0f });
                rep.check (std::abs (clip()->gainDb - 6.0f) < 0.3f, "arrastar a alça de baixo 30 px para cima: +"_t + juce::String (clip()->gainDb, 1) + " dB");
                rep.check (app.run ("edit.undo").ok && clip()->gainDb == 0.0f, "um Ctrl+Z desfaz o arrasto do ganho inteiro"_t);
                app.run ("edit.redo");
                list.refresh (model.get());
                grid->mouseMove (mouse ({ centre, bottom - 10.0f }, { centre, bottom - 10.0f }));
                rep.check (savePng (list, ctx.work.getChildFile ("voz-clipe.png")), "captura do bloco com o fade, o ganho e as alças"_t);
            }

            host.removeChildComponent (&list);
            host.setLookAndFeel (nullptr);
        }

        const auto file = work.getChildFile ("clipe").getChildFile ("Clipe.wnl");
        app.run ("project.save", args ({ { "path", file.getFullPathName() } }));
        const auto json = file.loadFileAsString();
        app.run ("project.new");
        app.run ("project.open", args ({ { "path", file.getFullPathName() } }));
        rep.check (json.contains ("\"gainDb\"") && json.contains ("\"fadeIn\"") && clip() != nullptr && clip()->fadeIn > 0.4 && clip()->gainDb > 5.0f,
                   "o .wnl guarda o ganho e os fades do bloco (campos opcionais), e reabre igual"_t);
    }

    //==========================================================================
    rep.section ("Gravar voz: os arquivos e a entrada pela playlist (fatia 5)"_t);
    {
        app.run ("project.new");
        app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
        app.run ("transport.setSongMode", args ({ { "on", true } }));
        app.run ("record.setCountIn", args ({ { "bars", 0 } }));
        rec->simulateDevice (rate, 128, 352);

        const auto lane = model.get().playlistTracks[1];
        const auto laneInput = app.run ("playlist.setLaneInput", args ({ { "track", 2 }, { "first", 1 } }));
        const auto mx = laneInput.value["mixerTrack"].toString();
        const auto* m = model.get().findMixer (mx);
        rep.check (laneInput.ok && m != nullptr && m->name == lane.name && m->armed && model.get().playlistTracks[1].recordTrack == mx
                       && rec->settings().firstInput == 0 && ! rec->settings().stereo,
                   "escolher a entrada 1 no cabeçalho da raia 2 cria a trilha \""_t + lane.name + "\" no mixer e já arma (mono)"_t);

        const auto folder = ctx.data.getChildFile ("Gravacoes");
        const auto base = lane.name.replaceCharacter (' ', '_');

        for (int k = 1; k <= 3; ++k)
            folder.getChildFile (base + "_take" + juce::String (k).paddedLeft ('0', 2) + ".wav").deleteFile();   // de uma rodada anterior

        auto takeOnce = [&] (double seconds)
        {
            app.run ("transport.setPosition", args ({ { "at", 0 } }));
            app.run ("transport.record", args ({ { "on", true } }));
            app.run ("transport.play");
            feed (*rec, voiceInput (work.getChildFile ("entrada-raia.wav"), seconds, -1), 512);
        };

        takeOnce (1.5);
        app.run ("transport.stop");
        const auto* laneTake1 = model.get().findChannel (channelId (model.get(), lane.name + " take 1"));
        const auto* take1Item = laneTake1 != nullptr ? itemFor (model.get(), laneTake1->id) : nullptr;
        rep.check (laneTake1 != nullptr && take1Item != nullptr && take1Item->track == lane.id
                       && juce::File (laneTake1->samplePath).getFileName() == base + "_take01.wav",
                   "o take cai na raia que grava, e o arquivo se chama \""_t + (laneTake1 != nullptr ? juce::File (laneTake1->samplePath).getFileName() : juce::String())
                       + "\" (a trilha e o número)"_t);

        takeOnce (1.5);
        app.run ("transport.stop");
        const auto* take2 = model.get().findChannel (channelId (model.get(), lane.name + " take 2"));
        rep.check (take2 != nullptr && juce::File (take2->samplePath).getFileName() == base + "_take02.wav", "o segundo vira \"_take02.wav\""_t);

        // cancelar: o arquivo é apagado e nada entra no projeto
        const auto channelsBefore = model.get().channels.size();
        takeOnce (1.0);
        const auto cancelled = folder.getChildFile (base + "_take03.wav");
        const bool existedWhileRecording = cancelled.existsAsFile();
        rep.check (app.run ("record.cancel").ok && ! rec->isRecording() && ! app.isPlaying() && existedWhileRecording && ! cancelled.existsAsFile()
                       && model.get().channels.size() == channelsBefore && rec->lastMessage().contains ("cancelado"),
                   "cancelar o take apaga o arquivo e não põe nada no projeto: \""_t + rec->lastMessage() + "\"");
        rep.check (! app.run ("record.cancel").ok, "cancelar sem estar gravando é recusado"_t);

        // Ctrl+Z tira o take inteiro: as voltas do loop, o canal e o que o grupo mudou nos outros takes
        const auto beforeLoop = model.get().playlistItems;
        app.setTimeSelection ({ 0, 2 * ticksPerBar4 });
        takeOnce (2.5 * 2 * ticksPerBar4 * tick);
        app.run ("transport.stop");
        app.setTimeSelection ({});
        const auto loopTake = channelId (model.get(), lane.name + " take 3");
        int loopItems = 0;

        for (auto& i : model.get().playlistItems)
            loopItems += loopTake.isNotEmpty() && i.ref == loopTake ? 1 : 0;

        rep.check (loopTake.isNotEmpty() && loopItems >= 2 && app.run ("edit.undo").ok && model.get().findChannel (loopTake) == nullptr
                       && model.get().playlistItems == beforeLoop,
                   "um Ctrl+Z tira o take em loop inteiro ("_t + juce::String (loopItems) + " blocos), e os outros takes voltam como estavam");

        const auto file = work.getChildFile ("raia").getChildFile ("Raia.wnl");
        app.run ("project.save", args ({ { "path", file.getFullPathName() } }));
        const auto json = file.loadFileAsString();
        app.run ("project.new");
        app.run ("project.open", args ({ { "path", file.getFullPathName() } }));
        rep.check (json.contains ("\"recordTrack\"") && model.get().playlistTracks[1].recordTrack == mx, "o .wnl guarda a raia que grava"_t);

        {
            theme::LookAndFeel laf;
            juce::Component host;
            host.setLookAndFeel (&laf);
            host.setSize (1000, 260);
            Playlist list (app);
            host.addAndMakeVisible (list);
            list.setBounds (host.getLocalBounds());
            list.refresh (model.get());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            rep.check (savePng (list, ctx.work.getChildFile ("voz-raia.png")), "captura da raia que grava, com a entrada no cabeçalho"_t);
            host.removeChildComponent (&list);
            host.setLookAndFeel (nullptr);
        }

        rep.check (app.run ("playlist.setLaneInput", args ({ { "track", 2 }, { "first", 0 } })).ok && model.get().playlistTracks[1].recordTrack.isEmpty()
                       && model.get().findMixer (mx) != nullptr && ! model.get().findMixer (mx)->armed,
                   "\"Parar de gravar nesta raia\" desarma e solta a raia (a trilha do mixer fica)"_t);
    }

    //==========================================================================
    rep.section ("Gravar voz: o beat em um gesto (fatia 6)"_t);
    {
        // um beat sintético (bumbo, caixa e chimbal), em estéreo, a 44,1 kHz
        auto writeBeat = [&] (const juce::File& f, double bpm, double seconds)
        {
            const double r = 44100.0, beatSeconds = 60.0 / bpm;
            juce::AudioBuffer<float> b (2, (int) (seconds * r));
            b.clear();
            juce::Random random (11);

            auto hit = [&] (double t, double hz, double decay, float amp)
            {
                const int at = (int) (t * r);

                for (int i = 0; i < (int) (decay * 5 * r) && at + i < b.getNumSamples(); ++i)
                {
                    const float v = (float) (amp * std::exp (-i / (decay * r)) * (hz > 0.0 ? std::sin (juce::MathConstants<double>::twoPi * hz * i / r)
                                                                                             : random.nextFloat() * 2.0 - 1.0));
                    b.addSample (0, at + i, v);
                    b.addSample (1, at + i, v);
                }
            };

            for (int k = 0; k * beatSeconds < seconds; ++k)
            {
                if (k % 2 == 0) hit (k * beatSeconds, 55.0, 0.12, 0.7f);
                if (k % 2 == 1) hit (k * beatSeconds, 0.0, 0.06, 0.4f);
                hit (k * beatSeconds, 0.0, 0.01, 0.12f);
                hit ((k + 0.5) * beatSeconds, 0.0, 0.01, 0.12f);
            }

            f.deleteFile();
            std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
            juce::WavAudioFormat wav;
            auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (r).withNumChannels (2).withBitsPerSample (24));
            return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        };

        app.run ("project.new");
        const auto unnamed = work.getChildFile ("beat sem nome.wav");
        const auto named = work.getChildFile ("Trap 150bpm.wav");
        rep.check (writeBeat (unnamed, 92.0, 12.0) && writeBeat (named, 150.0, 10.0), "dois beats: um a 92 BPM sem o andamento no nome, outro \"Trap 150bpm\""_t);

        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1200, 300);
        Playlist list (app);
        host.addAndMakeVisible (list);
        list.setBounds (host.getLocalBounds());
        list.refresh (model.get());
        juce::String status;

        if (auto* grid = childOf<PlaylistGrid> (list))
        {
            grid->onStatus = [&status] (const juce::String& text, bool) { status = text; };

            // arrastar o beat do Explorer para a raia 1, no começo
            grid->filesDropped ({ unnamed.getFullPathName() }, 151, 28 + 27);
            const auto* beatItem = model.get().playlistItems.empty() ? nullptr : &model.get().playlistItems.back();
            const int cover = (int) std::ceil (secondsToTicks (12.0, 92.0) - 1.0e-6);
            rep.check (std::abs (model.get().bpm - 92.0) < 0.01 && beatItem != nullptr && beatItem->start == 0 && beatItem->length == cover,
                       "soltar o beat num projeto vazio põe o projeto em "_t + juce::String (model.get().bpm, 2) + " BPM (pela análise), e o bloco cobre o beat ("_t
                           + juce::String (beatItem != nullptr ? beatItem->length : -1) + " ticks)");
            rep.check (status.contains ("92") && status.contains ("análise"_t), "e a barra diz: \""_t + status + "\"");
            rep.check (app.run ("edit.undo").ok && model.get().playlistItems.empty() && std::abs (model.get().bpm - 140.0) < 0.01,
                       "um Ctrl+Z tira o beat e volta o BPM"_t);
            app.run ("edit.redo");
            list.refresh (model.get());

            // com o projeto já andando, o outro beat só diz o andamento dele (pelo nome)
            grid->filesDropped ({ named.getFullPathName() }, 151, 28 + 54 + 27);
            const auto namedItem = model.get().playlistItems.back().id;
            rep.check (std::abs (model.get().bpm - 92.0) < 0.01 && status.contains ("150"),
                       "com mais coisa na playlist, o BPM fica, e a barra diz o do beat novo: \""_t + status + "\"");

            const auto matched = app.run ("audio.matchBpm", args ({ { "item", namedItem } }));
            const int namedCover = (int) std::ceil (secondsToTicks (10.0, 150.0) - 1.0e-6);
            rep.check (matched.ok && matched.value["source"].toString() == "nome" && std::abs (model.get().bpm - 150.0) < 0.01
                           && model.get().findItem (namedItem)->length == namedCover,
                       "\"Acertar o BPM por este áudio\" (a setinha do bloco): 150 pelo nome do arquivo, e o bloco cobre o beat"_t);

            const auto doubled = app.run ("audio.matchBpm", args ({ { "item", namedItem }, { "bpm", model.get().bpm * 0.5 } }));
            rep.check (doubled.ok && std::abs (model.get().bpm - 75.0) < 0.01, "e a metade (75), para o beat que veio em meio tempo"_t);

            juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
            rep.check (savePng (list, ctx.work.getChildFile ("voz-beat.png")), "captura da playlist com os dois beats"_t);

            // O beat com o andamento no nome e compassos inteiros, o caso que travava a exportação: o Tracktion ligava o
            // auto-tempo pelo nome do arquivo (achado da Fatiar e 808; corrigido no extra-onda1 abde694). Soltar acerta
            // o BPM pelo nome, e a música exporta inteira.
            app.run ("project.new");
            list.refresh (model.get());
            const double wholeSeconds = 8 * 4 * 60.0 / 128.0;   // 8 compassos a 128: 15 s
            const auto wholeBars = work.getChildFile ("Beat 128bpm.wav");
            const bool wroteWhole = writeBeat (wholeBars, 128.0, wholeSeconds);
            grid->filesDropped ({ wholeBars.getFullPathName() }, 151, 28 + 27);
            rep.check (wroteWhole && std::abs (model.get().bpm - 128.0) < 0.01 && model.get().playlistItems.size() == 1 && model.get().songMode,
                       "soltar \"Beat 128bpm.wav\" (8 compassos inteiros) põe o projeto em 128 BPM pelo nome e passa para SONG (fatia 8)"_t);

            const auto exported = work.getChildFile ("beat-128bpm-exportado.wav");
            exported.deleteFile();
            juce::String exportError;

            {
                Watchdog guard (rep, "exportar a música com \"Beat 128bpm.wav\" na playlist"_t, 90.0);
                const auto exportedResult = app.run ("project.exportWav", args ({ { "path", exported.getFullPathName() }, { "what", "song" } }));
                exportError = exportedResult.ok ? juce::String() : exportedResult.error;
            }

            const auto exportedWav = readWav (exported);
            const double gotSeconds = exportedWav.sampleRate > 0 ? exportedWav.buffer.getNumSamples() / exportedWav.sampleRate : 0.0;
            const float gotPeak = gotSeconds > 0.0 ? peakIn (exportedWav, 0.0, wholeSeconds) : 0.0f;
            rep.check (exportError.isEmpty() && gotSeconds > wholeSeconds - 0.1 && gotPeak > 0.3f && gotPeak < 1.0f,
                       "e a música com ele exporta inteira, sem travar ("_t + juce::String (gotSeconds, 2) + " s, pico "
                           + juce::String (gotPeak, 2) + (exportError.isEmpty() ? juce::String() : ", erro: " + exportError) + ")");
        }

        host.removeChildComponent (&list);
        host.setLookAndFeel (nullptr);
    }

    //==========================================================================
    rep.section ("Gravar voz: o assistente de placas e microfones (fatia 7)"_t);
    {
        const auto micBefore = rec->settings().mic;
        const auto inputBefore = rec->settings();
        app.run ("project.new");
        const auto vozA = app.run ("mixer.addTrack", args ({ { "name", "Voz" } })).value["track"].toString();
        app.run ("record.arm", args ({ { "track", vozA } }));   // armada, ela monitora pelo app (o padrão)

        app.run ("record.setMic", args ({ { "type", "condenser" } }));
        const auto scarlett = app.run ("record.guide", args ({ { "driver", "ASIO" }, { "device", "Focusrite USB ASIO" } })).value;
        juce::String steps;

        if (auto* list = scarlett["steps"].getArray())
            for (auto& s : *list)
                steps << s.toString() << " | ";

        rep.check (scarlett["kind"].toString() == "interface" && (bool) scarlett["phantom"] && (bool) scarlett["needsPhantom"]
                       && (bool) scarlett["preferAsio"] && steps.contains ("48V") && steps.contains ("mono"),
                   "uma interface pelo driver ASIO dela, com condensador: o 48V, o ASIO, a entrada 1 em mono"_t);
        rep.check ((bool) scarlett["warnDirectMonitor"] && steps.contains ("eco"),
                   "a placa tem monitoração direta e o app também monitora: o aviso de eco"_t);

        const auto usbMic = app.run ("record.guide", args ({ { "driver", "Windows Audio" }, { "device", "Microfone (AT2020USB+)" } })).value;
        const auto notebook = app.run ("record.guide", args ({ { "driver", "Windows Audio" }, { "device", "Microfone (Realtek(R) Audio)" } })).value;
        const auto behringer = app.run ("record.guide", args ({ { "driver", "ASIO" }, { "device", "UMC ASIO Driver" } })).value;
        rep.check (usbMic["kind"].toString() == "usbMic" && ! (bool) usbMic["preferAsio"] && notebook["kind"].toString() == "builtIn"
                       && behringer["kind"].toString() == "interface",
                   "o microfone USB (sem ASIO), a placa do computador e a outra interface são reconhecidos"_t);
        rep.check (! app.run ("record.setMic", args ({ { "type", "fita k7" } })).ok, "tipo de microfone desconhecido é recusado"_t);

        // o medidor mede a entrada escolhida mesmo sem gravar (para acertar o ganho antes do take)
        app.run ("record.setInput", args ({ { "first", 2 }, { "stereo", false } }));
        rec->takeInputPeak (0);
        rec->takeInputPeak (1);
        {
            juce::AudioBuffer<float> b (2, 512);
            b.clear();

            for (int i = 0; i < 512; ++i)
            {
                b.setSample (0, i, 0.9f * (float) std::sin (0.05 * i));   // a entrada 1, que não é a escolhida
                b.setSample (1, i, 0.2f * (float) std::sin (0.05 * i));   // a entrada 2: -14 dBFS de pico
            }

            const float* ptrs[2] { b.getReadPointer (0), b.getReadPointer (1) };
            rec->input().pushInput (ptrs, 2, 512, 0);
        }

        const float mixerPeak = rec->takeInputPeak (0), guidePeak = rec->takeInputPeak (1);
        const double guideDb = 20.0 * std::log10 (guidePeak);
        rep.check (std::abs (mixerPeak - 0.2f) < 0.005f && std::abs (guidePeak - 0.2f) < 0.005f && guide::judge (guideDb) == guide::Level::good,
                   "sem gravar, o medidor lê a entrada 2 (a escolhida): pico "_t + juce::String (guideDb, 1) + " dBFS, no alvo; os dois leitores veem o mesmo");
        rep.check (guide::judge (-30.0) == guide::Level::low && guide::judge (-0.5) == guide::Level::clip, "abaixo de -18 é baixo; acima de -1, estourou"_t);

        {
            // a seção Gravação do Áudio e MIDI com o assistente
            theme::LookAndFeel laf;
            juce::Component stage;
            stage.setLookAndFeel (&laf);
            auto panel = std::make_unique<DeviceSettings> (app, [] { return false; }, [] (bool) {});
            stage.setSize (panel->getWidth(), panel->getHeight());
            stage.addAndMakeVisible (*panel);
            auto* mic = dynamic_cast<juce::ComboBox*> (panel->findChildWithID ("micType"));
            auto* text = dynamic_cast<juce::Label*> (panel->findChildWithID ("guideText"));
            rep.check (mic != nullptr && text != nullptr && mic->getSelectedId() == 2 && text->getText().contains ("48V"),
                       "a janela Áudio e MIDI mostra o microfone (condensador) e os passos: \""_t
                           + (text != nullptr ? text->getText().upToFirstOccurrenceOf ("\n", false, false) : juce::String()) + "\"");

            if (mic != nullptr)
            {
                mic->setSelectedId (3, juce::sendNotificationSync);
                rep.check (rec->settings().mic == "dynamic" && text != nullptr && text->getText().contains ("ganho"),
                           "trocar para dinâmico muda os passos (mais ganho, sem 48V)"_t);
            }

            auto* device = dynamic_cast<juce::ComboBox*> (panel->findChildWithID ("micDevice"));
            rep.check (device != nullptr && device->getNumItems() >= 2,
                       "a seção Gravação tem o campo \"Microfone (aparelho)\" (fatia 8): "_t
                           + (device != nullptr ? device->getText() : juce::String()));

            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            rep.check (savePng (stage, ctx.work.getChildFile ("voz-assistente.png")), "captura da seção Gravação com o assistente"_t);
            stage.removeChildComponent (panel.get());
            stage.setLookAndFeel (nullptr);
        }

        rec->setMic (micBefore);
        rec->setInput (inputBefore.firstInput, inputBefore.stereo);
    }

    //==========================================================================
    rep.section ("Gravar voz: o botão Gravar voz, o microfone e o fone (fatia 8)"_t);
    {
        app.run ("project.new");
        app.run ("record.setCountIn", args ({ { "bars", 0 } }));
        rec->simulateDevice (rate, 128, 352);

        // REC sem nada armado: o aviso sai ao ligar, e o fim diz o que fazer
        app.run ("transport.record", args ({ { "on", true } }));
        const auto warnedNow = rec->lastMessage();
        app.run ("transport.play");
        app.run ("transport.stop");
        const auto warnedEnd = rec->lastMessage();
        app.run ("transport.record", args ({ { "on", false } }));
        rep.check (warnedNow.contains ("Nada armado") && warnedNow.contains ("Gravar voz") && warnedEnd.contains ("Gravar voz"),
                   "REC sem nada armado: o aviso sai ao ligar (\""_t + warnedNow + "\") e no fim (\"" + warnedEnd + "\")");

        // o botão "Gravar voz": num projeto novo, cria a trilha "Voz", arma, liga a contagem e o REC
        const auto voice = app.run ("record.voice");
        const auto voiceId = voice.value["track"].toString();
        const auto* voiceTrack = model.get().findMixer (voiceId);
        rep.check (voice.ok && (bool) voice.value["created"] && voiceTrack != nullptr && voiceTrack->name == "Voz" && voiceTrack->armed
                       && rec->isRecordMode() && rec->settings().countInBars >= 1,
                   "Gravar voz num projeto novo: cria a trilha \"Voz\", arma, liga a contagem ("_t
                       + juce::String (rec->settings().countInBars) + " compasso) e o REC");

        app.run ("transport.play");
        feed (*rec, voiceInput (work.getChildFile ("entrada-gravar-voz.wav"), 4.0, -1), 512);
        app.run ("transport.stop");
        const auto* voiceTake = model.get().findChannel (channelId (model.get(), "Voz take 1"));
        rep.check (voiceTake != nullptr && voiceTake->output == voiceId && itemFor (model.get(), voiceTake->id) != nullptr,
                   "o play grava a voz: \"Voz take 1\" sai pela trilha Voz, com o bloco na playlist"_t);

        const auto again = app.run ("record.voice");
        const auto voices = std::count_if (model.get().mixer.begin(), model.get().mixer.end(), [] (const MixerTrack& m) { return m.name == "Voz"; });
        rep.check (again.ok && ! (bool) again.value["created"] && again.value["track"].toString() == voiceId && voices == 1,
                   "de novo: acha a mesma trilha \"Voz\", sem criar outra"_t);
        app.run ("transport.record", args ({ { "on", false } }));
        app.run ("record.setCountIn", args ({ { "bars", 0 } }));

        // o microfone (o aparelho de entrada): a lista, escolher um, recusar o que não está ligado, voltar ao padrão
        rec->simulateDeviceName ("Windows Audio", "Microfone USB (simulado)");
        const auto mics = app.run ("record.inputDevices");
        const auto* devices = mics.value["devices"].getArray();
        rep.check (mics.ok && (bool) mics.value["separate"] && devices != nullptr && devices->size() == 2,
                   "record.inputDevices lista os microfones do Windows Audio ("_t + juce::String (devices != nullptr ? devices->size() : 0) + ")");
        rep.check (app.run ("record.setInputDevice", args ({ { "name", "Microfone USB (simulado)" } })).ok
                       && rec->settings().inputDevice == "Microfone USB (simulado)"
                       && app.run ("record.guide").value["device"].toString() == "Microfone USB (simulado)",
                   "escolher o microfone USB: fica no gravacao.json, e o assistente passa a olhar para ele"_t);
        rep.check (! app.run ("record.setInputDevice", args ({ { "name", "Microfone que não existe"_t } })).ok
                       && rec->settings().inputDevice == "Microfone USB (simulado)",
                   "um microfone que não está na lista é recusado"_t);
        rep.check (app.run ("record.setInputDevice", args ({ { "name", "" } })).ok && rec->settings().inputDevice.isEmpty(),
                   "e \"\" volta para o padrão do Windows"_t);
        rec->simulateDeviceName ({}, {});
    }

    // não deixa ajuste para as outras áreas (a contagem fica no gravacao.json da pasta de dados)
    app.run ("record.setHeadphones", args ({ { "answer", headphonesBefore == 1 ? "yes" : headphonesBefore == 2 ? "no" : "" } }));
    app.run ("transport.record", args ({ { "on", false } }));
    app.run ("record.setCountIn", args ({ { "bars", 0 } }));
    app.run ("transport.setPosition", args ({ { "at", 0 } }));
}

} // namespace whynot::selftest
