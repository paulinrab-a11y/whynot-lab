#include "PluginCatalog.h"

#if JUCE_WINDOWS
// Só o necessário da API do Windows, sem puxar o windows.h inteiro.
extern "C" __declspec (dllimport) unsigned int __stdcall SetErrorMode (unsigned int mode);
#endif

namespace whynot
{

namespace
{
    constexpr const char* rootTag = "WHYNOT_PLUGINS";
    constexpr int cacheVersion = 1;

    juce::String describeExitCode (juce::uint32 code)
    {
        switch (code)
        {
            case 3:            return "deu erro ao ser aberto (exceção dentro do plugin)"_t;
            case 0xC0000005u:  return "derrubou o processo de scan (acesso inválido à memória)"_t;
            case 0xC00000FDu:  return "derrubou o processo de scan (estouro de pilha)"_t;
            case 0xC0000409u:  return "derrubou o processo de scan (proteção de pilha)"_t;
            default:           break;
        }

        return "derrubou o processo de scan (código 0x"_t + juce::String::toHexString ((juce::int64) code).toUpperCase() + ")";
    }

    // O que roda dentro do processo filho. Separado (e sem inline) para o __try abaixo não ter objetos a destruir.
   #if JUCE_MSVC
    __declspec (noinline)
   #endif
    void findTypes (const juce::String& file, juce::OwnedArray<juce::PluginDescription>& found)
    {
        juce::VST3PluginFormat format;
        format.findAllTypesForFile (found, file);
    }

