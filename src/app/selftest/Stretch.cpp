// Autoteste do esticar o áudio no tempo (pedidos de 21/10, item 2.3/3.4): a duração exportada muda, o tom
// fica (Hz medido antes e depois), a fita muda o tom junto, Ctrl+Z desfaz, salvar e reabrir mantém, e o
// gesto na playlist (segurar A na borda, a borda normal além do fim do arquivo). Dono: frente Esticar áudio.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"
#include "../../ui/Playlist.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    /** Um tom puro em estéreo, com rampas de 10 ms nas pontas (sem estalo). */
    bool writeTone (const juce::File& f, double hz, double seconds, double rate)
    {
        const int n = (int) (seconds * rate), ramp = (int) (0.01 * rate);
        juce::AudioBuffer<float> b (2, n);

        for (int i = 0; i < n; ++i)
        {
            const double env = juce::jmin (1.0, juce::jmin (i, n - 1 - i) / (double) ramp);
            const auto v = (float) (0.5 * env * std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }

        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, n);
    }

    template <typename T>
    T* childOf (juce::Component& parent)
    {
        for (auto* c : parent.getChildren())
            if (auto* t = dynamic_cast<T*> (c))
                return t;

        return nullptr;
    }

    juce::MouseEvent mouseWith (juce::Component& c, juce::Point<float> p, juce::Point<float> down)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, down, now, 1, p != down };
    }

    // aperta em `from`, passa pelo meio e solta em `to`
    void dragMouse (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
    {
        c.mouseDown (mouseWith (c, from, from));
        c.mouseDrag (mouseWith (c, (from + to) * 0.5f, from));
        c.mouseDrag (mouseWith (c, to, from));
        c.mouseUp (mouseWith (c, to, from));
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

    double secondsOf (const Audio& a)   { return a.valid && a.sampleRate > 0 ? a.buffer.getNumSamples() / a.sampleRate : 0.0; }
    juce::String fmt (double v, int decimals = 3)   { return juce::String (v, decimals); }
}

void runStretchSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    const auto tone = ctx.work.getChildFile ("esticar_440.wav");
    constexpr double toneHz = 440.0, toneSeconds = 2.0;
    const bool toneOk = writeTone (tone, toneHz, toneSeconds, 48000.0);

    auto exportSong = [&] (const char* name)
    {
        const auto file = ctx.work.getChildFile (name);
        app.run ("project.exportWav", args ({ { "path", file.getFullPathName() }, { "what", "song" } }));
        return readWav (file);
    };

    // o Ctrl+Z de verdade: a ação que o atalho dispara (no preset "fl", desfazer passo a passo)
    const auto undoAction = app.getKeyMap().actionFor (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
    auto ctrlZ = [&] { return app.run (undoAction).ok; };

    //--------------------------------------------------------------------------
    rep.section ("Esticar áudio: duração, tom e fita"_t);
    {
        app.run ("project.new");
        app.run ("playlist.clear");
        rep.check (toneOk, "tom de teste de 440 Hz e 2 s gravado"_t);

        const auto added = app.run ("playlist.addItem", args ({ { "path", tone.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
        const auto id = added.value["item"].toString();
        const auto* item = model.get().findItem (id);
        const int baseLength = item != nullptr ? item->length : 0;
        const int fileTicks = juce::roundToInt (secondsToTicks (toneSeconds, model.get().bpm));
        rep.check (added.ok && item != nullptr && item->stretch == 1.0 && std::abs (baseLength - fileTicks) <= 1,
                   "o clipe novo entra sem esticar, com o tamanho do arquivo ("_t + juce::String (baseLength) + " ticks)");

        const auto base = exportSong ("esticar_1x.wav");
        const double hzBase = measureHz (base, 0.2, 1.8);
        rep.check (std::abs (secondsOf (base) - toneSeconds) < 0.01 && std::abs (hzBase - toneHz) < toneHz * 0.005,
                   "sem esticar: "_t + fmt (secondsOf (base)) + " s e " + fmt (hzBase, 1) + " Hz");

        // esticar 1,5 vezes pelo comando: a duração vai junto e o corte do começo fica
        const auto stretched = app.run ("playlist.stretchItem", args ({ { "item", id }, { "stretch", 1.5 } }));
        item = model.get().findItem (id);
        rep.check (stretched.ok && item != nullptr && item->stretch == 1.5 && item->length == juce::roundToInt (baseLength * 1.5)
                       && item->start == 0 && item->offsetSeconds == 0.0,
                   "playlist.stretchItem 1,5: o bloco passa a "_t + juce::String (item != nullptr ? item->length : -1) + " ticks");

        const auto slow = exportSong ("esticar_1_5x.wav");
        const double expected = ticksToSeconds (juce::roundToInt (baseLength * 1.5), model.get().bpm);
        const double hzSlow = measureHz (slow, 0.3, 2.7);
        rep.check (std::abs (secondsOf (slow) - expected) < 0.01,
                   "a duração exportada muda: "_t + fmt (secondsOf (base)) + " s viram " + fmt (secondsOf (slow)) + " s (esperado "
                       + fmt (expected) + " s)");
        rep.check (peakIn (slow, 2.6, 2.9) > 0.1f,
                   "o som vai até o fim do bloco esticado (pico de "_t + fmt (peakIn (slow, 2.6, 2.9)) + " entre 2,6 e 2,9 s; o arquivo só tem 2 s)");
        rep.check (hzBase > 0.0 && std::abs (hzSlow / hzBase - 1.0) < 0.01,
                   "o tom fica igual: "_t + fmt (hzBase, 1) + " Hz antes, " + fmt (hzSlow, 1) + " Hz esticado");

        // a fita: o mesmo esticar, com o tom caindo junto (1,5 vezes mais lento = 1,5 vezes mais grave)
        app.run ("playlist.updateItem", args ({ { "item", id }, { "tape", true } }));
        const auto tape = exportSong ("esticar_fita.wav");
        const double hzTape = measureHz (tape, 0.3, 2.7);
        rep.check (std::abs (secondsOf (tape) - expected) < 0.01 && std::abs (hzTape / (hzBase / 1.5) - 1.0) < 0.015,
                   "a fita muda o tom junto: "_t + fmt (hzTape, 1) + " Hz (esperado " + fmt (hzBase / 1.5, 1) + ")");

        // Ctrl+Z desfaz a fita e depois o esticar
        rep.check (undoAction == "edit.undo", "o Ctrl+Z dispara o desfazer passo a passo ("_t + undoAction + ")");
        const bool undoTape = ctrlZ() && model.get().findItem (id) != nullptr && ! model.get().findItem (id)->tape;
        const bool undoStretch = ctrlZ() && model.get().findItem (id) != nullptr && model.get().findItem (id)->stretch == 1.0
                                 && model.get().findItem (id)->length == baseLength;
        rep.check (undoTape && undoStretch, "Ctrl+Z desfaz a fita e depois o esticar, de volta aos "_t + juce::String (baseLength) + " ticks");

        // limites e recusas
        app.run ("playlist.stretchItem", args ({ { "item", id }, { "stretch", 10.0 } }));
        item = model.get().findItem (id);
        rep.check (item != nullptr && item->stretch == maxStretch && item->length == baseLength * 4,
                   "esticar além de 4 vezes para em 4"_t);
        app.run ("edit.undo");
        app.run ("playlist.updateItem", args ({ { "item", id }, { "stretch", 2.0 } }));
        app.run ("playlist.updateItem", args ({ { "item", id }, { "stretch", -2.0 } }));
        item = model.get().findItem (id);
        rep.check (item != nullptr && item->stretch == 1.0 && item->length == baseLength, "stretch inválido volta para 1"_t);

        const auto pattern = app.run ("playlist.addItem", args ({ { "ref", model.get().currentPattern }, { "track", 3 }, { "start", 0 } }));
        rep.check (! app.run ("playlist.stretchItem", args ({ { "item", pattern.value["item"].toString() }, { "stretch", 2.0 } })).ok
                       && ! app.run ("playlist.stretchItem", args ({ { "item", id } })).ok
                       && ! app.run ("playlist.stretchItem", args ({ { "item", id }, { "stretch", 2.0 }, { "edge", "meio" } })).ok,
                   "recusa: bloco de padrão, sem o quanto e borda que não existe"_t);
        app.run ("playlist.removeItem", args ({ { "item", pattern.value["item"].toString() } }));

        // pela borda esquerda o fim fica parado
        app.run ("playlist.updateItem", args ({ { "item", id }, { "start", 2 * ticksPerBar4 } }));
        const int end = 2 * ticksPerBar4 + baseLength;
        app.run ("playlist.stretchItem", args ({ { "item", id }, { "stretch", 1.25 }, { "edge", "left" } }));
        item = model.get().findItem (id);
        rep.check (item != nullptr && item->end() == end && item->length == juce::roundToInt (baseLength * 1.25) && item->stretch == 1.25,
                   "esticar pela borda esquerda: o fim fica parado em "_t + juce::String (end));
        app.run ("edit.undo");
        app.run ("edit.undo");

        // cortar um esticado: o pedaço da direita anda no arquivo metade do tempo da playlist
        app.run ("playlist.stretchItem", args ({ { "item", id }, { "stretch", 2.0 } }));
        const auto cut = app.run ("playlist.splitItem", args ({ { "item", id }, { "at", ticksPerBar4 } }));
        const auto* right = model.get().findItem (cut.value["right"].toString());
        const double rightOffset = ticksToSeconds (ticksPerBar4, model.get().bpm) / 2.0;
        rep.check (cut.ok && right != nullptr && right->stretch == 2.0 && std::abs (right->offsetSeconds - rightOffset) < 1.0e-5,
                   "cortar um esticado: o da direita fica esticado e começa "_t + fmt (right != nullptr ? right->offsetSeconds : -1.0)
                       + " s dentro do arquivo (esperado " + fmt (rightOffset) + ")");
        app.run ("edit.undo");
        app.run ("edit.undo");

        // salvar e reabrir mantém o esticar e a fita; um bloco sem esticar não escreve o campo
        rep.check (! wnl::toJson (model.get()).contains ("\"stretch\""), "bloco sem esticar não escreve stretch no .wnl"_t);
        app.run ("playlist.stretchItem", args ({ { "item", id }, { "stretch", 1.5 } }));
        app.run ("playlist.updateItem", args ({ { "item", id }, { "tape", true } }));
        const auto saved = ctx.work.getChildFile ("esticar.wnl");
        const bool saveOk = app.run ("project.save", args ({ { "path", saved.getFullPathName() } })).ok;
        app.run ("project.new");
        const bool openOk = app.run ("project.open", args ({ { "path", saved.getFullPathName() } })).ok;
        const auto* reopened = model.get().findItem (id);
        rep.check (saveOk && openOk && reopened != nullptr && reopened->stretch == 1.5 && reopened->tape
                       && reopened->length == juce::roundToInt (baseLength * 1.5),
                   "salvar e reabrir mantém o esticar (×1,50) e a fita"_t);

        app.run ("playlist.updateItem", args ({ { "item", id }, { "tape", false } }));
        const auto again = exportSong ("esticar_reaberto.wav");
        rep.check (std::abs (secondsOf (again) - expected) < 0.01 && std::abs (measureHz (again, 0.3, 2.7) / hzBase - 1.0) < 0.01,
                   "o projeto reaberto exporta esticado: "_t + fmt (secondsOf (again)) + " s e " + fmt (measureHz (again, 0.3, 2.7), 1) + " Hz");
    }

    //--------------------------------------------------------------------------
    rep.section ("Esticar áudio: segurar A na borda da playlist"_t);
    {
        app.run ("project.new");
        app.run ("playlist.clear");
        const auto a = app.run ("playlist.addItem", args ({ { "path", tone.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
        const auto b = app.run ("playlist.addItem", args ({ { "path", tone.getFullPathName() }, { "track", 2 }, { "start", 2 * ticksPerBar4 } }));
        const auto idA = a.value["item"].toString(), idB = b.value["item"].toString();
        const int len = model.get().findItem (idA) != nullptr ? model.get().findItem (idA)->length : 0;

        theme::LookAndFeel laf;   // o visual da casa, só neste pai (nada global)
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1400, 400);

        Playlist list (app);
        host.addAndMakeVisible (list);
        list.setBounds (host.getLocalBounds());
        list.refresh (model.get());
        auto* grid = childOf<PlaylistGrid> (list);
        rep.check (grid != nullptr && len > 0, "playlist montada sem tela, com dois clipes de áudio"_t);

        if (grid != nullptr && len > 0)
        {
            // a grade começa depois das raias (150 px), a régua tem 28 px e cada raia 54 px; no zoom de sempre
            // a grade automática encaixa no tempo (960 ticks)
            const float px = grid->getPxPerTick();
            auto at = [&] (int tick, int track) { return juce::Point<float> (150.0f + (float) tick * px, 28.0f + 54.0f * (float) track + 27.0f); };
            auto rightEdge = [&] (int tick, int track) { return at (tick, track) - juce::Point<float> (3.0f, 0.0f); };
            auto itemA = [&] { return model.get().findItem (idA); };
            auto itemB = [&] { return model.get().findItem (idB); };

            grid->mouseMove (mouseWith (*grid, rightEdge (len, 0), rightEdge (len, 0)));
            const bool trimCursor = grid->getMouseCursor() == juce::MouseCursor::LeftRightResizeCursor;
            grid->keyPressed (juce::KeyPress ('a'));
            rep.check (trimCursor && grid->getMouseCursor() != juce::MouseCursor::LeftRightResizeCursor,
                       "com o A segurado, a borda do áudio troca o cursor de aparar pelo de esticar"_t);

            // borda direita até o tempo 7 (6720 ticks): 1,5 vezes
            dragMouse (*grid, rightEdge (len, 0), rightEdge (6720, 0));
            rep.check (itemA() != nullptr && itemA()->length == 6720 && itemA()->start == 0
                           && std::abs (itemA()->stretch - 6720.0 / len) < 1.0e-5,
                       "A + arrastar a borda direita estica: "_t + PlaylistGrid::stretchLabel (itemA() != nullptr ? itemA()->stretch : 0.0)
                           + ", começo parado");
            rep.check (ctrlZ() && itemA() != nullptr && itemA()->stretch == 1.0 && itemA()->length == len,
                       "um Ctrl+Z desfaz o gesto inteiro"_t);
            app.run ("edit.redo");
            list.refresh (model.get());

            // borda esquerda do segundo até o tempo 5 (4800 ticks): o fim fica parado
            const int endB = 2 * ticksPerBar4 + len;
            dragMouse (*grid, at (2 * ticksPerBar4, 1) + juce::Point<float> (2.0f, 0.0f), at (4800, 1) + juce::Point<float> (2.0f, 0.0f));
            const double expectedB = (endB - 4800) / (double) len;
            rep.check (itemB() != nullptr && itemB()->start == 4800 && itemB()->end() == endB && itemB()->offsetSeconds == 0.0
                           && std::abs (itemB()->stretch - expectedB) < 1.0e-5,
                       "A + arrastar a borda esquerda estica com o fim parado: "_t
                           + PlaylistGrid::stretchLabel (itemB() != nullptr ? itemB()->stretch : 0.0));
            list.refresh (model.get());

            // solto o A, a borda volta a aparar, e agora passa do fim do arquivo (o resto fica em silêncio)
            grid->keyStateChanged (false);
            grid->mouseMove (mouseWith (*grid, rightEdge (endB, 1), rightEdge (endB, 1)));
            rep.check (grid->getMouseCursor() == juce::MouseCursor::LeftRightResizeCursor, "soltar o A volta o cursor de aparar"_t);
            const double stretchB = itemB() != nullptr ? itemB()->stretch : 0.0;
            dragMouse (*grid, rightEdge (endB, 1), rightEdge (14400, 1));
            rep.check (itemB() != nullptr && itemB()->end() == 14400 && itemB()->stretch == stretchB,
                       "sem o A, a borda passa do fim do arquivo sem esticar (fim em "_t + juce::String (itemB() != nullptr ? itemB()->end() : -1) + ")");
            list.refresh (model.get());

            rep.check (PlaylistGrid::stretchLabel (1.5) == "×1,50"_t, "o cabeçalho mostra ×1,50"_t);

            // a forma de onda carrega em segundo plano: um instante para a captura sair com ela
            juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
            rep.check (savePng (list, ctx.work.getChildFile ("esticar-playlist.png")),
                       "captura da playlist com os dois esticados e o silêncio depois do fim"_t);
        }

        host.removeChildComponent (&list);
        host.setLookAndFeel (nullptr);
    }
}

} // namespace whynot::selftest
