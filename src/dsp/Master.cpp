#include "Master.h"

namespace whynot::dsp
{

//==============================================================================
// Estéreo

namespace
{
    const ParamInfo stereoParams[] = {
        { "width",       "Largura",          0, 200, 100, "%",  ParamKind::continuous, false, {}, "0% = mono, 100% = original, 200% = bem aberto." },
        { "monoBass",    "Grave mono até",   0, 400, 0,   "Hz", ParamKind::continuous, false, {}, "Abaixo daqui tudo fica no centro (808 e kick firmes). 0 = desligado." },
        { "pan",         "Balanço",          -100, 100, 0, "%" },
        { "gain",        "Ganho",            -24, 24, 0,  "dB" },
        { "invertLeft",  "Inverter fase E",  0, 1, 0,     "",   ParamKind::toggle },
        { "invertRight", "Inverter fase D",  0, 1, 0,     "",   ParamKind::toggle },
        { "mono",        "Mono",             0, 1, 0,     "",   ParamKind::toggle, false, {}, "Ouça em mono para conferir se nada some." },
    };

    static_assert (std::size (stereoParams) == StereoProcessor::count);
}

const FxInfo& StereoProcessor::staticInfo()
{
    static const FxInfo info { "stereo", "Estéreo", "Estéreo", "Imagem",
                               "Abre ou fecha a imagem, deixa o grave mono e corrige fase.",
                               stereoParams };
    return info;
}

void StereoProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = stereoParams[i].def;

    for (auto* s : { &widthAmount, &outGain, &invertL, &invertR, &balanceL, &balanceR })
    {
        s->prepare (rate, 20.0f);
        s->snap (1.0f);
    }

    bassMonoMix.prepare (rate, 7.0f);
    bassMonoMix.snap (0.0f);
    corrCoeff = std::exp (-1.0 / (0.3 * rate));
    splitRunning = false;
    monoFreq = 0.0f;
    reset();
}

void StereoProcessor::snapToTargets() noexcept
{
    for (auto* s : { &widthAmount, &outGain, &invertL, &invertR, &balanceL, &balanceR, &bassMonoMix })
        s->snap (s->getTarget());
}

void StereoProcessor::reset() noexcept
{
    midSplit.reset();
    sideSplit.reset();
    corrLR = corrLL = corrRR = 0.0;
}

void StereoProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;

    widthAmount.setTarget (values[mono] >= 0.5f ? 0.0f : values[width] * 0.01f);
    outGain.setTarget (dbToGain (values[gain]));

    // Inverter a fase desliza de +1 a -1 em 20 ms: sem estalo.
    invertL.setTarget (values[invertLeft] >= 0.5f ? -1.0f : 1.0f);
    invertR.setTarget (values[invertRight] >= 0.5f ? -1.0f : 1.0f);

    const float p = values[pan] * 0.01f;
    balanceL.setTarget (p > 0.0f ? 1.0f - p : 1.0f);
    balanceR.setTarget (p < 0.0f ? 1.0f + p : 1.0f);

    if (index == monoBass)
    {
        // Liga e desliga por mistura; a última frequência fica até a mistura sumir.
        const bool on = value >= 20.0f;
        bassMonoMix.setTarget (on ? 1.0f : 0.0f);

        if (on && value != monoFreq)
        {
            monoFreq = value;
            midSplit.setup (value, sampleRate);
            sideSplit.setup (value, sampleRate);
        }
    }
}

void StereoProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    const bool needed = bassMonoMix.getTarget() > 0.0f || bassMonoMix.getCurrent() > 0.0f;

    if (needed && ! splitRunning)
    {
        midSplit.reset();
        sideSplit.reset();
    }

    splitRunning = needed && monoFreq >= 20.0f;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float g = outGain.next();

        // A inversão de fase vem antes da largura (corrige o sinal de entrada); o balanço, no fim.
        const float l = block.left[i] * g * invertL.next();
        const float r = block.right[i] * g * invertR.next();

        float mid = 0.5f * (l + r);
        float side = 0.5f * (l - r);

        if (splitRunning)
        {
            const float bm = bassMonoMix.next();
            float lo, hi, sideLo, sideHi;
            midSplit.process (mid, lo, hi);
            sideSplit.process (side, sideLo, sideHi);
            mid += bm * ((lo + hi) - mid);       // mesmo passa-tudo do lado: fase coerente
            side += bm * (sideHi - side);        // o lado perde o grave: grave no centro
        }

        side *= widthAmount.next();

        const float ol = (mid + side) * balanceL.next();
        const float orr = (mid - side) * balanceR.next();
        block.left[i] = ol;
        block.right[i] = orr;

        corrLR = corrLR * corrCoeff + (double) ol * orr;
        corrLL = corrLL * corrCoeff + (double) ol * ol;
        corrRR = corrRR * corrCoeff + (double) orr * orr;
    }

    const double denom = std::sqrt (corrLL * corrRR);
    setMeter (correlation, denom > 1.0e-12 ? (float) std::clamp (corrLR / denom, -1.0, 1.0) : 1.0f);
}

//==============================================================================
// Mínimo deslizante

