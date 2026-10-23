#include "Tune.h"

#include <cmath>
#include <iterator>

namespace whynot::dsp
{

namespace
{
    const char* const keyNames[] = { "Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si" };
    const char* const scaleNames[] = { "Menor", "Maior", "Menor harmônica", "Frígio", "Dórico", "Pentatônica menor", "Cromática" };
    const char* const rangeNames[] = { "Ampla", "Grave", "Aguda" };

    const ParamInfo tuneParams[] = {
        { "key",    "Tom",        0, 11, 9,    "",   ParamKind::choice,     false, keyNames,
          "Tônica da música. Ao entrar no mixer, o efeito pega o tom do projeto." },
        { "scale",  "Escala",     0, 6, 0,     "",   ParamKind::choice,     false, scaleNames,
          "As notas permitidas. Cromática corrige para o semitom mais perto." },
        { "speed",  "Velocidade", 0, 400, 25,  "ms", ParamKind::continuous, false, {},
          "Quanto tempo a correção leva. 0 ms = salto na hora (o efeito robô); 50 a 150 ms soa natural." },
        { "amount", "Força",      0, 100, 100, "%",  ParamKind::continuous, false, {},
          "Quanto da distância até a nota certa é corrigido." },
        { "range",  "Voz",        0, 2, 0,     "",   ParamKind::choice,     false, rangeNames,
          "Faixa de tom procurada: grave 60-500 Hz, aguda 120-1000 Hz, ampla 60-1000 Hz." },
        { "mix",    "Mistura",    0, 100, 100, "%",  ParamKind::continuous, false, {},
          "Quanto da voz corrigida entra; o resto é a voz original, atrasada junto. Abaixo de 100%, enquanto corrige, "
          "as duas somam com um atraso que varia uns milissegundos: soa como uma dobra." },
        { "humanize", "Humanizar", 0, 100, 0, "%", ParamKind::continuous, false, {},
          "Nas notas longas a correção fica mais lenta e deixa o vibrato passar; a troca de nota continua rápida. "
          "0% com velocidade 0 = o efeito duro do trap." },
        { "follow",   "Seguir o tom do projeto", 0, 1, 1, "", ParamKind::toggle, false, {},
          "Ligado: quando o tom ou a escala do projeto mudam, a afinação muda junto. Escolher o tom aqui desliga." },
    };

    static_assert (std::size (tuneParams) == TuneProcessor::count);

    int pitchClassOf (int key) noexcept   { return ((key % 12) + 12) % 12; }
}

const FxInfo& TuneProcessor::staticInfo()
{
    static const FxInfo info { "tune", "Afinação", "Afin.", "Voz",
                               "Corrige a afinação da voz para as notas da escala, do natural ao efeito robô.",
                               tuneParams };
    return info;
}

int TuneProcessor::scaleMask (int scaleIndex) noexcept
{
    static const int degrees[7][8] = {
        { 0, 2, 3, 5, 7, 8, 10, -1 },   // menor
        { 0, 2, 4, 5, 7, 9, 11, -1 },   // maior
        { 0, 2, 3, 5, 7, 8, 11, -1 },   // menor harmônica
        { 0, 1, 3, 5, 7, 8, 10, -1 },   // frígio
        { 0, 2, 3, 5, 7, 9, 10, -1 },   // dórico
        { 0, 3, 5, 7, 10, -1, -1, -1 }, // pentatônica menor
        { -1, -1, -1, -1, -1, -1, -1, -1 },
    };

    if (scaleIndex < 0 || scaleIndex >= 6)
        return 0xFFF;   // cromática

    int mask = 0;

    for (int d : degrees[scaleIndex])
        if (d >= 0)
            mask |= 1 << d;

    return mask;
}

int TuneProcessor::nearestInScale (float midi, int keyRoot, int mask) noexcept
{
    const int centre = (int) std::lround (midi);
    int best = centre;
    float bestDist = 1.0e9f;

    for (int k = centre - 6; k <= centre + 6; ++k)
    {
        if (((mask >> pitchClassOf (k - keyRoot)) & 1) == 0)
            continue;

        const float d = std::abs (midi - (float) k);

        if (d < bestDist)
        {
            bestDist = d;
            best = k;
        }
    }

    return best;
}

//==============================================================================
void TuneProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = tuneParams[i].def;

