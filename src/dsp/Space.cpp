#include "Space.h"

namespace whynot::dsp
{

namespace
{
    void hadamard8 (float* x) noexcept
    {
        for (int h = 1; h < 8; h *= 2)
            for (int i = 0; i < 8; i += h * 2)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }

        constexpr float scale = 0.35355339f;   // 1 / sqrt (8): a matriz fica ortogonal (estável)

        for (int i = 0; i < 8; ++i)
            x[i] *= scale;
    }

    //==========================================================================
    const char* const reverbModes[] = { "Sala", "Placa", "Salão" };

    const ParamInfo reverbParams[] = {
        { "mode",     "Tipo",          0, 2, 1,          "",   ParamKind::choice,     false, reverbModes, "Sala: curta e natural. Placa: densa e brilhante (voz). Salão: grande." },
        { "size",     "Tamanho",       0, 100, 50,       "%" },
        { "decay",    "Decaimento",    0.2f, 12, 1.6f,   "s",  ParamKind::continuous, true,  {}, "Tempo até a cauda cair 60 dB." },
        { "predelay", "Pré-atraso",    0, 250, 20,       "ms", ParamKind::continuous, false, {}, "Separa a voz da cauda: mais clareza." },
        { "damping",  "Abafar agudos", 1000, 20000, 8000, "Hz", ParamKind::continuous, true, {}, "Agudos acima daqui somem mais rápido na cauda." },
        { "lowCut",   "Corte grave",   20, 1000, 200,    "Hz", ParamKind::continuous, true,  {}, "Tira grave do reverb para não embolar." },
        { "width",    "Largura",       0, 100, 100,      "%" },
        { "mix",      "Mistura",       0, 100, 25,       "%",  ParamKind::continuous, false, {}, "Em trilha de envio (bus), use 100%." },
    };

    static_assert (std::size (reverbParams) == ReverbProcessor::count);

    struct ReverbMode
    {
        float lines[8];
        float diffusers[4];
        float diffusion;
        float modMs;
    };

    const ReverbMode reverbModeData[] = {
        { { 9.13f, 11.71f, 13.39f, 15.97f, 17.63f, 19.81f, 22.37f, 24.53f },  { 2.41f, 1.83f, 6.37f, 4.66f },  0.62f, 0.10f },
        { { 13.67f, 16.33f, 19.21f, 21.53f, 24.29f, 26.71f, 29.83f, 32.17f }, { 3.82f, 2.87f, 10.19f, 7.45f }, 0.72f, 0.25f },
        { { 27.31f, 31.93f, 36.67f, 41.27f, 46.19f, 51.47f, 56.83f, 62.11f }, { 4.77f, 3.59f, 12.73f, 9.31f }, 0.66f, 0.35f },
    };

    constexpr float maxSizeFactor = 1.6f;
}

const FxInfo& ReverbProcessor::staticInfo()
{
    static const FxInfo info { "reverb", "Reverb", "Reverb", "Espaço",
                               "Ambiência de sala, placa ou salão. Melhor numa trilha de envio, com 100% de mistura.",
                               reverbParams };
    return info;
}

void ReverbProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = reverbParams[i].def;

    const int maxLine = (int) std::ceil (62.11 * maxSizeFactor * rate / 1000.0 + 0.5 * rate / 1000.0) + 8;

    for (auto& l : lines)
        l.prepare (maxLine);

    const int maxDiffuser = (int) std::ceil (12.73 * 1.07 * rate / 1000.0) + 4;

    for (int i = 0; i < numDiffusers; ++i)
    {
        diffuseL[(size_t) i].line.prepare (maxDiffuser);
        diffuseR[(size_t) i].line.prepare (maxDiffuser);
    }

    preL.prepare ((int) (0.26 * rate) + 8);
    preR.prepare ((int) (0.26 * rate) + 8);

    sizeFactor.prepare (rate, 300.0f);
    predelaySamples.prepare (rate, 80.0f);
    wet.prepare (rate, 20.0f);
    widthAmount.prepare (rate, 20.0f);
    modeGain.prepare (rate, 3.0f);

    currentMode = -1;
    sizeFactor.setTarget (0.4f + 1.2f * values[size] * 0.01f);
    predelaySamples.setTarget (values[predelay] * 0.001f * (float) rate);
    wet.setTarget (values[mix] * 0.01f);
    widthAmount.setTarget (values[width] * 0.01f);
    snapToTargets();
    reset();
}

