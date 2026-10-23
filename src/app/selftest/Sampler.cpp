// Autoteste do sampler: nota automática (YIN), forma da nota (afinação, envelope, queda de tom,
// início), .wnl e o painel do channel rack aberto sem tela. Dono: Sampler.

#include "SelfTestKit.h"
#include "../../engine/Projector.h"
#include "../../model/WnlFormat.h"
#include "../../ui/PianoRoll.h"
#include "../../ui/ChannelRack.h"
#include "../../ui/MixerView.h"
#include "../../ui/Playlist.h"
#include "../../ui/SamplerPanel.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

void runSamplerSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto& workFolder = ctx.work;

    //--------------------------------------------------------------------------
    rep.section ("Sampler: nota automática"_t);
    app.run ("project.new");
    {
        const auto& pk = model.get();
        auto kitFile = [&] (const char* name)
        {
            const auto* ch = pk.findChannel (channelId (pk, name));
            return ch != nullptr ? model.resolveSample (ch->samplePath) : juce::File();
        };
        const auto file808 = kitFile ("808"), fileBell = kitFile ("Bell"), fileHat = kitFile ("Hat");

        const auto a808 = app.run ("channel.addSampler", args ({ { "path", file808.getFullPathName() }, { "name", "808 sem raiz" } }));
        rep.check (a808.ok && (bool) a808.value["rootDetected"] && (int) a808.value["rootKey"] == 36,
                   "o 808 do kit, sem dizer a raiz, cai em C2 (nota "_t + juce::String ((int) a808.value["rootKey"]) + ")");
        const auto aBell = app.run ("channel.addSampler", args ({ { "path", fileBell.getFullPathName() }, { "name", "Bell sem raiz" } }));
        rep.check (aBell.ok && (int) aBell.value["rootKey"] == 72, "o Bell cai em C5 (nota "_t + juce::String ((int) aBell.value["rootKey"]) + ")");
        const auto aHat = app.run ("channel.addSampler", args ({ { "path", fileHat.getFullPathName() }, { "name", "Hat sem raiz" } }));
        rep.check (aHat.ok && ! (bool) aHat.value["rootDetected"] && (int) aHat.value["rootKey"] == 60,
                   "chimbal não tem nota: a raiz fica em 60"_t);
        const auto aKept = app.run ("channel.addSampler", args ({ { "path", file808.getFullPathName() }, { "name", "808 raiz dada" }, { "rootKey", 40 } }));
        rep.check (aKept.ok && (int) aKept.value["rootKey"] == 40 && ! (bool) aKept.value["rootDetected"],
                   "rootKey dado no comando vence a detecção"_t);

        const auto detected = app.run ("channel.detectRoot", args ({ { "channel", channelId (model.get(), "Bell") }, { "apply", false } }));
        rep.check (detected.ok && (bool) detected.value["found"] && (int) detected.value["rootKey"] == 72,
                   "channel.detectRoot no Bell do kit: C5, confiança "_t + detected.value["confidence"].toString());
        const auto onHat = app.run ("channel.detectRoot", args ({ { "channel", channelId (model.get(), "Hat") } }));
        rep.check (onHat.ok && ! (bool) onHat.value["found"] && model.get().findChannel (channelId (model.get(), "Hat"))->rootKey == 60,
                   "channel.detectRoot no chimbal: sem nota, raiz intacta"_t);
    }

    // Sample desafinado: Lá 3 com 30 cents a mais. A raiz vira 57 +30 cents e o canal toca afinado.
    const auto sharpFile = workFolder.getChildFile ("la3_mais_30_cents.wav");
    {
        constexpr double rate = 48000.0;
        const double hz = 220.0 * std::pow (2.0, 0.3 / 12.0);
        juce::AudioBuffer<float> sine (1, (int) (1.5 * rate));

        for (int i = 0; i < sine.getNumSamples(); ++i)
            sine.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));

        sharpFile.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (sharpFile.createOutputStream().release());
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
        rep.check (writer != nullptr && writer->writeFromAudioSampleBuffer (sine, 0, sine.getNumSamples()), "WAV de teste com 30 cents a mais"_t);
    }

    const auto sharp = app.run ("channel.addSampler", args ({ { "path", sharpFile.getFullPathName() }, { "name", "Sharp" } }));
    const auto sharpId = sharp.value["channel"].toString();
    const auto sharpMixer = sharp.value["mixerTrack"].toString();
    rep.check (sharp.ok && (int) sharp.value["rootKey"] == 57 && std::abs ((double) sharp.value["rootCents"] - 30.0) < 3.0,
               "raiz detectada: "_t + Project::keyName ((int) sharp.value["rootKey"]) + " " + juce::String ((double) sharp.value["rootCents"], 1) + " cents (esperado A3, +30)");

    app.run ("pianoroll.addNotes", args ({ { "channel", sharpId }, { "notes", juce::Array<juce::var> { noteVar (0, 4, 57, false) } }, { "unit", "beats" } }));
    app.run ("mixer.setSolo", args ({ { "track", sharpMixer }, { "on", true } }));

    int renderIndex = 0;
    auto renderSharp = [&]
    {
        const auto f = workFolder.getChildFile ("sampler_" + juce::String (++renderIndex) + ".wav");
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        return readWav (f);
    };
    auto shape = [&] (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto a = args (kv);
        a.getDynamicObject()->setProperty ("channel", sharpId);
        return app.run ("channel.setSampler", a);
    };

    {
        const auto tuned = renderSharp();
        const double hz = measureHz (tuned, 0.1, 1.0);
        rep.check (std::abs (hz - 220.0) < 0.8, "o sample desafinado toca afinado na nota 57: "_t + juce::String (hz, 2) + " Hz (220,00)");
    }

    rep.section ("Sampler: forma da nota"_t);
    {
        rep.check (shape ({ { "tune", 12.0 } }).ok, "afinação +12"_t);
        const double hz = measureHz (renderSharp(), 0.1, 1.0);
        rep.check (std::abs (hz - 440.0) < 1.5, "afinação +12 toca uma oitava acima: "_t + juce::String (hz, 2) + " Hz (440,00)");
        shape ({ { "tune", 0.0 } });
    }
    {
        shape ({ { "sustain", 0.0 }, { "decayMs", 100.0 } });
        const auto out = renderSharp();
        const float early = peakIn (out, 0.01, 0.04), late = peakIn (out, 0.4, 0.6);
        rep.check (early > 0.05f && late < early * 0.001f, "sustain 0 e decay de 100 ms: a nota some antes de 0,4 s ("_t
                       + juce::String (juce::Decibels::gainToDecibels (late, -200.0f), 1) + " dBFS)");
        shape ({ { "sustain", 1.0 }, { "decayMs", 300.0 } });
    }
    {
        shape ({ { "pitchDrop", 12.0 }, { "pitchDropMs", 80.0 } });
        const auto out = renderSharp();
        const double start = measureHz (out, 0.0, 0.02), body = measureHz (out, 0.3, 1.0);
        rep.check (start > 300.0 && std::abs (body - 220.0) < 0.8, "queda de tom: começa em "_t + juce::String (start, 0)
                       + " Hz e assenta em " + juce::String (body, 2) + " Hz");
        shape ({ { "pitchDrop", 0.0 } });
    }
    {
        const float fullTail = peakIn (renderSharp(), 0.9, 1.2);
        shape ({ { "start", 0.5 } });
        const float halfTail = peakIn (renderSharp(), 0.9, 1.2);
        rep.check (fullTail > 0.1f && halfTail < 0.001f, "início em 50%: o sample de 1,5 s acaba em 0,75 s"_t);
    }

    {
        const auto decayBefore = model.get().findChannel (sharpId)->decayMs;
        shape ({ { "decayMs", 1234.0 }, { "sustain", 0.25 }, { "attackMs", 7.0 } });
        app.run ("edit.undo");
        rep.check (model.get().findChannel (sharpId)->decayMs == decayBefore, "desfazer volta a forma da nota"_t);
        app.run ("edit.redo");
        rep.check (! shape ({ { "decayMs", std::numeric_limits<double>::quiet_NaN() } }).ok
                       || std::isfinite (model.get().findChannel (sharpId)->decayMs),
                   "valor não finito não entra no modelo"_t);
    }

    {
        const auto file = workFolder.getChildFile ("Sampler Teste").getChildFile ("Sampler Teste.wnl");
        const auto saved = model.get();
        const auto wavBefore = renderSharp();
        rep.check (app.run ("project.save", args ({ { "path", file.getFullPathName() } })).ok, "salva com a forma da nota");
        app.run ("project.new");
        rep.check (app.run ("project.open", args ({ { "path", file.getFullPathName() } })).ok, "reabre");

        const auto* was = saved.findChannel (sharpId);
        const auto* now = model.get().findChannel (sharpId);
        rep.check (was != nullptr && now != nullptr && was->rootKey == now->rootKey && std::abs (was->rootCents - now->rootCents) < 0.051f
                       && now->tune == was->tune && now->attackMs == was->attackMs && now->decayMs == was->decayMs && now->sustain == was->sustain
                       && now->pitchDrop == was->pitchDrop && now->pitchDropMs == was->pitchDropMs && now->start == was->start,
                   "raiz, cents, afinação, envelope, queda e início voltam iguais do .wnl"_t);
        rep.check (maxDiff (wavBefore, renderSharp()) < 1.0e-4f, "e o canal soa igual depois de reabrir"_t);
        app.run ("project.new");
    }

    //--------------------------------------------------------------------------
    // Tarefa 3 do plano: grupo de corte (choke) e swing por canal.
    rep.section ("Sampler: choke e swing"_t);
    {
        app.run ("project.new");
        const auto open = channelId (model.get(), "Open hat"), hat = channelId (model.get(), "Hat");
        const auto openMixer = model.get().findChannel (open)->output;
        const double step = beat140 / 4.0;   // um 16 avos a 140 BPM

        int renders = 0;
        auto renderPattern = [&]
        {
            const auto f = workFolder.getChildFile ("choke_" + juce::String (++renders) + ".wav");
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            return readWav (f);
        };

        auto rms = [] (const Audio& a, double t0, double t1)
        {
            const int from = juce::jmax (0, (int) (t0 * a.sampleRate)), to = juce::jmin (a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));
            double sum = 0.0;
            for (int ch = 0; ch < a.buffer.getNumChannels(); ++ch)
                for (int i = from; i < to; ++i)
                    sum += (double) a.buffer.getSample (ch, i) * a.buffer.getSample (ch, i);
            return to > from ? std::sqrt (sum / (double) ((to - from) * a.buffer.getNumChannels())) : 0.0;
        };

        setGrid (app, open, "x...............");
        setGrid (app, hat,  "....x...........");
        app.run ("mixer.setSolo", args ({ { "track", openMixer }, { "on", true } }));   // só o hat aberto soa; o fechado ainda corta

        const auto free = renderPattern();
        rep.check (app.run ("channel.setChoke", args ({ { "channel", open }, { "group", 1 } })).ok
                       && app.run ("channel.setChoke", args ({ { "channel", hat }, { "group", 1 } })).ok,
                   "hat aberto e fechado no grupo de corte 1"_t);
        const auto choked = renderPattern();

        const double cut = 4.0 * step;
        const double freeAfter = rms (free, cut + 0.010, cut + 0.040), chokedAfter = rms (choked, cut + 0.010, cut + 0.040);
        const double dropDb = 20.0 * std::log10 (std::max (chokedAfter, 1.0e-12) / std::max (freeAfter, 1.0e-12));
        rep.check (freeAfter > 1.0e-3 && dropDb < -40.0, "o hat fechado corta o aberto: 10 ms depois do corte caiu "_t
                                                            + juce::String (dropDb, 1) + " dB (esperado < -40 dB)");

        float before = 0.0f;
        for (int ch = 0; ch < free.buffer.getNumChannels(); ++ch)
            for (int i = 0; i < (int) ((cut - 0.002) * free.sampleRate); ++i)
                before = juce::jmax (before, std::abs (free.buffer.getSample (ch, i) - choked.buffer.getSample (ch, i)));
        rep.check (before < 1.0e-6f, "antes do corte o hat aberto soa igual com e sem o grupo"_t);

        // O hat é ruído: o salto entre amostras já é grande. Sem estalo = o corte não salta mais que o som sem corte.
        auto maxJump = [cut] (const Audio& a)
        {
            float j = 0.0f;
            for (int i = (int) ((cut - 0.001) * a.sampleRate); i < (int) ((cut + 0.006) * a.sampleRate); ++i)
                j = juce::jmax (j, std::abs (a.buffer.getSample (0, i) - a.buffer.getSample (0, i - 1)));
            return j;
        };
        rep.check (maxJump (choked) <= maxJump (free) * 1.05f + 1.0e-4f, "o corte não estala (maior salto "_t + juce::String (maxJump (choked), 4)
                                                                            + ", sem corte " + juce::String (maxJump (free), 4) + ")");

        const auto saved = wnl::toJson (model.get());
        rep.check (saved.contains ("\"choke\": 1"), "o grupo vai para o .wnl (\"choke\": 1)"_t);
        app.run ("mixer.setSolo", args ({ { "track", openMixer }, { "on", false } }));

        // Swing: um clique curto nos passos 0 e 1; o passo 1 atrasa swing/100 * passo/3.
        const auto clickFile = workFolder.getChildFile ("clique.wav");
        {
            constexpr double rate = 48000.0;
            juce::AudioBuffer<float> b (1, (int) (0.005 * rate));
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / rate));
            clickFile.deleteFile();
            std::unique_ptr<juce::OutputStream> stream (clickFile.createOutputStream().release());
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }

        const auto click = app.run ("channel.addSampler", args ({ { "path", clickFile.getFullPathName() }, { "name", "Clique" }, { "detectRoot", false } }));
        const auto clickId = click.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", click.value["mixerTrack"].toString() }, { "on", true } }));
        setGrid (app, clickId, "xx..............");

        auto onset = [] (const Audio& a, double from)
        {
            for (int i = juce::jmax (0, (int) (from * a.sampleRate)); i < a.buffer.getNumSamples(); ++i)
                if (std::abs (a.buffer.getSample (0, i)) > 0.01f)
                    return i;
            return -1;
        };

        const auto straight = renderPattern();
        app.run ("channel.setSwing", args ({ { "channel", clickId }, { "amount", 50.0 } }));
        const auto swung = renderPattern();
        const int expected = juce::roundToInt (juce::roundToInt (50.0 / 100.0 * ticksPerStep / 3.0) / (double) ticksPerBeat * 60.0 / 140.0 * 48000.0);
        const int delay = onset (swung, step - 0.004) - onset (straight, step - 0.004);
        rep.check (std::abs (delay - expected) <= 2, "swing de 50%: o passo 2 atrasa "_t + juce::String (delay) + " amostras (esperado "
                                                       + juce::String (expected) + ")");
        rep.check (onset (straight, 0.0) == onset (swung, 0.0), "o passo 1 (par) não muda com o swing"_t);

        const auto json = wnl::toJson (model.get());
        Project back;
        rep.check (json.contains ("\"swing\": 50.0") && wnl::fromJson (json, back).wasOk()
                       && back.findChannel (clickId) != nullptr && back.findChannel (clickId)->swing == 50.0f,
                   "o swing vai e volta do .wnl"_t);

        // Passar os passos para o piano roll mantém o groove: as notas nascem onde os passos soavam.
        app.run ("pianoroll.stepsToNotes", args ({ { "channel", clickId } }));
        const auto converted = renderPattern();
        rep.check (onset (converted, step - 0.004) == onset (swung, step - 0.004), "passos para o piano roll mantêm o swing"_t);
        app.run ("project.new");
    }

    //--------------------------------------------------------------------------
    // O painel abre fora da área de trabalho, dentro de um componente pai: sem janela na tela,
    // sem roubar o foco de quem estiver usando o PC, e funciona numa sessão sem tela.
    rep.section ("Sampler: painel"_t);
    {
        app.run ("project.new");
        const auto bassId = channelId (model.get(), "808");

        theme::LookAndFeel laf;   // o visual da casa, só neste pai (nada global)
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1000, 600);

        auto& box = SamplerPanel::show (app, bassId, { 40, 40, 160, 24 }, nullptr, &host);
        SamplerPanel* panel = nullptr;

        for (auto* child : box.getChildren())
            if (auto* p = dynamic_cast<SamplerPanel*> (child))
                panel = p;

        rep.check (box.getParentComponent() == &host && box.isVisible() && panel != nullptr,
                   "SamplerPanel::show abre o painel num CallOutBox (sem tela)"_t);

        if (panel != nullptr)
        {
            rep.check (panel->getWidth() >= 600 && panel->getHeight() >= 200,
                       "painel com "_t + juce::String (panel->getWidth()) + " x " + juce::String (panel->getHeight()) + " px");

            // O painel acompanha o modelo (undo, IA): mudar pela linha de comando chega nele sem erro.
            rep.check (app.run ("channel.setSampler", args ({ { "channel", bassId }, { "tune", 1.5 }, { "sustain", 0.4 } })).ok,
                       "comando com o painel aberto"_t);

            const auto shot = workFolder.getChildFile ("sampler-painel.png");
            const auto image = box.createComponentSnapshot (box.getLocalBounds(), true, 2.0f);
            shot.deleteFile();
            bool saved = false;

            if (juce::FileOutputStream out (shot); out.openedOk())
            {
                juce::PNGImageFormat png;
                saved = png.writeImageToStream (image, out);
            }

            rep.check (saved && image.getWidth() > 0, "captura do painel em "_t + shot.getFullPathName());
        }

        // Canal apagado com o painel aberto: o painel se fecha sozinho. O CallOutBox fecha por
        // mensagem; com o PC ocupado (logo depois de um build) 100 ms já não bastaram, então espera
        // até 2 s, olhando a cada 50 ms.
        app.run ("channel.remove", args ({ { "channel", bassId } }));

        auto hasCallOut = [&host]
        {
            for (auto* child : host.getChildren())
                if (dynamic_cast<juce::CallOutBox*> (child) != nullptr)
                    return true;

            return false;
        };

        for (int i = 0; i < 40 && hasCallOut(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const bool stillOpen = hasCallOut();

        if (stillOpen)   // não deixa janela para trás, mesmo se a checagem falhar
            for (auto* child : host.getChildren())
                if (auto* b = dynamic_cast<juce::CallOutBox*> (child))
                    b->dismiss();

        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        rep.check (! stillOpen && host.getNumChildComponents() == 0, "canal apagado fecha o painel, sem sobrar componente"_t);
        host.setLookAndFeel (nullptr);
        app.run ("project.new");
    }
}

