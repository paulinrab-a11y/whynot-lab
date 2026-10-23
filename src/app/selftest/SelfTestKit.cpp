#include "SelfTestKit.h"

namespace whynot::selftest
{

void Report::check (bool ok, const juce::String& what)
{
    lines.add ((ok ? "  ok: " : "  FALHOU: ") + what);
    if (! ok) ++failures;
    flush();
}

void Report::section (const juce::String& title)
{
    lines.add (title);
    flush();
}

void Report::flush() const
{
    if (file != juce::File())
        file.replaceWithText (lines.joinIntoString (juce::newLine) + juce::newLine);
}

juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
{
    auto o = new juce::DynamicObject();
    for (auto& [k, v] : kv) o->setProperty (k, v);
    return juce::var (o);
}

Audio readWav (const juce::File& f)
{
    Audio a;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));

    if (r == nullptr)
        return a;

    a.buffer.setSize ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&a.buffer, 0, (int) r->lengthInSamples, 0, true, true);
    a.sampleRate = r->sampleRate;
    a.valid = true;
    return a;
}

float peakDb (const Audio& a)
{
    float peak = 0;
    for (int c = 0; c < a.buffer.getNumChannels(); ++c)
        peak = juce::jmax (peak, a.buffer.getMagnitude (c, 0, a.buffer.getNumSamples()));
    return juce::Decibels::gainToDecibels (peak, -200.0f);
}

float maxDiff (const Audio& a, const Audio& b)
{
    if (a.buffer.getNumChannels() != b.buffer.getNumChannels() || a.buffer.getNumSamples() != b.buffer.getNumSamples())
        return 1.0e9f;

    float d = 0;
    for (int c = 0; c < a.buffer.getNumChannels(); ++c)
        for (int i = 0; i < a.buffer.getNumSamples(); ++i)
            d = juce::jmax (d, std::abs (a.buffer.getSample (c, i) - b.buffer.getSample (c, i)));
    return d;
}

/** Frequência média por cruzamentos de zero subindo, no canal 0, entre t0 e t1 segundos. */
double measureHz (const Audio& a, double t0, double t1)
{
    const int from = (int) (t0 * a.sampleRate), to = juce::jmin (a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));
    const float* x = a.buffer.getReadPointer (0);
    int first = -1, last = -1, crossings = 0;

    for (int i = juce::jmax (1, from); i < to; ++i)
    {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            if (crossings == 0) first = i;
            last = i;
            ++crossings;
        }
    }

    return crossings < 2 ? 0.0 : (crossings - 1) * a.sampleRate / (double) (last - first);
}

float peakIn (const Audio& a, double t0, double t1)
{
    const int from = juce::jmax (0, (int) (t0 * a.sampleRate));
    const int to = juce::jmin (a.buffer.getNumSamples(), (int) (t1 * a.sampleRate));
    float peak = 0.0f;

    for (int c = 0; c < a.buffer.getNumChannels() && to > from; ++c)
        peak = juce::jmax (peak, a.buffer.getMagnitude (c, from, to - from));

    return peak;
}

/** Correlação normalizada entre `b` (inteiro) e o trecho de `a` que começa em `startSeconds`. */
double correlation (const Audio& a, double startSeconds, const Audio& b)
{
    const int offset = juce::roundToInt (startSeconds * a.sampleRate);
    const int n = juce::jmin (b.buffer.getNumSamples(), a.buffer.getNumSamples() - offset);
    double ab = 0, aa = 0, bb = 0;

    for (int i = 0; i < n; ++i)
    {
        const double x = a.buffer.getSample (0, offset + i), y = b.buffer.getSample (0, i);
        ab += x * y; aa += x * x; bb += y * y;
    }

    return (aa > 0 && bb > 0) ? ab / std::sqrt (aa * bb) : 0.0;
}

juce::var noteVar (double t, double len, int key, bool slide)
{
    auto o = new juce::DynamicObject();
    o->setProperty ("t", t);
    o->setProperty ("len", len);
    o->setProperty ("key", key);
    o->setProperty ("vel", 110);

    if (slide)
        o->setProperty ("slide", true);

    return juce::var (o);
}

juce::String channelId (const Project& p, const juce::String& name)
{
    for (auto& c : p.channels)
        if (c.name == name)
            return c.id;
    return {};
}

float rmsDb (const Audio& x)
{
    double sum = 0.0;
    for (int c = 0; c < x.buffer.getNumChannels(); ++c)
        for (int i = 0; i < x.buffer.getNumSamples(); ++i)
            sum += (double) x.buffer.getSample (c, i) * x.buffer.getSample (c, i);
    const auto n = juce::jmax (1, x.buffer.getNumChannels() * x.buffer.getNumSamples());
    return juce::Decibels::gainToDecibels ((float) std::sqrt (sum / n), -200.0f);
}

bool setGrid (AppController& app, const juce::String& channel, const char* grid, int velocity)
{
    return app.run ("steps.setGrid", args ({ { "channel", channel }, { "grid", grid }, { "velocity", velocity } })).ok;
}

CommandResult addNotes (AppController& app, const juce::String& channel, const juce::Array<juce::var>& notes)
{
    return app.run ("pianoroll.addNotes", args ({ { "channel", channel }, { "notes", notes }, { "unit", "beats" } }));
}

} // namespace whynot::selftest
