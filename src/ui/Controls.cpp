#include "Controls.h"
#include "Theme.h"

namespace whynot
{

namespace
{
    constexpr float iconGap = 7.0f;   // entre ícone e rótulo

    float iconSizeFor (float height)   { return juce::jlimit (14.0f, 20.0f, height * 0.5f); }

    float labelWidth (const juce::String& text)
    {
        return text.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body, theme::Weight::medium), text);
    }
}

//==============================================================================
IconButton::IconButton (const juce::String& name, Icon i, const juce::String& l, Style s)
    : juce::Button (name), icon (i), label (l), style (s)
{
    setTooltip (name);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

void IconButton::setIcon (Icon i)                   { if (i != icon) { icon = i; repaint(); } }
void IconButton::setLabel (const juce::String& l)   { if (l != label) { label = l; repaint(); } }
void IconButton::setStyle (Style s)                 { if (s != style) { style = s; repaint(); } }
void IconButton::setShowsMenuArrow (bool b)         { if (b != menuArrow) { menuArrow = b; repaint(); } }
void IconButton::setNeutralWhenOn (bool b)          { if (b != neutralOn) { neutralOn = b; repaint(); } }
void IconButton::setAlert (float level)             { level = juce::jlimit (0.0f, 1.0f, level); if (level != alert) { alert = level; repaint(); } }

int IconButton::getIdealWidth (int height) const
{
    const float iconSize = icon != Icon::none ? iconSizeFor ((float) height) : 0.0f;

    if (label.isEmpty() && ! menuArrow)
        return height;

    float w = 24.0f + iconSize + labelWidth (label);
    if (icon != Icon::none && label.isNotEmpty()) w += iconGap;
    if (menuArrow) w += 16.0f;
    return juce::roundToInt (w);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace theme;
    const auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const bool enabled = isEnabled();

    juce::Colour fill = juce::Colours::transparentBlack;

    if (on && neutralOn)                 fill = highlighted ? colour::raisedHigh.brighter (0.12f) : colour::raisedHigh.brighter (0.06f);
    else if (on)                         fill = highlighted ? colour::signal.withAlpha (0.26f) : colour::signalTint;
    else if (down)                       fill = colour::raisedHigh.brighter (0.08f);
    else if (highlighted && enabled)     fill = colour::raisedHigh;
    else if (style == Style::filled)     fill = colour::raised;

    if (alert > 0.0f)
        fill = colour::meterHigh.withAlpha (0.12f + 0.14f * alert);

    if (! fill.isTransparent())
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, metric::radius);
    }

    // ghost em repouso fica cinza e acende com o mouse; filled e ligado ficam sempre fortes
    auto ink = on ? (neutralOn ? colour::text : colour::signal)
                  : ((style == Style::filled || highlighted) ? colour::text : colour::textDim);
    if (! enabled) ink = colour::textFaint.withAlpha (0.7f);
    if (alert > 0.0f) ink = colour::meterHigh.withAlpha (0.45f + 0.55f * alert);

    const float iconSize = icon != Icon::none ? iconSizeFor (r.getHeight()) : 0.0f;
    float contentWidth = iconSize + labelWidth (label) + (menuArrow ? 16.0f : 0.0f);
    if (icon != Icon::none && label.isNotEmpty()) contentWidth += iconGap;

    float x = label.isEmpty() && ! menuArrow ? r.getCentreX() - iconSize * 0.5f
                                             : juce::jmax (r.getX() + 10.0f, r.getCentreX() - contentWidth * 0.5f);

    if (icon != Icon::none)
    {
        icons::draw (g, icon, { x, r.getCentreY() - iconSize * 0.5f, iconSize, iconSize }, ink);
        x += iconSize + (label.isNotEmpty() ? iconGap : 0.0f);
    }

    if (label.isNotEmpty())
    {
        const float w = labelWidth (label);
        g.setColour (ink);
        g.setFont (font (type::body, Weight::medium));
        g.drawText (label, juce::Rectangle<float> (x, r.getY(), juce::jmin (w + 1.0f, r.getRight() - x), r.getHeight()),
                    juce::Justification::centredLeft, true);
        x += w;
    }

    if (menuArrow)
        icons::draw (g, Icon::chevronDown, { x + 3.0f, r.getCentreY() - 6.5f, 13.0f, 13.0f }, on ? ink : colour::textDim);
}

//==============================================================================
SegmentedControl::SegmentedControl (bool isAccent) : accent (isAccent)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

