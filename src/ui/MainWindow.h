#pragma once

#include <JuceHeader.h>

namespace whynot
{

class AppController;
class MainComponent;

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (AppController&);
    ~MainWindow() override;

    void closeButtonPressed() override;

    /** Pergunta se pode descartar alterações não salvas. true = pode seguir. */
    bool confirmDiscardChanges();

    /** Grava a janela inteira em PNG (usado pelo modo --screenshot). */
    bool saveScreenshot (const juce::File&);

    /** Grava em PNG a janela do efeito no slot `slot` (0 a 7) da trilha de mixer `trackName`
        (modo --screenshot-fx, para a documentação). */
    bool saveFxScreenshot (const juce::File&, const juce::String& trackName, int slot);

    /** Grava a janela do WHYNOT Synth do canal `channelName` (página 0..3; camada 0..3, 4 = sub). */
    bool saveSynthScreenshot (const juce::File&, const juce::String& channelName, int page, int layer);

    /** Mostra "playlist", "channelRack" ou "pianoRoll" na parte de cima. */
    void showView (const juce::String& panel);

    /** A tela principal (a ponte da IA liga nela o aviso do pé e a pergunta, Fase 4). */
    MainComponent& getContent() const noexcept   { return *content; }

private:
    AppController& app;
    MainComponent* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

} // namespace whynot
