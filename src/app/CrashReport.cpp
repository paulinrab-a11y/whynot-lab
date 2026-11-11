// Registro de queda (ver CrashReport.h). Sem JUCE: o <windows.h> fica isolado aqui.

#include "CrashReport.h"

#include "../common/Log.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>

#if defined (_WIN32) && defined (_MSC_VER)
 #define WHYNOT_CRASH_HANDLER 1

 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef PSAPI_VERSION
  #define PSAPI_VERSION 2   // GetMappedFileNameW direto do kernel32, sem a psapi.lib
 #endif
 #include <windows.h>
 #include <psapi.h>
 #include <dbghelp.h>       // só os tipos: a dbghelp.dll é carregada em install(), sem entrar no link

 #include <csignal>
 #include <cstdlib>
 #include <exception>
#else
 #define WHYNOT_CRASH_HANDLER 0
#endif

namespace whynot
{

//==============================================================================
// Partes puras
namespace crashtext
{

namespace
{
    // Escritor de texto num buffer fixo: nada de alocação nem de printf (roda dentro do filtro de queda).
    struct Out
    {
        char* data;
        std::size_t capacity;
        std::size_t size = 0;

        void ch (char c) noexcept
        {
            if (size + 1 < capacity)
                data[size++] = c;
        }

        void str (const char* text) noexcept
        {
            if (text != nullptr)
                while (*text != 0)
                    ch (*text++);
        }

        void dec (std::uint64_t value, int minDigits = 1) noexcept
        {
            char digits[24];
            int n = 0;

            do
            {
                digits[n++] = (char) ('0' + (int) (value % 10));
                value /= 10;
            }
            while (value != 0 && n < 24);

            for (int pad = minDigits - n; pad > 0; --pad)
                ch ('0');

            while (n > 0)
                ch (digits[--n]);
        }

        // sempre com "0x"; digits = 0 escreve só os dígitos necessários
        void hex (std::uint64_t value, int digits = 0) noexcept
        {
            static const char table[] = "0123456789ABCDEF";

            if (digits <= 0)
            {
                digits = 1;

                for (auto v = value >> 4; v != 0; v >>= 4)
                    ++digits;
            }

            str ("0x");

            for (int i = digits; --i >= 0;)
                ch (table[(value >> (4 * i)) & 0xF]);
        }

        void finish() noexcept
        {
            if (capacity > 0)
                data[size < capacity ? size : capacity - 1] = 0;
        }
    };

    char lower (char c) noexcept   { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; }
    bool isSlash (char c) noexcept { return c == '\\' || c == '/'; }

