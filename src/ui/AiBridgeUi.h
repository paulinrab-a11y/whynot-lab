#pragma once

#include <JuceHeader.h>

#include "Controls.h"

namespace whynot
{

/** A pergunta da IA (Fase 4, fatia 4.5; docs/fase-4-plano.md, seção 5.5): quando um comando da IA precisa do
    seu ok (abrir um VST3, gravar por cima, um caminho fora das pastas), um cartão aparece no alto da tela,
    sem roubar o foco e sem travar o resto: dá para continuar mexendo e responder depois.
    - O que a IA quer, o porquê e os parâmetros, numa linha cortada.
    - Recusar (ou Esc, depois de um clique no cartão) e Permitir. O Permitir só acende depois de um instante, para um
      clique que já vinha no caminho não autorizar nada; Enter não autoriza.
    - Um relógio mostra quanto falta: sem resposta em 2 min, a ponte recusa e fecha o cartão.
    O clique é só seu: a IA não tem comando para clicar na tela.
*/
class AiConfirmCard : public juce::Component,
                      private juce::Timer
{
public:
    AiConfirmCard (const juce::String& commandTitle, const juce::String& commandId, const juce::String& reason,
                   const juce::String& details, int seconds);

    /** true = Permitir. Chamado uma vez; quem abriu fecha o cartão (e este componente pode sumir na chamada). */
    std::function<void (bool allowed)> onAnswer;

    /** O mesmo que os botões (o autoteste não passa pelo mouse). O Permitir ainda apagado não faz nada. */
    void answer (bool allowed);

    bool isArmed() const noexcept   { return armed; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool hitTest (int x, int y) override;

    /** O tamanho do componente, com a margem da sombra. */
    static juce::Rectangle<int> boundsIn (juce::Rectangle<int> area);

    static constexpr int cardWidth = 520, cardHeight = 214, shadow = 18, armMs = 700;

private:
    void timerCallback() override;
    juce::Rectangle<int> card() const;
    juce::String remainingText() const;

    juce::String title, commandId, reason, details;
    double deadline = 0.0, shownAt = 0.0;   // ms (getMillisecondCounterHiRes)
    bool armed = false, answered = false;

    IconButton allowButton { "Permitir", Icon::check, "Permitir", IconButton::Style::filled };
    IconButton refuseButton { "Recusar", Icon::close, "Recusar" };
};

/** O aviso da IA no pé da janela (seção 5.7), só quando a WHYNOT abriu com --control:
    - conectada: "IA conectada" e "Desligar a IA", que fecha a conexão e recusa as próximas;
    - desligada: "IA desligada" e "Ligar a IA";
    - ligada e sem ninguém: some.
    Sem laranja: um ponto claro quando conectada (docs/design.md, princípio 3: cor não dá importância).
*/
class AiStatusNotice : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    AiStatusNotice();

    enum class State { hidden, connected, waiting, off };

    void setState (State);
    State getState() const noexcept   { return state; }

    /** "Desligar a IA" ou "Ligar a IA". */
    std::function<void()> onToggle;

    int getIdealWidth() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String text() const;

    State state = State::hidden;
    IconButton toggleButton { "Desligar a IA", Icon::none, "Desligar a IA" };

    static constexpr int buttonHeight = 22, dot = 7;
};

} // namespace whynot
