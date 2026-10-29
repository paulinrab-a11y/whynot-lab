// Autoteste da revisão de usabilidade de 28/10 (telas de 26 a 28/10):
// - as três pendências da playlist fatia 6: a rodinha no painel de propriedades vira um passo de desfazer só; o nome
//   digitado grava quando o painel fecha sem Enter (o clique fora); a IA não move, apara, corta, apaga nem destrava um
//   bloco travado (o CommandRegistry::vetoChange com o control::lockedItemsVeto, como a ponte liga);
// - as capturas de depois: o painel de propriedades e a régua com marcadores (o número do compasso fora da bandeirinha).
// Dono: Revisão de usabilidade (28/10).

#include "SelfTestKit.h"
#include "../../control/CommandGate.h"
#include "../../ui/BlockProperties.h"
#include "../../ui/Playlist.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;

    juce::File toneFile (const juce::File& folder, const juce::String& name, double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (float) (0.25 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / rate)));

        const auto f = folder.getChildFile (name);
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream != nullptr)
            if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());

        return f;
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

    /** Um clique da rodinha para cima sobre `c`; cada evento com a sua hora (o Slider ignora dois na mesma hora). */
    void wheelUp (juce::Component& c, int n)
    {
        const auto when = juce::Time::getCurrentTime() + juce::RelativeTime::milliseconds (10 * n);
        const juce::Point<float> p { c.getWidth() * 0.5f, c.getHeight() * 0.5f };
        const juce::MouseEvent e { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(),
                                   juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                   juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                   &c, &c, when, p, when, 0, false };
        juce::MouseWheelDetails w;
        w.deltaX = 0.0f;
        w.deltaY = 0.14f;   // um clique da rodinha no Windows
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        c.mouseWheelMove (e, w);
    }

    /** O painel de propriedades num pai fora da área de trabalho, como o autoteste da fatia 6. */
    struct OffscreenPanel
    {
        OffscreenPanel (AppController& app, const juce::String& item) : panel (app, item)
        {
            host.setLookAndFeel (&laf);
            host.setSize (panel.getWidth(), panel.getHeight());
            host.addAndMakeVisible (panel);
        }

        ~OffscreenPanel()
        {
            host.removeChildComponent (&panel);
            host.setLookAndFeel (nullptr);
        }

        theme::LookAndFeel laf;
        juce::Component host;
        BlockProperties panel;
    };
}

void runUxReviewSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    auto& registry = app.getCommands();
    const auto work = ctx.work.getChildFile ("ux-2810");
    work.createDirectory();

    static PlaylistItem missing;
    auto find = [&] (const juce::String& id) { const auto* f = model.get().findItem (id); return f != nullptr ? f : &missing; };

    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 120.0 } }));
    const auto clip = app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", toneFile (work, "voz.wav", 2.0).getFullPathName() },
                                                          { "track", 1 }, { "start", 0 } })).value["item"].toString();
    const auto chId = find (clip)->ref;
    auto channelName = [&] { const auto* ch = model.get().findChannel (chId); return ch != nullptr ? ch->name : juce::String(); };

    //==========================================================================
    rep.section ("Revisão de usabilidade 28/10: pendências da playlist fatia 6"_t);

    {
        // a rodinha: 5 cliques seguidos no ganho são um passo de desfazer só
        OffscreenPanel view (app, clip);
        auto& gain = view.panel.gainSlider();
        const auto undoBefore = registry.undoDescription();

        for (int n = 0; n < 5; ++n)
            wheelUp (gain, n);

        view.panel.finishWheel();   // o que a pausa de 600 ms da rodinha faria
        const float moved = find (clip)->gainDb;
        rep.check (moved > 0.0f && registry.undoDescription() != undoBefore,
                   "5 cliques da rodinha no ganho do painel mudam o bloco ("_t + juce::String (moved, 1) + " dB)");
        app.run ("edit.undo");
        view.panel.refresh (model.get());
        rep.check (find (clip)->gainDb == 0.0f && gain.getValue() == 0.0,
                   "e um Ctrl+Z volta os 5 cliques de uma vez (antes, um passo por clique)"_t);

        // a rodinha num controle e depois em outro: dois passos
        wheelUp (gain, 10);
        wheelUp (view.panel.panSlider(), 11);
        view.panel.finishWheel();
        app.run ("edit.undo");
        rep.check (find (clip)->gainDb > 0.0f && find (clip)->pan == 0.0f,
                   "rodinha no ganho e depois no pan: o Ctrl+Z desfaz só o pan"_t);
        app.run ("edit.undo");
    }

    {
        // o nome digitado sem Enter grava quando o painel fecha (o clique fora fecha sem tirar o foco)
        {
            OffscreenPanel view (app, clip);
            view.panel.nameEditor().setText ("Coro", juce::dontSendNotification);
        }
        rep.check (channelName() == "Coro", "fechar o painel com o nome digitado (sem Enter) grava o nome: \""_t + channelName() + "\"");

        {
            OffscreenPanel view (app, clip);
            auto& name = view.panel.nameEditor();
            name.setText ("Ponte", juce::dontSendNotification);
            name.onEscapeKey();
        }
        rep.check (channelName() == "Coro", "com Esc antes de fechar, o nome digitado não grava"_t);
        app.run ("edit.undo");
    }

    {
        // a IA e o bloco travado: o registro com o veto ligado, como a ponte faz em cada comando e lote dela
        app.run ("audio.setProperties", args ({ { "item", clip }, { "locked", true } }));
        registry.vetoChange = control::lockedItemsVeto;
        const auto undoBefore = registry.undoDescription();

        const auto move = app.run ("playlist.updateItem", args ({ { "item", clip }, { "start", 1920 } }));
        const auto cut = app.run ("playlist.splitItem", args ({ { "item", clip }, { "at", 960 } }));
        const auto unlock = app.run ("audio.setProperties", args ({ { "item", clip }, { "locked", false } }));
        rep.check (! move.ok && ! cut.ok && ! unlock.ok && find (clip)->start == 0 && find (clip)->locked
                       && model.get().playlistItems.size() == 1 && registry.undoDescription() == undoBefore,
                   "a IA não move, não corta nem destrava o bloco travado, e nada entra no desfazer: \""_t + move.error + "\"");

        {
            // pela ponte, cada comando roda num gesto "IA: ..."
            CommandRegistry::ScopedGesture gesture (registry, "IA: Apagar bloco");
            rep.check (! app.run ("playlist.removeItem", args ({ { "item", clip } })).ok, "num gesto da IA, apagar o travado é recusado"_t);
        }
        rep.check (model.get().findItem (clip) != nullptr && registry.undoDescription() == undoBefore, "e o bloco continua lá"_t);

        const auto gain = app.run ("audio.setProperties", args ({ { "item", clip }, { "gainDb", -3.0 } }));
        rep.check (gain.ok && std::abs (find (clip)->gainDb + 3.0f) < 1.0e-4f, "o ganho do travado a IA muda (como no painel)"_t);
        app.run ("edit.undo");

        juce::Array<juce::var> steps { args ({ { "id", "audio.setProperties" }, { "params", args ({ { "item", clip }, { "gainDb", -6.0 } }) } }),
                                       args ({ { "id", "playlist.removeItem" }, { "params", args ({ { "item", clip } }) } }) };
        const auto batch = registry.runBatch (steps, "IA: Lote");
        rep.check (! batch.ok && find (clip)->gainDb == 0.0f && model.get().findItem (clip) != nullptr,
                   "num lote da IA, o passo que apaga o travado recusa o lote inteiro (o ganho do passo 1 volta)"_t);

        registry.vetoChange = nullptr;
        rep.check (app.run ("playlist.updateItem", args ({ { "item", clip }, { "start", 1920 } })).ok && find (clip)->start == 1920,
                   "sem o veto (a pessoa, pela tela e pelos atalhos), o comando passa como antes"_t);
        app.run ("edit.undo");
        app.run ("audio.setProperties", args ({ { "item", clip }, { "locked", false } }));
    }

    //==========================================================================
    rep.section ("Revisão de usabilidade 28/10: capturas"_t);

    {
        OffscreenPanel view (app, clip);
        view.panel.semitoneSlider().setValue (-5.0, juce::sendNotificationSync);
        view.panel.centsSlider().setValue (-30.0, juce::sendNotificationSync);
        view.panel.gainSlider().setValue (-3.5, juce::sendNotificationSync);
        view.panel.muteButton().setToggleState (true, juce::sendNotificationSync);
        view.panel.refresh (model.get());
        rep.check (view.panel.gainSlider().getTextFromValue (-3.5) == "-3,5 dB", "o ganho no painel com vírgula: \""_t
                       + view.panel.gainSlider().getTextFromValue (-3.5) + "\"");
        rep.check (savePng (view.panel, work.getChildFile ("ux-propriedades.png")), "captura: o painel de propriedades"_t);
        app.run ("audio.setProperties", args ({ { "item", clip }, { "semitones", 0 }, { "cents", 0 }, { "gainDb", 0.0 }, { "muted", false } }));
    }

    {
        // marcadores no começo dos compassos 1, 5 e 9: o número do compasso aparece depois da bandeirinha
        for (auto [name, bar] : { std::pair { "Intro", 0 }, std::pair { "Verso", 4 }, std::pair { "Refrão", 8 } })
            app.run ("playlist.addMarker", args ({ { "name", juce::String::fromUTF8 (name) }, { "at", bar * 4 * ticksPerBeat } }));

        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setBounds (0, 0, 1200, 300);
        PlaylistGrid grid (app);
        host.addAndMakeVisible (grid);
        grid.setBounds (host.getLocalBounds());
        grid.refresh (model.get());
        rep.check (savePng (grid, work.getChildFile ("ux-marcadores.png")), "captura: a régua com três marcadores"_t);
        host.removeChildComponent (&grid);
        host.setLookAndFeel (nullptr);
    }

    app.run ("project.new");
}

} // namespace whynot::selftest