    void moduleAndOffset (Out& out, const Facts& f, int module, std::uint64_t address) noexcept
    {
        if (module < 0 || module >= f.moduleCount || f.modules == nullptr)
        {
            out.str ("(fora de qualquer módulo)");
            return;
        }

        out.str (fileNameOf (f.modules[module].path));
        out.ch ('+');
        out.hex (address - f.modules[module].base);
    }
}

const char* describeCode (std::uint32_t code) noexcept
{
    switch (code)
    {
        case 0xC0000005u:           return "acesso inválido à memória";
        case 0xC0000006u:           return "erro ao ler uma página do disco";
        case 0xC000001Du:           return "instrução inválida";
        case 0xC000008Cu:           return "índice fora dos limites";
        case 0xC000008Eu:           return "divisão de ponto flutuante por zero";
        case 0xC0000094u:           return "divisão inteira por zero";
        case 0xC0000096u:           return "instrução privilegiada";
        case 0xC00000FDu:           return "estouro de pilha";
        case 0xC0000135u:           return "DLL não encontrada";
        case 0xC0000374u:           return "heap corrompido";
        case 0xC0000409u:           return "proteção de pilha";
        case 0xC06D007Eu:           return "DLL de carga adiada não encontrada";
        case 0xE06D7363u:           return "exceção de C++ sem tratamento";
        case 0x80000003u:           return "ponto de parada";
        case codeInvalidParameter:  return "parâmetro inválido numa função da biblioteca C";
        case codePureCall:          return "chamada de função virtual pura";
        case codeTerminate:         return "std::terminate";
        case codeAbort:             return "abort";
        default:                    break;
    }

    return "exceção do Windows";
}

const char* kindOf (std::uint32_t code) noexcept
{
    switch (code)
    {
        case codeInvalidParameter:  return "parametro invalido";
        case codePureCall:          return "chamada virtual pura";
        case codeTerminate:         return "terminate";
        case codeAbort:             return "abort";
        default:                    break;
    }

    return "excecao";
}

int findModule (const ModuleRange* modules, int count, std::uint64_t address) noexcept
{
    if (modules == nullptr)
        return -1;

    for (int i = 0; i < count; ++i)
        if (modules[i].size != 0 && address >= modules[i].base && address - modules[i].base < modules[i].size)
            return i;

    return -1;
}

const char* fileNameOf (const char* path) noexcept
{
    if (path == nullptr)
        return "";

    const char* name = path;

    for (const char* p = path; *p != 0; ++p)
        if (isSlash (*p))
            name = p + 1;

    return name;
}

bool isSystemPath (const char* path) noexcept
{
    // "\Windows\" em qualquer ponto: C:\Windows\System32\..., \Device\HarddiskVolume3\Windows\WinSxS\...
    static const char needle[] = "\\windows\\";

    if (path == nullptr)
        return false;

    for (const char* p = path; *p != 0; ++p)
    {
        int i = 0;

        while (needle[i] != 0 && p[i] != 0 && lower (p[i] == '/' ? '\\' : p[i]) == needle[i])
            ++i;

        if (needle[i] == 0)
            return true;
    }

    return false;
}

bool sameFolder (const char* pathA, const char* pathB) noexcept
{
    if (pathA == nullptr || pathB == nullptr)
        return false;

    const auto folderA = (std::size_t) (fileNameOf (pathA) - pathA);
    const auto folderB = (std::size_t) (fileNameOf (pathB) - pathB);

    if (folderA != folderB || folderA == 0)
        return false;

    for (std::size_t i = 0; i < folderA; ++i)
        if (lower (pathA[i]) != lower (pathB[i]) && ! (isSlash (pathA[i]) && isSlash (pathB[i])))
            return false;

    return true;
}

int pickSuspect (const ModuleRange* modules, int moduleCount,
                 const int* frameModules, int frameCount, const char* exePath) noexcept
{
    if (modules == nullptr || frameModules == nullptr)
        return -1;

    for (int i = 0; i < frameCount; ++i)
    {
        const int m = frameModules[i];

        if (m < 0 || m >= moduleCount)
            continue;

        const char* path = modules[m].path;

        if (path[0] == 0 || isSystemPath (path))
            continue;

        if (exePath != nullptr && exePath[0] != 0 && sameFolder (path, exePath))
            continue;   // o próprio app, ou uma DLL que veio com ele

        return m;
    }

    return -1;
}

std::size_t stem (char* out, std::size_t capacity, int year, int month, int day,
                  int hour, int minute, int second, std::uint32_t pid) noexcept
{
    Out o { out, capacity };
    o.dec ((std::uint64_t) year, 4);
    o.dec ((std::uint64_t) month, 2);
    o.dec ((std::uint64_t) day, 2);
    o.ch ('-');
    o.dec ((std::uint64_t) hour, 2);
    o.dec ((std::uint64_t) minute, 2);
    o.dec ((std::uint64_t) second, 2);
    o.ch ('-');
    o.dec (pid);
    o.finish();
    return o.size;
}

std::size_t format (char* out, std::size_t capacity, const Facts& f) noexcept
{
    Out o { out, capacity };

    o.str ("WHYNOT LAB - registro de queda\n");
    o.str ("versao: ");     o.str (f.version);  o.ch ('\n');

    o.str ("quando: ");
    o.dec ((std::uint64_t) f.year, 4);   o.ch ('-');  o.dec ((std::uint64_t) f.month, 2);   o.ch ('-');  o.dec ((std::uint64_t) f.day, 2);
    o.ch (' ');
    o.dec ((std::uint64_t) f.hour, 2);   o.ch (':');  o.dec ((std::uint64_t) f.minute, 2);  o.ch (':');  o.dec ((std::uint64_t) f.second, 2);
    o.ch ('\n');

    o.str ("pid: ");        o.dec (f.pid);       o.ch ('\n');
    o.str ("thread: ");     o.dec (f.threadId);  o.ch ('\n');
    o.str ("tipo: ");       o.str (kindOf (f.code));  o.ch ('\n');
    o.str ("codigo: ");     o.hex (f.code, 8);   o.ch ('\n');

    o.str ("descricao: ");
    o.str (describeCode (f.code));

    if (f.hasAccess)
    {
        o.str (f.accessType == 1 ? " (escrita em " : f.accessType == 8 ? " (execução de " : " (leitura de ");
        o.hex (f.accessAddress, 16);
        o.ch (')');
    }

    o.ch ('\n');

    o.str ("endereco: ");   o.hex (f.address, 16);  o.ch ('\n');
    o.str ("modulo: ");     moduleAndOffset (o, f, f.faultModule, f.address);  o.ch ('\n');

    if (f.faultModule >= 0 && f.faultModule < f.moduleCount && f.modules != nullptr)
    {
        o.str ("caminho: ");
        o.str (f.modules[f.faultModule].path);
        o.ch ('\n');
    }

    if (f.suspectModule >= 0 && f.suspectModule < f.moduleCount && f.modules != nullptr)
    {
        o.str ("suspeito: ");
        o.str (fileNameOf (f.modules[f.suspectModule].path));
        o.ch ('\n');
        o.str ("caminho_suspeito: ");
        o.str (f.modules[f.suspectModule].path);
        o.ch ('\n');
    }

    o.str ("dump: ");       o.str (f.dumpName);  o.ch ('\n');

    // por último: o que vem depois de "pilha:" é só para gente ler
    o.str ("pilha:\n");

    for (int i = 0; i < f.frameCount && f.frames != nullptr; ++i)
    {
        o.str ("  ");
        o.dec ((std::uint64_t) i, 2);
        o.ch (' ');
        o.hex (f.frames[i], 16);
        o.ch (' ');
        moduleAndOffset (o, f, f.frameModules != nullptr ? f.frameModules[i] : -1, f.frames[i]);
        o.ch ('\n');
    }

    o.finish();
    return o.size;
}

CrashReport::LastCrash parse (std::string_view text)
{
    CrashReport::LastCrash crash;

    while (! text.empty())
    {
        const auto lineEnd = text.find ('\n');
        auto line = text.substr (0, lineEnd);
        text = lineEnd == std::string_view::npos ? std::string_view() : text.substr (lineEnd + 1);

        if (! line.empty() && line.back() == '\r')
            line.remove_suffix (1);

        if (line == "pilha:")
            break;

        const auto colon = line.find (": ");

        if (colon == std::string_view::npos)
            continue;

        const auto key = line.substr (0, colon);
        const std::string value (line.substr (colon + 2));

        if (key == "versao")            crash.version = value;
        else if (key == "quando")       crash.when = value;
        else if (key == "tipo")         crash.kind = value;
        else if (key == "codigo")       crash.code = value;
        else if (key == "descricao")    crash.description = value;
        else if (key == "modulo")       crash.module = ! value.empty() && value[0] == '(' ? std::string() : value;
        else if (key == "caminho")      crash.modulePath = value;
        else if (key == "suspeito")     crash.suspect = value;
        else if (key == "dump")         crash.dump = value;
    }

    crash.found = ! crash.code.empty();
    return crash;
}

} // namespace crashtext

//==============================================================================
std::string CrashReport::LastCrash::moduleName() const
{
    // "Plugin.vst3+0x1234": o nome pode ter '+', o deslocamento é sempre o último
    const auto plus = module.rfind ("+0x");
    return plus == std::string::npos ? module : module.substr (0, plus);
}

std::string CrashReport::LastCrash::sentence() const
{
    // o módulo do Windows onde o endereço caiu (ntdll, KERNELBASE) não diz nada a quem lê a tela: fica só no .txt
    std::string where = suspect;

    if (where.empty() && ! crashtext::isSystemPath (modulePath.c_str()))
        where = moduleName();

    // "acesso inválido à memória (leitura de 0x...)": a tela fica só com o começo
    auto what = description.substr (0, description.find (" ("));

    if (what.empty())
        what = code;

    std::string text = "caiu";

    if (! where.empty())
        text += " em " + where;

    if (! what.empty())
        text += " (" + what + ")";

    return text;
}

CrashReport::LastCrash CrashReport::lastCrash (const std::filesystem::path& folder, bool markSeen)
{
    try
    {
        std::error_code error;
        std::string newest;

        for (std::filesystem::directory_iterator it (folder, error), end; ! error && it != end; it.increment (error))
        {
            if (it->path().extension() != ".txt")
                continue;

            const auto name = it->path().stem().string();

            if (sessionlog::isSessionStem (name) && name > newest)
                newest = name;
        }

        if (newest.empty())
            return {};

        const auto seenFile = folder / "visto.txt";
        std::string seen;

        if (std::ifstream in (seenFile); in)
            in >> seen;

        if (newest <= seen)
            return {};

        const auto textFile = folder / (newest + ".txt");
        std::string text;

        if (std::ifstream in (textFile, std::ios::binary); in)
            text.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());

        auto crash = crashtext::parse (text);

        if (! crash.found)
            return {};   // arquivo pela metade: fica para a próxima vez não, mas também não é contado como visto

        crash.stem = newest;
        crash.textFile = textFile;

        if (const auto dump = folder / (newest + ".dmp"); std::filesystem::exists (dump, error))
            crash.dumpFile = dump;

        if (markSeen)
            if (std::ofstream out (seenFile, std::ios::trunc); out)
                out << newest << "\n";

        return crash;
    }
    catch (...)
    {
        return {};
    }
}

