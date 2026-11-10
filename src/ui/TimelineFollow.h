#pragma once

#include <algorithm>

/** Seguir a reprodução na playlist e no piano roll, sem JUCE (testado em tests/editors).

    Por página: enquanto a agulha está à vista a tela fica parada; quando ela sai (pela direita, ou para trás
    num laço), a tela pula e deixa a agulha a 10% da borda esquerda. Rolar, dar zoom ou arrastar a barra desliga
    sozinho (a tela é do usuário) até tocar de novo ou clicar no botão; com o mouse apertado na grade (um gesto
    de edição) a tela não anda. Só contas: nada de alocação, pode rodar a cada quadro. */
namespace whynot::follow
{

constexpr double pageMargin = 0.1;   // fração da largura que fica atrás da agulha depois do pulo

/** A rolagem (em ticks) para a agulha ficar à vista; a mesma de antes se ela já está. */
inline double pageScroll (double playhead, double scroll, double visibleTicks) noexcept
{
    if (playhead < 0.0 || visibleTicks <= 0.0)
        return scroll;

    if (playhead >= scroll && playhead < scroll + visibleTicks)
        return scroll;

    return std::max (0.0, playhead - visibleTicks * pageMargin);
}

class Follower
{
public:
    struct Result
    {
        double scroll;       // a rolagem pedida (igual à de entrada = não mexer)
        bool stateChanged;   // ligou ou desligou sozinho: o botão da barra precisa acompanhar
    };

    /** A preferência do usuário (o botão). */
    bool isWanted() const noexcept   { return wanted; }
    /** Seguindo de fato: ligado e não suspenso por uma rolagem à mão. */
    bool isOn() const noexcept       { return wanted && ! paused; }

    void setOn (bool on) noexcept
    {
        wanted = on;
        paused = false;
        rebase = true;
    }

    /** Um quadro. `started` = a agulha acabou de aparecer (começou a tocar); `gesture` = mouse apertado na grade.
        Se a rolagem pedida for outra, quem chama aplica, limita ao conteúdo e avisa com settled(). */
    Result step (double playhead, bool started, bool gesture, double scroll, double zoom, double visibleTicks) noexcept
    {
        Result r { scroll, false };

        if (playhead < 0.0)
            return r;

        if (started || rebase)
        {
            if (started && paused)
            {
                paused = false;
                r.stateChanged = wanted;
            }

            lastScroll = scroll;
            lastZoom = zoom;
            rebase = false;
        }

        if (! wanted || paused || gesture)
            return r;

        if (scroll != lastScroll || zoom != lastZoom)
        {
            paused = true;   // alguém mexeu na vista desde o último quadro: é do usuário
            r.stateChanged = true;
            return r;
        }

        r.scroll = pageScroll (playhead, scroll, visibleTicks);
        return r;
    }

    /** A vista como ficou depois de um pulo do próprio seguir (para ele não se confundir com o usuário). */
    void settled (double scroll, double zoom) noexcept
    {
        lastScroll = scroll;
        lastZoom = zoom;
    }

private:
    bool wanted = true, paused = false, rebase = true;
    double lastScroll = 0.0, lastZoom = 0.0;
};

} // namespace whynot::follow
