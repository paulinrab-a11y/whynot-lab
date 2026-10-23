#include "Color.h"

namespace whynot::dsp
{

namespace
{
    // tanh dos desvios das curvas assimétricas (constantes: não recalcular 4x por amostra).
    constexpr float tanhTubeBias = 0.29131261f;   // tanh (0,3)
    constexpr float tanhFuzzBias = 0.33637554f;   // tanh (0,35)

    bool isAsymmetric (SaturationMode m) noexcept   { return m == SaturationMode::tube || m == SaturationMode::fuzz; }
}

float saturate (SaturationMode mode, float x) noexcept
{
    switch (mode)
    {
        case SaturationMode::tape:
            return std::tanh (x);

        case SaturationMode::tube:
            // tanh deslocada: lados diferentes = harmônicos pares (o "quente" da válvula).
            return (std::tanh (x + 0.3f) - tanhTubeBias) / (1.0f - tanhTubeBias * tanhTubeBias);

        case SaturationMode::soft:
        {
            const float u = std::clamp (x * (2.0f / 3.0f), -1.0f, 1.0f);
            return 1.5f * (u - u * u * u / 3.0f);
        }

        case SaturationMode::hard:
            return std::clamp (x, -1.0f, 1.0f);

        case SaturationMode::fuzz:
            // Bem assimétrica: um lado esmaga em ~0,47 e o outro em ~0,94 (muito harmônico par).
            return (std::tanh (1.6f * x + 0.35f) - tanhFuzzBias) / (1.6f * (1.0f - tanhFuzzBias * tanhFuzzBias));
    }

    return x;
}

//==============================================================================
namespace
{
    const char* const satModes[] = { "Fita", "Válvula", "Suave", "Duro", "Fuzz" };

    const ParamInfo satParams[] = {
        { "mode",     "Tipo",            0, 4, 0,       "",   ParamKind::choice,     false, satModes, "Fita e Válvula aquecem; Suave e Duro cortam picos; Fuzz destrói." },
        { "drive",    "Drive",           0, 36, 6,      "dB", ParamKind::continuous, false, {}, "Quanto o sinal empurra a saturação." },
        { "tone",     "Tom",             -100, 100, 0,  "%",  ParamKind::continuous, false, {}, "Negativo escurece, positivo clareia." },
        { "protect",  "Proteger graves", 0, 300, 0,     "Hz", ParamKind::continuous, false, {}, "Abaixo daqui o som fica limpo (o sub do 808 não suja). 0 = desligado." },
        { "mix",      "Mistura",         0, 100, 100,   "%" },
        { "autoGain", "Compensar volume", 0, 1, 1,      "",   ParamKind::toggle,     false, {}, "Mantém o volume parecido para comparar só a cor." },
        { "output",   "Saída",           -24, 12, 0,    "dB" },
    };

    static_assert (std::size (satParams) == SaturatorProcessor::count);
}

const FxInfo& SaturatorProcessor::staticInfo()
{
    static const FxInfo info { "sat", "Saturador", "Sat", "Cor",
                               "Harmônicos que fazem o 808 aparecer no celular, calor na voz e peso no kick. Sobreamostrado 4x.",
                               satParams };
    return info;
}

void SaturatorProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = satParams[i].def;

    for (auto& os : oversamplers)
        os.prepare();

    latency = oversamplers[0].latency();

    for (auto& d : cleanDelay)
        d.prepare (latency + 4);

    for (auto& dc : dcBlock)
        dc.prepare (rate);

    driveGain.prepare (rate, 20.0f);
    wet.prepare (rate, 20.0f);
    outGain.prepare (rate, 20.0f);
    autoCompensation.prepare (rate, 120.0f);
    modeFade.prepare (rate, 3.0f);
    protectMix.prepare (rate, 7.0f);
    toneAmount.prepare (rate, 20.0f);
    rmsCoeff = onePoleCoeff (300.0f, rate);

    dirty = true;
    snapToTargets();
    reset();
}

