#pragma once

#include <JuceHeader.h>

namespace whynot
{

/** A logo da WHYNOT enquanto o app abre (o motor de áudio, os plugins e o piano carregam), como os programas de
    música costumam fazer. Sempre nas cores da marca, qualquer que seja o tema. O Main.cpp não a mostra no
    autoteste, nas capturas (--screenshot) nem no --render.
*/
class SplashWindow : public juce::Component,
                     private juce::Timer
{
public:
    /** Abre e pinta na hora: a fila de mensagens ainda não roda, e o trabalho pesado vem logo depois. */
    static std::unique_ptr<SplashWindow> showNow();

    /** A janela principal abriu: a logo fica pelo menos 0,9 s na tela e some num fade de 0,25 s. No fim,
        `onGone` é chamado fora do timer, para quem é dono apagar a janela. */
    void fadeOut (std::function<void()> onGone);

    void paint (juce::Graphics&) override;

    /** A tela de abertura desenhada fora da tela (para o autoteste e a documentação). */
    static juce::Image preview();

    static constexpr int width = 560, height = 340;

private:
    SplashWindow();
    void timerCallback() override;

    juce::uint32 shownAt = 0;
    std::function<void()> gone;
};

} // namespace whynot
