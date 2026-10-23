#include "PreviewRenderer.h"
#include "SystemMemory.h"

#include "../dsp/LoudnessMeter.h"
#include "../engine/Exporter.h"
#include "../model/WnlFormat.h"

#include <thread>

namespace whynot::library
{

struct PreviewRenderer::Running
{
    juce::File wnl, wav, part;
    std::unique_ptr<RenderJob> job;
    RenderThread worker;                       // sai antes do job: o join (true) interrompe as esperas do Tracktion
    std::atomic<bool> done { false };
    bool paused = false;                       // cancelado porque a música começou: volta para a fila
    juce::Result result = juce::Result::ok();  // escrito pela thread de trabalho antes do `done`
    PreviewInfo info;
};

PreviewRenderer::PreviewRenderer (Host h) : host (std::move (h)) {}

PreviewRenderer::~PreviewRenderer()
{
    stopTimer();

    if (running != nullptr)
    {
        // O app fechando no meio de uma prévia (o Ctrl+S e logo o X): o render para no próximo bloco, e o
        // join (true) interrompe as esperas dele pela thread de mensagem, que está aqui parada no join.
        // Sem isso as duas esperavam uma pela outra e o app ficava preso (o conserto do abde694).
        running->job->cancel();
        running->worker.join (true);
        running->job = nullptr;   // o grafo e o Edit do render saem aqui, na thread de mensagem (e o WAV pela metade)
        running->wav.deleteFile();
        running->part.deleteFile();
        running = nullptr;
    }
}

void PreviewRenderer::enqueue (const juce::File& wnl, bool urgent)
{
    if (running != nullptr && running->wnl == wnl)
        return;

    failures.erase (wnl.getFullPathName());

    if (std::find (queue.begin(), queue.end(), wnl) == queue.end())
    {
        if (urgent)
            queue.push_front (wnl);
        else
            queue.push_back (wnl);
    }

    startTimerHz (10);
    timerCallback();   // começa já, se puder
}

PreviewState PreviewRenderer::stateOf (const juce::File& wnl, float* progress, juce::String* error) const
{
    if (progress != nullptr)
        *progress = 0.0f;

    if (running != nullptr && running->wnl == wnl)
    {
        if (progress != nullptr)
            *progress = running->job != nullptr ? running->job->getProgress() : 1.0f;

        return PreviewState::rendering;
    }

    if (waitingCommit.count (wnl.getFullPathName()) > 0)
        return PreviewState::rendering;

    if (std::find (queue.begin(), queue.end(), wnl) != queue.end())
        return PreviewState::queued;

    if (auto it = failures.find (wnl.getFullPathName()); it != failures.end())
    {
        if (error != nullptr)
            *error = it->second;

        return PreviewState::failed;
    }

    return PreviewState::none;
}

juce::File PreviewRenderer::renderingNow (float* progress) const
{
    if (progress != nullptr)
        *progress = running != nullptr && running->job != nullptr ? running->job->getProgress() : 0.0f;

    return running != nullptr ? running->wnl : juce::File();
}

bool PreviewRenderer::isIdle() const noexcept
{
    return queue.empty() && running == nullptr && waitingCommit.empty();
}

void PreviewRenderer::fail (const juce::File& wnl, const juce::String& why)
{
    failures[wnl.getFullPathName()] = why;

    if (onFinished)
        onFinished (wnl, false);
}

void PreviewRenderer::timerCallback()
{
    const bool wait = host.shouldWait != nullptr && host.shouldWait();

    if (running != nullptr && ! running->done.load() && wait && ! running->paused)
    {
        running->paused = true;   // o play pausa o render (decisão do usuário, 21/10)
        running->job->cancel();
    }

    if (running != nullptr && running->done.load())
        finishRunning();

    for (auto it = waitingCommit.begin(); it != waitingCommit.end();)
    {
        const auto path = (it++)->first;   // commit() apaga a entrada
        commit (path);
    }

    if (running == nullptr)
        startNext();

    if (isIdle())
    {
        waitReason = {};
        stopTimer();
    }
}

void PreviewRenderer::startNext()
{
    while (running == nullptr && ! queue.empty())
    {
        if (host.shouldWait != nullptr && host.shouldWait())
        {
            waitReason = "Espera a música parar."_t;
            return;
        }

        if (const int free = minFreeMemoryMB > 0 ? availableMemoryMB() : -1; free >= 0 && free < minFreeMemoryMB)
        {
            waitReason = "Espera memória livre ("_t + juce::String (free) + " MB)";
            return;
        }

        waitReason = {};
        const auto wnl = queue.front();
        queue.pop_front();

        // o .wnl salvo, só como dado: o hash e o projeto saem dos mesmos bytes
        juce::MemoryBlock bytes;

        if (! wnl.loadFileAsData (bytes))
        {
            fail (wnl, "Não consegui ler o projeto."_t);
            continue;
        }

        Project project;
        const auto parsed = wnl::fromJson (juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()), (int) bytes.getSize()), project);

        if (parsed.failed())
        {
            fail (wnl, parsed.getErrorMessage());
            continue;
        }

        auto run = std::make_unique<Running>();
        run->wnl = wnl;
        run->info.wnlHash = hashBytes (bytes.getData(), bytes.getSize());
        const bool song = ! project.playlistItems.empty();
        run->info.what = song ? "song" : "pattern";

        const auto folder = previewFolder (wnl);
        run->wav = folder.getChildFile (wnl.getFileNameWithoutExtension() + ".render.wav");
        run->part = folder.getChildFile (wnl.getFileNameWithoutExtension() + ".flac.part");

