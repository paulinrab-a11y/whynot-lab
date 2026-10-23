#include "ProjectIndex.h"

#include "../model/WnlFormat.h"

#include <set>

namespace whynot::library
{

namespace
{
    const char* const rootNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    bool isMinorScale (const juce::String& scale)
    {
        return scale == "minor" || scale == "harmonicMinor" || scale == "phrygian" || scale == "dorian"
            || scale == "pentatonicMinor";
    }

    bool skipFolder (const juce::File& dir)
    {
        const auto name = dir.getFileName();
        return name.startsWithChar ('.') || name.equalsIgnoreCase ("history") || name.equalsIgnoreCase ("preview")
            || name.equalsIgnoreCase ("samples") || name.equalsIgnoreCase ("takes") || name.equalsIgnoreCase ("autosave");
    }

    void collect (const juce::File& dir, int depth, std::set<juce::String>& seen, juce::Array<juce::File>& out)
    {
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto f = entry.getFile();

            if (entry.isDirectory())
            {
                if (depth > 1 && ! entry.isHidden() && ! skipFolder (f))
                    collect (f, depth - 1, seen, out);
            }
            else if (f.hasFileExtension ("wnl") && seen.insert (f.getFullPathName().toLowerCase()).second)
            {
                out.add (f);
            }
        }
    }
}

juce::String ProjectEntry::keyName() const
{
    const auto root = juce::String (rootNames[juce::jlimit (0, 11, keyRoot)]);
    return isMinorScale (scale) ? root + "m" : root;
}

juce::File previewFolder (const juce::File& wnl)   { return wnl.getSiblingFile ("preview"); }
juce::File previewAudio (const juce::File& wnl)    { return previewFolder (wnl).getChildFile (wnl.getFileNameWithoutExtension() + ".flac"); }
juce::File previewJson (const juce::File& wnl)     { return previewFolder (wnl).getChildFile (wnl.getFileNameWithoutExtension() + ".preview.json"); }

juce::String hashBytes (const void* data, size_t size)
{
    juce::uint64 h = 0xcbf29ce484222325ull;
    const auto* p = static_cast<const juce::uint8*> (data);

    for (size_t i = 0; i < size; ++i)
    {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }

    return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16);
}

juce::String hashFile (const juce::File& f)
{
    juce::MemoryBlock bytes;

    if (! f.loadFileAsData (bytes))
        return {};

    return hashBytes (bytes.getData(), bytes.getSize());
}

ProjectEntry readEntry (const juce::File& wnl)
{
    ProjectEntry e;
    e.file = wnl;
    e.name = wnl.getFileNameWithoutExtension();
    e.size = wnl.getSize();
    e.modified = wnl.getLastModificationTime();

    juce::MemoryBlock bytes;

    if (! wnl.loadFileAsData (bytes))
    {
        e.error = "Não consegui ler o arquivo."_t;
        return e;
    }

    e.hash = hashBytes (bytes.getData(), bytes.getSize());

    Project p;
    const auto result = wnl::fromJson (juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()), (int) bytes.getSize()), p);

    if (result.failed())
    {
        e.error = result.getErrorMessage();
        return e;
    }

    // O nome na lista é o do arquivo: "Salvar como" e as versões (_2, _3) guardam o nome antigo dentro do .wnl.
    e.bpm = p.bpm;
    e.timeSigNum = p.timeSigNum;
    e.timeSigDen = p.timeSigDen;
    e.keyRoot = p.keyRoot;
    e.scale = p.scale;
    e.hasSong = ! p.playlistItems.empty();
    return e;
}

PreviewInfo readPreviewInfo (const juce::File& wnl)
{
    PreviewInfo info;
    const auto json = previewJson (wnl);

    if (! json.existsAsFile() || ! previewAudio (wnl).existsAsFile())
        return info;

    const auto v = juce::JSON::parse (json);

    if (! v.isObject() || (int) v.getProperty ("version", 0) != 1)
        return info;

    info.wnlHash = v.getProperty ("wnlHash", {}).toString();
    info.seconds = (double) v.getProperty ("seconds", 0.0);
    info.sampleRate = (double) v.getProperty ("sampleRate", 48000.0);
    info.lufs = (float) (double) v.getProperty ("lufs", -100.0);
    info.truePeakDb = (float) (double) v.getProperty ("truePeakDb", -100.0);
    info.what = v.getProperty ("what", "song").toString();
    info.renderedAt = juce::Time::fromISO8601 (v.getProperty ("renderedAt", {}).toString());

    if (auto* peaks = v.getProperty ("peaks", {}).getArray())
    {
        info.peaks.reserve ((size_t) peaks->size());

        for (auto& p : *peaks)
            info.peaks.push_back (juce::jlimit (0.0f, 1.0f, (float) (double) p));
    }

    info.valid = info.wnlHash.isNotEmpty() && info.seconds > 0.0;
    return info;
}

juce::Result writePreviewInfo (const juce::File& wnl, const PreviewInfo& info)
{
    auto o = new juce::DynamicObject();
    juce::var v (o);
    o->setProperty ("version", 1);
    o->setProperty ("app", "WHYNOT LAB");
    o->setProperty ("wnl", wnl.getFileName());
    o->setProperty ("wnlHash", info.wnlHash);
    o->setProperty ("seconds", std::round (info.seconds * 1000.0) / 1000.0);
    o->setProperty ("sampleRate", info.sampleRate);
    o->setProperty ("lufs", std::round (info.lufs * 100.0) / 100.0);
    o->setProperty ("truePeakDb", std::round (info.truePeakDb * 100.0) / 100.0);
    o->setProperty ("what", info.what);
    o->setProperty ("renderedAt", info.renderedAt.toISO8601 (true));

    juce::Array<juce::var> peaks;
    peaks.ensureStorageAllocated ((int) info.peaks.size());

    for (auto p : info.peaks)
        peaks.add (std::round (p * 1000.0f) / 1000.0f);

    o->setProperty ("peaks", peaks);

    const auto file = previewJson (wnl);

    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("Não consegui criar a pasta "_t + file.getParentDirectory().getFullPathName());

    // replaceWithText grava num temporário e troca: quem lê nunca pega o arquivo pela metade
    if (! file.replaceWithText (juce::JSON::toString (v, true)))
        return juce::Result::fail ("Não consegui gravar "_t + file.getFullPathName());

    return juce::Result::ok();
}

PreviewState diskState (const ProjectEntry& e, const PreviewInfo& info)
{
    if (! info.valid)
        return PreviewState::none;

    return info.wnlHash == e.hash ? PreviewState::ok : PreviewState::stale;
}

juce::Array<juce::File> findProjects (const juce::Array<juce::File>& roots)
{
    std::set<juce::String> seen;
    juce::Array<juce::File> out;

    for (auto& root : roots)
        if (root.isDirectory())
            collect (root, 3, seen, out);

    std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFullPathName().compareNatural (b.getFullPathName()) < 0; });
    return out;
}

} // namespace whynot::library