void ReverbProcessor::snapToTargets() noexcept
{
    configureMode();

    for (auto* s : { &sizeFactor, &predelaySamples, &wet, &widthAmount })
        s->snap (s->getTarget());

    modeGain.snap (1.0f);
    lowCutCoeffs = SvfCoeffs::make (FilterType::highpass, values[lowCut], 0.7071, 0.0, sampleRate);
    updateLoop();
}

bool ReverbProcessor::clearStep (int budget) noexcept
{
    std::array<DelayLine*, numLines + 2 * numDiffusers + 2> all {};
    size_t n = 0;

    for (auto& l : lines) all[n++] = &l;
    for (auto& a : diffuseL) all[n++] = &a.line;
    for (auto& a : diffuseR) all[n++] = &a.line;

    all[n++] = &preL;
    all[n++] = &preR;

    if (! clearer.step (all, budget))
        return false;

    resetSmallState();
    return true;
}

void ReverbProcessor::reset() noexcept
{
    for (auto& l : lines) l.reset();
    for (auto& a : diffuseL) a.line.reset();
    for (auto& a : diffuseR) a.line.reset();

    preL.reset();
    preR.reset();
    clearer = {};
    resetSmallState();
}

void ReverbProcessor::resetSmallState() noexcept
{
    lowCutL.reset();
    lowCutR.reset();
    dampState.fill (0.0f);

    for (int i = 0; i < numLines; ++i)
    {
        const double phase = 2.0 * pi * i / numLines;
        lfoCos[(size_t) i] = (float) std::cos (phase);
        lfoSin[(size_t) i] = (float) std::sin (phase);
    }

    updateLoop();
}

void ReverbProcessor::configureMode() noexcept
{
    const int m = std::clamp ((int) values[mode], 0, 2);

    if (m == currentMode)
        return;

    currentMode = m;
    const auto& data = reverbModeData[m];

    for (int i = 0; i < numLines; ++i)
    {
        baseDelayMs[(size_t) i] = data.lines[i];

        // LFOs lentos e diferentes por linha (0,35 a 0,95 Hz).
        const double step = 2.0 * pi * (0.35 + 0.6 * i / (numLines - 1)) / sampleRate;
        lfoStepCos[(size_t) i] = (float) std::cos (step);
        lfoStepSin[(size_t) i] = (float) std::sin (step);
    }

    for (int i = 0; i < numDiffusers; ++i)
    {
        diffuseL[(size_t) i].delay = std::max (1, (int) std::lround (data.diffusers[i] * sampleRate / 1000.0));
        diffuseR[(size_t) i].delay = std::max (1, (int) std::lround (data.diffusers[i] * 1.07 * sampleRate / 1000.0));
        diffuseL[(size_t) i].gain = diffuseR[(size_t) i].gain = data.diffusion;
    }

    modDepth = (float) (data.modMs * sampleRate / 1000.0);
}

void ReverbProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;

    switch (index)
    {
        case mode:     if ((int) value != currentMode) modeGain.setTarget (0.0f); break;   // troca quando o molhado sumir
        case size:     sizeFactor.setTarget (0.4f + 1.2f * value * 0.01f); break;
        case predelay: predelaySamples.setTarget (value * 0.001f * (float) sampleRate); break;
        case lowCut:   lowCutCoeffs = SvfCoeffs::make (FilterType::highpass, value, 0.7071, 0.0, sampleRate); break;
        case width:    widthAmount.setTarget (value * 0.01f); break;
        case mix:      wet.setTarget (value * 0.01f); break;
        default: break;
    }
}

void ReverbProcessor::updateLoop() noexcept
{
    const float sf = sizeFactor.getCurrent();
    const double rt60 = std::max (0.05, (double) values[decay]);

    for (int i = 0; i < numLines; ++i)
    {
        const double d = baseDelayMs[(size_t) i] * sf * sampleRate / 1000.0;
        delaySamples[(size_t) i] = (float) d;
        loopGain[(size_t) i] = (float) std::pow (10.0, -3.0 * d / (sampleRate * rt60));
    }

    damp = (float) std::exp (-2.0 * pi * values[damping] / sampleRate);

    // Corrige o desvio numérico dos osciladores de rotação.
    for (int i = 0; i < numLines; ++i)
    {
        const float mag = std::sqrt (lfoCos[(size_t) i] * lfoCos[(size_t) i] + lfoSin[(size_t) i] * lfoSin[(size_t) i]);

        if (mag > 0.0f)
        {
            lfoCos[(size_t) i] /= mag;
            lfoSin[(size_t) i] /= mag;
        }
    }
}

void ReverbProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    for (int start = 0; start < block.numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, block.numSamples - start);
        sizeFactor.skip (n);
        updateLoop();

        for (int i = start; i < start + n; ++i)
        {
            const float dryL = block.left[i], dryR = block.right[i];

            if (modeGain.getTarget() == 0.0f && modeGain.getCurrent() == 0.0f)
            {
                configureMode();
                updateLoop();
                modeGain.setTarget (1.0f);
            }

            preL.push (dryL);
            preR.push (dryR);
            const float pd = 1.0f + predelaySamples.next();
            float xl = lowCutL.process (preL.readHermite (pd), lowCutCoeffs);
            float xr = lowCutR.process (preR.readHermite (pd), lowCutCoeffs);

            for (int d = 0; d < numDiffusers; ++d)
            {
                xl = diffuseL[(size_t) d].process (xl);
                xr = diffuseR[(size_t) d].process (xr);
            }

            float y[numLines];

            for (int l = 0; l < numLines; ++l)
            {
                auto& c = lfoCos[(size_t) l];
                auto& s = lfoSin[(size_t) l];
                const float nc = c * lfoStepCos[(size_t) l] - s * lfoStepSin[(size_t) l];
                s = s * lfoStepCos[(size_t) l] + c * lfoStepSin[(size_t) l];
                c = nc;

                const float delay = delaySamples[(size_t) l] + modDepth * s;
                const float v = lines[(size_t) l].readLinear (std::max (0.0f, delay - 1.0f));
                auto& state = dampState[(size_t) l];
                state = v + damp * (state - v);
                y[l] = state * loopGain[(size_t) l];
            }

            float wl = 0.6f * (y[0] + y[2] + y[4] + y[6]);
            float wr = 0.6f * (y[1] + y[3] + y[5] + y[7]);

            hadamard8 (y);

            for (int l = 0; l < numLines; ++l)
                lines[(size_t) l].push (y[l] + 0.5f * ((l & 1) == 0 ? xl : xr));

            const float mg = modeGain.next();
            const float mid = 0.5f * (wl + wr) * mg;
            const float side = 0.5f * (wl - wr) * widthAmount.next() * mg;
            wl = mid + side;
            wr = mid - side;

            const float w = wet.next();
            block.left[i] = dryL + w * (wl - dryL);
            block.right[i] = dryR + w * (wr - dryR);
        }
    }
}

//==============================================================================
namespace
{
    const char* const divisions[] = { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1 compasso" };
    const double divisionBeats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    const char* const delayModes[] = { "Estéreo", "Pingue-pongue", "Mono" };

    const ParamInfo delayParams[] = {
        { "sync",     "Sincronizar", 0, 1, 1,          "",   ParamKind::toggle,     false, {}, "Segue o BPM do projeto." },
        { "division", "Divisão",     0, 11, 8,         "",   ParamKind::choice,     false, divisions, "T = tercina; ponto = pontuada (1,5x)." },
        { "time",     "Tempo",       1, 2000, 250,     "ms", ParamKind::continuous, true,  {}, "Usado com a sincronia desligada." },
        { "feedback", "Repetições",  0, 95, 30,        "%" },
        { "mode",     "Modo",        0, 2, 1,          "",   ParamKind::choice,     false, delayModes },
        { "hpf",      "Corte grave", 20, 2000, 250,    "Hz", ParamKind::continuous, true },
        { "lpf",      "Corte agudo", 1000, 20000, 6000, "Hz", ParamKind::continuous, true, {}, "Repetições mais escuras ficam atrás da voz." },
        { "ducking",  "Abaixar sob o som", 0, 100, 0,  "%",  ParamKind::continuous, false, {}, "O eco abaixa enquanto a voz canta e volta nas pausas." },
        { "width",    "Largura",     0, 100, 100,      "%" },
        { "mix",      "Mistura",     0, 100, 25,       "%",  ParamKind::continuous, false, {}, "Em trilha de envio (bus), use 100%." },
        { "bpm",      "Andamento",   20, 999, 120,     "BPM", ParamKind::continuous, false, {}, "Vem do projeto.", true },
    };

    static_assert (std::size (delayParams) == DelayProcessor::count);
}

const FxInfo& DelayProcessor::staticInfo()
{
    static const FxInfo info { "delay", "Delay", "Delay", "Espaço",
                               "Eco no tempo da música: 1/4 e 1/8 pontuado na voz, pingue-pongue para abrir.",
                               delayParams };
    return info;
}

double DelayProcessor::delayMs (const float* v) noexcept
{
    if (v[sync] >= 0.5f)
    {
        const int d = std::clamp ((int) v[division], 0, (int) std::size (divisionBeats) - 1);
        return divisionBeats[d] * 60000.0 / std::max (20.0f, v[bpm]);
    }

    return v[time];
}

void DelayProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = delayParams[i].def;

