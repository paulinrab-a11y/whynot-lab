#include "AudioPreview.h"

#include "../common/Text.h"
#include "../dsp/LoudnessMeter.h"

namespace whynot::control
{

namespace
{
    constexpr float silenceLevel = 0.001f;   // -60 dBFS
    constexpr float clipLevel = 0.9999f;     // 0 dBFS, com folga para o arredondamento do PCM

    float toDb (double energy)   { return energy > 1.0e-12 ? (float) (10.0 * std::log10 (energy)) : -100.0f; }

    double tenth (double v)      { return std::round (v * 10.0) / 10.0; }
}

AudioMeasures measureAudio (const juce::File& file, double bpm, int beatsPerBar)
{
    AudioMeasures m;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
    {
        m.error = "Não consegui ler o áudio da prévia: "_t + file.getFullPathName();
        return m;
    }

    const double rate = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    const int channels = (int) juce::jmin (2u, reader->numChannels);   // a prévia é estéreo; um 3º canal não conta

    m.sampleRate = rate;
    m.channels = channels;
    m.seconds = (double) total / rate;
    m.barSeconds = 60.0 / juce::jmax (1.0, bpm) * juce::jmax (1, beatsPerBar);

    const double barSamples = m.barSeconds * rate;
    const auto bars = (juce::int64) std::ceil ((double) total / barSamples);
    const int keptBars = (int) juce::jlimit<juce::int64> (0, AudioMeasures::maxBars, bars);
    m.barsCut = bars > AudioMeasures::maxBars;

    std::vector<double> barEnergy ((size_t) keptBars, 0.0), barCount ((size_t) keptBars, 0.0);

    dsp::LoudnessMeter meter;
    meter.prepare (rate);

    constexpr int blockSize = 32768;
    juce::AudioBuffer<float> block (2, blockSize);
    double energy = 0.0;
    float peak = 0.0f;
    juce::int64 firstLoud = -1, lastLoud = -1;

    for (juce::int64 pos = 0; pos < total; pos += blockSize)
    {
        const int n = (int) juce::jmin<juce::int64> (blockSize, total - pos);
        block.clear();
        reader->read (&block, 0, n, pos, true, channels > 1);

        const float* left = block.getReadPointer (0);
        const float* right = channels > 1 ? block.getReadPointer (1) : left;   // mono: o mesmo nos dois lados
        meter.process (left, right, n);

        for (int i = 0; i < n; ++i)
        {
            const float l = std::abs (left[i]), r = std::abs (right[i]);
            const float loudest = juce::jmax (l, r);
            const double square = channels > 1 ? 0.5 * ((double) l * l + (double) r * r) : (double) l * l;

            peak = juce::jmax (peak, loudest);
            energy += square;
            m.clipped += (l >= clipLevel ? 1 : 0) + (channels > 1 && r >= clipLevel ? 1 : 0);

            if (loudest > silenceLevel)
            {
                if (firstLoud < 0)
                    firstLoud = pos + i;

                lastLoud = pos + i;
            }

            if (const auto bar = (juce::int64) ((double) (pos + i) / barSamples); bar < keptBars)
            {
                barEnergy[(size_t) bar] += square;
                barCount[(size_t) bar] += 1.0;
            }
        }
    }

    m.peakDb = peak > 0.0f ? juce::Decibels::gainToDecibels (peak, -100.0f) : -100.0f;
    m.truePeakDb = meter.truePeak();
    m.rmsDb = total > 0 ? toDb (energy / (double) total) : -100.0f;
    m.lufs = meter.integrated();
    m.silenceStart = firstLoud < 0 ? m.seconds : (double) firstLoud / rate;
    m.silenceEnd = lastLoud < 0 ? m.seconds : (double) (total - 1 - lastLoud) / rate;

    for (size_t b = 0; b < barEnergy.size(); ++b)
        m.barsRmsDb.push_back (barCount[b] > 0.0 ? toDb (barEnergy[b] / barCount[b]) : -100.0f);

    m.ok = true;
    return m;
}

juce::var toVar (const AudioMeasures& m)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("seconds", tenth (m.seconds * 10.0) / 10.0);   // duas casas: 7,95 s
    o->setProperty ("sampleRate", m.sampleRate);
    o->setProperty ("channels", m.channels);
    o->setProperty ("peakDb", tenth (m.peakDb));
    o->setProperty ("truePeakDb", tenth (m.truePeakDb));
    o->setProperty ("rmsDb", tenth (m.rmsDb));
    o->setProperty ("lufs", tenth (m.lufs));
    o->setProperty ("clipped", m.clipped);
    o->setProperty ("silenceStart", tenth (m.silenceStart * 10.0) / 10.0);
    o->setProperty ("silenceEnd", tenth (m.silenceEnd * 10.0) / 10.0);
    o->setProperty ("barSeconds", tenth (m.barSeconds * 10.0) / 10.0);

    juce::Array<juce::var> bars;

    for (auto db : m.barsRmsDb)
        bars.add (tenth (db));

    o->setProperty ("barsRmsDb", bars);
    o->setProperty ("barsCut", m.barsCut);
    return juce::var (o);
}

} // namespace whynot::control
