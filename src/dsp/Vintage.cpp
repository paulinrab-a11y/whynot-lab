#include "Vintage.h"
#include "Dynamics.h"

namespace whynot::dsp
{

//==============================================================================
// Compressor óptico

namespace
{
    const char* const optoModes[] = { "Suave", "Limitar" };

    const ParamInfo optoParams[] = {
        { "reduction", "Redução",  0, 100, 40,  "%",  ParamKind::continuous, false, {},
          "Quanto comprime. Mais redução = o limiar desce (de 0 a -40 dB)." },
        { "gain",      "Ganho",    -12, 24, 0,  "dB", ParamKind::continuous, false, {},
          "Devolve o volume perdido na compressão." },
        { "mode",      "Modo",     0, 1, 0,     "",   ParamKind::choice,     false, optoModes,
          "Suave comprime 3:1 com joelho largo; Limitar segura mais (10:1)." },
        { "emphasis",  "Agudo no detector", 0, 100, 0, "%", ParamKind::continuous, false, {},
          "Faz o compressor reagir mais aos agudos: segura os \"s\" e as notas estridentes." },
        { "mix",       "Mistura",  0, 100, 100, "%" },
    };

    static_assert (std::size (optoParams) == OptoProcessor::count);
}

const FxInfo& OptoProcessor::staticInfo()
{
    static const FxInfo info { "opto", "Comp óptico", "Óptico", "Dinâmica",
                               "Compressor suave e musical: segura a voz sem esmagar e solta devagar, conforme o canto. "
                               "Um botão de redução e um de ganho.",
                               optoParams, true };
    return info;
}

void OptoProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = optoParams[i].def;

    rmsCoeff = onePoleCoeff (5.0f, rate);
    fastAttack = onePoleCoeff (10.0f, rate);
    fastRelease = onePoleCoeff (60.0f, rate);
    slowAttack = onePoleCoeff (250.0f, rate);
    memCharge = onePoleCoeff (4000.0f, rate);
    memDischarge = onePoleCoeff (8000.0f, rate);
    makeupGain.prepare (rate, 20.0f);
    wet.prepare (rate, 20.0f);
    dirty = true;
    snapToTargets();
    reset();
}

void OptoProcessor::snapToTargets() noexcept
{
    update();
    makeupGain.snap (makeupGain.getTarget());
    wet.snap (wet.getTarget());
}

void OptoProcessor::reset() noexcept
{
    emphL.reset();
    emphR.reset();
    meanSquare = 0.0f;
    fastGr = slowGr = memory = 0.0f;
    setMeter (gainReduction, 0.0f);
    setMeter (memoryMeter, 0.0f);
}

void OptoProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void OptoProcessor::update() noexcept
{
    dirty = false;
    thresholdDb = -0.4f * values[reduction];
    const bool limit = values[mode] >= 0.5f;
    ratio = limit ? 10.0f : 3.0f;
    kneeDb = limit ? 6.0f : 10.0f;

    const bool wasUsing = useEmphasis;
    useEmphasis = values[emphasis] > 0.5f;

    if (useEmphasis && ! wasUsing)
    {
        emphL.reset();
        emphR.reset();
    }

    emphasisCoeffs = SvfCoeffs::make (FilterType::highShelf, 3000.0, 0.7071, 0.12 * values[emphasis], sampleRate);
    makeupGain.setTarget (dbToGain (values[gain]));
    wet.setTarget (values[mix] * 0.01f);
}

void OptoProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const bool external = block.sideLeft != nullptr && block.sideRight != nullptr;
    float maxReduction = 0.0f, inPeak = -120.0f, outPeak = 0.0f;

    for (int start = 0; start < block.numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, block.numSamples - start);

        // A parte lenta solta de 0,4 s (pouca memória) a 4 s (comprimiu muito tempo seguido).
        const float slowRelease = onePoleCoeff (400.0f + 3600.0f * memory, sampleRate);

        for (int j = start; j < start + n; ++j)
        {
            const float l = block.left[j], r = block.right[j];
            float dl = external ? block.sideLeft[j] : l;
            float dr = external ? block.sideRight[j] : r;

            if (useEmphasis)
            {
                dl = emphL.process (dl, emphasisCoeffs);
                dr = emphR.process (dr, emphasisCoeffs);
            }

            const float ms = 0.5f * (dl * dl + dr * dr);
            meanSquare = ms + (meanSquare - ms) * rmsCoeff;

            // RMS + 3 dB: um seno lê o próprio pico (o limiar fica na mesma escala do medidor).
            const float levelDb = 10.0f * std::log10 (std::max (1.0e-12f, 2.0f * meanSquare));
            inPeak = std::max (inPeak, levelDb);
            const float target = compressorGainDb (levelDb, thresholdDb, ratio, kneeDb);

            fastGr = target + (fastGr - target) * (target < fastGr ? fastAttack : fastRelease);
            slowGr = target + (slowGr - target) * (target < slowGr ? slowAttack : slowRelease);

            const float compressing = target < -1.0f ? 1.0f : 0.0f;
            memory = compressing + (memory - compressing) * (compressing > memory ? memCharge : memDischarge);

            const float gr = 0.6f * fastGr + 0.4f * slowGr;
            maxReduction = std::min (maxReduction, gr);

            const float g = (gr > -1.0e-4f ? 1.0f : dbToGain (gr)) * makeupGain.next();
            const float w = wet.next();
            const float ol = l + w * (l * g - l);
            const float orr = r + w * (r * g - r);
            block.left[j] = ol;
            block.right[j] = orr;
            outPeak = std::max (outPeak, std::max (std::abs (ol), std::abs (orr)));
        }
    }

    setMeter (gainReduction, -maxReduction);
    setMeter (inputLevel, std::max (-120.0f, inPeak));
    setMeter (outputLevel, gainToDb (outPeak));
    setMeter (memoryMeter, memory);
}

//==============================================================================
// Compressor FET

namespace
{
    const char* const fetRatios[] = { "4:1", "8:1", "12:1", "20:1", "Todos" };

    const ParamInfo fetParams[] = {
        { "threshold", "Limiar",  -50, 0, -20,     "dB", ParamKind::continuous, false, {},
          "Acima deste nível o compressor age." },
        { "ratio",     "Razão",   0, 4, 0,         "",   ParamKind::choice,     false, fetRatios,
          "4:1 controla; 20:1 limita. \"Todos\" aperta todas ao mesmo tempo: o som esmagado e agressivo." },
        { "attack",    "Ataque",  0.02f, 0.8f, 0.25f, "ms", ParamKind::continuous, true, {},
          "Muito rápido: pega até o estalo da consoante. Mais lento deixa passar o ataque." },
        { "release",   "Soltura", 50, 1100, 250,   "ms", ParamKind::continuous, true,  {},
          "Soltura curta = mais pressão e mais \"bombeado\"." },
        { "makeup",    "Ganho",   -12, 24, 0,      "dB" },
        { "color",     "Cor",     0, 100, 30,      "%",  ParamKind::continuous, false, {},
          "Harmônicos de transistor: mais presença e agressividade. 0% = limpo." },
        { "mix",       "Mistura", 0, 100, 100,     "%",  ParamKind::continuous, false, {},
          "Abaixo de 100% vira compressão paralela (o truque da voz de rap na cara)." },
    };

    static_assert (std::size (fetParams) == FetProcessor::count);
}

const FxInfo& FetProcessor::staticInfo()
{
    static const FxInfo info { "fet", "Comp FET", "FET", "Dinâmica",
                               "Compressor muito rápido e agressivo, com cor: voz de rap na cara, bateria com pressão.",
                               fetParams, true };
    return info;
}

