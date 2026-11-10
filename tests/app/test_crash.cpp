// Testes do registro de queda e do log de sessão (sem JUCE): o texto do .txt, a escolha dos arquivos
// antigos a apagar, o módulo de um endereço, o "suspeito" da pilha, o log em arquivo e, no Windows,
// processos filhos que caem de propósito (acesso inválido, estouro de pilha, abort...) para conferir
// que o .dmp e o .txt nascem.
//
//   test_crash                       roda tudo
//   test_crash --crash <tipo> <pasta>  (uso interno) instala o registro em <pasta>\quedas e cai

#include "../../src/app/CrashReport.h"
#include "../../src/common/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <crtdbg.h>
#endif

using namespace whynot;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static std::string readAll (const fs::path& file)
{
    std::ifstream in (file, std::ios::binary);
    return std::string (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
}

static bool contains (const std::string& text, const char* part)   { return text.find (part) != std::string::npos; }

static void touch (const fs::path& file, const char* text = "x")
{
    std::ofstream out (file, std::ios::binary);
    out << text;
}

static void setPath (char* dst, const char* text)   { std::snprintf (dst, crashtext::maxPath, "%s", text); }

// Um tipo com toStdString(), como o juce::String.
struct FakeJuceString
{
    std::string text;
    std::string toStdString() const   { return text; }
};

//==============================================================================
static void testPure()
{
    using namespace crashtext;

    std::printf ("Módulo de um endereço\n");
    {
        static ModuleRange modules[3];
        modules[0].base = 0x7FF600000000ull;  modules[0].size = 0x200000;
        setPath (modules[0].path, "\\Device\\HarddiskVolume3\\Program Files\\WHYNOT LAB\\WHYNOT LAB.exe");
        modules[1].base = 0x7FFA10000000ull;  modules[1].size = 0x1F0000;
        setPath (modules[1].path, "\\Device\\HarddiskVolume3\\Windows\\System32\\ntdll.dll");
        modules[2].base = 0x180000000ull;     modules[2].size = 0x50000;
        setPath (modules[2].path, "\\Device\\HarddiskVolume3\\Program Files\\Common Files\\VST3\\Ruim.vst3\\Contents\\x86_64-win\\Ruim.vst3");

        CHECK (findModule (modules, 3, 0x7FF600000000ull) == 0, "o primeiro byte do módulo é dele");
        CHECK (findModule (modules, 3, 0x7FF6001FFFFFull) == 0, "o último byte também");
        CHECK (findModule (modules, 3, 0x7FF600200000ull) == -1, "um byte depois do fim já não é");
        CHECK (findModule (modules, 3, 0x18004ABCDull) == 2, "endereço no meio do plugin");
        CHECK (findModule (modules, 3, 0) == -1 && findModule (modules, 3, ~0ull) == -1, "nulo e o topo da memória: nenhum módulo");
        CHECK (findModule (modules, 0, 0x18004ABCDull) == -1 && findModule (nullptr, 3, 1) == -1, "lista vazia ou nula: nenhum");

        CHECK (std::string (fileNameOf (modules[2].path)) == "Ruim.vst3", "nome do arquivo de um caminho de dispositivo");
        CHECK (std::string (fileNameOf ("C:/a/b.dll")) == "b.dll" && std::string (fileNameOf ("solto.dll")) == "solto.dll"
                 && std::string (fileNameOf (nullptr)).empty(), "nome com barra normal, sem pasta e nulo");

        CHECK (isSystemPath (modules[1].path) && isSystemPath ("C:\\WINDOWS\\system32\\kernel32.dll")
                 && isSystemPath ("c:/windows/SysWOW64/x.dll"), "pasta do Windows em três formatos");
        CHECK (! isSystemPath (modules[0].path) && ! isSystemPath (modules[2].path) && ! isSystemPath ("C:\\MeusWindows\\a.dll")
                 && ! isSystemPath (nullptr), "o app, o plugin e uma pasta de nome parecido não são do sistema");

        CHECK (sameFolder ("C:\\App\\WHYNOT LAB.exe", "c:\\app\\ajuda.dll") && sameFolder ("C:/App/a.exe", "C:\\App\\b.dll"),
               "mesma pasta, sem diferenciar maiúsculas nem o tipo de barra");
        CHECK (! sameFolder ("C:\\App\\a.exe", "C:\\App\\sub\\b.dll") && ! sameFolder ("a.exe", "b.dll") && ! sameFolder (nullptr, "b"),
               "subpasta, sem pasta e nulo: não");

        std::printf ("Suspeito da pilha\n");
        {
            const int stack[] = { 1, 1, -1, 0, 2, 0 };
            CHECK (pickSuspect (modules, 3, stack, 6, modules[0].path) == 2, "pula o Windows, o endereço solto e o app: sobra o plugin");
            CHECK (pickSuspect (modules, 3, stack, 4, modules[0].path) == -1, "pilha só com o Windows e o app: ninguém");
            const int first[] = { 2, 0, 1 };
            CHECK (pickSuspect (modules, 3, first, 3, modules[0].path) == 2, "plugin no topo da pilha");
            const int wrong[] = { 7, -3, 3 };
            CHECK (pickSuspect (modules, 3, wrong, 3, modules[0].path) == -1, "índices fora da lista são ignorados");
            CHECK (pickSuspect (modules, 3, stack, 6, "") == 0, "sem o caminho do app, o app conta como qualquer módulo");
        }

        std::printf ("Texto do registro\n");
        {
            const std::uint64_t frames[] = { 0x18004ABCDull, 0x7FF600001234ull, 0x1234 };
            const int frameModules[] = { 2, 0, -1 };

            Facts f;
            f.version = "0.9.1";
            f.year = 2026; f.month = 11; f.day = 10; f.hour = 9; f.minute = 5; f.second = 7;
            f.pid = 4321; f.threadId = 99;
            f.code = 0xC0000005u;
            f.address = 0x18004ABCDull;
            f.hasAccess = true; f.accessType = 1; f.accessAddress = 0x10;
            f.modules = modules; f.moduleCount = 3;
            f.faultModule = 2; f.suspectModule = 2;
            f.frames = frames; f.frameModules = frameModules; f.frameCount = 3;
            f.dumpName = "20261110-090507-4321.dmp";

            char text[4096];
            const auto size = format (text, sizeof (text), f);
            const std::string s (text, size);

            CHECK (size == std::strlen (text), "o tamanho devolvido é o do texto");
            CHECK (contains (s, "versao: 0.9.1\n") && contains (s, "quando: 2026-11-10 09:05:07\n") && contains (s, "pid: 4321\n"),
                   "versão, data com zeros à esquerda e pid");
            CHECK (contains (s, "tipo: excecao\n") && contains (s, "codigo: 0xC0000005\n"), "tipo e código em hexadecimal de 8 dígitos");
            CHECK (contains (s, "descricao: acesso inválido à memória (escrita em 0x0000000000000010)\n"), "o acesso que falhou");
            CHECK (contains (s, "endereco: 0x000000018004ABCD\n") && contains (s, "modulo: Ruim.vst3+0x4ABCD\n"), "endereço e módulo+deslocamento");
            CHECK (contains (s, "suspeito: Ruim.vst3\n") && contains (s, "dump: 20261110-090507-4321.dmp\n"), "suspeito e nome do dump");
            CHECK (contains (s, "  00 0x000000018004ABCD Ruim.vst3+0x4ABCD\n") && contains (s, "  01 0x00007FF600001234 WHYNOT LAB.exe+0x1234\n")
                     && contains (s, "  02 0x0000000000001234 (fora de qualquer módulo)\n"), "a pilha, quadro a quadro");

            const auto back = parse (s);
            CHECK (back.found && back.version == "0.9.1" && back.when == "2026-11-10 09:05:07" && back.kind == "excecao"
                     && back.code == "0xC0000005", "o texto lido de volta: versão, data, tipo e código");
            CHECK (back.module == "Ruim.vst3+0x4ABCD" && back.moduleName() == "Ruim.vst3" && back.suspect == "Ruim.vst3"
                     && back.dump == "20261110-090507-4321.dmp" && contains (back.modulePath, "VST3"), "módulo, suspeito e dump de volta");
            CHECK (back.sentence() == "caiu em Ruim.vst3 (acesso inválido à memória)", "frase: \"%s\"", back.sentence().c_str());

            // buffer pequeno: corta, mas termina em zero e não passa do fim
            char small[40];
            std::memset (small, 'Z', sizeof (small));
            const auto cut = format (small, 32, f);
            CHECK (cut == 31 && small[31] == 0 && small[32] == 'Z', "buffer pequeno: texto cortado, zero no fim, nada além");

            // endereço fora de módulo e sem suspeito
            f.faultModule = -1; f.suspectModule = -1; f.hasAccess = false; f.code = 0xC00000FDu; f.frameCount = 0;
            const auto size2 = format (text, sizeof (text), f);
            const auto lone = parse (std::string (text, size2));
            CHECK (lone.found && lone.module.empty() && lone.suspect.empty() && lone.sentence() == "caiu (estouro de pilha)",
                   "sem módulo: \"%s\"", lone.sentence().c_str());

            // caiu dentro do Windows sem plugin na pilha: a frase não culpa a ntdll
            f.faultModule = 1; f.code = 0xC0000374u;
            const auto size3 = format (text, sizeof (text), f);
            const auto sys = parse (std::string (text, size3));
            CHECK (sys.moduleName() == "ntdll.dll" && sys.sentence() == "caiu (heap corrompido)", "módulo do Windows: \"%s\"", sys.sentence().c_str());

            f.code = codeAbort;
            CHECK (std::string (kindOf (codeAbort)) == "abort" && std::string (kindOf (codePureCall)) == "chamada virtual pura"
                     && std::string (kindOf (codeTerminate)) == "terminate" && std::string (kindOf (codeInvalidParameter)) == "parametro invalido"
                     && std::string (kindOf (0xC0000005u)) == "excecao", "os tipos de morte");
            CHECK (std::string (describeCode (0x12345678u)) == "exceção do Windows", "código desconhecido tem descrição genérica");
            CHECK (! parse ("qualquer coisa\nsem: codigo\n").found && ! parse ("").found, "texto que não é registro: não achado");
        }
    }

    std::printf ("Nome de sessão e rodízio\n");
    {
        char text[64];
        const auto n = stem (text, sizeof (text), 2026, 1, 2, 3, 4, 5, 77);
        CHECK (std::string (text, n) == "20260102-030405-77", "nome sem alocação: %s", text);
        CHECK (sessionlog::sessionStem (2026, 1, 2, 3, 4, 5, 77) == text, "o do log é igual");
        CHECK (sessionlog::isSessionStem (text) && sessionlog::isSessionStem ("20261110-235959-123456"), "nomes válidos");
        CHECK (! sessionlog::isSessionStem ("visto") && ! sessionlog::isSessionStem ("20261110-235959-") && ! sessionlog::isSessionStem ("2026111x-235959-1")
                 && ! sessionlog::isSessionStem ("20261110_235959-1") && ! sessionlog::isSessionStem (""), "nomes inválidos");

        const std::vector<std::string> names { "20261101-100000-1.dmp", "20261101-100000-1.txt", "20261102-100000-2.dmp", "20261102-100000-2.txt",
                                               "20261103-100000-3.txt", "20261104-100000-4.dmp", "20261104-100000-4.txt", "visto.txt", "leia-me.txt" };
        const auto out = sessionlog::filesToPrune (names, 2);
        CHECK (out == (std::vector<std::string> { "20261101-100000-1.dmp", "20261101-100000-1.txt", "20261102-100000-2.dmp", "20261102-100000-2.txt" }),
               "ficam as 2 quedas mais novas; .dmp e .txt saem juntos; visto.txt e arquivo alheio ficam");
        CHECK (sessionlog::filesToPrune (names, 4).empty() && sessionlog::filesToPrune (names, 9).empty(), "até o limite, nada sai");
        CHECK (sessionlog::filesToPrune (names, 0).size() == 7, "limite zero: saem todas as quedas (7 arquivos)");
        CHECK (sessionlog::filesToPrune ({}, 3).empty(), "pasta vazia");
    }

    std::printf ("Linha do log\n");
    {
        CHECK (sessionlog::cleanLine ("a\nb\r\tc") == "a b  c", "quebras de linha e tabulação viram espaço");
        CHECK (sessionlog::cleanLine ("ação", 100) == "ação", "acentos passam");
        const auto cut = sessionlog::cleanLine ("aaaaç", 5);   // o 'ç' ocupa os bytes 4 e 5
        CHECK (cut == "aaaa [...]", "corte não parte um caractere UTF-8: \"%s\"", cut.c_str());
    }
}

//==============================================================================
static void testLogAndLastCrash (const fs::path& work)
{
    std::printf ("Log de sessão em arquivo\n");
    {
        const auto logs = work / "logs";
        fs::create_directories (logs);

        for (const char* old : { "20200101-000000-1.log", "20200102-000000-2.log", "20200103-000000-3.log", "anotacao.log" })
            touch (logs / old);

        whynot::log ("antes de abrir: some");
        CHECK (! sessionlog::isOpen() && sessionlog::currentFile().empty(), "fechado: log() não faz nada");
        CHECK (sessionlog::start (logs, 3), "abre o log");

        const auto file = sessionlog::currentFile();
        CHECK (sessionlog::isOpen() && file.extension() == ".log" && sessionlog::isSessionStem (file.stem().string()), "arquivo com nome de sessão");
        CHECK (! fs::exists (logs / "20200101-000000-1.log") && fs::exists (logs / "20200102-000000-2.log")
                 && fs::exists (logs / "20200103-000000-3.log") && fs::exists (logs / "anotacao.log"),
               "rodízio: ficam 3 sessões contando a nova; arquivo alheio fica");

        whynot::log ("Projeto aberto: C:\\Músicas\\Beat.wnl");
        whynot::log (FakeJuceString { "linha de um tipo com toStdString()" });
        whynot::log (std::string ("duas\nlinhas"));

        std::thread other ([] { for (int i = 0; i < 200; ++i) whynot::log ("outra thread"); });
        for (int i = 0; i < 200; ++i) whynot::log ("thread principal");
        other.join();

        auto text = readAll (file);   // lido com o log ainda aberto: cada linha já está no arquivo
        CHECK (contains (text, "Projeto aberto: C:\\Músicas\\Beat.wnl\n") && contains (text, "toStdString()\n") && contains (text, "duas linhas\n"),
               "as linhas estão no arquivo antes de fechar");
        CHECK (! contains (text, "antes de abrir"), "o que veio antes de abrir não entrou");
        CHECK (std::count (text.begin(), text.end(), '\n') == 403, "403 linhas inteiras com duas threads escrevendo");
        CHECK (text.size() > 13 && text[2] == ':' && text[5] == ':' && text[8] == '.' && text[13] == '[', "cada linha começa com a hora e a thread");

        // o teto de tamanho
        const std::string big (1900, 'x');
        for (int i = 0; i < 2400; ++i) whynot::log (big);
        whynot::log ("depois do teto");
        sessionlog::stop();
        whynot::log ("depois de fechar");

        text = readAll (file);
        CHECK (text.size() <= sessionlog::maxFileBytes + 200 && contains (text, "(log cheio") && ! contains (text, "depois do teto")
                 && ! contains (text, "depois de fechar"), "para de crescer em %u MB e avisa", (unsigned) (sessionlog::maxFileBytes >> 20));
        CHECK (! sessionlog::isOpen(), "fechado de novo");
    }

    std::printf ("Última queda\n");
    {
        const auto folder = work / "quedas-falsas";
        CHECK (! CrashReport::lastCrash (folder).found && ! fs::exists (folder), "pasta que não existe: nenhuma queda, e a pasta não é criada");

        fs::create_directories (folder);
        CHECK (! CrashReport::lastCrash (folder).found, "pasta vazia: nenhuma");

        touch (folder / "20261101-100000-1.txt", "WHYNOT LAB - registro de queda\nversao: 1\ncodigo: 0xC0000005\ndescricao: acesso inválido à memória\n"
                                                 "modulo: Velho.vst3+0x1\ncaminho: C:\\VST3\\Velho.vst3\ndump: 20261101-100000-1.dmp\npilha:\n");
        touch (folder / "20261105-100000-2.txt", "WHYNOT LAB - registro de queda\r\nversao: 2\r\ncodigo: 0xC00000FD\r\ndescricao: estouro de pilha\r\n"
                                                 "modulo: WHYNOT LAB.exe+0x10\r\ncaminho: C:\\App\\WHYNOT LAB.exe\r\nsuspeito: Novo.vst3\r\n"
                                                 "dump: 20261105-100000-2.dmp\r\npilha:\r\n  00 0x1 modulo: falso\r\n");
        touch (folder / "20261105-100000-2.dmp", "MDMP");
        touch (folder / "leia-me.txt", "codigo: 0xFFFFFFFF\n");

        const auto peek = CrashReport::lastCrash (folder, false);
        CHECK (peek.found && peek.stem == "20261105-100000-2" && peek.version == "2" && peek.suspect == "Novo.vst3"
                 && peek.moduleName() == "WHYNOT LAB.exe", "a mais nova, com fim de linha do Windows; o que vem depois de \"pilha:\" não conta");
        CHECK (peek.sentence() == "caiu em Novo.vst3 (estouro de pilha)", "frase com o suspeito: \"%s\"", peek.sentence().c_str());
        CHECK (peek.dumpFile == folder / "20261105-100000-2.dmp" && peek.textFile == folder / "20261105-100000-2.txt", "caminhos do .txt e do .dmp");
        CHECK (CrashReport::lastCrash (folder).found, "sem marcar, ela continua nova");
        CHECK (! CrashReport::lastCrash (folder).found && fs::exists (folder / "visto.txt"), "marcada como vista, não volta");

        touch (folder / "20261106-100000-3.txt", "lixo sem codigo\n");
        CHECK (! CrashReport::lastCrash (folder).found, "registro pela metade não conta");
        touch (folder / "20261107-100000-4.txt", "codigo: 0xE0574E03\ndescricao: abort\n");
        const auto next = CrashReport::lastCrash (folder);
        CHECK (next.found && next.dumpFile.empty() && next.sentence() == "caiu (abort)", "queda nova depois da vista, sem dump: \"%s\"", next.sentence().c_str());
    }
}

//==============================================================================
#if defined (_WIN32) && defined (_MSC_VER)

#pragma warning (push)
#pragma warning (disable: 4717 4702 4297)   // recursão sem fim, código inalcançável e throw em noexcept: de propósito

struct PureBase
{
    PureBase()                  { callIt (this); }
    virtual ~PureBase() = default;
    virtual void pure() = 0;
    static __declspec (noinline) void callIt (PureBase* p)   { p->pure(); }
};

struct PureDerived : PureBase   { void pure() override {} };

static __declspec (noinline) int recurse (volatile int depth)
{
    volatile char pad[512];
    pad[0] = (char) depth;
    return recurse (depth + 1) + pad[0];
}

static __declspec (noinline) void writeNull()
{
    volatile int* p = nullptr;
    *p = 42;
}

static void throwInNoexcept() noexcept   { throw std::runtime_error ("sem catch"); }

static int runChild (const std::string& kind, const fs::path& folder)
{
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    sessionlog::start (folder / "logs");
    whynot::log ("filho do teste: " + kind);

    if (! CrashReport::install ({ folder / "quedas", "9.9.9-teste", 5 }))
        return 90;

    if (fs::exists (folder / "quedas"))
        return 91;   // a pasta só nasce na queda

    CrashReport::setLogFile (sessionlog::currentFile());

    if (kind == "av")               writeNull();
    else if (kind == "av-thread")   { std::thread t (writeNull); t.join(); }
    else if (kind == "nullcall")    { using Fn = void (*)(); volatile Fn fn = nullptr; fn(); }
    else if (kind == "stack")       return recurse (0);
    else if (kind == "stack-thread") { std::thread t ([] { recurse (0); }); t.join(); }
    else if (kind == "abort")       std::abort();
    else if (kind == "terminate")   std::terminate();
    else if (kind == "noexcept-thread") { std::thread t ([] { throwInNoexcept(); }); t.join(); }
    else if (kind == "cpp")         throw std::runtime_error ("sem catch");
    else if (kind == "purecall")    { PureDerived d; }
    else if (kind == "invalid")     { volatile std::FILE* f = nullptr; std::fclose (const_cast<std::FILE*> (f)); }
    else if (kind == "none")        return 0;

    return 92;   // não caiu
}

#pragma warning (pop)

static unsigned long runSelf (const std::string& kind, const fs::path& folder)
{
    wchar_t exe[1024];
    GetModuleFileNameW (nullptr, exe, 1024);

    std::wstring command = L"\"" + std::wstring (exe) + L"\" --crash " + std::wstring (kind.begin(), kind.end()) + L" \"" + folder.wstring() + L"\"";

    STARTUPINFOW startup = {};
    startup.cb = sizeof (startup);
    PROCESS_INFORMATION process = {};

    if (! CreateProcessW (nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return 0xFFFFFFFFul;

    DWORD code = 0xFFFFFFFEul;

    if (WaitForSingleObject (process.hProcess, 60000) == WAIT_OBJECT_0)
        GetExitCodeProcess (process.hProcess, &code);
    else
        TerminateProcess (process.hProcess, 99);

    CloseHandle (process.hThread);
    CloseHandle (process.hProcess);
    return code;
}

static void testChildren (const fs::path& work)
{
    wchar_t exePath[1024];
    GetModuleFileNameW (nullptr, exePath, 1024);
    const auto exeName = fs::path (exePath).filename().string();

    struct Case
    {
        const char* kind;
        unsigned long exitCode;
        const char* code;
        const char* crashKind;
        bool moduleIsExe;      // o endereço da queda cai no próprio executável do teste
    };

    const Case cases[] = {
        { "av",              0xC0000005ul, "0xC0000005", "excecao",              true  },
        { "av-thread",       0xC0000005ul, "0xC0000005", "excecao",              true  },
        { "nullcall",        0xC0000005ul, "0xC0000005", "excecao",              false },
        { "stack",           0xC00000FDul, "0xC00000FD", "excecao",              true  },
        { "stack-thread",    0xC00000FDul, "0xC00000FD", "excecao",              true  },
        { "cpp",             0xE06D7363ul, "0xE06D7363", "excecao",              false },
        { "abort",           0xE0574E03ul, "0xE0574E03", "abort",                true  },
        { "terminate",       0xE0574E02ul, "0xE0574E02", "terminate",            true  },
        { "noexcept-thread", 0xE0574E03ul, "0xE0574E03", "abort",                true  },
        { "purecall",        0xE0574E01ul, "0xE0574E01", "chamada virtual pura", true  },
       #ifndef _DEBUG   // a biblioteca C de depuração para num assert antes do tratador de parâmetro inválido: caso só da versão normal
        { "invalid",         0xC0000417ul, "0xC0000417", "parametro invalido",   true  },
       #endif
    };

    for (const auto& c : cases)
    {
        std::printf ("Processo filho: %s\n", c.kind);

        const auto folder = work / c.kind;
        fs::create_directories (folder);

        const auto exitCode = runSelf (c.kind, folder);
        CHECK (exitCode == c.exitCode, "o processo sai com o código da queda (0x%08lX)", exitCode);

        const auto crash = CrashReport::lastCrash (folder / "quedas");
        CHECK (crash.found && crash.code == c.code && crash.kind == c.crashKind && crash.version == "9.9.9-teste",
               "o .txt nasceu: código %s, tipo \"%s\"", crash.code.c_str(), crash.kind.c_str());

        if (c.moduleIsExe)
            CHECK (crash.moduleName() == exeName, "módulo culpado: %s", crash.module.c_str());

        CHECK (crash.suspect.empty(), "sem plugin na pilha, sem suspeito (%s)", crash.suspect.c_str());

        const auto text = readAll (crash.textFile);
        CHECK (contains (text, ("  00 0x")) && contains (text, exeName.c_str()), "a pilha tem quadros e passa pelo executável do teste");

        const auto dump = readAll (crash.dumpFile);
        CHECK (! crash.dumpFile.empty() && dump.size() > 1000 && dump.compare (0, 4, "MDMP") == 0 && ! contains (text, "dump_falhou"),
               "o .dmp nasceu e é um minidump (%u KB)", (unsigned) (dump.size() / 1024));

        std::string log;
        std::error_code error;

        for (fs::directory_iterator it (folder / "logs", error), end; ! error && it != end; it.increment (error))
            log += readAll (it->path());

        CHECK (contains (log, "filho do teste") && contains (log, "QUEDA: ") && contains (log, crash.stem.c_str()),
               "a última linha do log da sessão diz que caiu e aponta o registro");

        if (std::string (c.kind) == "nullcall")
            CHECK (crash.module.empty() && contains (text, "(execução de 0x0000000000000000)") && crash.sentence() == "caiu (acesso inválido à memória)",
                   "chamada por ponteiro nulo: fora de qualquer módulo, e a pilha acha quem chamou");

        if (std::string (c.kind) == "av")
            CHECK (contains (text, "(escrita em 0x0000000000000000)") && crash.sentence() == "caiu em " + exeName + " (acesso inválido à memória)",
                   "frase: \"%s\"", crash.sentence().c_str());
    }

    std::printf ("Processo filho: sem queda\n");
    {
        const auto folder = work / "none";
        fs::create_directories (folder);
        CHECK (runSelf ("none", folder) == 0, "instalar o registro não muda a saída normal");
        CHECK (! fs::exists (folder / "quedas"), "sem queda, nem a pasta de quedas é criada");
    }

    std::printf ("Rodízio das quedas na instalação\n");
    {
        const auto folder = work / "rodizio";
        fs::create_directories (folder / "quedas");

        for (int i = 1; i <= 7; ++i)
        {
            const auto stem = sessionlog::sessionStem (2020, 1, i, 0, 0, 0, 1);
            touch (folder / "quedas" / (stem + ".txt"), "codigo: 0x1\n");
            touch (folder / "quedas" / (stem + ".dmp"));
        }

        touch (folder / "quedas" / "visto.txt", "20200107-000000-1\n");
        CHECK (runSelf ("none", folder) == 91, "o filho instalou (a pasta já existia)");

        int left = 0;
        for (const auto& entry : fs::directory_iterator (folder / "quedas"))
            left += entry.path().extension() == ".dmp" ? 1 : 0;

        CHECK (left == 5 && ! fs::exists (folder / "quedas" / "20200102-000000-1.txt") && fs::exists (folder / "quedas" / "20200103-000000-1.txt")
                 && fs::exists (folder / "quedas" / "visto.txt"), "ficam os 5 dumps mais novos, com os .txt deles, e o visto.txt");
    }

    CHECK (! CrashReport::defaultFolder().empty() && CrashReport::defaultFolder().filename() == "quedas", "pasta padrão em %%APPDATA%%");
}

#endif

// Nenhuma janela, nunca: nem a de "parou de funcionar", nem a do abort, nem a de assert da biblioteca C de
// depuração. Vale para o teste e para os filhos que caem de propósito (tudo vai para o stderr).
static void noDialogs()
{
   #if defined (_WIN32) && defined (_MSC_VER)
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    for (int type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        (void) type;   // na versão normal, as duas chamadas abaixo são macros vazias
        _CrtSetReportMode (type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile (type, _CRTDBG_FILE_STDERR);
    }
   #endif
}

//==============================================================================
int main (int argc, char** argv)
{
    noDialogs();

   #if defined (_WIN32) && defined (_MSC_VER)
    if (argc == 4 && std::string (argv[1]) == "--crash")
        return runChild (argv[2], fs::path (argv[3]));
   #else
    (void) argc; (void) argv;
   #endif

    std::error_code error;
    const auto work = fs::temp_directory_path() / ("whynot-crash-test-" + std::to_string ((unsigned long long) std::hash<std::thread::id>() (std::this_thread::get_id())));
    fs::remove_all (work, error);
    fs::create_directories (work);

    testPure();
    testLogAndLastCrash (work);

   #if defined (_WIN32) && defined (_MSC_VER)
    testChildren (work);
   #endif

    fs::remove_all (work, error);

    std::printf ("\n%s (%d falha%s)\n", failures == 0 ? "TUDO OK" : "FALHOU", failures, failures == 1 ? "" : "s");
    return failures;
}
