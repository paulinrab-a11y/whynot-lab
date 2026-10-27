#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "../automation/AutomationMath.h"
#include "../dsp/Processor.h"
#include "../model/Project.h"

#include <optional>

namespace whynot
{

class AppController;

/** Automação (3.5), embaixo da grade da playlist e na mesma linha do tempo: uma raia por curva.
    Clique cria um ponto, arrastar move, botão direito apaga; cada arrasto vira um passo de desfazer
    (automation.setPoints no fim). A curva toca no modo SONG: no PAT ela fica apagada, em cinza.
    (26/10) A bolinha no meio de cada trecho curva o trecho (a tensão): arrastar para cima ou para baixo,
    duplo clique deixa reto. Com a gravação de automação ligada, o cabeçalho avisa em vermelho.
*/
class AutomationLanes : public juce::Component,
                        public juce::TooltipClient
{
public:
    explicit AutomationLanes (AppController&);

    void refresh (const Project&);
    /** O mesmo enquadramento da grade: o tick na borda esquerda e o zoom. */
    void setView (double scrollTicks, float pxPerTick);
    void setPlayhead (double ticks);   // < 0 = parado
    /** O cabeçalho e até 3 raias à vista (as outras rolam com a rodinha). */
    int getIdealHeight() const;

    std::function<int()> getSnapTicks;          // o encaixe da grade da playlist
    std::function<void()> onHeightChanged;      // entrou ou saiu uma curva
    std::function<void (const juce::String&, bool isError)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

    static constexpr int headerHeight = 28, laneHeight = 56, maxVisible = 3;
    static constexpr int nameWidth = 150, scrollSize = 12;   // as mesmas medidas da grade da playlist

private:
    enum class Format { percent, db, pan, effect };

    struct Lane
    {
        juce::String id, name;
        std::optional<automation::Range> range;   // nullopt: o alvo saiu (efeito apagado, trilha removida)
        std::vector<automation::Point> points;
        Format format = Format::percent;
        const dsp::ParamInfo* param = nullptr;    // efeito nativo: para o texto do valor ("1,2 kHz")
    };

    struct Source { juce::String id, name; bool isTrack = true; };

    juce::Rectangle<int> laneBounds (int index) const;   // a raia inteira, na tela (vazio se fora da vista)
    juce::Rectangle<int> curveBounds (int index) const;  // só a parte da curva, com respiro em cima e embaixo
    int laneAt (juce::Point<float>) const;
    int pointAt (int lane, juce::Point<float>) const;
    /** O trecho cuja bolinha do meio está embaixo do mouse (o índice do ponto onde ele começa), ou -1. */
    int handleAt (int lane, juce::Point<float>) const;
    juce::Point<float> handlePosition (int lane, int segment) const;
    float tickToX (double tick) const;
    double xToTick (float x) const;
    float yToPosition (int lane, float y) const;
    int snapTick (double tick, bool free) const;
    juce::String formatValue (const Lane&, float position) const;
    std::optional<float> shownPosition (const Lane&, int index) const;   // o valor ao lado do nome

    void commit (int lane);
    void showAddMenu();
    void showLaneMenu (int lane);
    void clampScroll();
    void reveal (const juce::String& laneId);

    AppController& app;
    IconButton addButton { "Nova curva", Icon::plus, "Nova curva" };

    std::vector<Lane> lanes;
    std::vector<Source> sources;   // trilhas do mixer e instrumentos VST3, para o menu de Nova curva
    bool songMode = false;
    int barTicks = ticksPerBar4;
    double scrollTicks = 0.0, playhead = -1.0;
    float pxPerTick = 96.0f / (float) ticksPerBar4;
    int firstLane = 0;
    juce::String revealLane;       // curva nova: rola até ela no próximo refresh

    int dragLane = -1, dragPoint = -1;
    int dragSegment = -1;   // arrastando a bolinha do meio: a tensão do trecho que começa nesse ponto
    bool dragging = false;
    bool recordingShown = false;
    juce::Point<float> hoverPos { -1.0f, -1.0f };
};

} // namespace whynot
