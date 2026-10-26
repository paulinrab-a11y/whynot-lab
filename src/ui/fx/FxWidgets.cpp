#include "FxWidgets.h"
#include "../Theme.h"

namespace whynot
{

namespace fxui
{
    juce::String utf8 (const char* s)   { return juce::String::fromUTF8 (s); }

    juce::String caps (const juce::String& text)
    {
        auto upper = text.toUpperCase();
        const auto lower = juce::String::fromUTF8 ("áàâãéêíóôõúüç"), high = juce::String::fromUTF8 ("ÁÀÂÃÉÊÍÓÔÕÚÜÇ");

        for (int i = 0; i < lower.length(); ++i)
            upper = upper.replaceCharacter (lower[i], high[i]);

        return upper;
    }

    namespace
    {
        juce::String number (double v, int decimals)
        {
            // String (x, 0) no JUCE quer dizer "todas as casas": zero casas é arredondar.
            return decimals <= 0 ? juce::String (juce::roundToInt (v))
                                 : juce::String (v, decimals).replaceCharacter ('.', ',');
        }
    }

    juce::String formatValue (const dsp::ParamInfo& p, float v)
    {
        const juce::String unit = utf8 (p.unit);

        if (p.kind == dsp::ParamKind::toggle)
            return v >= 0.5f ? "Ligado" : "Desligado";

        if (p.kind == dsp::ParamKind::choice)
        {
            const int i = juce::jlimit (0, (int) p.choices.size() - 1, (int) std::round (v));
            return p.choices.empty() ? juce::String ((int) v) : utf8 (p.choices[(size_t) i]);
        }

        // Parâmetros em que 0 quer dizer "desligado" (proteger graves, grave mono).
        if (p.min == 0.0f && unit == "Hz" && v < 1.0f)
            return "Desligado";

        // Abaixo de 10 Hz com duas casas: a taxa do LFO do synth (0,02 a 30 Hz) aparecia "0 Hz" em 0,15 Hz.
        if (unit == "Hz")
            return v >= 1000.0f ? number (v / 1000.0, v >= 10000.0f ? 1 : 2) + " kHz" : number (v, v < 10.0f ? 2 : 0) + " Hz";

        if (unit == "dB" || unit == "dBTP")
        {
            const bool bipolar = p.min < 0.0f && p.max > 0.0f && unit == "dB";
            return (bipolar && v > 0.05f ? "+" : "") + number (v, 1) + " " + unit;
        }

        // Abaixo de 1 ms com duas casas: o ataque do FET (0,02 a 0,8 ms) e o do gate (0,05 ms).
        if (unit == "ms")
            return v >= 1000.0f ? number (v / 1000.0, 2) + " s" : number (v, v < 1.0f ? 2 : v < 10.0f ? 1 : 0) + " ms";

        if (unit == "%")
            return (p.min < 0.0f && v > 0.5f ? "+" : "") + number (v, 0) + "%";

        if (unit == ":1")
            return number (v, v < 10.0f ? 1 : 0) + ":1";

        if (unit == "Q")
            return "Q " + number (v, v < 10.0f ? 2 : 1);

        if (unit == "s")
            return number (v, v < 10.0f ? 2 : 1) + " s";

        if (unit == "LUFS")
            return number (v, 1) + " LUFS";

        if (unit == "st")   // semitons, com sinal e duas casas: 0,50 st = 50 cents
            return (v > 0.005f ? "+" : "") + number (v, std::abs (v) < 10.0f ? 2 : 1) + " st";

        return number (v, 1) + (unit.isNotEmpty() ? " " + unit : juce::String());
    }

    juce::NormalisableRange<double> rangeFor (const dsp::ParamInfo& p)
    {
        if (p.kind != dsp::ParamKind::continuous)
            return { p.min, p.max, 1.0 };

        if (p.logScale && p.min > 0.0f)
        {
            const double lo = p.min, hi = p.max;
            return { lo, hi,
                     [] (double start, double end, double x) { return start * std::pow (end / start, x); },
                     [] (double start, double end, double v) { return std::log (v / start) / std::log (end / start); },
                     [] (double start, double end, double v) { return juce::jlimit (start, end, v); } };
        }

        return { p.min, p.max };
    }
}

//==============================================================================
ParamControl::ParamControl (const dsp::ParamInfo& p) : info (p)
{
    const auto help = fxui::utf8 (info.help);
    setTooltip (help.isNotEmpty() ? fxui::utf8 (info.name) + ": " + help : fxui::utf8 (info.name));

    switch (info.kind)
    {
        case dsp::ParamKind::continuous:
            knob.setNormalisableRange (fxui::rangeFor (info));
            knob.setDoubleClickReturnValue (true, info.def);
            knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            knob.setTooltip (getTooltip() + " Duplo clique volta ao padrão."_t);
            knob.onDragStart = [this] { if (onGestureStart) onGestureStart(); };
            knob.onDragEnd = [this] { if (onGestureEnd) onGestureEnd(); };
            knob.onValueChange = [this] { changed ((float) knob.getValue()); };
            knob.onMenu = [this] { showMenu(); };
            knob.setWantsKeyboardFocus (false);
            addAndMakeVisible (knob);
            break;

        case dsp::ParamKind::toggle:
            toggle.setClickingTogglesState (false);
            toggle.setTooltip (getTooltip());
            toggle.onClick = [this] { changed (value >= 0.5f ? 0.0f : 1.0f); };
            toggle.setWantsKeyboardFocus (false);
            addAndMakeVisible (toggle);
            break;

        case dsp::ParamKind::choice:
            for (size_t i = 0; i < info.choices.size(); ++i)
                list.addItem (fxui::utf8 (info.choices[i]), (int) i + 1);

            theme::styleComboBox (list);
            list.setTooltip (getTooltip());
            list.onChange = [this] { if (list.getSelectedId() > 0) changed ((float) (list.getSelectedId() - 1)); };
            list.setWantsKeyboardFocus (false);
            addAndMakeVisible (list);
            break;
    }

    setValue (info.def);
}

void ParamControl::setValue (float v)
{
    value = v;

    switch (info.kind)
    {
        case dsp::ParamKind::continuous: knob.setValue (v, juce::dontSendNotification); break;
        case dsp::ParamKind::toggle:
            toggle.setToggleState (v >= 0.5f, juce::dontSendNotification);
            toggle.setButtonText (v >= 0.5f ? "Ligado" : "Desligado");
            break;
        case dsp::ParamKind::choice:     list.setSelectedId ((int) std::round (v) + 1, juce::dontSendNotification); break;
    }

    repaint();
}

void ParamControl::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())   // na legenda e no valor (o knob tem o próprio)
        showMenu();
}

