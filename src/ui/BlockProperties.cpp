#include "BlockProperties.h"

#include "Theme.h"
#include "../app/AppController.h"

namespace whynot
{

namespace
{
    const char* curveIds[] { "linear", "convex", "concave", "s" };

    juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();
        for (auto& [k, v] : kv) o->setProperty (k, v);
        return juce::var (o);
    }

    juce::String panText (double v)
    {
        const int n = juce::roundToInt (v);
        return n == 0 ? juce::String ("C") : (n < 0 ? "E " : "D ") + juce::String (std::abs (n));
    }

    /** Número com vírgula, como no resto do app ("-3,5 dB"); aceita ponto ou vírgula ao digitar. */
    juce::String decimalText (double v)   { return juce::String (v, 1).replaceCharacter ('.', ','); }

    double decimalValue (const juce::String& text)
    {
        return text.replaceCharacter (',', '.').retainCharacters ("-0123456789.").getDoubleValue();
    }

    double panValue (const juce::String& text)
    {
        const auto t = text.trim().toUpperCase();
        const double n = std::abs (t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue());
        return t.startsWithChar ('E') || t.startsWithChar ('-') ? -n : n;
    }
}

BlockProperties::BlockProperties (AppController& a, const juce::String& i) : app (a), item (i)
{
    using namespace theme;
    title.setText ("Propriedades do bloco"_t, juce::dontSendNotification);
    title.setFont (font (type::body, Weight::semibold));
    title.setColour (juce::Label::textColourId, colour::text);
    subtitle.setFont (font (type::small));
    subtitle.setColour (juce::Label::textColourId, colour::textDim);

    for (auto [label, text] : { std::pair { &nameLabel, "Nome" }, std::pair { &colourLabel, "Cor" }, std::pair { &gainLabel, "Ganho" },
                                std::pair { &panLabel, "Pan" }, std::pair { &pitchLabel, "Tom e fina" },
                                std::pair { &fadeInLabel, "Fade de entrada" }, std::pair { &fadeOutLabel, "Fade de saída" } })
    {
        label->setText (juce::String::fromUTF8 (text), juce::dontSendNotification);
        label->setFont (font (type::small));
        label->setColour (juce::Label::textColourId, colour::textDim);
        addAndMakeVisible (*label);
    }

    name.setFont (font (type::body));
    name.setColour (juce::TextEditor::backgroundColourId, colour::raised);
    name.setColour (juce::TextEditor::outlineColourId, colour::line);
    name.setColour (juce::TextEditor::textColourId, colour::text);
    name.setTooltip ("O nome do canal deste áudio (o do cabeçalho). Enter grava."_t);
    name.onReturnKey = [this] { commitName(); };
    name.onFocusLost = [this] { commitName(); };
    name.onEscapeKey = [this]
    {
        const auto& p = app.getModel().get();

        if (const auto* i = p.findItem (item))
            if (const auto* ch = p.findChannel (i->ref))
                name.setText (ch->name, juce::dontSendNotification);   // Esc desiste do nome digitado
    };

    gain.setRange (-60.0, 24.0, 0.1);
    gain.setTextValueSuffix (" dB");
    gain.textFromValueFunction = decimalText;
    gain.valueFromTextFunction = decimalValue;
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setTooltip ("O ganho do bloco (a alça de baixo). Duplo clique volta a 0 dB."_t);
    pan.setRange (-100.0, 100.0, 1.0);
    pan.textFromValueFunction = panText;
    pan.valueFromTextFunction = panValue;
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setTooltip ("Pan do bloco: E (esquerda), C (centro), D (direita). Duplo clique centraliza."_t);
    semitones.setRange (-24.0, 24.0, 1.0);
    semitones.setTextValueSuffix (" st");
    semitones.setDoubleClickReturnValue (true, 0.0);
    semitones.setTooltip ("Tom do bloco em semitons, sem mudar o tempo."_t);
    cents.setRange (-50.0, 50.0, 1.0);
    cents.setTextValueSuffix (" ct");
    cents.setDoubleClickReturnValue (true, 0.0);
    cents.setTooltip ("Afinação fina em centésimos de semitom."_t);

    for (auto* s : { &fadeIn, &fadeOut })
    {
        s->setRange (0.0, 1000.0, 1.0);
        s->setTextValueSuffix (" ms");
        s->setDoubleClickReturnValue (true, 0.0);
    }

    struct Wire { WheelSlider* slider; const char* gesture; std::function<juce::var (double)> params; };
    const Wire wires[] {
        { &gain, "Ganho do bloco", [this] (double v) { return args ({ { "item", item }, { "gainDb", v } }); } },
        { &pan, "Pan do bloco", [this] (double v) { return args ({ { "item", item }, { "pan", v / 100.0 } }); } },
        { &semitones, "Tom do bloco", [this] (double v) { return args ({ { "item", item }, { "semitones", v }, { "cents", cents.getValue() } }); } },
        { &cents, "Afinação fina do bloco", [this] (double v) { return args ({ { "item", item }, { "semitones", semitones.getValue() }, { "cents", v } }); } },
        { &fadeIn, "Fade do bloco", [this] (double v) { return args ({ { "item", item }, { "fadeIn", v / 1000.0 } }); } },
        { &fadeOut, "Fade do bloco", [this] (double v) { return args ({ { "item", item }, { "fadeOut", v / 1000.0 } }); } },
    };

    for (auto& w : wires)
    {
        auto* s = w.slider;
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        // tom e fina dividem a linha: a caixa de texto menor deixa o trilho com largura para arrastar (revisão de 28/10)
        const bool half = s == &semitones || s == &cents;
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, half ? 54 : 72, metric::control - 6);
        s->setColour (juce::Slider::trackColourId, colour::signal);        // as cores da casa, não o azul do JUCE
        s->setColour (juce::Slider::backgroundColourId, colour::raised);
        s->setColour (juce::Slider::thumbColourId, colour::text);
        s->setColour (juce::Slider::textBoxBackgroundColourId, colour::raised);
        s->setColour (juce::Slider::textBoxOutlineColourId, colour::line);
        s->setColour (juce::Slider::textBoxTextColourId, colour::text);

        // um arrasto do começo ao fim é um passo de desfazer só (o texto digitado, um passo por vez)
        const juce::String gesture = juce::String::fromUTF8 (w.gesture);
        s->onDragStart = [this, s, gesture]
        {
            if (s->wheeling)
                return;   // a rodinha: o passo é o do wheelTurned

            if (wheelSlider != nullptr)
                timerCallback();   // a rodinha de antes fecha o passo dela; o arrasto é outro

            if (! inGesture) { inGesture = true; app.getCommands().beginGesture (gesture); }
        };
        s->onWheel = [this, s, gesture] { wheelTurned (s, gesture); };
        s->onDragEnd = [this, s] { if (! s->wheeling && inGesture) { inGesture = false; app.getCommands().endGesture(); } };
        s->onValueChange = [this, s, params = w.params] { if (! updating) run (params (s->getValue()), s); };
        addAndMakeVisible (*s);
    }

    for (auto* box : { &fadeInCurve, &fadeOutCurve })
    {
        box->addItemList ({ "Reta"_t, "Rápida"_t, "Lenta"_t, "Em S"_t }, 1);
        box->setTooltip ("A curva do fade: rápida sobe logo (a de mesma potência), lenta demora a subir."_t);
        box->onChange = [this, box]
        {
            if (! updating && box->getSelectedId() > 0)
                run (args ({ { "item", item }, { box == &fadeInCurve ? "inCurve" : "outCurve", curveIds[box->getSelectedId() - 1] } }));
        };
        addAndMakeVisible (*box);
    }

    reverse.setTooltip ("O bloco toca de trás para frente (o arquivo invertido fica ao lado do original)."_t);
    mute.setTooltip ("Mudo só neste bloco."_t);
    // mudo e travado não são sinal: o visto fica branco, não laranja (docs/design.md, "Mudo nunca é laranja")
    mute.setColour (juce::ToggleButton::tickColourId, theme::colour::text);
    lock.setColour (juce::ToggleButton::tickColourId, theme::colour::text);
    lock.setTooltip ("Travado: o mouse não move, não apara, não corta nem apaga o bloco."_t);
    reverse.onClick = [this]
    {
        if (updating)
            return;

        if (const auto r = app.run ("audio.reverse", args ({ { "item", item } })); ! r.ok)
            refresh (app.getModel().get());
    };
    mute.onClick = [this] { if (! updating) run (args ({ { "item", item }, { "muted", mute.getToggleState() } })); };
    lock.onClick = [this] { if (! updating) run (args ({ { "item", item }, { "locked", lock.getToggleState() } })); };

    for (juce::Component* c : { (juce::Component*) &title, (juce::Component*) &subtitle, (juce::Component*) &name,
                                (juce::Component*) &reverse, (juce::Component*) &mute, (juce::Component*) &lock })
        addAndMakeVisible (c);

    setSize (420, 2 * metric::gap + 22 + 18 + 4 + 8 * (metric::control + rowGap));
    refresh (app.getModel().get());
}

