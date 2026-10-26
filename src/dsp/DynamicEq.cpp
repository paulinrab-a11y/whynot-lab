#include "DynamicEq.h"

#include <iterator>

namespace whynot::dsp
{

namespace
{
    const ParamInfo dynEqParams[] = {
        { "b1On",    "Banda 1",    0, 1, 1, "", ParamKind::toggle, false, {}, "Liga a banda 1." },
        { "b1Freq",  "Frequência", 20, 20000, 250, "Hz", ParamKind::continuous, true, {},
          "O centro da banda 1. 200 a 400 Hz é onde o grave embola." },
        { "b1Q",     "Q",          0.3f, 10, 1.2f, "", ParamKind::continuous, true, {},
          "Largura da banda 1: Q alto pega uma faixa estreita." },
        { "b1Thr",   "Limiar",     -60, 0, -24, "dB", ParamKind::continuous, false, {},
          "A banda 1 só mexe quando essa faixa passa daqui." },
        { "b1Range", "Faixa",      -24, 24, -6, "dB", ParamKind::continuous, false, {},
          "Quanto a banda 1 pode mudar. Negativa corta quando passa do limiar; positiva realça." },
        { "b2On",    "Banda 2",    0, 1, 1, "", ParamKind::toggle, false, {}, "Liga a banda 2." },
        { "b2Freq",  "Frequência", 20, 20000, 3500, "Hz", ParamKind::continuous, true, {},
          "O centro da banda 2. 2 a 5 kHz é onde a voz e o synth furam." },
        { "b2Q",     "Q",          0.3f, 10, 2, "", ParamKind::continuous, true, {},
          "Largura da banda 2." },
        { "b2Thr",   "Limiar",     -60, 0, -24, "dB", ParamKind::continuous, false, {},
          "A banda 2 só mexe quando essa faixa passa daqui." },
        { "b2Range", "Faixa",      -24, 24, -4, "dB", ParamKind::continuous, false, {},
          "Quanto a banda 2 pode mudar. Negativa corta; positiva realça." },
        { "ratio",   "Razão",      1, 10, 3, ":1", ParamKind::continuous, false, {},
          "Quanto a banda reage ao que passa do limiar. 3:1 = a cada 3 dB acima, 2 dB de mudança." },
        { "attack",  "Ataque",     0.5f, 100, 5, "ms", ParamKind::continuous, true, {},
          "Quão rápido a banda entra. Rápido pega a sílaba; lento deixa o ataque passar." },
        { "release", "Soltura",    10, 1000, 120, "ms", ParamKind::continuous, true, {},
          "Quão rápido a banda volta para 0 dB." },
        { "output",  "Saída",      -12, 12, 0, "dB", ParamKind::continuous, false, {},
          "Volume final." },
    };

    static_assert (std::size (dynEqParams) == DynamicEqProcessor::count);
}

const FxInfo& DynamicEqProcessor::staticInfo()
{
    static const FxInfo info { "dynEq", "EQ dinâmico", "EQ din.", "Equalização",
                               "Duas bandas que só cortam (ou realçam) quando aquela faixa passa do limiar: tira o embolado do 808 "
                               "só nas notas que embolam e o agudo que fura da voz só onde fura. Aceita sidechain.",
                               dynEqParams, true };
    return info;
}

float DynamicEqProcessor::gainChange (float levelDb, float thresholdDb, float rangeDb, float r) noexcept
{
    const float over = levelDb - thresholdDb;

    if (over <= 0.0f || rangeDb == 0.0f)
        return 0.0f;

    const float amount = std::min (std::abs (rangeDb), over * (1.0f - 1.0f / std::max (1.0f, r)));
    return rangeDb < 0.0f ? -amount : amount;
}

void DynamicEqProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = dynEqParams[i].def;

    outGain.prepare (rate, 20.0f);
    rampCoeff = onePoleCoeff (3.0f, rate / rampStep);
    dirty = true;
    snapToTargets();
    reset();
}

void DynamicEqProcessor::snapToTargets() noexcept
{
    update();
    outGain.snap (outGain.getTarget());
}

void DynamicEqProcessor::reset() noexcept
{
    for (auto& b : bands)
    {
        b.detector.reset();

        for (auto& f : b.bell)
            f.reset();

        b.env = 0.0f;
        b.targetDb = b.appliedDb = 0.0f;
        b.bellCoeffs = SvfCoeffs::make (FilterType::bell, 1000.0, 1.0, 0.0, sampleRate);
    }

    setMeter (band1Gain, 0.0f);
    setMeter (band2Gain, 0.0f);
}

void DynamicEqProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void DynamicEqProcessor::update() noexcept
{
    dirty = false;

    for (int b = 0; b < numBands; ++b)
    {
        const int p = b * paramsPerBand;
        auto& band = bands[(size_t) b];
        band.detectCoeffs = SvfCoeffs::make (FilterType::bandpass, values[(size_t) p + 1], values[(size_t) p + 2], 0.0, sampleRate);
        band.bellCoeffs = SvfCoeffs::make (FilterType::bell, values[(size_t) p + 1], values[(size_t) p + 2], band.appliedDb, sampleRate);
    }

    ballistics.set (values[attack], values[release], sampleRate);
    outGain.setTarget (dbToGain (values[output]));
}

void DynamicEqProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const bool external = block.sideLeft != nullptr && block.sideRight != nullptr;

    for (int start = 0; start < block.numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, block.numSamples - start);

        // 1. O detector de cada banda anda o pedaço inteiro (16 amostras), e o nível do fim dele vira o alvo
        //    do sino. A banda 2 escuta o que saiu da banda 1.
        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[(size_t) b];
            const int p = b * paramsPerBand;
            const bool on = values[(size_t) p] >= 0.5f;
            float target = 0.0f;

            if (on)
            {
                for (int i = start; i < start + n; ++i)
                {
                    const float d = external ? 0.5f * (block.sideLeft[i] + block.sideRight[i])
                                             : 0.5f * (block.left[i] + block.right[i]);
                    band.env = ballistics.apply (band.env, std::abs (band.detector.process (d, band.detectCoeffs)));
                }

                target = gainChange (gainToDb (band.env), values[(size_t) p + 3], values[(size_t) p + 4], values[ratio]);
            }
            else
            {
                band.env = 0.0f;
                band.detector.reset();
            }

            band.targetDb = target;

            // 2. O sino nos dois canais. O ganho anda até o alvo em passos de 4 amostras; perto do fim encosta
            //    nele, e em 0 dB o sino devolve a entrada exata.
            for (int from = start; from < start + n; from += rampStep)
            {
                if (band.appliedDb != band.targetDb)
                {
                    const float next = band.targetDb + (band.appliedDb - band.targetDb) * rampCoeff;
                    band.appliedDb = std::abs (next - band.targetDb) < 0.01f ? band.targetDb : next;
                    band.bellCoeffs = SvfCoeffs::make (FilterType::bell, values[(size_t) p + 1], values[(size_t) p + 2], band.appliedDb, sampleRate);
                }

                for (int i = from; i < std::min (from + rampStep, start + n); ++i)
                {
                    block.left[i] = band.bell[0].process (block.left[i], band.bellCoeffs);
                    block.right[i] = band.bell[1].process (block.right[i], band.bellCoeffs);
                }
            }
        }

        for (int i = start; i < start + n; ++i)
        {
            const float g = outGain.next();
            block.left[i] *= g;
            block.right[i] *= g;
        }
    }

    setMeter (band1Gain, bands[0].appliedDb);
    setMeter (band2Gain, bands[1].appliedDb);
}

} // namespace whynot::dsp
