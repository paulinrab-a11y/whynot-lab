#include "InputRecorder.h"

namespace whynot::record
{

/** A thread de fundo que esvazia a fila no disco. Tudo dela é lido pela thread de mensagem só
    depois do stopThread() (o join garante a ordem). */
class InputRecorder::Writer : public juce::Thread
{
public:
    Writer (Queue& q, std::unique_ptr<juce::AudioFormatWriter> w, int channels, double sampleRate, const std::atomic<int64_t>& lostCounter)
        : juce::Thread ("WHYNOT gravação"_t), queue (q), writer (std::move (w)), numChannels (channels),
          maxSilenceToFill ((int64_t) (sampleRate * 2.0)), lost (lostCounter) {}

    void run() override
    {
        for (;;)
        {
            bool any = false;

            while (queue.pop (chunk))
            {
                any = true;
                write (chunk);
            }

            // Só sai com a fila vazia: o stop() já garantiu que nada mais entra.
            if (! any)
            {
                if (threadShouldExit())
                    break;

                wait (2);
            }
        }

        writer = nullptr;   // fecha o arquivo (o cabeçalho do WAV é escrito aqui)
    }

    std::atomic<int64_t> written { 0 };
    int64_t firstStream = -1, expected = 0;
    int gaps = 0;
    bool writeFailed = false, stopped = false;

private:
    void write (const Chunk& c)
    {
        if (stopped)
            return;   // a placa reiniciou no meio: o take terminou ali (ver stitch)

        if (firstStream < 0)
            firstStream = expected = c.streamSample;

        // Buraco de falha de buffer, ou de fila cheia (o disco atrasou), vira silêncio de qualquer
        // tamanho, para o resto seguir no tempo. Pulo sem explicação: o take para aqui.
        const int64_t lostNow = lost.load (std::memory_order_relaxed);
        const auto s = record::stitch (expected, c.streamSample, c.numFrames, lostNow - lostFilled, maxSilenceToFill);
        int skip = 0;

        switch (s.action)
        {
            case record::Stitch::Action::fillThenWrite:
                ++gaps;
                writeSilence (s.silence);
                lostFilled += juce::jmin (s.silence, juce::jmax<int64_t> (0, lostNow - lostFilled));
                break;

            case record::Stitch::Action::skipThenWrite:
                ++gaps;
                skip = s.skip;
                break;

            case record::Stitch::Action::stop:
                ++gaps;
                stopped = true;
                return;

            case record::Stitch::Action::write:
            default:
                break;
        }

        const float* ptrs[maxChannels] {};

        for (int ch = 0; ch < numChannels; ++ch)
            ptrs[ch] = c.data[ch] + skip;

        const int n = c.numFrames - skip;

        if (n > 0 && ! writer->writeFromFloatArrays (ptrs, numChannels, n))
            writeFailed = true;

        expected = c.streamSample + c.numFrames;
        written.fetch_add (juce::jmax (0, n), std::memory_order_relaxed);
    }

    void writeSilence (int64_t samples)
    {
        static const float zeros[chunkFrames] {};
        const float* ptrs[maxChannels] { zeros, zeros };

        while (samples > 0)
        {
            const int n = (int) juce::jmin<int64_t> (samples, chunkFrames);

            if (! writer->writeFromFloatArrays (ptrs, numChannels, n))
                writeFailed = true;

            written.fetch_add (n, std::memory_order_relaxed);
            samples -= n;
        }
    }

    Queue& queue;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    const int numChannels;
    const int64_t maxSilenceToFill;      // 2 s: até isso é falha de buffer; mais, só se a fila perdeu
    const std::atomic<int64_t>& lost;    // o que a thread de áudio não conseguiu pôr na fila
    int64_t lostFilled = 0;              // quanto disso já virou silêncio
    Chunk chunk {};
};

//==============================================================================
InputRecorder::InputRecorder() : queue (std::make_unique<Queue>()) {}

InputRecorder::~InputRecorder()
{
    if (writer != nullptr)
        stop (false);
}

juce::Result InputRecorder::start (const juce::File& file, double sampleRate, int firstInput, int numChannels)
{
    if (writer != nullptr)
        return juce::Result::fail ("Já está gravando."_t);

    if (sampleRate <= 0.0)
        return juce::Result::fail ("Taxa de amostragem inválida."_t);

    numChannels = juce::jlimit (1, maxChannels, numChannels);

    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("Não consegui criar a pasta "_t + file.getParentDirectory().getFullPathName());

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());

    if (stream == nullptr)
        return juce::Result::fail ("Não consegui criar o arquivo "_t + file.getFullPathName());

    juce::WavAudioFormat wav;
    auto fileWriter = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (sampleRate)
                                                     .withNumChannels (numChannels)
                                                     .withBitsPerSample (32)
                                                     .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));

    if (fileWriter == nullptr)
        return juce::Result::fail ("Não consegui abrir o WAV para gravar: "_t + file.getFullPathName());

    // A fila já deve estar vazia (o stop() espera a thread de áudio); um pedaço atrasado de um take
    // anterior não pode entrar no começo deste. Aqui ninguém mais consome: o escritor antigo terminou.
    for (Chunk stale; queue->pop (stale);)
    {}

    lost.store (0, std::memory_order_relaxed);
    peaks[0].store (0.0f, std::memory_order_relaxed);
    firstChannel.store (juce::jmax (0, firstInput), std::memory_order_relaxed);
    numChannelsToRecord.store (numChannels, std::memory_order_relaxed);

    writer = std::make_unique<Writer> (*queue, std::move (fileWriter), numChannels, sampleRate, lost);
    writerFile = file;
    writerRate = sampleRate;
    writer->startThread (juce::Thread::Priority::high);

    capturing.store (true, std::memory_order_seq_cst);
    return juce::Result::ok();
}

