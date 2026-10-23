#include "WelcomeCard.h"
#include "Theme.h"

namespace whynot
{

WelcomeCard::Step::Step (int n, juce::String t, juce::String d)
    : juce::Button (t), number (n), title (std::move (t)), detail (std::move (d))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

void WelcomeCard::Step::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat();

    if (highlighted || down)
    {
        g.setColour (down ? colour::raisedHigh.brighter (0.08f) : colour::raisedHigh);
        g.fillRoundedRectangle (r, metric::radius);
    }

    // o número do passo num círculo, no laranja da marca: é o caminho a seguir
    auto badge = r.removeFromLeft (44.0f).withSizeKeepingCentre (24.0f, 24.0f);
    g.setColour (colour::signalTint);
    g.fillEllipse (badge);
    g.setColour (colour::signal);
    g.setFont (font (type::body, Weight::semibold));
    g.drawText (juce::String (number), badge, juce::Justification::centred, false);

    r.removeFromRight (8.0f);
    auto lines = r.withSizeKeepingCentre (r.getWidth(), 36.0f);
    g.setColour (colour::text);
    g.setFont (font (type::body, Weight::semibold));
    g.drawText (title, lines.removeFromTop (18.0f), juce::Justification::centredLeft, true);
    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawText (detail, lines, juce::Justification::centredLeft, true);
}

//==============================================================================
WelcomeCard::WelcomeCard()
    : beat (1, "Pôr um beat de exemplo"_t, "Kick, clap e hats. Depois, aperte o play."_t),
      voice (2, "Gravar a sua voz"_t, "No mixer, aperte REC e depois GRAVAR."_t),
      exportMp3 (3, "Exportar em MP3"_t, "Para mandar no WhatsApp ou postar."_t)
{
    beat.onClick = [this] { if (onBeat) onBeat(); };
    voice.onClick = [this] { if (onVoice) onVoice(); };
    exportMp3.onClick = [this] { if (onExport) onExport(); };
    closeButton.onClick = [this] { if (onClose) onClose (false); };
    hideButton.onClick = [this] { if (onClose) onClose (true); };

    for (juce::Component* c : { (juce::Component*) &beat, (juce::Component*) &voice, (juce::Component*) &exportMp3,
                                (juce::Component*) &closeButton, (juce::Component*) &hideButton })
    {
        addAndMakeVisible (c);
        c->setWantsKeyboardFocus (false);
    }

    setSize (width, height);
    shadow.setOwner (this);
}

void WelcomeCard::setCollapsed (bool c)
{
    if (c == collapsed)
        return;

    collapsed = c;
    const auto size = idealSize();
    setBounds (getRight() - size.x, getY(), size.x, size.y);   // o canto de cima à direita fica no lugar

    for (juce::Component* child : { (juce::Component*) &beat, (juce::Component*) &voice, (juce::Component*) &exportMp3,
                                    (juce::Component*) &closeButton, (juce::Component*) &hideButton })
        child->setVisible (! collapsed);

    setMouseCursor (collapsed ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip (collapsed ? "Comece aqui: os três primeiros passos (clique para abrir)"_t : juce::String());
    repaint();
}

void WelcomeCard::resized()
{
    if (collapsed)
        return;

    auto r = getLocalBounds().reduced (12, 0);
    closeButton.setBounds (getWidth() - 12 - 24, 10, 24, 24);

    r.removeFromTop (64);   // legenda e título
    for (auto* step : { &beat, &voice, &exportMp3 })
    {
        step->setBounds (r.removeFromTop (50));
        r.removeFromTop (2);
    }

    hideButton.setBounds (r.removeFromBottom (36).withSizeKeepingCentre (r.getWidth(), 26)
                              .withWidth (hideButton.getIdealWidth (26)));
}

void WelcomeCard::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto r = getLocalBounds().toFloat();
    const float radius = collapsed ? r.getHeight() * 0.5f : metric::radiusCard;

    g.setColour (colour::floating);
    g.fillRoundedRectangle (r, radius);
    g.setColour (colour::floatingEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

    if (collapsed)
    {
        // a pílula: o mesmo número do caminho e o nome; abre com um clique
        auto row = r.reduced (12.0f, 0.0f);
        const auto badge = row.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
        g.setColour (colour::signalTint);
        g.fillEllipse (badge);
        g.setColour (colour::signal);
        g.setFont (font (type::small, Weight::semibold));
        g.drawText ("3", badge, juce::Justification::centred, false);
        row.removeFromLeft (8.0f);
        g.setColour (colour::text);
        g.setFont (font (type::body, Weight::semibold));
        g.drawText ("Comece aqui", row.withTrimmedRight (18.0f), juce::Justification::centredLeft, true);
        icons::draw (g, Icon::chevronDown, row.removeFromRight (14.0f).withSizeKeepingCentre (14.0f, 14.0f), colour::textDim);
        return;
    }

    g.setColour (colour::textFaint);
    g.setFont (captionFont());
    g.drawText ("COMECE AQUI", juce::Rectangle<float> (16.0f, 12.0f, 200.0f, 18.0f), juce::Justification::centredLeft, false);

    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Seu primeiro beat em 3 passos", juce::Rectangle<float> (16.0f, 32.0f, r.getWidth() - 32.0f, 24.0f),
                juce::Justification::centredLeft, true);
}

void WelcomeCard::mouseUp (const juce::MouseEvent& e)
{
    if (collapsed && getLocalBounds().contains (e.getPosition()))
        setCollapsed (false);
}

} // namespace whynot