    for (auto& l : lines)
        l.prepare ((int) (maxSeconds * rate) + 8);

    delaySamples.prepare (rate, 120.0f);
    fb.prepare (rate, 20.0f);
    wet.prepare (rate, 20.0f);
    widthAmount.prepare (rate, 20.0f);
    monoMix.prepare (rate, 7.0f);
    duckAmount.prepare (rate, 20.0f);
    duckBallistics.set (5.0f, 250.0f, rate);

    dirty = true;
    snapToTargets();
    reset();
}

void DelayProcessor::snapToTargets() noexcept
{
    update();

    for (auto* s : { &delaySamples, &fb, &wet, &widthAmount, &monoMix, &duckAmount })
        s->snap (s->getTarget());
}

bool DelayProcessor::clearStep (int budget) noexcept
{
    if (! clearer.step (std::array<DelayLine*, 2> { &lines[0], &lines[1] }, budget))
        return false;

    for (auto& f : hpState) f.reset();
    for (auto& f : lpState) f.reset();
    duckEnv = 0.0f;
    return true;
}

void DelayProcessor::reset() noexcept
{
    clearer = {};

    for (auto& l : lines) l.reset();
    for (auto& f : hpState) f.reset();
    for (auto& f : lpState) f.reset();
    duckEnv = 0.0f;
}

void DelayProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void DelayProcessor::update() noexcept
{
    dirty = false;
    const double samples = delayMs (values.data()) * sampleRate / 1000.0;
    delaySamples.setTarget ((float) std::clamp (samples, 2.0, maxSeconds * sampleRate - 4.0));
    fb.setTarget (values[feedback] * 0.01f);
    wet.setTarget (values[mix] * 0.01f);
    widthAmount.setTarget (values[width] * 0.01f);
    monoMix.setTarget ((int) values[mode] == 2 ? 1.0f : 0.0f);
    duckAmount.setTarget (values[ducking] * 0.01f);
    hpCoeffs = SvfCoeffs::make (FilterType::highpass, values[hpf], 0.7071, 0.0, sampleRate);
    lpCoeffs = SvfCoeffs::make (FilterType::lowpass, values[lpf], 0.7071, 0.0, sampleRate);
}

double DelayProcessor::tailSecondsFor (const float* v) const noexcept
{
    const double seconds = delayMs (v) / 1000.0;
    const double f = v[feedback] * 0.01;
    const double repeats = f > 0.01 ? 60.0 / (-20.0 * std::log10 (f)) : 1.0;
    return std::min (30.0, seconds * (repeats + 1.0) + 0.1);
}

void DelayProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const int m = (int) values[mode];

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float inL = block.left[i], inR = block.right[i];
        const float d = delaySamples.next() - 1.0f;
        const float f = fb.next();

        float wl = lpState[0].process (hpState[0].process (lines[0].readHermite (d), hpCoeffs), lpCoeffs);
        float wr = lpState[1].process (hpState[1].process (lines[1].readHermite (d), hpCoeffs), lpCoeffs);
        wr += monoMix.next() * (wl - wr);   // em mono, a direita desliza para a esquerda

        // tanh na realimentação: com muitas repetições o eco satura em vez de explodir.
        if (m == 0)
        {
            lines[0].push (inL + std::tanh (f * wl));
            lines[1].push (inR + std::tanh (f * wr));
        }
        else if (m == 1)
        {
            lines[0].push (0.5f * (inL + inR) + std::tanh (f * wr));
            lines[1].push (std::tanh (f * wl));
        }
        else
        {
            lines[0].push (0.5f * (inL + inR) + std::tanh (f * wl));
            lines[1].push (0.0f);
        }

        duckEnv = duckBallistics.apply (duckEnv, std::max (std::abs (inL), std::abs (inR)));
        const float duck = duckAmount.next();
        const float duckGain = duck > 0.0f ? 1.0f - duck * std::clamp ((gainToDb (duckEnv) + 40.0f) / 30.0f, 0.0f, 1.0f) : 1.0f;

        const float mid = 0.5f * (wl + wr);
        const float side = 0.5f * (wl - wr) * widthAmount.next();
        wl = (mid + side) * duckGain;
        wr = (mid - side) * duckGain;

        const float w = wet.next();
        block.left[i] = inL + w * (wl - inL);
        block.right[i] = inR + w * (wr - inR);
    }
}

} // namespace whynot::dsp
