#include "Console.h"
#include "Color.h"
#include "Dynamics.h"

namespace whynot::dsp
{

namespace
{
    const ParamInfo consoleParams[] = {
        { "hpf",     "Corte grave", 20, 400, 20,   "Hz", ParamKind::continuous, true,  {},
          "Tira o grave abaixo daqui (24 dB/oit). Voz: 80 a 120 Hz. No fim da escala (20 Hz), desligado." },
        { "gate",    "Gate",        -80, 0, -80,   "dB", ParamKind::continuous, false, {},
          "Abaixo deste nível a trilha baixa 25 dB (ruído entre as frases). -80 dB = desligado." },
        { "comp",    "Compressor",  -40, 0, 0,     "dB", ParamKind::continuous, false, {},
          "Limiar do compressor. 0 dB = desligado." },
        { "ratio",   "Razão",       1, 10, 3,      ":1", ParamKind::continuous, false, {},
          "3:1 controla a voz; 6:1 ou mais deixa na cara." },
        { "fast",    "Ataque rápido", 0, 1, 0,     "",   ParamKind::toggle,     false, {},
          "Ligado: 3 ms e soltura de 80 ms (segura até as consoantes). Desligado: 30 ms e 250 ms (deixa o ataque passar)." },
        { "low",     "Grave",       -15, 15, 0,    "dB", ParamKind::continuous, false, {},
          "Prateleira em 100 Hz." },
        { "mid",     "Médio",       -15, 15, 0,    "dB", ParamKind::continuous, false, {},
          "Sino na frequência do médio." },
        { "midFreq", "Freq. do médio", 200, 8000, 2500, "Hz", ParamKind::continuous, true, {},
          "Voz: 250 Hz embola (cortar), 2 a 4 kHz é presença (subir)." },
        { "high",    "Agudo",       -15, 15, 0,    "dB", ParamKind::continuous, false, {},
          "Prateleira em 10 kHz: o ar da voz." },
        { "drive",   "Cor",         0, 100, 0,     "%",  ParamKind::continuous, false, {},
          "Saturação de válvula no fim: mais calor e presença. 0% = limpo." },
        { "output",  "Saída",       -18, 18, 0,    "dB" },
    };

    static_assert (std::size (consoleParams) == ConsoleProcessor::count);

    constexpr double lowHz = 100.0, highHz = 10000.0;
}

const FxInfo& ConsoleProcessor::staticInfo()
{
    static const FxInfo info { "console", "Console", "Console", "Voz",
                               "A trilha de uma mesa de som num efeito só: corte grave, gate, compressor, EQ e cor. "
                               "O jeito rápido de deixar a voz pronta.",
                               consoleParams };
    return info;
}

void ConsoleProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = consoleParams[i].def;

    gateCore.prepare (rate);
    rmsCoeff = onePoleCoeff (5.0f, rate);

    for (auto& o : oversamplers)
        o.prepare();

    for (auto& dc : colorDc)
        dc.prepare (rate * Oversampler4x::factor);

    latency = oversamplers[0].latency();

    eq[0].type = FilterType::lowShelf;
    eq[1].type = FilterType::bell;
    eq[2].type = FilterType::highShelf;

    for (auto& b : eq)
    {
        b.gainDb.prepare (rate, 30.0f);
        b.logFreq.prepare (rate, 30.0f);
    }

    driveAmount.prepare (rate, 20.0f);
    outGain.prepare (rate, 20.0f);
    dirty = true;
    snapToTargets();
    reset();
}

void ConsoleProcessor::snapToTargets() noexcept
{
    update();

    for (auto& b : eq)
    {
        b.gainDb.snap (b.gainDb.getTarget());
        b.logFreq.snap (b.logFreq.getTarget());
    }

    driveAmount.snap (driveAmount.getTarget());
    outGain.snap (outGain.getTarget());
    updateEq();
}

void ConsoleProcessor::reset() noexcept
{
    for (auto& s : hpL) s.reset();
    for (auto& s : hpR) s.reset();
    for (auto& o : oversamplers) o.reset();
    for (auto& dc : colorDc) dc.reset();

    for (auto& b : eq)
    {
        b.left.reset();
        b.right.reset();
    }

    gateCore.reset();
    compGr = meanSquare = 0.0f;
    setMeter (compReduction, 0.0f);
    setMeter (gateReduction, 0.0f);
    setMeter (gateOpen, 1.0f);
}

void ConsoleProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void ConsoleProcessor::update() noexcept
{
    dirty = false;

    const bool wasUsingHp = useHp;
    useHp = values[hpf] > 21.0f;

    if (useHp && ! wasUsingHp)
    {
        for (auto& s : hpL) s.reset();
        for (auto& s : hpR) s.reset();
    }

    for (int s = 0; s < 2; ++s)
        hpCoeffs[(size_t) s] = SvfCoeffs::make (FilterType::highpass, values[hpf], butterworthQ (2, s), 0.0, sampleRate);

    const bool wasGateOn = gateOn;
    gateOn = values[gate] > -79.5f;

    if (gateOn && ! wasGateOn)
        gateCore.reset();

    gateCore.set (false, values[gate], 25.0f, 1.0f, 0.5f, 40.0f, 120.0f, 4.0f);

    const bool quick = values[fast] >= 0.5f;
    compAttack = onePoleCoeff (quick ? 3.0f : 30.0f, sampleRate);
    compRelease = onePoleCoeff (quick ? 80.0f : 250.0f, sampleRate);

    eq[0].gainDb.setTarget (values[low]);
    eq[0].logFreq.setTarget ((float) std::log2 (lowHz));
    eq[1].gainDb.setTarget (values[mid]);
    eq[1].logFreq.setTarget (std::log2 (values[midFreq]));
    eq[2].gainDb.setTarget (values[high]);
    eq[2].logFreq.setTarget ((float) std::log2 (std::min (highHz, sampleRate * 0.45)));

    driveAmount.setTarget (values[drive] * 0.01f);
    outGain.setTarget (dbToGain (values[output]));
}

