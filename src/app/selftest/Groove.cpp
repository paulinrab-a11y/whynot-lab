// Autoteste do swing e do começo do play no PAT (pedidos dos produtores, 29/10). Dono: Swing e início do play.
// - swing global (project.setSwing): o render atrasa o contratempo o número certo de amostras e soma ao do canal;
// - groove das notas (channel.setGroove): o swing das notas do piano roll atrasa no render, e o modelo fica na grade;
// - humanizar: dentro dos limites, sempre o mesmo sorteio (dois renders iguais amostra por amostra);
// - .wnl: campos opcionais que vão e voltam, e um projeto sem swing não ganha campo nenhum;
// - começo do play no PAT: play e stop no ponto, o loop no padrão inteiro, outro padrão volta ao começo;
// - telas: o knob do rack, a faixa dos passos, a régua e o menu Groove do piano roll, com as capturas.

#include "SelfTestKit.h"
#include "../../engine/Projector.h"
#include "../../model/WnlFormat.h"
#include "../../ui/ChannelRack.h"
#include "../../ui/PianoRoll.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;

    juce::File writeClick (const juce::File& folder)
    {
        // um clique curto de 1 kHz: o ataque dele é fácil de achar no render
        juce::AudioBuffer<float> b (1, (int) (0.005 * rate));

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / rate));

        const auto f = folder.getChildFile ("groove-clique.wav");
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
        juce::WavAudioFormat wav;

        if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());

        return f;
    }

    int onset (const Audio& a, double from)
    {
        for (int i = juce::jmax (0, (int) (from * a.sampleRate)); i < a.buffer.getNumSamples(); ++i)
            if (std::abs (a.buffer.getSample (0, i)) > 0.01f)
                return i;

        return -1;
    }

    /** O atraso, em amostras a 48 kHz e 140 BPM, de um swing em % (o que o motor faz: ticks inteiros). */
    int expectedDelay (float percent)
    {
        const int ticks = Channel::swingTicksFor (percent);
        return juce::roundToInt (ticks / (double) ticksPerBeat * 60.0 / 140.0 * rate);
    }

    double transportSeconds (AppController& app)   { return app.getEdit().getTransport().getPosition().inSeconds(); }

    bool savePngOnPanel (juce::Component& c, const juce::File& file)
    {
        juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()), juce::jmax (1, c.getHeight()), true);
        {
            juce::Graphics g (image);
            g.fillAll (theme::colour::panel);
            g.drawImageAt (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), 0, 0);
        }
        file.deleteFile();

        if (juce::FileOutputStream out (file); out.openedOk())
        {
            juce::PNGImageFormat png;
            return png.writeImageToStream (image, out) && image.getWidth() > 0;
        }

        return false;
    }

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, p, now, 1, false };
    }
}

void runGrooveSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    rep.section ("Swing e começo do play no PAT (29/10)"_t);

    app.run ("project.new");
    rep.check (! wnl::toJson (model.get()).contains ("\"swing\"") && ! wnl::toJson (model.get()).contains ("\"noteSwing\"")
                   && ! wnl::toJson (model.get()).contains ("\"humanize\""),
               "projeto novo: nenhum campo de swing no .wnl (os campos são opcionais)"_t);

    int renders = 0;
    auto renderPattern = [&]
    {
        const auto f = ctx.work.getChildFile ("groove_" + juce::String (++renders) + ".wav");
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        return readWav (f);
    };

    const auto click = app.run ("channel.addSampler", args ({ { "path", writeClick (ctx.work).getFullPathName() }, { "name", "Clique" },
                                                             { "detectRoot", false } }));
    const auto clickId = click.value["channel"].toString();
    app.run ("mixer.setSolo", args ({ { "track", click.value["mixerTrack"].toString() }, { "on", true } }));
    setGrid (app, clickId, "xx..............");
    const double step = beat140 / 4.0;

    //--------------------------------------------------------------------------
    // swing global: o render
    const auto straight = renderPattern();
    rep.check (app.run ("project.setSwing", args ({ { "amount", 50 } })).ok && model.get().swing == 50.0f, "project.setSwing 50"_t);
    const auto swung = renderPattern();
    const int delay = onset (swung, step - 0.004) - onset (straight, step - 0.004);
    rep.check (straight.valid && swung.valid && std::abs (delay - expectedDelay (50.0f)) <= 2,
               "swing global de 50%: o contratempo atrasa "_t + juce::String (delay) + " amostras (esperado " + juce::String (expectedDelay (50.0f))
                   + ", 40 ticks a 140 BPM)");
    rep.check (onset (straight, 0.0) == onset (swung, 0.0) && onset (straight, 0.0) >= 0, "o passo do tempo (par) não sai do lugar"_t);

    // soma com o swing do canal, até 100 %
    app.run ("channel.setSwing", args ({ { "channel", clickId }, { "amount", 25 } }));
    const auto summed = renderPattern();
    const int delaySum = onset (summed, step - 0.004) - onset (straight, step - 0.004);
    rep.check (std::abs (delaySum - expectedDelay (75.0f)) <= 2,
               "global 50% + canal 25%: atrasa "_t + juce::String (delaySum) + " amostras (esperado " + juce::String (expectedDelay (75.0f)) + ")");
    app.run ("project.setSwing", args ({ { "amount", 90 } }));
    rep.check (model.get().stepSwingTicks (*model.get().findChannel (clickId)) == Channel::swingTicksFor (100.0f),
               "90% + 25% para em 100% (80 ticks, a sensação de tercina)"_t);
    rep.check (app.getCommands().undo() && model.get().swing == 50.0f, "Ctrl+Z volta o swing global para 50%"_t);
    app.run ("channel.setSwing", args ({ { "channel", clickId }, { "amount", 0 } }));

    //--------------------------------------------------------------------------
    // groove das notas do piano roll: só ao tocar
    app.run ("project.setSwing", args ({ { "amount", 0 } }));
    app.run ("steps.clear", args ({ { "channel", clickId } }));
    addNotes (app, clickId, { noteVar (0.0, 0.1, 60, false), noteVar (0.25, 0.1, 60, false), noteVar (0.5, 0.1, 60, false) });
    const auto notesStraight = renderPattern();
    rep.check (app.run ("channel.setGroove", args ({ { "channel", clickId }, { "swing", 50 } })).ok
                   && model.get().findChannel (clickId)->noteSwing == 50.0f,
               "channel.setGroove: swing 50% nas notas"_t);
    const auto notesSwung = renderPattern();
    const int noteDelay = onset (notesSwung, step - 0.004) - onset (notesStraight, step - 0.004);
    rep.check (std::abs (noteDelay - expectedDelay (50.0f)) <= 2,
               "swing das notas: a nota no contratempo atrasa "_t + juce::String (noteDelay) + " amostras (esperado "
                   + juce::String (expectedDelay (50.0f)) + ")");
    rep.check (onset (notesSwung, 2.0 * step - 0.004) == onset (notesStraight, 2.0 * step - 0.004),
               "a nota da colcheia seguinte não sai do lugar"_t);

    {
        const auto* pat = model.get().currentPatternPtr();
        const auto* list = pat != nullptr ? pat->notesFor (clickId) : nullptr;
        rep.check (list != nullptr && list->size() == 3 && (*list)[1].start == ticksPerStep,
                   "no modelo a nota continua na grade (tick 240): o swing é só na hora de tocar"_t);
    }

    rep.check (Projector::swingWarp (0, 80) == 0 && Projector::swingWarp (240, 80) == 320 && Projector::swingWarp (480, 80) == 480
                   && Projector::swingWarp (120, 80) == 160 && Projector::swingWarp (720, 80) == 800 && Projector::swingWarp (360, 0) == 360,
               "a conta do swing das notas: o começo e o fim da colcheia ficam, o meio atrasa, entre eles em linha reta"_t);
    rep.check (! app.run ("channel.setGroove", args ({ { "channel", clickId } })).ok
                   && ! app.run ("channel.setGroove", args ({ { "channel", "ch_nao_existe" }, { "swing", 10 } })).ok,
               "channel.setGroove sem valor ou com canal errado: recusado"_t);

    //--------------------------------------------------------------------------
    // humanizar: limites e o mesmo sorteio
    {
        app.run ("channel.setGroove", args ({ { "channel", clickId }, { "swing", 0 } }));
        app.run ("pianoroll.clear", args ({ { "channel", clickId } }));
        setGrid (app, clickId, "x.x.x.x.x.x.x.x.");
        const auto plain = Projector::notesFor (model.get(), *model.get().currentPatternPtr(), *model.get().findChannel (clickId), 8 * ticksPerBar4);
        app.run ("channel.setGroove", args ({ { "channel", clickId }, { "humanize", 100 } }));
        const auto& p = model.get();
        const auto a = Projector::notesFor (p, *p.currentPatternPtr(), *p.findChannel (clickId), 8 * ticksPerBar4);
        const auto b = Projector::notesFor (p, *p.currentPatternPtr(), *p.findChannel (clickId), 8 * ticksPerBar4);

        bool inside = a.size() == plain.size(), moved = false, same = a.size() == b.size(), loopsDiffer = false;

        for (size_t i = 0; inside && i < a.size(); ++i)
        {
            inside = inside && std::abs (a[i].start - plain[i].start) <= 40 && std::abs (a[i].velocity - plain[i].velocity) <= 20;
            moved = moved || a[i].start != plain[i].start || a[i].velocity != plain[i].velocity;
            same = same && a[i].start == b[i].start && a[i].velocity == b[i].velocity;
        }

        for (size_t i = 0; i + 8 < a.size(); ++i)   // a mesma nota na volta seguinte (8 passos por volta) sorteia de novo
            loopsDiffer = loopsDiffer || (a[i + 8].start - a[i].start != ticksPerBar4);

        rep.check (inside && moved, "humanizar 100%: cada nota anda até 40 ticks e a força muda até 20 (e alguma mudou)"_t);
        rep.check (same, "humanizar: o mesmo sorteio nas duas contas"_t);
        rep.check (loopsDiffer && Projector::humanizes (p), "humanizar: cada volta do padrão sorteia de novo"_t);

        const auto r1 = renderPattern(), r2 = renderPattern();
        rep.check (r1.valid && r2.valid && maxDiff (r1, r2) == 0.0f, "humanizar: dois renders iguais amostra por amostra"_t);
    }

    //--------------------------------------------------------------------------
    // .wnl: vão e voltam
    {
        app.run ("project.setSwing", args ({ { "amount", 33.3 } }));
        app.run ("channel.setGroove", args ({ { "channel", clickId }, { "swing", 66 }, { "humanize", 15 } }));
        const auto json = wnl::toJson (model.get());
        Project back;
        const bool read = wnl::fromJson (json, back).wasOk();
        const auto* ch = back.findChannel (clickId);
        rep.check (read && back.swing == 33.3f && ch != nullptr && ch->noteSwing == 66.0f && ch->humanize == 15.0f,
                   "o swing global, o swing das notas e o humanizar vão e voltam do .wnl"_t);
        app.run ("project.setSwing", args ({ { "amount", 0 } }));
        app.run ("channel.setGroove", args ({ { "channel", clickId }, { "swing", 0 }, { "humanize", 0 } }));
    }

    //--------------------------------------------------------------------------
    // começo do play no PAT
    {
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        const auto set = app.run ("transport.setPatternStart", args ({ { "at", 4 }, { "unit", "steps" } }));
        const double startSeconds = ticksToSeconds (4 * ticksPerStep, model.get().bpm);
        rep.check (set.ok && (int) set.value["at"] == 4 * ticksPerStep && app.patternStart() == 4 * ticksPerStep,
                   "transport.setPatternStart no passo 5 (tick 960)"_t);
        rep.check (std::abs (transportSeconds (app) - startSeconds) < 1.0e-6, "parado, o transporte já fica no ponto"_t);

        app.play();
        rep.check (std::abs (transportSeconds (app) - startSeconds) < 1.0e-6,
                   "o play do PAT começa do ponto escolhido ("_t + juce::String (transportSeconds (app), 4) + " s)");
        const auto loop = app.getEdit().getTransport().getLoopRange();
        rep.check (loop.getStart().inSeconds() == 0.0 && std::abs (loop.getEnd().inSeconds() - app.getProjector().lengthSeconds()) < 1.0e-6,
                   "o loop continua no padrão inteiro (do 0 ao fim)"_t);
        app.stop();
        rep.check (std::abs (transportSeconds (app) - startSeconds) < 1.0e-6, "o stop volta para o ponto, como na playlist"_t);

        app.run ("transport.setPatternStart", args ({ { "at", 99 }, { "unit", "bars" } }));
        rep.check (app.patternStart() == model.get().currentPatternPtr()->lengthTicks() - 1, "um ponto depois do fim fica dentro do padrão"_t);
        app.run ("transport.setPatternStart", args ({ { "at", 0 } }));
        rep.check (app.patternStart() == 0, "0 volta ao começo"_t);

        app.run ("transport.setPatternStart", args ({ { "at", 1 }, { "unit", "beats" } }));
        const auto first = model.get().currentPattern;
        app.run ("pattern.add", args ({ { "name", "Outro" } }));
        app.run ("pattern.select", args ({ { "pattern", model.get().patterns.back().id } }));
        rep.check (model.get().currentPattern != first && app.patternStart() == 0, "outro padrão: o play volta a começar do começo"_t);
        app.run ("pattern.select", args ({ { "pattern", first } }));
        rep.check (app.patternStart() == ticksPerBeat, "de volta ao primeiro, o ponto dele continua"_t);

        // no SONG, quem manda é a agulha
        app.run ("transport.setSongMode", args ({ { "on", true } }));
        app.run ("transport.setPosition", args ({ { "at", 0 } }));
        app.stop();
        rep.check (std::abs (transportSeconds (app)) < 1.0e-6, "no SONG o stop volta para a agulha, não para o ponto do padrão"_t);
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        app.stop();
    }

    //--------------------------------------------------------------------------
    // telas: o knob do rack, a faixa dos passos, a régua e o menu Groove
    {
        theme::LookAndFeel laf;   // o visual da casa, só neste pai (nada global)
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1400, 640);

        setGrid (app, clickId, "x.x.x.x.x.x.x.x.");
        app.run ("project.setSwing", args ({ { "amount", 50 } }));
        app.run ("transport.setPatternStart", args ({ { "at", 8 }, { "unit", "steps" } }));

        {
            ChannelRack rack (app);
            rack.setSize (1180, 300);
            host.addAndMakeVisible (rack);
            rack.refresh (model.get());
            rack.setPatternStart (app.patternStart());
            rep.check (rack.getSwingKnob().getValue() == 50.0 && rack.getSwingKnob().isVisible(), "o knob de swing no topo do rack mostra 50%"_t);
            rep.check (savePngOnPanel (rack, ctx.work.getChildFile ("swing-rack.png")), "captura do rack com o swing e o começo no passo 9"_t);

            rack.getSwingKnob().setValue (66.0, juce::sendNotificationSync);
            rep.check (model.get().swing == 66.0f, "girar o knob muda o swing global (66%)"_t);

            rack.mouseDown (mouseAt (rack, rack.stepRulerPoint (12).toFloat()));
            rack.mouseUp (mouseAt (rack, rack.stepRulerPoint (12).toFloat()));
            rep.check (app.patternStart() == 12 * ticksPerStep, "clique sobre o passo 13 no rack: o padrão começa dali ("_t
                                                                    + juce::String (app.patternStart()) + ")");
            host.removeChildComponent (&rack);
        }

        {
            app.run ("project.setSwing", args ({ { "amount", 0 } }));   // as notas nascem na grade; o swing delas vem do Groove
            app.run ("pianoroll.stepsToNotes", args ({ { "channel", clickId } }));
            PianoRoll roll (app);
            host.addAndMakeVisible (roll);
            roll.setBounds (host.getLocalBounds());
            roll.setChannel (clickId);
            roll.refresh (model.get());
            auto& grid = roll.getGrid();

            rep.check (roll.makeGrooveMenu().getNumItems() == 12, "o menu Groove: 2 títulos, 6 swings e 4 humanizar"_t);
            roll.chooseGroove (102);
            roll.chooseGroove (201);
            roll.refresh (model.get());
            rep.check (model.get().findChannel (clickId)->noteSwing == 50.0f && model.get().findChannel (clickId)->humanize == 15.0f
                           && roll.getGrooveButton().getToggleState(),
                       "escolher no menu: swing 50% e humanizar leve, e o botão acende"_t);

            grid.mouseDown (mouseAt (grid, grid.rulerPoint (2 * ticksPerBeat)));
            grid.mouseUp (mouseAt (grid, grid.rulerPoint (2 * ticksPerBeat)));
            rep.check (app.patternStart() == 2 * ticksPerBeat, "clique na régua do piano roll: o padrão começa no tempo 3 ("_t
                                                                   + juce::String (app.patternStart()) + ")");
            grid.setPatternStart (app.patternStart());
            rep.check (savePngOnPanel (roll, ctx.work.getChildFile ("swing-pianoroll.png")), "captura do piano roll com o Groove e a marca na régua"_t);

            // a barra num notebook de 1366 com o navegador fechado: nenhum controle da barra fica por cima de outro
            // (o "Swing 50%" com o nome empurrava o tom para baixo do Editar)
            for (const int width : { 1240, 1400 })
            {
                roll.setSize (width, 640);
                juce::Array<juce::Rectangle<int>> boxes;

                int narrowestBox = 1000;   // as caixas (canal, grade, tom, escala): o tom não pode ficar espremido

                for (auto* child : roll.getChildren())
                    if (child->isVisible() && child->getY() < 44 && child->getHeight() < 44 && ! child->getBounds().isEmpty())
                    {
                        boxes.add (child->getBounds());

                        if (dynamic_cast<juce::ComboBox*> (child) != nullptr)
                            narrowestBox = juce::jmin (narrowestBox, child->getWidth());
                    }

                bool apart = boxes.size() >= 8 && narrowestBox >= 68;

                for (int i = 0; i < boxes.size(); ++i)
                    for (int j = i + 1; j < boxes.size(); ++j)
                        apart = apart && ! boxes[i].intersects (boxes[j]);

                for (auto& b : boxes)
                    apart = apart && b.getX() >= 0 && b.getRight() <= width;

                rep.check (apart, "a "_t + juce::String (width) + " px os " + juce::String (boxes.size())
                                      + " controles da barra não se sobrepõem, com o Groove aceso");
            }

            roll.setSize (1240, 640);
            rep.check (savePngOnPanel (roll, ctx.work.getChildFile ("swing-pianoroll-1240.png")), "captura da barra do piano roll a 1240 px"_t);
            host.removeChildComponent (&roll);
        }

        host.setLookAndFeel (nullptr);
    }

    app.run ("project.new");
}

} // namespace whynot::selftest
