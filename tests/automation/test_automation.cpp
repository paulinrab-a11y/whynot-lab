// Testes das contas da automação (sem JUCE): posição do knob, pontos e o valor da curva no tick.

#include "../../src/automation/AutomationMath.h"

#include <algorithm>
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

    std::printf ("Tensão\n");
    {
        CHECK (shaped (0.5, 0.0f) == 0.5 && near (shaped (0.5, 1.0f), 1.0 / 256.0, 1e-9) && near (shaped (0.5, -1.0f), std::pow (0.5, 0.125), 1e-9)
                   && shaped (0.0, 0.7f) == 0.0 && shaped (1.0, -0.7f) == 1.0 && shaped (0.5, NAN) == 0.5,
               "tensão 0 é reta; 1 segura no começo; -1 corre no começo; as pontas não mudam; NaN vira reta");

        bool inverse = true;

        for (float k : { -0.9f, -0.5f, -0.1f, 0.0f, 0.3f, 0.8f })
            inverse = inverse && near (tensionForMidpoint (shaped (0.5, k)), k, 1e-4);

        CHECK (inverse && near (tensionForMidpoint (0.0), 1.0, 1e-4) && near (tensionForMidpoint (1.0), -1.0, 1e-4),
               "arrastar o meio do trecho dá a tensão de volta (e para nas pontas)");

        const std::vector<Point> bent { { 0, 0.0f, 0.6f }, { 3840, 1.0f } };
        const double expectedMid = shaped (0.5, 0.6f);
        CHECK (near (positionAt (bent, 1920, 0.0f), expectedMid, 1e-6) && positionAt (bent, 3840, 0.0f) == 1.0f
                   && positionAt ({ { 0, 1.0f, 0.6f }, { 100, 0.0f } }, 50, 0.0f) > 0.9f,
               "a tensão do primeiro ponto dobra o trecho (subindo e descendo); no meio: %.4f", positionAt (bent, 1920, 0.0f));

        const auto baked = bake (bent);
        double worst = 0.0;

        for (int t = 0; t <= 3840; t += 7)
        {
            // o que o Tracktion toca: reta entre os pontos do bake
            const double linear = positionAt (baked, t, 0.0f);
            worst = std::max (worst, std::abs (linear - positionAt (bent, t, 0.0f)));
        }

        bool flat = true;

        for (auto& p : baked)
            flat = flat && p.tension == 0.0f;

        CHECK (baked.size() == 33 && flat && worst < 0.01, "bake: 32 retas por trecho com tensão, sem tensão na saída, erro máximo %.4f", worst);
        CHECK (bake ({ { 0, 0.0f }, { 960, 1.0f }, { 961, 0.0f, 1.0f }, { 962, 1.0f } }).size() == 4,
               "trecho reto fica como está; trecho de 1 tick não ganha pontos");

        const auto cleaned = cleanPoints ({ { 0, 0.5f, 3.0f }, { 10, 0.5f, NAN } }, 100);
        CHECK (cleaned[0].tension == 1.0f && cleaned[1].tension == 0.0f, "cleanPoints põe a tensão entre -1 e 1 (NaN vira 0)");
    }

    std::printf ("Gravar automação: redução e o trecho por cima\n");
    {
        // 4 s de knob a cada 20 ms: rampa de 0 a 1 em 2 s e parado em 1 depois
        std::vector<Point> samples;

        for (int i = 0; i <= 200; ++i)
            samples.push_back ({ i * 10, i <= 100 ? (float) i / 100.0f : 1.0f });

        const auto reduced = reduce (samples);
        CHECK (reduced.size() == 3 && reduced[1] == (Point { 1000, 1.0f }) && reduced.back().t == 2000,
               "rampa e platô: de 201 medidas sobram %zu pontos (começo, a quina e o fim)", reduced.size());

        std::vector<Point> wave;

        for (int i = 0; i <= 400; ++i)
            wave.push_back ({ i * 10, (float) (0.5 + 0.4 * std::sin (i * 0.05)) });

        const auto waveReduced = reduce (wave);
        double worst = 0.0;

        for (auto& p : wave)
            worst = std::max (worst, (double) std::abs (positionAt (waveReduced, p.t, 0.0f) - p.v));

        CHECK (waveReduced.size() < 100 && worst <= 0.0041, "senoide: de 401 medidas sobram %zu pontos, erro máximo %.4f", waveReduced.size(), worst);
        CHECK (reduce ({ { 0, 0.2f } }).size() == 1 && reduce ({}).empty(), "uma medida só fica como está");

        const std::vector<Point> old { { 0, 0.0f }, { 1000, 1.0f, 0.5f }, { 4000, 0.0f } };
        const auto replaced = replaceRange (old, { { 1500, 0.3f }, { 2500, 0.3f } }, 1500, 2500);
        double outside = 0.0;

        for (int t = 0; t <= 4500; t += 5)
            if (t < 1499 || t > 2501)
                outside = std::max (outside, (double) std::abs (positionAt (replaced, t, 0.0f) - positionAt (old, t, 0.0f)));

        CHECK (near (positionAt (replaced, 2000, 0.0f), 0.3, 1e-6) && outside < 0.01,
               "a passada substitui só o trecho dela; antes e depois a curva soa como antes (erro máximo %.4f, com a tensão)", outside);

        const auto fresh = replaceRange ({}, { { 100, 0.2f }, { 900, 0.8f } }, 100, 900);
        CHECK (fresh.size() == 2 && fresh[1] == (Point { 900, 0.8f }), "curva vazia: só os pontos gravados (o último segura)");
        CHECK (replaceRange (old, {}, 0, 100) == old, "passada vazia não muda nada");

        // a revisão de 26/10: o trecho curvo depois da emenda continua curvo, e um ponto antigo bem na emenda fica
        const std::vector<Point> two { { 0, 0.0f, 0.5f }, { 1000, 1.0f, 0.5f }, { 2000, 0.0f } };
        const auto cut = replaceRange (two, { { 400, 0.2f }, { 600, 0.2f } }, 400, 600);
        double after = 0.0;

        for (int t = 1000; t <= 2000; t += 5)
            after = std::max (after, (double) std::abs (positionAt (cut, t, 0.0f) - positionAt (two, t, 0.0f)));

        const auto atSeam = replaceRange (two, { { 400, 0.2f }, { 999, 0.2f } }, 400, 999);
        CHECK (after < 1e-6 && std::any_of (atSeam.begin(), atSeam.end(), [] (const Point& p) { return p.t == 1000 && p.tension == 0.5f; }),
               "o trecho curvo depois da emenda fica igual (erro %.6f); o ponto antigo em to + 1 fica com a tensão", after);
    }

    std::printf (failures == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d TESTE(S) FALHARAM\n", failures);
    return failures;
}
