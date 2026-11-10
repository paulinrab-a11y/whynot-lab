// Carregamento assíncrono de samples (sem JUCE): um trabalho por arquivo, o lote de cada canal com geração,
// o pedido obsoleto descartado e a entrega entre duas threads de verdade.
// Uso: whynot_sample_loads_tests

#include "../../src/engine/SampleLoads.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace whynot::loads;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

using Flight = InFlight<std::string, int, int>;   // chave, resultado, quem espera (um número)

//==============================================================================
static void testOneJobPerKey()
{
    std::printf ("\nUm trabalho por arquivo\n");
    Flight flight;

    CHECK (flight.request ("kick.wav", 1), "o primeiro pedido do arquivo manda decodificar");
    CHECK (! flight.request ("kick.wav", 2), "o segundo pedido do mesmo arquivo so entra na fila");
    CHECK (! flight.request ("kick.wav", 3), "o terceiro tambem");
    CHECK (flight.request ("snare.wav", 4), "outro arquivo e outro trabalho");
    CHECK (flight.size() == 2 && flight.running() == 2, "dois trabalhos em voo (%d, %d)", (int) flight.size(), (int) flight.running());
    CHECK (flight.drain().empty(), "nada pronto: o drain sai vazio");

    flight.complete ("kick.wav", 111);
    CHECK (flight.running() == 1 && flight.size() == 2, "o pronto espera o drain (rodando %d, guardados %d)",
           (int) flight.running(), (int) flight.size());

    // entre o trabalho ficar pronto e a thread de mensagem recolher, um pedido novo pega o mesmo resultado
    CHECK (! flight.request ("kick.wav", 5), "pedido que chega com o trabalho pronto nao decodifica de novo");

    flight.complete ("kick.wav", 999);   // entrega repetida: ignorada
    auto done = flight.drain();
    CHECK (done.size() == 1 && done[0].key == "kick.wav" && done[0].result == 111, "o drain entrega o kick com o primeiro resultado");
    CHECK (done.size() == 1 && done[0].waiters == std::vector<int> ({ 1, 2, 3, 5 }), "os quatro que pediram sao avisados, na ordem");
    CHECK (flight.size() == 1, "o snare continua em voo");

    CHECK (flight.request ("kick.wav", 6), "depois do drain, o arquivo pode ser pedido de novo (mudou no disco)");
    flight.complete ("fantasma.wav", 1);   // chave que ninguém pediu
    CHECK (flight.size() == 2, "entrega de chave desconhecida nao cria trabalho");

    CHECK (! flight.waitUntilIdle (std::chrono::milliseconds (20)), "com trabalho rodando, a espera acaba no tempo dado");
    flight.cancelAll();
    CHECK (flight.size() == 0 && flight.waitUntilIdle (std::chrono::milliseconds (0)), "cancelar tudo esvazia e solta a espera");
    flight.complete ("snare.wav", 5);
    CHECK (flight.drain().empty(), "o que chega depois de cancelar e descartado");
}

