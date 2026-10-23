#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "../common/Text.h"

namespace whynot
{

class AppController;

/** Áudio e MIDI (F10): a placa de som (WASAPI, ASIO quando há driver), a gravação (entrada da placa
    e ajuste da latência), as entradas MIDI (controladores, que tocam o canal escolhido no rack) e o
    teclado do computador como piano.
*/
class DeviceSettings : public juce::Component,
                       private juce::ChangeListener,
                       private juce::ComponentListener,
                       private juce::Timer
{
public:
    DeviceSettings (AppController&, std::function<bool()> typingIsOn, std::function<void (bool)> setTyping);
    ~DeviceSettings() override;

    static void show (AppController&, juce::Component* parent, std::function<bool()> typingIsOn, std::function<void (bool)> setTyping);

    /** O conteúdo da janela: os ajustes até maxHeight de altura; daí para cima, eles rolam. */
    static std::unique_ptr<juce::Component> fitToHeight (std::unique_ptr<DeviceSettings>, int maxHeight);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;
    void rebuildMidiList();
    void rebuildRecordInputs();
    void rebuildMicDevices();            // o microfone (o aparelho de entrada), no Windows Audio
    void selectRecordInput();
    void refreshRecordNote();
    void updateSize();
    void refreshGuide();                 // o assistente: a placa, o microfone e os passos
    void timerCallback() override;       // o medidor com alvo (30 vezes por segundo)
    int recordSectionHeight() const;
    int midiSectionHeight() const;

    AppController& app;
    std::function<bool()> typingIsOn;
    std::function<void (bool)> setTyping;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> audio;

    // Gravação: os ids da lista são 2 * entrada + 1 (mono) e 2 * entrada + 2 (o par estéreo), entrada a partir de 0
    juce::ComboBox micDevice;     // id 1 = o padrão do Windows; 2 em diante = os aparelhos, na ordem de inputDevices()
    juce::ComboBox recordInput;
    juce::Slider recordAdjust { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label recordNote;
    juce::String recordError;   // a última recusa do record.setInput (gravando, entrada inválida)

    // O assistente de gravação (gravar voz, fatia 7): o tipo de microfone, a placa reconhecida pelo nome que o
    // driver dá, os passos recomendados e o medidor com o alvo da voz (pico entre -18 e -10 dBFS).
    juce::ComboBox micType;
    juce::Label guideText;
    IconButton applyGuide { "Aplicar", Icon::check, "Aplicar: entrada 1 mono" };
    int guideHeight = 112;   // a área dos passos: acompanha o texto (refreshGuide)
    float meterDb = -100.0f, meterHoldDb = -100.0f;
    int meterHoldTicks = 0;
    juce::Rectangle<int> meterArea;

    std::vector<std::unique_ptr<juce::ToggleButton>> midiToggles;
    juce::StringArray midiIds;
    IconButton rescanButton { "Procurar", Icon::search, "Procurar de novo" };
    juce::ToggleButton typing { "Teclado do computador toca o canal escolhido (Ctrl+T)" };

    static constexpr int rowHeight = 30;
    static constexpr int recordRow = 40, labelWidth = 150;
};

} // namespace whynot
