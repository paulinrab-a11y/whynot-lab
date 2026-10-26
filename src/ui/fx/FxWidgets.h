#pragma once

#include <JuceHeader.h>

#include "../../dsp/Processor.h"

namespace whynot
{

namespace fxui
{
    /** Texto de um valor com a unidade, em português ("1,2 kHz", "+3,0 dB", "Desligado"). */
    juce::String formatValue (const dsp::ParamInfo&, float value);

    /** Faixa do controle: logarítmica para frequências e tempos, inteira para listas. */
    juce::NormalisableRange<double> rangeFor (const dsp::ParamInfo&);

    juce::String utf8 (const char*);

    /** Maiúsculas com os acentos do português: o toUpperCase do JUCE não sobe "ç", "ã", "í"... no Windows
        ("AFINAçãO"). Como o caps do ShortcutEditor. */
    juce::String caps (const juce::String&);
}

/** Um juce::Slider cujo botão direito abre um menu (onMenu) em vez de começar a arrastar: o gesto não fica aberto
    por baixo do menu (bloco 4c, o "Ligar ao macro"). Sem onMenu, é um Slider comum. */
class MenuSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;

    std::function<void()> onMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        menuClick = e.mods.isPopupMenu() && onMenu != nullptr;

        if (menuClick)
            onMenu();
        else
            juce::Slider::mouseDown (e);
    }

    // só o clique que abriu o menu é engolido: arrastando com o esquerdo, o direito apertado no meio não deixa o
    // gesto preso (o JUCE manda o up do arraste com os botões de agora)
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! menuClick)
            juce::Slider::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! std::exchange (menuClick, false))
            juce::Slider::mouseUp (e);
    }

private:
    bool menuClick = false;
};

/** Um parâmetro de efeito: knob (contínuo), botão (liga/desliga) ou lista (escolha).
    Nome em legenda em cima, valor em números embaixo. Mexer avisa por onChange; o começo e o
    fim do gesto viram um passo de desfazer só (onGestureStart / onGestureEnd). */
class ParamControl : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    explicit ParamControl (const dsp::ParamInfo&);

    void setValue (float value);   // sem avisar
    float getValue() const noexcept   { return value; }
    const dsp::ParamInfo& getInfo() const noexcept   { return info; }

    /** Arco laranja: o parâmetro está agindo agora (redução de ganho, por exemplo). */
    void setAccent (bool);

    /** Legenda mais curta que o nome (dentro de um grupo que já diz o contexto). */
    void setCaption (const juce::String& c)   { caption = c; repaint(); }

    std::function<void (float)> onChange;
    std::function<void()> onGestureStart, onGestureEnd;

    /** Botão direito no knob ou na legenda: o menu do parâmetro (ligar ao macro, controlador, automação). Sem ele, o
        botão direito oferece "Voltar ao padrão" (26/10, como no FL). */
    std::function<void()> onMenu;

    /** Volta ao valor padrão do parâmetro, como um gesto (um passo de desfazer). */
    void resetToDefault();

    /** "Voltar ao padrão (valor)" para o menu do botão direito. */
    juce::String resetLabel() const;

    static constexpr int cellWidth = 80, cellHeight = 84;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void changed (float);
    void showMenu();

    const dsp::ParamInfo& info;
    juce::String caption;
    float value = 0.0f;
    MenuSlider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton toggle;
    juce::ComboBox list;
};

/** Barra vertical de redução de ganho (de cima para baixo, laranja), com escala em dB. */
class ReductionMeter : public juce::Component
{
public:
    explicit ReductionMeter (float maxDb = 24.0f) : range (maxDb) {}

    void setReduction (float db);
    void paint (juce::Graphics&) override;

private:
    float range, current = 0.0f, peakHold = 0.0f;
    int holdFrames = 0;
};

/** Barra horizontal de -1 a +1 (correlação de fase). */
class CorrelationMeter : public juce::Component
{
public:
    void setValue (float v)   { if (std::abs (v - value) > 0.005f) { value = v; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    float value = 1.0f;
};

} // namespace whynot
