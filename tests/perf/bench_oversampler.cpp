// Banco de medida do sobreamostrador 4x (passada de desempenho de 10/11), sem JUCE.
//
// Mede o custo por bloco da ida e volta (Oversampler4x::process) e só da ida (upsample, o detector de pico
// real), em mono e em estéreo, com blocos de 64, 256 e 1024 amostras, e diz quanto isso é do bloco de tempo
// real a 44,1 e a 96 kHz. Cada caso roda lado a lado com o sobreamostrador antigo (tests/dsp/OversamplerReference.h),
// alternando as repetições, para a comparação aguentar a máquina ocupada. No fim, o limitador do master inteiro
// (que usa quatro sobreamostradores e mais dois no medidor de loudness).
//
// whynot_oversampler_bench            imprime as tabelas
// whynot_oversampler_bench --reps N   N repetições por caso (padrão 15); o número é a mediana delas

#include "../../src/dsp/Master.h"
#include "../dsp/OversamplerReference.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    struct Spread
    {
        double median = 0.0, low = 0.0, high = 0.0;

        static Spread of (std::vector<double> t)
        {
            Spread s;

            if (t.empty())
                return s;

            std::sort (t.begin(), t.end());
            s.median = t[t.size() / 2];
            s.low = t.front();
            s.high = t.back();
            return s;
        }

        /** Meia largura da faixa, em % da mediana. */
        double variation() const noexcept   { return median > 0.0 ? 50.0 * (high - low) / median : 0.0; }
    };

    /** Sinal de teste: dois senos e ruído, perto do fundo de escala. Determinístico. */
    void fillSignal (std::vector<float>& x, std::uint32_t seed)
    {
        for (size_t i = 0; i < x.size(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const double noise = (double) (seed >> 8) / 16777216.0 - 0.5;
            x[i] = (float) (0.5 * std::sin (0.031 * (double) i) + 0.3 * std::sin (0.83 * (double) i) + 0.3 * noise);
        }
    }

    /** Roda `process (bloco)` por ~`budgetMs` e devolve a mediana do tempo de um bloco, em microssegundos. */
    template <typename Process>
    double timeBlocks (Process&& process, int blocks)
    {
        std::vector<double> t;
        t.reserve ((size_t) blocks);

        for (int b = 0; b < blocks; ++b)
        {
            const auto t0 = Clock::now();
            process();
            const auto t1 = Clock::now();
            t.push_back (std::chrono::duration<double, std::micro> (t1 - t0).count());
        }

        std::sort (t.begin(), t.end());
        return t[t.size() / 2];
    }

    inline float shape (float v) noexcept   { return std::clamp (v, -0.9f, 0.9f); }   // um clip barato: o custo é o do filtro

    volatile float sink = 0.0f;

    template <typename Os>
    struct RoundTrip
    {
        Os os[2];

        void prepare()   { for (auto& o : os) o.prepare(); }

        void run (float* const* io, int channels, int n) noexcept
        {
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < n; ++i)
                    io[c][i] = os[c].process (io[c][i], shape);
        }
    };

    template <typename Os>
    struct UpOnly
    {
        Os os[2];

        void prepare()   { for (auto& o : os) o.prepare(); }

        void run (float* const* io, int channels, int n) noexcept
        {
            float peak = 0.0f;

            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < n; ++i)
                {
                    float out[4];
                    os[c].upsample (io[c][i], out);

                    for (float v : out)
                        peak = std::max (peak, std::abs (v));
                }

            sink = peak;
        }
    };

    struct Row
    {
        Spread oldUs, newUs;
    };

    template <template <typename> class Case>
    Row compare (int channels, int block, int reps)
    {
        Case<whynot::dsp::reference::Oversampler4x> before;
        Case<whynot::dsp::Oversampler4x> after;
        before.prepare();
        after.prepare();

        std::vector<float> source[2], work[2];

        for (int c = 0; c < 2; ++c)
        {
            source[c].resize ((size_t) block);
            work[c].resize ((size_t) block);
            fillSignal (source[c], 12345u + (std::uint32_t) c);
        }

        float* io[2] = { work[0].data(), work[1].data() };
        const int blocks = std::max (64, 200000 / block);   // ~200 mil amostras por repetição
        std::vector<double> tOld, tNew;
        const whynot::dsp::DenormalGuard guard;

        auto refill = [&]
        {
            for (int c = 0; c < channels; ++c)
                std::copy (source[c].begin(), source[c].end(), work[c].begin());
        };

        for (int r = -1; r < reps; ++r)   // a repetição -1 só aquece
        {
            const double a = timeBlocks ([&] { refill(); before.run (io, channels, block); }, blocks);
            const double b = timeBlocks ([&] { refill(); after.run (io, channels, block); }, blocks);

            if (r >= 0)
            {
                tOld.push_back (a);
                tNew.push_back (b);
            }
        }

        return { Spread::of (tOld), Spread::of (tNew) };
    }

    void printRow (const char* what, int channels, int block, const Row& r)
    {
        const double b441 = block / 44100.0 * 1.0e6, b96 = block / 96000.0 * 1.0e6;
        std::printf ("  %-12s %-7s %5d | %8.2f ±%4.1f%% %6.2f%% %6.2f%% | %8.2f ±%4.1f%% %6.2f%% %6.2f%% | %5.2fx %+6.1f%%\n",
                     what, channels == 1 ? "mono" : "estéreo", block,
                     r.oldUs.median, r.oldUs.variation(), 100.0 * r.oldUs.median / b441, 100.0 * r.oldUs.median / b96,
                     r.newUs.median, r.newUs.variation(), 100.0 * r.newUs.median / b441, 100.0 * r.newUs.median / b96,
                     r.oldUs.median / r.newUs.median, 100.0 * (r.newUs.median / r.oldUs.median - 1.0));
    }

    void benchLimiter (double rate, int block, int reps)
    {
        whynot::dsp::LimiterProcessor limiter;
        limiter.prepare (rate, block);
        limiter.setParam (whynot::dsp::LimiterProcessor::gain, 6.0f);
        limiter.setParam (whynot::dsp::LimiterProcessor::clip, 50.0f);
        limiter.snapToTargets();

        std::vector<float> source[2], work[2];

        for (int c = 0; c < 2; ++c)
        {
            source[c].resize ((size_t) block);
            work[c].resize ((size_t) block);
            fillSignal (source[c], 777u + (std::uint32_t) c);
        }

        const int blocks = std::max (64, 100000 / block);
        std::vector<double> t;

        for (int r = -1; r < reps; ++r)
        {
            const double us = timeBlocks ([&]
            {
                for (int c = 0; c < 2; ++c)
                    std::copy (source[c].begin(), source[c].end(), work[c].begin());

                whynot::dsp::AudioBlock b { work[0].data(), work[1].data(), block };
                limiter.process (b);
            }, blocks);

            if (r >= 0)
                t.push_back (us);
        }

        const auto s = Spread::of (t);
        std::printf ("  limitador estéreo, %5.1f kHz, bloco %4d: %8.2f us ±%4.1f%%, %5.2f%% do bloco\n",
                     rate / 1000.0, block, s.median, s.variation(), 100.0 * s.median / (block / rate * 1.0e6));
    }
}

int main (int argc, char** argv)
{
    int reps = 15;

    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp (argv[i], "--reps") == 0)
            reps = std::max (3, std::atoi (argv[i + 1]));

    std::printf ("Sobreamostrador 4x: tempo por bloco (mediana de %d repetições, ± meia faixa entre a menor e a maior)\n", reps);
    std::printf ("  %-12s %-7s %5s | %-33s | %-33s | %s\n", "caso", "canais", "bloco",
                 "antigo: us, % de 44,1k, % de 96k", "atual: us, % de 44,1k, % de 96k", "ganho");

    for (int channels : { 1, 2 })
        for (int block : { 64, 256, 1024 })
            printRow ("ida e volta", channels, block, compare<RoundTrip> (channels, block, reps));

    for (int channels : { 1, 2 })
        for (int block : { 64, 256, 1024 })
            printRow ("só a ida", channels, block, compare<UpOnly> (channels, block, reps));

    std::printf ("\nLimitador do master (clipper a 50%%, +6 dB de entrada; 2 sobreamostradores de ida e volta, 2 detectores e 2 no medidor)\n");

    for (double rate : { 44100.0, 96000.0 })
        for (int block : { 64, 256, 1024 })
            benchLimiter (rate, block, reps);

    return 0;
}