void ParamControl::showMenu()
{
    if (onMenu)
    {
        onMenu();
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (fxui::caps (fxui::utf8 (info.name)));
    menu.addItem (1, resetLabel(), value != info.def);
    juce::Component::SafePointer<ParamControl> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int chosen)
    {
        if (safe != nullptr && chosen == 1)
            safe->resetToDefault();
    });
}

juce::String ParamControl::resetLabel() const
{
    return "Voltar ao padrão ("_t + fxui::formatValue (info, info.def) + ")";
}

void ParamControl::resetToDefault()
{
    if (onGestureStart)
        onGestureStart();

    setValue (info.def);
    value = info.def + 1.0f;   // força o aviso, mesmo numa lista ou num botão
    changed (info.def);

    if (onGestureEnd)
        onGestureEnd();
}

void ParamControl::setAccent (bool on)
{
    if ((bool) knob.getProperties()["accent"] != on)
    {
        knob.getProperties().set ("accent", on);
        knob.repaint();
    }
}

void ParamControl::changed (float v)
{
    v = info.clamp (v);

    if (v == value && info.kind != dsp::ParamKind::continuous)
        return;

    value = v;

    if (info.kind == dsp::ParamKind::toggle)
        setValue (v);

    repaint();

    if (onChange)
        onChange (v);
}

void ParamControl::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds();

    g.setColour (colour::textFaint);
    g.setFont (captionFont());
    g.drawFittedText (caption.isNotEmpty() ? caption : fxui::caps (fxui::utf8 (info.name)), r.removeFromTop (14),
                      juce::Justification::centred, 1, 0.7f);

    if (info.kind == dsp::ParamKind::continuous)
    {
        g.setColour (colour::text);
        g.setFont (numberFont (type::small));
        g.drawFittedText (fxui::formatValue (info, value), r.removeFromBottom (16), juce::Justification::centred, 1, 0.8f);
    }
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (16);

    switch (info.kind)
    {
        case dsp::ParamKind::continuous:
            r.removeFromBottom (16);
            knob.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
            break;

        case dsp::ParamKind::toggle:
            toggle.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 4, 76), 26));
            break;

        case dsp::ParamKind::choice:
            list.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, 28));
            break;
    }
}

//==============================================================================
void ReductionMeter::setReduction (float db)
{
    db = juce::jlimit (0.0f, range, db);

    if (db >= peakHold)
    {
        peakHold = db;
        holdFrames = 45;
    }
    else if (--holdFrames <= 0)
    {
        peakHold = juce::jmax (db, peakHold - 0.5f);
    }

    if (std::abs (db - current) > 0.05f || holdFrames > 0)
    {
        current = db;
        repaint();
    }
}

void ReductionMeter::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat().reduced (0.0f, 7.0f);   // espaço para os números das pontas
    auto bar = r.removeFromLeft (10.0f);

    g.setColour (colour::raised);
    g.fillRoundedRectangle (bar, 3.0f);

    const float h = bar.getHeight() * current / range;
    g.setColour (colour::signal);
    g.fillRoundedRectangle (bar.withHeight (h), 3.0f);

    const float hy = bar.getY() + bar.getHeight() * peakHold / range;
    g.fillRect (bar.getX(), hy - 1.0f, bar.getWidth(), 2.0f);

    g.setFont (numberFont (type::small));

    for (float db : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
    {
        if (db > range)
            break;

        const float y = bar.getY() + bar.getHeight() * db / range;
        g.setColour (colour::textFaint);
        g.drawText (juce::String ((int) db), r.withY (y - 7.0f).withHeight (14.0f).reduced (4.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void CorrelationMeter::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat();
    auto label = r.removeFromBottom (14.0f);
    auto bar = r.withSizeKeepingCentre (r.getWidth(), 8.0f);

    g.setColour (colour::raised);
    g.fillRoundedRectangle (bar, 4.0f);

    const float centre = bar.getCentreX();
    const float x = centre + value * bar.getWidth() * 0.5f;
    g.setColour (value < 0.0f ? colour::meterHigh : colour::textDim);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (centre, x), bar.getY(), std::abs (x - centre) + 2.0f, bar.getHeight()), 3.0f);

    g.setColour (colour::text);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x, bar.getCentreY() }));

    g.setFont (captionFont());
    g.setColour (colour::textFaint);
    g.drawText ("-1", label, juce::Justification::centredLeft);
    g.drawText ("CORRELAÇÃO "_t + juce::String (value, 2).replaceCharacter ('.', ','), label, juce::Justification::centred);
    g.drawText ("+1", label, juce::Justification::centredRight);
}

} // namespace whynot
