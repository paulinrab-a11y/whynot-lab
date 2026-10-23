#include "Theme.h"
#include "WhynotAssets.h"

namespace whynot::theme
{

namespace
{
    struct Typefaces
    {
        juce::Typeface::Ptr regular, medium, semibold, bold;
    };

    const Typefaces& typefaces()
    {
        static const Typefaces t = []
        {
            auto load = [] (const char* data, int size)
            {
                return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
            };

            Typefaces f;
            f.regular  = load (WhynotAssets::MontserratRegular_ttf,  WhynotAssets::MontserratRegular_ttfSize);
            f.medium   = load (WhynotAssets::MontserratMedium_ttf,   WhynotAssets::MontserratMedium_ttfSize);
            f.semibold = load (WhynotAssets::MontserratSemiBold_ttf, WhynotAssets::MontserratSemiBold_ttfSize);
            f.bold     = load (WhynotAssets::MontserratBold_ttf,     WhynotAssets::MontserratBold_ttfSize);
            return f;
        }();

        return t;
    }

    juce::Typeface::Ptr typefaceFor (Weight w)
    {
        const auto& t = typefaces();

        switch (w)
        {
            case Weight::medium:   return t.medium;
            case Weight::semibold: return t.semibold;
            case Weight::bold:     return t.bold;
            case Weight::regular:  break;
        }

        return t.regular;
    }

    /** Dica: o texto principal em branco; o atalho entre parênteses no fim, em cinza. */
    juce::TextLayout tooltipLayout (const juce::String& text)
    {
        juce::AttributedString s;
        const int open = text.lastIndexOf (" (");

        if (text.endsWithChar (')') && open > 0)
        {
            s.append (text.substring (0, open), font (12.5f, Weight::medium), colour::text);
            s.append (text.substring (open), font (12.5f, Weight::medium), colour::textDim);
        }
        else
        {
            s.append (text, font (12.5f, Weight::medium), colour::text);
        }

        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, 340.0f);
        return layout;
    }
}

juce::Font font (float height, Weight weight)
{
    return juce::Font (juce::FontOptions (typefaceFor (weight)).withHeight (height));
}

juce::Font numberFont (float height)   { return font (height, Weight::semibold); }
juce::Font displayFont (float height)  { return font (height, Weight::bold); }
juce::Font captionFont()               { return font (9.5f, Weight::semibold).withExtraKerningFactor (0.08f); }

juce::Colour fromHex (const juce::String& hex, juce::Colour fallback)
{
    const auto clean = hex.trimCharactersAtStart ("#");
    return clean.length() == 6 ? juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) clean.getHexValue32())) : fallback;
}

const juce::Drawable* logo()
{
    static const auto drawable = juce::Drawable::createFromImageData (WhynotAssets::whynotlogowhite_svg,
                                                                      (size_t) WhynotAssets::whynotlogowhite_svgSize);
    return drawable.get();
}

juce::Image iconImage (int size)
{
    static const auto drawable = juce::Drawable::createFromImageData (WhynotAssets::whynoticon_svg,
                                                                      (size_t) WhynotAssets::whynoticon_svgSize);
    juce::Image image (juce::Image::ARGB, size, size, true);

    if (drawable != nullptr)
    {
        juce::Graphics g (image);
        drawable->drawWithin (g, juce::Rectangle<float> (0, 0, (float) size, (float) size), juce::RectanglePlacement::centred, 1.0f);
    }

    return image;
}

juce::Path hexagon (juce::Rectangle<float> area)
{
    juce::Path p;
    const auto c = area.getCentre();
    const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;

    for (int i = 0; i < 6; ++i)
    {
        const float a = juce::MathConstants<float>::pi / 3.0f * (float) i - juce::MathConstants<float>::halfPi;
        const juce::Point<float> v (c.x + r * std::cos (a), c.y + r * std::sin (a));
        if (i == 0) p.startNewSubPath (v); else p.lineTo (v);
    }

    p.closeSubPath();
    return p;
}

