#include "StatusBar.h"
#include "Theme.h"

namespace whynot
{

namespace
{
    constexpr int infoMs = 7000;      // o aviso comum some sozinho
    constexpr float pillHeight = 22.0f;

    juce::Font messageFont()   { return theme::font (theme::type::body, theme::Weight::medium); }
    float textWidth (const juce::String& s, const juce::Font& f)   { return juce::GlyphArrangement::getStringWidth (f, s); }
}

StatusBar::StatusBar()
{
    actionButton.setWantsKeyboardFocus (false);
    closeButton.setWantsKeyboardFocus (false);

    actionButton.onClick = [this]
    {
        auto run = action;   // limpar antes: a ação pode abrir uma janela ou mostrar outra mensagem
        clearMessage();

        if (run)
            run();
    };

    closeButton.onClick = [this] { clearMessage(); };

    addChildComponent (actionButton);
    addChildComponent (closeButton);
}

void StatusBar::showInfo (const juce::String& text)
{
    message = text;
    error = note = false;
    action = nullptr;
    startTimer (infoMs);
    resized();
    repaint();
}

void StatusBar::showNote (const juce::String& text, const juce::String& actionLabel, std::function<void()> fn)
{
    showError (text, actionLabel, std::move (fn));
    error = false;
    note = true;
    resized();
    repaint();
}

void StatusBar::showError (const juce::String& text, const juce::String& actionLabel, std::function<void()> fn)
{
    message = text;
    error = true;
    note = false;
    action = std::move (fn);
    actionButton.setLabel (actionLabel);
    actionButton.setTooltip (actionLabel);
    stopTimer();   // o erro fica até alguém fechar
    resized();
    repaint();
}

void StatusBar::clearMessage()
{
    message.clear();
    error = note = false;
    action = nullptr;
    stopTimer();
    resized();
    repaint();
}

void StatusBar::timerCallback()
{
    if (! error && ! note)
        clearMessage();
}

void StatusBar::setStandingProblem (const juce::String& text)
{
    if (text != standing)
    {
        standing = text;
        repaint();
    }
}

void StatusBar::setLamp (const juce::String& text)
{
    if (text != lamp)
    {
        lamp = text;
        repaint();
    }
}

void StatusBar::setRightText (const juce::String& text)
{
    if (text != rightText)
    {
        rightText = text;
        resized();
        repaint();
    }
}

void StatusBar::setMessageLimit (int x)
{
    if (x != messageLimit)
    {
        messageLimit = x;
        resized();
        repaint();
    }
}

//==============================================================================
juce::Rectangle<int> StatusBar::rightArea() const
{
    const int w = juce::roundToInt (textWidth (rightText, theme::font (theme::type::small))) + 4;
    return getLocalBounds().reduced (theme::metric::pad, 0).removeFromRight (w);
}

juce::Rectangle<int> StatusBar::lampArea() const
{
    return { rightArea().getX() - 16 - lampWidth, 0, lampWidth, getHeight() };
}

juce::Rectangle<int> StatusBar::messageArea() const
{
    auto r = getLocalBounds().reduced (theme::metric::pad, 0);
    r.setRight ((lamp.isNotEmpty() ? lampArea().getX() : rightArea().getX()) - 16);

    if (messageLimit >= 0)
        r.setRight (juce::jmax (r.getX(), juce::jmin (r.getRight(), messageLimit)));

    return r;
}

juce::Rectangle<float> StatusBar::pillArea() const
{
    // ícone, texto, botão da saída e o ×, com 10 px de respiro nas pontas
    const auto area = messageArea().toFloat();
    float w = 10.0f + 15.0f + 8.0f + textWidth (message, messageFont()) + 8.0f;

    if (action)
        w += (float) actionButton.getIdealWidth ((int) pillHeight) + 6.0f;

    w += pillHeight + 4.0f;   // o ×
    return area.withWidth (juce::jmin (area.getWidth(), w)).withSizeKeepingCentre (juce::jmin (area.getWidth(), w), pillHeight);
}

void StatusBar::resized()
{
    const bool pill = hasPill();
    closeButton.setVisible (pill);
    actionButton.setVisible (pill && action != nullptr);

    if (! pill)
        return;

    auto box = pillArea().toNearestInt();
    closeButton.setBounds (box.removeFromRight ((int) pillHeight).reduced (1));

    if (action)
    {
        box.removeFromRight (4);
        actionButton.setBounds (box.removeFromRight (actionButton.getIdealWidth ((int) pillHeight)).reduced (0, 1));
    }
}

void StatusBar::paint (juce::Graphics& g)
{
    using namespace theme;

    // à direita: a escala da interface e, quando ligado, a luz do teclado-piano
    g.setColour (colour::textFaint);
    g.setFont (font (type::small));
    g.drawText (rightText, rightArea(), juce::Justification::centredRight, false);

    if (lamp.isNotEmpty())
    {
        const auto r = lampArea().toFloat().reduced (0.0f, 5.0f);
        g.setColour (colour::signalTint);
        g.fillRoundedRectangle (r, metric::radiusSmall);
        g.setColour (colour::signal);
        g.setFont (font (type::small, Weight::medium));
        g.drawText (lamp, r.toNearestInt(), juce::Justification::centred, true);
    }

    auto area = messageArea();

    if (hasPill())
    {
        // o erro: pílula no vermelho funcional suave e o ícone de aviso; a orientação: pílula neutra, com o "i".
        // As duas ficam até alguém fechar.
        const auto pill = pillArea();
        g.setColour (error ? colour::meterHigh.withAlpha (0.16f) : colour::raised);
        g.fillRoundedRectangle (pill, pillHeight * 0.5f);

        auto inner = pill.reduced (10.0f, 0.0f);
        const auto mark = inner.removeFromLeft (15.0f).withSizeKeepingCentre (15.0f, 15.0f);

        if (error)
        {
            icons::draw (g, Icon::warning, mark, colour::meterHigh);
        }
        else
        {
            g.setColour (colour::textDim);
            g.drawEllipse (mark.reduced (1.0f), 1.3f);
            g.setFont (font (10.5f, Weight::bold));
            g.drawText ("i", mark, juce::Justification::centred, false);
        }

        inner.removeFromLeft (8.0f);

        const float right = (actionButton.isVisible() ? (float) actionButton.getX() : (float) closeButton.getX()) - 6.0f;
        g.setColour (colour::text);
        g.setFont (messageFont());
        g.drawText (message, inner.withRight (right), juce::Justification::centredLeft, true);
        return;
    }

    if (message.isNotEmpty())
    {
        g.setColour (colour::textDim);
        g.setFont (messageFont());
        g.drawText (message, area, juce::Justification::centredLeft, true);
        return;
    }

    if (standing.isNotEmpty())
    {
        icons::draw (g, Icon::warning, area.removeFromLeft (16).toFloat().withSizeKeepingCentre (15.0f, 15.0f), colour::warning);
        area.removeFromLeft (8);
        g.setColour (colour::warning);
        g.setFont (messageFont());
        g.drawText (standing, area, juce::Justification::centredLeft, true);
    }
}

} // namespace whynot
