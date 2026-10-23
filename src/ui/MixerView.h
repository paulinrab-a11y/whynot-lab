#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "LevelMeter.h"
#include "fx/InsertPanel.h"
#include "../model/Project.h"

namespace whynot
{

class AppController;

class MixerStrip : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    MixerStrip (AppController&, const juce::String& trackId);

    const juce::String& getTrackId() const noexcept   { return trackId; }
    void update (const Project&, const MixerTrack&);
    void tick()                                        { meter.tick(); }
    void setSelected (bool);

    /** Gravação (3.5): a trilha armada mostra o pico da entrada (0 a 1+) e pisca o armar enquanto grava. */
    bool isArmed() const noexcept                      { return armed; }
    void setRecordState (float inputPeak, bool recording);

    std::function<void (const juce::String&, bool)> onStatus;
    std::function<void()> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> tapeArea() const     { return { 6, 8, getWidth() - 12, 28 }; }
    juce::Rectangle<int> outputArea() const   { return { 6, 42, getWidth() - 12, 24 }; }
    juce::Rectangle<int> fxArea() const       { return { 6, 70, getWidth() - 12, 20 }; }
    void showOutputMenu();
    void showNameMenu();
    void showMonitorMenu();   // botão direito no armar: monitorar a entrada (gravar voz)
    void toggleFrozen();
    void rename();

    AppController& app;
    const juce::String trackId;
    juce::String name, outputName;
    juce::Colour colour;
    bool isMaster = false, selected = false;
    bool outputIsMaster = false;   // destino padrão: a pílula sai em cinza
    float volumeDb = 0.0f;
    juce::String fxSummary;
    int fxCount = 0;
    bool armed = false, frozen = false;
    juce::String monitor;          // MixerTrack::monitor: "" = quando armada, "off", "on"
    bool armMenuClick = false;     // o botão direito no armar abriu o menu: o clique dele não arma
    float inputLevel = 0.0f;   // 0 a 1 na escala do medidor
    double lastInputTick = -1.0;   // ms: a queda do medidor da entrada conta o tempo, não os quadros

    LevelMeter meter { true };
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    IconButton armButton { "Armar", Icon::record, {}, IconButton::Style::filled };   // o terceiro da linha: M S ●
};

class MixerView : public juce::Component,
                  public juce::DragAndDropContainer
{
public:
    explicit MixerView (AppController&);

    void refresh (const Project&);
    void tick();

    std::function<void (const juce::String&, bool)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Trilha cujos efeitos aparecem no painel da direita. */
    void selectTrack (const juce::String& mixerId);

    /** O LUFS do master na barra de cima (bloco 1, 22/10): o texto de agora, para o autoteste. */
    juce::String getLoudnessText() const;
    /** O alvo de loudness (0 = nenhum), escolhido no clique do LUFS. */
    static void setLoudnessTarget (float lufs, const juce::String& name);
    /** Comparar com e sem efeitos (mixer.compare): "off", "bypass" ou "matched". */
    void setCompareMode (const juce::String& mode);
    const juce::String& getCompareMode() const noexcept   { return compareMode; }

private:
    void showStyleMenu();
    void showCompareMenu();
    void showLoudnessMenu();
    juce::Rectangle<int> loudnessArea() const;

    AppController& app;
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<MixerStrip> strips;
    std::unique_ptr<MixerStrip> masterStrip;
    InsertPanel insertPanel;
    juce::Viewport panelViewport;
    IconButton styleButton { "Estilo de mixagem: aplica a receita de um gênero em todas as trilhas e no master"_t, Icon::mixer,
                             "Estilo de mixagem" };
    IconButton compareButton { "Comparar com e sem efeitos", Icon::speaker, "Comparar" };
    juce::String selectedTrack;
    juce::String compareMode { "off" };

    // o LUFS do master: o que está na tela (repinta só quando muda) e o alvo escolhido (vale para a sessão)
    float shownLufs = -100.0f, shownPeak = -100.0f;
    bool shownLive = false, wasPlaying = false;
    static inline float targetLufs = 0.0f;   // 0 = sem alvo
    static inline juce::String targetName;

    static constexpr int stripWidth = 96, headerHeight = theme::metric::toolbar;
};

} // namespace whynot
