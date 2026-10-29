// Banco de medida da thread de áudio (passada de desempenho de 29/10), sem JUCE nem Tracktion.
//
// Mede o custo por bloco (256 amostras a 48 kHz, orçamento de 5,33 ms) dos efeitos nativos, da afinação, das vozes
// do sampler com e sem o bend do canal e do WHYNOT Synth com um preset 3 OSC, e conta as alocações feitas dentro do
// process (o operator new global é trocado só neste executável). Cada caso toca 4 s de som e depois 4 s de
// silêncio: a cauda é onde os números desnormais aparecem, então o custo do silêncio sai à parte.
//
// whynot_rt_bench            mede tudo e imprime a tabela (média, p99 e pior bloco, em microssegundos)
// whynot_rt_bench --check    só as checagens (nenhuma alocação no process); é o que o ctest roda
// whynot_rt_bench --csv f    também grava a tabela em CSV

#define _CRT_SECURE_NO_WARNINGS 1   // fopen do --csv

#include "../../src/audio/ChannelPitch.h"
#include "../../src/audio/SampleVoice.h"
#include "../../src/dsp/FxCatalog.h"
#include "../../src/synth/Synth.h"
#include "../../src/synth/SynthPresets.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

//==============================================================================
// Alocações contadas só enquanto `counting` está ligado (o trecho do process).

static std::atomic<bool> counting { false };
static std::atomic<long> allocations { 0 };

void* operator new (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);

    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)                      { return operator new (size); }
