#pragma once

#include <JuceHeader.h>

#include "Controls.h"

namespace whynot
{

/** "Comece aqui": o cartão da primeira vez (revisão de iniciante, 22/10). Três passos numerados, no canto de cima à
    direita de um projeto novo: pôr um beat de exemplo, gravar a voz e exportar em MP3.
    - Fecha no ×. "Não mostrar de novo" vale para as próximas aberturas.
    - Depois de um passo, ele encolhe para a pílula "Comece aqui", que abre de novo com um clique, para não cobrir o
      que o passo acabou de mostrar.
*/
class WelcomeCard : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    WelcomeCard();

    std::function<void()> onBeat, onVoice, onExport;
    std::function<void (bool forever)> onClose;

    bool isCollapsed() const noexcept   { return collapsed; }

    /** Encolhe ou abre mantendo o canto de cima à direita no lugar. */
    void setCollapsed (bool);

    /** O tamanho que o cartão pede agora (aberto ou encolhido). */
    juce::Point<int> idealSize() const noexcept   { return collapsed ? juce::Point<int> (pillWidth, pillHeight) : juce::Point<int> (width, height); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int width = 336, height = 260, pillWidth = 148, pillHeight = 32;

private:
    struct Step : juce::Button
    {
        Step (int number, juce::String title, juce::String detail);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

        int number;
        juce::String title, detail;
    };

    Step beat, voice, exportMp3;
    IconButton closeButton { "Fechar", Icon::close };
    IconButton hideButton { "Não mostrar este cartão ao abrir o app"_t, Icon::none, "Não mostrar de novo"_t };
    bool collapsed = false;
    juce::DropShadower shadow { juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 18, { 0, 6 }) };
};

} // namespace whynot
