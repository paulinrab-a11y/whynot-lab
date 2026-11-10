// Testes da lógica da tela que não precisa do JUCE (frente ui-atualizacao, 10/11):
// - quais telas atualizar dada a máscara da mudança e o que está à vista (RefreshPlan.h);
// - o que fazer quando a placa de som some e volta (DeviceWatch.h).

#include "../../src/model/ProjectChange.h"
#include "../../src/ui/DeviceWatch.h"
#include "../../src/ui/RefreshPlan.h"

#include <cstdio>

using namespace whynot;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static void testScreensFor()
{
    using namespace refreshplan;
    std::printf ("Telas por tipo de mudança\n");

    CHECK (screensFor (change::all) == allScreens, "tudo: as quatro telas");
    CHECK (screensFor (0) == allScreens, "máscara vazia vale tudo (ninguém fica com tela velha)");
    CHECK (screensFor (change::patterns) == (rack | pianoRoll | playlist), "padrões: rack, piano roll e playlist (as prévias); o mixer não");
    CHECK (screensFor (change::playlist) == playlist, "playlist: só a playlist");
    CHECK (screensFor (change::mixer) == (rack | playlist | mixer), "mixer: mixer, rack (nome da saída) e playlist (curvas); o piano roll não");
    CHECK (screensFor (change::channels) == allScreens, "canais: as quatro");
    CHECK (screensFor (change::transport) == (rack | pianoRoll | playlist), "transporte e projeto: tudo menos o mixer");
    CHECK (screensFor (change::patterns | change::mixer) == allScreens, "duas mudanças juntas: a soma das telas");
    CHECK (screensFor (1u << 20) == playlist, "bit que ninguém conhece: pelo menos a playlist (depende de tudo)");

    // toda mudança conhecida chega em alguma tela, e nenhuma devolve tela que não existe
    bool sane = true;

    for (unsigned m = 1; m < 32; ++m)
        sane = sane && screensFor (m) != 0 && (screensFor (m) & ~allScreens) == 0;

    CHECK (sane, "as 31 combinações dos cinco bits: sempre alguma tela, nunca uma tela inventada");
}

static void testPlan()
{
    using namespace refreshplan;
    std::printf ("Tela escondida fica pendente\n");

    auto rackAndMixer = visibleScreens (true, false, false, true);   // sem const: o /W4 reclama de condição constante
    CHECK (rackAndMixer == (rack | mixer), "rack em cima e mixer embaixo: os dois à vista");
    CHECK (visibleScreens (false, false, false, false) == 0, "nada à vista (tela de projetos, mixer recolhido)");

    // arrastar uma nota com o piano roll na tela e o mixer aberto
    const auto pianoAndMixer = visibleScreens (false, true, false, true);
    auto plan = onChange (0, change::all, pianoAndMixer);
    CHECK (plan.now == (pianoRoll | mixer), "mudança sem máscara: atualiza na hora só o que está à vista");
    CHECK (plan.pending == (rack | playlist), "rack e playlist ficam pendentes");

    plan = onChange (plan.pending, change::patterns, pianoAndMixer);
    CHECK (plan.now == pianoRoll, "nota arrastada (padrões): só o piano roll; o mixer nem é chamado");
    CHECK (plan.pending == (rack | playlist), "as pendentes continuam pendentes (não acumulam trabalho)");

    // troca de aba para a playlist: a pendente sai, o rack continua esperando
    plan = split (plan.pending, visibleScreens (false, false, true, true));
    CHECK (plan.now == playlist, "a playlist apareceu: atualiza ela");
    CHECK (plan.pending == rack, "o rack continua pendente");

    // captura ou autoteste: tudo de uma vez
    plan = split (plan.pending, allScreens);
    CHECK (plan.now == rack && plan.pending == 0, "flush: o que faltava sai, nada fica pendente");

    plan = split (0, allScreens);
    CHECK (plan.now == 0 && plan.pending == 0, "flush sem pendência: não chama ninguém");

    // mixer recolhido: mexer no volume não atualiza o mixer agora, e ele não se perde
    plan = onChange (0, change::mixer, visibleScreens (true, false, false, false));
    CHECK (plan.now == rack && plan.pending == (playlist | mixer), "mixer recolhido: fica pendente até o F9");

    // nada se perde: o que entra sai agora ou fica pendente, para qualquer combinação
    bool conserved = true;

    for (unsigned pending = 0; pending < 16; ++pending)
        for (unsigned visible = 0; visible < 16; ++visible)
            for (unsigned m = 0; m < 32; ++m)
            {
                const auto r = onChange (pending, m, visible);
                const auto wanted = pending | screensFor (m);
                conserved = conserved && (r.now | r.pending) == wanted && (r.now & r.pending) == 0
                            && (r.now & ~visible) == 0 && (r.pending & visible) == 0;
            }

    CHECK (conserved, "8192 combinações: agora + pendente = o pedido, sem repetir, e só o visível sai agora");
}

static void testDeviceWatch()
{
    using namespace devicewatch;
    std::printf ("Placa de som que some e volta\n");

    auto facts = [] (bool watching, bool lost, bool open, bool isWatched, bool listed, bool explicitOther = false)
    {
        Facts f;
        f.watching = watching;
        f.lost = lost;
        f.open = open;
        f.isWatched = isWatched;
        f.watchedListed = listed;
        f.explicitOther = explicitOther;
        return f;
    };

    CHECK (decide (facts (false, false, false, false, false)) == Action::none, "sem placa desde o começo (autoteste): nada");
    CHECK (decide (facts (false, false, true, false, false)) == Action::track, "a primeira placa abriu: passa a vigiar");
    CHECK (decide (facts (true, false, true, true, true)) == Action::track, "a mesma placa (mudou o buffer): só guarda o ajuste");
    CHECK (decide (facts (true, false, true, false, true)) == Action::track, "a pessoa escolheu outra saída e a antiga continua na lista: vigia a nova, sem aviso");
    CHECK (decide (facts (true, false, false, false, true)) == Action::track, "a pessoa fechou a saída (nenhuma): sem aviso de placa desconectada");
    CHECK (decide (facts (true, false, false, false, false)) == Action::lostNoSound, "a placa saiu da lista e nada abriu: erro na hora");
    CHECK (decide (facts (true, false, true, false, false)) == Action::lostFallback, "a placa saiu e o som foi para outra saída: aviso na hora");
    CHECK (decide (facts (true, true, false, false, false)) == Action::none, "esperando e ela ainda não voltou: nada (não repete o aviso)");
    CHECK (decide (facts (true, true, true, false, false)) == Action::none, "esperando, com o som na reserva: nada");
    CHECK (decide (facts (true, true, true, false, true)) == Action::reopen, "ela voltou para a lista: reabrir nela");
    CHECK (decide (facts (true, true, false, false, true)) == Action::reopen, "voltou e estava sem som: reabrir nela");
    CHECK (decide (facts (true, true, true, true, true)) == Action::returned, "já está aberta de novo: só avisar que voltou");
    CHECK (decide (facts (true, true, true, false, true, true)) == Action::track, "voltou, mas a pessoa escolheu outra enquanto esperava: não puxa de volta");
    CHECK (decide (facts (true, true, true, false, false, true)) == Action::track, "escolheu outra enquanto esperava: para de esperar");
    CHECK (decide (facts (true, false, true, false, false, true)) == Action::lostFallback, "a escolha guardada só conta enquanto espera");
}

int main()
{
    testScreensFor();
    testPlan();
    testDeviceWatch();

    std::printf (failures == 0 ? "\nui: tudo ok\n" : "\nui: %d FALHA(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
