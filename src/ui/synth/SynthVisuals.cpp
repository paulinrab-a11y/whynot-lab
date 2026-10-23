#include "SynthVisuals.h"
#include "../Theme.h"
#include "../../dsp/Svf.h"
#include "../../synth/SynthDsp.h"
#include "../../synth/SynthParams.h"
#include "../../synth/Wavetables.h"

namespace whynot::synthui
{

using namespace synth;

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    juce::String utf8 (const char* s)   { return juce::String::fromUTF8 (s); }

    /** Rótulo da escolha atual de um parâmetro de lista. */
    juce::String choiceText (const std::vector<float>& v, int index)
    {
        const auto& p = param (index);
        const int i = juce::jlimit (0, (int) p.choices.size() - 1, juce::roundToInt (v[(size_t) index]));
        return p.choices.empty() ? juce::String() : utf8 (p.choices[(size_t) i]);
    }

    float read (const float* table, float t)
    {
        const float pos = t * (float) Wavetables::tableSize;
        const int i = juce::jlimit (0, Wavetables::tableSize - 1, (int) pos);
        return table[i] + (pos - (float) i) * (table[i + 1] - table[i]);
    }

    void normalise (std::vector<float>& points)
    {
        float peak = 0.0f;

        for (auto x : points)
            peak = juce::jmax (peak, std::abs (x));

        if (peak > 1.0e-6f)
            for (auto& x : points)
                x /= peak;
    }

    juce::Path polyline (const std::vector<float>& points, juce::Rectangle<float> r, float lo, float hi)
    {
        juce::Path p;

        for (size_t i = 0; i < points.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) juce::jmax<size_t> (1, points.size() - 1);
            const float y = juce::jmap (juce::jlimit (lo, hi, points[i]), lo, hi, r.getBottom(), r.getY());

            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }

        return p;
    }
}

juce::Path starPath (juce::Rectangle<float> area)
{
    juce::Path p;
    p.addStar (area.getCentre(), 5, area.getWidth() * 0.22f, area.getWidth() * 0.5f, 0.0f);
    return p;
}

//==============================================================================
void Graph::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat();
    g.setColour (colour::background.withAlpha (0.55f));
    g.fillRoundedRectangle (r, metric::radius);

    paintGraph (g, plotArea());

    auto text = r.reduced (10.0f, 4.0f).removeFromBottom (14.0f);
    g.setFont (captionFont());
    g.setColour (colour::textFaint);
    g.drawText (caption.toUpperCase(), text, juce::Justification::centredLeft);
    g.setColour (dim ? colour::textFaint : colour::textDim);
    g.drawText (note, text, juce::Justification::centredRight);
}