void SegmentedControl::setItems (std::vector<Item> newItems)
{
    items = std::move (newItems);
    selected = juce::jlimit (0, juce::jmax (0, (int) items.size() - 1), selected);
    repaint();
}

void SegmentedControl::setSelected (int index)
{
    if (index != selected)
    {
        selected = index;
        repaint();
    }
}

void SegmentedControl::setLabels (Labels mode)
{
    if (mode != labels)
    {
        labels = mode;
        repaint();
    }
}

float SegmentedControl::labelRoom() const
{
    // a largura fica a mesma com qualquer escolha: o espaço do nome mais longo
    float widest = 0.0f;

    for (auto& item : items)
        if (item.label.isNotEmpty())
            widest = juce::jmax (widest, labelWidth (item.label) + (item.icon != Icon::none ? iconGap : 0.0f));

    return widest;
}

int SegmentedControl::getIdealWidth() const
{
    if (labels == Labels::selected)
        return juce::roundToInt ((18.0f + 24.0f) * (float) items.size() + labelRoom()) + 6;

    // segmentos de largura igual, do tamanho do maior
    float widest = 0.0f;

    for (auto& item : items)
    {
        float w = item.icon != Icon::none ? 18.0f : 0.0f;
        if (labels == Labels::all && item.label.isNotEmpty())
            w += labelWidth (item.label) + (item.icon != Icon::none ? iconGap : 0.0f);
        widest = juce::jmax (widest, w);
    }

    return juce::roundToInt ((widest + 24.0f) * (float) items.size()) + 6;
}

juce::Rectangle<float> SegmentedControl::segment (int index) const
{
    const auto inner = getLocalBounds().toFloat().reduced (3.0f);
    const int n = juce::jmax (1, (int) items.size());

    if (labels == Labels::selected)
    {
        // o escolhido ganha o espaço do nome; os outros dividem o resto por igual
        const float room = juce::jmin (labelRoom(), juce::jmax (0.0f, inner.getWidth() - 30.0f * (float) n));
        const float w = (inner.getWidth() - room) / (float) n;
        const float x = inner.getX() + w * (float) index + (index > selected ? room : 0.0f);
        return { x, inner.getY(), w + (index == selected ? room : 0.0f), inner.getHeight() };
    }

    const float w = inner.getWidth() / (float) n;
    return { inner.getX() + w * (float) index, inner.getY(), w, inner.getHeight() };
}

int SegmentedControl::indexAt (int x) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (segment (i).expanded (3.0f, 0.0f).getHorizontalRange().contains ((float) x))
            return i;

    return -1;
}

void SegmentedControl::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto r = getLocalBounds().toFloat();
    const bool enabled = isEnabled();

    g.setColour (colour::raised);
    g.fillRoundedRectangle (r, metric::radius);

    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& item = items[(size_t) i];
        const auto seg = segment (i);
        const bool isOn = i == selected;

        if (isOn)
        {
            g.setColour (accent ? colour::signalTint : colour::raisedHigh.brighter (0.06f));
            g.fillRoundedRectangle (seg, metric::radius - 2.5f);
        }
        else if (i == hover && enabled)
        {
            g.setColour (colour::raisedHigh.withAlpha (0.5f));
            g.fillRoundedRectangle (seg, metric::radius - 2.5f);
        }

        auto ink = isOn ? (accent ? colour::signal : colour::text) : (i == hover ? colour::text : colour::textDim);
        if (! enabled) ink = colour::textFaint;

        const bool showLabel = item.label.isNotEmpty() && (labels == Labels::all || (labels == Labels::selected && isOn));
        const float iconSize = item.icon != Icon::none ? juce::jmin (18.0f, seg.getHeight() - 10.0f) : 0.0f;
        const float textW = showLabel ? labelWidth (item.label) : 0.0f;
        const float total = iconSize + textW + (iconSize > 0.0f && showLabel ? iconGap : 0.0f);
        float x = seg.getCentreX() - total * 0.5f;

        if (item.icon != Icon::none)
        {
            icons::draw (g, item.icon, { x, seg.getCentreY() - iconSize * 0.5f, iconSize, iconSize }, ink);
            x += iconSize + iconGap;
        }

        if (showLabel)
        {
            g.setColour (ink);
            g.setFont (font (type::body, isOn ? Weight::semibold : Weight::medium));
            g.drawText (item.label, juce::Rectangle<float> (x, seg.getY(), textW + 2.0f, seg.getHeight()),
                        juce::Justification::centredLeft, false);
        }
    }
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int h = indexAt (e.x);

    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    const int index = indexAt (e.x);

    if (index < 0 || ! isEnabled())
        return;

    setSelected (index);

    if (onChange)
        onChange (index);
}

