#include "Fluid.h"

namespace whynot::fluid
{

FrameClock::FrameClock (juce::Component& owner, std::function<void (double, double)> onFrame)
    : callback (std::move (onFrame)),
      attachment (&owner, [this] (double seconds) { frame (seconds); })
{
}

void FrameClock::frame (double seconds)
{
    // o primeiro quadro (ou o primeiro depois de a janela ficar escondida) não tem intervalo de verdade
    const double dt = lastFrame < 0.0 ? 0.0 : juce::jlimit (0.0, 0.1, seconds - lastFrame);

    if (dt > 0.0)
        fps += (1.0 / dt - fps) * 0.1;

    lastFrame = seconds;
    lastWallMs = juce::Time::getMillisecondCounterHiRes();

    if (callback != nullptr)
        callback (seconds, dt);
}

bool FrameClock::isActive() const noexcept
{
    return lastWallMs >= 0.0 && juce::Time::getMillisecondCounterHiRes() - lastWallMs < 250.0;
}

//==============================================================================
double PlayheadSmoother::update (double measured, double dt, double ticksPerSecond) noexcept
{
    if (measured < 0.0 || ! std::isfinite (measured))
        return shown = -1.0;

    if (shown < 0.0 || dt <= 0.0 || ! (ticksPerSecond > 0.0))
        return shown = measured;

    const double predicted = shown + dt * ticksPerSecond;
    const double error = measured - predicted;
    constexpr double jump = 240.0;   // um passo: o laço voltou, ou a pessoa mudou a agulha de lugar

    if (std::abs (error) > jump)
        return shown = measured;

    // corrige ~63 % do erro a cada 50 ms (uns 3 quadros a 60 Hz), sem nunca andar para trás
    const double k = 1.0 - std::exp (-dt / 0.05);
    shown = juce::jmax (shown, predicted + error * k);
    return shown;
}

//==============================================================================
double Ramp::advance (double dt) noexcept
{
    if (dt <= 0.0 || time <= 0.0)
    {
        value = dt > 0.0 ? target : value;
        return value;
    }

    value += (target - value) * (1.0 - std::exp (-3.0 * dt / time));

    if (std::abs (target - value) < 1.0e-6)
        value = target;

    return value;
}

} // namespace whynot::fluid
