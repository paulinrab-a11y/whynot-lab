#include "BeatKeyNotice.h"

#include "Theme.h"
#include "../common/Text.h"

namespace whynot
{

BeatKeyNotice::BeatKeyNotice()
{
    useButton.setTooltip ("Muda o tom do projeto para o do beat: o piano roll escurece as notas fora da escala"_t);
    laterButton.setTooltip ("Esconde este aviso; o tom do projeto fica como está"_t);
    useButton.onClick = [this] { if (onUse) onUse(); };
    laterButton.onClick = [this] { if (onDismiss) onDismiss(); };

    for (auto* b : { &useButton, &laterButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
}

void BeatKeyNotice::show (const juce::String& newText, const juce::String& details)
{
    text = newText;
    setTooltip (details);
    repaint();
}

int BeatKeyNotice::getIdealWidth() const
{
    const auto textWidth = juce::GlyphArrangement::getStringWidthInt (theme::font (theme::type::small, theme::Weight::medium), text);
    return textWidth + 12 + useButton.getIdealWidth (buttonHeight) + 4 + laterButton.getIdealWidth (buttonHeight);
}

void BeatKeyNotice::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds();
    r.removeFromRight (useButton.getIdealWidth (buttonHeight) + 4 + laterButton.getIdealWidth (buttonHeight) + 12);
    g.setFont (font (type::small, Weight::medium));
    g.setColour (colour::textDim);
    g.drawText (text, r, juce::Justification::centredLeft, true);
}

void BeatKeyNotice::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre (getWidth(), buttonHeight);
    laterButton.setBounds (r.removeFromRight (laterButton.getIdealWidth (buttonHeight)));
    r.removeFromRight (4);
    useButton.setBounds (r.removeFromRight (useButton.getIdealWidth (buttonHeight)));
}

} // namespace whynot
