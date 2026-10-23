#include "PreviewPlayer.h"

namespace whynot
{

bool PreviewPlayer::play (const juce::File& file, juce::AudioFormatManager& formats)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;

    auto clip = std::make_unique<Clip>();
    const auto frames = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    clip->audio.setSize (channels, frames);
    reader->read (&clip->audio, 0, frames, 0, true, channels > 1);
    clip->sampleRate = reader->sampleRate;
    clip->id = nextId++;
    lastId = clip->id;

    // sem denormais: o callback não precisa de proteção extra na cauda
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* d = clip->audio.getWritePointer (ch);

        for (int i = 0; i < frames; ++i)
            if (std::abs (d[i]) < 1.0e-15f)
                d[i] = 0.0f;
    }

    // Se o áudio estiver atrasado, a prévia espera em `pending` e service() tenta de novo
    // (publish esvazia o ponteiro quando entrega).
    pending = std::move (clip);
    slot.publish (pending);
    return true;
}

void PreviewPlayer::stop() noexcept
{
    // A que nem chegou ao áudio não toca mais. Ela era a última pedida (o pending é sempre a mais nova):
    // sem isto, isPlaying() esperaria para sempre um fim que a thread de áudio nunca vai marcar.
    if (pending != nullptr)
    {
        pending = nullptr;
        lastId = 0;
    }

    stopRequests.fetch_add (1);
}

void PreviewPlayer::service() noexcept
{
    slot.collectGarbage();

    if (pending != nullptr)
        slot.publish (pending);
}

void PreviewPlayer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr && device->getCurrentSampleRate() > 0.0)
        deviceRate = device->getCurrentSampleRate();
}

void PreviewPlayer::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                      int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    // o JUCE soma este callback com os outros: aqui se escreve, não se acumula
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const auto* clip = slot.current();

    if (clip == nullptr)
        return;

    if (clip->id != playingId)   // prévia nova: começa do zero
    {
        playingId = clip->id;
        position = 0.0;
        fade = 1.0f;
        stopping = false;
        finished = false;
    }

    if (const auto stops = stopRequests.load(); stops != seenStops)
    {
        seenStops = stops;
        stopping = true;
    }

    if (finished)
        return;

    const int frames = clip->audio.getNumSamples();
    const int clipChannels = clip->audio.getNumChannels();
    const double step = clip->sampleRate / deviceRate.load();
    const float fadeStep = 1.0f / (0.01f * (float) deviceRate.load());   // 10 ms

    for (int i = 0; i < numSamples; ++i)
    {
        const int index = (int) position;

        if (index + 1 >= frames || fade <= 0.0f)
        {
            finished = true;
            finishedId.store (playingId);
            break;
        }

        const float frac = (float) (position - index);

        if (stopping)
            fade = juce::jmax (0.0f, fade - fadeStep);

        for (int ch = 0; ch < numOutputs; ++ch)
        {
            if (outputs[ch] == nullptr)
                continue;

            const auto* src = clip->audio.getReadPointer (juce::jmin (ch, clipChannels - 1));
            const float s = src[index] + (src[index + 1] - src[index]) * frac;   // interpolação linear
            outputs[ch][i] = s * gain * fade;
        }

        position += step;
    }
}

} // namespace whynot
