#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace whynot
{

/** Registro de queda: quando o app cai, grava um minidump e um `.txt` com o porquê.

    Sem JUCE (compila sozinho nos testes). Só existe de verdade no Windows com o MSVC; fora dele,
    install() não faz nada e lastCrash() só lê.

    O que fica em `%APPDATA%\WHYNOT LAB\quedas\`:
      - `<AAAAMMDD-HHMMSS-pid>.txt`: versão do app, código da exceção, endereço, o módulo (DLL) onde o
        endereço cai, a pilha da thread que caiu e o "suspeito": o primeiro módulo da pilha que não é o
        app nem o Windows. É ele que aponta o plugin VST3.
      - `<AAAAMMDD-HHMMSS-pid>.dmp`: minidump pequeno (pilhas, threads, módulos), para abrir no Visual
        Studio ou no WinDbg.
      - `visto.txt`: a última queda que o app já contou a alguém (lastCrash).

    Nada é enviado para lugar nenhum (sem telemetria: docs/pesquisa.md 3.7, item 6).

    O que pega: exceção não tratada em qualquer thread (acesso inválido, estouro de pilha, exceção de C++
    sem catch...), parâmetro inválido da biblioteca C, chamada virtual pura, std::terminate e abort().
    O que não pega: `__fastfail` (proteção de pilha /GS, parte dos erros de heap), que o Windows encerra
    sem chamar ninguém, e o abort() de um plugin com a biblioteca C dele (o processo sai com código 3).
*/
struct CrashReport
{
    struct Options
    {
        std::filesystem::path folder;    // onde vão o .dmp e o .txt; a pasta de cima tem de poder ser criada
        std::string version;             // versão do app, para a primeira linha do .txt
        std::size_t keep = 5;            // quedas guardadas; as mais antigas saem em install()
    };

    /** Instala os filtros. Chame uma vez, o mais cedo possível, na thread principal.
        false = não instalou (fora do Windows, pasta impossível, caminho comprido demais). */
    static bool install (const Options&);

    static bool isInstalled() noexcept;

    /** O log desta sessão (common/Log.h): na queda, a última linha dele passa a dizer onde caiu. */
    static void setLogFile (const std::filesystem::path&);

    /** Reserva pilha para o filtro rodar depois de um estouro de pilha NESTA thread. install() já faz
        isso para a thread principal; as outras só precisam se tiverem recursão funda. */
    static void guardThisThreadStack() noexcept;

    /** `%APPDATA%\WHYNOT LAB\quedas` (vazio se não houver %APPDATA%). */
    static std::filesystem::path defaultFolder();

    //==========================================================================
    /** O que o `.txt` de uma queda diz. */
    struct LastCrash
    {
        bool found = false;
        std::string stem;          // "20261110-153012-1234"
        std::string when;          // "2026-11-10 15:30:12"
        std::string version;
        std::string kind;          // "excecao", "abort", "terminate", "chamada virtual pura", "parametro invalido"
        std::string code;          // "0xC0000005"
        std::string description;   // "acesso inválido à memória (leitura de 0x0000000000000000)"
        std::string module;        // "Plugin.vst3+0x1234", ou vazio se o endereço não é de módulo nenhum
        std::string modulePath;
        std::string suspect;       // "Plugin.vst3": primeiro módulo da pilha que não é o app nem o Windows
        std::string dump;          // nome do .dmp ao lado
        std::filesystem::path textFile;
        std::filesystem::path dumpFile;   // vazio se o dump não existe

        /** Nome do arquivo onde caiu, sem o deslocamento ("Plugin.vst3"). */
        std::string moduleName() const;

        /** Frase para a tela e para o log, sem ponto final:
            "caiu em Plugin.vst3 (acesso inválido à memória)" ou "caiu (estouro de pilha)". */
        std::string sentence() const;
    };

    /** A queda mais nova da pasta que ainda não foi vista (found == false: nenhuma).
        Com markSeen, anota em `visto.txt` que esta já foi contada: a próxima chamada não a devolve. */
    static LastCrash lastCrash (const std::filesystem::path& folder, bool markSeen = true);
};

//==============================================================================
/** As partes puras do registro: sem alocação, sem biblioteca C com estado, só contas e cópia de texto.
    O filtro de queda usa exatamente estas funções; os testes (tests/app/test_crash.cpp) também. */
namespace crashtext
{
    // Códigos próprios para as mortes que não são exceção do Windows ('WN' no meio).
    constexpr std::uint32_t codePureCall         = 0xE0574E01u;
    constexpr std::uint32_t codeTerminate        = 0xE0574E02u;
    constexpr std::uint32_t codeAbort            = 0xE0574E03u;
    constexpr std::uint32_t codeInvalidParameter = 0xC0000417u;   // STATUS_INVALID_CRUNTIME_PARAMETER

    constexpr int maxModules = 48;
    constexpr int maxFrames = 32;
    constexpr int maxPath = 520;

    struct ModuleRange
    {
        std::uint64_t base = 0;
        std::uint64_t size = 0;
        char path[maxPath] = {};   // UTF-8; pode vir no formato "\Device\HarddiskVolume3\..."
    };

    struct Facts
    {
        const char* version = "";
        int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
        std::uint32_t pid = 0;
        std::uint32_t threadId = 0;

        std::uint32_t code = 0;
        std::uint64_t address = 0;

        bool hasAccess = false;          // só para acesso inválido (0xC0000005) e erro de página (0xC0000006)
        int accessType = 0;              // 0 leitura, 1 escrita, 8 execução
        std::uint64_t accessAddress = 0;

        const ModuleRange* modules = nullptr;
        int moduleCount = 0;
        int faultModule = -1;            // índice em modules, ou -1
        int suspectModule = -1;

        const std::uint64_t* frames = nullptr;   // pilha da thread que caiu; frames[0] é onde caiu
        const int* frameModules = nullptr;       // módulo de cada quadro, ou -1
        int frameCount = 0;

        const char* dumpName = "";
    };

    /** Escreve o `.txt` em `out` (sempre terminado em zero). Devolve o tamanho, sem contar o zero. */
    std::size_t format (char* out, std::size_t capacity, const Facts&) noexcept;

    /** "20261110-153012-1234" em `out`. Devolve o tamanho. */
    std::size_t stem (char* out, std::size_t capacity, int year, int month, int day,
                      int hour, int minute, int second, std::uint32_t pid) noexcept;

    const char* describeCode (std::uint32_t code) noexcept;
    const char* kindOf (std::uint32_t code) noexcept;

    /** Índice do módulo que contém o endereço, ou -1. */
    int findModule (const ModuleRange* modules, int count, std::uint64_t address) noexcept;

    /** O nome do arquivo de um caminho (o que vem depois da última barra). */
    const char* fileNameOf (const char* path) noexcept;

    /** true se o caminho é de dentro da pasta do Windows (qualquer formato de caminho). */
    bool isSystemPath (const char* path) noexcept;

    /** true se os dois arquivos estão na mesma pasta (sem diferenciar maiúsculas). */
    bool sameFolder (const char* pathA, const char* pathB) noexcept;

    /** O primeiro módulo da pilha que não é o app (nem as DLLs ao lado dele) nem o Windows: índice em
        modules, ou -1. `exePath` é o caminho do executável no mesmo formato dos módulos. */
    int pickSuspect (const ModuleRange* modules, int moduleCount,
                     const int* frameModules, int frameCount, const char* exePath) noexcept;

    /** Lê de volta o texto de format(). `found` vem true se o texto tem pelo menos o código. */
    CrashReport::LastCrash parse (std::string_view text);
}

} // namespace whynot