BlockProperties::~BlockProperties()
{
    stopTimer();
    name.onFocusLost = nullptr;
    wheelSlider = nullptr;

    if (inGesture)   // fechou no meio de um arrasto ou da rodinha: o gesto termina aqui, com o que já mudou
        app.getCommands().endGesture();

    // Fechar com um clique fora não tira o foco do nome antes: o nome digitado grava aqui (pendência da fatia 6).
    // O Esc já voltou o texto ao nome do canal. Saindo do app, não: o AppController pode já ter ido embora.
    closing = true;   // o refresh que o comando provoca não mexe mais neste painel

    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr && ! mm->hasStopMessageBeenSent())
        commitName();
}

void BlockProperties::wheelTurned (juce::Slider* s, const juce::String& gesture)
{
    if (wheelSlider != s)
    {
        if (wheelSlider != nullptr)
            timerCallback();   // a rodinha passou para outro controle: outro passo

        if (inGesture)
            return;   // no meio de um arrasto, a rodinha entra no passo dele

        inGesture = true;
        wheelSlider = s;
        app.getCommands().beginGesture (gesture);
    }

    startTimer (wheelPauseMs);   // cada clique da rodinha empurra o fim do passo
}

void BlockProperties::timerCallback()
{
    stopTimer();

    if (wheelSlider == nullptr)
        return;

    wheelSlider = nullptr;

    if (inGesture)
    {
        inGesture = false;
        app.getCommands().endGesture();
    }
}

