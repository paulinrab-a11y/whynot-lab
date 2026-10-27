#include "NoteTuneFiles.h"

#include "../common/Text.h"
#include "../engine/SampleAnalysis.h"

#include <map>
#include <mutex>
#include <set>

namespace whynot::notetune
{

namespace
{
    // sobe quando a análise ou o render mudam: o cache do disco e os arquivos refeitos antigos deixam de valer
    constexpr int analysisVersion = 1;
    constexpr int renderVersion = 1;
    constexpr juce::int64 maxSamples = (juce::int64) 48000 * 60 * 60;
    constexpr int keptRenders = 6;   // versões refeitas guardadas por arquivo (o desfazer volta a elas sem refazer)

    std::mutex cacheLock;
    std::map<juce::String, std::shared_ptr<const dsp::NoteAnalysis>> analyses;
    std::vector<std::pair<juce::String, std::shared_ptr<const SourceAudio>>> audios;   // os dois últimos
    std::set<juce::String> failedRenders;   // o playedFile não tenta de novo a cada mudança do projeto
    CacheCounts counts;

    juce::String keyFor (const juce::File& f)
    {
        return f.getFullPathName() + "|" + juce::String (f.getSize()) + "|" + juce::String (f.getLastModificationTime().toMilliseconds());
    }

    juce::File diskCacheFor (const juce::String& key)
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("WHYNOT LAB").getChildFile ("notas")
            .getChildFile (juce::String::toHexString ((juce::int64) (key + "|v" + juce::String (analysisVersion)).hashCode64()) + ".bin");
    }

    void writeDisk (const juce::File& file, const dsp::NoteAnalysis& a)
    {
        juce::MemoryOutputStream out;
        out.writeInt (0x4e4c4e57);   // "WNLN"
        out.writeInt (analysisVersion);
        out.writeDouble (a.sampleRate);
        out.writeInt64 ((juce::int64) a.numFrames);
        out.writeDouble (a.curve.hopSeconds);
        out.writeInt ((int) a.curve.midi.size());
        out.write (a.curve.midi.data(), a.curve.midi.size() * sizeof (float));
        out.write (a.curve.level.data(), a.curve.level.size() * sizeof (float));
        out.writeInt ((int) a.notes.size());

        for (auto& n : a.notes)
        {
            out.writeDouble (n.start);
            out.writeDouble (n.end);
            out.writeFloat (n.midi);
            out.writeFloat (n.driftCents);
            out.writeFloat (n.vibratoCents);
        }

        file.getParentDirectory().createDirectory();
        file.replaceWithData (out.getData(), out.getDataSize());
    }

    std::shared_ptr<const dsp::NoteAnalysis> readDisk (const juce::File& file)
    {
        juce::MemoryBlock data;

        if (! file.existsAsFile() || ! file.loadFileAsData (data))
            return nullptr;

        juce::MemoryInputStream in (data, false);

        if (in.readInt() != 0x4e4c4e57 || in.readInt() != analysisVersion)
            return nullptr;

        auto a = std::make_shared<dsp::NoteAnalysis>();
        a->sampleRate = in.readDouble();
        a->numFrames = (std::size_t) juce::jmax ((juce::int64) 0, in.readInt64());
        a->curve.hopSeconds = in.readDouble();
        const int frames = in.readInt();

        if (frames < 0 || frames > 2'000'000 || ! (a->curve.hopSeconds > 0.0) || (juce::int64) frames * 8 > in.getNumBytesRemaining())
            return nullptr;

        a->curve.midi.resize ((size_t) frames);
        a->curve.level.resize ((size_t) frames);
        in.read (a->curve.midi.data(), frames * (int) sizeof (float));
        in.read (a->curve.level.data(), frames * (int) sizeof (float));
        const int notes = in.readInt();

        if (notes < 0 || (juce::int64) notes * 28 > in.getNumBytesRemaining())
            return nullptr;

        for (int i = 0; i < notes; ++i)
        {
            dsp::NoteSegment s;
            s.start = in.readDouble();
            s.end = in.readDouble();
            s.midi = in.readFloat();
            s.driftCents = in.readFloat();
            s.vibratoCents = in.readFloat();
            a->notes.push_back (s);
        }

        return a;
    }

    std::vector<float> mixToMono (const SourceAudio& audio)
    {
        std::vector<float> mono = audio.channels[0];

        if (audio.channels.size() > 1)
            for (size_t i = 0; i < mono.size(); ++i)
                mono[i] = 0.5f * (mono[i] + audio.channels[1][i]);

        return mono;
    }