void ConsoleProcessor::updateEq() noexcept
{
    for (auto& b : eq)
    {
        const bool wasActive = b.active;
        b.active = b.gainDb.getCurrent() != 0.0f || b.gainDb.getTarget() != 0.0f;

        if (b.active && ! wasActive)
        {
            b.left.reset();
            b.right.reset();
        }

        b.coeffs = SvfCoeffs::make (b.type, std::exp2 ((double) b.logFreq.getCurrent()), b.type == FilterType::bell ? 1.0 : 0.7071,
                                    b.gainDb.getCurrent(), sampleRate);
    }
}

void ConsoleProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const float thr = values[comp], rat = values[ratio];
    const bool compOn = thr < -0.01f;
    float maxReduction = 0.0f, minGate = 1.0f;

    for (int start = 0; start < block.numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, block.numSamples - start);
        bool smoothing = start == 0;

        for (auto& b : eq)
            smoothing |= b.gainDb.isSmoothing() || b.logFreq.isSmoothing();

        if (smoothing)
        {
            for (auto& b : eq)
            {
                b.gainDb.skip (n);
                b.logFreq.skip (n);
            }

            updateEq();
        }

        for (int j = start; j < start + n; ++j)
        {
            float l = block.left[j], r = block.right[j];

            if (useHp)
                for (int s = 0; s < 2; ++s)
                {
                    l = hpL[(size_t) s].process (l, hpCoeffs[(size_t) s]);
                    r = hpR[(size_t) s].process (r, hpCoeffs[(size_t) s]);
                }

            const float level = std::max (std::abs (l), std::abs (r));

            if (gateOn)
            {
                const float g = gateCore.next (level);
                minGate = std::min (minGate, g);
                l *= g;
                r *= g;
            }

            if (compOn)
            {
                const float ms = 0.5f * (l * l + r * r);
                meanSquare = ms + (meanSquare - ms) * rmsCoeff;
                const float levelDb = 10.0f * std::log10 (std::max (1.0e-12f, 2.0f * meanSquare));
                const float target = compressorGainDb (levelDb, thr, rat, 6.0f);
                compGr = target + (compGr - target) * (target < compGr ? compAttack : compRelease);
                maxReduction = std::min (maxReduction, compGr);

                if (compGr < -1.0e-4f)
                {
                    const float g = dbToGain (compGr);
                    l *= g;
                    r *= g;
                }
            }
            else
            {
                compGr = meanSquare = 0.0f;
            }

            for (auto& b : eq)
                if (b.active)
                {
                    l = b.left.process (l, b.coeffs);
                    r = b.right.process (r, b.coeffs);
                }

            // Cor: válvula com o ganho de entrada desfeito na saída (o volume baixo não muda).
            // O DC da assimetria sai só da diferença: com a cor em 0 o sinal limpo passa exato.
            const float d = driveAmount.next();
            const float pre = dbToGain (12.0f * d);
            auto shapeFor = [d, pre] (DcBlocker& dc)
            {
                return [d, pre, &dc] (float v)
                {
                    return d <= 0.0f ? v : v + dc.process (d * (saturate (SaturationMode::tube, v * pre) / pre - v));
                };
            };

            l = oversamplers[0].process (l, shapeFor (colorDc[0]));
            r = oversamplers[1].process (r, shapeFor (colorDc[1]));

            const float o = outGain.next();
            block.left[j] = l * o;
            block.right[j] = r * o;
        }
    }

    setMeter (compReduction, -maxReduction);
    setMeter (gateReduction, -gainToDb (minGate, -80.0f));
    setMeter (gateOpen, ! gateOn || gateCore.isOpen() ? 1.0f : 0.0f);
}

double ConsoleProcessor::responseDb (const float* v, double freq, double sampleRate)
{
    double db = v[output];

    if (v[hpf] > 21.0f)
        for (int s = 0; s < 2; ++s)
            db += SvfCoeffs::make (FilterType::highpass, v[hpf], butterworthQ (2, s), 0.0, sampleRate).magnitudeDb (freq, sampleRate);

    if (v[low] != 0.0f)
        db += SvfCoeffs::make (FilterType::lowShelf, lowHz, 0.7071, v[low], sampleRate).magnitudeDb (freq, sampleRate);

    if (v[mid] != 0.0f)
        db += SvfCoeffs::make (FilterType::bell, v[midFreq], 1.0, v[mid], sampleRate).magnitudeDb (freq, sampleRate);

    if (v[high] != 0.0f)
        db += SvfCoeffs::make (FilterType::highShelf, std::min (highHz, sampleRate * 0.45), 0.7071, v[high], sampleRate).magnitudeDb (freq, sampleRate);

    return db;
}

} // namespace whynot::dsp