void BlockProperties::run (const juce::var& params, const juce::Slider* from)
{
    if (wheelSlider != nullptr && from != wheelSlider)
        timerCallback();   // outro controle: o passo da rodinha fecha antes, para não levar esta mudança junto

    if (const auto r = app.run ("audio.setProperties", params); ! r.ok)
        refresh (app.getModel().get());   // recusado: o painel volta a mostrar o que está no bloco
}

void BlockProperties::commitName()
{
    if (updating)
        return;

    const auto& p = app.getModel().get();
    const auto* i = p.findItem (item);
    const auto* ch = i != nullptr ? p.findChannel (i->ref) : nullptr;
    const auto text = name.getText().trim();

    if (ch != nullptr && text.isNotEmpty() && text != ch->name)
        run (args ({ { "item", item }, { "name", text } }));
    else if (ch != nullptr && text.isEmpty())
        name.setText (ch->name, juce::dontSendNotification);
}

void BlockProperties::chooseColour (int index)
{
    if (juce::isPositiveAndBelow (index, numSwatches))
        run (args ({ { "item", item }, { "colour", channelColourForIndex (index) } }));
}

void BlockProperties::refresh (const Project& p)
{
    if (closing)
        return;

    const auto* i = p.findItem (item);
    const auto* ch = i != nullptr && i->kind == "audio" ? p.findChannel (i->ref) : nullptr;

    if (ch == nullptr)
    {
        // o bloco saiu (apagado, ou o desfazer voltou antes dele): o painel fecha
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();

        return;
    }

    const juce::ScopedValueSetter<bool> guard (updating, true);
    blockSeconds = juce::jmax (0.001, ticksToSeconds (i->length, p.bpm));
    subtitle.setText (juce::String (blockSeconds, 2).replaceCharacter ('.', ',') + " s"
                          + (i->file.isNotEmpty() ? juce::String (" · consolidado"_t) : juce::String())
                          + (i->takeGroup.isNotEmpty() ? " · take "_t + juce::String (i->take) : juce::String()),
                      juce::dontSendNotification);

    if (! name.hasKeyboardFocus (true))
        name.setText (ch->name, juce::dontSendNotification);

    colour = ch->colour;
    gain.setValue (i->gainDb, juce::dontSendNotification);
    pan.setValue (i->pan * 100.0, juce::dontSendNotification);
    const double whole = std::round ((double) i->pitch);
    semitones.setValue (whole, juce::dontSendNotification);
    cents.setValue (std::round (((double) i->pitch - whole) * 100.0), juce::dontSendNotification);

    const double maxMs = std::floor (blockSeconds * 1000.0);

    for (auto [slider, value] : { std::pair { &fadeIn, i->fadeIn }, std::pair { &fadeOut, i->fadeOut } })
    {
        slider->setRange (0.0, juce::jmax (1.0, maxMs), 1.0);
        slider->setValue (value * 1000.0, juce::dontSendNotification);
    }

    fadeInCurve.setSelectedId (juce::jlimit (0, 3, i->fadeInCurve) + 1, juce::dontSendNotification);
    fadeOutCurve.setSelectedId (juce::jlimit (0, 3, i->fadeOutCurve) + 1, juce::dontSendNotification);
    reverse.setToggleState (i->reversed, juce::dontSendNotification);
    mute.setToggleState (i->muted, juce::dontSendNotification);
    lock.setToggleState (i->locked, juce::dontSendNotification);
    repaint();
}

