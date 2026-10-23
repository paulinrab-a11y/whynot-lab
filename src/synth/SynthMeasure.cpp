#include "SynthMeasure.h"
#include "Synth.h"

#include "../dsp/LoudnessMeter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace whynot::synth
{

std::array<int, 4> chordKeys (int root) noexcept
{
    return { root, root + 3, root + 7, root + 10 };
}

LevelReading measureLevel (std::span<const float> values, int key, double sampleRate)
{
    const int keys[] { key };
    return measureLevel (values, keys, sampleRate);
}

LevelReading measureLevel (std::span<const float> values, std::span<const int> keys, double sampleRate)
{
    constexpr int block = 256;
    Synth synth;
    synth.setAll (values);
    synth.setParam (P::bpm, 120.0f);
    synth.prepare (sampleRate, block);

    dsp::LoudnessMeter meter;
    meter.prepare (sampleRate);

    // Ataque lento (pads, texturas): a nota dura o bastante para chegar ao volume cheio.
    const double attack = values.size() > (size_t) P::env (0, eAttack) ? values[(size_t) P::env (0, eAttack)] * 0.001 : 0.0;
    const double noteSeconds = std::clamp (attack * 1.3 + 0.8, levelNoteSeconds, 5.0);
    const int noteOff = (int) std::lround (noteSeconds * sampleRate);
    const int total = noteOff + (int) std::lround (0.6 * sampleRate);
    std::vector<float> left ((size_t) block), right ((size_t) block);
    LevelReading r;
    float peak = 0.0f;

    for (int start = 0; start < total; start += block)
    {
        const int n = std::min (block, total - start);
        Event events[8];
        int numEvents = 0;

        for (int key : keys.first (std::min<size_t> (keys.size(), 8)))
        {
            if (start == 0)
                events[numEvents++] = { 0, Event::Type::noteOn, key, levelVelocity };
            else if (noteOff >= start && noteOff < start + n)
                events[numEvents++] = { noteOff - start, Event::Type::noteOff, key, 0.0f };
        }

        synth.process (left.data(), right.data(), n, events, numEvents);
        meter.process (left.data(), right.data(), n);
        peak = std::max (peak, synth.lastPeakBeforeClip());
        r.lufs = std::max (r.lufs, meter.momentary());

        for (int i = 0; i < n; ++i)
            r.finite &= std::isfinite (left[(size_t) i]) && std::isfinite (right[(size_t) i]);
    }

    r.peakDb = 20.0f * std::log10 (std::max (peak, 1.0e-6f));
    return r;
}

float levelledVolume (const Preset& p, double sampleRate)
{
    // Mede bem abaixo do teto (nada encosta no soft clip) e corrige: o volume é a última coisa da cadeia.
    constexpr float probe = -24.0f;
    auto values = p.values;
    values[(size_t) P::volume] = probe;

    const auto* category = findCategory (p.category);
    const auto r = measureLevel (values, category != nullptr ? category->testKey : 60, sampleRate);

    if (r.lufs <= -90.0f)
        return 0.0f;

    const float byLoudness = levelTargetLufs - r.lufs;
    float byPeak = levelPeakCeilingDb - r.peakDb;

    // Timbre de acorde: o acorde de 4 notas também cabe abaixo do teto (sem encostar no soft clip).
    if (category != nullptr && isChordCategory (category->id))
    {
        const auto chord = chordKeys (category->testKey);
        const auto rc = measureLevel (values, chord, sampleRate);
        byPeak = std::min (byPeak, levelChordPeakCeilingDb - rc.peakDb);
    }

    return param (P::volume).clamp (probe + std::min (byLoudness, byPeak));
}

} // namespace whynot::synth
