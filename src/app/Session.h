#pragma once

#include <JuceHeader.h>

#include "CrashReport.h"

namespace whynot
{

/** Marca de sessão para achar autosaves depois de uma queda.

    Ao abrir, o app grava `sessoes/sessao-<pid>.txt` na pasta de dados; ao fechar direito, apaga.
    Uma marca cujo processo já não existe é de um app que caiu (ou foi morto): se ela aponta
    para um autosave que ainda existe, ele é oferecido para recuperar.
*/
class Session
{
public:
    struct Recovery
    {
        juce::File autosave;   // o autosave mais novo da sessão que caiu
        juce::File original;   // o .wnl de onde ele veio (vazio = projeto nunca salvo)
        juce::Time when;

        /** Onde a sessão anterior caiu, pelo registro de queda (CrashReport): "caiu em Plugin.vst3 (acesso
            inválido à memória)", sem ponto final. Vazio = não há registro novo. Vem preenchido mesmo sem
            autosave a recuperar (autosave == File()). */
        juce::String crash;
    };

    explicit Session (const juce::File& dataFolder);
    ~Session();

    /** O que uma sessão anterior deixou para trás (autosave == File() = nada). Lê e limpa. */
    Recovery takeRecovery()   { return std::exchange (recovery, {}); }

    /** O registro da queda mais nova que ainda não tinha sido vista (found == false = nenhuma): caminho do
        .txt e do .dmp, módulo, suspeito. Lido uma vez, ao abrir; não é limpo pelo takeRecovery. */
    const CrashReport::LastCrash& lastCrash() const noexcept   { return crashRecord; }

    /** Anota o último autosave desta sessão na marca. */
    void noteAutosave (const juce::File& autosave, const juce::File& original);

    /** true se há um processo vivo com esse PID. */
    static bool processAlive (int pid);

    /** true se o processo com esse PID está vivo e é este mesmo programa (WHYNOT LAB.exe). Depois de
        reiniciar o Windows, o PID de uma sessão que caiu pode ter ido para outro programa. */
    static bool isWhynotProcess (int pid);

    static int currentPid();

private:
    juce::File marker;
    Recovery recovery;
    CrashReport::LastCrash crashRecord;
};

} // namespace whynot