void drawNamePlate (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& name, juce::Colour edge,
                    bool selected, bool problem, float fontHeight, std::optional<Icon> badge)
{
    const float radius = metric::radius - 2.0f;
    g.setColour (selected ? colour::raisedHigh : colour::raised);
    g.fillRoundedRectangle (r, radius);

    // a cor do canal vira uma pílula fina com respiro, não uma borda
    const float inset = juce::jmin (7.0f, r.getHeight() * 0.25f);
    g.setColour (edge);
    g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 6.0f, r.getY() + inset, 3.0f, r.getHeight() - inset * 2.0f), 1.5f);

    if (problem || selected)
    {
        g.setColour (problem ? colour::meterHigh : colour::textDim.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    auto text = r.withTrimmedLeft (17.0f).withTrimmedRight (8.0f);

    if (badge.has_value())
    {
        const float s = juce::jmin (13.0f, r.getHeight() - 8.0f);
        icons::draw (g, *badge, text.removeFromRight (s).withSizeKeepingCentre (s, s), colour::textDim);
        text.removeFromRight (4.0f);
    }

    g.setColour (colour::text);
    g.setFont (font (fontHeight, Weight::medium));
    g.drawFittedText (name, text.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
}

void drawRoutePill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& destination, bool hovered,
                    bool dimText, float fontHeight)
{
    g.setColour (hovered ? colour::raisedHigh : colour::raised.withAlpha (0.55f));
    g.fillRoundedRectangle (r, metric::radius - 2.0f);

    const float s = juce::jmin (14.0f, r.getHeight() - 8.0f);
    icons::draw (g, Icon::route, { r.getX() + 7.0f, r.getCentreY() - s * 0.5f, s, s }, colour::textFaint);

    g.setColour (dimText ? colour::textDim : colour::text);
    g.setFont (font (fontHeight, Weight::medium));
    g.drawFittedText (destination, r.withTrimmedLeft (s + 12.0f).withTrimmedRight (6.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.8f);
}

void drawTabularText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& f,
                      juce::Justification justification)
{
    float digit = 0.0f;
    for (auto c : juce::String ("0123456789"))
        digit = juce::jmax (digit, juce::GlyphArrangement::getStringWidth (f, juce::String::charToString (c)));

    auto widthOf = [&] (juce::juce_wchar c)
    {
        return juce::CharacterFunctions::isDigit (c) ? digit : juce::GlyphArrangement::getStringWidth (f, juce::String::charToString (c));
    };

    float total = 0.0f;
    for (auto c : text)
        total += widthOf (c);

    float x = area.getX();
    if (justification.testFlags (juce::Justification::horizontallyCentred))  x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags (juce::Justification::right))           x = area.getRight() - total;

    g.setFont (f);

    for (auto c : text)
    {
        const float w = widthOf (c);
        g.drawText (juce::String::charToString (c), juce::Rectangle<float> (x, area.getY(), w, area.getHeight()),
                    juce::Justification::centred, false);
        x += w;
    }
}

juce::Path cornerMask (juce::Rectangle<float> r, float radius)
{
    juce::Path p;
    p.addRectangle (r);
    p.addRoundedRectangle (r, radius);
    p.setUsingNonZeroWinding (false);
    return p;
}

void styleComboBox (juce::ComboBox& box, Icon icon, bool bare)
{
    box.getProperties().set ("icon", (int) icon);
    box.getProperties().set ("style", bare ? "bare" : "");
    box.setRepaintsOnMouseActivity (true);
    box.setWantsKeyboardFocus (false);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    refreshColours();
}

void LookAndFeel::refreshColours()
{
    using namespace colour;

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId, background);

    setColour (juce::TextButton::buttonColourId, raised);
    setColour (juce::TextButton::buttonOnColourId, signalTint);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, signal);

    setColour (juce::Label::textColourId, text);
    setColour (juce::Label::outlineWhenEditingColourId, signal);
    setColour (juce::Label::backgroundWhenEditingColourId, raised);
    setColour (juce::Label::textWhenEditingColourId, text);

    setColour (juce::TextEditor::backgroundColourId, raised);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, signal.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, signal);
    setColour (juce::CaretComponent::caretColourId, text);

    setColour (juce::ComboBox::backgroundColourId, raised);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::ComboBox::focusedOutlineColourId, text);

    // Fundo do menu com um fio de transparência: a janela deixa de ser opaca e os cantos saem redondos.
    setColour (juce::PopupMenu::backgroundColourId, colour::floating.withAlpha (0.99f));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, raisedHigh);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textFaint);

    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour (juce::ScrollBar::thumbColourId, raisedHigh);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour (juce::TooltipWindow::backgroundColourId, colour::floating);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, colour::floatingEdge);

    setColour (juce::AlertWindow::backgroundColourId, colour::floating);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, colour::floatingEdge);

    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::textColourId, text);

    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, signal);
    setColour (juce::ToggleButton::tickDisabledColourId, textFaint);

    setColour (juce::GroupComponent::outlineColourId, line);
    setColour (juce::GroupComponent::textColourId, textDim);

    setColour (juce::HyperlinkButton::textColourId, signal);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Componentes do JUCE que pedem a fonte padrão (diálogos, seletor de áudio) também saem em Montserrat.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return typefaceFor (f.isBold() ? Weight::semibold : Weight::regular);

    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jlimit (12.0f, type::body, buttonHeight * 0.44f), Weight::semibold);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    if (box.getProperties()["style"].toString() == "lcd")
        return numberFont (17.0f);

    return font (type::body, Weight::medium);
}

