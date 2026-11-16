// Bloco por canal (16/11): a escolha da raia livre (src/model/LaneStack.h), sem JUCE. "Separar por canal" e
// "pôr na playlist (só este canal)" não pisam nos blocos que já estão na playlist.

#include "../../src/model/LaneStack.h"

#include <cstdio>
#include <random>
#include <vector>

using namespace whynot::lanestack;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static void testFirstFree()
{
    std::printf ("A primeira raia livre\n");

    const std::vector<Busy> none;
    CHECK (firstFree (none, 0, 0, 96, 12) == 0, "playlist vazia: a primeira raia");
    CHECK (firstFree (none, 5, 0, 96, 12) == 5, "playlist vazia, a partir da raia 6: a raia 6");
    CHECK (firstFree (none, -3, 0, 96, 12) == 0, "raia negativa vale a primeira");

    const std::vector<Busy> busy { { 0, 0, 96 }, { 1, 48, 144 }, { 2, 96, 192 }, { 3, 0, 10 } };
    CHECK (firstFree (busy, 0, 0, 96, 12) == 2, "o bloco que encosta na borda (começa onde o trecho acaba) não ocupa");
    CHECK (firstFree (busy, 0, 96, 192, 12) == 0, "depois do fim do bloco da raia 1 ela está livre");
    CHECK (firstFree (busy, 0, 0, 200, 12) == 4, "trecho longo: desce até a raia sem nada");
    CHECK (firstFree (busy, 0, 5, 6, 12) == 1, "trecho de um tick dentro de um bloco: a raia de baixo");
    CHECK (firstFree (busy, 0, 0, 200, 4) == -1, "sem raia livre até o teto: -1");
    CHECK (firstFree (busy, 0, 0, 200, 5) == 4, "a raia que ainda não existe conta como livre, até o teto");
    CHECK (isFree (busy, 3, 10, 20) && ! isFree (busy, 3, 9, 20), "isFree: a borda é aberta no fim");
}

static void testStack()
{
    std::printf ("Empilhar blocos\n");

    const std::vector<Busy> none;
    CHECK ((stack (none, 2, 0, 96, 3, 12) == std::vector<int> { 2, 3, 4 }), "sem nada no caminho: raias seguidas, a partir da pedida");
    CHECK (stack (none, 0, 0, 96, 0, 12).empty(), "zero blocos: nada");

    // o próprio bloco que vai ser separado não entra na lista (quem chama tira): a raia dele está livre
    const std::vector<Busy> busy { { 3, 0, 96 }, { 4, 90, 100 }, { 6, 200, 300 } };
    CHECK ((stack (busy, 2, 0, 96, 4, 12) == std::vector<int> { 2, 5, 6, 7 }), "pula as raias ocupadas no trecho, na ordem");
    CHECK ((stack (busy, 3, 0, 96, 2, 12) == std::vector<int> { 5, 6 }), "a raia pedida ocupada: o primeiro também desce");
    CHECK (stack (busy, 2, 0, 96, 4, 7).empty(), "não cabem todos até o teto: nenhum é colocado");
    CHECK ((stack (busy, 2, 0, 96, 4, 8) == std::vector<int> { 2, 5, 6, 7 }), "cabem exatamente no teto");
}

static void testFuzz()
{
    std::printf ("Sorteio\n");

    std::mt19937 rng (20261116u);
    bool sound = true, ordered = true, minimal = true;

    for (int round = 0; round < 20000 && sound && ordered && minimal; ++round)
    {
        std::vector<Busy> busy;
        const int blocks = (int) (rng() % 30);

        for (int k = 0; k < blocks; ++k)
        {
            const int start = (int) (rng() % 400);
            busy.push_back ({ (int) (rng() % 10), start, start + 1 + (int) (rng() % 200) });
        }

        const int from = (int) (rng() % 10), start = (int) (rng() % 400), end = start + 1 + (int) (rng() % 200);
        const int count = (int) (rng() % 6), maxLanes = 8 + (int) (rng() % 12);
        const auto lanes = stack (busy, from, start, end, count, maxLanes);

        if (lanes.empty())
        {
            // só fica vazio sem pedido, ou quando faltam raias livres de `from` até o teto
            int freeLanes = 0;

            for (int lane = from; lane < maxLanes; ++lane)
                freeLanes += isFree (busy, lane, start, end) ? 1 : 0;

            minimal = minimal && (count == 0 || freeLanes < count);
            continue;
        }

        sound = sound && (int) lanes.size() == count;
        int previous = from - 1;

        for (int lane : lanes)
        {
            sound = sound && lane >= from && lane < maxLanes && isFree (busy, lane, start, end);
            ordered = ordered && lane > previous;

            // nenhuma raia livre foi pulada entre o bloco anterior e este
            for (int skipped = previous + 1; skipped < lane; ++skipped)
                minimal = minimal && ! isFree (busy, skipped, start, end);

            previous = lane;
        }
    }

    CHECK (sound, "20 mil playlists sorteadas: toda raia escolhida está livre no trecho e dentro do teto");
    CHECK (ordered, "as raias da pilha descem, uma por bloco");
    CHECK (minimal, "nenhuma raia livre é pulada, e a pilha só falha quando faltam raias");
}

int main()
{
    testFirstFree();
    testStack();
    testFuzz();

    std::printf (failures == 0 ? "\nTUDO OK: 0 falha(s)\n" : "\n%d FALHA(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
