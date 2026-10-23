#pragma once

#include <JuceHeader.h>

namespace whynot
{

namespace te = tracktion;

/** Medidor estéreo de pico, lendo um LevelMeterPlugin do Tracktion.
    Guarda uma referência ao plugin, então continua seguro se a trilha for apagada.
*/
class LevelMeter : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    explicit LevelMeter (bool vertical);
    ~LevelMeter() override;

    void setSource (te::LevelMeterPlugin*);

    /** Um quadro: pode vir de 30 Hz a 144 Hz (a queda conta o tempo de verdade, não os quadros). */
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override   { gradientSize = {}; }
    void mouseDown (const juce::MouseEvent&) override   { clipped = false; repaint(); }

private:
    static float toProportion (float db) noexcept;

    /** Onde o desenho de agora cai, em pixels: só repinta quando isto muda (fluidez, 21/10). */
    struct Pixels
    {
        int level[2] = { -1, -1 }, peak[2] = { -1, -1 };
        bool clipped = false;
        bool operator== (const Pixels&) const = default;
    };

    Pixels pixelsNow() const;
    const juce::ColourGradient& gradientFor (juce::Rectangle<float> bar, int channel);

    const bool vertical;
    te::Plugin::Ptr source;
    te::LevelMeasurer::Client client;
    float level[2] = { -100.0f, -100.0f };
    float peak[2] = { -100.0f, -100.0f };
    double peakHoldUntil[2] = {};    // ms (getMillisecondCounterHiRes) até o pico começar a cair
    double lastTickMs = -1.0;
    bool clipped = false;
    Pixels shown;

    // o gradiente das barras só muda com o tamanho: era montado duas vezes a cada quadro
    juce::Rectangle<int> gradientSize;
    juce::ColourGradient gradients[2];
};

} // namespace whynot