    /** Um nome que o render escreve: "<nome>_nota_" + 10 dígitos hexadecimais + ".wav" (nada mais da pasta). */
    bool isRenderOf (const juce::File& source, const juce::File& f)
    {
        const auto prefix = source.getFileNameWithoutExtension() + "_nota_";
        const auto name = f.getFileName();

        if (! name.startsWith (prefix) || ! name.endsWithIgnoreCase (".wav") || name.length() != prefix.length() + 14)
            return false;

        return name.substring (prefix.length(), prefix.length() + 10).containsOnly ("0123456789abcdef");
    }

    /** Apaga as versões refeitas mais antigas deste original, menos `keep`. */
    void pruneRenders (const juce::File& source, const juce::File& keep)
    {
        auto list = source.getParentDirectory().findChildFiles (juce::File::findFiles, false, source.getFileNameWithoutExtension() + "_nota_*.wav");
        list.removeIf ([&source] (const juce::File& f) { return ! isRenderOf (source, f); });
        std::sort (list.begin(), list.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getLastModificationTime() > b.getLastModificationTime(); });

        for (int i = keptRenders; i < list.size(); ++i)
            if (list[i] != keep)
                list[i].deleteFile();
    }
}

//==============================================================================
std::shared_ptr<const SourceAudio> loadAudio (const juce::File& file, juce::String& error)
{
    const auto key = keyFor (file);

    {
        std::lock_guard<std::mutex> lock (cacheLock);

        for (auto& [k, a] : audios)
            if (k == key)
                return a;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > maxSamples || reader->sampleRate <= 0.0)
    {
        error = "Não consegui ler o áudio: "_t + file.getFullPathName();
        return nullptr;
    }

    auto audio = std::make_shared<SourceAudio>();

    try
    {
        const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        juce::AudioBuffer<float> buffer (channels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, channels > 1);
        audio->sampleRate = reader->sampleRate;

        // o MP3 começa com o atraso do codificador, que o clipe da playlist pula (Projector): aqui também
        const int skip = juce::jlimit (0, buffer.getNumSamples() - 1, (int) std::lround (mp3EncoderDelaySeconds (file) * reader->sampleRate));

        for (int ch = 0; ch < channels; ++ch)
            audio->channels.emplace_back (buffer.getReadPointer (ch) + skip, buffer.getReadPointer (ch) + buffer.getNumSamples());
    }
    catch (const std::bad_alloc&)
    {
        error = "Memória insuficiente para ler o áudio: "_t + file.getFileName();
        return nullptr;
    }

    std::lock_guard<std::mutex> lock (cacheLock);
    audios.insert (audios.begin(), { key, audio });

    if (audios.size() > 2)
        audios.resize (2);

    return audio;
}

std::shared_ptr<const dsp::NoteAnalysis> cachedAnalysis (const juce::File& file)
{
    std::lock_guard<std::mutex> lock (cacheLock);
    const auto it = analyses.find (keyFor (file));
    return it != analyses.end() ? it->second : nullptr;
}

std::shared_ptr<const dsp::NoteAnalysis> analyse (const juce::File& file, juce::String& error, const std::atomic<bool>* cancel)
{
    const auto key = keyFor (file);

    {
        std::lock_guard<std::mutex> lock (cacheLock);

        if (const auto it = analyses.find (key); it != analyses.end())
        {
            ++counts.memory;
            return it->second;
        }
    }

    const auto disk = diskCacheFor (key);

    if (auto fromDisk = readDisk (disk))
    {
        std::lock_guard<std::mutex> lock (cacheLock);
        ++counts.disk;
        analyses[key] = fromDisk;
        return fromDisk;
    }

    const auto audio = loadAudio (file, error);

    if (audio == nullptr)
        return nullptr;

    std::shared_ptr<const dsp::NoteAnalysis> result;

    try
    {
        const auto mono = mixToMono (*audio);
        dsp::NoteAnalysisOptions options;
        options.cancel = cancel;
        auto done = dsp::analyseNotes (mono.data(), mono.size(), audio->sampleRate, options);

        if (done.cancelled)
            return nullptr;

        result = std::make_shared<const dsp::NoteAnalysis> (std::move (done));
    }
    catch (const std::bad_alloc&)
    {
        error = "Memória insuficiente para analisar "_t + file.getFileName();
        return nullptr;
    }

    writeDisk (disk, *result);

    std::lock_guard<std::mutex> lock (cacheLock);
    ++counts.computed;
    analyses[key] = result;
    return result;
}