juce::Font LookAndFeel::getPopupMenuFont()                    { return font (type::body, Weight::medium); }
juce::Font LookAndFeel::getLabelFont (juce::Label& label)
{
    // quem escolheu a fonte com setFont fica com ela; o Label que veio com a padrão do JUCE (15, sem nome) sai no corpo
    // (achado da Gravação de voz, 22/10: antes, todo Label saía no corpo e o setFont não valia)
    const auto& f = label.getFont();
    const bool juceDefault = f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()
                             && std::abs (f.getHeight() - 15.0f) < 0.01f && ! f.isBold() && ! f.isItalic();
    return juceDefault ? font (type::body) : f;
}
juce::Font LookAndFeel::getAlertWindowMessageFont()           { return font (type::body); }
juce::Font LookAndFeel::getAlertWindowTitleFont()             { return font (type::title + 1.0f, Weight::semibold); }

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                        bool highlighted, bool down)
{
    // Sem contorno: o degrau de cor já separa o botão do fundo. Ligado = laranja translúcido;
    // com a propriedade "neutral" (estado que não soa, como o mute), ligado = a pílula clara de "escolhido".
    const auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const bool neutral = (bool) b.getProperties()["neutral"];

    const auto onFill = neutral ? colour::raisedHigh.brighter (0.06f) : colour::signalTint;
    auto fill = on ? onFill : colour::raised;
    if (highlighted) fill = on ? (neutral ? colour::raisedHigh.brighter (0.12f) : colour::signal.withAlpha (0.26f)) : colour::raisedHigh;
    if (down)        fill = on ? (neutral ? colour::raisedHigh.brighter (0.16f) : colour::signal.withAlpha (0.32f)) : colour::raisedHigh.brighter (0.08f);
    if (! b.isEnabled()) fill = fill.withMultipliedAlpha (0.5f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, metric::radius);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }

    // Fader: trilho fino, trecho abaixo da capa aceso, marca de 0 dB e capa com risco central.
    const float cx = (float) x + (float) w * 0.5f;
    const auto track = juce::Rectangle<float> (cx - 2.0f, (float) y, 4.0f, (float) h);
    g.setColour (colour::raised);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (colour::textFaint.withAlpha (0.55f));
    g.fillRoundedRectangle (track.withTop (pos), 2.0f);

    const auto zeroDb = (float) s.valueToProportionOfLength (0.0);
    const float zeroY = (float) y + (float) h * (1.0f - zeroDb);
    g.setColour (colour::textFaint);
    g.fillRect (juce::Rectangle<float> (cx - 12.0f, zeroY - 0.5f, 6.0f, 1.0f));
    g.fillRect (juce::Rectangle<float> (cx + 6.0f, zeroY - 0.5f, 6.0f, 1.0f));

    const auto cap = juce::Rectangle<float> (cx - 13.0f, pos - 7.0f, 26.0f, 14.0f);
    g.setColour (colour::raisedHigh.brighter (0.3f));
    g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (colour::text);
    g.fillRoundedRectangle (cap.withSizeKeepingCentre (14.0f, 2.0f), 1.0f);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto stroke = juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path back;
    back.addCentredArc (centre.x, centre.y, radius - 1.5f, radius - 1.5f, 0.0f, startAngle, endAngle, true);
    g.setColour (colour::raisedHigh);
    g.strokePath (back, stroke);

    // Arco do valor: pan parte do centro; os outros, do começo.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - 1.5f, radius - 1.5f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    // propriedade "accent" = parâmetro agindo agora (ex.: redução de ganho): arco laranja
    g.setColour ((bool) s.getProperties()["accent"] ? colour::signal : colour::textDim);
    g.strokePath (value, stroke);

    g.setColour (colour::raised.brighter (0.15f));
    g.fillEllipse (bounds.reduced (radius * 0.3f));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.0f, -(radius * 0.7f), 2.0f, radius * 0.38f, 1.0f);
    g.setColour (colour::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    const auto style = box.getProperties()["style"].toString();
    const bool bare = style == "bare" || style == "lcd";
    const bool over = box.isMouseOver (true) && box.isEnabled();

    if (! bare || over || down)
    {
        g.setColour (over || down ? colour::raisedHigh : colour::raised);
        g.fillRoundedRectangle (r, metric::radius);
    }

    const auto ink = box.isEnabled() ? colour::textDim : colour::textFaint;
    const auto icon = (Icon) (int) box.getProperties().getWithDefault ("icon", 0);

    if (icon != Icon::none)
        icons::draw (g, icon, { 9.0f, (float) h * 0.5f - 8.0f, 16.0f, 16.0f }, ink);

    if (style != "lcd")
        icons::draw (g, Icon::chevronDown, { (float) w - 22.0f, (float) h * 0.5f - 7.0f, 14.0f, 14.0f }, ink);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const bool lcd = box.getProperties()["style"].toString() == "lcd";
    const bool icon = (int) box.getProperties().getWithDefault ("icon", 0) != 0;
    const int left = lcd ? 0 : (icon ? 31 : 11);
    const int right = lcd ? 0 : 26;

    // Uma linha só: a área do rótulo tem a altura de uma linha (o Label quebra em tantas linhas quantas couberem).
    // Texto longo encolhe até 70% e depois ganha reticências, em vez de virar "Passa- / baixas".
    const auto f = getComboBoxFont (box);
    const int lineHeight = juce::jmin (box.getHeight(), (int) std::ceil (f.getHeight() * 1.5f));
    label.setBounds (left, (box.getHeight() - lineHeight) / 2, box.getWidth() - left - right, lineHeight);
    label.setBorderSize ({});
    label.setMinimumHorizontalScale (0.7f);
    label.setFont (f);
    label.setJustificationType (lcd ? juce::Justification::centred : juce::Justification::centredLeft);
}