//==============================================================================
// Grade que segue o zoom, no piano roll e na playlist (pedido do usuário; os componentes são do
// Design). Fica neste arquivo para não mexer no CMakeLists.txt: um arquivo novo reconfigura o CMake,
// e isso recompila todos os arquivos do app. Pode virar selftest/Grid.cpp na próxima vez que o
// núcleo mexer no CMake.
//
// Tudo sem tela: os componentes ficam dentro de um pai fora da área de trabalho, o mouse é simulado
// e as capturas vão para a pasta do autoteste (grade-*.png).

namespace gridtest
{
    bool nests (const std::vector<int>& d)
    {
        if (d.empty())
            return false;

        for (size_t i = 1; i < d.size(); ++i)
            if (d[i] <= d[i - 1] || d[i] % d[i - 1] != 0)
                return false;

        return true;
    }

    template <typename T>
    T* childOf (juce::Component& parent)
    {
        for (auto* c : parent.getChildren())
            if (auto* t = dynamic_cast<T*> (c))
                return t;

        return nullptr;
    }

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, p, now, 1, false };
    }

    void click (juce::Component& c, juce::Point<float> p)
    {
        c.mouseDown (mouseAt (c, p));
        c.mouseUp (mouseAt (c, p));
    }

    juce::MouseEvent mouseWith (juce::Component& c, juce::Point<float> p, int mods, juce::Point<float> down)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | mods),
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, down, now, 1, p != down };
    }

    // aperta em `from`, passa pelo meio, solta em `to` (com Ctrl, Alt ou Shift em `mods`)
    void dragMouse (juce::Component& c, juce::Point<float> from, juce::Point<float> to, int mods = 0)
    {
        c.mouseDown (mouseWith (c, from, mods, from));
        c.mouseDrag (mouseWith (c, (from + to) * 0.5f, mods, from));
        c.mouseDrag (mouseWith (c, to, mods, from));
        c.mouseUp (mouseWith (c, to, mods, from));
    }

    juce::MouseWheelDetails wheel (float notches)
    {
        juce::MouseWheelDetails w;
        w.deltaX = 0.0f;
        w.deltaY = notches * 0.14f;   // um clique da rodinha no Windows
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        return w;
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

    const std::vector<Note>* notesOf (const Project& p, const juce::String& channel)
    {
        const auto* pat = p.currentPatternPtr();
        return pat != nullptr ? pat->notesFor (channel) : nullptr;
    }
}

