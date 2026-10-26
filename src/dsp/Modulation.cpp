#include "Modulation.h"

#include <iterator>

namespace whynot::dsp
{

namespace
{
    double wrapPhase (double p) noexcept   { return p >= 2.0 * pi ? p - 2.0 * pi : p; }

    /** Segundos para uma realimentação `fb` cair 60 dB, a `loopSeconds` por volta. */
    double feedbackTail (double fb, double loopSeconds) noexcept
    {
        const double g = std::min (0.95, std::abs (fb));
        return g < 0.01 ? loopSeconds : loopSeconds * (-3.0 / std::log10 (g)) + loopSeconds;
    }
}

//==============================================================================
// Chorus

namespace
{
    const char* const chorusVoices[] = { "2", "4" };

    const ParamInfo chorusParams[] = {
        { "rate",   "Velocidade", 0.05f, 5, 0.8f, "Hz", ParamKind::continuous, true, {},
          "Quantas vezes por segundo o atraso ondula. Devagar (0,3 a 1 Hz) engorda; rápido vira vibrato." },
        { "depth",  "Profundidade", 0, 100, 45, "%", ParamKind::continuous, false, {},
          "Quanto o atraso ondula. Pouco engrossa sem desafinar; muito dá o som de fita velha." },
        { "delay",  "Atraso", 7, 30, 14, "ms", ParamKind::continuous, false, {},
          "Onde as cópias ficam. Menos de 10 ms soa mais colado; acima de 20 ms abre como duas pessoas." },
        { "voices", "Cópias", 0, 1, 0, "", ParamKind::choice, false, chorusVoices,
          "2 cópias (uma de cada lado) ou 4 (mais cheio, para pad e guitarra)." },
        { "width",  "Largura", 0, 100, 100, "%", ParamKind::continuous, false, {},
          "Quanto as cópias abrem para os lados. 0% = tudo no centro (seguro para o mono)." },
        { "tone",   "Brilho das cópias", 1500, 20000, 12000, "Hz", ParamKind::continuous, true, {},
          "Corta o agudo só das cópias: mais baixo fica mais suave e escondido atrás do som." },
        { "mix",    "Mistura", 0, 100, 40, "%", ParamKind::continuous, false, {},
          "Quanto das cópias entra. 50% é o chorus clássico; 100% deixa só as cópias (com 2 cópias e largura 100%, vira vibrato)." },
    };

    static_assert (std::size (chorusParams) == ChorusProcessor::count);

    constexpr float chorusPan[ChorusProcessor::maxVoices] = { -1.0f, 1.0f, -0.5f, 0.5f };
    constexpr float chorusDelayFactor[ChorusProcessor::maxVoices] = { 1.0f, 1.0f, 1.32f, 1.32f };
    constexpr double chorusRateFactor[ChorusProcessor::maxVoices] = { 1.0, 1.0, 0.83, 0.83 };
}

const FxInfo& ChorusProcessor::staticInfo()
{
    static const FxInfo info { "chorus", "Chorus", "Chorus", "Modulação",
                               "Cópias com um atraso curto que ondula devagar, abertas nos lados: o som fica mais gordo e largo. "
                               "Clássico em pad, guitarra limpa, Rhodes e backing vocal.",
                               chorusParams };
    return info;
}

void ChorusProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = chorusParams[i].def;

    // 30 ms vezes o fator das cópias 3 e 4, mais a ondulação máxima.
    for (auto& line : lines)
        line.prepare ((int) std::ceil (0.05 * rate) + 8);

    for (auto* s : { &baseMs, &depthMs, &widthAmount, &wet, &extraVoices })
        s->prepare (rate, 40.0f);

    dirty = true;
    snapToTargets();
    reset();
}

void ChorusProcessor::snapToTargets() noexcept
{
    update();

    for (auto* s : { &baseMs, &depthMs, &widthAmount, &wet, &extraVoices })
        s->snap (s->getTarget());
}

