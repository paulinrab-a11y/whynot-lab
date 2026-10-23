// Autoteste da fluidez (pedido do usuário de 21/10 à noite: "animações fluidas, sem travamentos" e "fluido
// para quem faz o beat em 1 minuto"). Monta um projeto grande por comandos e mede, sem janela:
// - o paint das quatro telas numa área de 1920x1080 (inteiro e só a coluna da agulha), com createComponentSnapshot;
// - a latência dos comandos comuns (com o desfazer e a remontagem do motor) e se eles refazem o grafo do Tracktion;
// - o toJson, abrir e salvar;
// - o congelar: editar um canal que não sai pela trilha congelada não renderiza ela de novo.
// Os números vão para o relatório e para docs/desempenho-ui.md. Dono: frente Fluidez.

#include "SelfTestKit.h"
#include "../../engine/PreviewPlayer.h"
#include "../../model/WnlFormat.h"
#include "../../synth/SynthParams.h"
#include "../../dsp/FxCatalog.h"
#include "../../ui/ChannelRack.h"
#include "../../ui/MixerView.h"
#include "../../ui/PianoRoll.h"
#include "../../ui/Playlist.h"
#include "../../ui/Fluid.h"
#include "../../ui/Theme.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace whynot::selftest
{

namespace
{
    double nowMs()   { return juce::Time::getMillisecondCounterHiRes(); }

    /** Mediana de n execuções de f (i = número da execução), em ms: um tranco do sistema não estraga a conta. */
    template <typename F>
    double medianMs (int n, F&& f)
    {
        std::vector<double> times;

        for (int i = 0; i < n; ++i)
        {
            const auto t0 = nowMs();
            f (i);
            times.push_back (nowMs() - t0);
        }

        std::sort (times.begin(), times.end());
        return times.empty() ? 0.0 : times[times.size() / 2];
    }

    juce::String ms (double v)   { return juce::String (v, v < 10.0 ? 2 : 1) + " ms"; }

    void pump (int milliseconds)   { juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds); }

    /** O Edit do Tracktion marca "refazer o grafo de reprodução" (restartPlayback) ligando o próprio Timer por
        1 ms. Ele herda do juce::Timer em privado; o cast no estilo C chega na base mesmo assim, só para olhar. */
    struct RestartProbe
    {
        explicit RestartProbe (te::Edit& e) : timer ((juce::Timer&) e) {}

        void settle()
        {
            for (int i = 0; i < 20 && timer.isTimerRunning(); ++i)
                pump (5);
        }

        bool pending() const   { return timer.isTimerRunning(); }

        juce::Timer& timer;
    };

    bool writeTone (const juce::File& f, double hz, double seconds, double rate)
    {
        const int n = (int) (seconds * rate), ramp = (int) (0.01 * rate);
        juce::AudioBuffer<float> b (2, n);

        for (int i = 0; i < n; ++i)
        {
            const double env = juce::jmin (1.0, juce::jmin (i, n - 1 - i) / (double) ramp);
            const auto v = (float) (0.3 * env * std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }

        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (16));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, n);
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

    /** 28 canais, 4 padrões com passos e notas, 150 blocos de padrão e 12 clipes de áudio na playlist,
        6 trilhas com 2 ou 3 efeitos. Tudo num gesto, para montar rápido (um passo de desfazer). */
    bool buildBigProject (SelfTestContext& ctx, const juce::File& toneFile)
    {
        auto& app = ctx.app;
        auto& model = ctx.model;

        if (! app.run ("project.new").ok)
            return false;

        juce::Array<juce::File> kit;

        for (auto& c : model.get().channels)
            kit.add (model.resolveSample (c.samplePath));

        if (kit.isEmpty())
            return false;

        CommandRegistry::ScopedGesture gesture (app.getCommands(), "Montar o projeto grande");

        for (int i = (int) model.get().channels.size(); i < 28; ++i)
            app.run ("channel.addSampler", args ({ { "path", kit[i % kit.size()].getFullPathName() }, { "name", "Som " + juce::String (i + 1) },
                                                   { "rootKey", 60 }, { "detectRoot", false } }));

        static const char* const grids[] = { "x...x...x...x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
                                             "x..x..x...x..x..", "x.......x.......", ".x.x.x.x.x.x.x.x", "3...x...2...x..." };

        const auto firstPattern = model.get().currentPattern;
        juce::StringArray patterns { firstPattern };

        for (const auto& name : { "Verso"_t, "Refrão"_t, "Ponte"_t })
        {
            const auto added = app.run ("pattern.add", args ({ { "name", name } }));
            patterns.add (added.value["pattern"].toString());
        }

        for (int pi = 0; pi < patterns.size(); ++pi)
        {
            app.run ("pattern.select", args ({ { "pattern", patterns[pi] } }));
            const auto channels = model.get().channels;   // cópia: os comandos mexem no modelo

            for (size_t ci = 0; ci < channels.size(); ++ci)
            {
                if ((ci + (size_t) pi) % 3 == 2)
                    continue;   // nem todo canal toca em todo padrão

                setGrid (app, channels[ci].id, grids[(ci + (size_t) pi) % std::size (grids)], 90 + (int) (ci % 30));
            }

            // notas em 4 canais melódicos (o 808, o Bell e dois "Som")
            for (size_t ci : { (size_t) 4, (size_t) 5, (size_t) 10, (size_t) 17 })
            {
                if (ci >= channels.size())
                    continue;

                juce::Array<juce::var> notes;

                for (int n = 0; n < 24; ++n)
                    notes.add (noteVar (n * 0.5 + 0.25 * (n % 2), 0.5, 36 + (n * 5 + pi * 3 + (int) ci) % 36, n % 7 == 3));

                addNotes (app, channels[ci].id, notes);
            }
        }

        app.run ("pattern.select", args ({ { "pattern", firstPattern } }));

        // 150 blocos de padrão nas raias 1 a 10 (15 compassos cada) e 12 clipes de áudio nas raias 11 e 12
        for (int track = 1; track <= 10; ++track)
            for (int bar = 0; bar < 15; ++bar)
                app.run ("playlist.addItem", args ({ { "kind", "pattern" }, { "ref", patterns[(track + bar) % patterns.size()] },
                                                     { "track", track }, { "start", bar * ticksPerBar4 } }));

        for (int k = 0; k < 12; ++k)
            app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", toneFile.getFullPathName() },
                                                 { "track", 11 + k % 2 }, { "start", (k / 2) * 2 * ticksPerBar4 + (k % 2) * ticksPerBar4 } }));

        // 6 trilhas com efeitos
        const char* const chains[][3] = { { "eq", "comp", "sat" }, { "eq", "comp", nullptr }, { "filter", "delay", nullptr },
                                          { "eq", "reverb", nullptr }, { "comp", "sat", "limiter" }, { "eq", "stereo", "reverb" } };
        int fxTrack = 0;

        for (auto& m : model.get().mixer)
        {
            if (m.isMaster() || fxTrack >= 6)
                continue;

            const auto id = m.id;

            for (auto* type : chains[fxTrack])
                if (type != nullptr)
                    app.run ("mixer.addInsert", args ({ { "track", id }, { "type", type } }));

            ++fxTrack;
        }

        return true;
    }
    //==========================================================================
    // toJson igual ao de antes: um projeto com tudo o que o .wnl tem, e os casos chatos do texto

    Project trickyProject()
    {
        Project p;
        p.name = "Beat \"aspas\" \\ barra\nlinha\ttab \x01 ctrl ação ñ 漢字 😀"_t;
        p.bpm = 93.33333333;
        p.timeSigNum = 7;
        p.timeSigDen = 8;
        p.keyRoot = 9;
        p.scale = "dorian";
        p.metronome = true;
        p.loop = false;
        p.songMode = true;
        p.punchOn = true;
        p.punchIn = 960;
        p.punchOut = 123457;

        auto channel = [&] (const char* id, const char* kind)
        {
            Channel c;
            c.id = id;
            c.name = juce::String ("Canal ") + id;
            c.kind = kind;
            p.channels.push_back (c);
            return &p.channels.back();
        };

        auto* a = channel ("ch_a", "sampler");
        a->samplePath = "samples/Kick \"duro\".wav";
        a->originalPath = "C:/Users/Fulano/Música/Kick.wav"_t;
        a->rootKey = 36;
        a->oneShot = false;
        a->cutItself = true;
        a->mono = true;
        a->glideMs = 0.0f;
        a->rootCents = -12.34f;
        a->tune = 0.005f;
        a->attackMs = 1.0e-6f;
        a->decayMs = 1.0e7f;
        a->sustain = 0.33333f;
        a->pitchDrop = -24.0f;
        a->pitchDropMs = 2.5f;
        a->start = 0.12345f;
        a->chokeGroup = 3;
        a->swing = 66.66f;
        a->mute = true;
        a->output = "mx_bus";
        a->armed = true;
        // Fatiar e 808 (integração de teste, 22/10): release, laço e fatias
        a->releaseMs = 123.45f;
        a->loop = true;
        a->loopStart = 0.1234567f;
        a->loopEnd = 0.987654f;
        a->loopXfadeMs = 12.34f;
        a->slices = { 0.0f, 0.25f, 0.5000001f };
        a->sliceKey = 48;
        a->sliceBpm = 92.3456f;

        auto* s = channel ("ch_s", "synth");
        s->synthPreset = "Pad \"frio\"";
        int k = 0;

        for (auto& prm : synth::allParams())
            if (++k % 3 != 0)
                s->synthParams[prm.id] = (float) (k * 0.123456) - 3.0f;

        s->synthParams["param_que_nao_existe"] = 1.5f;   // o toJson só escreve os do synth, na ordem dele

        auto* piano = channel ("ch_p", "sampler");
        piano->instrument = "piano";
        piano->instrumentPreset = "lofi";

        auto* plug = channel ("ch_v", "plugin");
        plug->plugin.name = "Synth \"X\"";
        plug->plugin.manufacturer = "Fabricante/ç"_t;
        plug->plugin.file = "C:\\Program Files\\Common Files\\VST3\\X.vst3";
        plug->plugin.uid = "ABCDEF12";
        plug->plugin.instrument = true;
        plug->plugin.state = "VC2!+/=AAAA";

        auto* audio = channel ("ch_au", "audio");
        audio->samplePath = "C:/voz.wav";

        channel ("", "sampler");   // id vazio: arquivo estranho, mas o texto tem de sair igual

        Pattern pat;
        pat.id = "pt_1";
        pat.name = "Padrão ☆"_t;
        pat.lengthSteps = 32;
        pat.steps["ch_a"] = std::vector<Step> (32);
        pat.steps["ch_a"][0] = { true, 127, 1 };
        pat.steps["ch_a"][5] = { true, 1, 8 };
        pat.steps["ch_a"][31] = { true, 64, 3 };
        pat.steps["ch_s"] = std::vector<Step> (32);   // raia sem passo ligado nem nota: não sai
        pat.notes["ch_s"] = { { 1, 0, 240, 60, 100, false }, { 2, 960, 1, 127, 1, true },
                              { 3, 1920, 480, 64, 90, false, true } };   // nota muda (bloco 1, 22/10)
        pat.notes["ch_p"] = {};
        pat.notes["ch_que_nao_existe"] = { { 1, 0, 240, 60, 100, false } };
        p.patterns.push_back (pat);

        Pattern empty;
        empty.id = "pt_2";
        empty.name = "";
        empty.lengthSteps = 1;
        p.patterns.push_back (empty);   // "lanes": {} vazio
        p.currentPattern = "pt_1";

        p.playlistTracks = { { "pl_1", "Raia \"1\"" }, { "pl_2", "" } };

        PlaylistItem i1;
        i1.id = "it_1"; i1.ref = "pt_1"; i1.track = "pl_1"; i1.start = 0; i1.length = 7680;
        i1.patternOffset = 240; i1.muted = true;
        PlaylistItem i2;
        i2.id = "it_2"; i2.kind = "audio"; i2.ref = "ch_au"; i2.track = "pl_2"; i2.start = 3840; i2.length = 12345;
        i2.offsetSeconds = 1.2345678; i2.stretch = 1.5; i2.tape = true;
        PlaylistItem i3;
        i3.id = "it_3"; i3.kind = "audio"; i3.ref = "ch_au"; i3.track = "pl_2"; i3.start = 0; i3.length = 240;
        i3.offsetSeconds = 1.0e-7; i3.stretch = 0.25;
        // gravar voz (integração de teste, 22/10): takes, ganho e fades do bloco, a raia que grava e a escuta
        i2.takeGroup = "tg_1"; i2.take = 2; i2.gainDb = -3.25f; i2.fadeIn = 0.0123456789; i2.fadeOut = 1.5;
        i3.takeGroup = "tg_1"; i3.take = 1; i3.gainDb = 24.0f; i3.fadeOut = 1.0e-7;
        p.playlistTracks[1].recordTrack = "mx_bus";
        p.playlistItems = { i1, i2, i3 };
        p.markers = { { "mk_a", 0, "Intro \"aspas\"" }, { "mk_b", 7680, "Refrão"_t } };   // marcadores (bloco 1, 22/10)

        AutomationLane lane;
        lane.id = "au_1";
        lane.target = { "insert", "mx_bus", "", "fx_1", "cutoff" };
        lane.points = { { 0, 0.0f }, { 960, 0.33333f }, { 1920, 1.0f } };
        AutomationLane lane2;
        lane2.id = "au_2";
        lane2.target = { "channel", "", "ch_v", "", "3" };
        p.automation = { lane, lane2 };

        MixerTrack bus;
        bus.id = "mx_bus";
        bus.name = "Bus \"vocal\"";
        bus.colour = "#FF00AA";
        bus.volumeDb = -100.0f;
        bus.pan = -0.33333f;
        bus.mute = true;
        bus.solo = true;
        bus.autoFor = "ch_a";
        bus.role = "vocal";
        bus.armed = true;
        bus.frozen = true;
        bus.monitor = "on";

        Insert fx;
        fx.id = "fx_1";
        fx.type = "filter";
        fx.bypass = true;
        fx.preset = "Abafado";
        fx.sidechain = "mx_master";

        if (auto* info = dsp::findEffect ("filter"))
            for (auto& prm : info->params)
                fx.params[prm.id] = prm.def + 0.1f;

        fx.params["zzz_desconhecido"] = 2.0f;
        fx.params["aaa_desconhecido"] = -2.0f;

        Insert vst;
        vst.id = "fx_2";
        vst.kind = "vst3";
        vst.type = "EQ externo";
        vst.plugin.name = "EQ";
        vst.plugin.state = "abc";
        vst.params["0"] = 0.5f;

        Insert unknown;
        unknown.id = "fx_3";
        unknown.type = "tipo_que_nao_existe";
        unknown.params["b"] = 1.0f;
        unknown.params["a"] = 0.0f;

        bus.inserts = { fx, vst, unknown };
        bus.sends = { { "mx_master", -6.02f }, { "mx_x", 0.0f, true } };   // o segundo pré-fader (revisão do mixer, 22/10)
        p.mixer.push_back (bus);

        MixerTrack master;
        master.id = masterTrackId;
        master.name = "Master";
        master.volumeDb = 1.0e-6f;
        p.mixer.push_back (master);

        return p;
    }

    /** Compara o toJson novo com o de antes e diz onde o texto começa a diferir. */
    bool sameJson (const Project& p, juce::String& where)
    {
        const auto fast = wnl::toJson (p);
        const auto reference = wnl::toJsonReference (p);

        if (fast == reference)
            return true;

        // comparação pelos bytes (o operator[] do juce::String anda do começo a cada letra)
        const auto* a = reference.toRawUTF8();
        const auto* b = fast.toRawUTF8();
        const auto la = std::strlen (a), lb = std::strlen (b);
        size_t i = 0;

        while (i < la && i < lb && a[i] == b[i])
            ++i;

        auto around = [i] (const char* text, size_t length)
        {
            const size_t from = i > 30 ? i - 30 : 0;
            return juce::String::fromUTF8 (text + from, (int) (juce::jmin (length, i + 30) - from)).replace ("\n", "\\n");
        };

        where = "no byte " + juce::String ((juce::int64) i) + ": \"" + around (a, la) + "\" virou \"" + around (b, lb) + "\"";
        return false;
    }

    //==========================================================================
    /** Os ids dos clipes que o projetor pôs na trilha de um canal. */
    std::vector<juce::uint64> clipIds (AppController& app, const juce::String& channel)
    {
        std::vector<juce::uint64> ids;

        if (auto* track = app.getProjector().trackFor (channel))
            for (auto* clip : track->getClips())
                ids.push_back (clip->itemID.getRawID());

        std::sort (ids.begin(), ids.end());
        return ids;
    }

    int kept (const std::vector<juce::uint64>& before, const std::vector<juce::uint64>& after)
    {
        int n = 0;

        for (auto id : after)
            n += std::binary_search (before.begin(), before.end(), id) ? 1 : 0;

        return n;
    }

}

void runFluiditySelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    rep.section ("Fluidez: projeto grande (28 canais, 162 blocos, 6 trilhas com efeitos)"_t);

    const auto folder = ctx.work.getChildFile ("fluidez");
    folder.createDirectory();
    const auto toneFile = folder.getChildFile ("tom-4s.wav");
    rep.check (writeTone (toneFile, 220.0, 4.0, 44100.0), "WAV de 4 s para os clipes de áudio"_t);

    const auto t0 = nowMs();
    const bool built = buildBigProject (ctx, toneFile);
    pump (120);   // a remontagem do motor que o gesto deixou para depois
    const double buildMs = nowMs() - t0;

    const auto& p = model.get();
    int notes = 0, steps = 0;

    for (auto& pat : p.patterns)
    {
        for (auto& [id, list] : pat.notes)   notes += (int) list.size();
        for (auto& [id, list] : pat.steps)   for (auto& s : list) steps += s.on ? 1 : 0;
    }

    int inserts = 0;

    for (auto& m : p.mixer)
        inserts += (int) m.inserts.size();

    rep.check (built && p.channels.size() >= 29 && p.playlistItems.size() == 162 && inserts >= 14,
               juce::String ((int) p.channels.size()) + " canais (com o de áudio), "_t + juce::String ((int) p.patterns.size()) + " padrões, "_t
                   + juce::String (steps) + " passos ligados, " + juce::String (notes) + " notas, " + juce::String ((int) p.playlistItems.size())
                   + " blocos, " + juce::String ((int) p.mixer.size()) + " trilhas de mixer, " + juce::String (inserts) + " efeitos; montado em "
                   + ms (buildMs));

    //--------------------------------------------------------------------------
    // toJson, ler, abrir e salvar
    const auto json = wnl::toJson (p);
    const double tJson = medianMs (9, [&] (int) { (void) wnl::toJson (model.get()); });
    const double tRead = medianMs (9, [&] (int) { Project q; (void) wnl::fromJson (json, q); });
    const double tNormalise = medianMs (9, [&] (int) { Project q = model.get(); wnl::normalise (q); });

    rep.check (json.length() > 50000, "toJson "_t + ms (tJson) + " para " + juce::String (json.getNumBytesAsUTF8() / 1024) + " KB (2 por comando), ler "
                                      + ms (tRead) + ", normalise com cópia "_t + ms (tNormalise));

    const auto projectFile = folder.getChildFile ("Grande.wnl");
    projectFile.deleteFile();
    const double tSaveFirst = medianMs (1, [&] (int) { app.run ("project.save", args ({ { "path", projectFile.getFullPathName() } })); });
    const double tSave = medianMs (5, [&] (int) { app.run ("project.save", args ({ { "path", projectFile.getFullPathName() } })); });
    const double tOpen = medianMs (3, [&] (int) { app.run ("project.open", args ({ { "path", projectFile.getFullPathName() } })); });
    pump (50);

    rep.check (projectFile.existsAsFile() && model.get().playlistItems.size() == 162,
               "salvar "_t + ms (tSave) + " (o primeiro, que copia os samples para a pasta: " + ms (tSaveFirst) + "), abrir de novo o mesmo "
                   + ms (tOpen) + " (o motor já está montado)");

    // do zero: o motor monta as 60 trilhas, os plugins e os clipes de novo
    std::vector<double> cold;

    for (int i = 0; i < 2; ++i)
    {
        app.run ("project.new");
        pump (20);
        const auto start = nowMs();
        app.run ("project.open", args ({ { "path", projectFile.getFullPathName() } }));
        cold.push_back (nowMs() - start);
    }

    pump (50);
    rep.check (model.get().playlistItems.size() == 162, "abrir o projeto grande com outro aberto antes: "_t + ms (cold[0]) + " e " + ms (cold[1]));

    //--------------------------------------------------------------------------
    // Comandos comuns, cada um um passo de desfazer, com a remontagem do motor
    RestartProbe probe (app.getEdit());
    const auto& q = model.get();
    const auto kick = q.channels.front().id;
    const auto bassId = q.channels.size() > 4 ? q.channels[4].id : kick;
    const auto itemId = q.playlistItems.front().id;
    const int itemStart = q.playlistItems.front().start;
    juce::String volumeTrack;

    for (auto& m : q.mixer)
        if (! m.isMaster() && volumeTrack.isEmpty())
            volumeTrack = m.id;

    struct Timed { double ms = 0.0; bool rebuilds = false; };

    auto timeCommand = [&] (int n, std::function<void (int)> runOne)
    {
        Timed t;
        std::vector<double> times;

        for (int i = 0; i < n; ++i)
        {
            probe.settle();
            const auto start = nowMs();
            runOne (i);
            times.push_back (nowMs() - start);
            t.rebuilds = t.rebuilds || probe.pending();
        }

        std::sort (times.begin(), times.end());
        t.ms = times[times.size() / 2];
        probe.settle();
        return t;
    };

    auto stepToggle = [&] (int i) { app.run ("steps.set", args ({ { "channel", kick }, { "index", 1 + 2 * (i % 7) }, { "on", i % 2 == 0 } })); };
    auto addNote = [&] (int i) { addNotes (app, bassId, { noteVar (8.0 + i * 0.25, 0.25, 48 + i % 12, false) }); };
    auto moveBlock = [&] (int i) { app.run ("playlist.updateItem", args ({ { "item", itemId }, { "start", itemStart + ((i % 2) + 1) * ticksPerStep } })); };
    auto setVolume = [&] (int i) { app.run ("mixer.setVolume", args ({ { "track", volumeTrack }, { "db", -3.0 - 0.5 * (i % 5) } })); };

    app.run ("transport.setSongMode", args ({ { "on", false } }));
    const auto patStep = timeCommand (11, stepToggle);
    const auto patNote = timeCommand (11, addNote);
    const auto patVolume = timeCommand (11, setVolume);

    app.run ("transport.setSongMode", args ({ { "on", true } }));
    const auto songStep = timeCommand (11, stepToggle);
    const auto songMove = timeCommand (11, moveBlock);
    const auto songVolume = timeCommand (11, setVolume);

    auto yesNo = [] (bool b) { return b ? juce::String ("refaz o grafo") : juce::String ("não refaz o grafo"_t); };

    rep.check (true, "PAT: steps.set "_t + ms (patStep.ms) + " (" + yesNo (patStep.rebuilds) + "), pianoroll.addNotes " + ms (patNote.ms) + " ("
                         + yesNo (patNote.rebuilds) + "), mixer.setVolume " + ms (patVolume.ms) + " (" + yesNo (patVolume.rebuilds) + ")");
    rep.check (true, "SONG: steps.set "_t + ms (songStep.ms) + " (" + yesNo (songStep.rebuilds) + "), mover bloco " + ms (songMove.ms) + " ("
                         + yesNo (songMove.rebuilds) + "), mixer.setVolume " + ms (songVolume.ms) + " (" + yesNo (songVolume.rebuilds) + ")");
    rep.check (! patVolume.rebuilds && ! songVolume.rebuilds, "mexer no volume durante a música não refaz o grafo do Tracktion"_t);

    // a remontagem do motor sozinha, sem nada mudado (a conta de reconciliar o projeto inteiro)
    const double tSyncIdle = medianMs (9, [&] (int) { app.getProjector().sync (model.get(), model.projectFolder()); });

    rep.check (true, "motor: reconciliar o projeto inteiro sem mudança "_t + ms (tSyncIdle));

    //--------------------------------------------------------------------------
    // Paint das telas, numa área de 1920x1080, fora da tela
    theme::LookAndFeel laf;
    juce::Component host;
    host.setLookAndFeel (&laf);
    host.setSize (1920, 1080);

    juce::Component blank;
    blank.setSize (1920, 1080);
    const double tBlank = medianMs (5, [&] (int) { (void) blank.createComponentSnapshot (blank.getLocalBounds(), true, 1.0f); });

    struct ViewTimes { double refresh = 0, full = 0, column = 0, row = 0; };

    auto timeView = [&] (juce::Component& view, std::function<void()> refresh, const char* png)
    {
        ViewTimes t;
        host.addAndMakeVisible (view);
        view.setBounds (host.getLocalBounds());
        refresh();
        pump (30);
        (void) view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);   // aquece os caches de fonte

        t.refresh = medianMs (7, [&] (int) { refresh(); });
        t.full = medianMs (5, [&] (int) { (void) view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f); });
        t.column = medianMs (15, [&] (int i) { (void) view.createComponentSnapshot ({ 700 + 9 * i, 0, 5, view.getHeight() }, true, 1.0f); });

        // uma linha de 40 px na largura toda: o que um clique num passo repinta no rack (antes, o rack inteiro)
        t.row = medianMs (9, [&] (int) { (void) view.createComponentSnapshot ({ 0, theme::metric::toolbar, view.getWidth(), 40 }, true, 1.0f); });

        if (png != nullptr)
            savePng (view, folder.getChildFile (png));

        host.removeChildComponent (&view);
        return t;
    };

    const auto& big = model.get();
    Playlist list (app);
    PianoRoll roll (app);
    ChannelRack rack (app);
    MixerView mixerView (app);
    roll.setChannel (bassId);

    const auto tList = timeView (list, [&] { list.refresh (big); }, "fluidez-playlist.png");
    const auto tRoll = timeView (roll, [&] { roll.refresh (big); }, "fluidez-pianoroll.png");
    const auto tRack = timeView (rack, [&] { rack.refresh (big); }, "fluidez-rack.png");
    const auto tMixer = timeView (mixerView, [&] { mixerView.refresh (big); }, "fluidez-mixer.png");

    auto line = [&] (const char* name, const ViewTimes& t)
    {
        return juce::String::fromUTF8 (name) + ": atualizar " + ms (t.refresh) + ", pintar inteira " + ms (t.full) + ", coluna da agulha " + ms (t.column);
    };

    rep.check (true, "1920x1080 (o snapshot vazio custa "_t + ms (tBlank) + "). " + line ("playlist", tList));
    rep.check (true, line ("piano roll", tRoll));
    rep.check (true, line ("channel rack", tRack) + "; uma linha (o clique num passo) " + ms (tRack.row));
    rep.check (true, line ("mixer", tMixer));

    host.setLookAndFeel (nullptr);

    //--------------------------------------------------------------------------
    // O toJson novo escreve o texto direto; tem de sair igual ao de antes, byte a byte
    rep.section ("Fluidez: toJson igual ao de antes, byte a byte"_t);
    {
        juce::String where;
        const auto tricky = trickyProject();
        rep.check (sameJson (tricky, where), "projeto com tudo do .wnl (synth, piano, VST3, automação, punch, aspas, barras, controles, "
                                             "acentos, emoji, id vazio, padrão vazio, parâmetros desconhecidos) "_t + where);

        Project reread;
        rep.check (wnl::fromJson (wnl::toJson (tricky), reread).wasOk() && wnl::toJson (reread) == wnl::toJsonReference (reread),
                   "o mesmo projeto lido de volta e escrito de novo"_t);

        Project nothing;
        rep.check (sameJson (nothing, where) && sameJson (big, where), "projeto vazio e o projeto grande "_t + where);

        // todos os .wnl que as outras áreas do autoteste gravaram (esticar, gravação, synth, piano...)
        int files = 0, equal = 0;
        juce::StringArray different;

        for (const auto& entry : juce::RangedDirectoryIterator (ctx.work, true, "*.wnl"))
        {
            Project loaded;

            if (wnl::load (entry.getFile(), loaded).failed())
                continue;

            ++files;

            if (sameJson (loaded, where))
                ++equal;
            else
                different.add (entry.getFile().getFileName() + " " + where);
        }

        rep.check (files >= 1 && equal == files, juce::String (equal) + " de " + juce::String (files) + " projetos do autoteste iguais "
                                                     + different.joinIntoString ("; "));

        const double tReference = medianMs (3, [&] (int) { (void) wnl::toJsonReference (big); });
        const double tFast = medianMs (9, [&] (int) { (void) wnl::toJson (big); });
        rep.check (tFast < tReference, "toJson do projeto grande: antes "_t + ms (tReference) + ", agora " + ms (tFast)
                                           + " (" + juce::String (tReference / juce::jmax (0.001, tFast), 1) + " vezes mais rápido)");
    }

    //--------------------------------------------------------------------------
    // O som não pode falhar enquanto se edita: o Tracktion reconhece o clipe pelo id ao refazer o grafo
    rep.section ("Fluidez: editar durante a música mantém os clipes (o som segue)"_t);
    {
        app.run ("transport.setSongMode", args ({ { "on", true } }));
        pump (20);
        const auto bass = model.get().channels.size() > 4 ? model.get().channels[4].id : juce::String();
        const auto before = clipIds (app, bass);

        app.run ("steps.set", args ({ { "channel", bass }, { "index", 3 }, { "on", true } }));
        const auto afterStep = clipIds (app, bass);
        rep.check (! before.empty() && afterStep == before,
                   "um passo novo no 808: os "_t + juce::String ((int) before.size()) + " clipes dele são os mesmos (mesmo id), reescritos no lugar");

        addNotes (app, bass, { noteVar (2.0, 0.5, 41, false) });
        rep.check (clipIds (app, bass) == before, "uma nota nova no piano roll: os mesmos clipes"_t);

        const auto& items = model.get().playlistItems;
        const auto moved = items[5].id;
        const int movedStart = items[5].start;
        app.run ("playlist.updateItem", args ({ { "item", moved }, { "start", movedStart + ticksPerBar4 * 20 } }));
        const auto afterMove = clipIds (app, bass);
        rep.check (kept (before, afterMove) + 1 >= (int) afterMove.size(),
                   "mover um bloco: "_t + juce::String (kept (before, afterMove)) + " de " + juce::String ((int) afterMove.size())
                       + " clipes do 808 ficam com o mesmo id (antes, todos eram refeitos)");

        app.run ("edit.undo");
        app.run ("edit.undo");
        app.run ("edit.undo");
        pump (20);
        rep.check (model.get().findItem (moved) != nullptr && model.get().findItem (moved)->start == movedStart,
                   "três Ctrl+Z voltam o bloco, a nota e o passo"_t);

        // o que o projetor pôs no Tracktion confere com as notas do modelo (passos + piano roll, bloco a bloco)
        int expected = 0, projected = 0;

        for (auto& spec : model.get().playlistItems)
            if (spec.kind == "pattern" && ! spec.muted)
                if (auto* pat = model.get().findPattern (spec.ref))
                    if (auto* ch = model.get().findChannel (bass))
                        expected += (int) Projector::notesFor (model.get(), *pat, *ch, spec.length, spec.patternOffset).size();

        if (auto* track = app.getProjector().trackFor (bass))
            for (auto* clip : track->getClips())
                if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
                    projected += midi->getSequence().getNumNotes();

        rep.check (expected > 0 && projected == expected, "as notas nos clipes do 808 ("_t + juce::String (projected) + ") batem com o modelo ("
                                                              + juce::String (expected) + ")");
        app.run ("transport.setSongMode", args ({ { "on", false } }));
    }

    //--------------------------------------------------------------------------
    // A agulha: o transporte anda em blocos de áudio; a tela, a cada quadro
    rep.section ("Fluidez: agulha lisa (VBlank e posição suavizada)"_t);
    {
        // 140 BPM, blocos de 512 amostras a 44,1 kHz (11,6 ms), quadros de 60 Hz com até 2 ms de atraso
        const double tps = 140.0 / 60.0 * ticksPerBeat, block = 512.0 / 44100.0;
        auto measuredAt = [&] (double t) { return std::floor (t / block) * block * tps; };   // a posição só anda a cada bloco

        auto worstJitter = [&] (double frameRate, bool smooth)
        {
            fluid::PlayheadSmoother smoother;
            juce::Random rng (7);
            double t = 0.0, previous = -1.0, previousT = 0.0, worst = 0.0;

            for (int frame = 0; frame < 600; ++frame)
            {
                // o Timer de 30 Hz do Windows chega em 31 ou 47 ms; o VBlank chega a cada 16,7 ms, com pouca folga
                const double dt = smooth ? 1.0 / frameRate + rng.nextDouble() * 0.002 : (rng.nextBool() ? 0.0313 : 0.0469);
                t += dt;
                const double shown = smooth ? smoother.update (measuredAt (t), dt, tps) : measuredAt (t);

                if (previous >= 0.0 && frame > 30)
                {
                    // quanto a agulha andou neste quadro contra o que devia ter andado no tempo do quadro
                    const double ideal = (t - previousT) * tps;
                    worst = juce::jmax (worst, std::abs ((shown - previous) - ideal));
                }

                previous = shown;
                previousT = t;
            }

            return worst;
        };

        const double before = worstJitter (30.0, false);
        const double after = worstJitter (60.0, true);
        rep.check (after < before * 0.5, "tranco da agulha (a maior diferença entre o que andou num quadro e o que devia): antes "_t
                                             + juce::String (before, 1) + " ticks a 30 Hz, agora " + juce::String (after, 1) + " ticks a 60 Hz");

        fluid::PlayheadSmoother smoother;
        smoother.update (5000.0, 1.0 / 60.0, tps);
        rep.check (juce::approximatelyEqual (smoother.update (100.0, 1.0 / 60.0, tps), 100.0) && smoother.update (-1.0, 1.0 / 60.0, tps) < 0.0,
                   "o laço que volta ao começo pula direto, e parado some"_t);

        // a rampa das transições (para o Design: a logo ao abrir, os temas): ~95 % no tempo dado, sem passar do alvo
        fluid::Ramp ramp (0.12);
        ramp.jumpTo (0.0);
        ramp.setTarget (1.0);
        double rampValue = 0.0, rampMax = 0.0;

        for (int frame = 0; frame < 7; ++frame)   // 7 quadros de 60 Hz = 0,117 s
        {
            rampValue = ramp.advance (1.0 / 60.0);
            rampMax = juce::jmax (rampMax, rampValue);
        }

        rep.check (rampValue > 0.93 && rampValue < 0.97 && rampMax <= 1.0, "rampa com easing: "_t + juce::String (rampValue * 100.0, 1)
                                                                              + " % do caminho em 7 quadros de 60 Hz");
    }

    //--------------------------------------------------------------------------
    rep.section ("Fluidez: atalhos e cliques do beat de 1 minuto"_t);
    {
        app.run ("project.new");
        const auto first = model.get().currentPattern;
        setGrid (app, model.get().channels.front().id, "x...x...x...x...");
        const auto cloned = app.run ("pattern.clone");
        const auto* copy = model.get().findPattern (cloned.value["pattern"].toString());
        rep.check (cloned.ok && copy != nullptr && model.get().currentPattern == copy->id && copy->id != first
                       && copy->steps == model.get().findPattern (first)->steps && model.get().patterns[1].id == copy->id,
                   "pattern.clone copia os passos e as notas, entra logo depois do original e fica escolhido ("_t + (copy != nullptr ? copy->name : juce::String()) + ")");
        app.run ("edit.undo");
        rep.check (model.get().patterns.size() == 1 && model.get().currentPattern == first, "um Ctrl+Z desfaz o clone"_t);

        auto fl = KeyMap::preset ("fl");
        auto has = [&] (const char* id, const juce::KeyPress& k)
        {
            auto it = fl.find (id);
            return it != fl.end() && std::find (it->second.begin(), it->second.end(), k) != it->second.end();
        };

        using KP = juce::KeyPress;
        rep.check (has ("pattern.new", KP (KP::F4Key)) && has ("pattern.clone", KP ('C', juce::ModifierKeys (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0))
                       && has ("pattern.next", KP (KP::numberPadAdd)) && has ("pattern.previous", KP (KP::numberPadSubtract))
                       && KeyMap::findAction ("pattern.clone") != nullptr,
                   "preset FL: F4 (próximo padrão vazio), Ctrl+Shift+C (clonar), + e - do teclado numérico (trocar de padrão)"_t);

        // arrastar um som para o rack: o canal nasce com a nota achada no arquivo
        const auto kit = model.resolveSample (model.get().channels[4].samplePath);   // o 808 do kit
        const double tAdd = medianMs (5, [&] (int) { app.run ("channel.addSampler", args ({ { "path", kit.getFullPathName() } })); app.run ("edit.undo"); });
        const auto longFile = ctx.work.getChildFile ("fluidez").getChildFile ("tom-4s.wav");
        const double tAddAudio = medianMs (5, [&] (int i)
        {
            app.run ("playlist.addItem", args ({ { "path", longFile.getFullPathName() }, { "track", 2 }, { "start", i * ticksPerBar4 } }));
        });

        // a prévia do navegador decodifica o arquivo antes de tocar
        PreviewPlayer player;
        auto& formats = app.getEngine().getAudioFileFormatManager().readFormatManager;
        const double tPreviewShort = medianMs (5, [&] (int) { player.play (kit, formats); });
        const auto thirty = ctx.work.getChildFile ("fluidez").getChildFile ("tom-40s.wav");
        writeTone (thirty, 110.0, 40.0, 44100.0);
        const double tPreviewLong = medianMs (3, [&] (int) { player.play (thirty, formats); });

        rep.check (true, "soltar um som no rack (com a nota achada no arquivo e um Ctrl+Z) "_t + ms (tAdd) + "; soltar um áudio na playlist "
                             + ms (tAddAudio) + "; prévia de um som do kit " + ms (tPreviewShort) + ", de um áudio de 40 s " + ms (tPreviewLong));
    }

    //--------------------------------------------------------------------------
    // Achado da revisão de 22/10: com uma trilha congelada, cada clique em qualquer canal renderizava de novo
    // o congelado, na thread da tela (a assinatura levava todos os padrões e a playlist)
    rep.section ("Fluidez: congelar não renderiza de novo a cada clique"_t);
    {
        app.run ("project.new");
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        const auto frozenKick = channelId (model.get(), "Kick");
        const auto otherHat = channelId (model.get(), "Hat");
        setGrid (app, frozenKick, "x...x...x...x...");
        setGrid (app, otherHat, "x.x.x.x.x.x.x.x.", 90);
        const auto* frozenKickChannel = model.get().findChannel (frozenKick);
        const auto mxKick = frozenKickChannel != nullptr ? model.get().resolveOutput (frozenKickChannel->output) : juce::String();
        auto& projector = app.getProjector();

        auto twentyHatSteps = [&]
        {
            const auto start = nowMs();

            for (int i = 0; i < 20; ++i)
                app.run ("steps.set", args ({ { "channel", otherHat }, { "index", 1 + 2 * (i % 8) }, { "on", i % 2 == 0 } }));

            return nowMs() - start;
        };

        const double live = twentyHatSteps();

        const double tFreeze = medianMs (1, [&] (int) { app.run ("channel.setFrozen", args ({ { "channel", frozenKick }, { "on", true } })); });
        const auto freezeFile = projector.freezeFileFor (mxKick);
        const auto renderedAt = freezeFile.getLastModificationTime();
        const auto firstCopy = ctx.work.getChildFile ("fluidez").getChildFile ("congelado-kick.freeze");
        freezeFile.copyFileTo (firstCopy);

        const double frozenHat = twentyHatSteps();
        rep.check (projector.isFrozen (mxKick) && freezeFile.getLastModificationTime() == renderedAt && freezeFile.hasIdenticalContentTo (firstCopy),
                   "20 passos no Hat (que não sai pelo Kick) não renderizam o Kick congelado de novo"_t);
        rep.check (frozenHat < live * 1.5 + 20.0,
                   "20 steps.set no Hat: "_t + ms (frozenHat) + " com o Kick congelado, " + ms (live) + " sem congelar (antes, cada um custava um render do Kick: "
                       + ms (tFreeze) + ")");

        // o conteúdo do Kick muda: sem janela (espera 0) congela de novo na hora, com o som novo
        setGrid (app, frozenKick, "x.......x.......");
        rep.check (projector.isFrozen (mxKick) && ! projector.freezeFileFor (mxKick).hasIdenticalContentTo (firstCopy),
                   "um passo no Kick congela de novo, com o som novo (sem janela, na hora)"_t);

        // com janela: a trilha toca ao vivo na hora e o render sai 1 s depois da última mudança
        projector.setRefreezeDelay (0.4);
        const double tEdit = medianMs (1, [&] (int) { setGrid (app, frozenKick, "x...x...x...x..."); });
        const bool liveNow = ! projector.isFrozen (mxKick) && projector.pendingRefreezes() == 1 && ! projector.isRefreezeDue();
        setGrid (app, frozenKick, "x...x...x...x.x.");   // mexeu de novo: o tempo recomeça
        pump (450);
        const bool due = projector.isRefreezeDue();
        app.getProjector().sync (model.get(), model.projectFolder());   // o que o Housekeeping do app faz quando vence
        rep.check (liveNow && due && projector.isFrozen (mxKick) && projector.pendingRefreezes() == 0,
                   "com espera: mexer no Kick congelado leva "_t + ms (tEdit) + " (ao vivo na hora, sem render), e o render sai depois que para de mexer");

        projector.setRefreezeDelay (0.0);
        app.run ("channel.setFrozen", args ({ { "channel", frozenKick }, { "on", false } }));
    }
}

} // namespace whynot::selftest