//==============================================================================
#if WHYNOT_CRASH_HANDLER

namespace
{
    using MiniDumpFn = BOOL (WINAPI*) (HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                       PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                       PMINIDUMP_CALLBACK_INFORMATION);

    constexpr std::size_t widePathSize = 700;
    constexpr DWORD writerTimeoutMs = 20000;

    // Tudo pré-alocado em install(): dentro do filtro não se aloca nada.
    struct Global
    {
        bool installed;

        wchar_t folder[widePathSize];      // sem a barra no fim
        wchar_t logPath[widePathSize];
        volatile LONG hasLog;
        char version[64];
        char exePath[crashtext::maxPath];

        MiniDumpFn miniDump;
        HANDLE requestEvent, doneEvent, writerThread;
        DWORD writerThreadId;

        volatile LONG busy;                // 1 = uma queda já está sendo registrada
        DWORD busyThread;
        EXCEPTION_POINTERS* pointers;

        // rascunho, só de quem registra a queda
        crashtext::ModuleRange modules[crashtext::maxModules];
        int moduleCount;
        std::uint64_t frames[crashtext::maxFrames];
        int frameModules[crashtext::maxFrames];
        CONTEXT walkContext;
        char stem[64];
        char dumpName[80];
        wchar_t textPath[widePathSize];
        wchar_t dumpPath[widePathSize];
        char text[16384];
        char logLine[1024];
        char queuedLine[sessionlog::queuedLineBytes];
    };

