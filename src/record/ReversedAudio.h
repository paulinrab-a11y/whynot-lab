#pragma once

// O arquivo de trás para frente de um áudio (voz na playlist, 27/10): o audio.reverse escreve e o Projector
// toca no bloco com PlaylistItem::reversed, sem canal novo. Fica ao lado do original ("Voz_take03_invertido.wav");
// se ele sumir (o projeto foi copiado sem ele), o Projector escreve de novo a partir do original.

#include <JuceHeader.h>

#include "../common/Text.h"

namespace whynot::reversed
{

inline juce::File fileFor (const juce::File& source)
{
    return source.getSiblingFile (source.getFileNameWithoutExtension() + "_invertido.wav");
}

/** Escreve `target` com `source` de trás para frente, em WAV 32 bits float (igual à entrada, amostra por amostra).
    `seconds` recebe a duração. Até uma hora de áudio. */
inline juce::Result write (const juce::File& source, const juce::File& target, double& seconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > (juce::int64) 48000 * 60 * 60)
        return juce::Result::fail ("Não consegui ler o áudio do bloco: "_t + source.getFullPathName());

    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> all (channels, (int) reader->lengthInSamples);
    reader->read (&all, 0, all.getNumSamples(), 0, true, channels > 1);
    all.reverse (0, all.getNumSamples());
    seconds = all.getNumSamples() / reader->sampleRate;

    target.getParentDirectory().createDirectory();
    target.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (target.createOutputStream().release());

    if (stream == nullptr)
        return juce::Result::fail ("Não consegui criar "_t + target.getFullPathName());

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                 .withSampleRate (reader->sampleRate)
                                                 .withNumChannels (channels)
                                                 .withBitsPerSample (32)
                                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));

    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (all, 0, all.getNumSamples()))
        return juce::Result::fail ("Não consegui escrever "_t + target.getFullPathName());

    return juce::Result::ok();
}

/** O arquivo que um bloco toca: o original, ou o invertido se o bloco estiver invertido (escrito de novo se sumiu). */
inline juce::File playedFile (const juce::File& source, bool isReversed)
{
    if (! isReversed)
        return source;

    const auto target = fileFor (source);
    double seconds = 0.0;

    if (! target.existsAsFile() && source.existsAsFile())
        write (source, target, seconds);

    return target;
}

} // namespace whynot::reversed