float FetProcessor::colorCurve (float x, float amount) noexcept
{
    // x - k x³, monotônica até |x| = 1/sqrt(3k); depois disso fica no topo (sem dobrar a onda).
    const float k = 0.15f * std::clamp (amount, 0.0f, 1.0f);

    if (k <= 0.0f)
        return x;

    const float limit = 1.0f / std::sqrt (3.0f * k);
    const float c = std::clamp (x, -limit, limit);
    return c - k * c * c * c;
}

void FetProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = fetParams[i].def;

    for (auto& o : oversamplers)
        o.prepare();

    latency = oversamplers[0].latency();

    for (auto& d : dryDelay)
        d.prepare (latency + 4);

    makeupGain.prepare (rate, 20.0f);
    wet.prepare (rate, 20.0f);
    colorAmount.prepare (rate, 20.0f);
    dirty = true;
    snapToTargets();
    reset();
}

void FetProcessor::snapToTargets() noexcept
{
    update();
    makeupGain.snap (makeupGain.getTarget());
    wet.snap (wet.getTarget());
    colorAmount.snap (colorAmount.getTarget());
}

void FetProcessor::reset() noexcept
{
    for (auto& o : oversamplers)
        o.reset();

    for (auto& d : dryDelay)
        d.reset();

    smoothedGr = 0.0f;
    setMeter (gainReduction, 0.0f);
}

void FetProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void FetProcessor::update() noexcept
{
    dirty = false;
    static constexpr float ratios[] = { 4.0f, 8.0f, 12.0f, 20.0f, 20.0f };
    static constexpr float knees[] = { 6.0f, 4.0f, 3.0f, 2.0f, 0.0f };
    const int r = std::clamp ((int) values[ratio], 0, 4);
    const bool all = r == 4;

    ratioValue = ratios[r];
    kneeDb = knees[r];
    overshoot = all ? 1.3f : 1.0f;
    attackCoeff = onePoleCoeff (values[attack], sampleRate);
    releaseCoeff = onePoleCoeff (values[release], sampleRate);
    makeupGain.setTarget (dbToGain (values[makeup]));
    wet.setTarget (values[mix] * 0.01f);
    colorAmount.setTarget (std::min (1.0f, values[color] * 0.01f + (all ? 0.3f : 0.0f)));
}

void FetProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const bool external = block.sideLeft != nullptr && block.sideRight != nullptr;
    const float thr = values[threshold];
    float maxReduction = 0.0f, inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float l = block.left[i], r = block.right[i];
        const float dl = external ? block.sideLeft[i] : l;
        const float dr = external ? block.sideRight[i] : r;
        const float level = std::max (std::abs (dl), std::abs (dr));
        inPeak = std::max (inPeak, level);

        const float target = overshoot * compressorGainDb (gainToDb (level), thr, ratioValue, kneeDb);
        smoothedGr = target + (smoothedGr - target) * (target < smoothedGr ? attackCoeff : releaseCoeff);
        maxReduction = std::min (maxReduction, smoothedGr);

        const float g = (smoothedGr > -1.0e-4f ? 1.0f : dbToGain (smoothedGr)) * makeupGain.next();
        // A mesma curva do colorCurve, com k e o limite calculados uma vez por amostra.
        const float k = 0.15f * colorAmount.next();
        const float limit = k > 0.0f ? 1.0f / std::sqrt (3.0f * k) : 1.0e9f;
        auto shape = [k, limit] (float v)
        {
            const float c = std::clamp (v, -limit, limit);
            return c - k * c * c * c;
        };

        // A cor depois do ganho: quanto mais alto o sinal comprimido, mais harmônicos.
        const float wl = oversamplers[0].process (l * g, shape);
        const float wr = oversamplers[1].process (r * g, shape);

        dryDelay[0].push (l);
        dryDelay[1].push (r);
        const float sl = dryDelay[0].read (latency), sr = dryDelay[1].read (latency);
        const float w = wet.next();
        const float ol = sl + w * (wl - sl);
        const float orr = sr + w * (wr - sr);
        block.left[i] = ol;
        block.right[i] = orr;
        outPeak = std::max (outPeak, std::max (std::abs (ol), std::abs (orr)));
    }

    setMeter (gainReduction, -maxReduction);
    setMeter (inputLevel, gainToDb (inPeak));
    setMeter (outputLevel, gainToDb (outPeak));
}

