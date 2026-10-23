#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

#include <vector>

namespace whynot::timegrid
{

/** A grade da linha do tempo que acompanha o zoom: quanto mais perto, mais fina, e na grade
    automática a nota encaixa nas linhas que estão à vista. O piano roll e a playlist usam a mesma
    escada de divisões, em ticks, da mais fina para a mais grossa. Cada uma divide a seguinte, então as
    linhas sempre se encaixam: 1/16, 1/8, 1/4 e 1/2 passo, passo, as metades do tempo que couberem,
    tempo e compasso. */

constexpr int finest = ticksPerStep / 16;   // 15 ticks: 1/64 de tempo

// Valores especiais do encaixe (setSnap). Os positivos são ticks fixos; 1 é "sem grade".
constexpr int snapAuto = 0;     // segue as linhas à vista
constexpr int snapBeat = -1;    // um tempo, pela fórmula de compasso do projeto
constexpr int snapBar = -2;     // um compasso, pela fórmula de compasso do projeto

struct Meter
{
    int stepsPerBeat = 4, stepsPerBar = 16;

    int beat() const noexcept   { return juce::jmax (1, stepsPerBeat) * ticksPerStep; }
    int bar() const noexcept    { return juce::jmax (1, stepsPerBar) * ticksPerStep; }
};

/** As divisões da escada, da mais fina até o compasso. */
inline std::vector<int> divisions (Meter m)
{
    std::vector<int> d;

    for (int t = finest; t < ticksPerStep; t *= 2)
        d.push_back (t);

    d.push_back (ticksPerStep);

    for (int t = 2 * ticksPerStep; t < m.beat() && m.beat() % t == 0; t *= 2)
        d.push_back (t);

    if (m.beat() > d.back())
        d.push_back (m.beat());

    if (m.bar() > d.back())
        d.push_back (m.bar());

    return d;
}

/** Espaço mínimo, em pixels, para uma divisão aparecer. Abaixo do passo o espaço pedido é maior,
    para o zoom de sempre continuar no passo. */
struct Spacing
{
    float minPx = 7.0f, minPxBelowStep = 14.0f;

    bool fits (int ticks, float pxPerTick) const noexcept
    {
        return (float) ticks * pxPerTick >= (ticks < ticksPerStep ? minPxBelowStep : minPx);
    }
};

/** A divisão mais fina que cabe no zoom; no mínimo, o compasso. */
inline int lineTicks (float pxPerTick, Meter m, Spacing s)
{
    for (int t : divisions (m))
        if (s.fits (t, pxPerTick))
            return t;

    return m.bar();
}

/** O encaixe em ticks para um valor de setSnap. */
inline int resolveSnap (int setting, float pxPerTick, Meter m, Spacing s)
{
    if (setting == snapAuto)   return lineTicks (pxPerTick, m, s);
    if (setting == snapBeat)   return m.beat();
    if (setting == snapBar)    return m.bar();
    return juce::jmax (1, setting);
}

/** De quantos em quantos ticks a grade desenha linhas. Com encaixe fixo, nas posições do próprio
    encaixe quando cabem, para mostrar onde a nota vai cair; senão, e na grade automática ou sem
    grade, nas linhas do zoom. */
inline int visibleTicks (int setting, float pxPerTick, Meter m, Spacing s)
{
    const int zoomLines = lineTicks (pxPerTick, m, s);
    const int snap = resolveSnap (setting, pxPerTick, m, s);

    if (setting == snapAuto || snap <= 1)
        return zoomLines;

    if (s.fits (snap, pxPerTick))
        return snap;

    // encaixe apertado demais para o zoom: a primeira divisão à vista que seja múltipla dele
    for (int t : divisions (m))
        if (t >= zoomLines && t % snap == 0)
            return t;

    return m.bar();
}

enum class Level { bar, beat, step, fine };

/** Força da linha num tick: compasso, tempo, passo ou abaixo do passo. */
inline Level levelOf (juce::int64 tick, Meter m)
{
    if (tick % m.bar() == 0)        return Level::bar;
    if (tick % m.beat() == 0)       return Level::beat;
    if (tick % ticksPerStep == 0)   return Level::step;
    return Level::fine;
}

/** Nome curto de uma divisão: "1/4 passo", "passo", "1/2 tempo", "tempo", "compasso". */
inline juce::String name (int ticks, Meter m)
{
    if (ticks <= 1)                                          return "sem grade";
    if (ticks == m.bar())                                    return "compasso";
    if (ticks == m.beat())                                   return "tempo";
    if (ticks == ticksPerStep)                               return "passo";
    if (ticks < ticksPerStep && ticksPerStep % ticks == 0)   return "1/" + juce::String (ticksPerStep / ticks) + " passo";
    if (ticks < m.beat() && m.beat() % ticks == 0)           return "1/" + juce::String (m.beat() / ticks) + " tempo";
    if (ticks > m.bar() && ticks % m.bar() == 0)             return juce::String (ticks / m.bar()) + " compassos";
    return juce::String (ticks) + " ticks";
}

/** Posição musical de um tick: "3.2" no começo de um tempo, "3.2.4" no começo de um passo. */
inline juce::String positionLabel (juce::int64 tick, Meter m)
{
    const int beatsPerBar = juce::jmax (1, m.bar() / m.beat());
    const auto beat = tick / m.beat();
    auto label = juce::String (beat / beatsPerBar + 1) + "." + juce::String (beat % beatsPerBar + 1);

    if (tick % m.beat() != 0)
        label << "." << juce::String ((tick % m.beat()) / ticksPerStep + 1);

    return label;
}

/** A régua de perto, igual no piano roll e na playlist: o tempo ("3.2") a partir de 160 px por
    tempo, o passo ("3.2.4") a partir de 96 px por passo e marquinhas nas divisões à vista. Os
    números dos compassos ficam com quem chama; `toX` passa de tick para x. */
template <typename ToX>
void paintFineRuler (juce::Graphics& g, juce::Rectangle<int> r, double fromTick, double toTick, int every, float pxPerTick,
                     Meter m, ToX toX, const juce::Font& labelFont, juce::Colour text, juce::Colour marks)
{
    const bool steps = (float) ticksPerStep * pxPerTick >= 96.0f && m.beat() > ticksPerStep;
    const int labelTicks = steps ? ticksPerStep : m.beat();

    if (steps || (float) m.beat() * pxPerTick >= 160.0f)
    {
        g.setFont (labelFont);
        g.setColour (text);
        const auto first = juce::jmax ((juce::int64) 0, (juce::int64) std::floor (fromTick / labelTicks));
        const auto last = (juce::int64) std::ceil (toTick / labelTicks);

        for (auto i = first; i <= last; ++i)
            if (const auto tick = i * labelTicks; tick % m.bar() != 0)   // no compasso, o número já está lá
                g.drawText (positionLabel (tick, m), juce::Rectangle<float> (toX ((double) tick) + 3.0f, (float) r.getY(), 60.0f, (float) r.getHeight() - 4.0f),
                            juce::Justification::centredLeft, false);
    }

    if (every < m.beat())
    {
        g.setColour (marks);
        const auto first = juce::jmax ((juce::int64) 0, (juce::int64) std::floor (fromTick / every));
        const auto last = (juce::int64) std::ceil (toTick / every);

        for (auto i = first; i <= last; ++i)
            if ((i * every) % m.beat() != 0)
                g.fillRect (toX ((double) (i * every)), (float) r.getBottom() - 4.0f, 1.0f, 3.0f);
    }
}

} // namespace whynot::timegrid