    Global g;   // estático: começa zerado

    //==========================================================================
    void copyWide (wchar_t* dst, std::size_t capacity, const wchar_t* a, const wchar_t* b, const char* c, const wchar_t* d) noexcept
    {
        std::size_t n = 0;

        for (; a != nullptr && *a != 0 && n + 1 < capacity; ++a)  dst[n++] = *a;
        for (; b != nullptr && *b != 0 && n + 1 < capacity; ++b)  dst[n++] = *b;
        for (; c != nullptr && *c != 0 && n + 1 < capacity; ++c)  dst[n++] = (wchar_t) (unsigned char) *c;
        for (; d != nullptr && *d != 0 && n + 1 < capacity; ++d)  dst[n++] = *d;

        dst[n] = 0;
    }

    void copyText (char* dst, std::size_t capacity, const char* a, const char* b = nullptr) noexcept
    {
        std::size_t n = 0;

        for (; a != nullptr && *a != 0 && n + 1 < capacity; ++a)  dst[n++] = *a;
        for (; b != nullptr && *b != 0 && n + 1 < capacity; ++b)  dst[n++] = *b;

        dst[n] = 0;
    }

    // O tamanho da imagem, lido do cabeçalho PE que está mapeado em `base`.
    std::uint64_t imageSizeAt (const void* base) noexcept
    {
        __try
        {
            const auto* dos = static_cast<const IMAGE_DOS_HEADER*> (base);

            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x10000)
                return 0;

            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*> (static_cast<const char*> (base) + dos->e_lfanew);
            return nt->Signature == IMAGE_NT_SIGNATURE ? (std::uint64_t) nt->OptionalHeader.SizeOfImage : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    /** O módulo (DLL, exe, .vst3) onde o endereço cai: índice em g.modules, ou -1.
        VirtualQuery + GetMappedFileName: nenhum dos dois pega a trava do carregador de DLLs, que pode
        estar presa pela thread que caiu (queda dentro do DllMain de um plugin). */
    int moduleFor (std::uint64_t address) noexcept
    {
        if (const int known = crashtext::findModule (g.modules, g.moduleCount, address); known >= 0)
            return known;

        MEMORY_BASIC_INFORMATION info;

        if (g.moduleCount >= crashtext::maxModules
            || VirtualQuery (reinterpret_cast<const void*> ((std::uintptr_t) address), &info, sizeof (info)) == 0
            || info.Type != MEM_IMAGE || info.AllocationBase == nullptr)
            return -1;

        auto& m = g.modules[g.moduleCount];
        m.base = (std::uint64_t) reinterpret_cast<std::uintptr_t> (info.AllocationBase);
        m.size = imageSizeAt (info.AllocationBase);

        if (m.size == 0 || address - m.base >= m.size)   // cabeçalho ilegível: pelo menos a região do endereço
            m.size = (std::uint64_t) (reinterpret_cast<std::uintptr_t> (info.BaseAddress) - reinterpret_cast<std::uintptr_t> (info.AllocationBase))
                       + (std::uint64_t) info.RegionSize;

        wchar_t wide[crashtext::maxPath];
        const DWORD length = GetMappedFileNameW (GetCurrentProcess(), info.AllocationBase, wide, crashtext::maxPath - 1);
        wide[length < (DWORD) crashtext::maxPath ? length : 0] = 0;

        const int bytes = length > 0 ? WideCharToMultiByte (CP_UTF8, 0, wide, (int) length, m.path, crashtext::maxPath - 1, nullptr, nullptr) : 0;
        m.path[bytes > 0 ? bytes : 0] = 0;

        if (bytes <= 0)
            copyText (m.path, crashtext::maxPath, "(sem nome)");

        return g.moduleCount++;
    }

    /** A pilha da thread que caiu, a partir do contexto da exceção. Devolve o número de quadros. */
    int walkStack (const CONTEXT* context, std::uint64_t fallbackAddress) noexcept
    {
        int count = 0;

       #if defined (_M_X64)
        if (context != nullptr)
        {
            __try
            {
                g.walkContext = *context;
                auto& c = g.walkContext;

                while (count < crashtext::maxFrames)
                {
                    g.frames[count++] = c.Rip;

                    DWORD64 imageBase = 0;

                    if (auto* function = RtlLookupFunctionEntry (c.Rip, &imageBase, nullptr))
                    {
                        void* handlerData = nullptr;
                        DWORD64 establisher = 0;
                        RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, c.Rip, function, &c, &handlerData, &establisher, nullptr);
                    }
                    else
                    {
                        // função folha, ou salto para um endereço que não é código (ponteiro nulo): o retorno está no topo
                        c.Rip = *reinterpret_cast<const DWORD64*> (c.Rsp);
                        c.Rsp += 8;
                    }

                    if (c.Rip < 0x10000)
                        break;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {}
        }
       #else
        (void) context;
       #endif

        if (count == 0)
            g.frames[count++] = fallbackAddress;

        return count;
    }

    bool writeWholeFile (const wchar_t* path, const char* data, std::size_t size, DWORD disposition, DWORD access) noexcept
    {
        const HANDLE file = CreateFileW (path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                         disposition, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return false;

        DWORD written = 0;
        const bool ok = WriteFile (file, data, (DWORD) size, &written, nullptr) != 0 && written == (DWORD) size;
        FlushFileBuffers (file);
        CloseHandle (file);
        return ok;
    }

    bool writeDump (EXCEPTION_POINTERS* pointers, DWORD threadId) noexcept
    {
        if (g.miniDump == nullptr)
            return false;

        const HANDLE file = CreateFileW (g.dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return false;

        MINIDUMP_EXCEPTION_INFORMATION info;
        info.ThreadId = threadId;
        info.ExceptionPointers = pointers;
        info.ClientPointers = FALSE;

        // pequeno: pilhas, threads, módulos (inclusive os já descarregados) e a memória para onde as pilhas apontam
        const auto type = (MINIDUMP_TYPE) (MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules
                                           | MiniDumpWithIndirectlyReferencedMemory);

        const bool ok = g.miniDump (GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, nullptr, nullptr) != 0;
        FlushFileBuffers (file);
        CloseHandle (file);

        if (! ok)
            DeleteFileW (g.dumpPath);

        return ok;
    }

    void appendLogLine (const crashtext::Facts& f, const SYSTEMTIME& t) noexcept
    {
        if (InterlockedCompareExchange (&g.hasLog, 1, 1) == 0)
            return;

        // mesmo formato das linhas do common/Log.cpp
        char head[64];
        char* p = head;
        auto two = [&p] (unsigned v) { *p++ = (char) ('0' + (v / 10) % 10); *p++ = (char) ('0' + v % 10); };
        two (t.wHour); *p++ = ':'; two (t.wMinute); *p++ = ':'; two (t.wSecond); *p++ = '.';
        *p++ = (char) ('0' + (t.wMilliseconds / 100) % 10); two (t.wMilliseconds % 100);
        *p = 0;

        copyText (g.logLine, sizeof (g.logLine), head, " [queda] QUEDA: ");
        auto add = [] (const char* text) { copyText (g.logLine + strlen (g.logLine), sizeof (g.logLine) - strlen (g.logLine), text); };

        add (crashtext::describeCode (f.code));

        if (f.faultModule >= 0)
        {
            add (" em ");
            add (crashtext::fileNameOf (f.modules[f.faultModule].path));
        }

        if (f.suspectModule >= 0)
        {
            add ("; suspeito: ");
            add (crashtext::fileNameOf (f.modules[f.suspectModule].path));
        }

        add ("; registro: ");
        add (g.stem);
        add (".txt\n");

        const HANDLE file = CreateFileW (g.logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return;

        DWORD written = 0;

        // Primeiro o que a ponte do juce::Logger deixou na fila (as últimas falas do motor antes da queda).
        // Tirar da fila não trava nem aloca; o teto evita ficar preso se outra thread não parar de enfileirar.
        for (std::size_t i = 0; i < 2 * sessionlog::queueCapacity; ++i)
        {
            const auto size = sessionlog::popQueuedLine (g.queuedLine, sizeof (g.queuedLine));

            if (size == 0)
                break;

            WriteFile (file, g.queuedLine, (DWORD) size, &written, nullptr);
        }

        WriteFile (file, g.logLine, (DWORD) strlen (g.logLine), &written, nullptr);
        FlushFileBuffers (file);
        CloseHandle (file);
    }

    /** Grava o .txt e o .dmp. Roda na thread do registro (ou, se ela não existir, na que caiu). */
    void writeCrash (EXCEPTION_POINTERS* pointers, DWORD threadId) noexcept
    {
        if (pointers == nullptr || pointers->ExceptionRecord == nullptr)
            return;

        const auto& record = *pointers->ExceptionRecord;

        SYSTEMTIME now;
        GetLocalTime (&now);

        crashtext::stem (g.stem, sizeof (g.stem), now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                         (std::uint32_t) GetCurrentProcessId());
        copyText (g.dumpName, sizeof (g.dumpName), g.stem, ".dmp");
        copyWide (g.textPath, widePathSize, g.folder, L"\\", g.stem, L".txt");
        copyWide (g.dumpPath, widePathSize, g.folder, L"\\", g.stem, L".dmp");

        CreateDirectoryW (g.folder, nullptr);   // a pasta de cima já existe desde install()

        crashtext::Facts f;
        f.version = g.version;
        f.year = now.wYear;  f.month = now.wMonth;    f.day = now.wDay;
        f.hour = now.wHour;  f.minute = now.wMinute;  f.second = now.wSecond;
        f.pid = (std::uint32_t) GetCurrentProcessId();
        f.threadId = (std::uint32_t) threadId;
        f.code = (std::uint32_t) record.ExceptionCode;
        f.address = (std::uint64_t) reinterpret_cast<std::uintptr_t> (record.ExceptionAddress);

        if ((f.code == 0xC0000005u || f.code == 0xC0000006u) && record.NumberParameters >= 2)
        {
            f.hasAccess = true;
            f.accessType = (int) record.ExceptionInformation[0];
            f.accessAddress = (std::uint64_t) record.ExceptionInformation[1];
        }

        f.faultModule = moduleFor (f.address);
        f.frameCount = walkStack (pointers->ContextRecord, f.address);

        for (int i = 0; i < f.frameCount; ++i)
            g.frameModules[i] = moduleFor (g.frames[i]);

        f.modules = g.modules;
        f.moduleCount = g.moduleCount;
        f.frames = g.frames;
        f.frameModules = g.frameModules;
        f.suspectModule = crashtext::pickSuspect (g.modules, g.moduleCount, g.frameModules, f.frameCount, g.exePath);
        f.dumpName = g.dumpName;

        // o texto primeiro: é barato e é o que diz o módulo; o dump pode demorar ou falhar
        const auto size = crashtext::format (g.text, sizeof (g.text), f);
        writeWholeFile (g.textPath, g.text, size, CREATE_ALWAYS, GENERIC_WRITE);
        appendLogLine (f, now);

        if (! writeDump (pointers, threadId))
        {
            static const char note[] = "dump_falhou: o minidump nao foi gravado\n";
            writeWholeFile (g.textPath, note, sizeof (note) - 1, OPEN_EXISTING, FILE_APPEND_DATA);
        }
    }

    void writeCrashGuarded (EXCEPTION_POINTERS* pointers, DWORD threadId) noexcept
    {
        __try
        {
            writeCrash (pointers, threadId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {}
    }

    DWORD WINAPI writerMain (void*)
    {
        // Thread à parte, criada em install(): tem pilha própria (a da thread que caiu pode ter estourado)
        // e é a forma recomendada de chamar o MiniDumpWriteDump de dentro do processo.
        WaitForSingleObject (g.requestEvent, INFINITE);
        writeCrashGuarded (g.pointers, g.busyThread);
        SetEvent (g.doneEvent);
        return 0;
    }

    /** O caminho comum de toda queda. Volta depois de registrar; quem chamou encerra o processo. */
    void handleCrash (EXCEPTION_POINTERS* pointers) noexcept
    {
        const DWORD thread = GetCurrentThreadId();

        if (thread == g.writerThreadId)
            return;   // o próprio registro caiu: nada mais a fazer

        if (InterlockedExchange (&g.busy, 1) != 0)
        {
            if (thread == g.busyThread)
                return;   // caiu de novo dentro do filtro

            Sleep (INFINITE);   // outra thread já está registrando a queda dela; o processo acaba em seguida
        }

        g.busyThread = thread;
        g.pointers = pointers;

        if (g.writerThread != nullptr && g.requestEvent != nullptr && g.doneEvent != nullptr)
        {
            SetEvent (g.requestEvent);
            const HANDLE waitFor[2] = { g.doneEvent, g.writerThread };
            WaitForMultipleObjects (2, waitFor, FALSE, writerTimeoutMs);   // com prazo: o processo tem de morrer de qualquer jeito
        }
        else
        {
            writeCrashGuarded (pointers, thread);
        }
    }

    LONG WINAPI unhandledFilter (EXCEPTION_POINTERS* pointers)
    {
        handleCrash (pointers);

        // Encerra aqui mesmo, com o código da exceção (é o que o processo pai lê). Sem a janela do Windows
        // de "parou de funcionar" e sem rodar destrutores com o programa num estado inválido.
        TerminateProcess (GetCurrentProcess(), pointers != nullptr && pointers->ExceptionRecord != nullptr
                                                 ? (UINT) pointers->ExceptionRecord->ExceptionCode : 1u);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    /** As mortes que não passam pelo filtro de exceção (abort, terminate, virtual pura, parâmetro inválido):
        monta uma "exceção" com o contexto daqui e segue pelo mesmo caminho. */
    __declspec (noinline) void dieWith (DWORD code) noexcept
    {
        CONTEXT context;
        RtlCaptureContext (&context);

        EXCEPTION_RECORD record = {};
        record.ExceptionCode = code;
        record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
       #if defined (_M_X64)
        record.ExceptionAddress = reinterpret_cast<void*> (context.Rip);
       #else
        record.ExceptionAddress = nullptr;
       #endif

        EXCEPTION_POINTERS pointers = { &record, &context };
        handleCrash (&pointers);
        TerminateProcess (GetCurrentProcess(), (UINT) code);
    }

    void __cdecl onInvalidParameter (const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
    {
        dieWith (crashtext::codeInvalidParameter);
    }

    void __cdecl onPureCall()       { dieWith (crashtext::codePureCall); }
    void __cdecl onTerminate()      { dieWith (crashtext::codeTerminate); }
    void __cdecl onAbort (int)      { dieWith (crashtext::codeAbort); }

    void pruneOld (const std::filesystem::path& folder, std::size_t keep)
    {
        std::error_code error;
        std::vector<std::string> names;

        for (std::filesystem::directory_iterator it (folder, error), end; ! error && it != end; it.increment (error))
            if (it->path().extension() == ".dmp" || it->path().extension() == ".txt")
                names.push_back (it->path().filename().string());

        for (const auto& name : sessionlog::filesToPrune (names, keep))
            std::filesystem::remove (folder / name, error);
    }
}

bool CrashReport::install (const Options& options)
{
    if (g.installed)
        return true;

    try
    {
        if (options.folder.empty())
            return false;

        std::error_code error;
        const auto folder = std::filesystem::absolute (options.folder, error);

        if (error)
            return false;

        // só a pasta de cima: a de quedas nasce na primeira queda (o autoteste não deixa pasta vazia para trás)
        std::filesystem::create_directories (folder.parent_path(), error);
        pruneOld (folder, options.keep);

        const auto wide = folder.wstring();

        if (wide.size() + 80 >= widePathSize)
            return false;

        copyWide (g.folder, widePathSize, wide.c_str(), nullptr, nullptr, nullptr);
        copyText (g.version, sizeof (g.version), options.version.c_str());
    }
    catch (...)
    {
        return false;
    }

    // o executável entra primeiro na lista de módulos: é a referência do "suspeito"
    g.moduleCount = 0;

    if (const int exe = moduleFor ((std::uint64_t) reinterpret_cast<std::uintptr_t> (GetModuleHandleW (nullptr))); exe >= 0)
        copyText (g.exePath, sizeof (g.exePath), g.modules[exe].path);

    // A dbghelp do Windows, carregada agora (na queda não se carrega DLL) e só da pasta do sistema.
    // Sem ela, o .txt sai do mesmo jeito; só o .dmp fica faltando.
    if (const HMODULE dbghelp = LoadLibraryExW (L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
        g.miniDump = reinterpret_cast<MiniDumpFn> (reinterpret_cast<void*> (GetProcAddress (dbghelp, "MiniDumpWriteDump")));

    LoadLibraryExW (L"dbgcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);   // a dbghelp repassa o dump para ela

    g.requestEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
    g.doneEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);

    if (g.requestEvent != nullptr && g.doneEvent != nullptr)
        g.writerThread = CreateThread (nullptr, 256 * 1024, writerMain, nullptr, 0, &g.writerThreadId);

    guardThisThreadStack();

    SetUnhandledExceptionFilter (unhandledFilter);
    _set_invalid_parameter_handler (onInvalidParameter);
    _set_purecall_handler (onPureCall);
    std::set_terminate (onTerminate);   // vale para esta thread; nas outras, o terminate padrão chama abort()
    signal (SIGABRT, onAbort);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);   // sem janela de erro: o registro é o nosso

    g.installed = true;
    return true;
}

bool CrashReport::isInstalled() noexcept
{
    return g.installed;
}

void CrashReport::setLogFile (const std::filesystem::path& file)
{
    try
    {
        const auto wide = file.wstring();

        if (wide.empty() || wide.size() + 1 >= widePathSize)
            return;

        InterlockedExchange (&g.hasLog, 0);
        copyWide (g.logPath, widePathSize, wide.c_str(), nullptr, nullptr, nullptr);
        InterlockedExchange (&g.hasLog, 1);
    }
    catch (...)
    {}
}

void CrashReport::guardThisThreadStack() noexcept
{
    ULONG bytes = 64 * 1024;
    SetThreadStackGuarantee (&bytes);
}

std::filesystem::path CrashReport::defaultFolder()
{
    wchar_t appData[widePathSize];
    const DWORD length = GetEnvironmentVariableW (L"APPDATA", appData, (DWORD) widePathSize);

    if (length == 0 || length >= widePathSize)
        return {};

    return std::filesystem::path (appData) / "WHYNOT LAB" / "quedas";
}

#else // sem o filtro: fora do Windows/MSVC nada é instalado

bool CrashReport::install (const Options&)                       { return false; }
bool CrashReport::isInstalled() noexcept                         { return false; }
void CrashReport::setLogFile (const std::filesystem::path&)      {}
void CrashReport::guardThisThreadStack() noexcept                {}
std::filesystem::path CrashReport::defaultFolder()               { return {}; }

#endif

} // namespace whynot
