#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

#include <functional>
#include <vector>

namespace whynot
{

class AppController;

/** Os itens do MIDI learn (bloco 4d, 23/10) no menu do botão direito de um controle: o fader e o pan do mixer, um knob
    de efeito e um macro do rack. */
namespace midilearn
{
    /** O link do projeto para este alvo, ou nulo. */
    const MidiLink* linkFor (const Project&, const MidiLink& target);

    /** "Aprender do controlador MIDI" (ou "Controlador: CC 21 · trocar"), "Esquecer o controlador" quando há link e
        "Parar de aprender" enquanto este alvo espera o knob. Cada item roda actions[id - 1]. */
    void addItems (juce::PopupMenu&, AppController&, const MidiLink& target, std::vector<std::function<void()>>& actions);

    /** Um menu só com os itens do MIDI learn e um título ("VOLUME", "PAN"), mostrado junto do controle. */
    void showMenu (AppController&, const MidiLink& target, const juce::String& title, juce::Component* anchor);
}

} // namespace whynot