//==============================================================================
// EQ vintage

namespace
{
    const char* const lowFreqs[] = { "20 Hz", "30 Hz", "60 Hz", "100 Hz" };
    const char* const highFreqs[] = { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" };
    const char* const airFreqs[] = { "5 kHz", "10 kHz", "20 kHz" };

    constexpr float lowHz[] = { 20.0f, 30.0f, 60.0f, 100.0f };
    constexpr float highHz[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
    constexpr float airHz[] = { 5000.0f, 10000.0f, 20000.0f };

    const ParamInfo vintageEqParams[] = {
        { "lowFreq",   "Frequência do grave", 0, 3, 2, "", ParamKind::choice, false, lowFreqs,
          "Onde o reforço e a atenuação do grave agem." },
        { "lowBoost",  "Reforço grave",   0, 10, 0, "", ParamKind::continuous, false, {},
          "Sobe o grave em prateleira. Junto com a atenuação, dá peso com um vale logo acima (menos embolado)." },
        { "lowAtten",  "Atenuação grave", 0, 10, 0, "", ParamKind::continuous, false, {},
          "Corta o grave numa curva mais aberta que o reforço." },
        { "highFreq",  "Frequência do agudo", 0, 6, 4, "", ParamKind::choice, false, highFreqs,
          "Onde o reforço do agudo age: 3 a 5 kHz é presença; 10 a 16 kHz é ar." },
        { "highBoost", "Reforço agudo",   0, 10, 0, "", ParamKind::continuous, false, {},
          "Sobe o agudo num sino." },
        { "highWidth", "Largura",         0, 10, 5, "", ParamKind::continuous, false, {},
          "0 = sino estreito; 10 = bem largo e suave." },
        { "airFreq",   "Corte agudo em",  0, 2, 1, "", ParamKind::choice, false, airFreqs,
          "Onde a atenuação do agudo começa." },
        { "highAtten", "Atenuação agudo", 0, 10, 0, "", ParamKind::continuous, false, {},
          "Tira o chiado e o brilho demais em prateleira." },
        { "output",    "Saída",           -12, 12, 0, "dB" },
    };

    static_assert (std::size (vintageEqParams) == VintageEqProcessor::count);

    struct BandSpec
    {
        FilterType type;
        double freq, q, gainDb;
    };

    /** As quatro curvas para um conjunto de valores (a mesma conta no áudio e no desenho). */
    std::array<BandSpec, 4> vintageBands (const float* v) noexcept
    {
        const float low = lowHz[std::clamp ((int) v[VintageEqProcessor::lowFreq], 0, 3)];
        const float high = highHz[std::clamp ((int) v[VintageEqProcessor::highFreq], 0, 6)];
        const float air = airHz[std::clamp ((int) v[VintageEqProcessor::airFreq], 0, 2)];

        return {{
            { FilterType::lowShelf,  low,          0.8,  1.35 * v[VintageEqProcessor::lowBoost] },
            { FilterType::lowShelf,  low * 0.6,    0.35, -1.75 * v[VintageEqProcessor::lowAtten] },
            { FilterType::bell,      high,         2.5 - 0.2 * v[VintageEqProcessor::highWidth], 1.6 * v[VintageEqProcessor::highBoost] },
            { FilterType::highShelf, air,          0.6,  -1.6 * v[VintageEqProcessor::highAtten] },
        }};
    }
}

const FxInfo& VintageEqProcessor::staticInfo()
{
    static const FxInfo info { "vintageEq", "EQ vintage", "EQ vint.", "Equalização",
                               "EQ de poucos botões e curvas largas: peso no grave sem embolar (reforço e atenuação juntos) e ar no agudo.",
                               vintageEqParams };
    return info;
}

void VintageEqProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = vintageEqParams[i].def;

    for (auto& b : bands)
    {
        b.logFreq.prepare (rate, 30.0f);
        b.gainDb.prepare (rate, 30.0f);
        b.q.prepare (rate, 30.0f);
    }

    outGain.prepare (rate, 20.0f);
    dirty = true;
    snapToTargets();
    reset();
}

void VintageEqProcessor::snapToTargets() noexcept
{
    updateTargets();

    for (auto& b : bands)
    {
        b.logFreq.snap (b.logFreq.getTarget());
        b.gainDb.snap (b.gainDb.getTarget());
        b.q.snap (b.q.getTarget());
    }

    outGain.snap (outGain.getTarget());
    updateCoefficients();
}

void VintageEqProcessor::reset() noexcept
{
    for (auto& b : bands)
    {
        b.left.reset();
        b.right.reset();
    }

    updateCoefficients();
}

void VintageEqProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void VintageEqProcessor::updateTargets() noexcept
{
    dirty = false;
    const auto specs = vintageBands (values.data());

    for (size_t b = 0; b < bands.size(); ++b)
    {
        // O corte de ar não passa de 45% da taxa (a 44,1 kHz, 20 kHz ficaria em cima de Nyquist).
        bands[b].logFreq.setTarget ((float) std::log2 (std::min (specs[b].freq, sampleRate * 0.45)));
        bands[b].gainDb.setTarget ((float) specs[b].gainDb);
        bands[b].q.setTarget ((float) specs[b].q);
    }

    outGain.setTarget (dbToGain (values[output]));
}

void VintageEqProcessor::updateCoefficients() noexcept
{
    static constexpr FilterType types[] = { FilterType::lowShelf, FilterType::lowShelf, FilterType::bell, FilterType::highShelf };

    for (size_t b = 0; b < bands.size(); ++b)
    {
        auto& band = bands[b];
        const bool wasActive = band.active;
        band.active = band.gainDb.getCurrent() != 0.0f || band.gainDb.getTarget() != 0.0f;

        // Banda que volta a agir parte do zero (o estado parado ficou velho); a rampa do ganho esconde a partida.
        if (band.active && ! wasActive)
        {
            band.left.reset();
            band.right.reset();
        }

        band.coeffs = SvfCoeffs::make (types[b], std::exp2 ((double) band.logFreq.getCurrent()), band.q.getCurrent(),
                                       band.gainDb.getCurrent(), sampleRate);
    }
}

void VintageEqProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        updateTargets();