juce::Rectangle<int> BlockProperties::swatchArea (int index) const
{
    const auto row = colourLabel.getBounds().withX (colourLabel.getRight()).withRight (getWidth() - theme::metric::gap);
    const int size = row.getHeight() - 8;
    return { row.getX() + index * (size + 8), row.getY() + 4, size, size };
}

void BlockProperties::mouseUp (const juce::MouseEvent& e)
{
    for (int n = 0; n < numSwatches; ++n)
        if (swatchArea (n).contains (e.getPosition()))
            chooseColour (n);
}

void BlockProperties::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::floating);

    for (int n = 0; n < numSwatches; ++n)
    {
        const auto r = swatchArea (n).toFloat();
        const auto hex = channelColourForIndex (n);
        g.setColour (fromHex (hex));
        g.fillRoundedRectangle (r, metric::radiusSmall);

        if (hex.equalsIgnoreCase (colour))
        {
            g.setColour (colour::text);
            g.drawRoundedRectangle (r.expanded (2.5f), metric::radiusSmall + 2.0f, 1.5f);
        }
        else
        {
            g.setColour (colour::line);
            g.drawRoundedRectangle (r, metric::radiusSmall, 1.0f);
        }
    }
}

void BlockProperties::resized()
{
    using namespace theme;
    auto r = getLocalBounds().reduced (metric::gap);
    title.setBounds (r.removeFromTop (22));
    subtitle.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    auto row = [&r] { auto x = r.removeFromTop (metric::control); r.removeFromTop (rowGap); return x; };

    auto line = row();
    nameLabel.setBounds (line.removeFromLeft (labelWidth));
    name.setBounds (line.reduced (0, 3));

    line = row();
    colourLabel.setBounds (line.removeFromLeft (labelWidth));   // as amostras vão no resto (swatchArea)

    for (auto [label, slider] : { std::pair { &gainLabel, &gain }, std::pair { &panLabel, &pan } })
    {
        line = row();
        label->setBounds (line.removeFromLeft (labelWidth));
        slider->setBounds (line);
    }

    line = row();
    pitchLabel.setBounds (line.removeFromLeft (labelWidth));
    semitones.setBounds (line.removeFromLeft (line.getWidth() / 2).withTrimmedRight (4));
    cents.setBounds (line.withTrimmedLeft (4));

    for (auto [label, slider, box] : { std::tuple { &fadeInLabel, &fadeIn, &fadeInCurve }, std::tuple { &fadeOutLabel, &fadeOut, &fadeOutCurve } })
    {
        line = row();
        label->setBounds (line.removeFromLeft (labelWidth));
        box->setBounds (line.removeFromRight (92).reduced (0, 3));
        slider->setBounds (line.withTrimmedRight (6));
    }

    line = row();
    line.removeFromLeft (labelWidth);
    const int w = line.getWidth() / 3;
    reverse.setBounds (line.removeFromLeft (w));
    mute.setBounds (line.removeFromLeft (w));
    lock.setBounds (line);
}

} // namespace whynot
