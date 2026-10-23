#include "Enhance.h"

namespace whynot::dsp
{

namespace
{
    const ParamInfo bassParams[] = {
        { "freq",   "Frequência",   40, 200, 90, "Hz", ParamKind::continuous, true, {},
          "Abaixo daqui o som vira harmônicos. 808 e sub: 70 a 100 Hz; baixo e kick: 100 a 150 Hz." },
        { "amount", "Harmônicos",   0, 100, 50,  "%",  ParamKind::continuous, false, {},
          "Quanto dos harmônicos entra. Confira num fone pequeno ou no celular." },
        { "color",  "Cor",          0, 100, 40,  "%",  ParamKind::continuous, false, {},
          "0% = só o 2º harmônico (redondo, quente); 100% = só o 3º (mais duro, aparece mais)." },
        { "sub",    "Sub original", 0, 100, 100, "%",  ParamKind::continuous, false, {},
          "Quanto do grave de verdade fica. Abaixar libera volume no master (o celular nem toca o sub)." },
        { "output", "Saída",        -12, 12, 0,  "dB" },
    };

    static_assert (std::size (bassParams) == BassEnhancerProcessor::count);
}

const FxInfo& BassEnhancerProcessor::staticInfo()
{
    static const FxInfo info { "bassplus", "Grave+", "Grave+", "Cor",
                               "Faz o 808 e o baixo aparecerem em caixa pequena e no celular, com harmônicos do próprio grave.",
                               bassParams };
    return info;
}

void BassEnhancerProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = bassParams[i].def;

    envRelease = onePoleCoeff (100.0f, rate);
    envHoldSamples = (int) (0.031 * rate);

    for (auto* s : { &harmonicGain, &oddMix, &subGain, &outGain })
        s->prepare (rate, 20.0f);

    dirty = true;
    snapToTargets();
    reset();
}

void BassEnhancerProcessor::snapToTargets() noexcept
{
    update();

    for (auto* s : { &harmonicGain, &oddMix, &subGain, &outGain })
        s->snap (s->getTarget());
}

void BassEnhancerProcessor::reset() noexcept
{
    for (auto& s : split)
        s.reset();

    hp1.reset();
    hp2.reset();
    lp1.reset();
    env = 0.0f;
    envHold = 0;
    setMeter (harmonicLevel, -120.0f);
}

void BassEnhancerProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void BassEnhancerProcessor::update() noexcept
{
    dirty = false;
    const double f = values[freq];

    for (auto& s : split)
        s.setup (f, sampleRate);

    // Harmônicos só acima do corte (4ª ordem) e abaixo de ~6x ele: nada de sub novo nem chiado.
    hpCoeffs = SvfCoeffs::make (FilterType::highpass, f, 0.7071, 0.0, sampleRate);
    lpCoeffs = SvfCoeffs::make (FilterType::lowpass, std::min (f * 6.0, sampleRate * 0.45), 0.7071, 0.0, sampleRate);
    harmonicGain.setTarget (values[amount] * 0.015f);   // 100% = os harmônicos 3,5 dB acima do grave original
    oddMix.setTarget (values[color] * 0.01f);
    subGain.setTarget (values[sub] * 0.01f);
    outGain.setTarget (dbToGain (values[output]));
}

void BassEnhancerProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    float peakHarmonic = 0.0f;

    for (int i = 0; i < block.numSamples; ++i)
    {
        float lowL, highL, lowR, highR;
        split[0].process (block.left[i], lowL, highL);
        split[1].process (block.right[i], lowR, highR);

        // Harmônicos do grave em mono (o grave mora no centro).
        const float low = 0.5f * (lowL + lowR);
        if (std::abs (low) >= env)
        {
            env = std::abs (low);
            envHold = envHoldSamples;
        }
        else if (envHold > 0)
        {
            --envHold;
        }
        else
        {
            env *= envRelease;
        }
        float h = 0.0f;

        if (env > 1.0e-6f)
        {
            const float u = std::clamp (low / env, -1.0f, 1.0f);
            const float even = (2.0f * u * u - 1.0f) * env;       // 2º harmônico
            const float odd = (4.0f * u * u - 3.0f) * u * env;    // 3º harmônico
            const float c = oddMix.next();
            h = harmonicGain.next() * (even + c * (odd - even));
        }
        else
        {
            oddMix.next();
            harmonicGain.next();
        }

        h = lp1.process (hp2.process (hp1.process (h, hpCoeffs), hpCoeffs), lpCoeffs);
        peakHarmonic = std::max (peakHarmonic, std::abs (h));

        const float s = subGain.next(), o = outGain.next();
        block.left[i] = (highL + s * lowL + h) * o;
        block.right[i] = (highR + s * lowR + h) * o;
    }

    setMeter (harmonicLevel, gainToDb (peakHarmonic));
}

} // namespace whynot::dsp
