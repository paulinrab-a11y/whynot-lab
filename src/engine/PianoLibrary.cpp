#include "PianoLibrary.h"
#include "../common/Text.h"

#include <map>
#include <mutex>

namespace whynot::piano
{

namespace
{
    struct State
    {
        std::mutex lock;
        juce::File dataFolder, overrideFolder;
        std::map<juce::String, std::shared_ptr<const Library>> loaded;   // por pasta
    };

    State& state()
    {
        static State s;
        return s;
    }

    constexpr const char* manifestName = "whynot-piano.json";

    /** <base>/Bibliotecas/<nome>/whynot-piano.json: a primeira pasta, em ordem alfabética. */
    juce::File libraryIn (const juce::File& base)
    {
        const auto root = base.getChildFile ("Bibliotecas");

        if (base == juce::File() || ! root.isDirectory())
            return {};

        auto folders = root.findChildFiles (juce::File::findDirectories, false);
        folders.sort();

        for (auto& f : folders)
            if (f.getChildFile (manifestName).existsAsFile())
                return f;

        return {};
    }

    juce::File findLocked (const State& s)
    {
        if (s.overrideFolder != juce::File())
            return s.overrideFolder.getChildFile (manifestName).existsAsFile() ? s.overrideFolder : juce::File();

        const juce::File bases[] = {
            s.dataFolder,
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("WHYNOT LAB"),
            juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory(),
        };

        for (auto& b : bases)
            if (auto f = libraryIn (b); f != juce::File())
                return f;

        return {};
    }

    /** O manifesto é dado de fora: só nomes de arquivo simples, dentro da pasta dos samples. */
    bool plainName (const juce::String& n)
    {
        return n.isNotEmpty() && n.length() < 200 && ! n.containsAnyOf ("/\\:*?\"<>|") && n != "." && n != "..";
    }

    double number (const juce::var& o, const char* key, double fallback)
    {
        const auto& v = o[key];
        const double d = v.isDouble() || v.isInt() || v.isInt64() ? (double) v : fallback;
        return std::isfinite (d) ? d : fallback;
    }

    std::int16_t toInt16 (float x) noexcept
    {
        return (std::int16_t) juce::jlimit (-32768, 32767, juce::roundToInt (x * 32768.0f));
    }

