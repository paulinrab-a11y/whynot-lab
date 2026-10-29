#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

namespace whynot
{

class AppController;

/** O painel de propriedades de um bloco de áudio da playlist (playlist fatia 6, 27/10 noite): duplo clique no bloco ou
    "Propriedades..." no menu dele. Nome e cor (do canal), ganho, pan, tom e afinação fina, os dois fades com a curva,
    inverter, mudo e travar. Cada gesto é um passo de desfazer: arrastar um controle do começo ao fim vira um passo só.
    Tudo passa pelo audio.setProperties (o inverter, pelo audio.reverse). */
class BlockProperties : public juce::Component,
                        private juce::Timer
{
public:
    BlockProperties (AppController&, const juce::String& item);
    ~BlockProperties() override;

    /** Lê o bloco de novo (o desfazer, ou outra mudança, com o painel aberto). Sem o bloco, fecha. */
    void refresh (const Project&);
    const juce::String& getItem() const noexcept   { return item; }

    void paint (juce::Graphics&) override;
    void resized() override;

    // para o autoteste: os controles, como a pessoa usa
    juce::Slider& gainSlider() noexcept      { return gain; }
    juce::Slider& panSlider() noexcept       { return pan; }
    juce::Slider& semitoneSlider() noexcept  { return semitones; }
    juce::Slider& centsSlider() noexcept     { return cents; }
    juce::Slider& fadeInSlider() noexcept    { return fadeIn; }
    juce::Slider& fadeOutSlider() noexcept   { return fadeOut; }
    juce::ComboBox& fadeInCurveBox() noexcept   { return fadeInCurve; }
    juce::TextEditor& nameEditor() noexcept  { return name; }
    juce::ToggleButton& reverseButton() noexcept   { return reverse; }
    juce::ToggleButton& muteButton() noexcept      { return mute; }
    juce::ToggleButton& lockButton() noexcept      { return lock; }
    void chooseColour (int index);   // a amostra `index` da paleta dos canais
    static constexpr int numSwatches = 8;

    /** Um slider que avisa antes de a rodinha mudar o valor (29/10): os passos da rodinha seguidos viram um passo de
        desfazer só, como um arrasto. */
    struct WheelSlider : juce::Slider
    {
        std::function<void()> onWheel;
        bool wheeling = false;   // o JUCE chama o onDragStart e o onDragEnd em cada clique da rodinha: não é arrasto

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (onWheel && isEnabled())
                onWheel();

            const juce::ScopedValueSetter<bool> turning (wheeling, true);
            juce::Slider::mouseWheelMove (e, w);
        }
    };

    /** A rodinha parada por este tempo fecha o passo de desfazer dela. */
    static constexpr int wheelPauseMs = 600;

    /** Para o autoteste: fecha agora o passo aberto pela rodinha (o que a pausa faria). */
    void finishWheel()   { timerCallback(); }

private:
    void run (const juce::var& params, const juce::Slider* from = nullptr);
    void commitName();
    void wheelTurned (juce::Slider*, const juce::String& gesture);
    void timerCallback() override;
    juce::Rectangle<int> swatchArea (int index) const;
    void mouseUp (const juce::MouseEvent&) override;

    AppController& app;
    juce::String item;
    juce::String colour;
    bool updating = false;
    bool inGesture = false;
    bool closing = false;
    juce::Slider* wheelSlider = nullptr;   // o slider da rodinha com o passo aberto (nullptr: nenhum)
    double blockSeconds = 1.0;

    juce::Label title, subtitle;
    juce::Label nameLabel, colourLabel, gainLabel, panLabel, pitchLabel, fadeInLabel, fadeOutLabel;
    juce::TextEditor name;
    WheelSlider gain, pan, semitones, cents, fadeIn, fadeOut;
    juce::ComboBox fadeInCurve, fadeOutCurve;
    juce::ToggleButton reverse { "Inverter"_t }, mute { "Mudo"_t }, lock { "Travar"_t };

    static constexpr int labelWidth = 104, rowGap = 6;
};

} // namespace whynot