//==============================================================================
void WaveView::show (const std::vector<float>& v, int layer)
{
    constexpr int n = 256;
    constexpr int level = 3;   // 128 harmônicos: a forma sem o serrilhado do Gibbs exagerado
    const auto& tables = Wavetables::get();
    points.assign (n, 0.0f);
    note = {};

    if (layer < 0)
    {
        dim = v[(size_t) P::subOn] < 0.5f;
        const auto wave = (SubWave) juce::roundToInt (v[(size_t) P::subWave]);
        caption = "Sub " + choiceText (v, P::subOct);
        note = choiceText (v, P::subWave);

        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) n;
            const float* saw = tables.basic (Wavetables::basicSaw, level);

            switch (wave)
            {
                case SubWave::sine:     points[(size_t) i] = std::sin (twoPi * t); break;
                case SubWave::triangle: points[(size_t) i] = read (tables.basic (Wavetables::basicTriangle, level), t); break;
                case SubWave::square:   points[(size_t) i] = read (saw, std::fmod (t + 0.5f, 1.0f)) - read (saw, t); break;
            }
        }

        normalise (points);
        repaint();
        return;
    }

    auto at = [&v, layer] (int p) { return v[(size_t) P::layer (layer, p)]; };
    dim = at (lOn) < 0.5f;
    const auto type = (LayerType) juce::roundToInt (at (lType));
    const float shape = at (lShape) * 0.01f;
    caption = "Camada " + juce::String (layer + 1) + ": " + choiceText (v, P::layer (layer, lType));

    switch (type)
    {
        case LayerType::analog:
        {
            const auto wave = (Wave) juce::roundToInt (at (lWave));
            const float* saw = tables.basic (Wavetables::basicSaw, level);
            const float* tri = tables.basic (Wavetables::basicTriangle, level);
            const float width = juce::jlimit (0.02f, 0.98f, shape);
            note = choiceText (v, P::layer (layer, lWave)) + (wave == Wave::pulse ? " " + juce::String (juce::roundToInt (width * 100.0f)) + "%" : juce::String());

            for (int i = 0; i < n; ++i)
            {
                const float t = (float) i / (float) n;

                switch (wave)
                {
                    case Wave::saw:      points[(size_t) i] = read (saw, t); break;
                    case Wave::triangle: points[(size_t) i] = read (tri, t); break;
                    case Wave::sine:     points[(size_t) i] = std::sin (twoPi * t); break;
                    case Wave::pulse:    points[(size_t) i] = read (saw, std::fmod (t + 1.0f - width, 1.0f)) - read (saw, t); break;
                }
            }
            break;
        }

        case LayerType::wavetable:
        {
            const int table = juce::jlimit (0, Wavetables::numTables() - 1, juce::roundToInt (at (lTable)));
            const float pos = juce::jlimit (0.0f, 1.0f, shape) * (Wavetables::framesPerTable - 1);
            const int f0 = juce::jmin ((int) pos, Wavetables::framesPerTable - 2);
            const float* a = tables.frame (table, f0, level);
            const float* b = tables.frame (table, f0 + 1, level);
            note = choiceText (v, P::layer (layer, lTable)) + " " + juce::String (juce::roundToInt (shape * 100.0f)) + "%";

            for (int i = 0; i < n; ++i)
            {
                const float t = (float) i / (float) n;
                points[(size_t) i] = read (a, t) + (pos - (float) f0) * (read (b, t) - read (a, t));
            }
            break;
        }

        case LayerType::fm:
        {
            // O começo da nota (brilho máximo): índice em radianos, como no motor.
            const auto algo = (FmAlgo) juce::roundToInt (at (lFmAlgo));
            const float scale = 2.0f * shape * 8.0f * 0.01f;
            const float iB = at (lFmAmt2) * scale, iC = at (lFmAmt3) * scale, iD = at (lFmAmt4) * scale;
            const float rB = at (lFmRatio2), rC = at (lFmRatio3), rD = at (lFmRatio4);
            note = choiceText (v, P::layer (layer, lFmAlgo));

            for (int i = 0; i < n; ++i)
            {
                const float t = (float) i / (float) n;
                const float b = std::sin (twoPi * rB * t), c = std::sin (twoPi * rC * t), d = std::sin (twoPi * rD * t);
                float out = 0.0f;

                switch (algo)
                {
                    case FmAlgo::twoOp:         out = std::sin (twoPi * t + iB * b); break;
                    case FmAlgo::threeSerial:   out = std::sin (twoPi * t + iB * std::sin (twoPi * rB * t + iC * c)); break;
                    case FmAlgo::threeParallel: out = std::sin (twoPi * t + iB * b + iC * c); break;
                    case FmAlgo::fourSerial:    out = std::sin (twoPi * t + iB * std::sin (twoPi * rB * t + iC * std::sin (twoPi * rC * t + iD * d))); break;
                    case FmAlgo::fourParallel:  out = std::sin (twoPi * t + iB * b + iC * c + iD * d); break;
                    case FmAlgo::twoPairs:
                    case FmAlgo::count:         out = std::sin (twoPi * t + iB * b) + std::sin (twoPi * rC * t + iD * d); break;
                }

                points[(size_t) i] = out;
            }
            break;
        }

        case LayerType::noise:
        {
            Random rng (1234);
            const float a = shape < 0.5f ? 0.05f + 0.9f * shape * 2.0f : 1.0f;
            float y = 0.0f;
            note = shape < 0.4f ? "Escuro" : (shape > 0.6f ? "Claro" : "Branco");

            for (int i = 0; i < n; ++i)
            {
                const float x = rng.bipolar();
                y += a * (x - y);
                points[(size_t) i] = shape > 0.5f ? x - y * (1.0f - (shape - 0.5f) * 2.0f) : y;
            }
            break;
        }
    }

    if (dim)
        note = "Desligada";

    normalise (points);
    repaint();
}

