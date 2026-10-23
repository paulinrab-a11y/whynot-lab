#include "AiBridgeUi.h"

#include "Icons.h"
#include "Theme.h"
#include "../common/Text.h"

namespace whynot
{

AiConfirmCard::AiConfirmCard (const juce::String& commandTitle, const juce::String& id, const juce::String& why,
                              const juce::String& extra, int seconds)
    : title (commandTitle), commandId (id), reason (why), details (extra)
{
    // não pega o foco ao aparecer (você pode estar digitando ou tocando o teclado-piano); com um clique nele,
    // o Esc recusa, e as outras teclas seguem para a janela
    setWantsKeyboardFocus (true);

    shownAt = juce::Time::getMillisecondCounterHiRes();
    deadline = shownAt + seconds * 1000.0;

    allowButton.setTooltip ("Deixa a IA fazer isto uma vez"_t);
    refuseButton.setTooltip ("A IA não faz isto; ela fica sabendo que você recusou"_t);
    allowButton.onClick = [this] { answer (true); };
    refuseButton.onClick = [this] { answer (false); };
    allowButton.setEnabled (false);   // acende em armMs

    for (auto* b : { &allowButton, &refuseButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    startTimer (100);
}

void AiConfirmCard::answer (bool allowed)
{
    if (answered || (allowed && ! armed))
        return;

    answered = true;
    stopTimer();

    // quem abriu fecha o cartão, e este componente pode sumir na chamada: nada daqui depois dela
    if (auto callback = onAnswer)
        callback (allowed);
}

void AiConfirmCard::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (! armed && now - shownAt >= armMs)
    {
        armed = true;
        allowButton.setEnabled (true);
    }

    repaint (card().removeFromBottom (64));   // o relógio
}

juce::String AiConfirmCard::remainingText() const
{
    const int left = juce::jmax (0, (int) std::ceil ((deadline - juce::Time::getMillisecondCounterHiRes()) / 1000.0));
    return "Sem resposta em "_t + juce::String (left / 60) + ":" + juce::String (left % 60).paddedLeft ('0', 2) + ", fica recusado"_t;
}

juce::Rectangle<int> AiConfirmCard::boundsIn (juce::Rectangle<int> area)
{
    // no alto, centrado na área, com a margem da sombra em volta
    const int w = juce::jmin (cardWidth, area.getWidth() - 16) + 2 * shadow;
    return juce::Rectangle<int> (area.getCentreX() - w / 2, area.getY() + 12 - shadow / 2, w, cardHeight + 2 * shadow);
}

juce::Rectangle<int> AiConfirmCard::card() const
{
    return getLocalBounds().reduced (shadow);
}

bool AiConfirmCard::hitTest (int x, int y)
{
    return card().contains (x, y);   // a sombra não pega cliques: o que está atrás continua clicável
}

void AiConfirmCard::resized()
{
    using namespace theme;
    auto footer = card().removeFromBottom (64).reduced (metric::pad + 4, 16);
    allowButton.setBounds (footer.removeFromRight (allowButton.getIdealWidth (metric::control)));
    footer.removeFromRight (metric::gap);
    refuseButton.setBounds (footer.removeFromRight (refuseButton.getIdealWidth (metric::control)));
}

void AiConfirmCard::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto c = card();

    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), shadow, { 0, 6 }).drawForRectangle (g, c);
    g.setColour (colour::floating);
    g.fillRoundedRectangle (c.toFloat(), metric::radiusCard);
    g.setColour (colour::floatingEdge);
    g.drawRoundedRectangle (c.toFloat().reduced (0.5f), metric::radiusCard, 1.0f);

    auto r = c.reduced (metric::pad + 4, 0);
    auto head = r.removeFromTop (48);
    icons::draw (g, Icon::sparkle, head.removeFromLeft (18).withSizeKeepingCentre (18, 18).toFloat(), colour::textDim);
    head.removeFromLeft (10);
    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("A IA pede o seu ok"_t, head, juce::Justification::centredLeft, true);

    // o que ela quer: o título do comando e, apagado, o id (é o que aparece no ia.log)
    auto what = r.removeFromTop (20);
    const auto titleFont = font (type::body, Weight::semibold);
    const int titleWidth = juce::jmin (what.getWidth(), juce::GlyphArrangement::getStringWidthInt (titleFont, title) + 2);
    g.setFont (titleFont);
    g.drawText (title, what.removeFromLeft (titleWidth), juce::Justification::centredLeft, true);
    g.setColour (colour::textFaint);
    g.setFont (font (type::small));
    g.drawText ("   " + commandId, what, juce::Justification::centredLeft, true);
    r.removeFromTop (6);

    // o porquê, em até três linhas; os parâmetros logo abaixo dele
    juce::AttributedString why;
    why.append (reason, font (type::body), colour::textDim);
    juce::TextLayout layout;
    layout.createLayout (why, (float) r.getWidth());
    const int whyHeight = juce::jlimit (18, 50, (int) std::ceil (layout.getHeight()));

    g.setColour (colour::textDim);
    g.setFont (font (type::body));
    g.drawFittedText (reason, r.removeFromTop (whyHeight), juce::Justification::topLeft, 3, 1.0f);
    r.removeFromTop (4);

    if (details.isNotEmpty())
    {
        g.setColour (colour::textFaint);
        g.setFont (font (type::small));
        g.drawText (details, r.removeFromTop (18), juce::Justification::centredLeft, true);
    }

    // o relógio, à esquerda dos botões
    auto footer = card().removeFromBottom (64).reduced (metric::pad + 4, 16);
    footer.removeFromRight (allowButton.getWidth() + metric::gap + refuseButton.getWidth() + metric::gap);
    g.setColour (colour::textFaint);
    g.setFont (font (type::small));
    g.drawText (remainingText(), footer, juce::Justification::centredLeft, true);
}