//==============================================================================
static void testBatches()
{
    std::printf ("\nO lote do canal e a geracao\n");
    Batches<std::string, int> batches;

    // sample + duas camadas: só publica com os três
    const auto g1 = batches.begin ("canal", 3);
    CHECK (batches.isPending ("canal") && batches.pending() == 1, "o canal fica carregando");
    CHECK (! batches.deliver ("canal", g1, 2, 30).has_value(), "chegou a camada 2: ainda falta");
    CHECK (! batches.deliver ("canal", g1, 0, 10).has_value(), "chegou o sample: ainda falta a camada 1");
    CHECK (! batches.deliver ("canal", g1, 2, 99).has_value(), "entrega repetida do mesmo item nao conta");
    CHECK (! batches.deliver ("canal", g1, 7, 99).has_value(), "indice fora do lote nao conta");
    auto all = batches.deliver ("canal", g1, 1, 20);
    CHECK (all.has_value() && *all == std::vector<int> ({ 10, 20, 30 }), "o ultimo item fecha o lote, na ordem pedida (sample, camadas)");
    CHECK (! batches.isPending ("canal") && batches.pending() == 0, "o canal saiu do carregando");
    CHECK (! batches.deliver ("canal", g1, 1, 20).has_value(), "lote fechado nao fecha de novo");

    // o canal trocou de sample antes de o primeiro chegar
    const auto gA = batches.begin ("canal", 1);
    const auto gB = batches.begin ("canal", 1);
    CHECK (gB != gA && batches.pending() == 1, "pedido novo aposenta o anterior (um lote aberto)");
    CHECK (! batches.deliver ("canal", gA, 0, 1).has_value(), "o sample antigo chega depois: descartado");
    CHECK (batches.isPending ("canal"), "e o canal continua esperando o novo");
    auto b = batches.deliver ("canal", gB, 0, 2);
    CHECK (b.has_value() && b->size() == 1 && (*b)[0] == 2, "o novo e publicado");

    // A -> B -> A: a geração não se repete, o primeiro A não vale pelo segundo
    const auto a1 = batches.begin ("canal", 1);
    (void) batches.begin ("canal", 1);
    const auto a2 = batches.begin ("canal", 1);
    CHECK (a1 != a2 && ! batches.deliver ("canal", a1, 0, 1).has_value(), "A, B, A: a entrega do primeiro A e descartada");
    CHECK (batches.deliver ("canal", a2, 0, 1).has_value(), "a do ultimo A vale");

    // canal removido com pedido em voo
    const auto gone = batches.begin ("removido", 2);
    CHECK (batches.cancel ("removido") && ! batches.isPending ("removido"), "canal removido: o lote sai");
    CHECK (! batches.deliver ("removido", gone, 0, 1).has_value() && ! batches.deliver ("removido", gone, 1, 1).has_value(),
           "o que chega para o canal removido e descartado");
    CHECK (! batches.cancel ("removido"), "cancelar de novo nao acha nada");

    // geração depois de cancelar não reaproveita a antiga
    const auto after = batches.begin ("removido", 1);
    CHECK (after != gone && ! batches.deliver ("removido", gone, 0, 1).has_value(), "canal recriado com o mesmo id: a entrega antiga nao serve");
    CHECK (batches.deliver ("removido", after, 0, 1).has_value(), "a nova serve");

    // lote vazio (canal sem sample e sem camadas): nada pendente
    (void) batches.begin ("vazio", 0);
    CHECK (! batches.isPending ("vazio"), "lote de zero itens nao deixa o canal carregando");

    // um lote vazio aposenta o que estava aberto (o canal ficou sem sample)
    const auto old = batches.begin ("canal", 1);
    (void) batches.begin ("canal", 0);
    CHECK (! batches.isPending ("canal") && ! batches.deliver ("canal", old, 0, 1).has_value(), "tirar o sample aposenta o pedido em voo");

    // donos independentes
    const auto x = batches.begin ("x", 1);
    const auto y = batches.begin ("y", 1);
    CHECK (batches.pending() == 2, "dois canais carregando");
    CHECK (! batches.deliver ("x", y, 0, 1).has_value(), "a geracao de um canal nao fecha o lote de outro");
    CHECK (batches.deliver ("y", y, 0, 1).has_value() && batches.deliver ("x", x, 0, 1).has_value() && batches.pending() == 0,
           "cada um fecha o seu");

    batches.begin ("a", 1);
    batches.begin ("b", 1);
    batches.cancelAll();
    CHECK (batches.pending() == 0, "cancelar tudo (o Projector saindo)");
}

//==============================================================================
/** O desenho do app em miniatura: a "thread de mensagem" (esta) pede e recolhe; a de trabalho decodifica. */
struct MiniCache
{
    using Callback = std::function<void (int)>;
    using Board = InFlight<std::string, int, Callback>;

    MiniCache() : worker ([this] { run(); }) {}

    ~MiniCache()
    {
        board->cancelAll();
        quit = true;
        wake.notify_all();
        worker.join();
    }

