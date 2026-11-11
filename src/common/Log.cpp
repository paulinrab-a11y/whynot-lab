// Log de sessão (ver Log.h). Sem JUCE.

#include "Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>
#include <system_error>
#include <thread>

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

        std::thread writer;                // grava o que a ponte enfileira
        std::condition_variable wake;
        bool quit = false;
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

    //==========================================================================
    struct Stamp
    {
        int hour = 0, minute = 0, second = 0, millis = 0;
        unsigned long thread = 0;
    };

    // No Windows não aloca nem trava (o GetLocalTime só lê a hora que o sistema mantém): serve à thread de áudio.
    Stamp stampNow() noexcept
    {
       #ifdef _WIN32
        SYSTEMTIME t;
        GetLocalTime (&t);
        return { (int) t.wHour, (int) t.wMinute, (int) t.wSecond, (int) t.wMilliseconds, currentThread() };
       #else
        const auto n = now();
        return { n.local.tm_hour, n.local.tm_min, n.local.tm_sec, n.millis, currentThread() };
       #endif
    }

    constexpr std::size_t headBytes = 40;

    // "HH:MM:SS.mmm [thread] ", sem printf. `out` tem pelo menos headBytes. Devolve o tamanho.
    std::size_t writeHead (char* out, const Stamp& s) noexcept
    {
        char* p = out;
        auto two = [&p] (int v) { *p++ = (char) ('0' + (v / 10) % 10); *p++ = (char) ('0' + v % 10); };

        two (s.hour);    *p++ = ':';
        two (s.minute);  *p++ = ':';
        two (s.second);  *p++ = '.';
        *p++ = (char) ('0' + (s.millis / 100) % 10);
        two (s.millis % 100);
        *p++ = ' ';
        *p++ = '[';

        char digits[24];
        int n = 0;
        auto thread = s.thread;

        do
        {
            digits[n++] = (char) ('0' + (int) (thread % 10));
            thread /= 10;
        }
        while (thread != 0 && n < 20);

        while (n > 0)
            *p++ = digits[--n];

        *p++ = ']';
        *p++ = ' ';
        return (std::size_t) (p - out);
    }

    //==========================================================================
    /** A fila da ponte (logFromAnyThread): tamanho fixo, vários produtores, sem trava e sem alocação
        (fila limitada com um número de sequência por casa). Cheia, o produtor desiste na hora e a linha
        é contada em `dropped`. */
    constexpr std::size_t queueTextBytes = 480;

    struct Cell
    {
        std::atomic<std::size_t> sequence { 0 };
        Stamp stamp;
        std::size_t size = 0;
        bool cut = false;
        char text[queueTextBytes];
    };

    struct Queue
    {
        static constexpr std::size_t mask = sessionlog::queueCapacity - 1;

        Cell cells[sessionlog::queueCapacity];
        std::atomic<std::size_t> head { 0 };   // onde o próximo produtor escreve
        std::atomic<std::size_t> tail { 0 };   // de onde o próximo consumidor lê

        Queue() noexcept
        {
            for (std::size_t i = 0; i < sessionlog::queueCapacity; ++i)
                cells[i].sequence.store (i, std::memory_order_relaxed);
        }

        bool push (const char* text) noexcept
        {
            auto pos = head.load (std::memory_order_relaxed);
            Cell* cell = nullptr;

            for (;;)
            {
                cell = &cells[pos & mask];
                const auto diff = (std::ptrdiff_t) (cell->sequence.load (std::memory_order_acquire) - pos);

                if (diff == 0)
                {
                    if (head.compare_exchange_weak (pos, pos + 1, std::memory_order_relaxed))
                        break;
                }
                else if (diff < 0)
                    return false;   // cheia
                else
                    pos = head.load (std::memory_order_relaxed);
            }

            std::size_t n = 0;

            for (; n < queueTextBytes && text[n] != 0; ++n)
            {
                const auto c = static_cast<unsigned char> (text[n]);
                cell->text[n] = c < 0x20 || c == 0x7F ? ' ' : text[n];   // uma linha por registro
            }

            cell->cut = text[n] != 0;

            if (cell->cut)
                while (n > 0 && (static_cast<unsigned char> (text[n]) & 0xC0) == 0x80)   // não parte um caractere UTF-8
                    --n;

            cell->size = n;
            cell->stamp = stampNow();
            cell->sequence.store (pos + 1, std::memory_order_release);
            return true;
        }

        /** Tira a linha mais antiga, já formatada ("hora [thread] texto\n"), para `out`, que tem pelo menos
            sessionlog::queuedLineBytes. Devolve o tamanho; 0 = fila vazia. Sem trava e sem alocação. */
        std::size_t pop (char* out) noexcept
        {
            auto pos = tail.load (std::memory_order_relaxed);
            Cell* cell = nullptr;

            for (;;)
            {
                cell = &cells[pos & mask];
                const auto diff = (std::ptrdiff_t) (cell->sequence.load (std::memory_order_acquire) - (pos + 1));

                if (diff == 0)
                {
                    if (tail.compare_exchange_weak (pos, pos + 1, std::memory_order_relaxed))
                        break;
                }
                else if (diff < 0)
                    return 0;   // vazia
                else
                    pos = tail.load (std::memory_order_relaxed);
            }

            std::size_t n = writeHead (out, cell->stamp);
            std::memcpy (out + n, cell->text, cell->size);
            n += cell->size;

            if (cell->cut)
            {
                std::memcpy (out + n, " [...]", 6);
                n += 6;
            }

            out[n++] = '\n';
            cell->sequence.store (pos + mask + 1, std::memory_order_release);
            return n;
        }
    };

    Queue queue;
    std::atomic<unsigned> dropped { 0 };

    //==========================================================================
    // Com o mutex travado. `line` já vem com a hora e o '\n'.
    void writeRawLocked (State& s, const char* line, std::size_t size)
    {
        if (s.file == nullptr || s.full)
            return;

        if (s.written + size > sessionlog::maxFileBytes)
        {
            s.full = true;

            char full[headBytes + 80];
            std::size_t n = writeHead (full, stampNow());
            static const char note[] = "(log cheio: o resto desta sessão não foi registrado)\n";
            std::memcpy (full + n, note, sizeof (note) - 1);
            n += sizeof (note) - 1;
            std::fwrite (full, 1, n, s.file);
            s.written += n;
            return;
        }

        std::fwrite (line, 1, size, s.file);
        s.written += size;
    }

    // Com o mutex travado: passa para o arquivo o que a ponte enfileirou.
    void drainLocked (State& s)
    {
        char line[sessionlog::queuedLineBytes];
        bool any = false;

        // com teto: produtores sem parar não prendem quem está com o mutex
        for (std::size_t i = 0; i < 4 * sessionlog::queueCapacity; ++i)
        {
            const auto size = queue.pop (line);

            if (size == 0)
                break;

            writeRawLocked (s, line, size);
            any = true;
        }

        if (const auto lost = dropped.exchange (0, std::memory_order_relaxed); lost != 0)
        {
            std::size_t n = writeHead (line, stampNow());
            n += (std::size_t) std::snprintf (line + n, sizeof (line) - n, "%u linhas descartadas (a fila do log estava cheia)\n", lost);
            writeRawLocked (s, line, n);
            any = true;
        }

        if (any && s.file != nullptr)
            std::fflush (s.file);
    }

    // Com o mutex travado.
    void writeLocked (State& s, std::string_view text)
    {
        drainLocked (s);   // o que a ponte mandou antes desta linha sai antes dela

        char head[headBytes];
        const auto headSize = writeHead (head, stampNow());

        std::string line (head, headSize);
        line += sessionlog::cleanLine (text);
        line += '\n';

        writeRawLocked (s, line.data(), line.size());

        if (s.file != nullptr)
            std::fflush (s.file);   // a linha tem de estar no arquivo se o app cair logo depois
    }

    // A thread que grava o que a ponte enfileira: acorda de tempos em tempos, com prioridade baixa.
    void writerMain()
    {
        auto& s = state();
        std::unique_lock<std::mutex> lock (s.mutex);

        while (! s.quit)
        {
            s.wake.wait_for (lock, std::chrono::milliseconds (sessionlog::writerPeriodMs));
            drainLocked (s);
        }
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

void logFromAnyThread (const char* line) noexcept
{
    if (line == nullptr || ! opened.load (std::memory_order_acquire))
        return;

    if (! queue.push (line))
        dropped.fetch_add (1, std::memory_order_relaxed);
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
        s.quit = false;

        // sobra de uma sessão anterior neste processo não entra no arquivo novo
        for (char line[queuedLineBytes]; queue.pop (line) != 0;) {}
        dropped.store (0, std::memory_order_relaxed);

        opened.store (true, std::memory_order_release);

        try
        {
            s.writer = std::thread (writerMain);

           #ifdef _WIN32
            SetThreadPriority ((HANDLE) s.writer.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
           #endif
        }
        catch (...)
        {}   // sem a thread, a fila ainda sai a cada log() direto e no stop()

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
        std::thread writer;

        {
            const std::lock_guard<std::mutex> lock (s.mutex);
            opened.store (false, std::memory_order_release);   // daqui em diante ninguém mais enfileira
            s.quit = true;
            writer = std::move (s.writer);
        }

        s.wake.notify_all();

        if (writer.joinable())
            writer.join();

        const std::lock_guard<std::mutex> lock (s.mutex);
        drainLocked (s);   // o que ainda estava na fila

        if (s.file != nullptr)
            std::fclose (s.file);

        s.file = nullptr;
        s.path.clear();
    }
    catch (...)
    {}
}

void flushQueued() noexcept
{
    try
    {
        auto& s = state();
        const std::lock_guard<std::mutex> lock (s.mutex);
        drainLocked (s);
    }
    catch (...)
    {}
}

std::size_t popQueuedLine (char* out, std::size_t capacity) noexcept
{
    return out != nullptr && capacity >= queuedLineBytes ? queue.pop (out) : 0;
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
