#include "InputRecorder.h"

namespace whynot::record
{

/** A thread de fundo que esvazia a fila no disco. Tudo dela é lido pela thread de mensagem só
    depois do stopThread() (o join garante a ordem). */
class InputRecorder::Writer : public juce::Thread
{
public:
    Writer (Queue& q, std::unique_ptr<juce::AudioFormatWriter> w, juce::OutputStream* stream, int channels, double sampleRate,
            const std::atomic<int64_t>& lostCounter)
        : juce::Thread ("WHYNOT gravação"_t), queue (q), writer (std::move (w)), fileStream (stream), numChannels (channels),
          maxSilenceToFill ((int64_t) (sampleRate * 2.0)), flushEvery (juce::jmax<int64_t> (1, (int64_t) sampleRate)), lost (lostCounter) {}

    void run() override
    {
        for (;;)
        {
            bool any = false;

            while (queue.pop (chunk))
            {
                any = true;
                write (chunk);
                flushIfDue();
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
        sinceFlush += juce::jmax (0, n);
    }

    /** A cada segundo de áudio escrito, o cabeçalho do WAV ganha o tamanho atual: se o app cair, o arquivo
        abre com tudo até o último flush (e o TakeRecovery acha o resto pelo tamanho do arquivo).
        Conferido no JUCE (juce_WavAudioFormat.cpp, WavAudioFormatWriter::flush): guarda a posição, chama
        writeHeader() (volta ao começo e reescreve RIFF ou RF64, fmt e data com o lengthInSamples atual) e
        retorna à posição de escrita. Os setPosition do FileOutputStream esvaziam o buffer dele no sistema
        (WriteFile), o que basta para sobreviver à queda do app. Para a queda de energia, a cada 5 flushes
        o FileOutputStream::flush() chama o FlushFileBuffers. Tudo aqui, na thread escritora. */
    void flushIfDue()
    {
        if (sinceFlush < flushEvery)
            return;

        sinceFlush = 0;

        if (writer->flush() && ++flushes % 5 == 0 && fileStream != nullptr)
            fileStream->flush();
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
            sinceFlush += n;
            samples -= n;
        }
    }

    Queue& queue;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::OutputStream* const fileStream;   // o arquivo, que é do writer (vive enquanto ele viver)
    const int numChannels;
    const int64_t maxSilenceToFill;      // 2 s: até isso é falha de buffer; mais, só se a fila perdeu
    const int64_t flushEvery;            // 1 s de áudio entre um flush do cabeçalho e o próximo
    int64_t sinceFlush = 0;
    int flushes = 0;
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

    auto* rawStream = stream.get();   // o writer fica dono do stream; o escritor usa para o FlushFileBuffers
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

    writer = std::make_unique<Writer> (*queue, std::move (fileWriter), rawStream, numChannels, sampleRate, lost);
    writerFile = file;
    writerRate = sampleRate;

    // O marcador de gravação em andamento, ao lado do take: fica até o stop(). Escrita atômica (arquivo
    // temporário e troca de nome). Se não der para escrever, a gravação segue: só não haverá oferta de
    // recuperação se o app cair.
    markerFile = markerFileFor (file);
    {
        auto* o = new juce::DynamicObject();
        const juce::var json (o);   // o var fica dono do objeto
        o->setProperty ("version", 1);
        o->setProperty ("file", file.getFileName());
        o->setProperty ("path", file.getFullPathName());
        o->setProperty ("sampleRate", sampleRate);
        o->setProperty ("channels", numChannels);
        o->setProperty ("bitsPerSample", 32);
        o->setProperty ("format", "float");
        o->setProperty ("playFromSeconds", takeInfo.playFromSeconds);
        o->setProperty ("recordFromSeconds", takeInfo.recordFromSeconds);
        o->setProperty ("takeName", takeInfo.takeName);
        o->setProperty ("track", takeInfo.trackName);
        o->setProperty ("startedAt", juce::Time::getCurrentTime().toISO8601 (true));

        juce::TemporaryFile temp (markerFile);

        if (temp.getFile().replaceWithText (juce::JSON::toString (json), false, false, "\n"))
            temp.overwriteTargetFileWithTemporary();
    }
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
    markerFile.deleteFile();      // fechou limpo: não há o que recuperar
    markerFile = juce::File();
    takeInfo = {};

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
// Recuperação de takes que ficaram sem fechar

static const char* const markerSuffix = ".gravando.json";

juce::File InputRecorder::markerFileFor (const juce::File& takeFile)
{
    return takeFile.getSiblingFile (takeFile.getFileNameWithoutExtension() + markerSuffix);
}

std::vector<InputRecorder::RecoveredTake> InputRecorder::recoverOrphanTakes (const juce::File& folder, juce::StringArray* problems)
{
    std::vector<RecoveredTake> found;

    const auto problem = [problems] (const juce::String& text)
    {
        if (problems != nullptr)
            problems->add (text);
    };

    if (! folder.isDirectory())
        return found;

    for (const auto& marker : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + markerSuffix))
    {
        // O WAV sai do nome do marcador (nunca de um caminho escrito dentro dele): fica sempre nesta pasta.
        const auto name = marker.getFileName();
        const auto wav = folder.getChildFile (name.dropLastCharacters (juce::String (markerSuffix).length()) + ".wav");

        if (! wav.existsAsFile())
        {
            marker.deleteFile();   // o take foi apagado ou movido: o marcador não serve mais
            continue;
        }

        const auto size = wav.getSize();
        std::vector<uint8_t> head ((size_t) juce::jmin<juce::int64> (size, (juce::int64) recovery::headBytesWanted));

        {
            juce::FileInputStream in (wav);

            if (! in.openedOk() || (! head.empty() && in.read (head.data(), (int) head.size()) != (int) head.size()))
            {
                problem ("Não consegui ler "_t + wav.getFullPathName());
                continue;
            }
        }

        const auto plan = recovery::plan (head.data(), head.size(), (uint64_t) size);

        {
            // Abrir para escrita também é a prova de que ninguém está gravando neste arquivo agora
            // (no Windows o FileOutputStream não divide a escrita): se não abrir, fica tudo como está.
            juce::FileOutputStream out (wav);

            if (out.failedToOpen())
            {
                problem ("Em uso, ou sem permissão de escrita: "_t + wav.getFullPathName());
                continue;
            }

            if (plan.status == recovery::Status::repaired)
            {
                // primeiro o tamanho (corta o frame pela metade, ou põe o zero de enchimento), depois os remendos
                const auto finalSize = (juce::int64) plan.finalSize;
                bool ok = out.setPosition (juce::jmin (size, finalSize));

                if (ok && finalSize < size)
                    ok = out.truncate().wasOk();
                else if (ok && finalSize > size)
                    ok = out.writeRepeatedByte (0, (size_t) (finalSize - size));

                for (const auto& patch : plan.patches)
                    ok = ok && out.setPosition ((juce::int64) patch.offset) && out.write (patch.bytes.data(), patch.bytes.size());

                out.flush();

                if (! ok)
                {
                    problem ("Não consegui consertar o cabeçalho de "_t + wav.getFullPathName());
                    continue;
                }
            }
        }

        if (plan.status == recovery::Status::empty)
        {
            wav.deleteFile();   // nem um frame chegou ao disco: como um take cancelado
            marker.deleteFile();
            continue;
        }

        if (plan.status != recovery::Status::intact && plan.status != recovery::Status::repaired)
        {
            problem ((plan.status == recovery::Status::needsRf64 ? "Passa de 4 GB e não deu para consertar no lugar: "_t
                                                                 : "Não é um WAV que dê para recuperar: "_t)
                     + wav.getFullPathName());
            continue;
        }

        const auto json = juce::JSON::parse (marker.loadFileAsString());

        RecoveredTake take;
        take.file = wav;
        take.sampleRate = (double) plan.sampleRate;
        take.numChannels = (int) plan.channels;
        take.numSamples = (int64_t) plan.frames;
        take.repaired = plan.status == recovery::Status::repaired;
        take.info.playFromSeconds = (double) json.getProperty ("playFromSeconds", 0.0);
        take.info.recordFromSeconds = (double) json.getProperty ("recordFromSeconds", 0.0);
        take.info.takeName = json.getProperty ("takeName", juce::String()).toString();
        take.info.trackName = json.getProperty ("track", juce::String()).toString();
        take.startedAt = json.getProperty ("startedAt", juce::String()).toString();
        found.push_back (std::move (take));

        marker.deleteFile();   // resolvido: o take agora é um WAV comum na pasta de gravações
    }

    return found;
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
