#pragma once

// Edição de blocos da playlist que mais de um comando usa (voz na playlist, 27/10): o corte da ferramenta
// Cortar (playlist.splitItem), o picotar (playlist.chopItem), o corte dos takes (takes::split) e a edição por
// trecho de tempo (cortar na agulha, apagar o trecho puxando o resto, abrir espaço). Mexe só no modelo: quem
// chama é o comando, um passo de desfazer.

#include "Project.h"

#include <set>

namespace whynot::playlistedit
{

/** Corta `item` em `at` (ticks da playlist, já conferido dentro do bloco) e devolve o pedaço da direita, que o
    chamador põe na playlist com um id novo. O da direita continua de onde o primeiro parou: o áudio pelo
    offsetSeconds (esticado, o arquivo anda 1 / stretch por segundo da playlist), o padrão pelo patternOffset.

    Os fades das alças ficam nas pontas de fora (bug de 26/10: os dois pedaços herdavam os dois fades, e o som
    afundava no ponto do corte). O da esquerda guarda o fade de entrada e o da direita o de saída; no ponto do
    corte nenhum dos dois tem fade, e o som continua amostra por amostra. Cada fade cabe no pedaço dele. */
inline PlaylistItem splitAt (const Project& p, PlaylistItem& item, int at)
{
    PlaylistItem right = item;
    right.start = at;
    right.length = item.end() - at;

    if (item.kind == "audio")
    {
        right.offsetSeconds = roundToMicroseconds (item.offsetSeconds + item.fileSeconds (ticksToSeconds (at - item.start, p.bpm)));
        right.fadeIn = 0.0;
        right.fadeOut = juce::jmin (item.fadeOut, ticksToSeconds (right.length, p.bpm));
        item.fadeOut = 0.0;
        item.fadeIn = juce::jmin (item.fadeIn, ticksToSeconds (at - item.start, p.bpm));
    }
    else
    {
        right.patternOffset = item.patternOffset + (at - item.start);
    }

    item.length = at - item.start;
    return right;
}

/** Corta em `at` todos os blocos que passam por ele (ou só os de `only`, se não for vazio). Devolve os ids dos
    pedaços da direita, na ordem da playlist. Um bloco que começa ou acaba em `at` não é cortado. */
inline juce::StringArray cutAt (Project& p, int at, const std::set<juce::String>& only = {})
{
    juce::StringArray rights;
    const size_t count = p.playlistItems.size();   // os pedaços novos vão para o fim e não são vistos de novo

    for (size_t i = 0; i < count; ++i)
    {
        auto& item = p.playlistItems[i];

        if (at <= item.start || at >= item.end() || (! only.empty() && only.count (item.id) == 0))
            continue;

        auto right = splitAt (p, item, at);
        right.id = makeId ("it");
        rights.add (right.id);
        p.playlistItems.push_back (right);   // `item` pode mudar de endereço daqui em diante
    }

    return rights;
}

/** O que um trecho de tempo mexeu: blocos cortados, apagados e deslocados. */
struct SpaceEdit
{
    int cut = 0, removed = 0, moved = 0;
};

/** Apaga o trecho [from, to) da música inteira e puxa o que vem depois para a esquerda (ripple), como apagar
    um pedaço da fita: os blocos que atravessam as bordas são cortados nelas, o que fica dentro sai, e o que
    vem depois (blocos, marcadores e pontos de automação) anda `to - from` para trás. */
inline SpaceEdit removeSpace (Project& p, int from, int to)
{
    SpaceEdit e;

    if (to <= from)
        return e;

    const int gap = to - from;
    e.cut = cutAt (p, from).size() + cutAt (p, to).size();
    const auto before = p.playlistItems.size();
    std::erase_if (p.playlistItems, [from, to] (const PlaylistItem& i) { return i.start >= from && i.end() <= to; });
    e.removed = (int) (before - p.playlistItems.size());

    for (auto& i : p.playlistItems)
        if (i.start >= to)
            i.start -= gap, ++e.moved;

    std::erase_if (p.markers, [from, to] (const Marker& m) { return m.t >= from && m.t < to; });

    for (auto& m : p.markers)
        if (m.t >= to)
            m.t -= gap;

    for (auto& lane : p.automation)
    {
        std::erase_if (lane.points, [from, to] (const AutomationPoint& pt) { return pt.t >= from && pt.t < to; });

        for (auto& pt : lane.points)
            if (pt.t >= to)
                pt.t -= gap;
    }

    return e;
}

/** Abre um espaço vazio de `length` ticks em `at`: os blocos que passam por `at` são cortados ali, e tudo o
    que começa em `at` ou depois (blocos, marcadores e pontos de automação) anda `length` para a frente.
    Devolve false sem mexer em nada se algum bloco passaria do fim da linha do tempo. */
inline bool insertSpace (Project& p, int at, int length, SpaceEdit& e)
{
    for (auto& i : p.playlistItems)
        if (i.end() > at && i.end() + length > maxTimelineTicks)
            return false;

    e.cut = cutAt (p, at).size();

    for (auto& i : p.playlistItems)
        if (i.start >= at)
            i.start += length, ++e.moved;

    for (auto& m : p.markers)
        if (m.t >= at)
            m.t += length;

    for (auto& lane : p.automation)
        for (auto& pt : lane.points)
            if (pt.t >= at)
                pt.t += length;

    return true;
}

/** O ganho de um fade de entrada no ponto `alpha` (0 = começo, 1 = fim) para cada curva (FadeCurve), como o
    motor toca: a reta, a rápida (um quarto de seno, a de mesma potência), a lenta e a em S. O de saída é o espelho. */
inline float fadeCurveGain (int curve, float alpha) noexcept
{
    const float a = juce::jlimit (0.0f, 1.0f, alpha);
    constexpr float halfPi = juce::MathConstants<float>::halfPi;

    return curve == fadeCurveConvex ? std::sin (a * halfPi)
         : curve == fadeCurveConcave ? 1.0f - std::cos (a * halfPi)
         : curve == fadeCurveS ? (1.0f - a) * (1.0f - std::cos (a * halfPi)) + a * std::sin (a * halfPi)
         : a;
}

/** O crossfade automático de um bloco de áudio, em segundos da playlist: quando dois áudios que tocam se
    sobrepõem na mesma raia, o que acaba desce e o que começa sobe durante a sobra, com a curva de mesma
    potência. Um bloco inteiro dentro de outro é camada, não crossfade. */
struct Crossfade
{
    double in = 0.0, out = 0.0;
};

inline Crossfade autoCrossfade (const Project& p, const PlaylistItem& item)
{
    Crossfade x;

    if (item.kind != "audio" || ! p.itemSounds (item))
        return x;

    for (auto& o : p.playlistItems)
    {
        if (o.id == item.id || o.kind != "audio" || o.track != item.track || ! p.itemSounds (o))
            continue;

        if (o.start > item.start && o.start < item.end() && o.end() > item.end())   // o outro entra no fim deste
            x.out = juce::jmax (x.out, ticksToSeconds (item.end() - o.start, p.bpm));

        if (item.start > o.start && item.start < o.end() && item.end() > o.end())   // este entra no fim do outro
            x.in = juce::jmax (x.in, ticksToSeconds (o.end() - item.start, p.bpm));
    }

    const double length = ticksToSeconds (item.length, p.bpm);
    x.in = juce::jmin (x.in, length);
    x.out = juce::jmin (x.out, length - x.in);
    return x;
}

} // namespace whynot::playlistedit
