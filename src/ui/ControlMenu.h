#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

#include <functional>
#include <vector>

namespace whynot
{

class AppController;

/** O botão direito dos controles (26/10, como no FL): "Automatizar" cria a curva embaixo da playlist já no valor de
    agora (um passo de desfazer); com a curva criada, o item vira "Apagar a automação". */
namespace controlmenu
{
    /** Põe no menu o item da automação do alvo. Cada item roda actions[id - 1]. */
    void addAutomation (juce::PopupMenu&, AppController&, const AutomationTarget&, std::vector<std::function<void()>>& actions);

    /** Cria a curva do alvo com um ponto no começo, no valor de agora. Devolve o id da curva, ou vazio. */
    juce::String automate (AppController&, const AutomationTarget&);

    /** A posição do knob (0 a 1) do alvo agora, ou -1 se ele não existe. */
    float currentPosition (const Project&, const AutomationTarget&);
}

} // namespace whynot
