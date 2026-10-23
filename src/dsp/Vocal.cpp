#include "Vocal.h"

namespace whynot::dsp
{

//==============================================================================
// Nivelador de voz

namespace
{
    const ParamInfo levelerParams[] = {
        { "target",      "Alvo",          -40, -6, -18,  "dB", ParamKind::continuous, false, {},
          "O volume médio (RMS) que a voz deve ter. -18 dB é um bom ponto para depois comprimir." },
        { "range",       "Faixa",         0, 24, 10,     "dB", ParamKind::continuous, false, {},
          "Quanto o nivelador pode subir ou descer, no máximo." },
        { "speed",       "Velocidade",    50, 3000, 500, "ms", ParamKind::continuous, true,  {},
          "Rápido segura até as sílabas; devagar só iguala as frases (mais natural)." },
        { "sensitivity", "Sensibilidade", -80, -20, -50, "dB", ParamKind::continuous, false, {},
          "Abaixo disto é silêncio ou ruído: o ganho para e não sobe o chiado entre as frases." },
        { "output",      "Saída",         -12, 12, 0,    "dB" },
    };

    static_assert (std::size (levelerParams) == LevelerProcessor::count);
}

const FxInfo& LevelerProcessor::staticInfo()
{
    static const FxInfo info { "leveler", "Nivelador de voz", "Nivel.", "Voz",
                               "Sobe as partes baixas e desce as altas sozinho, como alguém mexendo no fader: a voz fica sempre na frente.",
                               levelerParams };
    return info;
}

void LevelerProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = levelerParams[i].def;

    hpCoeffs = SvfCoeffs::make (FilterType::highpass, 100.0, 0.7071, 0.0, rate);
    rmsCoeff = onePoleCoeff (30.0f, rate);
    maxDecayPerSample = (float) (20.0 / rate);
    outGain.prepare (rate, 20.0f);
    dirty = true;
    snapToTargets();
    reset();
}

void LevelerProcessor::snapToTargets() noexcept
{
    update();
    outGain.snap (outGain.getTarget());
}

void LevelerProcessor::reset() noexcept
{
    hpL.reset();
    hpR.reset();
    meanSquare = 0.0f;
    gainDb = 0.0f;
    recentMaxDb = -120.0f;
    setMeter (gainMeter, 0.0f);
    setMeter (activeMeter, 0.0f);
}

void LevelerProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void LevelerProcessor::update() noexcept
{
    dirty = false;
    upCoeff = onePoleCoeff (values[speed], sampleRate);
    downCoeff = onePoleCoeff (values[speed] * 0.4f, sampleRate);   // desce mais rápido: pico não passa
    outGain.setTarget (dbToGain (values[output]));
}

void LevelerProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const float tgt = values[target], maxRange = values[range], sens = values[sensitivity];
    float levelDb = -120.0f;
    bool active = false;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float l = block.left[i], r = block.right[i];
        const float hl = hpL.process (l, hpCoeffs), hr = hpR.process (r, hpCoeffs);
        const float ms = 0.5f * (hl * hl + hr * hr);
        meanSquare = ms + (meanSquare - ms) * rmsCoeff;
        levelDb = 10.0f * std::log10 (std::max (1.0e-12f, meanSquare));
        recentMaxDb = std::max (levelDb, recentMaxDb - maxDecayPerSample);
        active = levelDb > sens;

        if (active)
        {
            const float wanted = std::clamp (tgt - levelDb, -maxRange, maxRange);

            // Descer, sempre; subir, só com o nível firme (até 6 dB abaixo do pico recente): no fim de uma
            // frase o volume cai depressa, e o ganho não deve correr atrás da cauda e do ruído que vem depois.
            if (wanted < gainDb)
                gainDb = wanted + (gainDb - wanted) * downCoeff;
            else if (levelDb > recentMaxDb - 6.0f)
                gainDb = wanted + (gainDb - wanted) * upCoeff;
        }

        // A faixa pode ter diminuído: o ganho parado também respeita.
        gainDb = std::clamp (gainDb, -maxRange, maxRange);
        const float g = dbToGain (gainDb) * outGain.next();
        block.left[i] = l * g;
        block.right[i] = r * g;
    }

    setMeter (gainMeter, gainDb);
    setMeter (levelMeter, levelDb);
    setMeter (activeMeter, active ? 1.0f : 0.0f);
}