    std::shared_ptr<const Library> loadFolder (const juce::File& folder, juce::String& error)
    {
        const auto manifest = juce::JSON::parse (folder.getChildFile (manifestName));

        if (manifest["format"].toString() != "whynot-piano")
        {
            error = "O manifesto do piano não é um whynot-piano.json válido: "_t + folder.getFullPathName();
            return {};
        }

        const auto* zones = manifest["zones"].getArray();

        if (zones == nullptr || zones->isEmpty())
        {
            error = "O manifesto do piano não tem zonas: "_t + folder.getFullPathName();
            return {};
        }

        auto lib = std::make_shared<Library>();
        lib->folder = folder;
        lib->name = manifest["name"].toString().substring (0, 80);
        lib->credit = manifest["credit"].toString().substring (0, 400);
        lib->license = manifest["license"].toString().substring (0, 200);

        const auto samplesName = manifest["samples"].toString();
        const auto samples = folder.getChildFile (plainName (samplesName) ? samplesName : juce::String ("samples"));

        juce::WavAudioFormat wav;
        juce::AudioBuffer<float> buffer (2, headFrames);
        int& skipped = lib->skippedZones;

        for (auto& zv : *zones)
        {
            if (lib->set.zones.size() >= 4000)   // o índice da tabela é de 16 bits
                break;

            const auto fileName = zv["file"].toString();

            if (! plainName (fileName))
            {
                ++skipped;
                continue;
            }

            const auto file = samples.getChildFile (fileName);
            std::unique_ptr<juce::MemoryMappedAudioFormatReader> reader (wav.createMemoryMappedReader (file));

            if (reader == nullptr || ! reader->mapEntireFile() || reader->lengthInSamples <= 8
                || reader->sampleRate < 8000.0 || reader->sampleRate > 384000.0 || reader->numChannels < 1)
            {
                ++skipped;
                continue;
            }

            audio::PianoZone z;
            z.rootKey = juce::jlimit (0, 127, (int) number (zv, "root", 60));
            z.loKey = juce::jlimit (0, 127, (int) number (zv, "lo", z.rootKey));
            z.hiKey = juce::jlimit (z.loKey, 127, (int) number (zv, "hi", z.rootKey));
            z.loVel = juce::jlimit (1, 127, (int) number (zv, "lovel", 1));
            z.hiVel = juce::jlimit (z.loVel, 127, (int) number (zv, "hivel", 127));
            z.release = zv["trigger"].toString() == "release";
            z.gain = juce::Decibels::decibelsToGain ((float) juce::jlimit (-96.0, 24.0, number (zv, "volumeDb", 0.0)));
            z.velTrack = (float) juce::jlimit (0.0, 1.0, number (zv, "veltrack", 1.0));
            z.releaseSeconds = (float) juce::jlimit (0.01, 30.0, number (zv, "releaseSeconds", 1.0));
            z.rtDecayDb = (float) juce::jlimit (0.0, 60.0, number (zv, "rtDecay", 0.0));
            z.keyTrack = (float) juce::jlimit (0.0, 2.0, number (zv, "keytrack", 1.0));
            z.sampleRate = reader->sampleRate;
            z.totalFrames = reader->lengthInSamples;
            z.file = file.getFullPathName().toStdString();

            const int n = (int) std::min<std::int64_t> (headFrames, z.totalFrames);
            buffer.clear();
            reader->read (&buffer, 0, n, 0, true, true);   // mono: o JUCE duplica no canal 2
            z.head.resize ((std::size_t) n * 2);

            for (int i = 0; i < n; ++i)
            {
                z.head[(std::size_t) i * 2] = toInt16 (buffer.getSample (0, i));
                z.head[(std::size_t) i * 2 + 1] = toInt16 (buffer.getSample (1, i));
            }

            lib->set.zones.push_back (std::move (z));
            lib->readers.push_back (std::move (reader));
        }

        if (lib->set.zones.empty())
        {
            error = "Nenhum sample do piano abriu em "_t + samples.getFullPathName();
            return {};
        }

        lib->set.buildTables();
        return lib;
    }
}

bool Library::read (int zone, std::int64_t from, int num, juce::AudioBuffer<float>& dest) const
{
    if (zone < 0 || zone >= (int) readers.size() || readers[(std::size_t) zone] == nullptr
        || num <= 0 || num > dest.getNumSamples() || dest.getNumChannels() < 2)
        return false;

    readers[(std::size_t) zone]->read (&dest, 0, num, from, true, true);
    return true;
}

std::int64_t Library::headBytes() const noexcept
{
    std::int64_t total = 0;

    for (auto& z : set.zones)
        total += (std::int64_t) z.head.size() * (std::int64_t) sizeof (std::int16_t);

    return total;
}

void setDataFolder (const juce::File& f)
{
    auto& s = state();
    const std::lock_guard<std::mutex> l (s.lock);
    s.dataFolder = f;
}

void setLibraryOverride (const juce::File& f)
{
    auto& s = state();
    const std::lock_guard<std::mutex> l (s.lock);
    s.overrideFolder = f;
}

juce::File findLibraryFolder()
{
    auto& s = state();
    const std::lock_guard<std::mutex> l (s.lock);
    return findLocked (s);
}

std::shared_ptr<const Library> load (juce::String& error)
{
    auto& s = state();
    const std::lock_guard<std::mutex> l (s.lock);
    const auto folder = findLocked (s);

    if (folder == juce::File())
    {
        error = "O WHYNOT Piano precisa dos sons do piano, que não estão neste computador. Eles ficam em "
                "%APPDATA%\\WHYNOT LAB\\Bibliotecas\\Salamander (vêm junto no pacote com o piano)."_t;
        return {};
    }

    const auto key = folder.getFullPathName();

    if (auto it = s.loaded.find (key); it != s.loaded.end())
        return it->second;

    auto lib = loadFolder (folder, error);

    if (lib != nullptr)
        s.loaded[key] = lib;

    return lib;
}

Prefetch::Prefetch()
    : worker ([]
      {
          if (findLibraryFolder() != juce::File())
          {
              juce::String ignored;   // sem biblioteca, o canal mostra o motivo quando for criado
              load (ignored);
          }
      })
{
}

Prefetch::~Prefetch()
{
    if (worker.joinable())
        worker.join();
}

} // namespace whynot::piano
