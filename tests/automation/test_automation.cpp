// Testes das contas da automação (sem JUCE): posição do knob, pontos e o valor da curva no tick.

#include "../../src/automation/AutomationMath.h"

#include <cmath>
#include <cstdio>

using namespace whynot::automation;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }

int main()
{
    std::printf ("Posição do knob\n");
    {
        const Range cutoff { 20.0f, 20000.0f, Range::Scale::log };
        CHECK (near (cutoff.toNormalised (20.0f), 0.0, 1e-6) && near (cutoff.toNormalised (20000.0f), 1.0, 1e-6), "frequência: 20 Hz é 0, 20 kHz é 1");
        CHECK (near (cutoff.toNormalised (632.456f), 0.5, 1e-4), "escala logarítmica: o meio é a média geométrica (632 Hz)");
        CHECK (near (cutoff.fromNormalised (cutoff.toNormalised (1234.5f)), 1234.5, 0.01), "ida e volta 1234,5 Hz");
        CHECK (cutoff.clamp (50000.0f) == 20000.0f && cutoff.clamp (NAN) == 20.0f, "fora da faixa e NaN viram valores válidos");

        const Range volume { -100.0f, 6.0f, Range::Scale::fader };
        CHECK (near (volume.toNormalised (6.0f), 1.0, 1e-6) && volume.toNormalised (-100.0f) == 0.0f, "volume: +6 dB é o topo do fader, -100 dB é o fundo");
        CHECK (near (volume.toNormalised (0.0f), std::exp (-0.3), 1e-6), "0 dB fica onde o fader do Tracktion põe (e^-0,3)");
        CHECK (near (volume.fromNormalised (volume.toNormalised (-12.0f)), -12.0, 1e-4), "ida e volta -12 dB");

        const Range mode { 0.0f, 3.0f, Range::Scale::linear, true };
        CHECK (mode.clamp (1.4f) == 1.0f && mode.fromNormalised (0.55f) == 2.0f, "escolha: só valores inteiros");
    }

    std::printf ("Pontos\n");
    {
        const auto pts = cleanPoints ({ { 960, 0.5f }, { 0, -2.0f }, { 960, 0.25f }, { -5, 0.0f }, { 99999, 0.0f }, { 480, NAN } }, 3840);
        CHECK (pts.size() == 3 && pts[0] == (Point { 0, 0.0f }) && pts[1] == (Point { 480, 0.0f }) && pts[2] == (Point { 960, 0.25f }),
               "ordena, põe a posição entre 0 e 1 (NaN vira 0), um ponto por tick (o último vence) e tira os de fora da música");
    }

    std::printf ("Valor da curva\n");
    {
        const Range cutoff { 20.0f, 20000.0f, Range::Scale::log };
        const std::vector<Point> sweep { { 0, cutoff.toNormalised (200.0f) }, { 3840, cutoff.toNormalised (20000.0f) } };
        CHECK (near (valueAt (sweep, cutoff, -100, 1000.0f), 200.0, 0.01) && valueAt (sweep, cutoff, 9000, 1000.0f) == 20000.0f,
               "antes do primeiro ponto vale o primeiro; depois do último, o último");
        CHECK (near (valueAt (sweep, cutoff, 1920, 1000.0f), 2000.0, 0.5), "no meio do caminho de 200 Hz a 20 kHz: 2 kHz (média geométrica)");
        CHECK (valueAt ({}, cutoff, 100, 1000.0f) == 1000.0f, "sem pontos vale o valor do efeito");

        const std::vector<Point> three { { 0, 0.0f }, { 960, 1.0f }, { 1920, 0.5f } };
        CHECK (near (positionAt (three, 480, 0.0f), 0.5, 1e-6) && near (positionAt (three, 1440, 0.0f), 0.75, 1e-6),
               "reta entre cada par de pontos, na posição do knob");

        const Range volume { -100.0f, 6.0f, Range::Scale::fader };
        const float mid = valueAt ({ { 0, volume.toNormalised (-40.0f) }, { 1000, volume.toNormalised (0.0f) } }, volume, 500, 0.0f);
        const double expected = 20.0 * std::log (0.5 * (std::exp (-2.3) + std::exp (-0.3))) + 6.0;
        CHECK (near (mid, expected, 1e-3), "volume anda reto no fader: no meio de -40 a 0 dB fica %.2f dB", mid);
    }

    std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d TESTE(S) FALHARAM\n", failures);
    return failures;
}