void operator delete (void* p) noexcept                      { std::free (p); }
void operator delete[] (void* p) noexcept                    { std::free (p); }
void operator delete (void* p, std::size_t) noexcept         { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept       { std::free (p); }

//==============================================================================
namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    constexpr int soundBlocks = (int) (4.0 * rate) / block;
    constexpr int silenceBlocks = (int) (4.0 * rate) / block;
    constexpr double budgetUs = block / rate * 1.0e6;

    using Clock = std::chrono::steady_clock;

    struct Stats
    {
        // a mediana aguenta a máquina ocupada (um build ao lado); a média e o pior mostram os picos
        double median = 0.0, mean = 0.0, p99 = 0.0, worst = 0.0;

        static Stats of (std::vector<double> t)
        {
            Stats s;

            if (t.empty())
                return s;

            double sum = 0.0;
            for (double v : t) sum += v;
            s.mean = sum / (double) t.size();
            std::sort (t.begin(), t.end());
            s.median = t[t.size() / 2];
            s.p99 = t[(size_t) std::min ((double) t.size() - 1.0, std::floor (0.99 * (double) t.size()))];
            s.worst = t.back();
            return s;
        }
    };

    struct Result
    {
        std::string name;
        Stats sound, silence;
        long allocs = 0;
    };

    std::vector<Result> results;

    /** Um som de teste estéreo: acorde de serras com vibrato lento, um bumbo a cada meio segundo e um pouco de
        ruído (pega todos os ramos dos detectores). Determinístico. */
    struct TestSignal
    {
        double phase[3] {}, t = 0.0;
        std::uint32_t seed = 12345;

        void fill (float* l, float* r, int n) noexcept
        {
            static constexpr double freqs[3] = { 110.0, 164.81, 220.0 };

            for (int i = 0; i < n; ++i, t += 1.0 / rate)
            {
                double v = 0.0;

                for (int k = 0; k < 3; ++k)
                {
                    phase[k] += freqs[k] * (1.0 + 0.004 * std::sin (2.0 * 3.14159265 * 5.0 * t)) / rate;
                    phase[k] -= std::floor (phase[k]);
                    v += 0.18 * (2.0 * phase[k] - 1.0);
                }

                const double beat = std::fmod (t, 0.5);
                v += 0.6 * std::exp (-beat * 30.0) * std::sin (2.0 * 3.14159265 * 55.0 * beat);
                seed = seed * 1664525u + 1013904223u;
                const double noise = ((double) (seed >> 8) / 16777216.0 - 0.5) * 0.02;
                l[i] = (float) (v + noise);
                r[i] = (float) (0.9 * v - noise);
            }
        }
    };

    template <typename Process>
    Result measure (const std::string& name, Process&& process, bool feedSignal = true)
    {
        std::vector<float> left ((size_t) block), right ((size_t) block);
        std::vector<double> sound, silence;
        sound.reserve ((size_t) soundBlocks);
        silence.reserve ((size_t) silenceBlocks);
        TestSignal signal;
        Result result { name };

        // aquece (caches, primeira página tocada) sem medir
        for (int b = 0; b < 8; ++b)
        {
            if (feedSignal) signal.fill (left.data(), right.data(), block);
            else { std::fill (left.begin(), left.end(), 0.0f); std::fill (right.begin(), right.end(), 0.0f); }
            process (left.data(), right.data(), block, b);
        }

        for (int b = 0; b < soundBlocks + silenceBlocks; ++b)
        {
            const bool loud = b < soundBlocks;

            if (loud && feedSignal)
                signal.fill (left.data(), right.data(), block);
            else
            {
                std::fill (left.begin(), left.end(), 0.0f);
                std::fill (right.begin(), right.end(), 0.0f);
            }

            allocations.store (0);
            counting.store (true);
            const auto t0 = Clock::now();
            process (left.data(), right.data(), block, loud ? b : -1);
            const auto t1 = Clock::now();
            counting.store (false);
            result.allocs += allocations.load();

            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count();
            (loud ? sound : silence).push_back (us);
        }

        result.sound = Stats::of (sound);
        result.silence = Stats::of (silence);
        results.push_back (result);
        return result;
    }

    void benchEffect (const char* type)
    {
        auto fx = whynot::dsp::createProcessor (type);

        if (fx == nullptr)
        {
            std::printf ("  efeito %s não existe\n", type);
            return;
        }

        fx->prepare (rate, block);
        const auto& params = fx->info().params;

        for (size_t i = 0; i < params.size(); ++i)
            fx->setParam ((int) i, params[i].def);

        // os que vêm desligados por padrão (o EQ dinâmico tem as bandas desligadas): liga tudo o que é "on"
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].kind == whynot::dsp::ParamKind::toggle && std::strstr (params[i].id, "on") != nullptr)
                fx->setParam ((int) i, 1.0f);

        fx->snapToTargets();

        measure (std::string ("efeito ") + type, [&] (float* l, float* r, int n, int)
        {
            whynot::dsp::AudioBlock b { l, r, n };
            fx->process (b);
        });
    }

    /** O sample do teste: 2 s de uma serra que decai, mono, a 44,1 kHz (o caso comum de um 808 ou um pad). */
    whynot::audio::SampleData makeSample()
    {
        whynot::audio::SampleData s;
        s.sampleRate = 44100.0;
        s.rootKey = 60;
        s.channels.assign (1, std::vector<float> (88200));
        double ph = 0.0;

        for (size_t i = 0; i < s.channels[0].size(); ++i)
        {
            ph += 261.63 / 44100.0;
            ph -= std::floor (ph);
            s.channels[0][i] = (float) ((2.0 * ph - 1.0) * std::exp (-(double) i / 60000.0) * 0.5);
        }

        whynot::audio::buildMipmaps (s);
        whynot::audio::flushDenormals (s);
        return s;
    }

    void benchSampler (int numVoices, bool bend)
    {
        static const auto sample = makeSample();
        std::vector<whynot::audio::SampleVoice> voices ((size_t) numVoices);
        whynot::audio::ChannelPitch pitch;
        pitch.prepare (rate);
        float offsets[block];

        if (bend)
        {
            pitch.pitchWheel (8192 + 2000);
            pitch.controller (1, 90);
            pitch.noteStarts (true);
        }

        whynot::audio::VoiceParams p;
        p.sustain = 1.0f;

        for (auto& v : voices)
            v.prepare (rate);

        const std::string name = "sampler " + std::to_string (numVoices) + " vozes" + (bend ? " com bend e vibrato" : "");

        measure (name, [&] (float* l, float* r, int n, int b)
        {
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);

            // uma nota nova por voz a cada 1 s (dentro do bloco medido, como no projeto)
            if (b >= 0 && b % 187 == 0)
                for (int k = 0; k < numVoices; ++k)
                    voices[(size_t) k].noteOn (48 + (k * 7) % 24, 0.8f, false, p);

            if (b == -1)
                for (auto& v : voices)
                    if (v.isActive())
                        v.noteOff();

            float* out[2] = { l, r };
            const float* bent = pitch.fill (offsets, n) ? offsets : nullptr;

            for (auto& v : voices)
                v.render (sample, out, 2, n, bent);
        }, false);
    }

    void benchSynth (const char* presetId, int notes)
    {
        const auto* preset = whynot::synth::findPreset (presetId);

        if (preset == nullptr)
        {
            std::printf ("  preset %s não existe\n", presetId);
            return;
        }

        auto synth = std::make_unique<whynot::synth::Synth>();
        synth->setAll (preset->values);
        synth->prepare (rate, block);
        std::vector<whynot::synth::Event> events;
        events.reserve (64);

        measure (std::string ("synth ") + presetId + ", " + std::to_string (notes) + " notas", [&] (float* l, float* r, int n, int b)
        {
            events.clear();

            if (b >= 0 && b % 94 == 0)   // acorde novo a cada ~0,5 s
            {
                events.push_back ({ 0, whynot::synth::Event::Type::allNotesOff, 0, 0.0f });

                for (int k = 0; k < notes; ++k)
                    events.push_back ({ 0, whynot::synth::Event::Type::noteOn, 48 + (k * 5) % 24 + (b / 94) % 3, 0.8f });
            }
            else if (b == -1 && synth->activeVoices() > 0)
                events.push_back ({ 0, whynot::synth::Event::Type::allNotesOff, 0, 0.0f });

            synth->process (l, r, n, events.data(), (int) events.size());
        }, false);
    }

    void print (const Result& r, std::FILE* csv)
    {
        std::printf ("  %-40s %8.1f %8.1f %8.1f %8.1f | %8.1f %8.1f | %5.1f%% | %ld\n",
                     r.name.c_str(), r.sound.median, r.sound.mean, r.sound.p99, r.sound.worst, r.silence.median, r.silence.worst,
                     100.0 * r.sound.median / budgetUs, r.allocs);

        if (csv != nullptr)
            std::fprintf (csv, "%s;%.2f;%.2f;%.2f;%.2f;%.2f;%.2f;%ld\n", r.name.c_str(), r.sound.median, r.sound.mean, r.sound.p99,
                          r.sound.worst, r.silence.median, r.silence.worst, r.allocs);
    }
}

