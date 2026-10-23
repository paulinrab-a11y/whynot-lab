#include "EqProcessor.h"

namespace whynot::dsp
{

namespace
{
    const char* const hpSlopes[] = { "12 dB/oit", "24 dB/oit", "48 dB/oit" };
    const char* const lpSlopes[] = { "12 dB/oit", "24 dB/oit" };

    const ParamInfo params[] = {
        { "hpOn",     "Passa-altas",   0, 1, 0,         "",   ParamKind::toggle, false, {}, "Liga o corte de graves (tira ronco e embolado)." },
        { "hpFreq",   "Corte grave",   20, 2000, 30,    "Hz", ParamKind::continuous, true, {}, "Abaixo desta frequência o som é cortado." },
        { "hpSlope",  "Inclinação",    0, 2, 1,         "",   ParamKind::choice, false, hpSlopes, "Quão íngreme é o corte de graves." },
        { "lowFreq",  "Grave",         20, 1000, 100,   "Hz", ParamKind::continuous, true },
        { "lowGain",  "Ganho grave",   -18, 18, 0,      "dB" },
        { "b1Freq",   "Sino 1",        20, 20000, 250,  "Hz", ParamKind::continuous, true },
        { "b1Gain",   "Ganho 1",       -18, 18, 0,      "dB" },
        { "b1Q",      "Largura 1",     0.1f, 18, 1,     "Q",  ParamKind::continuous, true, {}, "Q alto = faixa estreita." },
        { "b2Freq",   "Sino 2",        20, 20000, 1000, "Hz", ParamKind::continuous, true },
        { "b2Gain",   "Ganho 2",       -18, 18, 0,      "dB" },
        { "b2Q",      "Largura 2",     0.1f, 18, 1,     "Q",  ParamKind::continuous, true },
        { "b3Freq",   "Sino 3",        20, 20000, 4000, "Hz", ParamKind::continuous, true },
        { "b3Gain",   "Ganho 3",       -18, 18, 0,      "dB" },
        { "b3Q",      "Largura 3",     0.1f, 18, 1,     "Q",  ParamKind::continuous, true },
        { "highFreq", "Agudo",         1000, 20000, 8000, "Hz", ParamKind::continuous, true },
        { "highGain", "Ganho agudo",   -18, 18, 0,      "dB" },
        { "lpOn",     "Passa-baixas",  0, 1, 0,         "",   ParamKind::toggle, false, {}, "Liga o corte de agudos." },
        { "lpFreq",   "Corte agudo",   1000, 22000, 18000, "Hz", ParamKind::continuous, true },
        { "lpSlope",  "Inclinação agudo", 0, 1, 0,      "",   ParamKind::choice, false, lpSlopes },
        { "output",   "Saída",         -24, 24, 0,      "dB" },
    };

    static_assert (std::size (params) == EqProcessor::count);

    int hpSections (float slope) noexcept   { return slope >= 2.0f ? 4 : (slope >= 1.0f ? 2 : 1); }
    int lpSections (float slope) noexcept   { return slope >= 1.0f ? 2 : 1; }

    FilterType toneType (int band) noexcept
    {
        return band == 0 ? FilterType::lowShelf : (band == 4 ? FilterType::highShelf : FilterType::bell);
    }

    // Índices de frequência, ganho e Q de cada banda de tom (Q = -1: fixo em 0,707).
    constexpr int toneParams[5][3] = {
        { EqProcessor::lowFreq,  EqProcessor::lowGain,  -1 },
        { EqProcessor::b1Freq,   EqProcessor::b1Gain,   EqProcessor::b1Q },
        { EqProcessor::b2Freq,   EqProcessor::b2Gain,   EqProcessor::b2Q },
        { EqProcessor::b3Freq,   EqProcessor::b3Gain,   EqProcessor::b3Q },
        { EqProcessor::highFreq, EqProcessor::highGain, -1 },
    };

}

const FxInfo& EqProcessor::staticInfo()
{
    static const FxInfo info { "eq", "Equalizador", "EQ", "Equalização",
                               "Corta o que embola e realça o que precisa aparecer. Sete faixas e analisador.",
                               params };
    return info;
}

void EqProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (auto& b : tone)
    {
        b.logFreq.prepare (rate, 30.0f);
        b.gainDb.prepare (rate, 30.0f);
        b.q.prepare (rate, 30.0f);
    }

    hp.type = FilterType::highpass;
    lp.type = FilterType::lowpass;

    for (auto* c : { &hp, &lp })
    {
        c->logFreq.prepare (rate, 30.0f);
        c->amount.prepare (rate, 7.0f);   // ~20 ms para ligar ou desligar
        c->filters.prepare (rate);
    }

    outGain.prepare (rate, 20.0f);

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = params[i].def;

    dirty = true;
    snapToTargets();
    reset();
}

void EqProcessor::snapToTargets() noexcept
{
    updateTargets();

    for (auto& b : tone)
    {
        b.logFreq.snap (b.logFreq.getTarget());
        b.gainDb.snap (b.gainDb.getTarget());
        b.q.snap (b.q.getTarget());
    }

    for (auto* c : { &hp, &lp })
    {
        c->logFreq.snap (c->logFreq.getTarget());
        c->amount.snap (c->amount.getTarget());
        c->filters.finishFade();
    }

    outGain.snap (outGain.getTarget());
    updateCoefficients();
}

void EqProcessor::reset() noexcept
{
    for (auto& b : tone)
    {
        b.left.reset();
        b.right.reset();
    }

    hp.filters.reset();
    lp.filters.reset();
    updateCoefficients();
}

void EqProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;
    dirty = true;
}

void EqProcessor::updateTargets() noexcept
{
    dirty = false;

    for (int b = 0; b < numTone; ++b)
    {
        auto& band = tone[(size_t) b];
        band.type = toneType (b);
        band.logFreq.setTarget (std::log2 (values[(size_t) toneParams[b][0]]));
        band.gainDb.setTarget (values[(size_t) toneParams[b][1]]);
        band.q.setTarget (toneParams[b][2] >= 0 ? values[(size_t) toneParams[b][2]] : 0.7071f);
    }

    hp.amount.setTarget (values[hpOn] >= 0.5f ? 1.0f : 0.0f);
    lp.amount.setTarget (values[lpOn] >= 0.5f ? 1.0f : 0.0f);
    hp.logFreq.setTarget (std::log2 (values[hpFreq]));
    lp.logFreq.setTarget ((float) std::log2 (std::min ((double) values[lpFreq], sampleRate * 0.45)));
    hp.filters.setSections (hpSections (values[hpSlope]));
    lp.filters.setSections (lpSections (values[lpSlope]));
    outGain.setTarget (dbToGain (values[output]));
}

void EqProcessor::updateCut (Cut& c) noexcept
{
    const bool wasActive = c.active;
    c.active = c.amount.getTarget() > 0.0f || c.amount.isSmoothing() || c.amount.getCurrent() > 0.0f;

    if (! c.active)
    {
        if (wasActive)
            c.filters.reset();

        return;
    }

    if (! wasActive)
        c.filters.reset();   // liga do zero: a mistura começa em 0 e esconde a partida do filtro

    const int n = c.filters.sections();

    for (int s = 0; s < n; ++s)
        c.filters.setCoeffs (s, SvfCoeffs::make (c.type, std::exp2 ((double) c.logFreq.getCurrent()), butterworthQ (n, s), 0.0, sampleRate));
}

void EqProcessor::updateCoefficients() noexcept
{
    for (auto& b : tone)
    {
        const bool wasActive = b.active;
        b.active = b.gainDb.getCurrent() != 0.0f || b.gainDb.getTarget() != 0.0f;

        if (b.active)
        {
            b.coeffs = SvfCoeffs::make (b.type, std::exp2 ((double) b.logFreq.getCurrent()), b.q.getCurrent(),
                                        b.gainDb.getCurrent(), sampleRate);
        }
        else if (wasActive)
        {
            b.left.reset();
            b.right.reset();
        }
    }

    updateCut (hp);
    updateCut (lp);
}

void EqProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        updateTargets();

    for (int start = 0; start < block.numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, block.numSamples - start);
        bool smoothing = start == 0;

        for (auto& b : tone)
            smoothing |= b.logFreq.isSmoothing() || b.gainDb.isSmoothing() || b.q.isSmoothing();

        smoothing |= hp.logFreq.isSmoothing() || lp.logFreq.isSmoothing() || hp.amount.isSmoothing() || lp.amount.isSmoothing();

        if (smoothing)
        {
            for (auto& b : tone)
            {
                b.logFreq.skip (n);
                b.gainDb.skip (n);
                b.q.skip (n);
            }

            hp.logFreq.skip (n);
            lp.logFreq.skip (n);
            updateCoefficients();
        }

        float* l = block.left + start;
        float* r = block.right + start;

        for (int i = 0; i < n; ++i)
        {
            float xl = l[i], xr = r[i];

            if (hp.active)
            {
                float yl = xl, yr = xr;
                hp.filters.process (yl, yr);
                const float a = hp.amount.next();
                xl += a * (yl - xl);
                xr += a * (yr - xr);
            }

            for (auto& b : tone)
                if (b.active)
                {
                    xl = b.left.process (xl, b.coeffs);
                    xr = b.right.process (xr, b.coeffs);
                }

            if (lp.active)
            {
                float yl = xl, yr = xr;
                lp.filters.process (yl, yr);
                const float a = lp.amount.next();
                xl += a * (yl - xl);
                xr += a * (yr - xr);
            }

            const float g = outGain.next();
            l[i] = xl * g;
            r[i] = xr * g;
            tap.push (0.5f * (l[i] + r[i]));
        }
    }
}

double EqProcessor::responseDb (const float* v, double freq, double sampleRate)
{
    double db = 0.0;

    for (int b = 0; b < numTone; ++b)
    {
        const float gain = v[toneParams[b][1]];

        if (gain == 0.0f)
            continue;

        const float q = toneParams[b][2] >= 0 ? v[toneParams[b][2]] : 0.7071f;
        db += SvfCoeffs::make (toneType (b), v[toneParams[b][0]], q, gain, sampleRate).magnitudeDb (freq, sampleRate);
    }

    if (v[hpOn] >= 0.5f)
    {
        const int n = hpSections (v[hpSlope]);

        for (int s = 0; s < n; ++s)
            db += SvfCoeffs::make (FilterType::highpass, v[hpFreq], butterworthQ (n, s), 0.0, sampleRate).magnitudeDb (freq, sampleRate);
    }

    if (v[lpOn] >= 0.5f)
    {
        const int n = lpSections (v[lpSlope]);

        for (int s = 0; s < n; ++s)
            db += SvfCoeffs::make (FilterType::lowpass, std::min ((double) v[lpFreq], sampleRate * 0.45), butterworthQ (n, s), 0.0, sampleRate)
                      .magnitudeDb (freq, sampleRate);
    }

    return db + v[output];
}

} // namespace whynot::dsp
