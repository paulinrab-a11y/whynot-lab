// Autoteste do Design: a galeria da base visual (docs/design.md).
// Monta as peças fora da janela, sobre um cartão, confere as cores que a base promete
// e grava a prancha em PNG para quem revisa a tela.

#include "SelfTestKit.h"
#include "../../ui/Controls.h"
#include "../../ui/NoteNames.h"
#include "../../ui/SplashWindow.h"
#include "../../ui/StatusBar.h"
#include "../../ui/WelcomeCard.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    bool isOrange (juce::Colour c)   { return (int) c.getRed() - (int) c.getBlue() > 25; }
    bool isGrey (juce::Colour c)     { return std::abs ((int) c.getRed() - (int) c.getBlue()) <= 8; }
    bool isRed (juce::Colour c)      { return (int) c.getRed() - (int) c.getGreen() > 120 && std::abs ((int) c.getGreen() - (int) c.getBlue()) < 30; }

    bool near (juce::Colour a, juce::Colour b, int tolerance)
    {
        return std::abs ((int) a.getRed() - (int) b.getRed()) <= tolerance
            && std::abs ((int) a.getGreen() - (int) b.getGreen()) <= tolerance
            && std::abs ((int) a.getBlue() - (int) b.getBlue()) <= tolerance;
    }

    /** O cartão onde as peças são montadas. */
    struct Stage : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            g.fillAll (colour::panel);
            g.setColour (colour::textFaint);
            g.setFont (captionFont());

            const auto title = [&g] (int x, int y, const juce::String& text)
            {
                g.drawText (text, x, y, 320, 12, juce::Justification::centredLeft);
            };

            title (metric::pad, 8, "BOTÕES"_t);
            title (metric::pad, 72, "SEGMENTOS E LISTAS");
            title (metric::pad, 160, "PLACA DE NOME, SAÍDA, KNOB E FADER"_t);
            title (metric::pad, 300, "ÍCONES"_t);
            title (520, 8, "CARTÃO COM SETA (CALLOUTBOX)"_t);
        }
    };

    /** Placas de nome e pílulas de saída, como no channel rack e no mixer. */
    struct Plates : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            drawNamePlate (g, { 0, 0, 150, 30 }, "Kick", colour::signal, false, false, type::body);
            drawNamePlate (g, { 0, 38, 150, 30 }, "808", juce::Colour (0xffffffff), true, false, type::body);
            drawNamePlate (g, { 0, 76, 150, 30 }, "Sem sample", juce::Colour (0xffa6a6a6), false, true, type::body);
            drawRoutePill (g, { 160, 4, 112, 24 }, "Master", false, true, type::small);
            drawRoutePill (g, { 160, 42, 112, 24 }, "Bateria", true, false, type::small);
        }
    };

    /** Conteúdo do cartão com seta: o mesmo esqueleto de um painel de ajustes. */
    struct CardContent : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            g.fillAll (colour::panel);
            auto r = getLocalBounds().reduced (metric::pad, 12);
            g.setColour (colour::text);
            g.setFont (font (type::title, Weight::semibold));
            g.drawText ("808", r.removeFromTop (22), juce::Justification::centredLeft);
            g.setColour (colour::textDim);
            g.setFont (font (type::small));
            g.drawText ("Sampler  808.wav", r.removeFromTop (18), juce::Justification::centredLeft);
            r.removeFromTop (10);
            g.setColour (colour::textFaint);
            g.setFont (captionFont());
            g.drawText ("RAIZ", r.removeFromTop (14), juce::Justification::centredLeft);
        }
    };
}

