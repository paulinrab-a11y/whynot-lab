// Autoteste da playlist fatia 6 (27/10, noite): o que ficou de fora da voz na playlist.
// - fatia 1: o painel de propriedades do bloco (audio.setProperties e a tela: nome, cor, ganho, pan, tom e fina, fades
//   com curva, inverter, mudo e travar), um passo de desfazer por gesto, o pan no WAV e a trava no mouse e no teclado;
// - fatia 2: a ferramenta Zoom (a caixa, o clique, o botão direito), Shift+Z na seleção e Alt+Z na música;
// - fatia 3: marcadores com ações (criar na agulha, renomear, pular com Alt+setas, o trecho e o laço entre dois,
//   os blocos do trecho);
// - fatia 4: consolidar sem canal novo (o WAV ao lado, um bloco no mesmo canal, o som igual, o desfazer);
// - fatia 5: o "abaixar" com rampa (medido no WAV) e o punch que anda com o apagar e o abrir espaço.
// Capturas: playlist-propriedades.png, playlist-zoom.png e playlist-marcadores.png.
// Dono: Playlist fatia 6.

#include "../../record/BlockFile.h"
#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"
#include "../../ui/BlockProperties.h"
#include "../../ui/Playlist.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    constexpr double rate = 48000.0;
    constexpr int second = 1920;        // 120 BPM: 1 s = 1920 ticks
    constexpr int bar = 4 * ticksPerBeat;

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

    /** Seno de 440 Hz; a amplitude de cada instante vem de `level` (segundos -> amplitude). */
    template <typename Level>
    juce::File toneFile (const juce::File& folder, const juce::String& name, double seconds, Level level)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            b.setSample (0, i, (float) (level (t) * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t)));
        }

        const auto f = folder.getChildFile (name);
        writeWav (f, b);
        return f;
    }

    float peakOfChannel (const Audio& a, int channel, double t0, double t1)
    {
        if (! a.valid || channel >= a.buffer.getNumChannels())
            return -1.0f;

        const int from = juce::jlimit (0, a.buffer.getNumSamples(), (int) (t0 * a.sampleRate));
        const int to = juce::jlimit (from, a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));
        return a.buffer.getMagnitude (channel, from, to - from);
    }

    juce::String num (double v, int places = 3)   { return juce::String (v, places).replaceCharacter ('.', ','); }

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

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, juce::Point<float> down,
                              juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, mods,
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, down, now, 1, p != down };
    }

    /** Um arrasto inteiro do mouse na grade, de `from` a `to`. */
    void dragOn (PlaylistGrid& grid, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        grid.mouseDown (mouseAt (grid, from, from, mods));
        grid.mouseDrag (mouseAt (grid, (from + to) * 0.5f, from, mods));
        grid.mouseDrag (mouseAt (grid, to, from, mods));
        grid.mouseUp (mouseAt (grid, to, from, mods));
    }

    /** A grade da playlist sem janela, como no autoteste da voz: um pai fora da área de trabalho. */
    struct OffscreenGrid
    {
        explicit OffscreenGrid (AppController& app) : grid (app)
        {
            host.setLookAndFeel (&laf);
            host.setBounds (0, 0, 1200, 600);
            host.addAndMakeVisible (grid);
            grid.setBounds (host.getLocalBounds());
        }

        ~OffscreenGrid()
        {
            host.removeChildComponent (&grid);
            host.setLookAndFeel (nullptr);
        }

        theme::LookAndFeel laf;
        juce::Component host;
        PlaylistGrid grid;
    };

    constexpr float headerWidth = 150.0f, rulerHeight = 28.0f;
}

void runPlaylistBlocksSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("playlist-fatia6");
    work.createDirectory();

    static PlaylistItem missing;
    auto find = [&] (const juce::String& id) { const auto* f = model.get().findItem (id); return f != nullptr ? f : &missing; };
    auto render = [&] (const juce::String& name)
    {
        const auto f = work.getChildFile (name);
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
        return readWav (f);
    };
    auto fresh = [&]
    {
        app.run ("project.new");
        app.run ("transport.setBpm", args ({ { "bpm", 120.0 } }));
        app.run ("transport.setSongMode", args ({ { "on", true } }));
    };
    auto place = [&] (const juce::File& f, int track, int start)
    {
        return app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", f.getFullPathName() }, { "track", track }, { "start", start } }))
                   .value["item"].toString();
    };

    const auto sine = toneFile (work, "voz.wav", 2.0, [] (double) { return 0.25; });

    //==========================================================================
    rep.section ("Playlist fatia 6: propriedades do bloco"_t);
    fresh();
    const auto clip = place (sine, 1, 0);
    const auto chId = find (clip)->ref;
    auto channel = [&] { const auto* ch = model.get().findChannel (chId); return ch != nullptr ? *ch : Channel(); };

    {
        const auto set = app.run ("audio.setProperties", args ({ { "item", clip }, { "name", "Voz dobrada" }, { "colour", "#ffa366" },
                                                                 { "gainDb", -3.0 }, { "pan", -0.5 }, { "semitones", 2 }, { "cents", 25 } }));
        rep.check (set.ok && channel().name == "Voz dobrada" && channel().colour == "#FFA366" && std::abs (find (clip)->gainDb + 3.0f) < 1.0e-4f
                       && std::abs (find (clip)->pan + 0.5f) < 1.0e-4f && std::abs (find (clip)->pitch - 2.25f) < 1.0e-4f,
                   "audio.setProperties muda nome e cor do canal, ganho, pan e tom com a fina (2 st + 25 ct = 2,25) num comando"_t);
        rep.check (app.run ("edit.undo").ok && channel().name != "Voz dobrada" && find (clip)->gainDb == 0.0f && find (clip)->pitch == 0.0f,
                   "e um Ctrl+Z volta tudo de uma vez"_t);
        rep.check (! app.run ("audio.setProperties", args ({ { "item", clip }, { "colour", "laranja" } })).ok
                       && ! app.run ("audio.setProperties", args ({ { "item", clip }, { "name", "  " } })).ok
                       && ! app.run ("audio.setProperties", args ({ { "item", clip }, { "semitones", 24 }, { "cents", 50 } })).ok,
                   "cor fora de #RRGGBB, nome vazio e tom acima de +24 são recusados"_t);
        rep.check (app.run ("audio.setProperties", args ({ { "item", clip }, { "fadeIn", 1.5 }, { "fadeOut", 1.5 } })).ok
                       && find (clip)->fadeIn + find (clip)->fadeOut <= 2.0 + 1.0e-6,
                   "fades maiores que o bloco: os dois cabem nele ("_t + num (find (clip)->fadeIn) + " + " + num (find (clip)->fadeOut) + " s)");
        app.run ("edit.undo");
    }

    {
        // a tela do painel: cada controle, como a pessoa usa; um arrasto inteiro é um passo de desfazer
        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        BlockProperties panel (app, clip);
        host.setSize (panel.getWidth(), panel.getHeight());
        host.addAndMakeVisible (panel);

        auto& gain = panel.gainSlider();
        gain.onDragStart();
        for (double v : { -1.0, -2.5, -4.0, -6.0 })
            gain.setValue (v, juce::sendNotificationSync);
        gain.onDragEnd();
        rep.check (std::abs (find (clip)->gainDb + 6.0f) < 1.0e-4f, "arrastar o ganho no painel: -6 dB no bloco"_t);
        app.run ("edit.undo");
        panel.refresh (model.get());
        rep.check (find (clip)->gainDb == 0.0f && gain.getValue() == 0.0,
                   "um Ctrl+Z desfaz o arrasto inteiro (4 valores no caminho), e o painel aberto volta a mostrar 0 dB"_t);

        panel.panSlider().setValue (100.0, juce::sendNotificationSync);
        panel.semitoneSlider().setValue (-5.0, juce::sendNotificationSync);
        panel.centsSlider().setValue (-30.0, juce::sendNotificationSync);
        panel.fadeInSlider().setValue (250.0, juce::sendNotificationSync);
        panel.fadeInCurveBox().setSelectedId (2, juce::sendNotificationSync);
        panel.fadeOutSlider().setValue (400.0, juce::sendNotificationSync);
        rep.check (std::abs (find (clip)->pan - 1.0f) < 1.0e-4f && std::abs (find (clip)->pitch + 5.3f) < 1.0e-4f
                       && std::abs (find (clip)->fadeIn - 0.25) < 1.0e-6 && find (clip)->fadeInCurve == fadeCurveConvex
                       && std::abs (find (clip)->fadeOut - 0.4) < 1.0e-6,
                   "pan à direita, -5 st e -30 ct (-5,30), fade de entrada de 250 ms rápido e de saída de 400 ms"_t);

        panel.nameEditor().setText (juce::String::fromUTF8 ("Refrão"));
        panel.nameEditor().onReturnKey();
        panel.chooseColour (3);
        rep.check (channel().name == juce::String::fromUTF8 ("Refrão") && channel().colour == channelColourForIndex (3),
                   "o nome (Enter grava) e a cor da paleta vão para o canal do bloco"_t);

        const auto channelsBefore = model.get().channels.size();   // o projeto novo já vem com os canais do kit
        panel.muteButton().setToggleState (true, juce::sendNotificationSync);
        panel.lockButton().setToggleState (true, juce::sendNotificationSync);
        panel.reverseButton().setToggleState (true, juce::sendNotificationSync);
        rep.check (find (clip)->muted && find (clip)->locked && find (clip)->reversed && channel().id == chId
                       && model.get().channels.size() == channelsBefore,
                   "mudo, travar e inverter pelo painel (sem canal novo): "_t + (find (clip)->muted ? "mudo " : "")
                       + (find (clip)->locked ? "travado " : "") + (find (clip)->reversed ? "invertido " : "")
                       + juce::String ((int) model.get().channels.size()) + " canais");
        panel.refresh (model.get());
        rep.check (savePng (panel, ctx.work.getChildFile ("playlist-propriedades.png")), "captura: o painel de propriedades"_t);

        for (const char* key : { "muted", "locked" })
            app.run ("audio.setProperties", args ({ { "item", clip }, { key, false } }));

        app.run ("audio.reverse", args ({ { "item", clip } }));
        app.run ("audio.setProperties", args ({ { "item", clip }, { "semitones", 0 }, { "cents", 0 }, { "fadeIn", 0.0 }, { "fadeOut", 0.0 }, { "pan", 0.0 } }));
        host.removeChildComponent (&panel);
        host.setLookAndFeel (nullptr);
    }

    {
        // o pan no WAV: todo à esquerda, o canal direito fica quieto
        const auto centre = render ("pan-centro.wav");
        app.run ("audio.setProperties", args ({ { "item", clip }, { "pan", -1.0 } }));
        const auto left = render ("pan-esquerda.wav");
        const float l = peakOfChannel (left, 0, 0.5, 1.5), r = peakOfChannel (left, 1, 0.5, 1.5);
        const float c0 = peakOfChannel (centre, 0, 0.5, 1.5), c1 = peakOfChannel (centre, 1, 0.5, 1.5);
        rep.check (centre.valid && left.valid && left.buffer.getNumChannels() == 2 && std::abs (c0 - c1) < 0.01f * c0 && l > 0.1f && r < 0.02f * l,
                   "pan do bloco no WAV: no centro L "_t + num (c0, 3) + " e R " + num (c1, 3) + "; todo à esquerda L " + num (l, 3) + " e R " + num (r, 4));
        app.run ("audio.setProperties", args ({ { "item", clip }, { "pan", 0.0 } }));
    }

    {
        // a cópia estéreo: sai com dois canais e o tamanho do original; um arquivo do usuário com o nome dela não é apagado
        const auto folder = ctx.work.getChildFile ("estereo");
        const auto mono = toneFile (folder, "Dobra.wav", 1.0, [] (double) { return 0.25; });
        const auto copy = blockfile::stereoOf (mono);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (copy));
        rep.check (copy.getFileName() == "Dobra_wnl-estereo.wav" && reader != nullptr && reader->numChannels == 2 && reader->lengthInSamples == (juce::int64) rate,
                   "cópia estéreo do bloco mono: dois canais, o mesmo tamanho"_t);
        reader.reset();

        const auto other = toneFile (folder, "Base.wav", 0.5, [] (double) { return 0.25; });
        const auto mine = toneFile (folder, "Base_wnl-estereo.wav", 2.0, [] (double) { return 0.1; });
        const auto sizeBefore = mine.getSize();
        rep.check (blockfile::stereoOf (other) == other && mine.existsAsFile() && mine.getSize() == sizeBefore,
                   "um arquivo do usuário com o nome da cópia fica intacto (o bloco toca o original)"_t);
    }

    {
        // o .wnl: os campos novos só quando mudam, iguais nos dois escritores, e voltam ao abrir
        app.run ("audio.setProperties", args ({ { "item", clip }, { "pan", 0.35 }, { "locked", true } }));
        const auto json = wnl::toJson (model.get());
        Project back;
        const auto read = wnl::fromJson (json, back);
        const auto* again = back.findItem (clip);
        rep.check (json == wnl::toJsonReference (model.get()) && json.contains ("\"pan\": 0.35") && json.contains ("\"locked\": true")
                       && read.wasOk() && again != nullptr && std::abs (again->pan - 0.35f) < 1.0e-6f && again->locked,
                   "no .wnl: \"pan\" e \"locked\" opcionais, toJson igual ao toJsonReference, e voltam ao abrir"_t);
        app.run ("audio.setProperties", args ({ { "item", clip }, { "pan", 0.0 } }));
    }

    {
        // travado: o mouse não move nem apara, o Delete e o Ctrl+E deixam o bloco
        OffscreenGrid view (app);
        auto& grid = view.grid;
        grid.refresh (model.get());
        const float px = grid.getPxPerTick(), y = rulerHeight + 20.0f;
        const juce::Point<float> from { headerWidth + 0.5f * second * px, y }, to { headerWidth + 1.5f * second * px, y };
        dragOn (grid, from, to);
        rep.check (find (clip)->start == 0, "arrastar um bloco travado não move"_t);
        grid.selectAll();
        grid.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        app.run ("transport.setPosition", args ({ { "at", second } }));
        grid.keyPressed (juce::KeyPress ('e', juce::ModifierKeys::commandModifier, 0));
        rep.check (model.get().findItem (clip) != nullptr && model.get().playlistItems.size() == 1,
                   "Delete e Ctrl+E deixam o bloco travado inteiro"_t);

        app.run ("audio.setProperties", args ({ { "item", clip }, { "locked", false } }));
        grid.refresh (model.get());
        dragOn (grid, from, to);
        rep.check (find (clip)->start > 0, "destravado, o mesmo arrasto move ("_t + juce::String (find (clip)->start) + " ticks)");
        app.run ("edit.undo");
        app.run ("transport.setPosition", args ({ { "at", 0 } }));
    }

    //==========================================================================
    rep.section ("Playlist fatia 6: ferramenta de zoom"_t);
    fresh();

    for (int n = 0; n < 4; ++n)
        place (sine, 1, n * 4 * bar);   // 4 vozes de 2 s, uma a cada 4 compassos: a música vai até 12 compassos e 1 s

    {
        OffscreenGrid view (app);
        auto& grid = view.grid;
        grid.refresh (model.get());
        auto near = [] (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; };
        const auto start = grid.visibleTicks();

        grid.zoomToRange (4 * bar, 8 * bar);
        const auto four = grid.visibleTicks();
        rep.check (four.getStart() <= 4 * bar && four.getEnd() >= 8 * bar && four.getLength() < 4.3 * bar,
                   "zoom num trecho de 4 compassos: a vista vai de "_t + num (four.getStart() / bar, 2) + " a " + num (four.getEnd() / bar, 2) + " compassos");

        grid.zoomBack();
        rep.check (near (grid.visibleTicks().getStart(), start.getStart(), 1.0) && near (grid.visibleTicks().getLength(), start.getLength(), 1.0),
                   "voltar o zoom devolve a vista de antes"_t);

        // a ferramenta: Z, a caixa arrastada, o clique e o botão direito
        rep.check (grid.keyPressed (juce::KeyPress ('z')) && grid.getTool() == PlaylistGrid::Tool::zoom, "a tecla Z escolhe a ferramenta Zoom"_t);
        const float y = rulerHeight + 60.0f;
        const auto x = [&] (double ticks) { return headerWidth + (float) ((ticks - grid.getScrollTicks()) * grid.getPxPerTick()); };
        dragOn (grid, { x (2 * bar), y }, { x (3 * bar), y });
        const auto boxed = grid.visibleTicks();
        rep.check (boxed.getStart() <= 2 * bar + 1 && boxed.getEnd() >= 3 * bar - 1 && boxed.getLength() < 1.2 * bar,
                   "arrastar uma caixa do compasso 3 ao 4 enquadra só ele ("_t + num (boxed.getLength() / bar, 2) + " compasso)");

        const float before = grid.getPxPerTick();
        const juce::Point<float> here { headerWidth + 300.0f, y };
        grid.mouseDown (mouseAt (grid, here, here));
        grid.mouseUp (mouseAt (grid, here, here));
        rep.check (grid.getPxPerTick() > before * 1.5f, "um clique com o Zoom aproxima"_t);

        const auto right = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);
        grid.mouseDown (mouseAt (grid, here, here, right));
        grid.mouseUp (mouseAt (grid, here, here, right));
        rep.check (near (grid.getPxPerTick(), before, 1.0e-6), "o botão direito volta um passo"_t);
        grid.mouseDown (mouseAt (grid, here, here, right));
        grid.mouseUp (mouseAt (grid, here, here, right));
        rep.check (near (grid.visibleTicks().getLength(), start.getLength(), 1.0), "e mais um, à vista de antes da caixa"_t);
        rep.check (model.get().playlistItems.size() == 4, "o botão direito com o Zoom não apaga bloco nenhum"_t);

        // Shift+Z na seleção, Alt+Z na música inteira
        grid.setTimeSelection ({ 8 * bar, 10 * bar });
        rep.check (grid.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::shiftModifier, 0))
                       && grid.visibleTicks().getStart() <= 8 * bar && grid.visibleTicks().getEnd() >= 10 * bar
                       && grid.visibleTicks().getLength() < 2.2 * bar,
                   "Shift+Z enquadra o trecho selecionado"_t);
        grid.setTimeSelection ({});
        rep.check (grid.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::altModifier, 0)) && grid.visibleTicks().getStart() <= 0.5
                       && grid.visibleTicks().getEnd() >= 12 * bar + second && grid.visibleTicks().getLength() < 14.0 * bar,
                   "Alt+Z enquadra a música inteira (até "_t + num (grid.visibleTicks().getEnd() / bar, 2) + " compassos)");
        grid.selectAll();
        grid.zoomToSelection();
        rep.check (grid.visibleTicks().getEnd() >= 12 * bar + second, "sem trecho, Shift+Z enquadra os blocos selecionados"_t);

        grid.setTool (PlaylistGrid::Tool::zoom);
        grid.zoomToRange (0, 4 * bar);
        grid.mouseMove (mouseAt (grid, { x (1 * bar), y }, { x (1 * bar), y }));
        grid.mouseDown (mouseAt (grid, { x (1 * bar), y }, { x (1 * bar), y }));
        grid.mouseDrag (mouseAt (grid, { x (2 * bar), y + 60.0f }, { x (1 * bar), y }));
        rep.check (savePng (view.host, ctx.work.getChildFile ("playlist-zoom.png")), "captura: a caixa da ferramenta Zoom"_t);
        grid.mouseUp (mouseAt (grid, { x (2 * bar), y + 60.0f }, { x (1 * bar), y }));
        grid.setTool (PlaylistGrid::Tool::draw);
    }

    //==========================================================================
    rep.section ("Playlist fatia 6: marcadores com ações"_t);

    {
        // criar na agulha (o Alt+T parado: o playlist.addMarker no playStart), renomear
        app.run ("transport.setPosition", args ({ { "at", 4 * bar } }));
        const auto verse = app.run ("playlist.addMarker", args ({ { "at", app.playStart() } })).value["id"].toString();
        const auto intro = app.run ("playlist.addMarker", args ({ { "at", 0 }, { "name", "Intro" } })).value["id"].toString();
        const auto chorus = app.run ("playlist.addMarker", args ({ { "at", 8 * bar }, { "name", juce::String::fromUTF8 ("Refrão") } })).value["id"].toString();
        rep.check (model.get().findMarker (verse) != nullptr && model.get().findMarker (verse)->t == 4 * bar && model.get().markers.size() == 3,
                   "criar na agulha: o marcador cai no compasso 5, onde ela está"_t);
        rep.check (app.run ("playlist.updateMarker", args ({ { "marker", verse }, { "name", "Verso" } })).ok
                       && model.get().findMarker (verse)->name == "Verso",
                   "renomear: \"Verso\""_t);

        OffscreenGrid view (app);
        auto& grid = view.grid;
        grid.refresh (model.get());
        const auto alt = juce::ModifierKeys::altModifier;

        app.run ("transport.setPosition", args ({ { "at", 2 * bar } }));
        rep.check (grid.keyPressed (juce::KeyPress (juce::KeyPress::rightKey, alt, 0)) && app.playStart() == 4 * bar,
                   "Alt+→ pula para o próximo marcador (Verso, compasso 5)"_t);
        grid.keyPressed (juce::KeyPress (juce::KeyPress::rightKey, alt, 0));
        rep.check (app.playStart() == 8 * bar, "de novo: Refrão (compasso 9)"_t);
        rep.check (! app.run ("playlist.gotoMarker", args ({ { "direction", "next" } })).ok && app.playStart() == 8 * bar,
                   "depois do último não há próximo: a agulha fica"_t);
        grid.keyPressed (juce::KeyPress (juce::KeyPress::leftKey, alt, 0));
        grid.keyPressed (juce::KeyPress (juce::KeyPress::leftKey, alt, 0));
        rep.check (app.playStart() == 0, "Alt+← duas vezes volta à Intro"_t);
        rep.check (app.run ("playlist.gotoMarker", args ({ { "marker", chorus } })).value["at"] == juce::var (8 * bar) && app.playStart() == 8 * bar,
                   "playlist.gotoMarker pelo id"_t);

        // o trecho entre dois (Shift+clique no outro), o laço (Ctrl+Shift+clique) e os blocos do trecho
        const float px = grid.getPxPerTick();
        auto markerAt = [&] (int t) { return juce::Point<float> { headerWidth + (float) ((t - grid.getScrollTicks()) * px) + 4.0f, 8.0f }; };
        const auto first = markerAt (0), second2 = markerAt (8 * bar);
        grid.mouseDown (mouseAt (grid, first, first));
        grid.mouseUp (mouseAt (grid, first, first));
        const auto shift = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
        grid.mouseDown (mouseAt (grid, second2, second2, shift));
        grid.mouseUp (mouseAt (grid, second2, second2, shift));
        rep.check (grid.getTimeSelection() == juce::Range<int> (0, 8 * bar) && app.getTimeSelection() == juce::Range<int> (0, 8 * bar),
                   "clique na Intro e Shift+clique no Refrão: o trecho dos compassos 1 a 9 fica selecionado (o play toca nele)"_t);

        app.run ("transport.setLoop", args ({ { "on", false } }));
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        const auto looped = app.run ("playlist.selectMarkers", args ({ { "from", chorus }, { "to", verse }, { "loop", true } }));
        rep.check (looped.ok && (int) looped.value["from"] == 4 * bar && (int) looped.value["to"] == 8 * bar && model.get().songMode && model.get().loop
                       && app.getTimeSelection() == juce::Range<int> (4 * bar, 8 * bar),
                   "o laço entre Refrão e Verso (a ordem não importa): compassos 5 a 9, SONG e loop ligados"_t);
        rep.check (app.run ("edit.undo").ok && ! model.get().songMode, "o Ctrl+Z desliga o SONG que o laço ligou"_t);
        app.run ("transport.setSongMode", args ({ { "on", true } }));

        grid.setNeedle (-1);   // o timer da tela passa aqui: a seleção que o comando pôs aparece
        rep.check (grid.getTimeSelection() == juce::Range<int> (4 * bar, 8 * bar), "a grade mostra o trecho que o comando escolheu"_t);
        rep.check (grid.selectBlocksIn ({ 4 * bar, 8 * bar }) == 1, "os blocos do trecho Verso: 1 bloco selecionado"_t);
        rep.check (! app.run ("playlist.selectMarkers", args ({ { "from", intro }, { "to", intro } })).ok, "um marcador com ele mesmo é recusado"_t);

        grid.refresh (model.get());
        rep.check (savePng (view.host, ctx.work.getChildFile ("playlist-marcadores.png")), "captura: os marcadores e o trecho selecionado"_t);
        grid.setTimeSelection ({});
    }

    //==========================================================================
    rep.section ("Playlist fatia 6: consolidar sem canal novo"_t);
    fresh();

    {
        const auto a = place (sine, 1, 0), b = place (sine, 1, 3 * second);
        app.run ("audio.setFades", args ({ { "item", a }, { "out", 0.3 } }));
        app.run ("audio.setGain", args ({ { "item", b }, { "db", -6.0 } }));
        const auto before = render ("antes-consolidar.wav");
        const auto channels = model.get().channels.size();
        const auto lane = find (a)->track;
        const auto ref = find (a)->ref;
        const auto merged = app.run ("audio.consolidate", args ({ { "items", juce::Array<juce::var> { a, b } } }));
        const auto id = merged.value["item"].toString();
        const auto file = juce::File (merged.value["path"].toString());
        rep.check (merged.ok && model.get().channels.size() == channels && model.get().playlistItems.size() == 1 && find (id)->ref == ref
                       && find (id)->track == lane && find (id)->file.isNotEmpty() && file.existsAsFile()
                       && file.getParentDirectory() == sine.getParentDirectory() && file.getFileName().startsWith ("voz_consolidado"),
                   "os dois blocos viram um só, no mesmo canal e na mesma raia; o WAV fica ao lado do original ("_t + file.getFileName() + ")");
        rep.check (model.get().findChannel (find (id)->ref) != nullptr && model.get().findChannel (find (id)->ref)->samplePath.contains ("voz.wav"),
                   "o canal continua tocando o original nos outros blocos (o arquivo é só do bloco)"_t);

        const auto after = render ("depois-consolidar.wav");
        const float p1 = peakIn (before, 0.2, 1.4), q1 = peakIn (after, 0.2, 1.4);
        const float p2 = peakIn (before, 3.2, 4.8), q2 = peakIn (after, 3.2, 4.8);
        const float gap = peakIn (after, 2.05, 2.95), tail = peakIn (after, 1.85, 1.95), tailBefore = peakIn (before, 1.85, 1.95);
        rep.check (before.valid && after.valid && std::abs (p1 - q1) < 0.01f * p1 && std::abs (p2 - q2) < 0.01f * p2 && gap < 1.0e-4f
                       && std::abs (tail - tailBefore) < 0.01f,
                   "o som igual: o primeiro "_t + num (p1, 3) + " e " + num (q1, 3) + ", o segundo (-6 dB) " + num (p2, 3) + " e " + num (q2, 3)
                       + ", o fade igual e o vão em silêncio");

        const auto json = wnl::toJson (model.get());
        rep.check (json == wnl::toJsonReference (model.get()) && json.contains ("\"file\""), "no .wnl o bloco leva \"file\", igual nos dois escritores"_t);

        {
            // a cópia (Ctrl+B) de um consolidado toca o WAV dele, não o original do canal (achado da revisão)
            OffscreenGrid view (app);
            view.grid.refresh (model.get());
            view.grid.selectAll();
            view.grid.keyPressed (juce::KeyPress ('b', juce::ModifierKeys::commandModifier, 0));
            const auto& items = model.get().playlistItems;
            rep.check (items.size() == 2 && items[0].file == items[1].file && items[1].ref == ref,
                       "Ctrl+B num consolidado: a cópia leva o WAV dele e o mesmo canal"_t);
            app.run ("edit.undo");
        }

        rep.check (app.run ("edit.undo").ok && model.get().playlistItems.size() == 2 && model.get().findItem (a) != nullptr
                       && model.get().channels.size() == channels,
                   "o Ctrl+Z volta os dois blocos"_t);

        // só o segundo (-6 dB): o WAV dele tem pico de 0,125; normalizar lê o WAV do bloco, não o original (0,25)
        const auto alone = app.run ("audio.consolidate", args ({ { "items", juce::Array<juce::var> { b } } })).value["item"].toString();
        rep.check (app.run ("audio.normalize", args ({ { "item", alone } })).ok && find (alone)->gainDb > 15.0f,
                   "normalizar o consolidado lê o WAV dele: +"_t + num (find (alone)->gainDb, 1) + " dB (o original daria +11)");
        app.run ("edit.undo");
        app.run ("edit.undo");

        const auto c = place (sine, 2, 0);
        rep.check (! app.run ("audio.consolidate", args ({ { "items", juce::Array<juce::var> { a, c } } })).ok,
                   "blocos de raias diferentes não se juntam: uma raia de cada vez"_t);
    }

    //==========================================================================
    rep.section ("Playlist fatia 6: abaixar com rampa e o punch com o ripple"_t);
    fresh();

    {
        // tom forte até 0,5 s, quieto (-48 dB) até 1,5 s, forte até 2 s: abaixar 15 dB no trecho quieto
        const auto quiet = toneFile (work, "voz-quieta.wav", 2.0, [] (double t) { return t < 0.5 || t >= 1.5 ? 0.25 : 0.004; });
        const auto item = place (quiet, 1, 0);
        const auto lowered = app.run ("audio.removeSilence", args ({ { "item", item }, { "thresholdDb", -40.0 }, { "reduceDb", -15.0 } }));
        const auto* regions = lowered.value["regions"].getArray();
        const int q0 = regions != nullptr && ! regions->isEmpty() ? (int) (*regions)[0]["from"] : 0;
        const auto wav = render ("abaixar-rampa.wav");
        const int ramp = juce::roundToInt (secondsToTicks (0.010, 120.0));
        const double mid = (q0 - ramp * 0.5) / (double) second;
        // o render tem o volume do canal e do master: a escala vem do trecho forte (0,25 no arquivo)
        const float scale = peakIn (wav, 0.1, 0.4) / 0.25f;
        const float full = 0.004f * scale, low = full * std::pow (10.0f, -15.0f / 20.0f);
        const float inRamp = peakIn (wav, mid - 0.0015, mid + 0.0015), beforeRamp = peakIn (wav, mid - 0.012, mid - 0.009);
        const float afterRamp = peakIn (wav, q0 / (double) second + 0.004, q0 / (double) second + 0.008);
        rep.check (lowered.ok && q0 > 0 && std::abs (beforeRamp - full) < 0.15f * full && std::abs (afterRamp - low) < 0.2f * low
                       && inRamp > low + 0.25f * (full - low) && inRamp < full - 0.15f * (full - low),
                   "abaixar 15 dB: antes "_t + num (beforeRamp, 4) + ", no meio da rampa de 10 ms " + num (inRamp, 4) + ", depois " + num (afterRamp, 4)
                       + " (sem o degrau de " + num (full, 4) + " para " + num (low, 4) + ")");

        int overlaps = 0;

        for (auto& x : model.get().playlistItems)
            for (auto& y : model.get().playlistItems)
                if (x.id != y.id && y.start < x.end() && y.start > x.start && y.end() > x.end()
                    && x.fadeOutCurve == fadeCurveLinear && y.fadeInCurve == fadeCurveLinear && x.fadeOut > 0.0 && y.fadeIn > 0.0)
                    ++overlaps;

        rep.check (overlaps == 2, "as duas bordas do trecho abaixado são um crossfade reto com o vizinho ("_t + juce::String (overlaps) + ")");
    }

    {
        // o punch anda com o apagar e o abrir espaço (antes ficava parado)
        app.run ("record.setPunch", args ({ { "start", 4 * bar }, { "end", 6 * bar }, { "on", true } }));
        app.run ("playlist.removeSpace", args ({ { "from", 0 }, { "to", bar } }));
        rep.check (model.get().punchIn == 3 * bar && model.get().punchOut == 5 * bar && model.get().punchOn,
                   "apagar o compasso 1 puxa o punch um compasso para trás (4 a 6 vira 3 a 5)"_t);
        app.run ("playlist.insertSpace", args ({ { "at", 4 * bar }, { "length", bar } }));
        rep.check (model.get().punchIn == 3 * bar && model.get().punchOut == 6 * bar, "abrir um compasso dentro do punch o alarga (3 a 6)"_t);
        app.run ("playlist.insertSpace", args ({ { "at", 0 }, { "length", 2 * bar } }));
        rep.check (model.get().punchIn == 5 * bar && model.get().punchOut == 8 * bar, "abrir antes dele empurra as duas pontas (5 a 8)"_t);
        app.run ("playlist.removeSpace", args ({ { "from", 4 * bar }, { "to", 10 * bar } }));
        rep.check (! model.get().punchOn && model.get().punchOut > model.get().punchIn,
                   "apagar um trecho que cobre o punch inteiro desliga o punch"_t);
        app.run ("edit.undo");
        rep.check (model.get().punchOn && model.get().punchIn == 5 * bar, "e o Ctrl+Z traz o punch de volta"_t);
    }

    app.run ("project.new");
}

} // namespace whynot::selftest