//==============================================================================
void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (colour::floating);
    g.fillRoundedRectangle (r, metric::radiusCard - 2.0f);
    g.setColour (colour::floatingEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), metric::radiusCard - 2.0f, 1.0f);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                     bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                     const juce::String& shortcutKeyText, const juce::Drawable* icon,
                                     const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (colour::line);
        g.fillRect (area.toFloat().reduced (10.0f, 0.0f).withSizeKeepingCentre ((float) area.getWidth() - 20.0f, 1.0f));
        return;
    }

    const auto r = area.toFloat().reduced (4.0f, 1.0f);

    if (isHighlighted && isActive)
    {
        g.setColour (colour::raisedHigh);
        g.fillRoundedRectangle (r, metric::radius - 2.0f);
    }

    auto content = r.reduced (8.0f, 0.0f);
    const auto mark = content.removeFromLeft (18.0f);
    content.removeFromLeft (6.0f);

    // marcado = um visto laranja na coluna da esquerda (a coluna existe sempre, para alinhar os itens)
    if (isTicked)
        icons::draw (g, Icon::check, mark.withSizeKeepingCentre (14.0f, 14.0f), colour::signal);
    else if (icon != nullptr)
        icon->drawWithin (g, mark.reduced (1.0f), juce::RectanglePlacement::centred, 1.0f);

    if (hasSubMenu)
        icons::draw (g, Icon::chevronRight, content.removeFromRight (14.0f).withSizeKeepingCentre (12.0f, 12.0f), colour::textDim);

    g.setFont (getPopupMenuFont());

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (colour::textFaint);
        g.drawText (shortcutKeyText, content, juce::Justification::centredRight, true);
    }

    g.setColour (! isActive ? colour::textFaint : (textColour != nullptr ? *textColour : colour::text));
    g.drawFittedText (text, content.toNearestInt(), juce::Justification::centredLeft, 1);
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                             int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 9;
        return;
    }

    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 30;
    idealWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text)) + 64;
}

int LookAndFeel::getPopupMenuBorderSize()   { return 5; }

