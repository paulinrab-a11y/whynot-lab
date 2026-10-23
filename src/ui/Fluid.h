#pragma once

// Fluidez da interface (pedido do usuário, 21/10 à noite: "animações fluidas, sem aqueles travamentos").
// Peças pequenas e comuns; cada tela continua dona do próprio desenho.
// - FrameClock: um quadro por atualização da tela (VBlank), em vez de um Timer de 30 Hz que no Windows
//   chega em intervalos de 31 ou 47 ms (é daí o andar aos trancos da agulha).
// - PlayheadSmoother: a posição do transporte só anda a cada bloco de áudio (~10 ms); entre um bloco e
//   outro a agulha segue no andamento e se corrige devagar, sem pular nem tremer.
// - Ramp: uma rampa com easing para valores que mudam de uma vez (pisca do REC, queda dos medidores).

#include <JuceHeader.h>

namespace whynot::fluid
{

/** Chama onFrame a cada VBlank da tela onde o componente está, com o tempo do quadro e o intervalo
    desde o anterior (limitado a 0,1 s). Sem janela (autoteste, componente fora da tela) nada chega:
    quem usa mantém o Timer de antes como reserva, com isActive(). */
class FrameClock
{
public:
    FrameClock (juce::Component& owner, std::function<void (double seconds, double dt)> onFrame);

    /** Chegou quadro nos últimos 0,25 s? */
    bool isActive() const noexcept;

    /** Quadros por segundo medidos (0 sem VBlank). */
    double framesPerSecond() const noexcept   { return fps; }

private:
    void frame (double seconds);

    std::function<void (double, double)> callback;
    double lastFrame = -1.0, lastWallMs = -1.0, fps = 0.0;
    juce::VBlankAttachment attachment;
};

/** A agulha entre um bloco de áudio e o próximo. ticksPerSecond = o andamento (BPM / 60 * 960). */
class PlayheadSmoother
{
public:
    /** measured < 0 = parado: devolve -1. Volta para trás (laço, Ctrl+clique na régua) ou erro de mais de
        um passo: pula direto. Senão, anda no andamento e chega na posição medida em ~3 quadros. */
    double update (double measured, double dt, double ticksPerSecond) noexcept;

    void reset() noexcept   { shown = -1.0; }

private:
    double shown = -1.0;
};

/** Valor que segue um alvo com easing exponencial (sem passar do alvo): ~95 % em `seconds`. */
class Ramp
{
public:
    explicit Ramp (double secondsTo95 = 0.12) noexcept : time (secondsTo95) {}

    void setTarget (double t) noexcept     { target = t; }
    void jumpTo (double v) noexcept        { value = target = v; }
    double getValue() const noexcept       { return value; }
    double getTarget() const noexcept      { return target; }
    bool isSettled (double tolerance = 1.0e-3) const noexcept   { return std::abs (value - target) <= tolerance; }

    /** Anda dt segundos; devolve o valor novo. */
    double advance (double dt) noexcept;

private:
    double time, value = 0.0, target = 0.0;
};

/** Liga o Graphics só ao que precisa ser pintado: true se `area` encosta na parte suja. Para pular o
    desenho caro (texto, caminhos) de quem está fora do retângulo que a tela pediu para repintar. */
inline bool needsPaint (const juce::Graphics& g, juce::Rectangle<int> area) noexcept
{
    return g.getClipBounds().intersects (area);
}

inline bool needsPaint (const juce::Graphics& g, juce::Rectangle<float> area) noexcept
{
    return g.getClipBounds().toFloat().intersects (area);
}

} // namespace whynot::fluid