void runDesignSelfTest (SelfTestContext& ctx)
{
    using namespace theme;
    auto& rep = ctx.rep;
    rep.section ("Design: galeria da base visual (docs/design.md)");

    LookAndFeel lf;
    Stage stage;
    stage.setLookAndFeel (&lf);
    stage.setSize (760, 520);

    // botões: ghost, filled, ligado (laranja), ligado neutro e desabilitado
    IconButton file { "Arquivo", Icon::none, "Arquivo" };
    IconButton undo { "Desfazer", Icon::undo };
    IconButton play { "Tocar", Icon::play, {}, IconButton::Style::filled };
    IconButton loop { "Loop", Icon::loop };
    IconButton plugins { "Plugins", Icon::plugin, "Plugins" };
    IconButton off { "Detectar", Icon::note, "Detectar", IconButton::Style::filled };
    IconButton rec { "Gravar", Icon::record, {}, IconButton::Style::filled };
    file.setShowsMenuArrow (true);
    rec.setToggleState (true, juce::dontSendNotification);
    rec.setAlert (1.0f);   // gravando agora
    loop.setToggleState (true, juce::dontSendNotification);
    plugins.setNeutralWhenOn (true);
    plugins.setToggleState (true, juce::dontSendNotification);
    off.setEnabled (false);

    int x = metric::pad;
    for (auto* b : { &file, &undo, &play, &loop, &plugins, &off, &rec })
    {
        stage.addAndMakeVisible (b);
        b->setBounds (x, 24, b->getIdealWidth (metric::control), metric::control);
        x = b->getRight() + metric::gap;
    }

    // segmentos e listas
    SegmentedControl tools, mode { true };
    tools.setItems ({ { "Desenhar", Icon::pencil, {} }, { "Pintar", Icon::brush, {} }, { "Selecionar", Icon::select, {} } });
    mode.setItems ({ { "PAT", Icon::none, {} }, { "SONG", Icon::none, {} } });
    mode.setSelected (1);
    juce::ComboBox snap, key;
    snap.addItem ("Grade: passo", 1);
    snap.setSelectedId (1, juce::dontSendNotification);
    key.addItem ("C", 1);
    key.setSelectedId (1, juce::dontSendNotification);
    styleComboBox (snap, Icon::magnet);
    styleComboBox (key, Icon::note);

    stage.addAndMakeVisible (tools);
    tools.setBounds (metric::pad, 88, tools.getIdealWidth(), metric::control);
    stage.addAndMakeVisible (mode);
    mode.setBounds (tools.getRight() + metric::gap, 88, 112, metric::control);
    stage.addAndMakeVisible (snap);
    snap.setBounds (metric::pad, 88 + 40, 156, metric::control);
    stage.addAndMakeVisible (key);
    key.setBounds (snap.getRight() + metric::gap, 88 + 40, 84, metric::control);

    // placa, saída, knobs (normal e "accent") e fader
    Plates plates;
    stage.addAndMakeVisible (plates);
    plates.setBounds (metric::pad, 176, 280, 108);

    juce::Slider pan (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    juce::Slider gr (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    juce::Slider fader (juce::Slider::LinearVertical, juce::Slider::NoTextBox);
    pan.setRange (-1.0, 1.0);
    pan.setValue (-0.35);
    pan.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    gr.setRange (0.0, 1.0);
    gr.setValue (0.6);
    gr.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    gr.getProperties().set ("accent", true);
    fader.setRange (-100.0, 6.0);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setValue (-6.0);

    stage.addAndMakeVisible (pan);
    pan.setBounds (312, 180, 40, 40);
    stage.addAndMakeVisible (gr);
    gr.setBounds (360, 180, 40, 40);
    stage.addAndMakeVisible (fader);
    fader.setBounds (416, 168, 40, 120);

    // pílulas de escolha e quantidade (vieram do painel Ideias)
    ChoiceChips chips;
    chips.setItems ({ "Trap", "Drill", "R&B", "Pop", "Lo-fi", "Boom bap" });
    chips.setSelected (1);
    AmountSlider amount ("Variação"_t);
    amount.setValue (0.65);
    stage.addAndMakeVisible (chips);
    chips.setBounds (metric::pad, 300, 300, chips.getHeightForWidth (300));
    stage.addAndMakeVisible (amount);
    amount.setBounds (340, 300, 300, 28);

    // cartão com seta apontando para uma placa de nome
    juce::Component anchor;
    stage.addAndMakeVisible (anchor);
    anchor.setBounds (520, 40, 150, 30);
    CardContent card;
    card.setSize (216, 110);
    juce::CallOutBox callout (card, anchor.getBounds(), &stage);

    // folha de ícones
    struct Icons : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            for (int i = 1; i <= (int) Icon::warning; ++i)
            {
                const int col = (i - 1) % 18, row = (i - 1) / 18;
                icons::draw (g, (Icon) i, juce::Rectangle<float> ((float) col * 40.0f, (float) row * 40.0f, 24.0f, 24.0f),
                             theme::colour::textDim);
            }
        }
    } iconSheet;
    stage.addAndMakeVisible (iconSheet);
    iconSheet.setBounds (metric::pad, 384, 720, 128);

    // Tudo que se lê pixel a pixel é imagem de software: no Windows a imagem nativa é do Direct2D (GPU),
    // e ler dela com um Graphics aberto travou o autoteste (16/10, 17:31).
    const auto image = juce::SoftwareImageType().convert (stage.createComponentSnapshot (stage.getLocalBounds(), true, 1.0f));

    // --- conferências
    auto pixelIn = [&image] (const juce::Component& c, int dx)
    {
        return image.getPixelAt (c.getX() + dx, c.getBounds().getCentreY());
    };

    rep.check (isOrange (pixelIn (loop, 3)), "botão ligado pinta o laranja da marca (fundo signalTint)"_t);
    rep.check (isGrey (pixelIn (plugins, 3)) && pixelIn (plugins, 3).getBrightness() > colour::panel.getBrightness() + 0.04f,
               "botão que abre painel, ligado com setNeutralWhenOn, fica cinza claro, sem laranja"_t);
    rep.check (! isOrange (pixelIn (off, 3)), "botão desabilitado não acende"_t);
    rep.check (isRed (image.getPixelAt (rec.getBounds().getCentreX(), rec.getBounds().getCentreY())),
               "gravando agora (setAlert) pinta o vermelho funcional, não o laranja"_t);

    const auto* content = &card;
    const auto cardEdge = image.getPixelAt (callout.getX() + content->getX() - 2, callout.getY() + content->getBounds().getCentreY());
    rep.check (juce::approximatelyEqual (lf.getCallOutBoxCornerSize (callout), metric::radiusCard),
               "cartão com seta usa cantos de 12 px (radiusCard)"_t);
    rep.check (near (cardEdge, colour::panel, 4),
               "cartão com seta pinta o fundo dos cartões (panel) em volta do conteúdo, não o padrão do JUCE: "_t
               + cardEdge.toDisplayString (false));

    int blankIcons = 0;
    for (int i = 1; i <= (int) Icon::warning; ++i)
    {
        juce::Image glyph (juce::Image::ARGB, 24, 24, true, juce::SoftwareImageType());

        {
            juce::Graphics g (glyph);   // fecha antes da leitura
            icons::draw (g, (Icon) i, { 0.0f, 0.0f, 24.0f, 24.0f }, juce::Colours::white);
        }

        int inked = 0;
        for (int py = 0; py < 24; ++py)
            for (int px = 0; px < 24; ++px)
                if (glyph.getPixelAt (px, py).getAlpha() > 64) ++inked;
        if (inked < 12) ++blankIcons;
    }
    rep.check (blankIcons == 0, juce::String ((int) Icon::warning) + " ícones desenham alguma coisa (vazios: "_t + juce::String (blankIcons) + ")");

    const auto png = ctx.work.getChildFile ("design-galeria.png");
    png.deleteFile();
    {
        juce::FileOutputStream out (png);
        juce::PNGImageFormat format;
        rep.check (out.openedOk() && format.writeImageToStream (image, out), "galeria gravada em " + png.getFullPathName());
    }

    stage.removeAllChildren();
    stage.setLookAndFeel (nullptr);

    //==========================================================================
    // Temas de cores (docs/design.md): uma amostra de cada lado a lado, o contraste do texto e a troca ao vivo
    rep.section ("Design: temas de cores (WHYNOT, Claro, Colorido, Frio)");

    auto luminance = [] (juce::Colour c)
    {
        auto channel = [] (juce::uint8 v)
        {
            const double s = v / 255.0;
            return s <= 0.03928 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
        };

        return 0.2126 * channel (c.getRed()) + 0.7152 * channel (c.getGreen()) + 0.0722 * channel (c.getBlue());
    };

    auto contrast = [&luminance] (juce::Colour a, juce::Colour b)
    {
        const double la = luminance (a), lb = luminance (b);
        return (juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05);
    };

    struct Sample : juce::Component
    {
        IconButton play { "Tocar", Icon::play, {}, IconButton::Style::filled };
        IconButton loop { "Loop", Icon::loop };
        SegmentedControl mode { true };

        Sample()
        {
            loop.setToggleState (true, juce::dontSendNotification);
            mode.setItems ({ { "PAT", Icon::none, {} }, { "SONG", Icon::none, {} } });
            mode.setSelected (1);

            for (auto* c : { (juce::Component*) &play, (juce::Component*) &loop, (juce::Component*) &mode })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            play.setBounds (16, 42, 32, 32);
            loop.setBounds (52, 42, 32, 32);
            mode.setBounds (92, 42, 76, 32);
        }

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            g.fillAll (colour::background);
            const auto card = getLocalBounds().toFloat().reduced (4.0f, 0.0f).withTrimmedTop (4.0f);
            g.setColour (colour::panel);
            g.fillRoundedRectangle (card, metric::radiusCard);

            g.setColour (colour::text);
            g.setFont (font (type::title, Weight::semibold));
            g.drawText (schemeName (currentScheme()), juce::Rectangle<float> (16.0f, 10.0f, 152.0f, 24.0f), juce::Justification::centredLeft);

            drawNamePlate (g, { 16.0f, 84.0f, 152.0f, 26.0f }, "Kick", colour::signal, false, false, type::body);

            for (int i = 0; i < 5; ++i)   // passos: ligado e desligado
            {
                g.setColour (i % 3 == 0 ? colour::signal : colour::raised);
                g.fillRoundedRectangle ({ 16.0f + (float) i * 19.0f, 118.0f, 16.0f, 16.0f }, metric::radiusSmall);
            }

            // uma nota do piano roll com o nome: a tinta sobre o sinal
            const juce::Rectangle<float> note (112.0f, 118.0f, 56.0f, 16.0f);
            g.setColour (colour::signal);
            g.fillRoundedRectangle (note, 3.0f);
            g.setColour (colour::onSignal);
            g.setFont (font (12.0f, Weight::semibold));
            g.drawText ("C3", note.withTrimmedLeft (5.0f), juce::Justification::centredLeft, false);

            g.setFont (font (type::small));
            g.setColour (colour::textDim);
            g.drawText ("Rótulo"_t, juce::Rectangle<float> (16.0f, 140.0f, 70.0f, 16.0f), juce::Justification::centredLeft);
            g.setColour (colour::textFaint);
            g.drawText ("Dica", juce::Rectangle<float> (90.0f, 140.0f, 70.0f, 16.0f), juce::Justification::centredLeft);
        }
    };

    constexpr int sampleW = 184, sampleH = 164;
    juce::Image strip (juce::Image::ARGB, sampleW * (int) std::size (theme::allSchemes), sampleH, true, juce::SoftwareImageType());
    juce::String contrasts;
    bool readable = true, functionalRed = true;
    int stripX = 0;

    for (auto scheme : theme::allSchemes)
    {
        theme::applyScheme (scheme);
        const auto& p = theme::palette (scheme);

        LookAndFeel themed;
        Sample sample;
        sample.setLookAndFeel (&themed);
        sample.setSize (sampleW, sampleH);
        const auto shot = juce::SoftwareImageType().convert (sample.createComponentSnapshot (sample.getLocalBounds(), true, 1.0f));

        {
            juce::Graphics g (strip);   // fecha antes da leitura
            g.drawImageAt (shot, stripX, 0);
        }

        sample.setLookAndFeel (nullptr);
        stripX += sampleW;

        // WCAG: texto 7:1, rótulo e dica 4,5:1 (a revisão de iniciante de 22/10 pediu dica legível) e o sinal 3:1
        const double t = contrast (p.text, p.panel), d = contrast (p.textDim, p.panel), f = contrast (p.textFaint, p.panel),
                     s = contrast (p.signal, p.panel);
        const double ink = contrast (p.onSignal, p.signal);   // o nome da nota sobre o sinal
        readable = readable && t >= 7.0 && d >= 4.5 && f >= 4.5 && s >= 3.0 && ink >= 4.5 && d > f;
        functionalRed = functionalRed && p.meterHigh == juce::Colour (0xffff3b30);
        contrasts << theme::schemeName (scheme) << " " << juce::String (t, 1) << "/" << juce::String (d, 1) << "/"
                  << juce::String (f, 1) << "/" << juce::String (s, 1) << "/" << juce::String (ink, 1) << "  ";
    }

    rep.check (readable, "texto legível nos quatro temas (texto/rótulo/dica/sinal sobre o cartão, e a nota sobre o sinal): "_t
                             + contrasts.trim());
    rep.check (functionalRed, "o vermelho funcional (clipe, erro, gravando) é o mesmo em todos os temas"_t);

    // troca ao vivo: a cor guardada com setColour segue o token, com o mesmo alfa
    theme::applyScheme (theme::ColourScheme::whynot);
    juce::Label probe;
    probe.setColour (juce::Label::textColourId, theme::colour::textDim);
    probe.setColour (juce::Label::backgroundColourId, theme::colour::signal.withAlpha (0.35f));
    theme::applyScheme (theme::ColourScheme::light);
    theme::reskin (probe, theme::ColourScheme::whynot);
    const auto& light = theme::palette (theme::ColourScheme::light);
    rep.check (probe.findColour (juce::Label::textColourId) == light.textDim
                   && probe.findColour (juce::Label::backgroundColourId) == light.signal.withAlpha (0.35f),
               "trocar de tema refaz as cores guardadas pelos componentes (WHYNOT para Claro, com o alfa)"_t);

    rep.check (theme::schemeFromKey ("frio") == theme::ColourScheme::cool && ! theme::schemeFromKey ("roxo").has_value(),
               "o nome salvo do tema volta para o tema certo; um nome desconhecido é ignorado"_t);

    theme::applyScheme (theme::ColourScheme::whynot);   // o resto do autoteste segue no padrão

    // a tela de abertura, desenhada fora da tela: o preto da marca e a logo branca no meio
    const auto splash = SplashWindow::preview();
    int logoInk = 0;   // pixels claros na área da logo (225..335, 52..184)

    for (int py = 52; py < 184; py += 2)
        for (int px = 225; px < 335; px += 2)
            if (splash.getPixelAt (px, py).getBrightness() > 0.5f)
                ++logoInk;
    rep.check (splash.getWidth() == SplashWindow::width && splash.getPixelAt (4, 4) == juce::Colour (0xff000000),
               "a tela de abertura é o cartão preto da marca, de 560 x 340 px"_t);
    rep.check (logoInk > 150, "a logo branca aparece no meio da tela de abertura ("_t + juce::String (logoInk) + " pontos claros)");

    //==========================================================================
    // Revisão de iniciante (22/10): o "Comece aqui", o erro que fica e os nomes das notas em português
    rep.section ("Design: para quem nunca usou (Comece aqui, erros, notas)"_t);

    {
        LookAndFeel themed;
        WelcomeCard welcome;
        welcome.setLookAndFeel (&themed);
        welcome.setSize (WelcomeCard::width, WelcomeCard::height);
        const auto shot = juce::SoftwareImageType().convert (welcome.createComponentSnapshot (welcome.getLocalBounds(), true, 1.0f));
        welcome.setLookAndFeel (nullptr);

        // o número do primeiro passo, no círculo do sinal: procura o laranja dentro do círculo (24 px em 34, 89)
        int signalInk = 0;

        for (int py = 89 - 12; py < 89 + 12; ++py)
            for (int px = 34 - 12; px < 34 + 12; ++px)
                if (const auto c = shot.getPixelAt (px, py); c.getRed() > 200 && c.getGreen() > 70 && c.getBlue() < 110)
                    ++signalInk;

        rep.check (signalInk > 8, "Comece aqui: os passos têm o número no laranja do sinal ("_t + juce::String (signalInk) + " pontos)");

        const auto welcomePng = ctx.work.getChildFile ("design-comece-aqui.png");
        welcomePng.deleteFile();
        juce::FileOutputStream out (welcomePng);
        juce::PNGImageFormat format;
        rep.check (out.openedOk() && format.writeImageToStream (shot, out), "Comece aqui gravado em " + welcomePng.getFullPathName());
    }

    {
        LookAndFeel themed;
        StatusBar bar;
        bar.setLookAndFeel (&themed);
        bar.setSize (900, metric::status);
        bar.setRightText ("Interface 100%   Ctrl+= / Ctrl+-");
        bar.showError ("Sem saída de som: a placa de som não abriu."_t, "Abrir Áudio e MIDI"_t, [] {});
        const bool errorShowsButtons = bar.hasError() && bar.getChildComponent (0)->isVisible() && bar.getChildComponent (1)->isVisible();
        const auto shot = juce::SoftwareImageType().convert (bar.createComponentSnapshot (bar.getLocalBounds(), true, 1.0f));
        const auto pill = shot.getPixelAt (metric::pad + 4, metric::status / 2);
        bar.showInfo ("Projeto salvo");
        const bool infoHasNoButtons = ! bar.hasPill() && ! bar.getChildComponent (1)->isVisible();
        bar.setLookAndFeel (nullptr);

        rep.check (errorShowsButtons && infoHasNoButtons,
                   "erro na barra de status: fica com o × e o botão da saída; o aviso comum não tem botão"_t);
        rep.check (pill.getRed() > pill.getGreen() + 8, "o erro tem o fundo no vermelho funcional ("_t + pill.toDisplayString (false) + ")");

        const auto errorPng = ctx.work.getChildFile ("design-erro.png");
        errorPng.deleteFile();
        juce::FileOutputStream out (errorPng);
        juce::PNGImageFormat format;
        rep.check (out.openedOk() && format.writeImageToStream (shot, out), "barra de status com erro gravada em " + errorPng.getFullPathName());
    }

    rep.check (noteNamePt (60) == "Dó4"_t && noteNamePt (66) == "Fá#4"_t && noteNamePt (57, false) == "Lá"_t,
               "nomes das notas em português, na oitava do C (Dó4 = C4, Fá#4, Lá)"_t);

    const auto splashPng = ctx.work.getChildFile ("design-abertura.png");
    splashPng.deleteFile();
    {
        juce::FileOutputStream out (splashPng);
        juce::PNGImageFormat format;
        rep.check (out.openedOk() && format.writeImageToStream (splash, out), "tela de abertura gravada em " + splashPng.getFullPathName());
    }

    const auto themesPng = ctx.work.getChildFile ("design-temas.png");
    themesPng.deleteFile();
    {
        juce::FileOutputStream out (themesPng);
        juce::PNGImageFormat format;
        rep.check (out.openedOk() && format.writeImageToStream (strip, out), "amostra dos temas gravada em " + themesPng.getFullPathName());
    }
}

} // namespace whynot::selftest