//==============================================================================
int LookAndFeel::getDefaultScrollbarWidth()   { return 10; }

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                 int thumbStart, int thumbSize, bool over, bool down)
{
    if (thumbSize <= 0)
        return;

    // Barra fina e discreta; engrossa quando o mouse chega.
    const float thickness = over || down ? 6.0f : 4.0f;
    auto thumb = vertical ? juce::Rectangle<float> ((float) x, (float) thumbStart, (float) w, (float) thumbSize)
                          : juce::Rectangle<float> ((float) thumbStart, (float) y, (float) thumbSize, (float) h);
    thumb = vertical ? thumb.withSizeKeepingCentre (thickness, thumb.getHeight() - 4.0f)
                     : thumb.withSizeKeepingCentre (thumb.getWidth() - 4.0f, thickness);

    g.setColour (down ? colour::textDim : (over ? colour::textFaint : colour::raisedHigh.brighter (0.2f)));
    g.fillRoundedRectangle (thumb, thickness * 0.5f);
}

//==============================================================================
juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> screenPos,
                                                    juce::Rectangle<int> parentArea)
{
    const auto layout = tooltipLayout (tip);
    const int w = (int) std::ceil (layout.getWidth()) + 22;
    const int h = (int) std::ceil (layout.getHeight()) + 14;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 18,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 20,
                                 w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<float> ((float) w, (float) h);
    g.setColour (colour::floating);
    g.fillRoundedRectangle (r, metric::radius);
    g.setColour (colour::floatingEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), metric::radius, 1.0f);
    tooltipLayout (text).draw (g, r.reduced (11.0f, 7.0f));
}

void LookAndFeel::drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path,
                                            juce::Image& cachedShadow)
{
    // sombra larga e suave (calculada uma vez), depois o cartão com a borda fina
    if (cachedShadow.isNull())
    {
        cachedShadow = juce::Image (juce::Image::ARGB, box.getWidth(), box.getHeight(), true);
        juce::Graphics shadow (cachedShadow);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 18, { 0, 6 }).drawForPath (shadow, path);
    }

    g.setColour (juce::Colours::black);
    g.drawImageAt (cachedShadow, 0, 0);

    g.setColour (colour::panel);
    g.fillPath (path);
    g.setColour (colour::floatingEdge);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

int LookAndFeel::getCallOutBoxBorderSize (const juce::CallOutBox&)        { return 20; }   // espaço da seta
float LookAndFeel::getCallOutBoxCornerSize (const juce::CallOutBox&)      { return metric::radiusCard; }

void LookAndFeel::drawStretchableLayoutResizerBar (juce::Graphics& g, int w, int h, bool vertical, bool over, bool dragging)
{
    // A fresta entre os cartões é a alça: só uma pílula aparece quando o mouse chega.
    if (! over && ! dragging)
        return;

    const auto r = juce::Rectangle<float> ((float) w, (float) h);
    g.setColour (dragging ? colour::textDim : colour::textFaint);
    g.fillRoundedRectangle (vertical ? r.withSizeKeepingCentre (3.0f, 40.0f) : r.withSizeKeepingCentre (40.0f, 3.0f), 1.5f);
}

//==============================================================================
// Temas de cores (docs/design.md, seção 3). Cada paleta segue a mesma escada: o chão (background), o cartão
// (panel) um degrau acima, o controle (raised) mais um, e o texto em três tons. O vermelho funcional não muda.

namespace
{
    juce::Colour argb (juce::uint32 v)   { return juce::Colour (v); }

    Palette makeWhynot()
    {
        Palette p;
        p.background = argb (0xff000000); p.panel = argb (0xff141414); p.raised = argb (0xff1f1f1f); p.raisedHigh = argb (0xff2c2c2c);
        p.line = argb (0xff262626); p.text = argb (0xffffffff); p.textDim = argb (0xffa6a6a6); p.textFaint = argb (0xff8a8a8a);
        p.signal = argb (0xffff751f); p.signalTint = argb (0x2eff751f);
        p.meterLow = argb (0xffd9d9d9); p.meterMid = argb (0xffff751f); p.meterHigh = argb (0xffff3b30); p.warning = argb (0xffff9a57);
        p.floating = argb (0xff1a1a1a); p.floatingEdge = argb (0xff333333); p.shade = argb (0xff000000); p.onSignal = argb (0xff000000);
        p.laneEven = argb (0xff0c0c0c); p.laneOdd = argb (0xff101010); p.fine = argb (0xff131313); p.step = argb (0xff151515);
        p.beat = argb (0xff181818); p.laneEdge = argb (0xff1c1c1c); p.clipHead = argb (0xff222222);
        p.rowOut = argb (0xff0a0a0a); p.rowDark = argb (0xff111111); p.row = argb (0xff181818); p.rowRoot = argb (0xff222222);
        p.keyWhite = argb (0xffd9d9d9); p.keyBlack = argb (0xff1b1b1b); p.keySeam = argb (0xff8c8c8c);
        p.keyHoverWhite = argb (0xffbfbfbf); p.keyHoverBlack = argb (0xff3a3a3a); p.keyLabel = argb (0xff2e2e2e);
        return p;
    }