    bool findTypesGuarded (const juce::String& file, juce::OwnedArray<juce::PluginDescription>& found)
    {
       #if JUCE_WINDOWS
        __try
        {
            findTypes (file, found);
            return true;
        }
        __except (1)   // EXCEPTION_EXECUTE_HANDLER: qualquer exceção, inclusive acesso inválido
        {
            return false;
        }
       #else
        findTypes (file, found);
        return true;
       #endif
    }
}

int runPluginScanChild (const juce::String& pluginFile, const juce::File& output)
{
   #if JUCE_WINDOWS
    // Sem janelas de "o programa parou de funcionar": quem decide o que fazer é o processo pai.
    SetErrorMode (0x0001 | 0x0002 | 0x8000);   // FAILCRITICALERRORS | NOGPFAULTERRORBOX | NOOPENFILEERRORBOX
   #endif

    juce::OwnedArray<juce::PluginDescription> found;

    if (! findTypesGuarded (pluginFile, found))
        return 3;

    juce::XmlElement root ("WHYNOT_SCAN");

    for (auto* d : found)
        root.addChildElement (d->createXml().release());

    return root.writeTo (output) ? 0 : 4;
}

//==============================================================================
PluginCatalog::PluginCatalog (const juce::File& file) : cacheFile (file)
{
    folders = defaultFolders();
}

juce::StringArray PluginCatalog::defaultFolders()
{
   #if JUCE_WINDOWS
    return { juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                 .getChildFile ("Common Files").getChildFile ("VST3").getFullPathName() };
   #elif JUCE_MAC
    return { "~/Library/Audio/Plug-Ins/VST3", "/Library/Audio/Plug-Ins/VST3" };
   #else
    return { "~/.vst3", "/usr/lib/vst3", "/usr/local/lib/vst3" };
   #endif
}

void PluginCatalog::load()
{
    const juce::ScopedLock sl (lock);
    files.clear();
    blocked.clear();
    folders = defaultFolders();

    auto xml = juce::parseXML (cacheFile);

    if (xml == nullptr || ! xml->hasTagName (rootTag))
        return;

    if (auto* f = xml->getChildByName ("FOLDERS"))
    {
        folders.clear();

        for (auto* e : f->getChildWithTagNameIterator ("FOLDER"))
            folders.addIfNotAlreadyThere (e->getStringAttribute ("path"));
    }

    for (auto* e : xml->getChildWithTagNameIterator ("FILE"))
    {
        FileEntry entry;
        entry.file = e->getStringAttribute ("path");
        entry.stamp = e->getStringAttribute ("stamp").getLargeIntValue();

        for (auto* d : e->getChildWithTagNameIterator ("PLUGIN"))
        {
            juce::PluginDescription desc;

            if (desc.loadFromXml (*d))
                entry.types.add (desc);
        }

        if (entry.file.isNotEmpty())
            files.push_back (std::move (entry));
    }

    for (auto* e : xml->getChildWithTagNameIterator ("BLOCKED"))
        blocked.push_back ({ e->getStringAttribute ("path"), e->getStringAttribute ("name"), e->getStringAttribute ("reason"),
                             juce::Time (e->getStringAttribute ("when").getLargeIntValue()) });

    ++version;
}

bool PluginCatalog::save() const
{
    juce::XmlElement xml (rootTag);
    xml.setAttribute ("version", cacheVersion);

    {
        const juce::ScopedLock sl (lock);
        auto* f = xml.createNewChildElement ("FOLDERS");

        for (auto& folder : folders)
            f->createNewChildElement ("FOLDER")->setAttribute ("path", folder);

        for (auto& entry : files)
        {
            auto* e = xml.createNewChildElement ("FILE");
            e->setAttribute ("path", entry.file);
            e->setAttribute ("stamp", juce::String (entry.stamp));

            for (auto& d : entry.types)
                e->addChildElement (d.createXml().release());
        }

        for (auto& b : blocked)
        {
            auto* e = xml.createNewChildElement ("BLOCKED");
            e->setAttribute ("path", b.file);
            e->setAttribute ("name", b.name);
            e->setAttribute ("reason", b.reason);
            e->setAttribute ("when", juce::String (b.when.toMilliseconds()));
        }
    }

    cacheFile.getParentDirectory().createDirectory();
    return xml.writeTo (cacheFile);
}

juce::Array<juce::PluginDescription> PluginCatalog::getTypes() const
{
    const juce::ScopedLock sl (lock);
    juce::Array<juce::PluginDescription> out;

    for (auto& entry : files)
        out.addArray (entry.types);

    std::sort (out.begin(), out.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
               { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}

std::vector<PluginCatalog::Blocked> PluginCatalog::getBlocked() const
{
    const juce::ScopedLock sl (lock);
    return blocked;
}

bool PluginCatalog::isBlocked (const juce::String& file) const
{
    return blockReason (file).isNotEmpty();
}

juce::String PluginCatalog::blockReason (const juce::String& file) const
{
    const juce::ScopedLock sl (lock);

    for (auto& b : blocked)
        if (b.file == file)
            return b.reason.isNotEmpty() ? b.reason : juce::String ("bloqueado");

    return {};
}

void PluginCatalog::block (const juce::String& file, const juce::String& name, const juce::String& reason)
{
    {
        const juce::ScopedLock sl (lock);
        std::erase_if (blocked, [&file] (const Blocked& b) { return b.file == file; });
        blocked.push_back ({ file, name, reason, juce::Time::getCurrentTime() });

        // o que estava no cache sai: o plugin só volta com um scan depois de desbloquear
        std::erase_if (files, [&file] (const FileEntry& e) { return e.file == file; });
    }

    ++version;
}

bool PluginCatalog::unblock (const juce::String& file)
{
    bool removed = false;

    {
        const juce::ScopedLock sl (lock);
        const auto before = blocked.size();
        std::erase_if (blocked, [&file] (const Blocked& b) { return b.file == file; });
        removed = blocked.size() < before;
    }

    if (removed)
        ++version;

    return removed;
}

juce::StringArray PluginCatalog::getFolders() const
{
    const juce::ScopedLock sl (lock);
    return folders;
}

void PluginCatalog::setFolders (const juce::StringArray& newFolders)
{
    const juce::ScopedLock sl (lock);
    folders.clear();

    for (auto& f : newFolders)
        if (f.trim().isNotEmpty())
            folders.addIfNotAlreadyThere (f.trim());
}

void PluginCatalog::fillKnownList (juce::KnownPluginList& list) const
{
    list.clear();
    list.clearBlacklistedFiles();

    for (auto& d : getTypes())
        list.addType (d);

    for (auto& b : getBlocked())
        list.addToBlacklist (b.file);
}

std::optional<juce::PluginDescription> PluginCatalog::find (const juce::String& idOrName) const
{
    const auto types = getTypes();

    for (auto& d : types)
        if (d.createIdentifierString() == idOrName)
            return d;

    for (auto& d : types)
        if (d.name.equalsIgnoreCase (idOrName.trim()))
            return d;

    return std::nullopt;
}

//==============================================================================
juce::int64 PluginCatalog::stampOf (const juce::File& f)
{
    // Um .vst3 pode ser pasta (pacote) ou arquivo: a data do binário de dentro é a que muda numa atualização.
    juce::int64 stamp = f.getLastModificationTime().toMilliseconds() ^ (f.getSize() << 20);

    if (f.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (f.getChildFile ("Contents"), true, "*.vst3;*.dll", juce::File::findFiles))
            stamp ^= entry.getModificationTime().toMilliseconds() + entry.getFileSize();

    return stamp;
}

juce::String PluginCatalog::scanInChildProcess (const juce::String& file, int timeoutMs, juce::Array<juce::PluginDescription>& found,
                                               const std::function<bool()>& shouldAbort)
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto output = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("whynot-scan", ".xml", false);

    juce::ChildProcess child;

    // sem pipes: o filho não escreve nada no console e um pipe cheio travaria o processo
    if (! child.start (juce::StringArray { exe.getFullPathName(), "--scan-plugin", file, output.getFullPathName() }, 0))
        return "!não consegui abrir o processo de scan"_t;   // '!' = problema nosso, não do plugin

    // espera em fatias curtas para o app poder cancelar (fechar no meio do scan não pode demorar)
    const auto started = juce::Time::getMillisecondCounter();

    while (! child.waitForProcessToFinish (100))
    {
        if (shouldAbort && shouldAbort())
        {
            child.kill();
            output.deleteFile();
            return "!scan cancelado"_t;
        }

        if ((int) (juce::Time::getMillisecondCounter() - started) >= timeoutMs)
        {
            child.kill();
            output.deleteFile();
            return "não respondeu em "_t + juce::String (juce::roundToInt (timeoutMs / 1000.0)) + " s (travou ao ser aberto)";
        }
    }

    const auto code = child.getExitCode();
    auto xml = output.existsAsFile() ? juce::parseXML (output) : nullptr;
    output.deleteFile();

    if (code != 0 || xml == nullptr)
        return describeExitCode (code);

    for (auto* e : xml->getChildWithTagNameIterator ("PLUGIN"))
    {
        juce::PluginDescription d;

        if (d.loadFromXml (*e))
            found.add (d);
    }

    return {};
}

PluginCatalog::ScanReport PluginCatalog::scan (int timeoutMs, const Progress& progress, const std::function<bool()>& shouldAbort)
{
    ScanReport report;

    juce::VST3PluginFormat format;
    juce::FileSearchPath path;

    for (auto& f : getFolders())
        path.add (juce::File (f));

    const auto candidates = format.searchPathsForPlugins (path, true, false);
    report.candidates = candidates.size();

    std::vector<FileEntry> next;

    for (int i = 0; i < candidates.size(); ++i)
    {
        const auto& file = candidates[i];

        if (shouldAbort && shouldAbort())
        {
            report.aborted = true;
            break;
        }

        if (progress)
            progress (i, candidates.size(), juce::File (file).getFileNameWithoutExtension());

        if (isBlocked (file))
            continue;

        const auto stamp = stampOf (juce::File (file));
        bool reused = false;

        {
            const juce::ScopedLock sl (lock);

            for (auto& entry : files)
            {
                if (entry.file == file && entry.stamp == stamp)
                {
                    next.push_back (entry);
                    reused = true;
                    break;
                }
            }
        }

        if (reused)
        {
            ++report.reused;
            continue;
        }

        juce::Array<juce::PluginDescription> found;
        const auto problem = scanInChildProcess (file, timeoutMs, found, shouldAbort);
        ++report.scanned;

        if (problem.startsWithChar ('!'))
        {
            report.newlyBlocked.add (juce::File (file).getFileName() + ": " + problem.substring (1));
            continue;   // falha nossa: não culpa o plugin
        }

        if (problem.isNotEmpty())
        {
            block (file, juce::File (file).getFileNameWithoutExtension(), problem);
            report.newlyBlocked.add (juce::File (file).getFileName() + ": " + problem);
            continue;
        }

        next.push_back ({ file, stamp, found });
    }

    if (! report.aborted)
    {
        const juce::ScopedLock sl (lock);
        files = std::move (next);   // arquivos que sumiram das pastas saem do catálogo
    }

    report.found = getTypes().size();
    ++version;

    if (progress)
        progress (candidates.size(), candidates.size(), {});

    return report;
}

} // namespace whynot
