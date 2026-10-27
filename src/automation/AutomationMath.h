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

/** Um ponto da curva: num tick da música, a posição do knob (0 a 1 na faixa do parâmetro), e a tensão do
    trecho que sai dele até o próximo ponto (-1 a 1, 0 = reta; o do último ponto não vale nada). */
struct Point
{
    int t = 0;
    float v = 0.0f;
    float tension = 0.0f;

    bool operator== (const Point&) const = default;
};

/** Quanto do caminho entre dois pontos a curva já andou, com a tensão: alpha (0 a 1) é o tempo andado no trecho.
    Tensão positiva segura no valor do começo e corre no fim; negativa corre no começo e assenta no fim; 0 é reta.
    É alpha elevado a 2^(3 * tensão): na tensão 1, o meio do tempo anda 1/256 do caminho; na -1, 92%. */
inline double shaped (double alpha, float tension) noexcept
{
    alpha = std::clamp (alpha, 0.0, 1.0);

    if (! std::isfinite (tension) || std::abs (tension) < 1.0e-4f)
        return alpha;

    return std::pow (alpha, std::exp2 (3.0 * (double) std::clamp (tension, -1.0f, 1.0f)));
}

/** A tensão que põe o meio do trecho (no tempo) em `fraction` do caminho (0 a 1): o inverso do shaped no meio. */
inline float tensionForMidpoint (double fraction) noexcept
{
    constexpr double lo = 0.00390625, hi = 0.917004;   // 0,5^8 e 0,5^(1/8): as pontas da tensão
    fraction = std::isfinite (fraction) ? std::clamp (fraction, lo, hi) : 0.5;
    const double exponent = std::log (fraction) / std::log (0.5);
    return (float) std::clamp (std::log2 (exponent) / 3.0, -1.0, 1.0);
}

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
        p.tension = std::isfinite (p.tension) ? std::clamp (p.tension, -1.0f, 1.0f) : 0.0f;

        if (! out.empty() && out.back().t == p.t)
            out.back() = p;
        else
            out.push_back (p);
    }

    return out;
}

/** A posição do knob no tick: entre dois pontos, reta ou dobrada pela tensão do primeiro. Antes do primeiro
    ponto vale o primeiro; depois do último, o último. Sem pontos, fallback. O Tracktion interpola reto: o que ele
    recebe é o bake() destes pontos, que segue a tensão de perto. */
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
    const double alpha = shaped ((tick - a.t) / (double) (b.t - a.t), a.tension);
    return (float) (a.v + alpha * (b.v - a.v));
}

/** Os pontos para quem só interpola reto (o Tracktion): cada trecho com tensão vira `pieces` retas que seguem a
    curva (com 32, o erro fica abaixo de 1% do knob). Os trechos retos ficam como estão. Sai sem tensão. */
inline std::vector<Point> bake (const std::vector<Point>& points, int pieces = 32)
{
    std::vector<Point> out;
    out.reserve (points.size());

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto& a = points[i];
        out.push_back ({ a.t, a.v });

        if (i + 1 >= points.size() || std::abs (a.tension) < 1.0e-4f)
            continue;

        const auto& b = points[i + 1];
        const int span = b.t - a.t;
        const int n = std::min (pieces, span);   // um ponto por tick, no máximo

        for (int k = 1; k < n; ++k)
        {
            const int t = a.t + (int) std::lround ((double) span * k / n);

            if (t <= out.back().t || t >= b.t)
                continue;

            const double alpha = shaped ((double) (t - a.t) / span, a.tension);
            out.push_back ({ t, (float) (a.v + alpha * (b.v - a.v)) });
        }
    }

    return out;
}

/** Tira os pontos que a reta entre os vizinhos já explica (Ramer-Douglas-Peucker na vertical): a gravação mede o
    knob a cada 20 ms, e a curva guarda só o que muda o som. tolerance é em posição do knob (0,004 = 0,4%). O primeiro
    e o último ficam sempre. A entrada precisa estar em ordem de t. */