bool AiConfirmCard::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        answer (false);
        return true;
    }

    return false;   // Enter não autoriza: só o clique no Permitir
}

//==============================================================================
AiStatusNotice::AiStatusNotice()
{
    toggleButton.setWantsKeyboardFocus (false);
    toggleButton.onClick = [this] { if (onToggle) onToggle(); };
    addAndMakeVisible (toggleButton);
    setVisible (false);
}

juce::String AiStatusNotice::text() const
{
    switch (state)
    {
        case State::connected:  return "IA conectada"_t;
        case State::waiting:    return "IA esperando o seu ok"_t;
        case State::off:        return "IA desligada"_t;
        case State::hidden:     break;
    }

    return {};
}

void AiStatusNotice::setState (State s)
{
    if (s == state)
        return;

    state = s;
    const bool off = state == State::off;
    toggleButton.setLabel (off ? "Ligar a IA"_t : "Desligar a IA"_t);
    toggleButton.setTooltip (off ? "Volta a aceitar a IA (o servidor MCP liga de novo no próximo pedido)"_t
                                 : "Fecha a conexão da IA e recusa as próximas, até você ligar de novo"_t);
    setTooltip (off ? "A ponte da IA está aberta, mas recusa conexões até você ligar de novo"_t
                    : "Uma IA (pelo servidor MCP) está usando a WHYNOT. O que ela faz fica no ia.log e no desfazer, como \"IA: ...\""_t);
    setVisible (state != State::hidden);
    resized();
    repaint();
}

int AiStatusNotice::getIdealWidth() const
{
    const auto textWidth = juce::GlyphArrangement::getStringWidthInt (theme::font (theme::type::small, theme::Weight::medium), text());
    return dot + 8 + textWidth + 12 + toggleButton.getIdealWidth (buttonHeight);
}

void AiStatusNotice::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds();
    r.removeFromRight (toggleButton.getIdealWidth (buttonHeight) + 12);

    const auto dotArea = r.removeFromLeft (dot).withSizeKeepingCentre (dot, dot).toFloat();
    g.setColour (state == State::off ? colour::textFaint : colour::text);

    if (state == State::off)
        g.drawEllipse (dotArea.reduced (0.5f), 1.0f);
    else
        g.fillEllipse (dotArea);

    r.removeFromLeft (8);
    g.setFont (font (type::small, Weight::medium));
    g.setColour (colour::textDim);
    g.drawText (text(), r, juce::Justification::centredLeft, true);
}

void AiStatusNotice::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre (getWidth(), buttonHeight);
    toggleButton.setBounds (r.removeFromRight (toggleButton.getIdealWidth (buttonHeight)));
}

} // namespace whynot
