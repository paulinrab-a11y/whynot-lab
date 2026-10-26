#include "Grit.h"

#include <iterator>

namespace whynot::dsp
{

//==============================================================================
// Clipper

namespace
{
    const ParamInfo clipperParams[] = {
        { "gain",     "Entrada", 0, 24, 0, "dB", ParamKind::continuous, false, {},
          "Empurra o som contra o teto. Cada dB a mais é um dB a mais de volume, cortado no pico." },
        { "ceiling",  "Teto", -24, 0, 0, "dB", ParamKind::continuous, false, {},
          "Onde o corte acontece. Nos agudos o pico ainda pode passar até 2 dB daqui; no master, ponha um limitador depois." },
        { "softness", "Suavidade", 0, 100, 30, "%", ParamKind::continuous, false, {},
          "0% corta reto (mais alto, mais agressivo); 100% arredonda o pico (mais quente, menos sujo)." },
        { "output",   "Saída", -24, 6, 0, "dB", ParamKind::continuous, false, {},
          "Volume depois do corte, para comparar no mesmo nível." },
    };

    static_assert (std::size (clipperParams) == ClipperProcessor::count);
}

const FxInfo& ClipperProcessor::staticInfo()
{
    static const FxInfo info { "clipper", "Clipper", "Clip", "Dinâmica",
                               "Corta os picos na hora, sem bombear: o kick e o 808 ficam mais altos sem perder o soco. "
                               "Use antes do limitador no master ou direto na bateria.",
                               clipperParams };
    return info;
}

float ClipperProcessor::clip (float x, float c, float s) noexcept
{
    const float a = std::abs (x);
    const float t = c * (1.0f - 0.4f * std::clamp (s, 0.0f, 1.0f));

    if (a <= t)
        return x;

    const float y = c - t > 1.0e-9f ? t + (c - t) * std::tanh ((a - t) / (c - t)) : c;
    return std::copysign (std::min (y, c), x);
}

void ClipperProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = clipperParams[i].def;

    for (auto& o : oversamplers)
        o.prepare();

    latency = oversamplers[0].latency();

    for (auto* s : { &inGain, &ceilingGain, &soft, &outGain })
        s->prepare (rate, 20.0f);

    meterRelease = onePoleCoeff (300.0f, rate / subBlock);
    snapToTargets();
    reset();
}

void ClipperProcessor::snapToTargets() noexcept
{
    for (int i = 0; i < count; ++i)
        setParam (i, values[(size_t) i]);

    for (auto* s : { &inGain, &ceilingGain, &soft, &outGain })
        s->snap (s->getTarget());
}

void ClipperProcessor::reset() noexcept
{
    for (auto& o : oversamplers)
        o.reset();

    meterHold = 0.0f;
    setMeter (clipping, 0.0f);
}

void ClipperProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;

    switch (index)
    {
        case gain:     inGain.setTarget (dbToGain (value)); break;
        case ceiling:  ceilingGain.setTarget (dbToGain (value)); break;
        case softness: soft.setTarget (value * 0.01f); break;
        case output:   outGain.setTarget (dbToGain (value)); break;
        default: break;
    }
}

void ClipperProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;
    float* const io[2] = { block.left, block.right };

    for (int start = 0; start < block.numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, block.numSamples - start);
        float over = 0.0f;

        for (int i = start; i < start + n; ++i)
        {
            const float g = inGain.next(), c = ceilingGain.next(), s = soft.next(), o = outGain.next();
            auto shape = [c, s] (float v) { return clip (v, c, s); };

            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = io[ch][i] * g;
                over = std::max (over, std::abs (x) / c);
                // A volta da sobreamostragem tira os harmônicos do corte e a onda passa um pouco do teto de
                // novo (até ~2 dB num agudo cortado reto). Um limite aqui traria o serrilhado de volta (medido:
                // -26 dB em vez de -59 dB num 7 kHz); o teto exato fica com o limitador depois.
                io[ch][i] = oversamplers[(size_t) ch].process (x, shape) * o;
            }
        }

        // O medidor: quantos dB o pico passou do teto neste pedaço (segura e desce em ~300 ms).
        const float db = over > 1.0f ? gainToDb (over) : 0.0f;
        meterHold = db > meterHold ? db : db + (meterHold - db) * meterRelease;
        setMeter (clipping, meterHold);
    }
}

//==============================================================================
// Lo-fi

namespace
{
    const ParamInfo lofiParams[] = {
        { "bits",  "Bits", 3, 16, 12, "bits", ParamKind::continuous, false, {},
          "Menos bits = mais ruído granulado no som. 12 lembra sampler antigo; abaixo de 6 é videogame." },
        { "rate",  "Taxa", 2000, 48000, 22050, "Hz", ParamKind::continuous, true, {},
          "Amostras por segundo. Abaixo de 12 kHz o agudo vira um brilho metálico e serrilhado." },
        { "wow",   "Fita", 0, 100, 15, "%", ParamKind::continuous, false, {},
          "A fita oscilando: o tom balança devagar e treme de leve. Pouco já dá cara de fita cassete." },
        { "tone",  "Brilho", 1000, 20000, 9000, "Hz", ParamKind::continuous, true, {},
          "Corta o agudo, como um alto-falante pequeno ou uma fita gasta. Em 20 kHz, sem corte." },
        { "noise", "Chiado", 0, 100, 10, "%", ParamKind::continuous, false, {},
          "Chiado que acompanha o volume do som. No silêncio, some junto." },
        { "mix",   "Mistura", 0, 100, 100, "%", ParamKind::continuous, false, {},
          "Quanto do som gasto entra. Abaixo de 50% fica só a textura por trás." },
    };

