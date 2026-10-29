#pragma once

// O arquivo que um bloco de áudio toca (playlist fatia 6, 27/10 noite). Normalmente é o sample do canal; um bloco
// consolidado sem canal novo tem o arquivo dele (PlaylistItem::file), ao lado do original. O reverso vale nos dois.
// Os comandos que leem o som do bloco (normalizar, silêncio, picotar, MIDI) passam por aqui, como o Projector.

#include "ReversedAudio.h"
#include "../model/ProjectModel.h"
#include "../engine/SampleAnalysis.h"

#include <map>

namespace whynot::blockfile
{

/** O arquivo de origem do bloco, antes do reverso: o dele, se houver, ou o sample do canal. */
inline juce::File sourceOf (const ProjectModel& model, const Channel& channel, const PlaylistItem& item)
{
    return model.resolveSample (item.file.isNotEmpty() ? item.file : channel.samplePath);
}

/** O que o bloco toca de verdade: a origem, ou o invertido ao lado dela (escrito de novo se sumiu). */
inline juce::File playedOf (const ProjectModel& model, const Channel& channel, const PlaylistItem& item)
{
    return reversed::playedFile (sourceOf (model, channel, item), item.reversed);
}

/** Para o pan do bloco: o Tracktion só aplica o pan de um clipe estéreo (num mono ele é ignorado). Um arquivo mono
    com pan toca uma cópia em dois canais ao lado ("Voz_wnl-estereo.wav", WAV 32 bits float, as duas metades iguais),
    escrita de novo se sumir ou se o original mudar depois dela. Num MP3, a cópia já sai sem o atraso do codificador
    (o Projector só compensa esse atraso no próprio MP3), para o bloco não andar ao ligar o pan. Copia em pedaços, sem
    o arquivo inteiro na memória. Um arquivo com o nome da cópia que não parece uma cópia nossa (outra taxa, outro
    tamanho, não estéreo) não é apagado: o bloco fica sem pan. Estéreo, ou sem conseguir escrever, fica o próprio. */
inline juce::File stereoOf (const juce::File& file)
{
    static juce::CriticalSection lock;   // o cache de quantos canais cada arquivo tem
    static std::map<juce::String, std::pair<juce::Time, unsigned int>> channelsSeen;
    const juce::ScopedLock sl (lock);

    if (! file.existsAsFile())
        return file;

    const auto modified = file.getLastModificationTime();
    auto& seen = channelsSeen[file.getFullPathName()];

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    if (seen.first != modified || seen.second == 0)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        seen = { modified, reader != nullptr ? reader->numChannels : 0u };
    }

    if (seen.second != 1)
        return file;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return file;

    const auto skip = juce::jlimit ((juce::int64) 0, reader->lengthInSamples - 1,
                                    (juce::int64) std::llround (mp3EncoderDelaySeconds (file) * reader->sampleRate));
    const auto length = reader->lengthInSamples - skip;
    const auto target = file.getSiblingFile (file.getFileNameWithoutExtension() + "_wnl-estereo.wav");

    if (target.existsAsFile())
    {
        std::unique_ptr<juce::AudioFormatReader> old (formats.createReaderFor (target));
        const bool ours = old != nullptr && old->numChannels == 2 && old->sampleRate == reader->sampleRate
                          && old->lengthInSamples == length && target.hasFileExtension ("wav");

        if (! ours)
            return file;   // não é uma cópia nossa: não apaga

        if (target.getLastModificationTime() >= modified)
            return target;
    }

    const auto temp = target.getSiblingFile (target.getFileNameWithoutExtension() + ".tmp");
    temp.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (temp.createOutputStream().release());

    if (stream == nullptr)
        return file;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                 .withSampleRate (reader->sampleRate)
                                                 .withNumChannels (2)
                                                 .withBitsPerSample (32)
                                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    bool ok = writer != nullptr;
    constexpr int chunk = 65536;
    juce::AudioBuffer<float> both (2, chunk);

    for (juce::int64 at = 0; ok && at < length; at += chunk)
    {
        const int n = (int) juce::jmin ((juce::int64) chunk, length - at);
        ok = reader->read (&both, 0, n, skip + at, true, false);
        both.copyFrom (1, 0, both, 0, 0, n);
        ok = ok && writer->writeFromAudioSampleBuffer (both, 0, n);
    }

    writer.reset();

    if (! ok || ! temp.moveFileTo (target))
    {
        temp.deleteFile();
        return file;
    }

    return target;
}

} // namespace whynot::blockfile
