#pragma once

#include <algorithm>

namespace whynot::audio
{

/** Corta um bloco de `total` amostras em pedaços de até `maxSlice` e chama `fn (offset, n)` para
    cada um, em ordem. Serve para processar um bloco maior que um buffer de rascunho já alocado,
    sem alocar e sem desistir do bloco. Com total <= maxSlice é uma chamada só: fn (0, total).
    maxSlice <= 0 (rascunho ainda sem tamanho) não chama nada. Thread de áudio. */
template <typename Fn>
void forEachSlice (int total, int maxSlice, Fn&& fn) noexcept
{
    if (total <= 0 || maxSlice <= 0)
        return;

    for (int done = 0; done < total; done += maxSlice)
        fn (done, std::min (maxSlice, total - done));
}

/** Os eventos de um pedaço do bloco, para quem processa em pedaços um motor que recebe a lista de
    eventos com a posição dentro do bloco (`offset`, um membro int do evento).

    A partir de `cursor`, pega os eventos seguidos que caem antes do fim do pedaço [from, from + n)
    e troca a posição de cada um pela posição dentro do pedaço (de 0 a n - 1; o que ficou para trás
    entra em 0, o mais cedo possível). No último pedaço (`last`) leva todos os que sobraram. Devolve
    quantos pegou: são events[cursor antigo] em diante; `cursor` avança. A ordem da lista não muda,
    então o motor trata os eventos na mesma ordem do bloco inteiro. Thread de áudio, sem alocar. */
template <typename Event>
int takeSliceEvents (Event* events, int numEvents, int& cursor, int from, int n, bool last, int Event::* offset) noexcept
{
    const int first = cursor;

    while (cursor < numEvents && (last || events[cursor].*offset < from + n))
    {
        events[cursor].*offset = std::clamp (events[cursor].*offset - from, 0, std::max (0, n - 1));
        ++cursor;
    }

    return cursor - first;
}

} // namespace whynot::audio
