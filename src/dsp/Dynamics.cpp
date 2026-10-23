#include "Dynamics.h"

namespace whynot::dsp
{

//==============================================================================
// Compressor

namespace
{
    const char* const detectors[] = { "Pico", "RMS" };

    const ParamInfo compParams[] = {
        { "threshold", "Limiar",    -60, 0, -18,    "dB", ParamKind::continuous, false, {}, "Acima deste nível o compressor age." },
        { "ratio",     "Razão",     1, 20, 4,       ":1", ParamKind::continuous, true,  {}, "4:1 = cada 4 dB acima do limiar viram 1 dB." },
        { "attack",    "Ataque",    0.1f, 100, 10,  "ms", ParamKind::continuous, true,  {}, "Ataque lento deixa passar o estalo (punch)." },
        { "release",   "Soltura",   10, 1500, 120,  "ms", ParamKind::continuous, true,  {}, "Quanto tempo leva para soltar depois do pico." },
        { "knee",      "Joelho",    0, 18, 6,       "dB", ParamKind::continuous, false, {}, "Joelho largo = entrada mais suave." },
        { "makeup",    "Ganho",     -12, 24, 0,     "dB", ParamKind::continuous, false, {}, "Devolve o volume perdido na compressão." },
        { "mix",       "Mistura",   0, 100, 100,    "%",  ParamKind::continuous, false, {}, "Abaixo de 100% vira compressão paralela." },
        { "detector",  "Detector",  0, 1, 0,        "",   ParamKind::choice,     false, detectors, "Pico reage a tudo; RMS segue o volume médio." },
        { "scHpf",     "Filtro do detector", 20, 500, 20, "Hz", ParamKind::continuous, true, {}, "Graves abaixo daqui não disparam o compressor." },
    };

    static_assert (std::size (compParams) == CompressorProcessor::count);
}

const FxInfo& CompressorProcessor::staticInfo()
{
    static const FxInfo info { "comp", "Compressor", "Comp", "Dinâmica",
                               "Segura os picos e dá corpo. Aceita sidechain de outra trilha (ex.: o kick abaixa o 808).",
                               compParams, true };
    return info;
}

void CompressorProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = compParams[i].def;

    rmsCoeff = onePoleCoeff (10.0f, rate);
    makeupGain.prepare (rate, 20.0f);
    wet.prepare (rate, 20.0f);
    dirty = true;
    snapToTargets();
    reset();
}

void CompressorProcessor::snapToTargets() noexcept
{
    update();
    makeupGain.snap (makeupGain.getTarget());
    wet.snap (wet.getTarget());
}

void CompressorProcessor::reset() noexcept
{
    hpL.reset();
    hpR.reset();
    meanSquare = 0.0f;
    smoothedGainDb = 0.0f;
    setMeter (gainReduction, 0.0f);
}

void CompressorProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void CompressorProcessor::update() noexcept
{
    dirty = false;
    ballistics.set (values[attack], values[release], sampleRate);
    const bool wasUsingHp = useHp;
    useHp = values[scHpf] > 21.0f;

    if (useHp && ! wasUsingHp)
    {
        hpL.reset();
        hpR.reset();
    }

    detectorHp = SvfCoeffs::make (FilterType::highpass, values[scHpf], 0.7071, 0.0, sampleRate);
    makeupGain.setTarget (dbToGain (values[makeup]));
    wet.setTarget (values[mix] * 0.01f);
}

void CompressorProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const float thr = values[threshold], rat = values[ratio], kn = values[knee];
    const bool rms = values[detector] >= 0.5f;
    const bool external = block.sideLeft != nullptr && block.sideRight != nullptr;

    float maxReduction = 0.0f, inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float l = block.left[i], r = block.right[i];
        float dl = external ? block.sideLeft[i] : l;
        float dr = external ? block.sideRight[i] : r;

        if (useHp)
        {
            dl = hpL.process (dl, detectorHp);
            dr = hpR.process (dr, detectorHp);
        }

        // A média quadrática anda sempre: trocar Pico por RMS não parte de um valor velho.
        const float ms = 0.5f * (dl * dl + dr * dr);
        meanSquare = ms + (meanSquare - ms) * rmsCoeff;
        const float level = rms ? std::sqrt (meanSquare) : std::max (std::abs (dl), std::abs (dr));

        inPeak = std::max (inPeak, level);

        // Suavização no domínio de dB: ataque quando a redução aumenta, soltura quando diminui.
        const float target = compressorGainDb (gainToDb (level), thr, rat, kn);
        const float c = target < smoothedGainDb ? ballistics.attack : ballistics.release;
        smoothedGainDb = target + (smoothedGainDb - target) * c;
        maxReduction = std::min (maxReduction, smoothedGainDb);

        const float g = (smoothedGainDb > -1.0e-4f ? 1.0f : dbToGain (smoothedGainDb)) * makeupGain.next();
        const float w = wet.next();
        const float ol = l + w * (l * g - l);
        const float orr = r + w * (r * g - r);
        block.left[i] = ol;
        block.right[i] = orr;
        outPeak = std::max (outPeak, std::max (std::abs (ol), std::abs (orr)));
    }

    setMeter (gainReduction, -maxReduction);
    setMeter (inputLevel, gainToDb (inPeak));
    setMeter (outputLevel, gainToDb (outPeak));
}