    // Detecção a ~12 kHz: o YIN fica barato e a voz cabe inteira (até 1 kHz de fundamental).
    decimation = std::max (1, (int) std::lround (rate / 12000.0));
    detectRate = rate / decimation;
    antiAlias = SvfCoeffs::make (FilterType::lowpass, std::min (4000.0, detectRate * 0.38), 0.70710678, 0.0, rate);
    tauMaxAll = (int) std::ceil (detectRate / 60.0);
    window = 2 * tauMaxAll;
    hop = std::max (16, (int) std::lround (detectRate * 0.005));
    ring.assign ((size_t) (window + tauMaxAll + 1), 0.0f);
    frame.assign (ring.size(), 0.0f);
    diff.assign ((size_t) tauMaxAll + 2, 1.0f);
    rawDiff.assign ((size_t) tauMaxAll + 2, 0.0f);

    // Limites das cabeças de leitura: cabem um salto de até um período a 60 Hz mais a janela.
    maxPeriod = (float) (rate / 60.0);
    minDelay = std::ceil (maxPeriod * 0.2f) + 16.0f;
    maxDelay = minDelay + maxPeriod + std::ceil ((float) rate * 0.006f);
    latency = (int) std::lround ((minDelay + maxDelay) * 0.5f);

    const int capacity = (int) maxDelay + (int) maxPeriod + 64;
    lineL.prepare (capacity);
    lineR.prepare (capacity);

    releaseCoeff = onePoleCoeff (60.0f, rate);
    wet.prepare (rate, 10.0f);
    snapToTargets();
    reset();
}

void TuneProcessor::reset() noexcept
{
    lineL.reset();
    lineR.reset();
    aa1.reset();
    aa2.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = ringFilled = hopCounter = decimCounter = 0;
    lastTarget = -1;
    detectedMidi = targetMidi = -1.0f;
    voiced = false;
    periodSamples = 0.0f;
    correctionTarget = correction = 0.0f;
    currentAttack = attackCoeff;
    heldSamples = 0;
    delayA = delayB = (float) latency;
    fading = false;
    fadePos = fadeLen = 0;

    setMeter (detectedNote, -1.0f);
    setMeter (targetNote, -1.0f);
    setMeter (correctionCents, 0.0f);
    setMeter (voicedMeter, 0.0f);
}

void TuneProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;

    if (index == speed)
        currentAttack = attackCoeff = value < 0.5f ? 0.0f : onePoleCoeff (value, sampleRate);
    else if (index == mix)
        wet.setTarget (value * 0.01f);
}

void TuneProcessor::snapToTargets() noexcept
{
    attackCoeff = values[speed] < 0.5f ? 0.0f : onePoleCoeff (values[speed], sampleRate);
    currentAttack = attackCoeff;
    wet.snap (values[mix] * 0.01f);
}