void WaveView::paintGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace theme;
    g.setColour (colour::line);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
    g.setColour (dim ? colour::textFaint : colour::text);
    g.strokePath (polyline (points, r.reduced (0.0f, 4.0f), -1.05f, 1.05f), juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
}

//==============================================================================
void FilterView::show (const std::vector<float>& v)
{
    constexpr int n = 128;
    constexpr double sampleRate = 48000.0;
    const bool on = v[(size_t) P::fltOn] >= 0.5f;
    const auto mode = (FilterMode) juce::roundToInt (v[(size_t) P::fltMode]);
    const bool steep = juce::roundToInt (v[(size_t) P::fltSlope]) == 1;
    const float cutoff = v[(size_t) P::fltCutoff], envOct = v[(size_t) P::fltEnv];
    const double q = 0.5 * std::pow (40.0, v[(size_t) P::fltReso] * 0.01);

    const auto type = mode == FilterMode::highpass ? dsp::FilterType::highpass
                    : mode == FilterMode::bandpass ? dsp::FilterType::bandpass
                    : mode == FilterMode::notch    ? dsp::FilterType::notch
                                                   : dsp::FilterType::lowpass;

    auto response = [&] (double fc, std::vector<float>& out)
    {
        fc = juce::jlimit (16.0, 0.47 * sampleRate, fc);
        const bool butterworth = type == dsp::FilterType::lowpass || type == dsp::FilterType::highpass;
        const auto first = dsp::SvfCoeffs::make (type, fc, steep ? (butterworth ? 0.541 : q) : q, 0.0, sampleRate);
        const auto second = dsp::SvfCoeffs::make (type, fc, q * 1.85, 0.0, sampleRate);
        out.resize (n);

        for (int i = 0; i < n; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / (double) (n - 1));
            out[(size_t) i] = (float) (first.magnitudeDb (f, sampleRate) + (steep ? second.magnitudeDb (f, sampleRate) : 0.0));
        }
    };

    dim = ! on;
    caption = on ? choiceText (v, P::fltMode) + " " + choiceText (v, P::fltSlope) : juce::String ("Filtro");
    note = on ? (cutoff >= 1000.0f ? juce::String (cutoff / 1000.0f, 1).replaceCharacter ('.', ',') + " kHz"
                                   : juce::String (juce::roundToInt (cutoff)) + " Hz")
              : juce::String ("Desligado");

    if (on)
        response (cutoff, rest);
    else
        rest.assign (n, 0.0f);

    opened.clear();

    if (on && std::abs (envOct) > 0.05f)
        response (cutoff * std::pow (2.0, (double) envOct), opened);

    repaint();
}