//==============================================================================
// Transiente

namespace
{
    const ParamInfo transientParams[] = {
        { "attack",  "Ataque",      -100, 100, 0, "%",  ParamKind::continuous, false, {}, "Positivo: mais estalo. Negativo: ataque mais macio." },
        { "sustain", "Sustentação", -100, 100, 0, "%",  ParamKind::continuous, false, {}, "Positivo: cauda mais longa. Negativo: som mais curto e seco." },
        { "output",  "Saída",       -24, 12, 0,   "dB" },
        { "mix",     "Mistura",     0, 100, 100,  "%" },
    };

    static_assert (std::size (transientParams) == TransientProcessor::count);
}

const FxInfo& TransientProcessor::staticInfo()
{
    static const FxInfo info { "transient", "Transiente", "Trans", "Dinâmica",
                               "Mais punch no kick e na caixa, ou chimbal mais curto, sem mexer no volume médio.",
                               transientParams };
    return info;
}

void TransientProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = transientParams[i].def;

    fastAttack.set (0.3f, 60.0f, rate);
    slowAttack.set (20.0f, 60.0f, rate);
    shortRelease.set (0.3f, 20.0f, rate);
    longRelease.set (0.3f, 250.0f, rate);

    for (auto* s : { &attackAmount, &sustainAmount, &outGain, &wet })
        s->prepare (rate, 20.0f);

    gainDb.prepare (rate, 1.0f);
    attackAmount.snap (0.0f);
    sustainAmount.snap (0.0f);
    outGain.snap (1.0f);
    wet.snap (1.0f);
    reset();
}

void TransientProcessor::snapToTargets() noexcept
{
    for (auto* s : { &attackAmount, &sustainAmount, &outGain, &wet })
        s->snap (s->getTarget());
}

void TransientProcessor::reset() noexcept
{
    envFastA = envSlowA = envLongR = envShortR = 0.0f;
    gainDb.snap (0.0f);
}

void TransientProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;

    switch (index)
    {
        case attack:  attackAmount.setTarget (value * 0.01f); break;
        case sustain: sustainAmount.setTarget (value * 0.01f); break;
        case output:  outGain.setTarget (dbToGain (value)); break;
        case mix:     wet.setTarget (value * 0.01f); break;
        default: break;
    }
}

void TransientProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;
    constexpr float floorLevel = 1.0e-5f;
    float maxChange = 0.0f;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float l = block.left[i], r = block.right[i];
        const float level = std::max (std::abs (l), std::abs (r));

        envFastA = fastAttack.apply (envFastA, level);
        envSlowA = slowAttack.apply (envSlowA, level);
        envShortR = shortRelease.apply (envShortR, level);
        envLongR = longRelease.apply (envLongR, level);

        const float att = attackAmount.next(), sus = sustainAmount.next();
        float target = 0.0f;

        if (att != 0.0f || sus != 0.0f)
        {
            constexpr float toDb = 8.6858896f;   // 20 / ln(10)
            // Ataque: o envelope rápido passa à frente do lento só no começo das notas.
            const float onsetDb = att != 0.0f ? std::clamp (toDb * std::log (std::max (envFastA, floorLevel) / std::max (envSlowA, floorLevel)), 0.0f, 24.0f) : 0.0f;
            // Cauda: o envelope de soltura longa fica acima do de soltura curta enquanto o som decai.
            const float tailDb = sus != 0.0f ? std::clamp (toDb * std::log (std::max (envLongR, floorLevel) / std::max (envShortR, floorLevel)), 0.0f, 24.0f) : 0.0f;
            target = std::clamp (att * onsetDb * 1.5f + sus * tailDb, -18.0f, 18.0f);
        }

        gainDb.setTarget (target);
        const float change = gainDb.next();
        maxChange = std::abs (change) > std::abs (maxChange) ? change : maxChange;

        const float g = (change == 0.0f ? 1.0f : dbToGain (change)) * outGain.next();
        const float w = wet.next();
        block.left[i] = l + w * (l * g - l);
        block.right[i] = r + w * (r * g - r);
    }

    setMeter (gainChange, maxChange);
}

