// Testes do medidor de CPU e de falhas de buffer (sem JUCE).

#include "../../src/perf/PerformanceMonitor.h"

#include <cmath>
#include <cstdio>

using namespace whynot::perf;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

int main()
{
    std::printf ("CPU\n");
    {
        PerformanceMonitor m;
        double t = 0.0;

        for (int i = 0; i < 20; ++i, t += 0.1)
            m.update (t, 0.2f, -1);

        CHECK (std::abs (m.snapshot().cpu - 0.2f) < 0.01f && ! m.snapshot().overloaded, "carga estável em 20%%, sem aviso");

        m.update (t, 0.95f, -1);
        t += 0.1;
        CHECK (m.snapshot().cpu >= 0.95f && m.snapshot().cpuPeak >= 0.95f && m.snapshot().overloaded, "pico de 95%% aparece na hora e acende o aviso");

        for (int i = 0; i < 10; ++i, t += 0.1)
            m.update (t, 0.2f, -1);

        CHECK (m.snapshot().cpu < 0.3f && m.snapshot().cpuPeak >= 0.95f, "depois de 1 s a carga já desceu, o pico continua guardado");

        for (int i = 0; i < 15; ++i, t += 0.1)
            m.update (t, 0.2f, -1);

        CHECK (m.snapshot().cpuPeak < 0.3f && ! m.snapshot().overloaded, "depois de 2 s o pico solta e o aviso apaga");

        m.update (t, NAN, -1);
        CHECK (std::isfinite (m.snapshot().cpu), "leitura inválida da placa não estraga o medidor");
    }

    std::printf ("Falhas de buffer\n");
    {
        PerformanceMonitor m;
        m.update (0.0, 0.1f, 7);   // a placa já tinha 7 antes de abrirmos: não contam
        CHECK (m.snapshot().xruns == 0 && m.snapshot().secondsSinceXrun < 0.0, "o contador começa em 0, mesmo com a placa já contando");

        m.update (0.1, 0.1f, 9);
        CHECK (m.snapshot().xruns == 2 && m.snapshot().overloaded, "duas falhas novas contam e acendem o aviso");

        m.update (2.0, 0.1f, 9);
        CHECK (m.snapshot().xruns == 2 && std::abs (m.snapshot().secondsSinceXrun - 1.9) < 1e-9, "conta o tempo desde a última falha");

        m.update (5.0, 0.1f, 9);
        CHECK (! m.snapshot().overloaded, "três segundos sem falha apagam o aviso");

        m.update (5.1, 0.1f, 1);   // a placa reiniciou: o contador dela voltou
        m.update (5.2, 0.1f, 2);
        CHECK (m.snapshot().xruns == 3, "a placa reiniciou: o total segue somando (3)");

        m.resetXruns();
        CHECK (m.snapshot().xruns == 0 && m.snapshot().secondsSinceXrun < 0.0, "zerar pelo botão");

        PerformanceMonitor silent;
        silent.update (0.0, 0.1f, -1);
        silent.update (1.0, 0.1f, -1);
        CHECK (silent.snapshot().xruns == 0, "placa que não informa falhas: fica em 0");
    }

    std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d TESTE(S) FALHARAM\n", failures);
    return failures;
}