void FilterView::paintGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace theme;
    constexpr float lo = -36.0f, hi = 18.0f;
    g.setFont (captionFont());

    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = r.getX() + r.getWidth() * std::log10 (f / 20.0f) / 3.0f;
        g.setColour (colour::line);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (colour::textFaint);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x + 3.0f, r.getY(), 30.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

    const float zero = juce::jmap (0.0f, lo, hi, r.getBottom(), r.getY());
    g.setColour (colour::raisedHigh);
    g.drawHorizontalLine ((int) zero, r.getX(), r.getRight());

    if (! opened.empty())
    {
        g.setColour (colour::textFaint);
        g.strokePath (polyline (opened, r, lo, hi), juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
    }

    g.setColour (dim ? colour::textFaint : colour::text);
    g.strokePath (polyline (rest, r, lo, hi), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
}

//==============================================================================
void EnvView::show (const std::vector<float>& v, int e)
{
    const float a = v[(size_t) P::env (e, eAttack)], d = v[(size_t) P::env (e, eDecay)];
    const float s = v[(size_t) P::env (e, eSustain)] * 0.01f, rel = v[(size_t) P::env (e, eRelease)];
    const float hold = juce::jmax (80.0f, 0.3f * (a + d + rel));
    const float total = a + d + hold + rel;
    const double rate = 600.0 / (total * 0.001);

    EnvelopeCoeffs coeffs;
    coeffs.set (a, d, s, rel, v[(size_t) P::env (e, eCurve)] * 0.01f, rate);
    Envelope env;
    env.noteOn();
    points.clear();

    const int offAt = (int) ((a + d + hold) * 0.001 * rate);

    for (int i = 0; i < 600; ++i)
    {
        if (i == offAt)
            env.noteOff();

        points.push_back (env.next (coeffs));
    }

    releaseStart = (float) offAt / 600.0f;
    dim = false;
    caption = utf8 (sectionOf (P::env (e, eAttack)));
    note = {};   // os tempos já estão nos botões embaixo do desenho
    repaint();
}

void EnvView::paintGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace theme;
    const float x = r.getX() + r.getWidth() * releaseStart;
    g.setColour (colour::line);
    g.drawVerticalLine ((int) x, r.getY(), r.getBottom());

    auto path = polyline (points, r, 0.0f, 1.0f);
    auto fill = path;
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setColour (colour::text.withAlpha (0.06f));
    g.fillPath (fill);
    g.setColour (colour::text);
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
}

//==============================================================================
void LfoView::show (const std::vector<float>& v, int l)
{
    const auto shape = (LfoShape) juce::roundToInt (v[(size_t) P::lfo (l, lfoShape)]);
    const bool random = shape == LfoShape::stepRandom || shape == LfoShape::smoothRandom;
    const int cycles = random ? 5 : 2;
    constexpr int n = 240;
    Random rng (77);
    std::vector<float> held ((size_t) cycles + 1);

    for (auto& h : held)
        h = rng.bipolar();

    points.assign (n, 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const float pos = (float) i / (float) n * (float) cycles;
        const int c = (int) pos;
        const float ph = pos - (float) c;
        float y = 0.0f;

        switch (shape)
        {
            case LfoShape::sine:         y = std::sin (twoPi * ph); break;
            case LfoShape::triangle:     { const float u = std::fmod (ph + 0.75f, 1.0f); y = 4.0f * std::abs (u - 0.5f) - 1.0f; break; }
            case LfoShape::rampUp:       y = 2.0f * ph - 1.0f; break;
            case LfoShape::rampDown:     y = 1.0f - 2.0f * ph; break;
            case LfoShape::square:       y = ph < 0.5f ? 1.0f : -1.0f; break;
            case LfoShape::stepRandom:   y = held[(size_t) c + 1]; break;
            case LfoShape::smoothRandom: y = held[(size_t) c] + (held[(size_t) c + 1] - held[(size_t) c]) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * ph)); break;
        }

        points[(size_t) i] = y;
    }

    dim = false;
    caption = utf8 (sectionOf (P::lfo (l, lfoShape))) + ": " + choiceText (v, P::lfo (l, lfoShape));
    const float hz = v[(size_t) P::lfo (l, lfoRate)];
    note = v[(size_t) P::lfo (l, lfoSync)] >= 0.5f ? choiceText (v, P::lfo (l, lfoDiv)) + " no BPM"
                                                   : juce::String (hz, hz < 10.0f ? 2 : 1).replaceCharacter ('.', ',') + " Hz";
    repaint();
}

void LfoView::paintGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace theme;
    g.setColour (colour::line);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
    g.setColour (colour::text);
    g.strokePath (polyline (points, r.reduced (0.0f, 4.0f), -1.05f, 1.05f), juce::PathStrokeType (1.6f));
}

} // namespace whynot::synthui