//==============================================================================
// De-esser

namespace
{
    const char* const deesserModes[] = { "Só agudos", "Banda toda" };

    const ParamInfo deesserParams[] = {
        { "freq",      "Frequência",     2000, 16000, 6500, "Hz", ParamKind::continuous, true,  {}, "Onde fica o chiado do \"s\" (voz grave ~5 kHz, aguda ~7-8 kHz)." },
        { "threshold", "Limiar",         -60, 0, -30,       "dB", ParamKind::continuous, false, {}, "Acima disto o de-esser reduz." },
        { "range",     "Redução máxima", 0, 24, 8,          "dB" },
        { "mode",      "Modo",           0, 1, 0,           "",   ParamKind::choice,     false, deesserModes, "Só agudos soa mais natural." },
        { "listen",    "Ouvir sibilância", 0, 1, 0,         "",   ParamKind::toggle,     false, {}, "Toca só o que o detector escuta, para achar a frequência." },
    };

    static_assert (std::size (deesserParams) == DeEsserProcessor::count);
}

const FxInfo& DeEsserProcessor::staticInfo()
{
    static const FxInfo info { "deesser", "De-esser", "De-ess", "Dinâmica",
                               "Tira o chiado dos \"s\" e \"x\" da voz depois de comprimir e realçar os agudos.",
                               deesserParams };
    return info;
}

void DeEsserProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = deesserParams[i].def;

    ballistics.set (0.5f, 60.0f, rate);
    gain.prepare (rate, 2.0f);
    gain.snap (1.0f);
    wideMix.prepare (rate, 4.0f);
    listenMix.prepare (rate, 4.0f);
    dirty = true;
    snapToTargets();
    reset();
}

void DeEsserProcessor::snapToTargets() noexcept
{
    update();
    wideMix.snap (wideMix.getTarget());
    listenMix.snap (listenMix.getTarget());
}

void DeEsserProcessor::reset() noexcept
{
    detect.reset();
    splitL.reset();
    splitR.reset();
    env = 0.0f;
    gain.snap (1.0f);
}

void DeEsserProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void DeEsserProcessor::update() noexcept
{
    dirty = false;
    detectCoeffs = SvfCoeffs::make (FilterType::bandpass, values[freq], 1.2, 0.0, sampleRate);
    splitCoeffs = SvfCoeffs::make (FilterType::highpass, values[freq] * 0.7f, 0.7071, 0.0, sampleRate);
    wideMix.setTarget (values[mode] >= 0.5f ? 1.0f : 0.0f);
    listenMix.setTarget (values[listen] >= 0.5f ? 1.0f : 0.0f);
}

void DeEsserProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const float thr = values[threshold], maxRange = values[range];
    float maxReduction = 0.0f;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float l = block.left[i], r = block.right[i];
        const float band = detect.process (0.5f * (l + r), detectCoeffs);
        env = ballistics.apply (env, std::abs (band));

        // Razão 4:1 acima do limiar, até a redução máxima.
        const float reductionDb = std::clamp ((gainToDb (env) - thr) * 0.75f, 0.0f, maxRange);
        maxReduction = std::max (maxReduction, reductionDb);
        gain.setTarget (dbToGain (-reductionDb));
        const float g = gain.next();

        const float hl = splitL.process (l, splitCoeffs);
        const float hr = splitR.process (r, splitCoeffs);

        // Só agudos, banda toda ou ouvir: misturas suaves, sem degrau ao trocar tocando.
        const float wm = wideMix.next(), lm = listenMix.next();
        float yl = l + (g - 1.0f) * hl, yr = r + (g - 1.0f) * hr;
        yl += wm * (l * g - yl);
        yr += wm * (r * g - yr);
        yl += lm * (hl - yl);
        yr += lm * (hr - yr);
        block.left[i] = yl;
        block.right[i] = yr;
    }

    setMeter (reduction, maxReduction);
}

} // namespace whynot::dsp
