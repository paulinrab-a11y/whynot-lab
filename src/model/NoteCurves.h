#pragma once

// Pitch bend e mod wheel desenhados na nota (item 17 da pesquisa das comunidades, 26/10): as contas das curvas.
// O projetor (tocar e renderizar) e o MIDI exportado usam as mesmas funções, para soarem iguais; o MIDI importado
// volta para curvas por aqui também.
//
// Cada nota pode ter uma curva de pitch (semitons) e uma de mod (a roda, de 0 a 1), em pontos de tempo desde o
// começo da nota. Antes do primeiro ponto vale o primeiro; depois do último, o último; entre dois, a curva anda reta.
// O pitch wheel e o CC1 são do canal MIDI inteiro: quando duas notas soam juntas, vale a curva da nota que começou
// por último (num acorde que começa junto, a que tem curva).

#include "Project.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace whynot::curves
{

enum class Kind { pitch, mod };

constexpr float maxBendSemis = 24.0f;        // o pitch desenhado vai de -24 a +24 semitons
constexpr int maxPoints = 256;               // por curva
constexpr int maxPointTick = 256 * ticksPerStep;   // a nota mais longa que o .wnl aceita
constexpr int stepTicks = 10;                // um evento a cada 10 ticks enquanto a curva anda (4,5 ms a 140 BPM)
constexpr int wheelCentre = 8192, wheelMax = 16383;

inline const std::vector<CurvePoint>& of (const Note& n, Kind k) noexcept   { return k == Kind::pitch ? n.pitch : n.mod; }
inline std::vector<CurvePoint>& of (Note& n, Kind k) noexcept               { return k == Kind::pitch ? n.pitch : n.mod; }

inline Kind kindFromName (const juce::String& s, bool& ok)
{
    ok = s == "pitch" || s == "mod";
    return s == "mod" ? Kind::mod : Kind::pitch;
}

/** O valor guardado: pitch em centésimos de semitom (o cent), mod em milésimos. Valor ruim vira 0. */
inline float clampValue (float v, Kind k) noexcept
{
    if (! std::isfinite (v))
        return 0.0f;

    return k == Kind::pitch ? std::round (juce::jlimit (-maxBendSemis, maxBendSemis, v) * 100.0f) / 100.0f
                            : std::round (juce::jlimit (0.0f, 1.0f, v) * 1000.0f) / 1000.0f;
}

/** Deixa a curva como o .wnl guarda: t de 0 ao teto, um ponto por t (o último vence), em ordem, até maxPoints. Uma
    curva toda em 0 some (a nota volta a não mexer). Os pontos depois do fim da nota ficam (esticar a nota de volta
    devolve a curva) e só não tocam. */
inline void tidy (std::vector<CurvePoint>& pts, Kind k)
{
    for (auto& p : pts)
    {
        p.t = juce::jlimit (0, maxPointTick, p.t);
        p.v = clampValue (p.v, k);
    }

    std::stable_sort (pts.begin(), pts.end(), [] (const CurvePoint& a, const CurvePoint& b) { return a.t < b.t; });

    std::vector<CurvePoint> out;

    for (auto& p : pts)
    {
        if (! out.empty() && out.back().t == p.t)
            out.back() = p;
        else
            out.push_back (p);
    }

    if (out.size() > (size_t) maxPoints)
        out.resize ((size_t) maxPoints);

    if (std::all_of (out.begin(), out.end(), [] (const CurvePoint& p) { return p.v == 0.0f; }))
        out.clear();

    pts = std::move (out);
}

/** O valor da curva em `t` ticks desde o começo da nota (0 sem curva). */
inline float valueAt (const std::vector<CurvePoint>& pts, double t) noexcept
{
    if (pts.empty())
        return 0.0f;

    if (t <= pts.front().t)
        return pts.front().v;

    for (size_t i = 1; i < pts.size(); ++i)
    {
        const auto& a = pts[i - 1];
        const auto& b = pts[i];

        if (t <= b.t)
            return b.t == a.t ? b.v : a.v + (b.v - a.v) * (float) ((t - a.t) / (double) (b.t - a.t));
    }

    return pts.back().v;
}

/** A curva vista a partir de `offset` ticks (o pedaço da direita de uma nota cortada): começa no valor que ela tinha ali. */
inline std::vector<CurvePoint> from (const std::vector<CurvePoint>& pts, int offset)
{
    if (pts.empty())
        return {};

    std::vector<CurvePoint> out { { 0, valueAt (pts, offset) } };

    for (auto& p : pts)
        if (p.t > offset)
            out.push_back ({ p.t - offset, p.v });

    return out;
}

/** Tira os pontos que uma reta entre os vizinhos já desenha (até `tolerance` de diferença): a curva lida de um MIDI,
    com um evento a cada poucos ticks, volta a ter só os cantos. */
inline std::vector<CurvePoint> simplify (const std::vector<CurvePoint>& pts, float tolerance)
{
    if (pts.size() <= 2)
        return pts;

    std::vector<CurvePoint> out { pts.front() };
    size_t anchor = 0;

    for (size_t next = 2; next < pts.size(); ++next)
    {
        const auto& a = pts[anchor];
        const auto& b = pts[next];
        bool fits = true;

        for (size_t i = anchor + 1; i < next && fits; ++i)
        {
            const double f = b.t == a.t ? 1.0 : (pts[i].t - a.t) / (double) (b.t - a.t);
            fits = std::abs (a.v + (b.v - a.v) * (float) f - pts[i].v) <= tolerance;
        }

        if (! fits)
        {
            anchor = next - 1;
            out.push_back (pts[anchor]);
        }
    }

    out.push_back (pts.back());
    return out;
}

/** Quantos semitons o pitch wheel inteiro dobra no instrumento do canal: o parâmetro "bend" do WHYNOT Synth, o
    bendRange do canal nos outros (o VST3 tem que estar com o mesmo). */
inline float bendRangeFor (const Channel& c)
{
    if (c.isSynth())
    {
        const auto it = c.synthParams.find ("bend");
        return it != c.synthParams.end() ? juce::jlimit (0.0f, maxBendSemis, it->second) : 2.0f;   // 2 = o padrão do synth
    }

    return (float) juce::jlimit (1, 24, c.bendRange);
}

/** Semitons → pitch wheel de 14 bits (8192 = no meio), como o synth lê de volta: (valor - 8192) / 8192 × alcance. */
inline int toWheel (float semis, float range) noexcept
{
    if (! (range > 0.0f) || ! std::isfinite (semis))
        return wheelCentre;

    return juce::jlimit (0, wheelMax, (int) std::lround (wheelCentre + semis / range * wheelCentre));
}

inline float fromWheel (int wheel, float range) noexcept   { return (float) (wheel - wheelCentre) / (float) wheelCentre * range; }
inline int toCc (float v) noexcept                         { return juce::jlimit (0, 127, (int) std::lround (v * 127.0f)); }

/** Um evento de controlador: em ticks (na mesma base das notas) e o valor já em MIDI (pitch wheel de 0 a 16383 ou
    CC1 de 0 a 127). */
struct ControlEvent
{
    int t = 0;
    int value = 0;

    bool operator== (const ControlEvent&) const = default;
};

/** Os eventos que as curvas das notas mandam ao canal, em ordem e sem repetir o valor (`range` = bendRangeFor, só no
    pitch). Em cada momento vale a nota que começou por último entre as que soam; sem curva, o valor de descanso
    (pitch no meio, mod em 0). Toda nota (com ou sem curva) manda o valor dela no começo, mesmo repetido: a volta do
    loop, o clipe seguinte e tocar de novo depois de parar no meio de um bend não herdam o bend de outra nota. Depois
    do fim de uma nota, sem outra soando, o valor fica (a cauda do release não pula de volta, como no FL). Nenhuma
    curva = nenhum evento. */
inline std::vector<ControlEvent> controlEvents (const std::vector<Note>& notes, Kind k, float range = 2.0f)
{
    const bool any = std::any_of (notes.begin(), notes.end(), [k] (const Note& n) { return ! of (n, k).empty() && n.length > 0; });

    if (! any)
        return {};

    auto quantise = [k, range] (float v) { return k == Kind::pitch ? toWheel (v, range) : toCc (v); };
    const int rest = quantise (0.0f);

    // as notas por começo: a dona de cada momento sai de uma busca, não de uma volta por todas
    std::vector<const Note*> byStart;

    for (auto& n : notes)
        if (n.length > 0)
            byStart.push_back (&n);

    std::stable_sort (byStart.begin(), byStart.end(), [] (const Note* a, const Note* b) { return a->start < b->start; });
    int longest = 0;

    for (auto* n : byStart)
        longest = std::max (longest, n->length);

    std::set<int> times, corners;

    for (auto* np : byStart)
    {
        const auto& n = *np;
        times.insert (n.start);
        times.insert (n.end());   // outra nota que ainda soa volta a mandar
        const auto& pts = of (n, k);

        if (pts.empty())
            continue;

        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (pts[i].t < n.length)
                times.insert (n.start + pts[i].t);

            // enquanto a curva anda, um evento a cada stepTicks; o ponto onde ela começa a andar manda o valor mesmo
            // repetido (quem lê o MIDI vê onde a rampa começa, não uma rampa desde o evento de antes)
            if (i + 1 < pts.size() && pts[i + 1].v != pts[i].v)
            {
                if (pts[i].t < n.length)
                    corners.insert (n.start + pts[i].t);

                for (int t = pts[i].t + stepTicks; t < pts[i + 1].t && t < n.length; t += stepTicks)
                    times.insert (n.start + t);
            }
        }
    }

    std::vector<ControlEvent> out;
    int last = rest;

    for (int t : times)
    {
        const Note* owner = nullptr;

        // de trás para a frente, a partir da última que começou até t: a primeira que soa é a dona; no empate de
        // começo, a que tem curva
        auto it = std::upper_bound (byStart.begin(), byStart.end(), t, [] (int value, const Note* n) { return value < n->start; });

        while (it != byStart.begin())
        {
            const Note& n = **--it;

            if (owner != nullptr && n.start < owner->start)
                break;

            if (n.start < t - longest)
                break;

            if (n.end() > t && (owner == nullptr || (of (*owner, k).empty() && ! of (n, k).empty())))
                owner = &n;
        }

        if (owner == nullptr)
            continue;   // nenhuma nota soa: o valor fica

        const int value = quantise (valueAt (of (*owner, k), t - owner->start));

        if (value != last || owner->start == t || corners.count (t) != 0)
            out.push_back ({ t, value });

        last = value;
    }

    return out;
}

