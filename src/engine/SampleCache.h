#pragma once

#include <JuceHeader.h>

#include "SampleAnalysis.h"
#include "../audio/SampleVoice.h"
#include "../common/Text.h"

#include <map>
#include <memory>

namespace whynot
{

/** Decodifica arquivos de áudio uma vez e compartilha o resultado entre canais e Edits.
    Thread de mensagem (ou de trabalho), nunca a de áudio.
*/
class SampleCache
{
public:
    explicit SampleCache (juce::AudioFormatManager& formats) : formatManager (formats) {}

    /** Devolve o sample decodificado; recarrega se o arquivo mudou no disco. */
    std::shared_ptr<const audio::SampleData> get (const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "Sample não encontrado: "_t + file.getFullPathName();
            return {};
        }

        const auto key = file.getFullPathName();
        const auto modified = file.getLastModificationTime();
        const auto size = file.getSize();

        if (auto it = entries.find (key); it != entries.end() && it->second.modified == modified && it->second.size == size)
            return it->second.data;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

        if (reader == nullptr)
        {
            error = "Formato de áudio não suportado: "_t + file.getFileName();
            return {};
        }

        constexpr double maxSeconds = 600.0;

        if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0
            || (double) reader->lengthInSamples / reader->sampleRate > maxSeconds)
        {
            error = "Sample vazio, inválido ou maior que 10 minutos: "_t + file.getFileName();
            return {};
        }

        const int numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        const int length = (int) reader->lengthInSamples;

        auto data = std::make_shared<audio::SampleData>();
        data->sampleRate = reader->sampleRate;
        data->rootKey = 60;   // a nota de raiz de cada canal é aplicada pelo sampler
        data->channels.resize ((size_t) numChannels);

        for (auto& channel : data->channels)
            channel.resize ((size_t) length);

        // Em blocos, direto no sample (caça de bugs do produtor, 21/10): ler tudo num buffer e depois
        // copiar pedia o dobro da memória. Uma música de 10 min em estéreo são 212 MB, e o pico ia a 424 MB.
        constexpr int chunk = 1 << 16;
        juce::AudioBuffer<float> buffer (numChannels, juce::jmin (chunk, length));

        for (int pos = 0; pos < length; pos += chunk)
        {
            const int n = juce::jmin (chunk, length - pos);
            reader->read (&buffer, 0, n, pos, true, numChannels > 1);

            for (int ch = 0; ch < numChannels; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, data->channels[(size_t) ch].begin() + pos);
        }

        // MP3: o atraso que o codificador põe no começo atrasaria o one-shot (o ataque sairia da grade).
        trimLeadingFrames (data->channels, leadingFramesToTrim (file, data->channels, reader->sampleRate));

        audio::flushDenormals (*data);
        audio::buildMipmaps (*data);   // os níveis de oitava: tocar mais agudo sem aliasing (até 30 s de sample)

        entries[key] = { modified, size, data };
        return data;
    }

    void clear() { entries.clear(); }

    /** Esquece os samples que só o cache ainda segura (caça de bugs do produtor, 21/10): sem isto, cada
        projeto aberto deixava os samples dele na memória até o app fechar. */
    void prune()
    {
        for (auto it = entries.begin(); it != entries.end();)
            it = it->second.data.use_count() <= 1 ? entries.erase (it) : std::next (it);
    }

    size_t size() const noexcept   { return entries.size(); }

private:
    struct Entry
    {
        juce::Time modified;
        juce::int64 size = 0;
        std::shared_ptr<const audio::SampleData> data;
    };

    juce::AudioFormatManager& formatManager;
    std::map<juce::String, Entry> entries;
};

} // namespace whynot