int main (int argc, char** argv)
{
    const bool checkOnly = argc > 1 && std::strcmp (argv[1], "--check") == 0;
    std::FILE* csv = nullptr;

    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp (argv[i], "--csv") == 0)
            csv = std::fopen (argv[i + 1], "w");

    if (csv != nullptr)
        std::fprintf (csv, "caso;mediana_us;media_us;p99_us;pior_us;silencio_mediana_us;silencio_pior_us;alocacoes\n");

    // os presets são montados fora do áudio (a primeira chamada aloca)
    whynot::synth::allPresets();

    for (const char* type : { "clipper", "lofi", "chorus", "flanger", "phaser", "dynEq", "tune",
                              "eq", "comp", "reverb", "delay", "limiter" })
        benchEffect (type);

    benchSampler (1, false);
    benchSampler (16, false);
    benchSampler (16, true);
    benchSynth ("3osc-supersaw", 8);
    benchSynth ("3osc-808", 1);

    int failures = 0;

    if (! checkOnly)
        std::printf ("\n  %-40s %8s %8s %8s %8s | %8s %8s | %6s | %s\n  (us por bloco de %d a 48 kHz; orçamento %.0f us; carga = mediana / orçamento)\n",
                     "caso", "mediana", "média", "p99", "pior", "silêncio", "pior", "carga", "aloc.", block, budgetUs);

    for (const auto& r : results)
    {
        if (! checkOnly)
            print (r, csv);

        if (r.allocs != 0)
        {
            ++failures;
            std::printf ("  FALHOU: %s alocou %ld vez(es) no process\n", r.name.c_str(), r.allocs);
        }
    }

    if (! checkOnly)
    {
        // O projeto pesado do pedido: 16 canais do Synth 3 OSC (8 acordes de supersaw, 8 linhas de 808), cada um com os
        // 6 efeitos novos, e a afinação numa voz. A soma das medianas é o custo de um bloco na thread de áudio (o
        // Tracktion pode dividir as trilhas entre núcleos; aqui é tudo num núcleo só, o pior caso).
        auto median = [] (const char* name)
        {
            for (const auto& r : results)
                if (r.name == name)
                    return r.sound.median;

            return 0.0;
        };

        const double perChannel = median ("efeito clipper") + median ("efeito lofi") + median ("efeito chorus")
                                + median ("efeito flanger") + median ("efeito phaser") + median ("efeito dynEq");
        const double heavy = 8.0 * (median ("synth 3osc-supersaw, 8 notas") + perChannel)
                           + 8.0 * (median ("synth 3osc-808, 1 notas") + perChannel) + median ("efeito tune");
        std::printf ("\n  projeto pesado (16 synths 3 OSC com os 6 efeitos novos e a afinação): %.0f us por bloco, %.0f%% do orçamento\n",
                     heavy, 100.0 * heavy / budgetUs);

        if (csv != nullptr)
            std::fprintf (csv, "projeto pesado;%.2f;;;;;;\n", heavy);
    }

    if (csv != nullptr)
        std::fclose (csv);

    std::printf (failures == 0 ? "\nNENHUMA ALOCAÇÃO NO PROCESS (%d casos)\n" : "\n%d CASO(S) ALOCARAM\n",
                 failures == 0 ? (int) results.size() : failures);
    return failures;
}