    for (int start = 0; start < block.numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, block.numSamples - start);
        bool smoothing = false;

        for (auto& b : bands)
            smoothing |= b.logFreq.isSmoothing() || b.gainDb.isSmoothing() || b.q.isSmoothing();

        if (smoothing || start == 0)
        {
            for (auto& b : bands)
            {
                b.logFreq.skip (n);
                b.gainDb.skip (n);
                b.q.skip (n);
            }

            updateCoefficients();
        }

        float* l = block.left + start;
        float* r = block.right + start;

        for (int i = 0; i < n; ++i)
        {
            float xl = l[i], xr = r[i];

            for (auto& b : bands)
                if (b.active)
                {
                    xl = b.left.process (xl, b.coeffs);
                    xr = b.right.process (xr, b.coeffs);
                }

            const float g = outGain.next();
            l[i] = xl * g;
            r[i] = xr * g;
        }
    }
}

double VintageEqProcessor::responseDb (const float* v, double freq, double sampleRate)
{
    double db = 0.0;

    for (auto& s : vintageBands (v))
        if (s.gainDb != 0.0)
            db += SvfCoeffs::make (s.type, std::min (s.freq, sampleRate * 0.45), s.q, s.gainDb, sampleRate).magnitudeDb (freq, sampleRate);

    return db + v[output];
}

} // namespace whynot::dsp