CacheCounts cacheCounts()
{
    std::lock_guard<std::mutex> lock (cacheLock);
    return counts;
}

void forgetMemoryCache()
{
    std::lock_guard<std::mutex> lock (cacheLock);
    analyses.clear();
    audios.clear();
}

//==============================================================================
juce::File renderedFileFor (const juce::File& source, const std::vector<TunedNote>& notes, bool formantFollows)
{
    juce::String key;
    key << "v" << renderVersion << "|" << source.getSize() << "|" << source.getLastModificationTime().toMilliseconds()
        << "|" << (formantFollows ? 1 : 0);

    for (auto& n : notes)
        key << "|" << (juce::int64) std::llround (n.start * 1.0e5) << "," << (juce::int64) std::llround (n.end * 1.0e5) << ","
            << juce::roundToInt (n.sourceMidi * 100.0) << "," << juce::roundToInt (n.shift * 100.0) << ","
            << juce::roundToInt (n.center * 100.0) << "," << juce::roundToInt (n.flatten * 100.0) << ","
            << juce::roundToInt (n.formant * 100.0) << "," << juce::roundToInt (n.transitionMs);

    const auto code = juce::String::toHexString ((juce::int64) key.hashCode64()).paddedLeft ('0', 16).substring (0, 10);
    return source.getSiblingFile (source.getFileNameWithoutExtension() + "_nota_" + code + ".wav");
}

juce::Result render (const juce::File& source, const std::vector<TunedNote>& notes, bool formantFollows, const juce::File& target,
                     dsp::NoteRenderStats* stats)
{
    juce::String error;
    const auto analysis = analyse (source, error);
    const auto audio = analysis != nullptr ? loadAudio (source, error) : nullptr;

    if (analysis == nullptr || audio == nullptr)
        return juce::Result::fail (error);

    juce::AudioBuffer<float> buffer;

    try
    {
        const auto out = dsp::renderNotes (audio->channels, audio->sampleRate, analysis->curve, notes, formantFollows, stats);
        buffer.setSize ((int) out.size(), (int) out[0].size());

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            std::copy (out[(size_t) ch].begin(), out[(size_t) ch].end(), buffer.getWritePointer (ch));
    }
    catch (const std::bad_alloc&)
    {
        return juce::Result::fail ("Memória insuficiente para refazer "_t + source.getFileName());
    }

    // escreve num temporário e troca: um arquivo pela metade nunca fica com o nome final
    const auto temp = target.getSiblingFile (target.getFileNameWithoutExtension() + ".tmp");
    temp.deleteFile();

    {
        std::unique_ptr<juce::OutputStream> stream (temp.createOutputStream().release());

        if (stream == nullptr)
            return juce::Result::fail ("Não consegui criar "_t + temp.getFullPathName());

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (audio->sampleRate)
                                                     .withNumChannels (buffer.getNumChannels())
                                                     .withBitsPerSample (32)
                                                     .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        {
            writer.reset();
            temp.deleteFile();
            return juce::Result::fail ("Não consegui escrever "_t + target.getFullPathName());
        }
    }

    if (! temp.moveFileTo (target))
    {
        temp.deleteFile();
        return juce::Result::fail ("Não consegui escrever "_t + target.getFullPathName());
    }

    pruneRenders (source, target);

    {
        std::lock_guard<std::mutex> lock (cacheLock);
        failedRenders.erase (target.getFullPathName());
    }

    return juce::Result::ok();
}

juce::File playedFile (const juce::File& source, const std::vector<TunedNote>& notes, bool formantFollows)
{
    if (! anyEdited (notes) || ! source.existsAsFile())
        return source;

    const auto target = renderedFileFor (source, notes, formantFollows);

    if (target.existsAsFile())
        return target;

    {
        // já falhou nesta execução (pasta sem escrita, disco cheio): não refaz a cada mudança do projeto
        std::lock_guard<std::mutex> lock (cacheLock);

        if (failedRenders.count (target.getFullPathName()) != 0)
            return source;
    }

    if (render (source, notes, formantFollows, target).failed())
    {
        std::lock_guard<std::mutex> lock (cacheLock);
        failedRenders.insert (target.getFullPathName());
        return source;   // sem como refazer, toca o original (o Projector avisa no pé)
    }

    return target;
}

} // namespace whynot::notetune
