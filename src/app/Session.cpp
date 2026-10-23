#include "Session.h"

#if JUCE_WINDOWS
// Só o necessário do kernel32, sem puxar o <windows.h> inteiro para perto do JUCE.
extern "C"
{
    __declspec (dllimport) void* __stdcall OpenProcess (unsigned long access, int inherit, unsigned long pid);
    __declspec (dllimport) int __stdcall GetExitCodeProcess (void* process, unsigned long* code);
    __declspec (dllimport) int __stdcall CloseHandle (void* handle);
    __declspec (dllimport) unsigned long __stdcall GetCurrentProcessId();
    __declspec (dllimport) int __stdcall QueryFullProcessImageNameW (void* process, unsigned long flags, wchar_t* name, unsigned long* size);
}
#endif

namespace whynot
{

int Session::currentPid()
{
   #if JUCE_WINDOWS
    return (int) GetCurrentProcessId();
   #else
    return 0;
   #endif
}

bool Session::processAlive (int pid)
{
   #if JUCE_WINDOWS
    constexpr unsigned long queryLimitedInformation = 0x1000;
    constexpr unsigned long stillActive = 259;

    if (pid <= 0)
        return false;

    void* process = OpenProcess (queryLimitedInformation, 0, (unsigned long) pid);

    if (process == nullptr)
        return false;   // não existe (ou não é nosso: nesse caso também não é uma WHYNOT nossa)

    unsigned long code = 0;
    const bool alive = GetExitCodeProcess (process, &code) != 0 && code == stillActive;
    CloseHandle (process);
    return alive;
   #else
    juce::ignoreUnused (pid);
    return false;
   #endif
}

bool Session::isWhynotProcess (int pid)
{
   #if JUCE_WINDOWS
    constexpr unsigned long queryLimitedInformation = 0x1000;
    constexpr unsigned long stillActive = 259;

    if (pid <= 0)
        return false;

    void* process = OpenProcess (queryLimitedInformation, 0, (unsigned long) pid);

    if (process == nullptr)
        return false;

    unsigned long code = 0;
    wchar_t path[1024] = {};
    unsigned long size = 1024;
    const bool alive = GetExitCodeProcess (process, &code) != 0 && code == stillActive;
    const bool named = QueryFullProcessImageNameW (process, 0, path, &size) != 0;
    CloseHandle (process);

    if (! alive || ! named)
        return false;

    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFileName();
    return juce::File (juce::String (path, (size_t) size)).getFileName().equalsIgnoreCase (exe);
   #else
    juce::ignoreUnused (pid);
    return false;
   #endif
}

Session::Session (const juce::File& dataFolder)
{
    const auto folder = dataFolder.getChildFile ("sessoes");
    folder.createDirectory();

    // marcas de sessões que não fecharam direito: guarda o autosave mais novo entre elas
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "sessao-*.txt"))
    {
        const int pid = f.getFileNameWithoutExtension().fromFirstOccurrenceOf ("sessao-", false, false).getIntValue();

        if (isWhynotProcess (pid))
            continue;   // outra WHYNOT aberta agora: não é queda (PID reusado por outro programa conta como queda)

        const auto lines = juce::StringArray::fromLines (f.loadFileAsString());
        const juce::File autosave (lines[0].trim());

        if (juce::File::isAbsolutePath (lines[0].trim()) && autosave.existsAsFile()
            && autosave.getLastModificationTime() > recovery.when)
        {
            recovery.autosave = autosave;
            recovery.original = juce::File::isAbsolutePath (lines[1].trim()) ? juce::File (lines[1].trim()) : juce::File();
            recovery.when = autosave.getLastModificationTime();
        }

        f.deleteFile();
    }

    marker = folder.getChildFile ("sessao-" + juce::String (currentPid()) + ".txt");
    marker.replaceWithText ({});
}

Session::~Session()
{
    marker.deleteFile();   // fechou direito: nada a recuperar
}

void Session::noteAutosave (const juce::File& autosave, const juce::File& original)
{
    marker.replaceWithText (autosave.getFullPathName() + "\n" + original.getFullPathName() + "\n");
}

} // namespace whynot