    void request (const std::string& file, Callback cb)
    {
        if (board->request (file, std::move (cb)))
        {
            {
                const std::lock_guard<std::mutex> lock (mutex);
                queue.push_back (file);
            }

            wake.notify_all();
        }
    }

    /** O que o callAsync faz no app: recolhe o pronto e avisa quem pediu. */
    int deliver()
    {
        int called = 0;

        for (auto& f : board->drain())
            for (auto& cb : f.waiters)
            {
                cb (f.result);
                ++called;
            }

        return called;
    }

    bool flush()
    {
        const bool idle = board->waitUntilIdle (std::chrono::seconds (20));
        deliver();
        return idle;
    }

    void run()
    {
        for (;;)
        {
            std::string file;

            {
                std::unique_lock<std::mutex> lock (mutex);
                wake.wait (lock, [this] { return quit.load() || ! queue.empty(); });

                if (quit)
                    return;

                file = queue.front();
                queue.erase (queue.begin());
            }

            ++decodes;

            if (slowMs > 0)
                std::this_thread::sleep_for (std::chrono::milliseconds (slowMs));

            int sum = 0;   // o "sample decodificado": depende só do nome

            for (char c : file)
                sum += (unsigned char) c;

            board->complete (file, sum);
        }
    }

    std::shared_ptr<Board> board = std::make_shared<Board>();
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::string> queue;
    std::atomic<bool> quit { false };
    std::atomic<int> decodes { 0 };
    int slowMs = 0;
    std::thread worker;   // por último: começa com o resto pronto
};

static int expectedFor (const std::string& file)
{
    int sum = 0;

    for (char c : file)
        sum += (unsigned char) c;

    return sum;
}

