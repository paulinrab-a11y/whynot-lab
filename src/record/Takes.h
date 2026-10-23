#pragma once

// Takes e comping (gravar voz, 21/10; desenho aprovado pelo usuário em revisoes\desenho-takes-comping-2026-10-21.md).
//
// Um grupo de takes são blocos de áudio da playlist com o mesmo PlaylistItem::takeGroup, na mesma raia: as
// voltas de uma gravação em loop, ou as passadas gravadas de novo no mesmo lugar pela mesma trilha do mixer.
// Num trecho, toca o bloco que não está mudo. Cortar corta todos os takes no mesmo ponto, e escolher um
// pedaço cala os outros takes onde ele está. No corte entre takes diferentes, o motor faz um crossfade
// curto (edges). Tudo aqui mexe só no modelo: quem chama é o comando, um passo de desfazer.

#include "../model/Project.h"

#include <set>

namespace whynot::takes
{

/** O crossfade no corte entre dois takes: 10 ms antes do corte (o que entra começa antes e sobe; o que sai desce). */
constexpr double crossfadeSeconds = 0.010;

inline bool overlaps (const PlaylistItem& a, int from, int to) noexcept   { return a.start < to && from < a.end(); }

/** Corta um bloco em `at`, como o playlist.splitItem (o áudio continua de onde o primeiro pedaço parou).
    Devolve o id do pedaço da direita, ou "" se o corte não cai dentro, ou deixaria um pedaço menor que um passo. */
inline juce::String split (Project& p, const juce::String& itemId, int at)
{
    auto* item = p.findItem (itemId);

    if (item == nullptr || at - item->start < ticksPerStep || item->end() - at < ticksPerStep)
        return {};

    PlaylistItem right = *item;
    right.id = makeId ("it");
    right.start = at;
    right.length = item->end() - at;

    if (item->kind == "audio")   // esticado, o arquivo anda 1 / stretch por segundo da playlist
        right.offsetSeconds = roundToMicroseconds (item->offsetSeconds + item->fileSeconds (ticksToSeconds (at - item->start, p.bpm)));
    else
        right.patternOffset = item->patternOffset + (at - item->start);

    item->length = at - item->start;
    p.playlistItems.push_back (right);   // `item` pode mudar de endereço daqui em diante
    return right.id;
}

/** Os blocos de um grupo, na ordem do take e do início. */
inline std::vector<const PlaylistItem*> itemsOf (const Project& p, const juce::String& group)
{
    std::vector<const PlaylistItem*> out;

    for (auto& i : p.playlistItems)
        if (group.isNotEmpty() && i.takeGroup == group)
            out.push_back (&i);

    std::stable_sort (out.begin(), out.end(), [] (const PlaylistItem* a, const PlaylistItem* b)
                      { return a->take != b->take ? a->take < b->take : a->start < b->start; });
    return out;
}

inline int takeCount (const Project& p, const juce::String& group)
{
    std::set<int> numbers;

    for (auto* i : itemsOf (p, group))
        numbers.insert (i->take);

    return (int) numbers.size();
}

/** Corta todos os takes do grupo em `at` (a ferramenta Cortar em cima do grupo). Devolve quantos cortou;
    0 se `at` não cai dentro de nenhum, ou se algum pedaço ficaria menor que um passo (aí não corta nenhum). */
inline int cut (Project& p, const juce::String& group, int at)
{
    juce::StringArray ids;

    for (auto& i : p.playlistItems)
    {
        if (i.takeGroup != group || at <= i.start || at >= i.end())
            continue;

        if (at - i.start < ticksPerStep || i.end() - at < ticksPerStep)
            return 0;

        ids.add (i.id);
    }

    for (auto& id : ids)
        split (p, id, at);

    return ids.size();
}

/** Escolhe o take deste pedaço: ele passa a tocar, e os outros takes do grupo calam onde ele está. Os
    outros são cortados nas pontas dele quando passam delas, para o resto deles seguir tocando. */
inline bool choose (Project& p, const juce::String& itemId)
{
    const auto* chosen = p.findItem (itemId);

    if (chosen == nullptr || chosen->takeGroup.isEmpty())
        return false;

    const auto group = chosen->takeGroup;
    const int from = chosen->start, to = chosen->end();
    juce::StringArray others;

    for (auto& i : p.playlistItems)
        if (i.takeGroup == group && i.id != itemId && overlaps (i, from, to))
            others.add (i.id);

    for (auto& id : others)
    {
        auto piece = id;

        if (const auto right = split (p, piece, from); right.isNotEmpty())
            piece = right;   // a parte de dentro começa em `from`

        split (p, piece, to);
    }

    // cala o que ficou embaixo dele (sobra de menos de um passo, que não dá para cortar, cala junto)
    for (auto& i : p.playlistItems)
        if (i.takeGroup == group && i.id != itemId && i.start >= from - ticksPerStep && i.end() <= to + ticksPerStep
            && overlaps (i, from, to))
            i.muted = true;

    if (auto* c = p.findItem (itemId))
        c->muted = false;

    return true;
}

/** Os takes novos (o mesmo canal, uma volta por bloco, na ordem) entram no grupo do que já foi gravado no
    mesmo lugar pela mesma trilha do mixer; sem grupo ainda, nasce um, com o que já estava como take 1.
    Uma gravação só, sem nada embaixo, não vira grupo. O mais novo toca: cada volta é escolhida na ordem,
    então a última vale onde ela existe e a de antes segue onde a última não chegou. Devolve o grupo. */
inline juce::String adopt (Project& p, const juce::StringArray& newItems)
{
    if (newItems.isEmpty())
        return {};

    const auto* first = p.findItem (newItems[0]);
    const auto* channel = first != nullptr ? p.findChannel (first->ref) : nullptr;

    if (channel == nullptr)
        return {};

    const auto output = p.resolveOutput (channel->output);
    int from = maxTimelineTicks, to = 0;

    for (auto& id : newItems)
        if (auto* i = p.findItem (id))
            from = juce::jmin (from, i->start), to = juce::jmax (to, i->end());

    // o que já foi gravado ali pela mesma trilha: áudio de outro canal, ou deste canal fora destes blocos
    juce::StringArray below;
    juce::String group, lane;

    for (auto& i : p.playlistItems)
    {
        if (i.kind != "audio" || newItems.contains (i.id) || ! overlaps (i, from, to))
            continue;

        const auto* c = p.findChannel (i.ref);

        if (c == nullptr || p.resolveOutput (c->output) != output)
            continue;

        below.add (i.id);

        if (group.isEmpty() && i.takeGroup.isNotEmpty())
        {
            group = i.takeGroup;
            lane = i.track;
        }
    }

    if (below.isEmpty() && newItems.size() < 2)
        return {};

    if (group.isEmpty())
    {
        group = makeId ("tk");
        lane = below.isEmpty() ? first->track : p.findItem (below[0])->track;

        // o que já estava vira take 1, 2...: um número por canal (cada gravação antiga é um arquivo)
        juce::StringArray refs;

        for (auto& id : below)
        {
            auto* i = p.findItem (id);
            refs.addIfNotAlreadyThere (i->ref);
            i->takeGroup = group;
            i->take = refs.indexOf (i->ref) + 1;
            i->track = lane;
        }
    }
    else
    {
        // o que estava embaixo e ainda não era do grupo (gravado antes, sem grupo) entra também
        int next = 0;

        for (auto* i : itemsOf (p, group))
            next = juce::jmax (next, i->take);

        juce::StringArray refs;

        for (auto& id : below)
        {
            auto* i = p.findItem (id);

            if (i->takeGroup == group)
                continue;

            refs.addIfNotAlreadyThere (i->ref);
            i->takeGroup = group;
            i->take = next + refs.indexOf (i->ref) + 1;
            i->track = lane;
        }
    }

    int next = 0;

    for (auto* i : itemsOf (p, group))
        next = juce::jmax (next, i->take);

    for (int k = 0; k < newItems.size(); ++k)
    {
        if (auto* i = p.findItem (newItems[k]))
        {
            i->takeGroup = group;
            i->take = next + k + 1;
            i->track = lane;
        }
    }

    for (auto& id : newItems)
        choose (p, id);

    return group;
}

/** Duas pontas de takes que se encostam sem o áudio continuar (outro take, ou outro ponto do arquivo). */
inline bool continuous (const Project& p, const PlaylistItem& left, const PlaylistItem& right)
{
    return left.ref == right.ref && left.stretch == right.stretch
           && std::abs (right.offsetSeconds - (left.offsetSeconds + left.fileSeconds (ticksToSeconds (left.length, p.bpm)))) < 1.0e-4;
}

/** O crossfade de um bloco do grupo que toca: quanto ele funde em cada ponta, em segundos (na entrada, o
    motor começa o bloco isso antes). 0 onde a ponta não encosta noutro take, ou onde o áudio continua. */
struct Edges
{
    double in = 0.0, out = 0.0;
};

inline Edges edges (const Project& p, const PlaylistItem& item)
{
    Edges e;

    if (item.takeGroup.isEmpty() || item.muted || item.kind != "audio")
        return e;

    for (auto& o : p.playlistItems)
    {
        if (o.takeGroup != item.takeGroup || o.muted || o.id == item.id)
            continue;

        if (const auto* ch = p.findChannel (o.ref); ch == nullptr || ch->mute)
            continue;   // o vizinho não soa (o canal dele está mudo): não há com quem fundir

        if (o.end() == item.start && ! continuous (p, o, item))
            e.in = crossfadeSeconds;

        if (item.end() == o.start && ! continuous (p, item, o))
            e.out = crossfadeSeconds;
    }

    return e;
}

/** O grupo para a tela e a IA (takes.list): os takes e, em cada trecho, qual toca. */
inline juce::var describe (const Project& p, const juce::String& group)
{
    juce::Array<juce::var> items;
    std::set<int> numbers;

    for (auto* i : itemsOf (p, group))
    {
        numbers.insert (i->take);
        auto o = new juce::DynamicObject();
        o->setProperty ("item", i->id);
        o->setProperty ("take", i->take);
        o->setProperty ("start", i->start);
        o->setProperty ("length", i->length);
        o->setProperty ("playing", ! i->muted);
        items.add (juce::var (o));
    }

    auto o = new juce::DynamicObject();
    o->setProperty ("group", group);
    o->setProperty ("takes", (int) numbers.size());
    o->setProperty ("items", items);
    return juce::var (o);
}

} // namespace whynot::takes