/** Os pontos de um comando ([{ t, v }], t em ticks vezes `tickScale`, a unidade do comando), já arrumados. Ponto
    sem t ou sem v, ou de tipo errado, fica de fora; `bad` conta quantos. */
inline std::vector<CurvePoint> pointsFromVar (const juce::var& list, double tickScale, Kind k, int& bad)
{
    std::vector<CurvePoint> pts;
    bad = 0;

    if (auto* arr = list.getArray())
    {
        for (auto& p : *arr)
        {
            const auto& t = p["t"];
            const auto& v = p["v"];
            const bool numbers = (t.isInt() || t.isInt64() || t.isDouble()) && (v.isInt() || v.isInt64() || v.isDouble());
            const double ticks = numbers ? (double) t * tickScale : -1.0;

            if (! numbers || ! std::isfinite (ticks) || ticks < 0.0 || ! std::isfinite ((double) v))
            {
                ++bad;
                continue;
            }

            // o valor limitado ainda em double: 1e300 não vira infinito (e 0) no float, vira o teto
            pts.push_back ({ (int) std::lround (std::min (ticks, (double) maxPointTick)), (float) juce::jlimit (-1.0e6, 1.0e6, (double) v) });
        }
    }

    tidy (pts, k);
    return pts;
}

/** O maior bend (em semitons, sem sinal) desenhado nas notas: para escolher o alcance do MIDI exportado. */
inline float maxBend (const std::vector<Note>& notes) noexcept
{
    float m = 0.0f;

    for (auto& n : notes)
        for (auto& p : n.pitch)
            m = std::max (m, std::abs (p.v));

    return m;
}

} // namespace whynot::curves
