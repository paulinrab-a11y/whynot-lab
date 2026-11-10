#include "SampleCache.h"

#include <chrono>

namespace whynot
{

SampleCache::SampleCache (juce::AudioFormatManager& formats)
    : formatManager (formats), shared (std::make_shared<Shared>())
{
    shared->owner = this;
}

SampleCache::~SampleCache()
{
    shared->owner = nullptr;       // o aviso que ainda estiver na fila de mensagens não acha mais o cache
    shared->board.cancelAll();     // quem esperava sai aqui, na thread de mensagem, sem ser chamado

    if (pool != nullptr)
        pool->removeAllJobs (true, 20000);   // a decodificação confere o shouldExit a cada bloco de 65536 amostras
}

//==============================================================================
SampleCache::Data SampleCache::decode (juce::AudioFormatManager& formats, const juce::File& file, juce::String& error,
                                       const juce::ThreadPoolJob* job)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

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
        if (job != nullptr && job->shouldExit())
            return {};   // o cache está saindo: ninguém vai ouvir este sample

        const int n = juce::jmin (chunk, length - pos);
        reader->read (&buffer, 0, n, pos, true, numChannels > 1);

        for (int ch = 0; ch < numChannels; ++ch)
            std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, data->channels[(size_t) ch].begin() + pos);
    }

    // MP3: o atraso que o codificador põe no começo atrasaria o one-shot (o ataque sairia da grade).
    trimLeadingFrames (data->channels, leadingFramesToTrim (file, data->channels, reader->sampleRate));

    audio::flushDenormals (*data);
    audio::buildMipmaps (*data);   // os níveis de oitava: tocar mais agudo sem aliasing (até 30 s de sample)
    return data;
}

//==============================================================================
SampleCache::Data SampleCache::get (const juce::File& file, juce::String& error)
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

    auto data = decode (formatManager, file, error, nullptr);

    if (data == nullptr)
        return {};

    entries[key] = { modified, size, data };
    return data;
}

SampleCache::Data SampleCache::getIfReady (const juce::File& file, juce::String& error) const
{
    if (! file.existsAsFile())
    {
        error = "Sample não encontrado: "_t + file.getFullPathName();
        return {};
    }

    if (auto it = entries.find (file.getFullPathName());
        it != entries.end() && it->second.modified == file.getLastModificationTime() && it->second.size == file.getSize())
        return it->second.data;

    return {};
}

//==============================================================================
/** Uma decodificação na thread de trabalho. Não toca no cache: deixa o resultado no quadro e chama a thread de mensagem. */
class SampleCache::DecodeJob : public juce::ThreadPoolJob
{
public:
    DecodeJob (std::shared_ptr<Shared> s, juce::AudioFormatManager& f, const juce::File& fileToRead)
        : juce::ThreadPoolJob ("WHYNOT sample"), state (std::move (s)), formats (f), file (fileToRead) {}

    JobStatus runJob() override
    {
        Loaded loaded;

        if (! file.existsAsFile())
        {
            loaded.error = "Sample não encontrado: "_t + file.getFullPathName();
        }
        else
        {
            loaded.modified = file.getLastModificationTime();
            loaded.size = file.getSize();

            try
            {
                loaded.data = decode (formats, file, loaded.error, this);
            }
            catch (const std::exception&)   // sem memória para um arquivo enorme: o canal avisa, o app segue
            {
                loaded.data = nullptr;
                loaded.error = "Não consegui carregar o sample (memória): "_t + file.getFileName();
            }
        }

        if (shouldExit())
            return jobHasFinished;   // o cache está saindo: não há a quem entregar

        state->board.complete (file.getFullPathName(), std::move (loaded));

        juce::MessageManager::callAsync ([weak = std::weak_ptr<Shared> (state)]
        {
            if (auto s = weak.lock())
                if (s->owner != nullptr)
                    s->owner->deliverFinished();
        });

        return jobHasFinished;
    }

private:
    std::shared_ptr<Shared> state;
    juce::AudioFormatManager& formats;
    const juce::File file;
};

void SampleCache::request (const juce::File& file, Callback callback)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! shared->board.request (file.getFullPathName(), std::move (callback)))
        return;   // este arquivo já está sendo decodificado: o pedido entrou na fila dele

    if (pool == nullptr)
        pool = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions().withThreadName ("WHYNOT samples").withNumberOfThreads (2));

    pool->addJob (new DecodeJob (shared, formatManager, file), true);
}

int SampleCache::pendingLoads() const
{
    return (int) shared->board.size();
}

void SampleCache::flushPendingLoads()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // A decodificação não pede nada à thread de mensagem: esperar por ela aqui não trava.
    if (pool != nullptr)
        while (! shared->board.waitUntilIdle (std::chrono::milliseconds (200)))
            if (pool->getNumJobs() == 0)
                break;   // nenhum trabalho vivo e o quadro não esvaziou: não espera para sempre

    deliverFinished();
}

void SampleCache::deliverFinished()
{
    for (auto& finished : shared->board.drain())
    {
        auto& loaded = finished.result;

        if (loaded.data != nullptr)
        {
            // um get() (export, render) pode ter decodificado o mesmo arquivo enquanto a thread de trabalho
            // lia: fica o que já está no cache, para todos dividirem o mesmo sample
            if (auto it = entries.find (finished.key);
                it != entries.end() && it->second.modified == loaded.modified && it->second.size == loaded.size)
                loaded.data = it->second.data;
            else
                entries[finished.key] = { loaded.modified, loaded.size, loaded.data };
        }

        for (auto& callback : finished.waiters)
            if (callback)
                callback (loaded.data, loaded.error);
    }
}

} // namespace whynot