juce::String SegmentedControl::getTooltip()
{
    if (juce::isPositiveAndBelow (hover, (int) items.size()))
        return items[(size_t) hover].tooltip;

    return {};
}

//==============================================================================
void ChoiceChips::setItems (const juce::StringArray& l)
{
    labels = l;
    selected = juce::jlimit (0, juce::jmax (0, labels.size() - 1), selected);
    resized();
    repaint();
}

void ChoiceChips::setSelected (int index)
{
    if (juce::isPositiveAndBelow (index, labels.size()) && index != selected)
    {
        selected = index;
        repaint();
    }
}

std::vector<juce::Rectangle<float>> ChoiceChips::layout (int width) const
{
    std::vector<juce::Rectangle<float>> out;
    const auto f = theme::font (theme::type::small, theme::Weight::medium);
    float x = 0.0f, y = 0.0f;

    for (auto& l : labels)
    {
        const float w = juce::jmax (36.0f, juce::GlyphArrangement::getStringWidth (f, l) + 22.0f);

        if (x > 0.0f && x + w > (float) width)
        {
            x = 0.0f;
            y += chipHeight + chipGap;
        }

        out.push_back ({ x, y, w, (float) chipHeight });
        x += w + chipGap;
    }

    return out;
}

int ChoiceChips::getHeightForWidth (int width) const
{
    const auto b = layout (width);
    return b.empty() ? chipHeight : (int) b.back().getBottom();
}

void ChoiceChips::resized()
{
    boxes = layout (getWidth());
}

int ChoiceChips::indexAt (juce::Point<float> p) const
{
    for (size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i].contains (p))
            return (int) i;

    return -1;
}

void ChoiceChips::paint (juce::Graphics& g)
{
    using namespace theme;

    for (size_t i = 0; i < boxes.size() && (int) i < labels.size(); ++i)
    {
        const bool on = (int) i == selected, over = (int) i == hover;
        const auto r = boxes[i];
        g.setColour (on ? colour::raisedHigh : (over ? colour::raisedHigh.withAlpha (0.6f) : colour::raised));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on || over ? colour::text : colour::textDim);
        g.setFont (font (type::small, on ? Weight::semibold : Weight::medium));
        g.drawText (labels[(int) i], r, juce::Justification::centred, false);
    }
}

void ChoiceChips::mouseMove (const juce::MouseEvent& e)
{
    const int h = indexAt (e.position);

    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ChoiceChips::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void ChoiceChips::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);

    if (i >= 0 && i != selected)
    {
        selected = i;
        repaint();

        if (onChange)
            onChange (i);
    }
}

//==============================================================================
AmountSlider::AmountSlider (const juce::String& l) : label (l)
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

juce::Rectangle<float> AmountSlider::track() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (104.0f).withTrimmedRight (44.0f).withSizeKeepingCentre (
        juce::jmax (10.0f, (float) getWidth() - 148.0f), 4.0f);
}

void AmountSlider::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat();
    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawText (label, r.removeFromLeft (100.0f), juce::Justification::centredLeft, false);

    g.setColour (colour::text);
    drawTabularText (g, juce::String (juce::roundToInt (value * 100)) + "%", r.removeFromRight (40.0f), numberFont (type::small),
                     juce::Justification::centredRight);

    const auto t = track();
    g.setColour (colour::raised);
    g.fillRoundedRectangle (t, 2.0f);
    g.setColour (colour::textDim);
    g.fillRoundedRectangle (t.withWidth (t.getWidth() * (float) value), 2.0f);
    g.setColour (colour::text);
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ t.getX() + t.getWidth() * (float) value, t.getCentreY() }));
}

void AmountSlider::setFromX (float x)
{
    const auto t = track();
    setValue ((x - t.getX()) / juce::jmax (1.0f, t.getWidth()));
}

void AmountSlider::mouseDown (const juce::MouseEvent& e)            { setFromX (e.position.x); }
void AmountSlider::mouseDrag (const juce::MouseEvent& e)            { setFromX (e.position.x); }
void AmountSlider::mouseDoubleClick (const juce::MouseEvent&)       { setValue (0.5); }

} // namespace whynot