    // Claro: chão cinza-claro, cartões brancos, texto quase preto. O laranja escurece um pouco para ler no branco.
    Palette makeLight()
    {
        Palette p;
        p.background = argb (0xffe8e8eb); p.panel = argb (0xfffbfbfc); p.raised = argb (0xffeeeef1); p.raisedHigh = argb (0xffe0e0e5);
        p.line = argb (0xffdcdce1); p.text = argb (0xff141416); p.textDim = argb (0xff4f4f56); p.textFaint = argb (0xff6f6f76);
        p.signal = argb (0xfff06414); p.signalTint = argb (0x26f06414);
        p.meterLow = argb (0xff46464c); p.meterMid = argb (0xfff06414); p.meterHigh = argb (0xffff3b30); p.warning = argb (0xffc2620f);
        p.floating = argb (0xffffffff); p.floatingEdge = argb (0xffd0d0d6); p.shade = argb (0xffc0c0ca); p.onSignal = argb (0xff141416);
        p.laneEven = argb (0xfff6f6f8); p.laneOdd = argb (0xfff1f1f4); p.fine = argb (0xffebebef); p.step = argb (0xffe5e5ea);
        p.beat = argb (0xffdcdce2); p.laneEdge = argb (0xffe2e2e7); p.clipHead = argb (0xffe6e6eb);
        p.rowOut = argb (0xffe3e3e8); p.rowDark = argb (0xffebebef); p.row = argb (0xfff5f5f7); p.rowRoot = argb (0xffffffff);
        p.keyWhite = argb (0xfffafafa); p.keyBlack = argb (0xff2a2a2e); p.keySeam = argb (0xffb8b8bf);
        p.keyHoverWhite = argb (0xffe3e3e8); p.keyHoverBlack = argb (0xff4a4a52); p.keyLabel = argb (0xff6a6a72);
        return p;
    }

    // Colorido: base índigo, sinal rosa-choque e medidor ciano.
    Palette makeColourful()
    {
        Palette p;
        p.background = argb (0xff0b0816); p.panel = argb (0xff151027); p.raised = argb (0xff201936); p.raisedHigh = argb (0xff2d2449);
        p.line = argb (0xff2a2140); p.text = argb (0xffffffff); p.textDim = argb (0xffb8acd9); p.textFaint = argb (0xff9387b8);
        p.signal = argb (0xffff4fb8); p.signalTint = argb (0x33ff4fb8);
        p.meterLow = argb (0xff5ee0ff); p.meterMid = argb (0xffff4fb8); p.meterHigh = argb (0xffff3b30); p.warning = argb (0xffffb84f);
        p.floating = argb (0xff1d1633); p.floatingEdge = argb (0xff3a2f58); p.shade = argb (0xff05030c); p.onSignal = argb (0xff0b0816);
        p.laneEven = argb (0xff0f0b1e); p.laneOdd = argb (0xff130e25); p.fine = argb (0xff161129); p.step = argb (0xff19132e);
        p.beat = argb (0xff1d1735); p.laneEdge = argb (0xff211a3b); p.clipHead = argb (0xff2a2147);
        p.rowOut = argb (0xff0d0a1a); p.rowDark = argb (0xff140f26); p.row = argb (0xff1c1634); p.rowRoot = argb (0xff261d45);
        p.keyWhite = argb (0xffe6e0f5); p.keyBlack = argb (0xff1e1830); p.keySeam = argb (0xff9a8fbd);
        p.keyHoverWhite = argb (0xffcfc6ea); p.keyHoverBlack = argb (0xff3c3260); p.keyLabel = argb (0xff3a3155);
        return p;
    }