        RenderJob::Settings settings;
        settings.sampleRate = 48000.0;
        settings.bitDepth = 32;   // float: o LUFS e o pico saem antes dos 16 bits do FLAC

        auto error = juce::Result::ok();
        run->job = RenderJob::prepare (host.engine, host.cache, project, wnl.getParentDirectory(), song, patternLoops,
                                       run->wav, host.catalog, settings, error);

        if (run->job == nullptr)
        {
            fail (wnl, error.failed() ? error.getErrorMessage() : "Não consegui preparar o render."_t);
            continue;
        }

        auto* raw = run.get();
        running = std::move (run);
        running->worker.start ([raw]
        {
            auto result = raw->job->run();

            if (result.wasOk())
                result = encode (raw->wav, raw->part, raw->info);

            raw->wav.deleteFile();
            raw->result = result;
            raw->done.store (true);
        });
    }
}

void PreviewRenderer::finishRunning()
{
    running->worker.join (false);   // já terminou (done): só recolhe a thread

    auto finished = std::move (running);
    finished->job = nullptr;   // o Edit do render sai na thread de mensagem (num render cancelado, o grafo e o WAV também)
    finished->wav.deleteFile();

    if (finished->paused)
    {
        finished->part.deleteFile();
        queue.push_front (finished->wnl);   // recomeça quando a música parar
        return;
    }

    if (finished->result.failed())
    {
        finished->part.deleteFile();
        fail (finished->wnl, finished->result.getErrorMessage());
        return;
    }

    const auto path = finished->wnl.getFullPathName();
    waitingCommit[path] = finished->info;
    commit (path);
}

bool PreviewRenderer::commit (const juce::String& path)
{
    const auto it = waitingCommit.find (path);

    if (it == waitingCommit.end())
        return true;

    const juce::File wnl (path);
    const auto flac = previewAudio (wnl);
    const auto part = previewFolder (wnl).getChildFile (wnl.getFileNameWithoutExtension() + ".flac.part");

    if (host.release)
        host.release (flac);   // o deck larga o arquivo; o Windows não troca um arquivo aberto

    if (! part.moveFileTo (flac))
        return false;          // ainda aberto (o áudio larga no próximo bloco): tenta no próximo tique

    const auto info = it->second;
    waitingCommit.erase (it);
    const auto written = writePreviewInfo (wnl, info);

    if (written.failed())
    {
        fail (wnl, written.getErrorMessage());
        return true;
    }

    if (onFinished)
        onFinished (wnl, true);

    return true;
}

juce::Result PreviewRenderer::encode (const juce::File& wav, const juce::File& flacFile, PreviewInfo& info)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return juce::Result::fail ("Não consegui reler o render da prévia."_t);

    const auto total = reader->lengthInSamples;
    const auto rate = reader->sampleRate;

    flacFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (flacFile.createOutputStream().release());

    if (stream == nullptr)
        return juce::Result::fail ("Não consegui gravar "_t + flacFile.getFullPathName());

    juce::FlacAudioFormat flac;
    auto writer = flac.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                    .withSampleRate (rate)
                                                    .withNumChannels (2)
                                                    .withBitsPerSample (16)
                                                    .withQualityOptionIndex (5));

    if (writer == nullptr)
        return juce::Result::fail ("Não consegui criar o FLAC da prévia."_t);

    dsp::LoudnessMeter meter;
    meter.prepare (rate);
    std::vector<float> peaks ((size_t) numPeaks, 0.0f);
    constexpr int block = 16384;
    juce::AudioBuffer<float> buffer (2, block);
    juce::HeapBlock<int> left (block), right (block);
    const int* channels[] = { left.get(), right.get(), nullptr };

    // 16 bits arredondados (o writer do JUCE truncaria: até 1 LSB de erro). O FLAC recebe o inteiro de
    // 32 bits alinhado à esquerda e desloca 16 bits: o valor chega exato.
    auto toInt16 = [] (float x) noexcept
    {
        const float v = std::isfinite (x) ? std::round (x * 32768.0f) : 0.0f;
        return (int) juce::jlimit (-32768.0f, 32767.0f, v) * 65536;
    };

    for (juce::int64 pos = 0; pos < total;)
    {
        const int n = (int) juce::jmin ((juce::int64) block, total - pos);

        if (! reader->read (&buffer, 0, n, pos, true, true))
            return juce::Result::fail ("Não consegui reler o render da prévia."_t);

        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);
        meter.process (l, r, n);

        for (int i = 0; i < n; ++i)
        {
            auto& peak = peaks[(size_t) ((pos + i) * numPeaks / total)];
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }

        for (int i = 0; i < n; ++i)
        {
            left[i] = toInt16 (l[i]);
            right[i] = toInt16 (r[i]);
        }

        if (! writer->write (channels, n))
            return juce::Result::fail ("Não consegui gravar o FLAC da prévia (disco cheio?)."_t);

        pos += n;
    }

    writer = nullptr;   // fecha o arquivo

    info.seconds = (double) total / rate;
    info.sampleRate = rate;
    info.lufs = meter.integrated();
    info.truePeakDb = meter.truePeak();
    info.renderedAt = juce::Time::getCurrentTime();
    info.peaks.resize (peaks.size());

    for (size_t i = 0; i < peaks.size(); ++i)
        info.peaks[i] = juce::jlimit (0.0f, 1.0f, peaks[i]);

    info.valid = true;
    return juce::Result::ok();
}

} // namespace whynot::library