void runGridSelfTest (SelfTestContext& ctx)
{
    using namespace gridtest;
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    rep.section ("Grade pelo zoom: divisões"_t);
    {
        using namespace timegrid;
        const Meter four { 4, 16 }, three { 4, 12 }, sixEight { 2, 12 };

        rep.check (nests (divisions (four)) && nests (divisions (three)) && nests (divisions (sixEight)),
                   "as divisões se encaixam (cada uma divide a seguinte) em 4/4, 3/4 e 6/8"_t);
        rep.check (divisions (four).front() == 15 && divisions (four).back() == 3840 && divisions (three).back() == 2880
                       && divisions (sixEight).back() == 2880,
                   "da 1/16 de passo (15 ticks) até o compasso, que segue a fórmula"_t);
        rep.check (name (60, four) == "1/4 passo" && name (480, four) == "1/2 tempo" && name (480, sixEight) == "tempo"
                       && name (160, four) == "1/6 tempo" && name (80, four) == "1/3 passo",
                   "nomes: 1/4 passo, 1/2 tempo, tempo (em 6/8), 1/6 tempo, 1/3 passo"_t);
        rep.check (lineTicks (0.1f, four, { 7.0f, 14.0f }) == ticksPerStep && lineTicks (2.0f, four, { 7.0f, 14.0f }) == 15
                       && lineTicks (0.008f, four, { 7.0f, 14.0f }) == 960,
                   "no zoom de sempre a grade fica no passo; bem perto, 1/16 de passo; bem longe, no tempo"_t);
        rep.check (positionLabel (5 * 960 + 2 * 240, four) == "2.2.3" && positionLabel (4 * 960, four) == "2.1"
                       && positionLabel (2880 + 960, three) == "2.2" && positionLabel (2880 + 480, three) == "2.1.3",
                   "régua de perto: tempo 2.1, passo 2.2.3; em 3/4, 2.2 e 2.1.3"_t);
    }

    theme::LookAndFeel laf;   // o visual da casa, só neste pai (nada global)
    juce::Component host;
    host.setLookAndFeel (&laf);
    host.setSize (1400, 700);

    //--------------------------------------------------------------------------
    rep.section ("Grade pelo zoom: piano roll"_t);
    {
        app.run ("project.new");
        const auto bass = channelId (model.get(), "808");
        addNotes (app, bass, { noteVar (0.0, 0.25, 36, false), noteVar (0.0625, 0.0625, 38, false),
                               noteVar (0.25, 0.0156, 41, false), noteVar (1.0, 1.0, 43, true) });

        PianoRoll roll (app);
        host.addAndMakeVisible (roll);
        roll.setBounds (host.getLocalBounds());
        roll.setChannel (bass);
        roll.refresh (model.get());

        auto* grid = childOf<PianoRollGrid> (roll);
        juce::ComboBox* gridBox = nullptr;   // a lista da grade (a primeira lista é a do canal)

        for (auto* c : roll.getChildren())
            if (auto* box = dynamic_cast<juce::ComboBox*> (c); box != nullptr && box->getTooltip().startsWith ("Grade:"))
                gridBox = box;

        rep.check (grid != nullptr && gridBox != nullptr, "piano roll montado sem tela"_t);

        if (grid != nullptr && gridBox != nullptr)
        {
            rep.check (gridBox->getSelectedId() == 1 && grid->getSnapTicks() == ticksPerStep && gridBox->getText() == "Auto: passo",
                       "abre na grade Auto, que no zoom de sempre é o passo, como antes ("_t + gridBox->getText() + ")");
            rep.check (savePng (roll, ctx.work.getChildFile ("grade-piano-passo.png")), "captura no zoom de sempre"_t);

            // a régua fica a partir do teclado (72 px): ancorar ali deixa o começo do padrão na borda
            grid->zoomBy (9.0f, 72.0f);
            rep.check (grid->getSnapTicks() == ticksPerStep / 4 && gridBox->getText() == "Auto: 1/4 passo",
                       "mais perto, a grade e o encaixe vão para 1/4 de passo ("_t + gridBox->getText() + ")");
            rep.check (savePng (roll, ctx.work.getChildFile ("grade-piano-quarto.png")), "captura em 1/4 de passo"_t);

            grid->zoomBy (60.0f, 72.0f);
            rep.check (juce::approximatelyEqual (grid->getPxPerTick(), 2.0f) && grid->getSnapTicks() == 15
                           && gridBox->getText() == "Auto: 1/16 passo",
                       "no zoom máximo (2 px por tick), 1/16 de passo: 15 ticks ("_t + gridBox->getText() + ")");
            rep.check (savePng (roll, ctx.work.getChildFile ("grade-piano-16.png")), "captura em 1/16 de passo"_t);

            // Clique entre as linhas de 1/16: a nota nasce na linha da esquerda (tick 165), um lugar que
            // a grade antiga (no máximo 1/3 de passo) não deixava escolher.
            const auto before = notesOf (model.get(), bass) != nullptr ? notesOf (model.get(), bass)->size() : (size_t) 0;
            click (*grid, { 72.0f + 172.0f * 2.0f, 28.0f + 8.5f * 16.0f });
            const auto* after = notesOf (model.get(), bass);
            int start = -1;

            if (after != nullptr && after->size() == before + 1)
                for (auto& n : *after)
                    if (n.start % ticksPerStep != 0 && n.start != 60)   // 60: a nota de 1/4 de passo do começo
                        start = n.start;

            rep.check (start == 165, "clique em 1/16 de passo põe a nota no tick 165 (veio "_t + juce::String (start) + ")");

            // Com a grade fixa, o encaixe e as linhas ficam nela, mesmo de perto.
            gridBox->setSelectedId (4, juce::sendNotificationSync);
            rep.check (grid->getSnapTicks() == ticksPerStep && gridBox->getText() == "Grade: passo",
                       "grade fixa no passo continua no passo com zoom"_t);
            rep.check (savePng (roll, ctx.work.getChildFile ("grade-piano-fixa.png")), "captura com a grade fixa no passo"_t);

            // Tempo e compasso seguem a fórmula de compasso (antes o compasso era sempre o de 4/4).
            app.run ("transport.setTimeSignature", args ({ { "numerator", 3 }, { "denominator", 4 } }));
            roll.refresh (model.get());
            gridBox->setSelectedId (6, juce::sendNotificationSync);
            const int bar34 = grid->getSnapTicks();
            gridBox->setSelectedId (5, juce::sendNotificationSync);
            const int beat34 = grid->getSnapTicks();
            app.run ("transport.setTimeSignature", args ({ { "numerator", 6 }, { "denominator", 8 } }));
            roll.refresh (model.get());
            const int beat68 = grid->getSnapTicks();
            rep.check (bar34 == 2880 && beat34 == 960 && beat68 == 480,
                       "compasso 3/4 = 2880 ticks, tempo 960; em 6/8 o tempo é a colcheia, 480 (vieram "_t
                           + juce::String (bar34) + ", " + juce::String (beat34) + ", " + juce::String (beat68) + ")");
            app.run ("transport.setTimeSignature", args ({ { "numerator", 4 }, { "denominator", 4 } }));
            roll.refresh (model.get());

            // A rodinha em cima da régua também dá zoom, sem Ctrl.
            gridBox->setSelectedId (1, juce::sendNotificationSync);
            const float px = grid->getPxPerTick();
            grid->mouseWheelMove (mouseAt (*grid, { 400.0f, 10.0f }), wheel (-8.0f));
            rep.check (grid->getPxPerTick() < px * 0.4f && gridBox->getText() != "Auto: 1/16 passo",
                       "rodinha na régua afasta o zoom e a grade engrossa ("_t + gridBox->getText() + ")");
            grid->mouseMove (mouseAt (*grid, { 400.0f, 10.0f }));
            rep.check (grid->getTooltip().contains ("zoom"), "a dica da régua conta da rodinha"_t);

            grid->zoomBy (-200.0f, 72.0f);
            rep.check (grid->getSnapTicks() >= ticksPerBeat, "bem longe, a Auto encaixa no tempo ou no compasso ("_t + gridBox->getText() + ")");
        }

        host.removeChildComponent (&roll);
    }

    //--------------------------------------------------------------------------
    rep.section ("Grade pelo zoom: playlist"_t);
    {
        app.run ("project.new");
        Playlist list (app);
        host.addAndMakeVisible (list);
        list.setBounds (host.getLocalBounds());
        list.refresh (model.get());

        auto* grid = childOf<PlaylistGrid> (list);
        auto* gridBox = childOf<juce::ComboBox> (list);
        rep.check (grid != nullptr && gridBox != nullptr, "playlist montada sem tela"_t);

        if (grid != nullptr && gridBox != nullptr)
        {
            rep.check (gridBox->getSelectedId() == 1 && grid->getSnapTicks() == ticksPerBeat && gridBox->getText() == "Auto: tempo",
                       "a playlist abre na Auto, que no zoom de sempre encaixa no tempo ("_t + gridBox->getText() + ")");

            gridBox->setSelectedId (2, juce::sendNotificationSync);
            rep.check (grid->getSnapTicks() == ticksPerBar4 && gridBox->getText() == "Grade: compasso", "o compasso continua na lista"_t);
            gridBox->setSelectedId (1, juce::sendNotificationSync);

            if (ctx.beatWav.existsAsFile())
                app.run ("playlist.addItem", args ({ { "path", ctx.beatWav.getFullPathName() }, { "track", 2 }, { "start", 0 } }));

            // as raias começam depois de 150 px; ancorar ali deixa o começo da música na borda
            grid->zoomBy (60.0f, 150.0f);
            list.refresh (model.get());
            rep.check (juce::approximatelyEqual (grid->getPxPerTick(), 2.0f) && grid->getSnapTicks() == 15
                           && gridBox->getText() == "Auto: 1/16 passo",
                       "no zoom máximo, 1/16 de passo também na playlist ("_t + gridBox->getText() + ")");

            // na quarta raia, longe do que o projeto novo já traz
            const auto items = model.get().playlistItems.size();
            click (*grid, { 150.0f + 172.0f * 2.0f, 28.0f + 3.0f * 54.0f + 27.0f });
            int start = -1;

            if (model.get().playlistItems.size() == items + 1)
                for (auto& item : model.get().playlistItems)
                    if (item.start % ticksPerStep != 0)
                        start = item.start;

            rep.check (start == 165, "clique em 1/16 de passo põe o padrão no tick 165 (veio "_t + juce::String (start) + ")");
            list.refresh (model.get());   // no app quem faz isto é a janela, a cada mudança do projeto

            // a forma de onda do clipe de áudio carrega em segundo plano
            for (int i = 0; i < 20; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            rep.check (savePng (list, ctx.work.getChildFile ("grade-playlist-16.png")), "captura da playlist em 1/16 de passo"_t);
            grid->mouseMove (mouseAt (*grid, { 400.0f, 10.0f }));
            rep.check (grid->getTooltip().contains ("zoom"), "a dica da régua da playlist conta da rodinha"_t);

            grid->zoomBy (-30.0f, 150.0f);
            rep.check (savePng (list, ctx.work.getChildFile ("grade-playlist-meio.png")), "captura da playlist num zoom médio ("_t + gridBox->getText() + ")");
        }

        host.removeChildComponent (&list);
    }

    //--------------------------------------------------------------------------
    // Pedido do usuário (21/10): esticar e arrastar não era fluido. Cada passo do mouse é um comando;
    // aqui se mede quanto ele custa com o motor remontado na hora (fora de gesto) e agrupado (no gesto),
    // e quanto cada tela leva para se atualizar, que a janela faz a cada mudança.
    rep.section ("Arraste: custo de cada passo"_t);
    {
        if (ctx.demoProject.existsAsFile())
            app.run ("project.open", args ({ { "path", ctx.demoProject.getFullPathName() } }));

        const auto& items = model.get().playlistItems;
        const auto itemId = items.empty() ? juce::String() : items.front().id;
        const int start0 = items.empty() ? 0 : items.front().start;

        auto timeMoves = [&] (bool inGesture, int firstStep)
        {
            std::unique_ptr<CommandRegistry::ScopedGesture> gesture;

            if (inGesture)
                gesture = std::make_unique<CommandRegistry::ScopedGesture> (app.getCommands(), "Mover");

            const auto t0 = juce::Time::getMillisecondCounterHiRes();

            for (int i = 1; i <= 30; ++i)
                app.run ("playlist.updateItem", args ({ { "item", itemId }, { "start", start0 + (firstStep + i) * ticksPerStep } }));

            return (juce::Time::getMillisecondCounterHiRes() - t0) / 30.0;
        };

        const double plain = timeMoves (false, 0);
        const double grouped = timeMoves (true, 30);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (80);   // a remontagem que ficou para depois do gesto

        // o arraste rápido termina no lugar certo, e um Ctrl+Z desfaz o arraste inteiro
        const auto findStart = [&] { for (auto& it : model.get().playlistItems) if (it.id == itemId) return it.start; return -1; };
        const int afterDrag = findStart();
        app.run ("edit.undo");
        const int afterUndo = findStart();
        rep.check (afterDrag == start0 + 60 * ticksPerStep && afterUndo == start0 + 30 * ticksPerStep,
                   "o bloco termina onde o mouse soltou, e um Ctrl+Z volta o arraste inteiro de uma vez"_t);

        // o que cada comando faz além do motor: o desfazer guarda o projeto em JSON antes e depois
        auto timeIt = [] (int n, std::function<void()> f)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < n; ++i) f();
            return (juce::Time::getMillisecondCounterHiRes() - t0) / n;
        };

        const auto json = wnl::toJson (model.get());
        const double tJson = timeIt (20, [&] { (void) wnl::toJson (model.get()); });
        const double tNormalise = timeIt (20, [&] { Project copy = model.get(); wnl::normalise (copy); });
        const double tCopy = timeIt (20, [&] { Project copy = model.get(); (void) copy; });
        const auto tree = juce::JSON::parse (json);
        const double tWriteMulti = timeIt (20, [&] { (void) juce::JSON::toString (tree, juce::JSON::FormatOptions{}
                                                                                    .withSpacing (juce::JSON::Spacing::multiLine)
                                                                                    .withMaxDecimalPlaces (4)); });
        const double tWriteOne = timeIt (20, [&] { (void) juce::JSON::toString (tree, juce::JSON::FormatOptions{}
                                                                                  .withSpacing (juce::JSON::Spacing::none)
                                                                                  .withMaxDecimalPlaces (4)); });
        const double tParse = timeIt (20, [&] { (void) juce::JSON::parse (json); });
        {
            auto timeJson = [&] (const Project& q)
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < 10; ++i) (void) wnl::toJson (q);
                return (juce::Time::getMillisecondCounterHiRes() - t0) / 10.0;
            };

            Project noPatterns = model.get();      noPatterns.patterns.clear();
            Project noMixer = model.get();         noMixer.mixer.clear();
            Project noPlaylist = model.get();      noPlaylist.playlistItems.clear(); noPlaylist.playlistTracks.clear();
            Project noChannels = model.get();      noChannels.channels.clear();
            Project empty;
            rep.check (true, "toJson sem padrões "_t + juce::String (timeJson (noPatterns), 2) + " ms, sem mixer " + juce::String (timeJson (noMixer), 2)
                                 + " ms, sem playlist " + juce::String (timeJson (noPlaylist), 2) + " ms, sem canais " + juce::String (timeJson (noChannels), 2)
                                 + " ms, vazio " + juce::String (timeJson (empty), 2) + " ms; " + juce::String ((int) model.get().channels.size()) + " canais, "
                                 + juce::String ((int) model.get().patterns.size()) + " padrões, " + juce::String ((int) model.get().mixer.size()) + " trilhas");
        }
        rep.check (true, "JSON: escrever em várias linhas "_t + juce::String (tWriteMulti, 2) + " ms, numa linha "
                             + juce::String (tWriteOne, 2) + " ms, ler " + juce::String (tParse, 2) + " ms");

        rep.check (itemId.isNotEmpty() && grouped <= plain,
                   "mover um bloco: "_t + juce::String (plain, 2) + " ms por passo com o motor remontado na hora, "
                       + juce::String (grouped, 2) + " ms no gesto (o motor se remonta a cada 30 ms)");
        rep.check (true, "por passo: JSON do projeto "_t + juce::String (tJson, 2) + " ms (" + juce::String (json.length() / 1024) + " KB, 2 por comando), "
                             + "normalise com cópia " + juce::String (tNormalise, 2) + " ms, cópia " + juce::String (tCopy, 2) + " ms");

        // as telas que a janela atualiza a cada mudança
        auto timeRefresh = [&] (juce::Component& view, std::function<void()> refresh)
        {
            host.addAndMakeVisible (view);
            view.setBounds (host.getLocalBounds());
            refresh();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();

            for (int i = 0; i < 10; ++i)
                refresh();

            const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 10.0;
            host.removeChildComponent (&view);
            return ms;
        };

        Playlist list (app);
        PianoRoll roll (app);
        ChannelRack rack (app);
        MixerView mixerView (app);
        const auto& p = model.get();
        const double tList = timeRefresh (list, [&] { list.refresh (p); });
        const double tRoll = timeRefresh (roll, [&] { roll.refresh (p); });
        const double tRack = timeRefresh (rack, [&] { rack.refresh (p); });
        const double tMixer = timeRefresh (mixerView, [&] { mixerView.refresh (p); });

        rep.check (true, "atualizar as telas: playlist "_t + juce::String (tList, 2) + " ms, piano roll " + juce::String (tRoll, 2)
                             + " ms, channel rack " + juce::String (tRack, 2) + " ms, mixer " + juce::String (tMixer, 2) + " ms");
    }

    //--------------------------------------------------------------------------
    // Onda 2 dos pedidos do usuário (21/10): editar a playlist e o piano roll como no FL.
    rep.section ("Edição estilo FL: cortar, mudo e trecho"_t);
    {
        app.run ("project.new");
        app.run ("playlist.clear");
        const auto pat = model.get().currentPattern;
        const auto* pattern = model.get().findPattern (pat);
        const int patLength = pattern != nullptr ? pattern->lengthTicks() : ticksPerBar4;

        // o deslocamento dentro do padrão: o pedaço da direita toca as mesmas notas que tocavam ali
        const auto kick = channelId (model.get(), "Kick");
        setGrid (app, kick, "x...x...x...x...");
        const auto* k = model.get().findChannel (kick);
        const auto* pt = model.get().findPattern (pat);
        bool sameNotes = k != nullptr && pt != nullptr;

        if (sameNotes)
        {
            const auto whole = Projector::notesFor (model.get(), *pt, *k, 2 * patLength);
            const int cut = patLength + ticksPerBeat;
            const auto right = Projector::notesFor (model.get(), *pt, *k, 2 * patLength - cut, cut);
            std::vector<int> expected, got;

            for (auto& n : whole)
                if (n.start >= cut)
                    expected.push_back (n.start - cut);

            for (auto& n : right)
                got.push_back (n.start);

            sameNotes = ! got.empty() && got == expected;
        }

        rep.check (sameNotes, "um bloco que começa no meio do padrão toca as notas certas"_t);

        // cortar um bloco de padrão
        const auto added = app.run ("playlist.addItem", args ({ { "ref", pat }, { "track", 1 }, { "start", 0 }, { "length", 2 * patLength } }));
        const auto itemId = added.value["item"].toString();
        const auto split = app.run ("playlist.splitItem", args ({ { "item", itemId }, { "at", patLength + ticksPerBeat } }));
        const auto* left = model.get().findItem (itemId);
        const auto* right = model.get().findItem (split.value["right"].toString());
        rep.check (split.ok && left != nullptr && right != nullptr && left->length == patLength + ticksPerBeat
                       && right->start == patLength + ticksPerBeat && right->length == patLength - ticksPerBeat
                       && right->patternOffset == patLength + ticksPerBeat,
                   "cortar um bloco de padrão: o da direita começa no meio do padrão"_t);
        rep.check (! app.run ("playlist.splitItem", args ({ { "item", itemId }, { "at", 0 } })).ok, "cortar fora do bloco é recusado"_t);

        // o deslocamento e o mudo vão e voltam do .wnl
        app.run ("playlist.updateItem", args ({ { "item", itemId }, { "muted", true } }));
        Project back;
        const auto json = wnl::toJson (model.get());
        rep.check (wnl::fromJson (json, back).wasOk() && back.findItem (itemId) != nullptr && back.findItem (itemId)->muted
                       && back.findItem (split.value["right"].toString())->patternOffset == patLength + ticksPerBeat,
                   "o mudo e o deslocamento do bloco vão e voltam do .wnl"_t);

        // cortar um clipe de áudio: o da direita segue o arquivo de onde o primeiro parou
        if (ctx.beatWav.existsAsFile())
        {
            const auto audio = app.run ("playlist.addItem", args ({ { "path", ctx.beatWav.getFullPathName() }, { "track", 2 }, { "start", 0 } }));
            const auto audioId = audio.value["item"].toString();
            const auto cut = app.run ("playlist.splitItem", args ({ { "item", audioId }, { "at", ticksPerBar4 } }));
            const auto* a2 = model.get().findItem (cut.value["right"].toString());
            const double expected = ticksToSeconds (ticksPerBar4, model.get().bpm);
            rep.check (cut.ok && a2 != nullptr && std::abs (a2->offsetSeconds - expected) < 1.0e-5,
                       "cortar áudio: o pedaço da direita começa "_t + juce::String (a2 != nullptr ? a2->offsetSeconds : -1.0, 3)
                           + " s dentro do arquivo (esperado " + juce::String (expected, 3) + ")");
        }

        // mudo: um bloco calado não soa
        app.run ("playlist.clear");
        app.run ("playlist.addItem", args ({ { "ref", pat }, { "track", 1 }, { "start", 0 }, { "length", patLength } }));
        const auto mutedId = model.get().playlistItems.front().id;
        const auto loud = ctx.work.getChildFile ("onda2_com_som.wav"), quiet = ctx.work.getChildFile ("onda2_mudo.wav");
        app.run ("project.exportWav", args ({ { "path", loud.getFullPathName() }, { "what", "song" } }));
        app.run ("playlist.updateItem", args ({ { "item", mutedId }, { "muted", true } }));
        app.run ("project.exportWav", args ({ { "path", quiet.getFullPathName() }, { "what", "song" } }));
        rep.check (peakDb (readWav (loud)) > -30.0f && peakDb (readWav (quiet)) < -90.0f,
                   "bloco mudo não soa (com som "_t + juce::String (peakDb (readWav (loud)), 1) + " dBFS, mudo "
                       + juce::String (peakDb (readWav (quiet)), 1) + " dBFS)");
        app.run ("playlist.updateItem", args ({ { "item", mutedId }, { "muted", false } }));

        // exportar só um trecho: o tempo do arquivo é o do trecho
        app.run ("playlist.addItem", args ({ { "ref", pat }, { "track", 1 }, { "start", patLength }, { "length", 3 * patLength } }));
        const auto partFile = ctx.work.getChildFile ("onda2_trecho.wav");
        const int from = patLength, to = 3 * patLength;
        const auto started = app.run ("project.exportWav", args ({ { "path", partFile.getFullPathName() }, { "what", "song" },
                                                                   { "from", from }, { "to", to }, { "background", true } }));
        juce::var st;

        for (int i = 0; i < 3000 && ! (bool) st["finished"]; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            st = app.run ("export.status").value;
        }

        const auto part = readWav (partFile);
        const double seconds = part.valid ? part.buffer.getNumSamples() / part.sampleRate : 0.0;
        const double expectedSeconds = ticksToSeconds (to - from, model.get().bpm);
        rep.check (started.ok && (bool) st["ok"] && std::abs (seconds - expectedSeconds) < 0.01,
                   "exportar só o trecho de 2 compassos: "_t + juce::String (seconds, 3) + " s (esperado " + juce::String (expectedSeconds, 3) + " s)");
        rep.check (! app.run ("project.exportWav", args ({ { "path", partFile.getFullPathName() }, { "from", 0 }, { "to", 100 } })).ok,
                   "o trecho sem background é recusado com explicação"_t);
        rep.check (app.run ("transport.playFrom", args ({ { "at", ticksPerBar4 } })).ok, "tocar daqui (a ferramenta Y)"_t);
    }

    rep.section ("Edição estilo FL: playlist"_t);
    {
        app.run ("project.new");
        app.run ("playlist.clear");
        const auto pat = model.get().currentPattern;
        app.run ("playlist.addItem", args ({ { "ref", pat }, { "track", 1 }, { "start", 0 }, { "length", ticksPerBar4 } }));
        app.run ("playlist.addItem", args ({ { "ref", pat }, { "track", 2 }, { "start", ticksPerBar4 }, { "length", ticksPerBar4 } }));
        const auto firstId = model.get().playlistItems[0].id;

        Playlist list (app);
        host.addAndMakeVisible (list);
        list.setBounds (host.getLocalBounds());
        list.refresh (model.get());
        auto* grid = childOf<PlaylistGrid> (list);
        rep.check (grid != nullptr, "playlist montada sem tela"_t);

        if (grid != nullptr)
        {
            using KP = juce::KeyPress;
            const int ctrl = juce::ModifierKeys::commandModifier;
            auto items = [&] { return (int) model.get().playlistItems.size(); };
            // na playlist a grade começa depois das raias (150 px), a régua tem 28 px e cada raia 54 px
            const float px = grid->getPxPerTick();
            auto at = [&] (int tick, int track) { return juce::Point<float> (150.0f + (float) tick * px, 28.0f + 54.0f * (float) track + 27.0f); };

            grid->keyPressed (KP ('a', juce::ModifierKeys (ctrl), 0));
            rep.check (grid->getSelection().size() == 2, "Ctrl+A seleciona os 2 blocos"_t);

            grid->keyPressed (KP ('b', juce::ModifierKeys (ctrl), 0));
            bool rightPlace = items() == 4;

            for (auto& i : model.get().playlistItems)
                if (grid->getSelection().count (i.id) != 0)
                    rightPlace = rightPlace && i.start >= 2 * ticksPerBar4;

            rep.check (rightPlace && grid->getSelection().size() == 2, "Ctrl+B repete a seleção logo à direita e seleciona as cópias"_t);
            app.run ("edit.undo");
            rep.check (items() == 2, "um Ctrl+Z desfaz o Ctrl+B inteiro"_t);
            list.refresh (model.get());

            grid->selectAll();
            grid->keyPressed (KP (KP::deleteKey));
            rep.check (items() == 0, "Delete apaga a seleção"_t);
            app.run ("edit.undo");
            list.refresh (model.get());
            rep.check (items() == 2, "e o Ctrl+Z volta os dois"_t);

            // copiar e colar com o mouse em cima da quarta raia
            grid->selectAll();
            grid->keyPressed (KP ('c', juce::ModifierKeys (ctrl), 0));
            grid->mouseMove (mouseAt (*grid, at (4 * ticksPerBar4, 3)));
            grid->keyPressed (KP ('v', juce::ModifierKeys (ctrl), 0));
            int pastedAt = -1;

            for (auto& i : model.get().playlistItems)
                if (grid->getSelection().count (i.id) != 0)
                    pastedAt = pastedAt < 0 ? i.start : juce::jmin (pastedAt, i.start);

            rep.check (items() == 4 && pastedAt == 4 * ticksPerBar4, "Ctrl+C e Ctrl+V colam onde o mouse está"_t);
            app.run ("edit.undo");
            list.refresh (model.get());

            // caixa de seleção com a ferramenta Selecionar: só o primeiro bloco
            grid->setTool (PlaylistGrid::Tool::select);
            dragMouse (*grid, at (ticksPerBar4 + 400, 0) + juce::Point<float> (0.0f, -20.0f), at (ticksPerBar4 - 400, 0));
            rep.check (grid->getSelection().size() == 1 && grid->getSelection().count (firstId) == 1,
                       "a caixa de seleção pega só o bloco que ela cobre"_t);

            // a tesoura: clique no meio do segundo bloco
            grid->setTool (PlaylistGrid::Tool::slice);
            click (*grid, at (ticksPerBar4 + ticksPerBar4 / 2, 1));
            rep.check (items() == 3, "a tesoura corta o bloco em dois"_t);
            app.run ("edit.undo");
            list.refresh (model.get());

            // mudo: um clique cala o bloco
            grid->setTool (PlaylistGrid::Tool::mute);
            click (*grid, at (ticksPerBar4 / 2, 0));
            const auto* first = model.get().findItem (firstId);
            rep.check (first != nullptr && first->muted, "a ferramenta Mudo cala o bloco"_t);
            list.refresh (model.get());

            // seleção de tempo: Ctrl+arrastar na régua, de 1 a 3
            grid->setTool (PlaylistGrid::Tool::draw);
            dragMouse (*grid, { 150.0f, 14.0f }, { 150.0f + 2.0f * ticksPerBar4 * px, 14.0f }, ctrl);
            const auto range = grid->getTimeSelection();
            rep.check (range.getStart() == 0 && range.getEnd() == 2 * ticksPerBar4 && app.getTimeSelection() == range,
                       "Ctrl+arrastar na régua seleciona o trecho (de "_t + juce::String (range.getStart()) + " a " + juce::String (range.getEnd()) + ")");
            rep.check (savePng (list, ctx.work.getChildFile ("onda2-playlist.png")), "captura da playlist com seleção, mudo e o trecho"_t);
            grid->setTimeSelection ({});
            grid->setTool (PlaylistGrid::Tool::draw);
        }

        host.removeChildComponent (&list);
    }

    rep.section ("Edição estilo FL: piano roll"_t);
    {
        app.run ("project.new");
        const auto bass = channelId (model.get(), "808");
        addNotes (app, bass, { noteVar (0.0, 0.5, 36, false), noteVar (1.1, 0.5, 38, false) });   // a segunda fora da grade

        PianoRoll roll (app);
        host.addAndMakeVisible (roll);
        roll.setBounds (host.getLocalBounds());
        roll.setChannel (bass);
        roll.refresh (model.get());
        auto* grid = childOf<PianoRollGrid> (roll);
        rep.check (grid != nullptr, "piano roll montado sem tela"_t);

        auto notes = [&] { const auto* list = notesOf (model.get(), bass); return list != nullptr ? *list : std::vector<Note>(); };

        if (grid != nullptr)
        {
            using KP = juce::KeyPress;
            const int ctrl = juce::ModifierKeys::commandModifier;

            grid->keyPressed (KP ('a', juce::ModifierKeys (ctrl), 0));
            grid->keyPressed (KP ('q', juce::ModifierKeys (juce::ModifierKeys::altModifier), 0));
            bool quantized = false;

            for (auto& n : notes())
                quantized = quantized || n.start == 4 * ticksPerStep;   // 1,1 tempo (1056 ticks) foi para o passo 5

            rep.check (quantized, "Alt+Q leva a nota fora da grade para o passo mais perto"_t);
            roll.refresh (model.get());

            grid->keyPressed (KP ('a', juce::ModifierKeys (ctrl), 0));
            grid->keyPressed (KP ('b', juce::ModifierKeys (ctrl), 0));
            const auto afterDuplicate = notes();
            bool copied = afterDuplicate.size() == 4;

            for (auto& n : afterDuplicate)
                copied = copied && (n.start == 0 || n.start == 4 * ticksPerStep || n.start == 2 * ticksPerBeat || n.start == 2 * ticksPerBeat + 4 * ticksPerStep);

            rep.check (copied, "Ctrl+B repete as notas logo à direita, arredondado no tempo"_t);
            app.run ("edit.undo");
            roll.refresh (model.get());

            // a tesoura no meio da primeira nota (0 a 480)
            grid->setTool (PianoRollGrid::Tool::slice);
            click (*grid, grid->pointAt (ticksPerStep, 36));
            const auto afterSlice = notes();
            bool sliced = afterSlice.size() == 3;

            for (auto& n : afterSlice)
                if (n.key == 36)
                    sliced = sliced && n.length == ticksPerStep;

            rep.check (sliced, "a tesoura divide a nota em duas"_t);
            app.run ("edit.undo");
            roll.refresh (model.get());

            // a alça de esticar: a seleção inteira fica com o dobro do tamanho
            grid->setTool (PianoRollGrid::Tool::select);
            grid->keyPressed (KP ('a', juce::ModifierKeys (ctrl), 0));
            int end = 0;

            for (auto& n : notes())
                end = juce::jmax (end, n.end());

            const auto handle = juce::Point<float> (grid->pointAt (end, 60).x, 21.0f);
            dragMouse (*grid, handle, { grid->pointAt (2 * end, 60).x, 21.0f });
            bool doubled = true;

            for (auto& n : notes())
                doubled = doubled && n.length == 2 * ticksPerBeat / 2 * 1;   // 0,5 tempo virou 1 tempo

            rep.check (doubled, "a alça estica a seleção: as notas dobram de tamanho"_t);
            app.run ("edit.undo");
            rep.check (savePng (roll, ctx.work.getChildFile ("onda2-pianoroll.png")), "captura do piano roll com a tesoura"_t);
        }

        host.removeChildComponent (&roll);
    }

    host.setLookAndFeel (nullptr);
    app.run ("project.new");
}

} // namespace whynot::selftest
