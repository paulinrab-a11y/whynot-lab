#pragma once

// Contas da automação, sem JUCE (testadas em tests/automation): a faixa do parâmetro, a posição do
// knob (de 0 a 1) e o valor da curva num tick.
//
// Os pontos guardam a posição do knob (o "v" do .wnl), e a curva anda reta nela, não no valor: numa
// frequência (escala logarítmica) ir de 200 Hz a 10 kHz passa por 1,4 kHz no meio, como um knob girado
// com o mouse. O Tracktion recebe as posições e interpola reto: valueAt() dá o mesmo valor que ele toca.

#include <algorithm>
#include <cmath>
#include <vector>

namespace whynot::automation
{

/** Como um parâmetro anda. */
struct Range
{
    enum class Scale
    {
        linear,    // posição proporcional ao valor (pan, %, dB de um efeito)
        log,       // frequências e tempos: posição proporcional ao logaritmo (min > 0)
        fader,     // volume em dB, com o mesmo fader do Tracktion: posição = e^((dB - 6) / 20), -100 dB = 0
    };

    float min = 0.0f, max = 1.0f;
    Scale scale = Scale::linear;
    bool stepped = false;   // escolha ou liga/desliga: só valores inteiros

    float clamp (float v) const noexcept
    {
        if (! std::isfinite (v))
            v = min;

        v = std::clamp (v, min, max);
        return stepped ? std::round (v) : v;
    }

    /** A posição do knob, de 0 a 1. */
    float toNormalised (float v) const noexcept
    {
        v = clamp (v);

        if (max <= min)
            return 0.0f;

        switch (scale)
        {
            case Scale::log:   return min > 0.0f ? std::log (v / min) / std::log (max / min) : (v - min) / (max - min);
            case Scale::fader: return v > -100.0f ? std::exp ((v - 6.0f) / 20.0f) : 0.0f;
            case Scale::linear:
            default:           return (v - min) / (max - min);
        }
    }

    /** O valor numa posição do knob (de 0 a 1). */
    float fromNormalised (float n) const noexcept
    {
        n = std::isfinite (n) ? std::clamp (n, 0.0f, 1.0f) : 0.0f;

        switch (scale)
        {
            case Scale::log:   return clamp (min > 0.0f ? min * std::pow (max / min, n) : min + n * (max - min));
            case Scale::fader: return clamp (n > 0.0f ? 20.0f * std::log (n) + 6.0f : -100.0f);
            case Scale::linear:
            default:           return clamp (min + n * (max - min));
        }
    }
};

/** Um ponto da curva: num tick da música, a posição do knob (0 a 1 na faixa do parâmetro). */
struct Point
{
    int t = 0;
    float v = 0.0f;

    bool operator== (const Point&) const = default;
};

/** Ordena por tick, deixa um ponto por tick (o último dado vence), tira os ticks fora de
    [0, maxTick] e põe as posições entre 0 e 1 (NaN vira 0). */
inline std::vector<Point> cleanPoints (std::vector<Point> points, int maxTick)
{
    std::stable_sort (points.begin(), points.end(), [] (const Point& a, const Point& b) { return a.t < b.t; });
    std::vector<Point> out;

    for (auto p : points)
    {
        if (p.t < 0 || p.t > maxTick)
            continue;

        p.v = std::isfinite (p.v) ? std::clamp (p.v, 0.0f, 1.0f) : 0.0f;

        if (! out.empty() && out.back().t == p.t)
            out.back() = p;
        else
            out.push_back (p);
    }

    return out;
}

/** A posição do knob no tick: reta entre os pontos. Antes do primeiro ponto vale o primeiro; depois
    do último, o último. Sem pontos, fallback. É o que o Tracktion toca (ele interpola reto). */
inline float positionAt (const std::vector<Point>& points, double tick, float fallback)
{
    if (points.empty())
        return fallback;

    if (tick <= points.front().t)
        return points.front().v;

    if (tick >= points.back().t)
        return points.back().v;

    auto next = std::upper_bound (points.begin(), points.end(), tick, [] (double t, const Point& p) { return t < p.t; });
    const auto& b = *next;
    const auto& a = *(next - 1);
    const double alpha = (tick - a.t) / (double) (b.t - a.t);
    return (float) (a.v + alpha * (b.v - a.v));
}

/** O valor do parâmetro no tick, na unidade dele (para a tela e os testes). */
inline float valueAt (const std::vector<Point>& points, const Range& range, double tick, float fallback)
{
    return points.empty() ? fallback : range.fromNormalised (positionAt (points, tick, 0.0f));
}

} // namespace whynot::automation