    // Frio: azul-ardósia, sinal azul-gelo.
    Palette makeCool()
    {
        Palette p;
        p.background = argb (0xff05080c); p.panel = argb (0xff0e141b); p.raised = argb (0xff172029); p.raisedHigh = argb (0xff212d39);
        p.line = argb (0xff1d2731); p.text = argb (0xfff1f6fa); p.textDim = argb (0xff9db0c2); p.textFaint = argb (0xff8496a9);
        p.signal = argb (0xff39c2ff); p.signalTint = argb (0x2e39c2ff);
        p.meterLow = argb (0xffcde4f2); p.meterMid = argb (0xff39c2ff); p.meterHigh = argb (0xffff3b30); p.warning = argb (0xffffb454);
        p.floating = argb (0xff131b24); p.floatingEdge = argb (0xff2b3846); p.shade = argb (0xff020406); p.onSignal = argb (0xff05080c);
        p.laneEven = argb (0xff0a0f15); p.laneOdd = argb (0xff0d131a); p.fine = argb (0xff10171f); p.step = argb (0xff121a23);
        p.beat = argb (0xff151e28); p.laneEdge = argb (0xff18222d); p.clipHead = argb (0xff1e2935);
        p.rowOut = argb (0xff080c11); p.rowDark = argb (0xff0e141b); p.row = argb (0xff151e27); p.rowRoot = argb (0xff1d2833);
        p.keyWhite = argb (0xffdde6ee); p.keyBlack = argb (0xff17202a); p.keySeam = argb (0xff8597a8);
        p.keyHoverWhite = argb (0xffbccbd8); p.keyHoverBlack = argb (0xff34424f); p.keyLabel = argb (0xff2b3642);
        return p;
    }

    // todos os campos da paleta, na mesma ordem dos tokens
    using Field = juce::Colour Palette::*;
    constexpr Field fields[] {
        &Palette::background, &Palette::panel, &Palette::raised, &Palette::raisedHigh, &Palette::line, &Palette::text,
        &Palette::textDim, &Palette::textFaint, &Palette::signal, &Palette::signalTint, &Palette::meterLow, &Palette::meterMid,
        &Palette::meterHigh, &Palette::warning, &Palette::floating, &Palette::floatingEdge, &Palette::shade, &Palette::onSignal,
        &Palette::laneEven, &Palette::laneOdd, &Palette::fine, &Palette::step, &Palette::beat, &Palette::laneEdge, &Palette::clipHead,
        &Palette::rowOut, &Palette::rowDark, &Palette::row, &Palette::rowRoot,
        &Palette::keyWhite, &Palette::keyBlack, &Palette::keySeam, &Palette::keyHoverWhite, &Palette::keyHoverBlack, &Palette::keyLabel
    };

    juce::Colour* const tokens[] {
        &colour::background, &colour::panel, &colour::raised, &colour::raisedHigh, &colour::line, &colour::text,
        &colour::textDim, &colour::textFaint, &colour::signal, &colour::signalTint, &colour::meterLow, &colour::meterMid,
        &colour::meterHigh, &colour::warning, &colour::floating, &colour::floatingEdge, &colour::shade, &colour::onSignal,
        &colour::grid::laneEven, &colour::grid::laneOdd, &colour::grid::fine, &colour::grid::step, &colour::grid::beat,
        &colour::grid::laneEdge, &colour::grid::clipHead,
        &colour::grid::rowOut, &colour::grid::rowDark, &colour::grid::row, &colour::grid::rowRoot,
        &colour::keys::white, &colour::keys::black, &colour::keys::seam, &colour::keys::hoverWhite, &colour::keys::hoverBlack,
        &colour::keys::label
    };

    static_assert (std::extent_v<decltype (fields)> == std::extent_v<decltype (tokens)>, "um campo da paleta por token");

    ColourScheme current = ColourScheme::whynot;

    // Troca as cores que um componente guardou (setColour; o JUCE guarda como "jcclr_<id em hexa>") da paleta
    // antiga para a nova, pelo token de mesma cor. O alfa de cada uma fica.
    void remap (juce::Component& c, const std::vector<std::pair<juce::uint32, juce::Colour>>& map)
    {
        std::vector<std::pair<int, juce::Colour>> changes;

        for (const auto& prop : c.getProperties())
        {
            const auto name = prop.name.toString();

            if (! name.startsWith ("jcclr_"))
                continue;

            const auto stored = juce::Colour ((juce::uint32) (int) prop.value);
            const auto rgb = stored.getARGB() & 0x00ffffffu;

            for (const auto& [from, to] : map)
            {
                if (from == rgb)
                {
                    changes.push_back ({ name.substring (6).getHexValue32(), to.withAlpha (stored.getAlpha()) });
                    break;
                }
            }
        }

        for (const auto& [id, newColour] : changes)
            c.setColour (id, newColour);

        for (auto* child : c.getChildren())
            remap (*child, map);
    }
}