void SlidingMinimum::prepare (int windowSize)
{
    window = std::max (1, windowSize);
    size = window + 2;   // cabe a janela cheia mais o valor novo, sem a fila parecer vazia
    values.assign ((size_t) size, 1.0f);
    stamps.assign ((size_t) size, 0);
    reset();
}

void SlidingMinimum::reset() noexcept
{
    head = tail = 0;
    now = 0;
}

float SlidingMinimum::push (float value) noexcept
{
    // Descarta do fim tudo que é maior ou igual ao novo: nunca será o mínimo.
    while (tail != head)
    {
        const int last = (tail - 1 + size) % size;

        if (values[(size_t) last] < value)
            break;

        tail = last;
    }

    values[(size_t) tail] = value;
    stamps[(size_t) tail] = now;
    tail = (tail + 1) % size;

    // Descarta do começo o que saiu da janela.
    while (stamps[(size_t) head] <= now - window)
        head = (head + 1) % size;

    ++now;
    return values[(size_t) head];
}

//==============================================================================
// Limitador

namespace
{
    const ParamInfo limiterParams[] = {
        { "gain",    "Ganho",   0, 24, 0,       "dB",   ParamKind::continuous, false, {}, "Empurra o volume contra o teto." },
        { "ceiling", "Teto",    -12, 0, -1,     "dBTP", ParamKind::continuous, false, {}, "Pico máximo de saída. -1 dBTP é seguro para streaming." },
        { "release", "Soltura", 1, 1000, 80,    "ms",   ParamKind::continuous, true,  {}, "Curta = mais alto e mais sujo; longa = mais limpo." },
        { "clip",    "Clipper", 0, 100, 0,      "%",    ParamKind::continuous, false, {}, "Corta a ponta dos picos antes do limitador: mais volume, mais agressivo (trap e funk)." },
        { "target",  "Alvo",    -20, -4, -9,    "LUFS", ParamKind::continuous, false, {}, "Referência de loudness para o medidor e o botão \"Igualar alvo\"." },
    };

    static_assert (std::size (limiterParams) == LimiterProcessor::count);

    float softClip (float u, float threshold) noexcept
    {
        const float a = std::abs (u);
        const float knee = 0.6f * threshold;

        if (a <= knee)
            return u;

        const float y = knee + (threshold - knee) * std::tanh ((a - knee) / (threshold - knee));
        return std::copysign (y, u);
    }
}

const FxInfo& LimiterProcessor::staticInfo()
{
    static const FxInfo info { "limiter", "Limitador", "Limiter", "Master",
                               "O último do master: deixa a música alta sem passar do teto, com medidor de LUFS.",
                               limiterParams };
    return info;
}

void LimiterProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = limiterParams[i].def;

    lookahead = std::max (8, (int) std::lround (0.002 * rate));

    for (auto& os : clipOversamplers) os.prepare();
    for (auto& os : detectors) os.prepare();

    clipLatency = clipOversamplers[0].latency();
    detectDelay = detectors[0].upsampleLatency();
    latency = clipLatency + detectDelay + lookahead;

    for (auto& d : audioDelay)
        d.prepare (lookahead + detectDelay + 8);

    holdMin.prepare (lookahead + 3);
    boxRing.assign ((size_t) lookahead, 1.0f);

    inGain.prepare (rate, 20.0f);
    ceilingGain.prepare (rate, 20.0f);
    clipAmount.prepare (rate, 20.0f);
    inGain.snap (1.0f);
    ceilingGain.snap (dbToGain (values[ceiling]));
    clipAmount.snap (0.0f);
    releaseCoeff = onePoleCoeff (values[release], rate);

    loudnessMeter.prepare (rate);
    reset();
}

void LimiterProcessor::snapToTargets() noexcept
{
    for (auto* s : { &inGain, &ceilingGain, &clipAmount })
        s->snap (s->getTarget());
}

void LimiterProcessor::reset() noexcept
{
    for (auto& os : clipOversamplers) os.reset();
    for (auto& os : detectors) os.reset();
    for (auto& d : audioDelay) d.reset();

    holdMin.reset();
    std::fill (boxRing.begin(), boxRing.end(), 1.0f);
    boxPos = 0;
    boxSum = (double) lookahead;
    envelope = 1.0f;
    // O medidor de loudness não zera aqui: comparar com e sem o limitador não perde a medição.
}

void LimiterProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;

    switch (index)
    {
        case gain:    inGain.setTarget (dbToGain (value)); break;
        case ceiling: ceilingGain.setTarget (dbToGain (value)); break;
        case release: releaseCoeff = onePoleCoeff (value, sampleRate); break;
        case clip:    clipAmount.setTarget (value * 0.01f); break;
        default: break;
    }
}

void LimiterProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;
    float* io[2] = { block.left, block.right };
    float minGain = 1.0f, outPeak = 0.0f;
    const int readDelay = lookahead + detectDelay;
    const float invLookahead = 1.0f / (float) lookahead;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float g = inGain.next();
        const bool moving = ceilingGain.isSmoothing() || clipAmount.isSmoothing() || i == 0;
        const float ceil = ceilingGain.next();
        const float amount = clipAmount.next();

        // Clipper: 100% corta no teto; menos que isso corta acima do teto (até +6 dB).
        // De 0 a 5% ele entra aos poucos, sem degrau ao ligar ou desligar.
        if (moving)
            clipThreshold = ceil * dbToGain ((1.0f - amount) * 6.0f);

        const float threshold = clipThreshold;
        const float blend = std::min (1.0f, amount * 20.0f);
        float peak = 0.0f;

        for (int c = 0; c < 2; ++c)
        {
            float x = io[c][i] * g;

            x = clipOversamplers[(size_t) c].process (x, [threshold, blend] (float v)
            {
                return blend > 0.0f ? v + blend * (softClip (v, threshold) - v) : v;
            });

            float os[4];
            detectors[(size_t) c].upsample (x, os);

            for (float v : os)
                peak = std::max (peak, std::abs (v));

            audioDelay[(size_t) c].push (x);
        }

        // Ganho necessário agora, segurado pela janela de antecipação e suavizado por uma
        // média móvel do mesmo tamanho: o ganho chega ao valor certo exatamente no pico.
        const float required = peak > ceil ? ceil / peak : 1.0f;
        const float held = holdMin.push (required);
        boxSum += (double) held - boxRing[(size_t) boxPos];
        boxRing[(size_t) boxPos] = held;
        boxPos = boxPos + 1 == lookahead ? 0 : boxPos + 1;
        const float attackGain = std::min (1.0f, (float) boxSum * invLookahead);

        envelope = attackGain < envelope ? attackGain : attackGain + (envelope - attackGain) * releaseCoeff;
        minGain = std::min (minGain, envelope);

        for (int c = 0; c < 2; ++c)
        {
            // Segurança final: nenhuma amostra passa do teto (e NaN vira silêncio).
            float y = audioDelay[(size_t) c].read (readDelay) * envelope;
            y = y == y ? std::clamp (y, -ceil, ceil) : 0.0f;
            io[c][i] = y;
            outPeak = std::max (outPeak, std::abs (y));
        }
    }

    loudnessMeter.process (block.left, block.right, block.numSamples);
    setMeter (gainReduction, -gainToDb (minGain));
    setMeter (outputPeak, gainToDb (outPeak));
}

//==============================================================================
// Medidor

namespace
{
    const ParamInfo meterParams[] = {
        { "target", "Alvo", -24, -4, -14, "LUFS", ParamKind::continuous, false, {}, "Referência desenhada no medidor." },
    };

    static_assert (std::size (meterParams) == MeterProcessor::count);
}

const FxInfo& MeterProcessor::staticInfo()
{
    static const FxInfo info { "meter", "Medidor", "Medidor", "Master",
                               "Loudness (LUFS), pico real e correlação. Não muda o som.",
                               meterParams };
    return info;
}

void MeterProcessor::prepare (double rate, int)
{
    loudnessMeter.prepare (rate);
}

void MeterProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;
    loudnessMeter.process (block.left, block.right, block.numSamples);

    for (int i = 0; i < block.numSamples; ++i)
        tap.push (0.5f * (block.left[i] + block.right[i]));
}

//==============================================================================
float limitTruePeakOffline (float* const* channels, int numChannels, int numSamples, double sampleRate, float ceilingDb)
{
    if (channels == nullptr || numChannels < 1 || numSamples <= 0)
        return 0.0f;

    constexpr int blockSize = 1024;
    LimiterProcessor limiter;
    limiter.prepare (sampleRate, blockSize);
    limiter.setParam (LimiterProcessor::gain, 0.0f);
    limiter.setParam (LimiterProcessor::ceiling, std::clamp (ceilingDb, -12.0f, 0.0f));
    limiter.setParam (LimiterProcessor::clip, 0.0f);
    limiter.snapToTargets();

    const int latency = limiter.latencySamples();
    const int total = numSamples + latency;   // a cauda empurra para fora o que ficou na antecipação
    std::vector<float> left ((size_t) blockSize), right ((size_t) blockSize);
    float maxReduction = 0.0f;

    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int len = std::min (blockSize, total - pos);

        for (int i = 0; i < len; ++i)
        {
            const int src = pos + i;
            left[(size_t) i] = src < numSamples ? channels[0][src] : 0.0f;
            right[(size_t) i] = numChannels > 1 ? (src < numSamples ? channels[1][src] : 0.0f) : left[(size_t) i];
        }

        AudioBlock block { left.data(), right.data(), len };
        limiter.process (block);
        maxReduction = std::max (maxReduction, limiter.meter (LimiterProcessor::gainReduction));

        // A saída k corresponde à entrada k - latência: escreve atrás do que já foi lido.
        for (int i = 0; i < len; ++i)
        {
            const int dst = pos + i - latency;

            if (dst < 0 || dst >= numSamples)
                continue;

            channels[0][dst] = left[(size_t) i];

            if (numChannels > 1)
                channels[1][dst] = right[(size_t) i];
        }
    }

    return maxReduction;
}

} // namespace whynot::dsp