void ChorusProcessor::reset() noexcept
{
    for (auto& line : lines)
        line.reset();

    for (int v = 0; v < maxVoices; ++v)
    {
        // Fases fixas: o render sai sempre igual. Esquerda e direita a 90° uma da outra.
        phase[(size_t) v] = 0.5 * pi * v;
        currentDelay[(size_t) v] = (float) (baseMs.getCurrent() * chorusDelayFactor[v] * 0.001 * sampleRate);
        delayStep[(size_t) v] = 0.0f;
    }

    gainL.fill (0.0f);
    gainR.fill (0.0f);
    lpState.fill (0.0f);
    controlCounter = 0;
}

void ChorusProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void ChorusProcessor::update() noexcept
{
    dirty = false;
    baseMs.setTarget (values[delay]);
    depthMs.setTarget (values[depth] * 0.01f * 4.0f);   // até 4 ms de ondulação
    widthAmount.setTarget (values[width] * 0.01f);
    wet.setTarget (values[mix] * 0.01f);
    extraVoices.setTarget (values[voices] >= 0.5f ? 1.0f : 0.0f);
    lpCoeff = (float) std::exp (-2.0 * pi * values[tone] / sampleRate);
}

void ChorusProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    auto control = [&]
    {
        const double step = controlRate / sampleRate;
        const double base = baseMs.skip (controlRate) * 0.001 * sampleRate;
        const double amp = depthMs.skip (controlRate) * 0.001 * sampleRate;
        const float w = widthAmount.skip (controlRate);
        const float extra = extraVoices.skip (controlRate);

        for (int v = 0; v < maxVoices; ++v)
        {
            const auto uv = (size_t) v;
            phase[uv] = wrapPhase (phase[uv] + 2.0 * pi * values[rate] * chorusRateFactor[v] * step);

            const double baseV = base * chorusDelayFactor[v];
            const double next = std::clamp (baseV + std::min (amp, baseV - 0.002 * sampleRate) * std::sin (phase[uv]),
                                            2.0, (double) lines[0].capacity() - 4.0);
            delayStep[uv] = (float) ((next - currentDelay[uv]) / controlRate);

            const float angle = (float) (0.25 * pi * (1.0 + chorusPan[v] * w));
            const float voiceGain = v < 2 ? 1.0f : 0.7f * extra;
            gainL[uv] = std::cos (angle) * voiceGain;
            gainR[uv] = std::sin (angle) * voiceGain;
        }

        // Cada lado soma 1 com as cópias em fase (no grave elas estão): ligar o chorus não sobe o volume,
        // com qualquer largura e 2 ou 4 cópias.
        float sumL = 0.0f, sumR = 0.0f;

        for (int v = 0; v < maxVoices; ++v)
        {
            sumL += gainL[(size_t) v];
            sumR += gainR[(size_t) v];
        }

        for (int v = 0; v < maxVoices; ++v)
        {
            gainL[(size_t) v] /= std::max (sumL, 1.0e-6f);
            gainR[(size_t) v] /= std::max (sumR, 1.0e-6f);
        }
    };

    for (int i = 0; i < block.numSamples; ++i)
    {
        if (controlCounter == 0)
            control();

        controlCounter = (controlCounter + 1) % controlRate;

        const float l = block.left[i], r = block.right[i];
        lines[0].push (l);
        lines[1].push (r);

        float cl = 0.0f, cr = 0.0f;

        for (int v = 0; v < maxVoices; ++v)
        {
            const auto uv = (size_t) v;
            currentDelay[uv] += delayStep[uv];
            const float c = lines[(size_t) (v & 1)].readHermite (currentDelay[uv]);
            cl += c * gainL[uv];
            cr += c * gainR[uv];
        }

        // O brilho: um polo só nas cópias.
        lpState[0] = cl + (lpState[0] - cl) * lpCoeff;
        lpState[1] = cr + (lpState[1] - cr) * lpCoeff;

        const float m = wet.next();
        block.left[i] = l + m * (lpState[0] - l);
        block.right[i] = r + m * (lpState[1] - r);
    }
}

//==============================================================================
// Flanger

