#pragma once

#include <JuceHeader.h>

#include "Icons.h"

namespace whynot
{

/** As telas que ocupam a parte de cima da janela (uma por vez). */
enum class View { playlist, channelRack, pianoRoll, projects };

/** Trilho à esquerda: a marca no topo, um botão por tela (ícone + nome) e o áudio no pé.
    Playlist (F5), Channel rack (F6), Piano roll (F7) e Projetos (F11) se alternam em cima; o Mixer (F9)
    fica embaixo e os Sons (Alt+F8) ou os Plugins (F8) num cartão à esquerda; esses acendem à parte.
*/
class ViewRail : public juce::Component,
                 public juce::TooltipClient
{
public:
    ViewRail();

    void setState (View current, bool upperVisible, bool mixerVisible, bool browserVisible, bool soundsVisible);

    /** Recebe o painel clicado: "playlist", "channelRack", "pianoRoll", "projects", "mixer", "sounds" ou "plugins". */
    std::function<void (const juce::String& panel)> onToggle;
    std::function<void()> onSettings;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    struct Item
    {
        juce::String panel, label, tooltip;
        Icon icon;
        juce::Rectangle<int> bounds;
    };

    bool isLit (const Item&) const;
    int itemAt (juce::Point<int>) const;

    std::vector<Item> items;   // as 4 telas de cima, mixer, sons, plugins e, por último, o áudio
    View view = View::channelRack;
    bool upper = true, mixer = true, browser = false, sounds = false;
    int hover = -1;
};

} // namespace whynot
