#pragma once

#include <JuceHeader.h>

#include "Controls.h"

namespace whynot
{

/** A barra de status no pé da janela (28 px).
    - À esquerda, a mensagem. O aviso comum some em 7 s. O erro fica até ser fechado (×) ou trocado por outra
      mensagem: pílula no vermelho funcional suave, o ícone de aviso, e o botão da saída quando houver
      ("Abrir Áudio e MIDI"). Quem só lê de relance não perde o erro.
    - Sem mensagem, o problema que continua valendo (um canal sem sample), em tom de aviso.
    - À direita, a luz do teclado-piano (quando ligado) e a escala da interface.
*/
class StatusBar : public juce::Component,
                  private juce::Timer
{
public:
    StatusBar();

    void showInfo (const juce::String&);
    void showError (const juce::String&, const juce::String& actionLabel = {}, std::function<void()> action = {});

    /** Uma orientação que fica até ser fechada, sem ser erro: pílula neutra, com o botão da saída se houver. */
    void showNote (const juce::String&, const juce::String& actionLabel = {}, std::function<void()> action = {});
    void clearMessage();

    /** Mudam só quando o texto muda (dá para chamar a cada tique). */
    void setStandingProblem (const juce::String&);
    void setLamp (const juce::String&);        // vazio = apagada
    void setRightText (const juce::String&);

    /** A mensagem acaba antes deste x (local), quando outro componente ocupa um pedaço do pé, como o aviso
        do tom do beat; -1 = sem limite (integração de teste, 22/10). */
    void setMessageLimit (int x);

    bool hasError() const noexcept                  { return error && message.isNotEmpty(); }
    bool hasPill() const noexcept                   { return (error || note) && message.isNotEmpty(); }
    const juce::String& getMessage() const noexcept { return message; }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int lampWidth = 210;

private:
    void timerCallback() override;
    juce::Rectangle<int> rightArea() const;
    juce::Rectangle<int> lampArea() const;
    juce::Rectangle<int> messageArea() const;
    juce::Rectangle<float> pillArea() const;   // o fundo do erro, do tamanho do conteúdo

    juce::String message, standing, lamp, rightText;
    bool error = false, note = false;
    int messageLimit = -1;
    std::function<void()> action;

    IconButton actionButton { "Resolver", Icon::none, "Resolver" };
    IconButton closeButton { "Fechar o aviso", Icon::close };
};

} // namespace whynot
