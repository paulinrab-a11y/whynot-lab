#pragma once

// Bloco por canal (16/11): em que raia da playlist entra um bloco novo sem pisar nos que já estão lá.
//
// A playlist deixa dois blocos na mesma raia e no mesmo trecho (colar e duplicar põem onde a pessoa mandou), mas
// "separar por canal" e "pôr na playlist (só este canal)" escolhem a raia sozinhos, então escolhem uma livre:
// - livre = nenhum bloco da raia cruza o trecho [start, end) (encostar na borda não é cruzar);
// - o primeiro bloco de uma pilha fica na raia pedida se ela estiver livre, senão desce até achar uma;
// - cada bloco seguinte vai para a primeira raia livre ABAIXO do anterior: a pilha sai na ordem dos canais;
// - a raia que ainda não existe conta como livre (quem chama cria), até `maxLanes`.
// Só contas com inteiros, sem JUCE: o teste é tests/model/test_lane_stack.cpp.

#include <vector>

namespace whynot::lanestack
{

/** Um bloco que já está na playlist: o índice da raia e o trecho, em ticks. */
struct Busy
{
    int lane = 0;
    int start = 0;
    int end = 0;
};

inline bool isFree (const std::vector<Busy>& busy, int lane, int start, int end) noexcept
{
    for (auto& b : busy)
        if (b.lane == lane && b.start < end && start < b.end)
            return false;

    return true;
}

/** A primeira raia livre no trecho, de `from` para baixo; -1 se não cabe em `maxLanes` raias. */
inline int firstFree (const std::vector<Busy>& busy, int from, int start, int end, int maxLanes) noexcept
{
    for (int lane = from < 0 ? 0 : from; lane < maxLanes; ++lane)
        if (isFree (busy, lane, start, end))
            return lane;

    return -1;
}

/** As raias de `count` blocos do mesmo trecho, empilhados a partir de `from`. Vazio se não couberem todos
    em `maxLanes` raias (nada é colocado pela metade). */
inline std::vector<int> stack (const std::vector<Busy>& busy, int from, int start, int end, int count, int maxLanes)
{
    std::vector<int> lanes;
    int next = from;

    for (int k = 0; k < count; ++k)
    {
        const int lane = firstFree (busy, next, start, end, maxLanes);

        if (lane < 0)
            return {};

        lanes.push_back (lane);
        next = lane + 1;
    }

    return lanes;
}

} // namespace whynot::lanestack