InputRecorder::Take InputRecorder::stop (bool waitForAudioThread)
{
    Take take;

    if (writer == nullptr)
        return take;

    // Primeiro desliga, depois conta os blocos (seq_cst: a leitura não passa na frente da escrita).
    // O bloco que estava no meio, se viu capturing = true, só soma em `blocks` depois de pôr o último
    // pedaço na fila; esperar dois blocos cobre esse e garante que o seguinte já viu o false.
    // Até 500 ms: um buffer de 4096 a 44,1 kHz leva 93 ms. Sem placa rodando, não há o que esperar.
    capturing.store (false, std::memory_order_seq_cst);
    const auto seen = blocks.load (std::memory_order_seq_cst);

    for (int i = 0; waitForAudioThread && i < 250 && blocks.load (std::memory_order_seq_cst) - seen < 2; ++i)
        juce::Thread::sleep (2);

    writer->stopThread (30000);   // esvazia a fila e fecha o arquivo

    take.file = writerFile;
    take.sampleRate = writerRate;
    take.numChannels = numChannelsToRecord.load (std::memory_order_relaxed);
    take.firstStreamSample = writer->firstStream;
    take.numSamples = writer->written.load();
    take.gaps = writer->gaps;
    take.lostSamples = lost.load (std::memory_order_relaxed);
    take.truncated = writer->stopped;

    if (writer->writeFailed)
        take.error = "O disco recusou parte da gravação (cheio?)."_t;

    writer = nullptr;
    return take;
}

double InputRecorder::writtenSeconds() const noexcept
{
    return writer != nullptr && writerRate > 0.0 ? (double) writer->written.load (std::memory_order_relaxed) / writerRate : 0.0;
}

void InputRecorder::setMeteredInput (int firstInput, int numChannels) noexcept
{
    if (! capturing.load (std::memory_order_acquire))   // gravando, a entrada é a do take
    {
        firstChannel.store (juce::jmax (0, firstInput), std::memory_order_relaxed);
        numChannelsToRecord.store (juce::jlimit (1, maxChannels, numChannels), std::memory_order_relaxed);
    }
}

//==============================================================================
void InputRecorder::pushInput (const float* const* channels, int numChannels, int numSamples, int64_t streamSample) noexcept
{
    // fora da gravação, só o pico da entrada escolhida: o medidor da trilha armada e o do assistente mostram o
    // nível antes do take (para acertar o ganho na placa). Sem cópia nem fila: um mínimo e um máximo por canal.
    const bool capture = capturing.load (std::memory_order_seq_cst);

    if (! capture && numSamples > 0 && channels != nullptr)
    {
        const int first = firstChannel.load (std::memory_order_relaxed);
        const int count = numChannelsToRecord.load (std::memory_order_relaxed);
        float blockPeak = 0.0f;

        for (int ch = 0; ch < count; ++ch)
        {
            if (first + ch < numChannels && channels[first + ch] != nullptr)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax (channels[first + ch], numSamples);
                blockPeak = juce::jmax (blockPeak, -range.getStart(), range.getEnd());   // NaN não passa no jmax
            }
        }

        for (auto& p : peaks)
            for (auto current = p.load (std::memory_order_relaxed);
                 blockPeak > current && ! p.compare_exchange_weak (current, blockPeak, std::memory_order_relaxed);)
            {}
    }

    // seq_cst: no x64 a leitura custa o mesmo que acquire, e casa com o handshake do stop()
    if (capture && numSamples > 0)
    {
        // lidos uma vez por bloco: o bloco inteiro usa a mesma entrada
        const int first = firstChannel.load (std::memory_order_relaxed);
        const int count = numChannelsToRecord.load (std::memory_order_relaxed);
        float blockPeak = 0.0f;

        for (int done = 0; done < numSamples;)
        {
            const int n = juce::jmin (chunkFrames, numSamples - done);
            staging.streamSample = streamSample + done;
            staging.numFrames = n;

            for (int ch = 0; ch < count; ++ch)
            {
                const int source = first + ch;
                float* dest = staging.data[ch];

                if (channels != nullptr && source < numChannels && channels[source] != nullptr)
                {
                    const float* src = channels[source] + done;

                    for (int i = 0; i < n; ++i)
                    {
                        dest[i] = src[i];
                        blockPeak = juce::jmax (blockPeak, std::abs (src[i]));
                    }
                }
                else
                {
                    std::fill (dest, dest + n, 0.0f);   // canal que a placa não tem: silêncio
                }
            }

            if (! queue->push (staging))
                lost.fetch_add (n, std::memory_order_relaxed);   // o escritor vê o buraco e põe silêncio

            done += n;
        }

        // máximo atômico sem trava: os medidores da tela leem e zeram
        for (auto& p : peaks)
            for (auto current = p.load (std::memory_order_relaxed);
                 blockPeak > current && ! p.compare_exchange_weak (current, blockPeak, std::memory_order_relaxed);)
            {}
    }

    blocks.fetch_add (1, std::memory_order_seq_cst);
}

} // namespace whynot::record
