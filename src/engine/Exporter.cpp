#include "Exporter.h"
#include "Projector.h"
#include "../common/FolderHints.h"

namespace whynot
{

//==============================================================================
bool isReservedFileName (const juce::String& fileName)
{
    // "CON", "con.wav", "Aux.final.wav" e "NUL .txt": o Windows olha o nome até o primeiro ponto
    const auto stem = fileName.upToFirstOccurrenceOf (".", false, false).trimEnd().toUpperCase();

    static const juce::StringArray reserved { "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$",
                                              "COM0", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                              "LPT0", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    return reserved.contains (stem);
}

juce::String packBaseName (const Project& p, const juce::String& title)
{
    const double bpm = p.bpm;
    const auto bpmText = std::abs (bpm - std::round (bpm)) < 0.005 ? juce::String (juce::roundToInt (bpm))
                                                                  : juce::String (bpm, 1).replaceCharacter ('.', ',');
    static const char* roots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::String key;

    if (p.scale != "chromatic")
        key = juce::String (" ") + roots[((p.keyRoot % 12) + 12) % 12] + (p.scale == "major" ? "" : "m");

    return safeFileName (title.trim() + " " + bpmText + "bpm" + key);
}

juce::String safeFileName (const juce::String& name)
{
    auto s = juce::File::createLegalFileName (name).trim();

    // "Beat." e "Beat " viram "Beat" no Windows: o app procuraria um arquivo que não existe
    while (s.endsWithChar ('.') || s.endsWithChar (' '))
        s = s.dropLastCharacters (1);

    if (s.isEmpty())
        s = "Sem título"_t;

    if (isReservedFileName (s))
    {
        const int stemLength = s.upToFirstOccurrenceOf (".", false, false).trimEnd().length();
        s = s.substring (0, stemLength) + "_" + s.substring (stemLength);
    }

    return s;
}

namespace
{
    constexpr int maxWindowsPath = 259;   // MAX_PATH sem o zero final: o app não pede os caminhos longos do Windows

    // O Tracktion devolve os erros de gravação em inglês e sem o caminho
    juce::Result explainRenderError (const juce::String& taskError, const juce::File& outFile)
    {
        if (taskError.containsIgnoreCase ("write to target") || taskError.containsIgnoreCase ("intermediate file"))
        {
            const auto why = checkWritableTarget (outFile);
            return juce::Result::fail (why.failed() ? why.getErrorMessage()
                                                    : "Não consegui gravar o WAV em "_t + outFile.getFullPathName()
                                                          + ". O disco pode estar cheio ou ter saído (pendrive)."_t);
        }

        if (taskError.containsIgnoreCase ("cancel"))
            return juce::Result::fail ("Exportação cancelada."_t);

        if (taskError.containsIgnoreCase ("timeout") || taskError.containsIgnoreCase ("Quit message"))
            return juce::Result::fail ("O render foi interrompido (o app estava fechando)."_t);

        return juce::Result::fail (taskError);
    }
}

juce::Result checkWritableTarget (const juce::File& file, bool keepExisting)
{
    if (file == juce::File())
        return juce::Result::fail ("Escolha onde salvar o arquivo.");

    const auto path = file.getFullPathName();
    const auto units = (int) (path.toUTF16().sizeInBytes() / sizeof (juce::CharPointer_UTF16::CharType)) - 1;   // o Windows conta em UTF-16

    if (units > maxWindowsPath)
        return juce::Result::fail ("O caminho do arquivo tem "_t + juce::String (units)
                                   + " caracteres, e o Windows aceita até 259. Escolha uma pasta mais curta"_t
                                   + " (a Área de Trabalho, por exemplo) ou um nome menor: "_t + path);

    if (isReservedFileName (file.getFileName()))
        return juce::Result::fail ("\""_t + file.getFileName() + "\" é um nome reservado do Windows (como CON, AUX e NUL)."_t
                                   + " Escolha outro nome."_t);

    const auto folder = file.getParentDirectory();

    if (! folder.createDirectory())
        return juce::Result::fail ("Não consegui criar a pasta "_t + folder.getFullPathName() + "."
                                   + noPermissionHint (folder.getParentDirectory()));

    if (file.isDirectory())
        return juce::Result::fail ("Já existe uma pasta com esse nome: "_t + path);

    if (file.existsAsFile())
    {
        if (keepExisting)
        {
            juce::FileOutputStream probe (file);   // abre para escrever sem mudar nada (o JUCE vai para o fim)

            if (probe.openedOk())
                return juce::Result::ok();
        }
        else if (file.deleteFile())
        {
            return juce::Result::ok();
        }

        // Aberto num programa que deixa escrever mas não apagar: esvazia e o render grava do começo.
        if (! keepExisting)
        {
            juce::FileOutputStream out (file);

            if (out.openedOk() && out.setPosition (0) && out.truncate().wasOk())
                return juce::Result::ok();
        }

        if (! file.hasWriteAccess())
            return juce::Result::fail ("O arquivo "_t + path + " está marcado como só leitura (ou sem permissão)."_t
                                       + noPermissionHint (folder));

        return juce::Result::fail ("O arquivo "_t + file.getFileName() + " está aberto em outro programa (um player, por exemplo)."_t
                                   + " Feche o arquivo lá ou escolha outro nome. Pasta: "_t + folder.getFullPathName());
    }

    // Prova: cria e apaga. Falha aqui é pasta sem permissão, disco cheio ou que saiu.
    juce::String reason;

    {
        juce::FileOutputStream probe (file);

        if (! probe.openedOk())
            reason = probe.getStatus().getErrorMessage().trim();
    }

    if (reason.isNotEmpty() || ! file.existsAsFile())
    {
        file.deleteFile();
        return juce::Result::fail ("Não consegui gravar em "_t + folder.getFullPathName()
                                   + (reason.isNotEmpty() ? " (" + reason + ")." : juce::String (".")) + noPermissionHint (folder));
    }

    file.deleteFile();
    return juce::Result::ok();
}

/** Depois do render: corta o silêncio da cauda (depois de keepSeconds) e conta as amostras em 0 dB. */
juce::Result finishWav (const juce::File& wav, double keepSeconds, bool trim, bool countClips, juce::String& warning);

//==============================================================================
std::unique_ptr<RenderJob> RenderJob::prepare (te::Engine& engine, SampleCache& cache, const Project& project,
                                               const juce::File& projectFolder, bool song, int loops, const juce::File& outFile,
                                               const PluginCatalog* catalog, const Settings& settings, juce::Result& error)
{
    error = juce::Result::ok();

    if (outFile == juce::File())
    {
        error = juce::Result::fail ("Escolha onde salvar o WAV.");
        return {};
    }

    std::unique_ptr<RenderJob> job (new RenderJob());
    job->outFile = outFile;

    job->edit = te::Edit::createSingleTrackEdit (engine, te::Edit::forRendering);

    if (job->edit == nullptr)
    {
        error = juce::Result::fail ("Não consegui preparar o render."_t);
        return {};
    }

    job->projector = std::make_unique<Projector> (*job->edit, cache, song ? Projector::Mode::song : Projector::Mode::pattern, loops, catalog);
    job->projector->setAudition (settings.audition.bypassInserts, settings.audition.trimDb);
    job->projector->sync (project, projectFolder);
    job->length = job->projector->lengthSeconds();

    // Sem proxy para MP3, FLAC e OGG (ver Exporter.h): o clipe é lido direto do arquivo, e o render não
    // espera por um WAV que só um Timer da thread de mensagem começaria.
    Projector::readCompressedClipsDirectly (*job->edit);

    if (job->length <= 0.0)
    {
        error = juce::Result::fail ("Não há nada para exportar."_t);
        return {};
    }

    // só o trecho selecionado na régua (pedido do usuário, 21/10: "salvar só o que eu quero")
    double from = 0.0, to = job->length;

    if (settings.toSeconds > settings.fromSeconds)
    {
        from = juce::jlimit (0.0, job->length, settings.fromSeconds);
        to = juce::jlimit (from, job->length, settings.toSeconds);

        if (to - from < 0.001)
        {
            error = juce::Result::fail ("O trecho selecionado está fora da música."_t);
            return {};
        }
    }

    // A música inteira ganha a cauda: o reverb e o 808 do último compasso tocam até acabar
    const bool wholeSong = song && ! (settings.toSeconds > settings.fromSeconds);
    job->keepSeconds = to - from;

    if (wholeSong && settings.tailSeconds > 0.0)
    {
        to += juce::jlimit (0.0, 30.0, settings.tailSeconds);
        job->trimAfter = settings.trimTail;
    }

    // Por último, porque apaga o arquivo antigo: caminho, nome, permissão e arquivo aberto em outro programa.
    if (auto writable = checkWritableTarget (outFile); writable.failed())
    {
        error = writable;
        return {};
    }

    te::Renderer::Parameters params (*job->edit);
    params.destFile = outFile;
    params.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
    params.bitDepth = settings.bitDepth == 16 || settings.bitDepth == 32 ? settings.bitDepth : 24;
    params.sampleRateForAudio = settings.sampleRate >= 22050.0 && settings.sampleRate <= 192000.0 ? settings.sampleRate : 48000.0;
    params.blockSizeForAudio = 512;
    params.time = { te::TimePosition::fromSeconds (from), te::TimePosition::fromSeconds (to) };
    params.usePlugins = true;
    params.useMasterPlugins = true;
    params.canRenderInMono = false;
    params.checkNodesForAudio = false;   // padrão vazio exporta silêncio, em vez de falhar
    params.ditheringEnabled = params.bitDepth == 16;   // 16 bits: ruído de dither em vez de distorção de quantização
    job->checkClip = params.bitDepth != 32;   // em float o arquivo guarda acima de 0 dB; em 16 e 24 bits, corta

    job->renderStatus = std::make_unique<te::Edit::ScopedRenderStatus> (*job->edit, true);
    job->task = te::render_utils::createRenderTask (params, "Exportar WAV", &job->progress, nullptr);

    if (job->task == nullptr)
    {
        error = juce::Result::fail ("Não consegui preparar o render."_t);
        return {};
    }

    auto* raw = job.get();
    job->task->setCancellationCheck ([raw] { return raw->cancelled.load(); });
    return job;
}

RenderJob::~RenderJob()
{
    task = nullptr;   // antes do estado de render e do Edit; num render cancelado, o grafo sai aqui
    renderStatus = nullptr;
    projector = nullptr;
    edit = nullptr;

    if (removeOutput)
        outFile.deleteFile();   // já fechado: a tarefa saiu acima
}

juce::Result RenderJob::run()
{
    if (task == nullptr)
        return juce::Result::fail ("Não consegui preparar o render."_t);

    while (! cancelled && task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
    {}

    // Cancelado (Parar ou o app fechando): a tarefa ainda segura o grafo, e desmontá-lo pede a thread
    // de mensagem, que pode estar esperando esta aqui. Ela sai no destrutor, junto com o arquivo.
    if (cancelled)
    {
        removeOutput = true;
        return juce::Result::fail ("Exportação cancelada."_t);
    }

    const auto taskError = task->errorMessage;

    if (taskError.isNotEmpty())
    {
        removeOutput = true;
        return explainRenderError (taskError, outFile);
    }

    task = nullptr;   // terminou: o grafo já saiu dentro do runJob; aqui o arquivo fecha
    progress = 1.0f;

    if (! outFile.existsAsFile())
        return juce::Result::fail ("O render terminou sem gerar "_t + outFile.getFullPathName());

    if (trimAfter || checkClip)
        return finishWav (outFile, keepSeconds, trimAfter, checkClip, warning);

    return juce::Result::ok();
}

juce::Result finishWav (const juce::File& wav, double keepSeconds, bool trim, bool countClips, juce::String& warning)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (wav.createInputStream().release(), true));

    if (reader == nullptr)
        return juce::Result::fail ("Não consegui reler o WAV exportado: "_t + wav.getFullPathName());

    const auto total = reader->lengthInSamples;
    const int channels = (int) juce::jlimit (1u, 8u, reader->numChannels);
    const double rate = reader->sampleRate;
    const auto keepFrames = juce::jlimit ((juce::int64) 0, total, (juce::int64) std::llround (keepSeconds * rate));
    constexpr float silence = 3.1622776e-5f;   // -90 dBFS
    constexpr float fullScale = 0.9999f;       // o maior valor de 16 e 24 bits fica em -0,0009 dB

    juce::AudioBuffer<float> block (channels, 32768);
    juce::int64 lastLoud = -1, clipped = 0;

    // uma passada só, em blocos: o arquivo inteiro nunca vai para a memória
    for (juce::int64 pos = 0; pos < total; pos += block.getNumSamples())
    {
        const int n = (int) juce::jmin ((juce::int64) block.getNumSamples(), total - pos);
        reader->read (&block, 0, n, pos, true, true);

        for (int c = 0; c < channels; ++c)
        {
            const float* d = block.getReadPointer (c);

            for (int i = 0; i < n; ++i)
            {
                const float a = std::abs (d[i]);

                if (a > silence)
                    lastLoud = juce::jmax (lastLoud, pos + i);

                if (countClips && a >= fullScale)
                    ++clipped;
            }
        }
    }

    if (clipped > 0)
        warning = "Atenção: o WAV passou de 0 dB em "_t + juce::String (clipped)
                  + " pontos e vai soar estourado. Abaixe o volume do master ou ponha um limitador no fim do master."_t;

    // A cauda acaba onde o som some, com 20 ms de folga, e a música em si fica sempre inteira. Se nada toca
    // depois do último bloco, o arquivo sai no tamanho exato da música, igual ao de antes.
    const auto cut = lastLoud >= keepFrames ? juce::jmin (total, lastLoud + 1 + (juce::int64) std::llround (0.02 * rate))
                                            : keepFrames;

    if (! trim || cut >= total)
        return juce::Result::ok();

    const auto temp = wav.getSiblingFile (wav.getFileNameWithoutExtension() + ".cauda.tmp");
    temp.deleteFile();

    {
        std::unique_ptr<juce::OutputStream> stream (temp.createOutputStream().release());
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                          .withSampleRate (rate)
                                                          .withNumChannels (channels)
                                                          .withBitsPerSample ((int) reader->bitsPerSample)
                                                          .withSampleFormat (reader->usesFloatingPointData
                                                                                 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                                 : juce::AudioFormatWriterOptions::SampleFormat::integral));

        if (writer == nullptr)
            return juce::Result::ok();   // não conseguiu cortar: fica com a cauda inteira, que não faz mal

        for (juce::int64 pos = 0; pos < cut; pos += block.getNumSamples())
        {
            const int n = (int) juce::jmin ((juce::int64) block.getNumSamples(), cut - pos);
            reader->read (&block, 0, n, pos, true, true);

            if (! writer->writeFromAudioSampleBuffer (block, 0, n))
            {
                writer = nullptr;
                temp.deleteFile();
                return juce::Result::ok();
            }
        }
    }

