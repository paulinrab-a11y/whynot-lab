#include "FxHost.h"

#include <array>
#include <limits>

namespace whynot::dsp
{

namespace
{
    /** true se o bloco tem NaN ou infinito (só eles sobrevivem a x * 0 como não zero). */
    bool hasBadSamples (const float* l, const float* r, int n) noexcept
    {
        float acc = 0.0f;

        for (int i = 0; i < n; ++i)
            acc += l[i] * 0.0f + r[i] * 0.0f;

        return acc != acc;
    }

    void scrub (float* x, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (x[i]))
                x[i] = 0.0f;
    }
}

FxHost::FxHost (std::unique_ptr<Processor> p) : proc (std::move (p))
{
    const auto& params = proc->info().params;
    numParams = std::min ((int) params.size(), maxParams);
    values = std::make_unique<std::atomic<float>[]> ((size_t) numParams);

    for (int i = 0; i < numParams; ++i)
        values[(size_t) i].store (params[(size_t) i].def);

    applied.assign ((size_t) numParams, std::numeric_limits<float>::quiet_NaN());
}

void FxHost::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < numParams)
        values[(size_t) index].store (proc->info().params[(size_t) index].clamp (value), std::memory_order_relaxed);
}

float FxHost::getParam (int index) const noexcept
{
    return index >= 0 && index < numParams ? values[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
}

double FxHost::tailSeconds() const noexcept
{
    std::array<float, maxParams> v {};

    for (int i = 0; i < numParams; ++i)
        v[(size_t) i] = values[(size_t) i].load (std::memory_order_relaxed);

    return proc->tailSecondsFor (v.data());
}

void FxHost::prepare (double sampleRate, int maxBlockSize)
{
    maxBlock = std::max (32, maxBlockSize);
    proc->prepare (sampleRate, maxBlock);
    latency = proc->latencySamples();

    for (int c = 0; c < 2; ++c)
    {
        dryDelay[c].prepare (latency + 4);
        dry[c].assign ((size_t) maxBlock, 0.0f);
    }

    // Os valores atuais entram já, sem rampa: um render começa com o som certo desde a 1ª amostra.
    for (int i = 0; i < numParams; ++i)
    {
        applied[(size_t) i] = values[(size_t) i].load (std::memory_order_relaxed);
        proc->setParam (i, applied[(size_t) i]);
    }

    proc->snapToTargets();
    proc->reset();

    dryAmount.prepare (sampleRate, 20.0f / 3.0f);   // ~20 ms até 95%
    fullyBypassed = isBypassed();
    cleared = true;
    dryAmount.snap (fullyBypassed ? 1.0f : 0.0f);
    prepared = true;
}

void FxHost::process (float* left, float* right, int numSamples, const float* sideLeft, const float* sideRight) noexcept
{
    if (! prepared)
        return;

    for (int i = 0; i < numParams; ++i)
    {
        const float v = values[(size_t) i].load (std::memory_order_relaxed);

        if (v != applied[(size_t) i])
        {
            applied[(size_t) i] = v;
            proc->setParam (i, v);
        }
    }

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - start);
        processChunk (left + start, right + start, n,
                      sideLeft != nullptr ? sideLeft + start : nullptr,
                      sideRight != nullptr ? sideRight + start : nullptr);
    }
}

void FxHost::processChunk (float* left, float* right, int n, const float* sideLeft, const float* sideRight) noexcept
{
    float* io[2] = { left, right };
    const bool wantBypass = isBypassed();
    dryAmount.setTarget (wantBypass ? 1.0f : 0.0f);

    if (hasBadSamples (left, right, n))
    {
        scrub (left, n);
        scrub (right, n);
    }

    // Sinal seco atrasado pela latência do efeito (alinhado com a saída dele).
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            dryDelay[c].push (io[c][i]);
            dry[c][(size_t) i] = dryDelay[c].read (latency);
        }

    if (fullyBypassed && wantBypass)
    {
        for (int c = 0; c < 2; ++c)
            std::copy (dry[c].begin(), dry[c].begin() + n, io[c]);

        // Zera o estado do efeito aos poucos (linhas de atraso grandes não pesam num bloco só).
        if (! cleared)
            cleared = proc->clearStep (16384);

        return;
    }

    if (fullyBypassed)
    {
        // Saindo do bypass: se a limpeza não terminou, termina agora (raro: liga e desliga rápido).
        if (! cleared)
            proc->reset();

        fullyBypassed = false;
        cleared = true;
    }

    AudioBlock block { left, right, n, sideLeft, sideRight };
    proc->process (block);

    if (hasBadSamples (left, right, n))
    {
        // O efeito gerou NaN (não deveria): volta do zero em vez de ficar mudo para sempre.
        proc->reset();
        std::fill_n (left, n, 0.0f);
        std::fill_n (right, n, 0.0f);
    }

    if (! dryAmount.isSmoothing() && dryAmount.getCurrent() == 0.0f)
        return;

    for (int i = 0; i < n; ++i)
    {
        const float d = dryAmount.next();

        for (int c = 0; c < 2; ++c)
            io[c][i] += d * (dry[c][(size_t) i] - io[c][i]);
    }

    if (wantBypass && ! dryAmount.isSmoothing())
    {
        fullyBypassed = true;
        cleared = proc->clearStep (16384);
    }
}

} // namespace whynot::dsp