const Palette& palette (ColourScheme s)
{
    static const Palette brandPalette = makeWhynot(), lightPalette = makeLight(), colourfulPalette = makeColourful(),
                         coolPalette = makeCool();

    switch (s)
    {
        case ColourScheme::light:      return lightPalette;
        case ColourScheme::colourful:  return colourfulPalette;
        case ColourScheme::cool:       return coolPalette;
        case ColourScheme::whynot:     break;
    }

    return brandPalette;
}

ColourScheme currentScheme() noexcept   { return current; }
bool isLightScheme() noexcept           { return current == ColourScheme::light; }

void applyScheme (ColourScheme s)
{
    const auto& p = palette (s);

    for (size_t i = 0; i < std::size (fields); ++i)
        *tokens[i] = p.*fields[i];

    current = s;
}

namespace
{
    // da cor antiga para a nova, na ordem dos tokens: se dois tokens tinham a mesma cor, vale o primeiro
    std::vector<std::pair<juce::uint32, juce::Colour>> colourMap (ColourScheme from)
    {
        const auto& a = palette (from);
        const auto& b = palette (current);
        std::vector<std::pair<juce::uint32, juce::Colour>> map;

        for (auto field : fields)
        {
            const auto rgb = (a.*field).getARGB() & 0x00ffffffu;

            if (std::none_of (map.begin(), map.end(), [rgb] (const auto& m) { return m.first == rgb; }))
                map.push_back ({ rgb, (b.*field).withAlpha (1.0f) });
        }

        return map;
    }
}

void reskin (juce::Component& c, ColourScheme from)
{
    remap (c, colourMap (from));
}

void reskinAll (ColourScheme from)
{
    const auto map = colourMap (from);
    auto& desktop = juce::Desktop::getInstance();

    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        if (auto* window = desktop.getComponent (i))
        {
            remap (*window, map);
            window->sendLookAndFeelChange();
            window->repaint();
        }
    }
}

juce::String schemeKey (ColourScheme s)
{
    switch (s)
    {
        case ColourScheme::light:      return "claro";
        case ColourScheme::colourful:  return "colorido";
        case ColourScheme::cool:       return "frio";
        case ColourScheme::whynot:     break;
    }

    return "whynot";
}

juce::String schemeName (ColourScheme s)
{
    switch (s)
    {
        case ColourScheme::light:      return "Claro";
        case ColourScheme::colourful:  return "Colorido";
        case ColourScheme::cool:       return "Frio";
        case ColourScheme::whynot:     break;
    }

    return "WHYNOT";
}

std::optional<ColourScheme> schemeFromKey (const juce::String& key)
{
    for (auto s : allSchemes)
        if (schemeKey (s).equalsIgnoreCase (key.trim()))
            return s;

    return std::nullopt;
}

ColourScheme savedScheme()
{
    // o mesmo arquivo do MainComponent, só para ler: quem grava a escolha é o menu Arquivo
    juce::PropertiesFile::Options options;
    options.applicationName = "WHYNOT LAB";
    options.filenameSuffix = ".settings";
    options.folderName = "WHYNOT LAB";
    options.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile settings (options);
    return schemeFromKey (settings.getValue ("theme")).value_or (ColourScheme::whynot);
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c)
{
    // uma cópia pintada por cor, guardada: a logo é um caminho só, em branco
    static std::unique_ptr<juce::Drawable> tinted;
    static juce::Colour tintedColour;

    auto* mark = logo();

    if (mark == nullptr)
        return;

    if (c == juce::Colours::white)
    {
        mark->drawWithin (g, area, juce::RectanglePlacement::centred, 1.0f);
        return;
    }

    if (tinted == nullptr || tintedColour != c)
    {
        tinted = mark->createCopy();
        tinted->replaceColour (juce::Colours::white, c);
        tintedColour = c;
    }

    tinted->drawWithin (g, area, juce::RectanglePlacement::centred, 1.0f);
}

} // namespace whynot::theme