static void testTwoThreads()
{
    std::printf ("\nDuas threads: pedir e recolher de um lado, decodificar do outro\n");

    // 1) muitos pedidos, poucos arquivos: cada arquivo decodifica uma vez por rodada e todo pedido é respondido uma vez
    {
        MiniCache cache;
        const int numFiles = 16, rounds = 200, perRound = 64;
        std::vector<std::string> files;

        for (int i = 0; i < numFiles; ++i)
            files.push_back ("som" + std::to_string (i) + ".wav");

        int asked = 0, answered = 0, wrong = 0;
        long long startedJobs = 0;

        for (int r = 0; r < rounds; ++r)
        {
            for (int i = 0; i < perRound; ++i)
            {
                const auto& file = files[(size_t) ((r * 7 + i * 13) % numFiles)];
                const int expected = expectedFor (file);
                const auto before = cache.board->size();
                cache.request (file, [&answered, &wrong, expected] (int v) { ++answered; wrong += v == expected ? 0 : 1; });
                startedJobs += cache.board->size() > before ? 1 : 0;
                ++asked;

                if (i % 9 == 0)
                    cache.deliver();   // a thread de mensagem recolhe no meio dos pedidos, como no app
            }

            if (r % 10 == 0)
                (void) cache.flush();
        }

        const bool idle = cache.flush();
        CHECK (idle, "todos os trabalhos acabaram");
        CHECK (answered == asked, "cada pedido foi respondido uma vez so (%d de %d)", answered, asked);
        CHECK (wrong == 0, "cada um recebeu o sample do arquivo que pediu (%d errados)", wrong);
        CHECK (cache.decodes.load() == (int) startedJobs, "uma decodificacao por trabalho aberto (%d de %lld)", cache.decodes.load(), startedJobs);
        CHECK (cache.decodes.load() < asked, "pedidos do mesmo arquivo dividiram a decodificacao (%d decodificacoes para %d pedidos)",
               cache.decodes.load(), asked);
        CHECK (cache.board->size() == 0, "nada ficou em voo");
    }

    // 2) o mesmo arquivo pedido por 8 canais enquanto decodifica: uma decodificação só
    {
        MiniCache cache;
        cache.slowMs = 60;
        int answered = 0;

        for (int i = 0; i < 8; ++i)
            cache.request ("vocal longo.wav", [&answered] (int) { ++answered; });

        CHECK (cache.deliver() == 0 && answered == 0, "ainda decodificando: ninguem foi avisado (o canal fica em silencio, sem erro)");
        CHECK (cache.flush() && answered == 8, "esperar os carregamentos entrega aos 8 canais (%d)", answered);
        CHECK (cache.decodes.load() == 1, "8 canais, uma decodificacao (%d)", cache.decodes.load());
    }

    // 3) o fluxo inteiro com o lote do canal: troca de sample no meio, canal removido e Projector destruído
    {
        MiniCache cache;
        cache.slowMs = 5;
        auto batches = std::make_shared<Batches<std::string, int>>();   // o que o Projector guarda (e o que morre com ele)
        std::vector<std::pair<std::string, std::vector<int>>> published;

        auto ask = [&] (const std::string& channel, const std::vector<std::string>& files)
        {
            const auto generation = batches->begin (channel, files.size());

            for (size_t i = 0; i < files.size(); ++i)
                cache.request (files[i], [weak = std::weak_ptr (batches), &published, channel, generation, i] (int v)
                {
                    if (auto alive = weak.lock())
                        if (auto done = alive->deliver (channel, generation, i, v))
                            published.push_back ({ channel, *done });
                });
        };

        ask ("kick", { "kick.wav" });
        ask ("pad", { "pad.wav", "pad forte.wav", "pad fraco.wav" });
        ask ("troca", { "velho.wav" });
        ask ("troca", { "novo.wav" });        // mudou de sample antes de o velho chegar
        ask ("some", { "some.wav" });
        batches->cancel ("some");             // canal removido com o pedido em voo
        ask ("gemeo", { "kick.wav" });        // o mesmo arquivo de outro canal

        CHECK (batches->pending() == 4, "quatro canais carregando (%d)", (int) batches->pending());
        CHECK (cache.flush(), "os carregamentos acabaram");
        CHECK (batches->pending() == 0, "nenhum canal ficou carregando");
        CHECK (published.size() == 4, "quatro publicacoes: kick, pad, troca e gemeo (%d)", (int) published.size());

        bool padOk = false, trocaOk = false, someSeen = false, kickTwice = false;
        int kicks = 0;

        for (auto& [channel, items] : published)
        {
            if (channel == "pad")
                padOk = items == std::vector<int> ({ expectedFor ("pad.wav"), expectedFor ("pad forte.wav"), expectedFor ("pad fraco.wav") });

            if (channel == "troca")
                trocaOk = items == std::vector<int> ({ expectedFor ("novo.wav") });

            someSeen = someSeen || channel == "some";
            kicks += (channel == "kick" || channel == "gemeo") && items == std::vector<int> ({ expectedFor ("kick.wav") }) ? 1 : 0;
        }

        kickTwice = kicks == 2;
        CHECK (padOk, "o pad saiu de uma vez, com o sample e as duas camadas na ordem");
        CHECK (trocaOk, "o canal que trocou de sample ficou com o novo; o velho foi descartado");
        CHECK (! someSeen, "o canal removido nao recebeu nada");
        CHECK (kickTwice && cache.decodes.load() == 7, "dois canais com o mesmo arquivo: os dois recebem, uma decodificacao (%d no total)",
               cache.decodes.load());

        // Projector destruído com pedidos em voo: o que chega depois não toca em nada
        const auto before = published.size();
        ask ("tarde", { "tarde.wav" });
        batches.reset();
        CHECK (cache.flush() && published.size() == before, "com o dono destruido, a entrega e descartada");
    }

    // 4) o cache destruído com trabalho em voo: a thread de trabalho entrega para ninguém e sai
    {
        int answered = 0;

        {
            MiniCache cache;
            cache.slowMs = 30;

            for (int i = 0; i < 4; ++i)
                cache.request ("fim" + std::to_string (i) + ".wav", [&answered] (int) { ++answered; });
        }

        CHECK (answered == 0, "fechar com carregamentos em voo nao chama ninguem");
    }
}

//==============================================================================
int main()
{
    testOneJobPerKey();
    testBatches();
    testTwoThreads();

    std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d TESTE(S) FALHARAM\n", failures);
    return failures == 0 ? 0 : 1;
}