//==============================================================================
float TuneProcessor::detect (float& clarity) noexcept
{
    clarity = 0.0f;
    const float* x = frame.data();
    const int total = (int) frame.size();
    double energy = 0.0;

    for (int j = 0; j < total; ++j)
        energy += (double) x[j] * x[j];

    // Porta: abaixo de ~-50 dBFS não há voz para afinar.
    if (std::sqrt (energy / total) < 0.003)
        return 0.0f;

    const int rangeIndex = (int) values[range];
    const double fLow = rangeIndex == 2 ? 120.0 : 60.0;
    const double fHigh = rangeIndex == 1 ? 500.0 : 1000.0;
    const int tauMin = std::max (2, (int) std::floor (detectRate / fHigh));
    const int tauMax = std::min (tauMaxAll, (int) std::ceil (detectRate / fLow));

    // YIN: diferença ao quadrado e a média acumulada normalizada. Cada atraso usa as amostras mais
    // novas do quadro (as últimas window + tau): a nota medida é a de ~18 ms atrás em 440 Hz, quase o
    // atraso da leitura (15 ms). Com as mais velhas, a medida chegava 14 ms mais tarde e o modo duro
    // deixava passar metade do vibrato.
    double running = 0.0;
    diff[0] = 1.0f;

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        const float* y = x + (total - window - tau);

        for (int j = 0; j < window; ++j)
        {
            const float e = y[j] - y[j + tau];
            d += (double) e * e;
        }

        running += d;
        rawDiff[(size_t) tau] = (float) d;
        diff[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
    }

    int found = -1;

    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (diff[(size_t) tau] < 0.15f)
        {
            while (tau + 1 <= tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau])
                ++tau;

            found = tau;
            break;
        }
    }

    if (found < 0)
    {
        // Sem mínimo abaixo do limiar: o menor da faixa, se ainda for claro o bastante.
        float best = 1.0e9f;

        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (diff[(size_t) tau] < best)
            {
                best = diff[(size_t) tau];
                found = tau;
            }

        if (best >= 0.3f)
            return 0.0f;
    }

    float t = (float) found;

    if (found > tauMin && found < tauMax)
    {
        // Na diferença crua, não na normalizada: a normalizada puxa o mínimo para os lados
        // (dava ~2 cents de erro a 440 Hz na taxa de 12 kHz).
        const float a = rawDiff[(size_t) found - 1], b = rawDiff[(size_t) found], c = rawDiff[(size_t) found + 1];
        const float den = a - 2.0f * b + c;

        // Vale achatado (den perto de zero ou do lado errado): o ajuste fica dentro de um passo.
        if (std::abs (den) > 1.0e-9f)
            t += std::clamp (0.5f * (a - c) / den, -1.0f, 1.0f);
    }

    clarity = 1.0f - diff[(size_t) found];
    return (float) (detectRate / std::max (1.0f, t));
}

void TuneProcessor::analyse() noexcept
{
    const int n = (int) ring.size();

    if (ringFilled < n)
        return;

    for (int k = 0; k < n; ++k)
        frame[(size_t) k] = ring[(size_t) ((ringPos + k) % n)];

    float clarity = 0.0f;
    const float hz = detect (clarity);

    if (hz <= 0.0f)
    {
        voiced = false;
        detectedMidi = targetMidi = -1.0f;
        correctionTarget = 0.0f;
        heldSamples = 0;
        return;
    }

    const int keyRoot = (int) values[key];
    const int mask = scaleMask ((int) values[scale]);
    voiced = true;
    detectedMidi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    periodSamples = (float) (sampleRate / hz);

    int target = nearestInScale (detectedMidi, keyRoot, mask);

    // Histerese: fica na nota anterior enquanto a voz está a menos de 0,65 semitom dela.
    if (lastTarget >= 0 && target != lastTarget && ((mask >> pitchClassOf (lastTarget - keyRoot)) & 1) != 0
        && std::abs (detectedMidi - (float) lastTarget) < 0.65f)
        target = lastTarget;

    // Humanizar: quanto mais a nota se sustenta, mais lenta a correção dentro dela.
    heldSamples = target == lastTarget ? heldSamples + hop * decimation : 0;
    float ms = values[speed];
    const float human = values[humanize] * 0.01f;

    if (human > 0.0f)
    {
        const float held = (float) heldSamples / (float) sampleRate;
        ms += human * 350.0f * std::clamp ((held - 0.12f) / 0.2f, 0.0f, 1.0f);
    }

    currentAttack = ms < 0.5f ? 0.0f : onePoleCoeff (ms, sampleRate);

    lastTarget = target;
    targetMidi = (float) target;
    const float wanted = std::clamp ((targetMidi - detectedMidi) * values[amount] * 0.01f, -3.0f, 3.0f);

    // Menos de 2 cents é inaudível: não corrige (a cabeça fica parada e volta ao centro, alinhada com o seco).
    correctionTarget = std::abs (wanted) < 0.02f ? 0.0f : wanted;
}

void TuneProcessor::startSplice (float newDelay, int length) noexcept
{
    delayB = std::clamp (newDelay, 2.0f, (float) lineL.capacity() - 4.0f);
    fadeLen = std::max (16, length);
    fadePos = 0;
    fading = true;
}

