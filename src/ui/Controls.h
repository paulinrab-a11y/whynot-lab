#pragma once

#include <JuceHeader.h>

#include "Icons.h"
#include "Theme.h"

namespace whynot
{

/** Botão da casa: ícone, rótulo opcional e, se abrir menu, uma setinha.
    ghost  = sem fundo até o mouse passar (barras de ferramentas);
    filled = com fundo de controle (ações principais, como tocar e parar).
    Ligado (toggle) = fundo laranja translúcido e ícone/rótulo laranja.
*/
class IconButton : public juce::Button
{
public:
    enum class Style { ghost, filled };

    IconButton (const juce::String& name, Icon, const juce::String& label = {}, Style = Style::ghost);

    void setIcon (Icon);
    void setLabel (const juce::String&);
    void setStyle (Style);
    void setShowsMenuArrow (bool);

    /** Ligado sem laranja: para botões que só abrem ou fecham um painel (Ideias, por exemplo).
        Fica com a pílula clara de "escolhido", a mesma do trilho e dos segmentos. */
    void setNeutralWhenOn (bool);

    /** Gravando agora: o vermelho funcional (o mesmo do clipe), com a intensidade de 0 a 1 para piscar.
        0 desliga o alerta e o botão volta ao estado normal. */
    void setAlert (float level);

    /** Largura que mostra tudo (ícone, rótulo e setinha) numa dada altura. */
    int getIdealWidth (int height) const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    juce::String label;
    Style style;
    bool menuArrow = false;
    bool neutralOn = false;
    float alert = 0.0f;
};

/** Escolha entre opções que se excluem (PAT | SONG, ferramentas do piano roll).
    A opção escolhida ganha uma pílula clara; com `accent`, a pílula é laranja (o que vai tocar).
*/
class SegmentedControl : public juce::Component,
                         public juce::TooltipClient
{
public:
    struct Item
    {
        juce::String label;
        Icon icon = Icon::none;
        juce::String tooltip;
    };

    explicit SegmentedControl (bool accent = false);

    void setItems (std::vector<Item>);
    void setSelected (int index);
    int getSelected() const noexcept        { return selected; }

    /** Quem mostra o nome: todos, nenhum (só ícones, para telas estreitas) ou só o escolhido, que se
        alarga para caber o nome enquanto os outros ficam no ícone. Os nomes continuam nas dicas. */
    enum class Labels { all, none, selected };
    void setLabels (Labels);
    void setShowsLabels (bool b)            { setLabels (b ? Labels::all : Labels::none); }
    int getIdealWidth() const;

    std::function<void (int index)> onChange;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void enablementChanged() override       { repaint(); }
    juce::String getTooltip() override;

private:
    int indexAt (int x) const;
    juce::Rectangle<float> segment (int index) const;

    float labelRoom() const;   // no modo "só o escolhido": o espaço a mais do segmento escolhido

    std::vector<Item> items;
    int selected = 0, hover = -1;
    bool accent;
    Labels labels = Labels::all;
};

/** Opções que se excluem, em pílulas que quebram linha (gênero, clima, parte, compassos no Ideias).
    Escolhida: pílula raisedHigh com texto semibold. Para poucas opções numa linha só, use SegmentedControl. */
class ChoiceChips : public juce::Component
{
public:
    void setItems (const juce::StringArray& labels);
    void setSelected (int index);
    int getSelected() const noexcept   { return selected; }
    int getHeightForWidth (int width) const;

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<juce::Rectangle<float>> layout (int width) const;
    int indexAt (juce::Point<float>) const;

    juce::StringArray labels;
    std::vector<juce::Rectangle<float>> boxes;
    int selected = 0, hover = -1;

    static constexpr int chipHeight = 28, chipGap = 6;
};

/** Quantidade de 0 a 100%: o nome à esquerda (100 px), trilho fino com o trecho aceso e a bolinha branca,
    e o valor à direita. Duplo clique volta a 50%. */
class AmountSlider : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    explicit AmountSlider (const juce::String& label);

    void setValue (double v)           { value = juce::jlimit (0.0, 1.0, v); repaint(); }
    double getValue() const noexcept   { return value; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> track() const;
    void setFromX (float x);

    juce::String label;
    double value = 0.5;
};

} // namespace whynot
