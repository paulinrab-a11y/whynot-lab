// Log de sessão (ver Log.h). Sem JUCE.

#include "Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <mutex>
#include <system_error>

#ifdef _WIN32
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <share.h>
#else
 #include <unistd.h>
#endif

namespace whynot
{

namespace
{
    struct State
    {
        std::mutex mutex;
        std::FILE* file = nullptr;
        std::filesystem::path path;
        std::size_t written = 0;
        bool full = false;
    };

    // De propósito sem destrutor: uma thread pode chamar log() enquanto o processo fecha.
    State& state()
    {
        static State* const s = new State();
        return *s;
    }

    std::atomic<bool> opened { false };

    unsigned long currentPid() noexcept
    {
       #ifdef _WIN32
        return (unsigned long) GetCurrentProcessId();
       #else
        return (unsigned long) getpid();
       #endif
    }

    unsigned long currentThread() noexcept
    {
       #ifdef _WIN32
        return (unsigned long) GetCurrentThreadId();
       #else
        return 0;
       #endif
    }

    struct Now
    {
        std::tm local {};
        int millis = 0;
    };

    Now now() noexcept
    {
        Now n;
        const auto point = std::chrono::system_clock::now();
        const std::time_t seconds = std::chrono::system_clock::to_time_t (point);
        n.millis = (int) (std::chrono::duration_cast<std::chrono::milliseconds> (point.time_since_epoch()).count() % 1000);

       #ifdef _WIN32
        localtime_s (&n.local, &seconds);
       #else
        localtime_r (&seconds, &n.local);
       #endif
        return n;
    }

    std::FILE* openForAppend (const std::filesystem::path& path) noexcept
    {
       #ifdef _WIN32
        // _SH_DENYNO: o registro de queda (CrashReport) acrescenta a última linha por outro identificador.
        return _wfsopen (path.c_str(), L"ab", _SH_DENYNO);
       #else
        return std::fopen (path.c_str(), "ab");
       #endif
    }

    // Com o mutex travado.
    void writeLocked (State& s, std::string_view text)
    {
        if (s.file == nullptr || s.full)
            return;

        const auto n = now();
        char head[48];
        const int headSize = std::snprintf (head, sizeof (head), "%02d:%02d:%02d.%03d [%lu] ",
                                            n.local.tm_hour, n.local.tm_min, n.local.tm_sec, n.millis, currentThread());

        std::string line (head, (std::size_t) std::max (0, headSize));
        line += sessionlog::cleanLine (text);
        line += '\n';

        if (s.written + line.size() > sessionlog::maxFileBytes)
        {
            s.full = true;
            line = std::string (head, (std::size_t) std::max (0, headSize)) + "(log cheio: o resto desta sessão não foi registrado)\n";
        }

        std::fwrite (line.data(), 1, line.size(), s.file);
        std::fflush (s.file);   // a linha tem de estar no arquivo se o app cair logo depois
        s.written += line.size();
    }
}

void log (std::string_view line) noexcept
{
    if (! opened.load (std::memory_order_acquire))
        return;

    try
    {
        auto& s = state();
        const std::lock_guard<std::mutex> lock (s.mutex);
        writeLocked (s, line);
    }
    catch (...)
    {}
}

namespace sessionlog
{

bool start (const std::filesystem::path& folder, std::size_t keep)
{
    try
    {
        auto& s = state();
        const std::lock_guard<std::mutex> lock (s.mutex);

        if (s.file != nullptr)
            return true;

        std::error_code error;
        std::filesystem::create_directories (folder, error);

        // rodízio: ficam as `keep - 1` sessões mais novas, mais a que começa agora
        std::vector<std::string> names;

        for (std::filesystem::directory_iterator it (folder, error), end; ! error && it != end; it.increment (error))
            if (it->path().extension() == ".log")
                names.push_back (it->path().filename().string());

        for (const auto& name : filesToPrune (names, keep > 0 ? keep - 1 : 0))
            std::filesystem::remove (folder / name, error);

        const auto n = now();
        const auto stem = sessionStem (n.local.tm_year + 1900, n.local.tm_mon + 1, n.local.tm_mday,
                                       n.local.tm_hour, n.local.tm_min, n.local.tm_sec, currentPid());
        const auto path = folder / (stem + ".log");

        s.file = openForAppend (path);

        if (s.file == nullptr)
            return false;

        s.path = path;
        s.written = 0;
        s.full = false;
        opened.store (true, std::memory_order_release);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void stop() noexcept
{
    try
    {
        auto& s = state();
        const std::lock_guard<std::mutex> lock (s.mutex);
        opened.store (false, std::memory_order_release);

        if (s.file != nullptr)
            std::fclose (s.file);

        s.file = nullptr;
        s.path.clear();
    }
    catch (...)
    {}
}

bool isOpen() noexcept
{
    return opened.load (std::memory_order_acquire);
}

std::filesystem::path currentFile()
{
    auto& s = state();
    const std::lock_guard<std::mutex> lock (s.mutex);
    return s.path;
}

//==============================================================================
std::string sessionStem (int year, int month, int day, int hour, int minute, int second, unsigned long pid)
{
    char text[64];
    std::snprintf (text, sizeof (text), "%04d%02d%02d-%02d%02d%02d-%lu", year, month, day, hour, minute, second, pid);
    return text;
}

bool isSessionStem (std::string_view stem) noexcept
{
    // AAAAMMDD-HHMMSS-pid
    if (stem.size() < 17 || stem[8] != '-' || stem[15] != '-')
        return false;

    for (std::size_t i = 0; i < stem.size(); ++i)
        if (i != 8 && i != 15 && (stem[i] < '0' || stem[i] > '9'))
            return false;

    return true;
}

std::vector<std::string> filesToPrune (const std::vector<std::string>& names, std::size_t keep)
{
    auto stemOf = [] (const std::string& name) { return name.substr (0, name.rfind ('.')); };

    std::vector<std::string> stems;

    for (const auto& name : names)
        if (isSessionStem (stemOf (name)))
            stems.push_back (stemOf (name));

    std::sort (stems.begin(), stems.end(), std::greater<>());   // a mais nova primeiro
    stems.erase (std::unique (stems.begin(), stems.end()), stems.end());

    if (stems.size() <= keep)
        return {};

    const std::vector<std::string> old (stems.begin() + (std::ptrdiff_t) keep, stems.end());
    std::vector<std::string> out;

    for (const auto& name : names)
        if (std::find (old.begin(), old.end(), stemOf (name)) != old.end())
            out.push_back (name);

    std::sort (out.begin(), out.end());
    return out;
}

std::string cleanLine (std::string_view text, std::size_t maxBytes)
{
    bool cut = false;

    if (text.size() > maxBytes)
    {
        std::size_t end = maxBytes;

        while (end > 0 && (static_cast<unsigned char> (text[end]) & 0xC0) == 0x80)   // não parte um caractere UTF-8
            --end;

        text = text.substr (0, end);
        cut = true;
    }

    std::string out (text);

    for (auto& c : out)
        if (static_cast<unsigned char> (c) < 0x20 || c == 0x7F)
            c = ' ';

    if (cut)
        out += " [...]";

    return out;
}

} // namespace sessionlog

} // namespace whynot