void TuneProcessor::process (AudioBlock& block) noexcept
{
    DenormalGuard guard;
    const int fadeMin = std::max (32, (int) (0.0027 * sampleRate));
    const int ringSize = (int) ring.size();

    for (int i = 0; i < block.numSamples; ++i)
    {
        // Um NaN ou infinito na entrada vira silêncio antes de entrar nas linhas de atraso e no detector.
        float l = block.left[i], r = block.right[i];

        if (! std::isfinite (l)) l = 0.0f;
        if (! std::isfinite (r)) r = 0.0f;

        lineL.push (l);
        lineR.push (r);

        // Detecção na taxa reduzida, a cada ~5 ms.
        const float m = aa2.process (aa1.process (0.5f * (l + r), antiAlias), antiAlias);

        if (++decimCounter >= decimation)
        {
            decimCounter = 0;
            ring[(size_t) ringPos] = m;
            ringPos = (ringPos + 1) % ringSize;
            ringFilled = std::min (ringFilled + 1, ringSize);

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                analyse();
            }
        }

        // Correção em semitons: a velocidade vale ao corrigir; sem voz, volta a zero em ~60 ms.
        const float coeff = voiced ? currentAttack : releaseCoeff;
        correction = correctionTarget + (correction - correctionTarget) * coeff;

        if (std::abs (correction - correctionTarget) < 1.0e-5f)
            correction = correctionTarget;

        const float ratio = std::exp2 (correction * (1.0f / 12.0f));
        const float drift = 1.0f - ratio;   // quanto o atraso muda por amostra
        delayA += drift;

        if (fading)
            delayB += drift;

        if (! fading)
        {
            const float period = voiced ? periodSamples : 0.0f;
            const int fade = period > 0.0f ? std::clamp ((int) period, fadeMin, (int) maxPeriod) : (int) (0.01 * sampleRate);

            if (delayA < minDelay || delayA > maxDelay)
            {
                // Salto de um múltiplo do período: a emenda cai na mesma fase da onda.
                const float jump = period > 0.0f ? period * std::ceil ((float) fade / period)
                                                 : (maxDelay - minDelay) * 0.5f;
                startSplice (delayA < minDelay ? delayA + jump : delayA - jump, fade);
            }
            else if (correction == 0.0f && std::abs (delayA - (float) latency) > 0.5f)
            {
                // Sem correção: a cabeça volta ao atraso nominal, alinhada com o sinal seco.
                if (period > 0.0f)
                {
                    const float k = std::round (((float) latency - delayA) / period);

                    if (k != 0.0f)
                        startSplice (delayA + k * period, fade);
                    else if (wet.getTarget() < 0.999f)
                        startSplice ((float) latency, fade);   // com Mistura < 100%: alinha com o seco (sem pente fixo)
                }
                else
                {
                    startSplice ((float) latency, fade);
                }
            }
        }

        float wl, wr;

        if (fading)
        {
            const float w = 0.5f - 0.5f * std::cos ((float) pi * (float) fadePos / (float) fadeLen);
            const float al = lineL.readHermite (delayA), bl = lineL.readHermite (delayB);
            const float ar = lineR.readHermite (delayA), br = lineR.readHermite (delayB);
            wl = al + w * (bl - al);
            wr = ar + w * (br - ar);

            if (++fadePos >= fadeLen)
            {
                fading = false;
                delayA = delayB;
            }
        }
        else
        {
            wl = lineL.readHermite (delayA);
            wr = lineR.readHermite (delayA);
        }

        const float dl = lineL.read (latency), dr = lineR.read (latency);
        const float w = wet.next();
        block.left[i] = dl + w * (wl - dl);
        block.right[i] = dr + w * (wr - dr);
    }

    setMeter (detectedNote, detectedMidi);
    setMeter (targetNote, targetMidi);
    setMeter (correctionCents, correction * 100.0f);
    setMeter (voicedMeter, voiced ? 1.0f : 0.0f);
}

} // namespace whynot::dsp