//==============================================================================
// Dobrador

namespace
{
    const char* const doublerModes[] = { "Dobra", "Coro" };
    const char* const voiceCounts[] = { "2", "4" };

    const ParamInfo doublerParams[] = {
        { "mode",   "Modo",          0, 1, 0,   "",      ParamKind::choice,     false, doublerModes,
          "Dobra: como um segundo take, irregular. Coro: ondulação regular, mais brilhante." },
        { "voices", "Cópias",        0, 1, 0,   "",      ParamKind::choice,     false, voiceCounts,
          "2 cópias (uma de cada lado) ou 4 (mais cheio, para refrão e ad-lib)." },
        { "delay",  "Atraso",        5, 40, 18, "ms",    ParamKind::continuous, false, {},
          "Quanto as cópias vêm depois da voz. 10 a 25 ms soa como dobra; acima de 30 vira eco curto." },
        { "detune", "Desafinação",   0, 30, 8,  "cents", ParamKind::continuous, false, {},
          "Quanto o tom das cópias varia. 5 a 12 cents engrossa; acima de 20 fica com cara de efeito." },
        { "width",  "Largura",       0, 100, 100, "%",   ParamKind::continuous, false, {},
          "Quanto as cópias abrem para os lados. 0% = todas no centro." },
        { "hpf",    "Corte grave das cópias", 20, 500, 150, "Hz", ParamKind::continuous, true, {},
          "As cópias sem grave: o peso da voz fica no centro e a mix não embola." },
        { "level",  "Nível das cópias", 0, 100, 40, "%", ParamKind::continuous, false, {},
          "O volume das cópias. A voz original continua igual." },
    };

    static_assert (std::size (doublerParams) == DoublerProcessor::count);

    constexpr float delayFactor[DoublerProcessor::maxVoices] = { 0.75f, 1.0f, 1.35f, 1.65f };
    constexpr float panOf[DoublerProcessor::maxVoices] = { -1.0f, 1.0f, -0.55f, 0.55f };
    constexpr double rateA[DoublerProcessor::maxVoices] = { 0.31, 0.37, 0.43, 0.29 };
    constexpr double rateB[DoublerProcessor::maxVoices] = { 0.77, 0.69, 0.83, 0.91 };
    constexpr double chorusRate = 0.8;
}

const FxInfo& DoublerProcessor::staticInfo()
{
    static const FxInfo info { "doubler", "Dobrador", "Dobra", "Voz",
                               "Cópias da voz com um pouco de atraso e de desafinação, abertas nos lados: voz mais grossa e larga, "
                               "como se tivesse gravado duas vezes.",
                               doublerParams };
    return info;
}

double DoublerProcessor::modulationSeconds (double cents, bool chorus) noexcept
{
    // Atraso d(t) muda o tom por (1 - d'(t)): o desvio máximo é a maior inclinação da modulação.
    const double slope = std::exp2 (std::max (0.0, cents) / 1200.0) - 1.0;

    if (chorus)
        return slope / (2.0 * pi * chorusRate);

    // Dobra: a/2 (sen(wA t) + sen(wB t)), inclinação máxima a/2 (wA + wB).
    return 2.0 * slope / (2.0 * pi * (rateA[0] + rateB[0]));
}

void DoublerProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = doublerParams[i].def;

    line.prepare ((int) std::ceil (0.085 * rate) + 8);

    for (auto* s : { &baseMs, &modSeconds, &widthAmount, &copyLevel, &extraVoices })
        s->prepare (rate, 40.0f);

    dirty = true;
    snapToTargets();
    reset();
}

void DoublerProcessor::snapToTargets() noexcept
{
    update();

    for (auto* s : { &baseMs, &modSeconds, &widthAmount, &copyLevel, &extraVoices })
        s->snap (s->getTarget());
}

void DoublerProcessor::reset() noexcept
{
    line.reset();
    hpState.reset();

    for (int v = 0; v < maxVoices; ++v)
    {
        // Fases fixas e diferentes por cópia: o render é sempre igual (determinístico).
        phaseA[(size_t) v] = 0.9 * v + 0.3;
        phaseB[(size_t) v] = 2.1 * v + 1.1;
        currentDelay[(size_t) v] = (float) (baseMs.getCurrent() * delayFactor[v] * 0.001 * sampleRate);
        delayStep[(size_t) v] = 0.0f;
    }

    controlCounter = 0;
    activeVoices = 2;
    gainL.fill (0.0f);
    gainR.fill (0.0f);
    corrLR = corrLL = corrRR = 0.0;
    setMeter (correlation, 1.0f);
}

void DoublerProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void DoublerProcessor::update() noexcept
{
    dirty = false;
    const bool chorus = values[mode] >= 0.5f;
    baseMs.setTarget (values[delay]);
    modSeconds.setTarget ((float) modulationSeconds (values[detune], chorus));
    widthAmount.setTarget (values[width] * 0.01f);
    copyLevel.setTarget (values[level] * 0.01f);
    extraVoices.setTarget (values[voices] >= 0.5f ? 1.0f : 0.0f);
    hpCoeffs = SvfCoeffs::make (FilterType::highpass, values[hpf], 0.7071, 0.0, sampleRate);
}

void DoublerProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const bool chorus = values[mode] >= 0.5f;
    const bool useHp = values[hpf] > 21.0f;

    auto control = [&]
    {
        // A cada 16 amostras: o atraso de cada cópia daqui a 16 amostras; no meio, reta.
        const double step = controlRate / sampleRate;
        const double base = baseMs.skip (controlRate) * 0.001 * sampleRate;
        const double mod = modSeconds.skip (controlRate) * sampleRate;
        const float w = widthAmount.skip (controlRate);
        const float extra = extraVoices.skip (controlRate);
        activeVoices = extra > 0.0f ? maxVoices : 2;

        for (int v = 0; v < activeVoices; ++v)
        {
            const auto uv = (size_t) v;
            const double baseV = base * delayFactor[v];
            const double amp = std::min (mod, std::max (0.0, baseV - 0.001 * sampleRate));
            double next;

            if (chorus)
            {
                phaseA[uv] += 2.0 * pi * chorusRate * step;
                next = baseV + amp * std::sin (phaseA[uv] + 0.5 * pi * v);
            }
            else
            {
                phaseA[uv] += 2.0 * pi * rateA[v] * step;
                phaseB[uv] += 2.0 * pi * rateB[v] * step;
                next = baseV + 0.5 * amp * (std::sin (phaseA[uv]) + std::sin (phaseB[uv]));
            }

            if (phaseA[uv] > 2.0 * pi) phaseA[uv] -= 2.0 * pi;
            if (phaseB[uv] > 2.0 * pi) phaseB[uv] -= 2.0 * pi;

            next = std::clamp (next, 2.0, (double) line.capacity() - 4.0);
            delayStep[uv] = (float) ((next - currentDelay[uv]) / controlRate);

            // Lei de potência constante; as cópias 3 e 4 entram e saem suaves.
            const float angle = (float) (0.25 * pi * (1.0 + panOf[v] * w));
            const float voiceGain = v < 2 ? 0.8f : 0.6f * extra;
            gainL[uv] = std::cos (angle) * voiceGain;
            gainR[uv] = std::sin (angle) * voiceGain;
        }
    };

    for (int i = 0; i < block.numSamples; ++i)
    {
        if (controlCounter == 0)
            control();

        controlCounter = (controlCounter + 1) % controlRate;

        const float l = block.left[i], r = block.right[i];
        const float m = 0.5f * (l + r);
        line.push (useHp ? hpState.process (m, hpCoeffs) : m);

        float cl = 0.0f, cr = 0.0f;

        for (int v = 0; v < activeVoices; ++v)
        {
            const auto uv = (size_t) v;
            currentDelay[uv] += delayStep[uv];
            const float c = line.readHermite (currentDelay[uv]);
            cl += c * gainL[uv];
            cr += c * gainR[uv];
        }

        const float lvl = copyLevel.next();
        const float ol = l + lvl * cl, orr = r + lvl * cr;
        block.left[i] = ol;
        block.right[i] = orr;

        corrLR += 0.0005 * ((double) ol * orr - corrLR);
        corrLL += 0.0005 * ((double) ol * ol - corrLL);
        corrRR += 0.0005 * ((double) orr * orr - corrRR);
    }

    const double den = std::sqrt (corrLL * corrRR);
    setMeter (correlation, den > 1.0e-9 ? (float) std::clamp (corrLR / den, -1.0, 1.0) : 1.0f);
}

} // namespace whynot::dsp
