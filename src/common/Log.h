#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/** Log de sessão da WHYNOT LAB: um arquivo de texto por abertura do app, em `%APPDATA%\WHYNOT LAB\logs\`.

    Sem JUCE (compila sozinho nos testes). Nada sai da máquina: é um arquivo local, para o produtor
    ou quem der suporte entender o que o app estava fazendo antes de um problema.

    Uso, de qualquer camada:

        #include "../common/Log.h"
        whynot::log ("Projeto aberto: " + arquivo.getFullPathName());   // juce::String ou texto UTF-8

    Regras:
      - barato quando o log está fechado (autoteste, --render, capturas): um teste atômico e volta;
      - seguro em qualquer thread, MENOS as de áudio e de MIDI: trava um mutex, aloca e grava em arquivo;
      - uma linha por fato ("Projeto salvo: ...", "Plugin não abriu: ... (motivo)"), sem repetir a cada
        quadro: o arquivo para de crescer em `maxFileBytes`.
*/
namespace whynot
{

/** Acrescenta uma linha (UTF-8) ao log da sessão, com a hora e a thread. Sem log aberto, não faz nada. */
void log (std::string_view line) noexcept;

/** O mesmo para juce::String (ou qualquer tipo com toStdString()), sem este arquivo depender do JUCE. */
template <typename Text>
auto log (const Text& text) noexcept -> decltype (text.toStdString(), void())
{
    try
    {
        const std::string utf8 = text.toStdString();
        log (std::string_view (utf8));
    }
    catch (...)
    {}
}

/** A entrada da ponte do juce::Logger (Main.cpp): o JUCE e o Tracktion podem chamar o Logger de qualquer
    thread, inclusive a de áudio e a de MIDI. Esta função NÃO trava, NÃO aloca e NÃO toca em arquivo: copia
    a linha (UTF-8, terminada em zero; o que passar de ~480 bytes é cortado) para uma fila de tamanho fixo.
    Uma thread de prioridade baixa grava a fila no arquivo a cada `sessionlog::writerPeriodMs`. Fila cheia =
    a linha é descartada e contada; o arquivo ganha depois "N linhas descartadas (a fila do log estava cheia)".
    As linhas saem com a hora em que foram enfileiradas, e antes de qualquer log() direto que venha depois. */
void logFromAnyThread (const char* line) noexcept;

namespace sessionlog
{
    constexpr std::size_t queueCapacity = 256;      // linhas esperando a gravação (potência de 2)
    constexpr std::size_t queuedLineBytes = 560;    // uma linha da fila já formatada cabe aqui
    constexpr int writerPeriodMs = 200;

    /** Grava agora o que está na fila da ponte (o stop() também faz isso). */
    void flushQueued() noexcept;

    /** Para o registro de queda: tira da fila a linha mais antiga, já formatada e terminada em '\n', sem
        travar nem alocar. `capacity` tem de ser pelo menos queuedLineBytes. Devolve o tamanho; 0 = vazia. */
    std::size_t popQueuedLine (char* out, std::size_t capacity) noexcept;

    constexpr std::size_t maxFileBytes = 4u * 1024u * 1024u;   // depois disso o log da sessão para de crescer
    constexpr std::size_t maxLineBytes = 2000;
    constexpr std::size_t defaultKeep = 10;                     // arquivos de sessão guardados (rodízio)

    /** Abre `<pasta>/<AAAAMMDD-HHMMSS-pid>.log` (cria a pasta) e apaga os mais antigos, deixando `keep`
        contando com o novo. false = não conseguiu abrir (o app segue sem log). */
    bool start (const std::filesystem::path& folder, std::size_t keep = defaultKeep);

    /** Fecha o arquivo. Chamar log() depois disso não faz nada. */
    void stop() noexcept;

    bool isOpen() noexcept;

    /** O arquivo desta sessão (vazio com o log fechado). */
    std::filesystem::path currentFile();

    //==========================================================================
    // Partes puras (testadas em tests/app/test_crash.cpp)

    /** "20261110-153012-1234": o nome, sem extensão, dos arquivos de uma sessão (log, dump, registro de queda).
        Ordem alfabética = ordem no tempo. */
    std::string sessionStem (int year, int month, int day, int hour, int minute, int second, unsigned long pid);

    /** true para um nome no formato de sessionStem. */
    bool isSessionStem (std::string_view stem) noexcept;

    /** Dos nomes de arquivo dados (com extensão), os que devem sair para ficarem só as `keep` sessões mais
        novas. Arquivos da mesma sessão (mesmo nome, extensões diferentes) ficam ou saem juntos; nome fora do
        formato de sessionStem nunca sai. */
    std::vector<std::string> filesToPrune (const std::vector<std::string>& names, std::size_t keep);

    /** A linha como vai para o arquivo: sem quebras de linha nem caracteres de controle, cortada em
        `maxBytes` sem partir um caractere UTF-8 no meio. */
    std::string cleanLine (std::string_view text, std::size_t maxBytes = maxLineBytes);
}

} // namespace whynot