void SaturatorProcessor::snapToTargets() noexcept
{
    update();
    previousMode = currentMode;

    for (auto* s : { &driveGain, &wet, &outGain, &modeFade, &protectMix, &toneAmount })
        s->snap (s->getTarget());

    modeFade.snap (1.0f);
    updateTilt();
}

void SaturatorProcessor::reset() noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        oversamplers[(size_t) c].reset();
        cleanDelay[(size_t) c].reset();
        cleanSplit[(size_t) c].reset();
        dirtySplit[(size_t) c].reset();
        lowTilt[(size_t) c].reset();
        highTilt[(size_t) c].reset();
        dcBlock[(size_t) c].reset();
    }

    inMeanSquare = outMeanSquare = 0.0f;
    autoCompensation.snap (1.0f);
}

void SaturatorProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void SaturatorProcessor::update() noexcept
{
    dirty = false;
    driveGain.setTarget (dbToGain (values[drive]));
    wet.setTarget (values[mix] * 0.01f);
    outGain.setTarget (dbToGain (values[output]));
    toneAmount.setTarget (values[tone] * 0.01f);

    // Trocar a curva tocando: as duas soam juntas por ~10 ms.
    const auto m = (SaturationMode) std::clamp ((int) values[mode], 0, 4);

    if (m != currentMode)
    {
        previousMode = modeFade.isSmoothing() ? previousMode : currentMode;
        currentMode = m;
        modeFade.snap (0.0f);
        modeFade.setTarget (1.0f);
    }

    // Proteger graves entra e sai por mistura; a última frequência fica até a mistura sumir.
    const bool on = values[protect] >= 20.0f;
    protectMix.setTarget (on ? 1.0f : 0.0f);

    if (on && values[protect] != protectFreq)
    {
        protectFreq = values[protect];

        for (int c = 0; c < 2; ++c)
        {
            cleanSplit[(size_t) c].setup (protectFreq, sampleRate);
            dirtySplit[(size_t) c].setup (protectFreq, sampleRate);
        }
    }
}

void SaturatorProcessor::updateTilt() noexcept
{
    const float t = toneAmount.getCurrent();
    lowTiltCoeffs = SvfCoeffs::make (FilterType::lowShelf, 250.0, 0.7071, -6.0 * t, sampleRate);
    highTiltCoeffs = SvfCoeffs::make (FilterType::highShelf, 2500.0, 0.7071, 6.0 * t, sampleRate);
}

void SaturatorProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    // Divisores e bloqueio de DC só rodam quando precisam; ao voltar, partem do zero.
    const bool protectNeeded = protectMix.getTarget() > 0.0f || protectMix.getCurrent() > 0.0f;

    if (protectNeeded && ! protectRunning)
        for (int c = 0; c < 2; ++c)
        {
            cleanSplit[(size_t) c].reset();
            dirtySplit[(size_t) c].reset();
        }

    protectRunning = protectNeeded && protectFreq >= 20.0f;

    const bool dcNeeded = isAsymmetric (currentMode) || (modeFade.isSmoothing() && isAsymmetric (previousMode));

    if (dcNeeded && ! dcActive)
        for (auto& dc : dcBlock)
            dc.reset();

    dcActive = dcNeeded;

    const bool compensate = values[autoGain] >= 0.5f;
    float* io[2] = { block.left, block.right };
    const auto now = currentMode, before = previousMode;

    for (int start = 0; start < block.numSamples; start += subBlock)
    {
        const int end = std::min (block.numSamples, start + subBlock);

        if (toneAmount.isSmoothing() || start == 0)
        {
            toneAmount.skip (end - start);
            updateTilt();
        }

        const bool tilting = std::abs (toneAmount.getCurrent()) > 0.0005f;

        for (int i = start; i < end; ++i)
        {
            const float g = driveGain.next();
            const float w = wet.next();
            const float out = outGain.next();
            const float fade = modeFade.isSmoothing() ? modeFade.next() : 1.0f;
            const float pm = protectMix.next();
            float inSq = 0.0f, outSq = 0.0f;

            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i];
                inSq += x * x;

                float y = oversamplers[(size_t) c].process (x, [now, before, g, w, fade] (float v)
                {
                    float s = saturate (now, v * g);

                    if (fade < 1.0f)
                    {
                        const float old = saturate (before, v * g);
                        s = old + fade * (s - old);
                    }

                    return v + w * (s - v);
                });

                auto& clean = cleanDelay[(size_t) c];
                clean.push (x);

                if (dcActive)
                    y = dcBlock[(size_t) c].process (y);

                if (protectRunning)
                {
                    float lowClean, highClean, lowDirty, highDirty;
                    cleanSplit[(size_t) c].process (clean.read (latency), lowClean, highClean);
                    dirtySplit[(size_t) c].process (y, lowDirty, highDirty);
                    y += pm * ((lowClean + highDirty) - y);
                }

                if (tilting)
                    y = highTilt[(size_t) c].process (lowTilt[(size_t) c].process (y, lowTiltCoeffs), highTiltCoeffs);

                outSq += y * y;
                io[c][i] = y;
            }

            if (compensate)
            {
                inMeanSquare = 0.5f * inSq + (inMeanSquare - 0.5f * inSq) * rmsCoeff;
                outMeanSquare = 0.5f * outSq + (outMeanSquare - 0.5f * outSq) * rmsCoeff;

                // Só compara com sinal presente; no silêncio, mantém a última compensação.
                if (inMeanSquare > 1.0e-6f && outMeanSquare > 1.0e-9f)
                    autoCompensation.setTarget (std::clamp (std::sqrt (inMeanSquare / outMeanSquare), 0.063f, 4.0f));
            }
            else
            {
                autoCompensation.setTarget (1.0f);
            }

            // A Saída vem depois da compensação: o knob sempre muda o volume.
            const float gain = autoCompensation.next() * out;
            io[0][i] *= gain;
            io[1][i] *= gain;
        }
    }

    if (! protectNeeded)
        protectRunning = false;

    setMeter (compensation, gainToDb (autoCompensation.getCurrent()));
}

//==============================================================================
namespace
{
    const char* const filterModes[] = { "Passa-baixas", "Passa-altas", "Passa-banda", "Rejeita-banda" };
    const char* const filterSlopes[] = { "12 dB/oit", "24 dB/oit" };

    const ParamInfo filterParams[] = {
        { "mode",      "Tipo",         0, 3, 0,          "",   ParamKind::choice,     false, filterModes },
        { "cutoff",    "Frequência",   20, 20000, 1000,  "Hz", ParamKind::continuous, true },
        { "resonance", "Ressonância",  0, 100, 20,       "%",  ParamKind::continuous, false, {}, "Realça a região do corte." },
        { "slope",     "Inclinação",   0, 1, 0,          "",   ParamKind::choice,     false, filterSlopes },
        { "drive",     "Drive",        0, 24, 0,         "dB", ParamKind::continuous, false, {}, "Satura antes do filtro." },
        { "mix",       "Mistura",      0, 100, 100,      "%" },
    };

    static_assert (std::size (filterParams) == FilterProcessor::count);

    FilterType filterTypeFor (float mode) noexcept
    {
        switch ((int) mode)
        {
            case 1:  return FilterType::highpass;
            case 2:  return FilterType::bandpass;
            case 3:  return FilterType::notch;
            default: return FilterType::lowpass;
        }
    }
}

const FxInfo& FilterProcessor::staticInfo()
{
    static const FxInfo info { "filter", "Filtro", "Filtro", "Cor",
                               "Voz de telefone, sample abafado de intro ou só o brilho de um synth.",
                               filterParams };
    return info;
}

void FilterProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = filterParams[i].def;

    logCutoff.prepare (rate, 30.0f);
    q.prepare (rate, 30.0f);
    driveGain.prepare (rate, 20.0f);
    driveAmount.prepare (rate, 20.0f);
    wet.prepare (rate, 20.0f);
    typeFade.prepare (rate, 3.0f);
    filters.prepare (rate);

    logCutoff.snap (std::log2 (values[cutoff]));
    q.snap (0.5f * std::pow (24.0f, values[resonance] * 0.01f));
    driveGain.snap (1.0f);
    driveAmount.snap (0.0f);
    wet.snap (1.0f);
    typeFade.snap (1.0f);
    reset();
}

void FilterProcessor::snapToTargets() noexcept
{
    for (auto* s : { &logCutoff, &q, &driveGain, &driveAmount, &wet })
        s->snap (s->getTarget());

    typeFade.snap (1.0f);
    previousType = currentType;
    filters.finishFade();
    updateCoefficients();
}

void FilterProcessor::reset() noexcept
{
    filters.reset();
    updateCoefficients();
}

void FilterProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;

    switch (index)
    {
        case cutoff:    logCutoff.setTarget (std::log2 (value)); break;
        case resonance: q.setTarget (0.5f * std::pow (24.0f, value * 0.01f)); break;
        case mix:       wet.setTarget (value * 0.01f); break;

        case drive:
            driveGain.setTarget (dbToGain (value));
            driveAmount.setTarget (std::min (1.0f, value));   // de 0 a 1 dB o drive entra aos poucos
            break;

        case slope:
            filters.setSections (value >= 0.5f ? 2 : 1);
            updateCoefficients();
            break;

        case mode:
        {
            const auto t = filterTypeFor (value);

            if (t != currentType)
            {
                // As saídas do filtro de estado dividem o mesmo estado: basta deslizar a mistura.
                previousType = currentType;
                currentType = t;
                typeFade.snap (0.0f);
                typeFade.setTarget (1.0f);
            }

            updateCoefficients();
            break;
        }

        default:
            break;
    }
}

void FilterProcessor::updateCoefficients() noexcept
{
    const double freq = std::exp2 ((double) logCutoff.getCurrent());
    const int sections = filters.sections();
    const double t = typeFade.getCurrent();

    for (int s = 0; s < sections; ++s)
    {
        // Em 24 dB, a ressonância fica só na segunda seção (a primeira é Butterworth).
        const double sectionQ = (sections == 2 && s == 0) ? 0.5412 : (double) q.getCurrent();
        auto c = SvfCoeffs::make (currentType, freq, sectionQ, 0.0, sampleRate);

        if (t < 1.0)
        {
            const auto old = SvfCoeffs::make (previousType, freq, sectionQ, 0.0, sampleRate);
            c.m0 = old.m0 + t * (c.m0 - old.m0);
            c.m1 = old.m1 + t * (c.m1 - old.m1);
            c.m2 = old.m2 + t * (c.m2 - old.m2);
        }

        filters.setCoeffs (s, c);
    }
}

void FilterProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    for (int start = 0; start < block.numSamples;)
    {
        // Na troca de tipo, os coeficientes andam a cada amostra (degraus de 8 viram chiado).
        const int n = std::min (typeFade.isSmoothing() ? 1 : subBlock, block.numSamples - start);

        if (logCutoff.isSmoothing() || q.isSmoothing() || typeFade.isSmoothing())
        {
            logCutoff.skip (n);
            q.skip (n);
            typeFade.skip (n);
            updateCoefficients();
        }

        const bool driving = driveAmount.getCurrent() > 0.0f || driveAmount.isSmoothing();

        for (int i = start; i < start + n; ++i)
        {
            const float g = driveGain.next();
            const float amt = driveAmount.next();
            const float w = wet.next();
            const float l = block.left[i], r = block.right[i];
            float fl = driving ? l + amt * (std::tanh (l * g) - l) : l;
            float fr = driving ? r + amt * (std::tanh (r * g) - r) : r;

            filters.process (fl, fr);

            block.left[i] = l + w * (fl - l);
            block.right[i] = r + w * (fr - r);
        }

        start += n;
    }
}

} // namespace whynot::dsp