    reader = nullptr;   // fecha o original antes de trocar

    if (! temp.moveFileTo (wav))
    {
        temp.deleteFile();
        return juce::Result::ok();
    }

    return juce::Result::ok();
}

//==============================================================================
void RenderThread::start (std::function<void()> body)
{
    join (true);
    auto registered = std::make_shared<juce::WaitableEvent>();

    thread = std::thread ([registered, body = std::move (body)]
    {
        registered->wait();   // só começa registrada: toda espera dela pela thread de mensagem pode ser interrompida
        body();
    });

    exitStatus.emplace (thread.get_id());
    registered->signal();
}

void RenderThread::join (bool interrupt)
{
    if (thread.joinable())
    {
        if (interrupt)
            te::signalThreadShouldExit (thread.get_id());

        thread.join();
    }

    exitStatus.reset();
}

juce::Result renderToWav (te::Engine& engine, SampleCache& cache, const Project& project,
                          const juce::File& projectFolder, bool song, int loops, const juce::File& outFile,
                          const PluginCatalog* catalog, double sampleRate, int bitDepth, const RenderAudition& audition)
{
    juce::Result error = juce::Result::ok();
    auto job = RenderJob::prepare (engine, cache, project, projectFolder, song, loops, outFile, catalog,
                                   { sampleRate, bitDepth, audition }, error);

    if (job == nullptr)
        return error;

    return job->run();
}

} // namespace whynot