namespace
{
    const ParamInfo flangerParams[] = {
        { "rate",     "Velocidade", 0.02f, 5, 0.25f, "Hz", ParamKind::continuous, true, {},
          "Quantas vezes por segundo o efeito sobe e desce. 0,1 a 0,3 Hz é o avião passando." },
        { "depth",    "Profundidade", 0, 100, 70, "%", ParamKind::continuous, false, {},
          "Até onde o atraso vai: quanto do espectro os vales percorrem." },
        { "delay",    "Atraso", 0.1f, 10, 1, "ms", ParamKind::continuous, true, {},
          "O ponto mais curto do atraso. Mais curto = vales mais agudos e espaçados." },
        { "feedback", "Realimentação", -95, 95, 50, "%", ParamKind::continuous, false, {},
          "Devolve a cópia para a entrada: vales mais fundos e som metálico. Negativa fica oca." },
        { "stereo",   "Estéreo", 0, 180, 90, "°", ParamKind::continuous, false, {},
          "Defasagem entre os lados. 0° = os dois juntos; 90° a 180° = o efeito gira de um lado para o outro." },
        { "mix",      "Mistura", 0, 100, 50, "%", ParamKind::continuous, false, {},
          "50% dá os vales mais fundos. 100% só a cópia (vira vibrato)." },
    };

    static_assert (std::size (flangerParams) == FlangerProcessor::count);

    constexpr double maxSweepMs = 7.0;
}

const FxInfo& FlangerProcessor::staticInfo()
{
    static const FxInfo info { "flanger", "Flanger", "Flanger", "Modulação",
                               "Uma cópia com atraso curtíssimo que sobe e desce: os vales andam pelo som, como um avião passando. "
                               "Bom em hat, pad, guitarra e transição.",
                               flangerParams };
    return info;
}

double FlangerProcessor::tailSecondsFor (const float* v) const noexcept
{
    const double loop = 0.001 * (v[delay] + maxSweepMs * v[depth] * 0.01);
    return feedbackTail (v[feedback] * 0.01, loop);
}

void FlangerProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = flangerParams[i].def;

    for (auto& line : lines)
        line.prepare ((int) std::ceil ((10.0 + maxSweepMs) * 0.001 * rate) + 8);

    for (auto* s : { &minMs, &sweepMs, &fb, &phaseOffset, &wet })
        s->prepare (rate, 40.0f);

    dirty = true;
    snapToTargets();
    reset();
}

void FlangerProcessor::snapToTargets() noexcept
{
    update();

    for (auto* s : { &minMs, &sweepMs, &fb, &phaseOffset, &wet })
        s->snap (s->getTarget());
}

void FlangerProcessor::reset() noexcept
{
    for (auto& line : lines)
        line.reset();

    lfoPhase = 0.0;
    const float start = std::max (3.0f, (float) (minMs.getCurrent() * 0.001 * sampleRate));
    currentDelay.fill (start);
    delayStep.fill (0.0f);
    lastTap.fill (0.0f);
    controlCounter = 0;
}

void FlangerProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void FlangerProcessor::update() noexcept
{
    dirty = false;
    minMs.setTarget (values[delay]);
    sweepMs.setTarget ((float) (maxSweepMs * values[depth] * 0.01));
    fb.setTarget (std::clamp (values[feedback] * 0.01f, -0.95f, 0.95f));
    phaseOffset.setTarget ((float) (values[stereo] * pi / 180.0));
    wet.setTarget (values[mix] * 0.01f);
}

void FlangerProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    float* const io[2] = { block.left, block.right };

    auto control = [&]
    {
        lfoPhase = wrapPhase (lfoPhase + 2.0 * pi * values[rate] * controlRate / sampleRate);
        const double lo = minMs.skip (controlRate) * 0.001 * sampleRate;
        const double sweep = sweepMs.skip (controlRate) * 0.001 * sampleRate;
        const double offset = phaseOffset.skip (controlRate);

        for (int c = 0; c < 2; ++c)
        {
            // Cosseno elevado: o atraso fica entre o mínimo e o mínimo mais a varredura, sem passar do zero.
            const double p = lfoPhase + (c == 1 ? offset : 0.0);
            const double next = std::clamp (lo + sweep * 0.5 * (1.0 - std::cos (p)), 3.0, (double) lines[0].capacity() - 4.0);
            delayStep[(size_t) c] = (float) ((next - currentDelay[(size_t) c]) / controlRate);
        }
    };

    for (int i = 0; i < block.numSamples; ++i)
    {
        if (controlCounter == 0)
            control();

        controlCounter = (controlCounter + 1) % controlRate;

        const float g = fb.next(), m = wet.next();
        const float fbComp = std::sqrt (1.0f - std::abs (g));   // picos de 1/(1-g) viram 1/raiz(1-g)

        for (int c = 0; c < 2; ++c)
        {
            const auto uc = (size_t) c;
            const float x = io[c][i];
            currentDelay[uc] += delayStep[uc];
            // Lê antes de empurrar (a realimentação precisa da cópia): um a menos, para o atraso ser o pedido.
            const float tap = lines[uc].readHermite (currentDelay[uc] - 1.0f);
            lines[uc].push (x + g * tap);
            io[c][i] = x + m * (tap * fbComp - x);
        }
    }
}

//==============================================================================
// Phaser

namespace
{
    const char* const phaserStages[] = { "4", "6", "8", "12" };
    constexpr int stageCounts[] = { 4, 6, 8, 12 };

    const ParamInfo phaserParams[] = {
        { "stages",   "Estágios", 0, 3, 1, "", ParamKind::choice, false, phaserStages,
          "Quantos filtros em fila: 4 é suave e vintage; 12 tem mais vales e é mais intenso." },
        { "rate",     "Velocidade", 0.02f, 5, 0.4f, "Hz", ParamKind::continuous, true, {},
          "Quantas vezes por segundo os vales passeiam pelo som." },
        { "depth",    "Profundidade", 0, 100, 70, "%", ParamKind::continuous, false, {},
          "Quanto os vales andam: 100% vai duas oitavas para cada lado do centro." },
        { "centre",   "Centro", 150, 4000, 800, "Hz", ParamKind::continuous, true, {},
          "Onde os vales passeiam. Grave encorpa; agudo deixa o efeito mais aéreo." },
        { "feedback", "Realimentação", -90, 90, 30, "%", ParamKind::continuous, false, {},
          "Picos entre os vales: mais alto fica mais ressonante e \"molhado\"." },
        { "stereo",   "Estéreo", 0, 180, 90, "°", ParamKind::continuous, false, {},
          "Defasagem entre os lados. 0° = juntos; 180° = um sobe enquanto o outro desce." },
        { "mix",      "Mistura", 0, 100, 50, "%", ParamKind::continuous, false, {},
          "50% dá os vales mais fundos." },
    };

    static_assert (std::size (phaserParams) == PhaserProcessor::count);
}

const FxInfo& PhaserProcessor::staticInfo()
{
    static const FxInfo info { "phaser", "Phaser", "Phaser", "Modulação",
                               "Uma fila de filtros que mexem só na fase; somados ao seco, abrem vales que passeiam pelo som. "
                               "Mais suave que o flanger: clássico em Rhodes, synth e guitarra.",
                               phaserParams };
    return info;
}

float PhaserProcessor::allpassCoeff (double freq, double rate) noexcept
{
    const double t = std::tan (pi * std::clamp (freq, 10.0, rate * 0.45) / rate);
    return (float) ((t - 1.0) / (t + 1.0));
}

void PhaserProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = phaserParams[i].def;

    for (auto* s : { &logCentre, &octaves, &fb, &phaseOffset, &wet })
        s->prepare (rate, 40.0f);

    stageFade.prepare (rate, 5.0f);   // ~15 ms até a troca terminar
    dirty = true;
    snapToTargets();
    reset();
}

void PhaserProcessor::snapToTargets() noexcept
{
    update();

    for (auto* s : { &logCentre, &octaves, &fb, &phaseOffset, &wet })
        s->snap (s->getTarget());

    previousStages = activeStages;
    stageFade.snap (1.0f);
}