    static_assert (std::size (lofiParams) == LofiProcessor::count);

    constexpr double baseSeconds = 0.006, wowSeconds = 0.004, flutterSeconds = 0.00015;
    constexpr double wowHz = 0.55, flutterHz = 6.3;
}

const FxInfo& LofiProcessor::staticInfo()
{
    static const FxInfo info { "lofi", "Lo-fi", "Lo-fi", "Cor",
                               "Gasta o som de propósito: menos bits, menos amostras, fita oscilando, menos brilho e chiado. "
                               "Para sample de piano, guitarra e o beat inteiro no lo-fi e no boom bap.",
                               lofiParams };
    return info;
}

void LofiProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = lofiParams[i].def;

    baseDelay = (int) std::lround (baseSeconds * rate);

    for (auto& line : lines)
        line.prepare (baseDelay + (int) std::ceil ((wowSeconds + flutterSeconds) * rate) + 8);

    envBallistics.set (5.0f, 150.0f, rate);

    for (auto* s : { &wowAmount, &quantSteps, &noiseLevel, &wet })
        s->prepare (rate, 30.0f);

    dirty = true;
    snapToTargets();
    reset();
}

void LofiProcessor::snapToTargets() noexcept
{
    update();

    for (auto* s : { &wowAmount, &quantSteps, &noiseLevel, &wet })
        s->snap (s->getTarget());
}

void LofiProcessor::reset() noexcept
{
    for (auto& line : lines)
        line.reset();

    for (auto& f : toneFilter)
        f.reset();

    wowPhase = flutterPhase = 0.0;
    holdPhase = 1.0;   // a primeira amostra já é pega
    currentDelay = (float) baseDelay;
    delayStep = 0.0f;
    held.fill (0.0f);
    env.fill (0.0f);
    noiseSeed = 1;
    controlCounter = 0;
}

void LofiProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void LofiProcessor::update() noexcept
{
    dirty = false;
    wowAmount.setTarget (values[wow] * 0.01f);
    quantSteps.setTarget (values[bits]);
    noiseLevel.setTarget (values[noise] * 0.01f * 0.05f);   // 100% = chiado a -26 dB do som
    wet.setTarget (values[mix] * 0.01f);
    toneCoeffs = SvfCoeffs::make (FilterType::lowpass, values[tone], 0.7071, 0.0, sampleRate);
}

void LofiProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    float* const io[2] = { block.left, block.right };
    const double holdStep = std::min (1.0, (double) values[rate] / sampleRate);
    const bool useTone = values[tone] < 19990.0f;
    auto control = [&]
    {
        const double step = controlRate / sampleRate;
        wowPhase += 2.0 * pi * wowHz * step;
        flutterPhase += 2.0 * pi * flutterHz * step;

        if (wowPhase >= 2.0 * pi)     wowPhase -= 2.0 * pi;
        if (flutterPhase >= 2.0 * pi) flutterPhase -= 2.0 * pi;

        const double amount = wowAmount.skip (controlRate);
        const double next = baseDelay + amount * sampleRate * (wowSeconds * std::sin (wowPhase) + flutterSeconds * std::sin (flutterPhase));
        delayStep = (float) ((next - currentDelay) / controlRate);
        quantStep = std::exp2 (quantSteps.skip (controlRate) - 1.0f);
    };

    for (int i = 0; i < block.numSamples; ++i)
    {
        if (controlCounter == 0)
            control();

        controlCounter = (controlCounter + 1) % controlRate;
        currentDelay += delayStep;

        holdPhase += holdStep;
        const bool take = holdPhase >= 1.0;

        if (take)
            holdPhase -= 1.0;

        const float nl = noiseLevel.next(), m = wet.next();

        for (int c = 0; c < 2; ++c)
        {
            const auto uc = (size_t) c;
            lines[uc].push (io[c][i]);
            const float dry = lines[uc].read (baseDelay);

            if (take)
                held[uc] = lines[uc].readHermite (currentDelay);

            float v = std::round (held[uc] * quantStep) / quantStep;
            // O filtro roda sempre (ligar e desligar não começa do zero); em 20 kHz só não é usado.
            const float filtered = toneFilter[uc].process (v, toneCoeffs);

            if (useTone)
                v = filtered;

            // O chiado segue o volume e some de vez abaixo de -100 dB: no silêncio, silêncio.
            env[uc] = envBallistics.apply (env[uc], std::abs (dry));
            noiseSeed = noiseSeed * 1664525u + 1013904223u;
            const float white = (float) (noiseSeed >> 8) * (2.0f / 16777216.0f) - 1.0f;

            if (env[uc] > 1.0e-5f)
                v += white * nl * env[uc];

            io[c][i] = dry + m * (v - dry);
        }
    }
}

} // namespace whynot::dsp
