#pragma once

namespace whynot
{

/** A placa de som que some e volta (frente ui-atualizacao, 10/11): a decisão, sem JUCE (tests/ui).

    O MainComponent escuta o juce::AudioDeviceManager o tempo todo e guarda a saída "vigiada": a que estava
    aberta por último. A cada aviso do gerente ele junta os fatos e pergunta aqui o que fazer.

    O que o JUCE já faz sozinho (AudioDeviceManager::audioDeviceListChanged): se a placa aberta saiu da
    lista, fecha e tenta a saída padrão do driver. Ele não avisa ninguém e não volta para a placa
    quando ela reaparece: essas duas partes são as nossas.
*/
namespace devicewatch
{
    struct Facts
    {
        bool watching = false;        // há uma saída vigiada (a última que esteve aberta)
        bool lost = false;            // ela sumiu e estamos esperando voltar
        bool open = false;            // há uma placa aberta agora
        bool isWatched = false;       // a aberta é a vigiada (mesmo driver, mesma saída)
        bool watchedListed = false;   // a vigiada aparece na lista do driver dela
        bool explicitOther = false;   // a escolha guardada pela pessoa (Áudio e MIDI) é outra saída
    };

    enum class Action
    {
        none,           // nada a fazer
        track,          // passa a vigiar o que está aberto agora (ou nada) e para de esperar
        lostNoSound,    // sumiu e nada abriu no lugar: erro no pé, na hora
        lostFallback,   // sumiu e o som passou para outra saída: aviso no pé
        reopen,         // voltou a aparecer: reabrir nela
        returned        // já está aberta de novo (alguém reabriu): só avisar
    };

    constexpr Action decide (Facts f) noexcept
    {
        if (! f.watching)
            return f.open ? Action::track : Action::none;

        if (f.isWatched)
            return f.lost ? Action::returned : Action::track;   // track: guarda a taxa e o buffer de agora

        if (! f.lost)
        {
            if (f.watchedListed)
                return Action::track;   // ela continua lá: a pessoa trocou ou fechou a saída

            return f.open ? Action::lostFallback : Action::lostNoSound;
        }

        if (f.explicitOther)
            return Action::track;       // escolheu outra enquanto esperava: não puxa de volta

        return f.watchedListed ? Action::reopen : Action::none;
    }
}

} // namespace whynot
