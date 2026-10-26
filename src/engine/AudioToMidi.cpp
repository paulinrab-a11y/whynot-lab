#include "AudioToMidi.h"
#include "../audio/Onsets.h"
#include "../dsp/PitchDetect.h"

#include <cmath>

namespace whynot::audiomidi
{

namespace
{
    /** Coeficiente de um filtro de um polo (passa-baixas) em fc. */
    double onePole (double fc, double rate)   { return 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * fc / rate); }

    int velocityOf (double peak)
    {
        // de -30 dBFS (força 40) a 0 dBFS (127)
        const double db = 20.0 * std::log10 (std::max (peak, 1.0e-6));
        return juce::jlimit (1, 127, (int) std::lround (40.0 + (db + 30.0) / 30.0 * 87.0));
    }
}

const char* roleName (DrumRole r) noexcept
{
    return r == DrumRole::kick ? "kick" : r == DrumRole::hat ? "hat" : "snare";
}

bool readMono (const juce::File& file, double fromSeconds, double toSeconds, std::vector<float>& out, double& rate)
{
    out.clear();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;

    rate = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    const auto from = juce::jlimit<juce::int64> (0, total, (juce::int64) std::floor (juce::jmax (0.0, fromSeconds) * rate));
    auto to = toSeconds > fromSeconds ? juce::jlimit<juce::int64> (from, total, (juce::int64) std::ceil (toSeconds * rate)) : total;
    to = juce::jmin (to, from + (juce::int64) (120.0 * rate));
    const int count = (int) (to - from);

    if (count <= 0)
        return false;

    juce::AudioBuffer<float> buffer ((int) juce::jmax (1u, reader->numChannels), count);

    if (! reader->read (&buffer, 0, count, from, true, true))
        return false;

    out.assign ((size_t) count, 0.0f);
    const float gain = 1.0f / (float) buffer.getNumChannels();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* x = buffer.getReadPointer (ch);

        for (int i = 0; i < count; ++i)
            out[(size_t) i] += x[i] * gain;
    }

    return true;
}

std::vector<DrumHit> drumHits (const std::vector<float>& input, double rate)
{
    std::vector<DrumHit> hits;

    // 20 ms de silêncio antes: a batida que começa na primeira amostra também tem de onde subir
    const auto lead = (size_t) std::lround (0.02 * rate);
    std::vector<float> mono (lead, 0.0f);
    mono.insert (mono.end(), input.begin(), input.end());
    const auto onsets = audio::detectOnsets (mono.data(), mono.size(), rate, audio::Sensitivity::medium);
    const double aLow = onePole (150.0, rate), aHigh = onePole (5000.0, rate);
    const auto window = (size_t) std::lround (0.06 * rate);

    for (size_t k = 0; k < onsets.size(); ++k)
    {
        const auto begin = (size_t) std::max (0.0, onsets[k]);
        auto end = std::min (mono.size(), begin + window);

        if (k + 1 < onsets.size())
            end = std::min (end, (size_t) std::max (0.0, onsets[k + 1]));

        if (end <= begin + 16)
            continue;

        // a parte grave (abaixo de 150 Hz) e a aguda (acima de 5 kHz) da energia do começo da batida, e quantas vezes o
        // som cruza o zero: o chimbal é ruído agudo (cruza quase a cada amostra), a caixa tem corpo
        double low = 0.0, lowE = 0.0, highLp = 0.0, highE = 0.0, all = 0.0, peak = 0.0;
        int crossings = 0;

        for (size_t i = begin; i < end; ++i)
        {
            const double x = mono[i];
            low += aLow * (x - low);
            highLp += aHigh * (x - highLp);
            const double high = x - highLp;
            lowE += low * low;
            highE += high * high;
            all += x * x;
            peak = std::max (peak, std::abs (x));

            if (i > begin && (mono[i] >= 0.0f) != (mono[i - 1] >= 0.0f))
                ++crossings;
        }

        if (all <= 1.0e-9)
            continue;

        DrumHit hit;
        hit.seconds = std::max (0.0, (onsets[k] - (double) lead) / rate);
        hit.lowShare = (float) (lowE / all);
        hit.highShare = (float) (highE / all);
        hit.crossings = (float) crossings / (float) (end - begin);
        hit.role = hit.lowShare > 0.4f                                                       ? DrumRole::kick
                 : hit.lowShare < 0.15f && (hit.crossings > 0.3f || hit.highShare > 0.3f) ? DrumRole::hat
                                                                                            : DrumRole::snare;
        hit.velocity = velocityOf (peak);
        hits.push_back (hit);
    }

    return hits;
}

std::vector<MelodyNote> melodyNotes (const std::vector<float>& mono, double rate)
{
    std::vector<MelodyNote> notes;
    const auto hop = (size_t) std::max (1L, std::lround (0.01 * rate));
    dsp::PitchOptions options;
    options.minHz = 40.0;    // Mi 1
    options.maxHz = 1200.0;  // Ré 6
    const auto frame = (size_t) std::ceil (2.0 * rate / options.minHz);

    if (mono.size() < frame + hop)
        return notes;

    // o nível de cada quadro, para o silêncio não virar nota
    std::vector<double> level;

    for (size_t pos = 0; pos + frame <= mono.size(); pos += hop)
    {
        double sum = 0.0;

        for (size_t i = pos; i < pos + hop; ++i)
            sum += (double) mono[i] * mono[i];

        level.push_back (std::sqrt (sum / (double) hop));
    }

    const double top = *std::max_element (level.begin(), level.end());

    if (top <= 1.0e-6)
        return notes;

    std::vector<int> keys (level.size(), -1);

    for (size_t f = 0; f < level.size(); ++f)
    {
        if (level[f] < top * 0.06)   // -24 dB do mais forte
            continue;

        const auto estimate = dsp::detectFramePitch (mono.data() + f * hop, frame, rate, options);

        if (estimate.found && estimate.confidence > 0.6)
            keys[f] = estimate.key;
    }

    // mediana de 3 quadros: um quadro errado no meio de uma nota não corta
    std::vector<int> smooth (keys.size(), -1);

    for (size_t f = 0; f < keys.size(); ++f)
    {
        const int a = f > 0 ? keys[f - 1] : keys[f], b = keys[f], c = f + 1 < keys.size() ? keys[f + 1] : keys[f];
        smooth[f] = std::max (std::min (a, b), std::min (std::max (a, b), c));
    }

    const size_t minFrames = 5;    // 50 ms
    const size_t maxGap = 3;       // 30 ms de buraco na mesma nota não corta
    size_t f = 0;

    while (f < smooth.size())
    {
        if (smooth[f] < 0)
        {
            ++f;
            continue;
        }

        const int key = smooth[f];
        size_t last = f, g = f + 1, gap = 0;
        double peak = level[f];

        for (; g < smooth.size(); ++g)
        {
            if (smooth[g] == key)
            {
                last = g;
                gap = 0;
                peak = std::max (peak, level[g]);
            }
            else if (++gap > maxGap)
            {
                break;
            }
        }

        if (last + 1 - f >= minFrames)
        {
            MelodyNote n;
            n.start = (double) (f * hop) / rate;
            n.end = (double) ((last + 1) * hop) / rate;
            n.key = key;
            n.velocity = velocityOf (peak * std::sqrt (2.0));
            notes.push_back (n);
        }

        f = last + 1;
    }

    return notes;
}

} // namespace whynot::audiomidi