double PhaserProcessor::tailSecondsFor (const float* v) const noexcept
{
    // O atraso de grupo de um estágio na frequência mais grave do passeio é (1 - a)/(1 + a) amostras; vezes os
    // estágios dá uma volta da realimentação. A 48 kHz, 12 estágios em 37,5 Hz e 90%: perto de 1,6 s.
    const double rateRef = 48000.0;
    const double lowest = v[centre] * std::exp2 (-2.0 * v[depth] * 0.01);
    const double a = allpassCoeff (lowest, rateRef);
    const double loop = stageCounts[std::clamp ((int) v[stages], 0, 3)] * (1.0 - a) / (1.0 + a) / rateRef;
    return feedbackTail (v[feedback] * 0.01, loop) + 0.05;
}

void PhaserProcessor::reset() noexcept
{
    for (auto& ch : state)
        ch.fill (0.0f);

    lfoPhase = 0.0;
    coeff.fill (allpassCoeff (std::exp2 (logCentre.getCurrent()), sampleRate));
    coeffStep.fill (0.0f);
    lastOut.fill (0.0f);
    controlCounter = 0;
}

void PhaserProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void PhaserProcessor::update() noexcept
{
    dirty = false;
    const int newStages = stageCounts[std::clamp ((int) values[stages], 0, 3)];

    if (newStages != activeStages)
    {
        // Os 12 estágios rodam sempre: a saída sai de uma contagem para a outra num crossfade.
        previousStages = activeStages;
        activeStages = newStages;
        stageFade.snap (0.0f);
        stageFade.setTarget (1.0f);
    }
    logCentre.setTarget (std::log2 (values[centre]));
    octaves.setTarget (values[depth] * 0.01f * 2.0f);
    fb.setTarget (std::clamp (values[feedback] * 0.01f, -0.9f, 0.9f));
    phaseOffset.setTarget ((float) (values[stereo] * pi / 180.0));
    wet.setTarget (values[mix] * 0.01f);
}

void PhaserProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    float* const io[2] = { block.left, block.right };

    auto control = [&]
    {
        lfoPhase = wrapPhase (lfoPhase + 2.0 * pi * values[rate] * controlRate / sampleRate);
        const double centreOct = logCentre.skip (controlRate);
        const double range = octaves.skip (controlRate);
        const double offset = phaseOffset.skip (controlRate);

        for (int c = 0; c < 2; ++c)
        {
            const double freq = std::exp2 (centreOct + range * std::sin (lfoPhase + (c == 1 ? offset : 0.0)));
            coeffStep[(size_t) c] = (allpassCoeff (freq, sampleRate) - coeff[(size_t) c]) / controlRate;
        }
    };

    for (int i = 0; i < block.numSamples; ++i)
    {
        if (controlCounter == 0)
            control();

        controlCounter = (controlCounter + 1) % controlRate;

        const float g = fb.next(), m = wet.next(), fade = stageFade.next();
        const float fbComp = std::sqrt (1.0f - std::abs (g));

        if (fade >= 1.0f)
            previousStages = activeStages;

        for (int c = 0; c < 2; ++c)
        {
            const auto uc = (size_t) c;
            coeff[uc] += coeffStep[uc];
            const float a = coeff[uc];
            const float x = io[c][i];
            float y = x + g * lastOut[uc];
            auto& s = state[uc];

            // Passa-tudo de 1ª ordem, forma transposta: y = a·x + s; s = x - a·y.
            float yNew = y, yOld = y;

            for (int k = 0; k < maxStages; ++k)
            {
                const float out = a * y + s[(size_t) k];
                s[(size_t) k] = y - a * out;
                y = out;

                if (k + 1 == activeStages)   yNew = y;
                if (k + 1 == previousStages) yOld = y;
            }

            y = yOld + fade * (yNew - yOld);
            lastOut[uc] = y;
            io[c][i] = x + m * (y * fbComp - x);
        }
    }
}

} // namespace whynot::dsp