inline std::vector<Point> reduce (const std::vector<Point>& points, float tolerance = 0.004f)
{
    if (points.size() <= 2)
        return points;

    std::vector<bool> keep (points.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> stack { { 0, points.size() - 1 } };

    while (! stack.empty())
    {
        const auto [first, last] = stack.back();
        stack.pop_back();

        if (last <= first + 1)
            continue;

        const auto& a = points[first];
        const auto& b = points[last];
        float worst = -1.0f;
        size_t at = first;

        for (size_t i = first + 1; i < last; ++i)
        {
            const double alpha = b.t > a.t ? (double) (points[i].t - a.t) / (b.t - a.t) : 0.0;
            const float d = std::abs (points[i].v - (float) (a.v + alpha * (b.v - a.v)));

            if (d > worst)
            {
                worst = d;
                at = i;
            }
        }

        if (worst > tolerance)
        {
            keep[at] = true;
            stack.push_back ({ first, at });
            stack.push_back ({ at, last });
        }
    }

    std::vector<Point> out;

    for (size_t i = 0; i < points.size(); ++i)
        if (keep[i])
            out.push_back (points[i]);

    return out;
}

/** Uma passada gravada por cima da curva: o trecho [from, to] passa a ser `written`, e fora dele a curva soa como
    antes. Se havia curva, entram dois pontos de emenda com o valor antigo (em from - 1 e to + 1), para o trecho
    antigo não virar uma rampa até o primeiro ponto novo; um trecho com tensão cortado pela emenda vira retas (o
    bake), que seguem a curva antiga de perto. Curva vazia: só os pontos novos (o último segura). */
inline std::vector<Point> replaceRange (const std::vector<Point>& old, const std::vector<Point>& written, int from, int to)
{
    if (written.empty() || to < from)
        return old;

    // os trechos com tensão que a emenda corta viram retas (bake): a parte que fica soa como antes
    auto kept = old;

    for (const int cut : { from - 1, to + 1 })
    {
        for (size_t i = 0; i + 1 < kept.size(); ++i)
        {
            if (kept[i].t < cut && cut < kept[i + 1].t && std::abs (kept[i].tension) >= 1.0e-4f)
            {
                auto piece = bake ({ kept[i], kept[i + 1] });
                piece.back().tension = kept[i + 1].tension;   // o trecho seguinte continua curvo
                kept.erase (kept.begin() + (std::ptrdiff_t) i, kept.begin() + (std::ptrdiff_t) i + 2);
                kept.insert (kept.begin() + (std::ptrdiff_t) i, piece.begin(), piece.end());
                break;
            }
        }
    }

    std::vector<Point> out;

    for (auto& p : kept)
        if (p.t < from - 1)
            out.push_back (p);

    if (! old.empty() && from > 0)
        out.push_back ({ from - 1, positionAt (kept, from - 1, 0.0f), 0.0f });

    for (auto& p : written)
        if (p.t >= from && p.t <= to)
            out.push_back ({ p.t, p.v, 0.0f });

    if (! old.empty())
    {
        // um ponto antigo bem na emenda fica (com a tensão dele); senão, a emenda com o valor antigo
        if (std::none_of (kept.begin(), kept.end(), [to] (const Point& p) { return p.t == to + 1; }))
            out.push_back ({ to + 1, positionAt (kept, to + 1, 0.0f), 0.0f });

        for (auto& p : kept)
            if (p.t >= to + 1)
                out.push_back (p);
    }

    return out;
}

/** O valor do parâmetro no tick, na unidade dele (para a tela e os testes). */
inline float valueAt (const std::vector<Point>& points, const Range& range, double tick, float fallback)
{
    return points.empty() ? fallback : range.fromNormalised (positionAt (points, tick, 0.0f));
}

} // namespace whynot::automation
