#include "VoicePitch.h"
#include "Tune.h"

namespace whynot::dsp
{

//==============================================================================
// YIN

void YinTracker::prepare (double sr)
{
    rate = sr;
    decimation = std::max (1, (int) std::lround (sr / 12000.0));
    detectRate = sr / decimation;
    antiAlias = SvfCoeffs::make (FilterType::lowpass, std::min (4000.0, detectRate * 0.38), 0.70710678, 0.0, sr);
    tauMaxAll = (int) std::ceil (detectRate / 60.0);
    window = 2 * tauMaxAll;
    hop = std::max (16, (int) std::lround (detectRate * 0.005));
    ring.assign ((size_t) (window + tauMaxAll + 1), 0.0f);
    frame.assign (ring.size(), 0.0f);
    diff.assign ((size_t) tauMaxAll + 2, 1.0f);
    rawDiff.assign ((size_t) tauMaxAll + 2, 0.0f);
    reset();
}

void YinTracker::reset() noexcept
{
    aa1.reset();
    aa2.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = ringFilled = hopCounter = decimCounter = 0;
    period = 0.0f;
    isVoiced = false;
}

bool YinTracker::push (float x) noexcept
{
    const float m = aa2.process (aa1.process (x, antiAlias), antiAlias);

    if (++decimCounter < decimation)
        return false;

    decimCounter = 0;
    const int n = (int) ring.size();
    ring[(size_t) ringPos] = m;
    ringPos = (ringPos + 1) % n;
    ringFilled = std::min (ringFilled + 1, n);

    if (++hopCounter < hop)
        return false;

    hopCounter = 0;
    return analyse();
}

bool YinTracker::analyse() noexcept
{
    const int total = (int) ring.size();

    if (ringFilled < total)
        return false;

    for (int k = 0; k < total; ++k)
        frame[(size_t) k] = ring[(size_t) ((ringPos + k) % total)];

    const float* x = frame.data();
    double energy = 0.0;

    for (int j = 0; j < total; ++j)
        energy += (double) x[j] * x[j];

    isVoiced = false;

    // Abaixo de ~-50 dBFS não há voz.
    if (std::sqrt (energy / total) < 0.003)
        return true;

    const int tauMin = std::max (2, (int) std::floor (detectRate / fHigh));
    const int tauMax = std::min (tauMaxAll, (int) std::ceil (detectRate / fLow));
    double running = 0.0;
    diff[0] = 1.0f;

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        const float* y = x + (total - window - tau);   // as amostras mais novas

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
        float best = 1.0e9f;

        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (diff[(size_t) tau] < best)
            {
                best = diff[(size_t) tau];
                found = tau;
            }

        if (found < 0 || best >= 0.3f)
            return true;
    }

    float t = (float) found;

    if (found > tauMin && found < tauMax)
    {
        const float a = rawDiff[(size_t) found - 1], b = rawDiff[(size_t) found], c = rawDiff[(size_t) found + 1];
        const float den = a - 2.0f * b + c;

        if (std::abs (den) > 1.0e-9f)
            t += std::clamp (0.5f * (a - c) / den, -1.0f, 1.0f);
    }

    period = std::max (2.0f, t) * (float) decimation;
    isVoiced = true;
    return true;
}

//==============================================================================
// PSOLA

namespace
{
    constexpr float gainExponent = 0.5f;   // pulsos da voz: a potência acompanha o número de períodos por segundo
}

void PsolaCore::prepare (double rate, int readCount, float maxGrainPeriods)
{
    sampleRate = rate;
    numReads = std::clamp (readCount, 1, maxReads);
    maxPeriod = (float) (rate / 60.0);
    unvoicedPeriod = (float) std::round (0.005 * rate);
    maxGrainPeriods = std::clamp (maxGrainPeriods, 1.5f, 2.0f);
    maxHalfOut = maxGrainPeriods * maxPeriod;

    // O grão de saída tem até 1,5 período de meia largura (formante -7; 2 períodos com a formante -12); ele é
    // feito 1,5 período depois da marca e lido meia largura antes dela: três períodos de 60 Hz (ou 3,5) e uma folga.
    latencySamples = (int) std::ceil ((1.5f + maxGrainPeriods) * maxPeriod) + 8;

    std::int64_t size = 4;

    while (size < (std::int64_t) latencySamples + (std::int64_t) (4.0f * maxPeriod) + 64)
        size *= 2;

    mask = size - 1;

    for (auto* v : { &inL, &inR, &lowRing })
        v->assign ((size_t) size, 0.0f);

    for (int k = 0; k < maxReads; ++k)
    {
        reads[(size_t) k].accL.assign (k < numReads ? (size_t) size : 0, 0.0f);
        reads[(size_t) k].accR.assign (k < numReads ? (size_t) size : 0, 0.0f);
    }

    lowCoeffs = SvfCoeffs::make (FilterType::lowpass, 1000.0, 0.7071, 0.0, rate);
    yin.prepare (rate);
    reset();
}

void PsolaCore::reset() noexcept
{
    for (auto* v : { &inL, &inR, &lowRing })
        std::fill (v->begin(), v->end(), 0.0f);

    for (auto& r : reads)
    {
        std::fill (r.accL.begin(), r.accL.end(), 0.0f);
        std::fill (r.accR.begin(), r.accR.end(), 0.0f);
        r.nextSynthesis = 0.0;
        r.lastGrain = -1.0e30;
    }

    lowFilter.reset();
    yin.reset();
    now = -1;
    markCount = 1;
    markHead = 1;
    marks[0] = { 0.0, unvoicedPeriod, false };
    lastMark = 0.0;
}

void PsolaCore::setShift (int read, float ratio, float formantRatio, bool voicedOnly) noexcept
{
    if (read < 0 || read >= numReads)
        return;

    auto& r = reads[(size_t) read];
    r.ratio = std::clamp (ratio, 0.25f, 4.0f);
    r.formant = std::clamp (formantRatio, 0.5f, 2.0f);
    r.voicedOnly = voicedOnly;
}

void PsolaCore::setEnabled (int read, bool enabled) noexcept
{
    if (read < 0 || read >= numReads || reads[(size_t) read].enabled == enabled)
        return;

    auto& r = reads[(size_t) read];
    r.enabled = enabled;

    if (enabled)
    {
        // Recomeça no grão que acabou de ficar pronto: o que ficou para trás já saiu em silêncio.
        r.nextSynthesis = std::max (r.nextSynthesis, (double) now - (1.5 * maxPeriod + 2.0));
        r.lastGrain = -1.0e30;
    }
}

const PsolaCore::Mark* PsolaCore::nearestMark (double pos) const noexcept
{
    const Mark* best = nullptr;
    double bestDistance = 1.0e30;

    for (int k = 0; k < markCount; ++k)
    {
        const auto& m = marks[(size_t) ((markHead - 1 - k + maxMarks) % maxMarks)];
        const double d = std::abs (m.pos - pos);

        if (d < bestDistance)
        {
            bestDistance = d;
            best = &m;
        }

        if (m.pos < pos - 2.0 * maxPeriod)
            break;   // as marcas estão em ordem: daqui para trás só piora
    }

    return best;
}

void PsolaCore::placeMarks() noexcept
{
    const bool voiced = yin.voiced();
    const float period = voiced ? std::clamp (yin.periodSamples(), 16.0f, maxPeriod) : unvoicedPeriod;

    for (;;)
    {
        const double candidate = lastMark + period;
        const double quarter = voiced ? period * 0.25 : 0.0;

        if (candidate + quarter + 1.0 > (double) now)
            break;

        double pos = candidate;

        if (voiced)
        {
            // O começo do período: o pico do sinal filtrado perto de onde ele devia estar.
            const auto from = (std::int64_t) std::ceil (candidate - quarter), to = (std::int64_t) std::floor (candidate + quarter);
            auto at = (std::int64_t) std::llround (candidate);
            float best = -1.0e30f;

            for (auto k = from; k <= to; ++k)
                if (const float v = lowRing[(size_t) (k & mask)]; v > best)
                {
                    best = v;
                    at = k;
                }

            pos = std::max ((double) at, lastMark + 0.5 * period);
        }

        // O período de cada marca: a distância real até a anterior (média das duas últimas), não a medida do YIN,
        // que chega uns 18 ms atrasada; num vibrato rápido o espaçamento dos grãos seguia o tom de antes.
        float markPeriod = period;
        const auto& previous = marks[(size_t) ((markHead - 1 + maxMarks) % maxMarks)];

        if (voiced && previous.voiced)
        {
            const float local = std::clamp ((float) (pos - lastMark), 0.6f * period, 1.4f * period);
            markPeriod = 0.5f * (local + std::clamp (previous.period, 0.6f * period, 1.4f * period));
        }

        marks[(size_t) markHead] = { pos, markPeriod, voiced };
        markHead = (markHead + 1) % maxMarks;
        markCount = std::min (markCount + 1, maxMarks);
        lastMark = pos;
    }
}

void PsolaCore::synthesize (Read& read) noexcept
{
    const double ready = 1.5 * maxPeriod + 2.0;   // o grão mais longo já tem todas as amostras de entrada

    while (read.nextSynthesis + ready <= (double) now)
    {
        double s = read.nextSynthesis;
        const Mark* a = nearestMark (s);

        if (a == nullptr)
        {
            read.nextSynthesis += unvoicedPeriod;
            continue;
        }

        // Sem mudança nenhuma, o grão cai em cima da marca: a leitura fica em fase com a entrada (a afinação com
        // Mistura abaixo de 100% não soa como um pente quando a voz já está afinada).
        // Só quando o grão inteiro ainda cabe antes do ponto de leitura (a marca pode estar até meio período atrás).
        if (read.ratio == 1.0f && read.formant == 1.0f && a->voiced && a->pos > read.lastGrain + 0.5 * a->period
            && a->pos - std::min ((double) a->period, (double) maxHalfOut) > (double) (now - latencySamples) + 1.0)
            s = a->pos;

        read.lastGrain = s;

        const double r = a->voiced ? read.ratio : 1.0;
        const double f = a->voiced ? read.formant : 1.0;
        const double spacing = a->period / r;
        read.nextSynthesis = s + spacing;

        if (read.voicedOnly && ! a->voiced)
            continue;   // a voz da harmonia não repete as consoantes

        // Potência por período constante: mais períodos por segundo (tom acima) = cada grão um pouco mais baixo;
        // menos (tom abaixo, grãos com folga entre si) = um pouco mais alto. A voz sai no mesmo volume.
        const double halfOut = std::min ((double) a->period / f, (double) maxHalfOut);   // nunca além da latência
        const float gain = (float) std::pow (spacing / halfOut, (double) gainExponent);
        const auto from = (std::int64_t) std::ceil (s - halfOut), to = (std::int64_t) std::floor (s + halfOut);

        for (auto j = from; j <= to; ++j)
        {
            const double rel = (double) j - s;
            const float w = gain * (float) (0.5 + 0.5 * std::cos (pi * rel / halfOut));
            const double src = a->pos + rel * f;
            const auto i = (std::int64_t) std::floor (src);
            const float frac = (float) (src - (double) i);
            const auto i0 = (size_t) (i & mask), i1 = (size_t) ((i + 1) & mask), o = (size_t) (j & mask);
            read.accL[o] += w * (inL[i0] + frac * (inL[i1] - inL[i0]));
            read.accR[o] += w * (inR[i0] + frac * (inR[i1] - inR[i0]));
        }
    }
}

void PsolaCore::process (float l, float r, float* wetL, float* wetR, float& dryL, float& dryR) noexcept
{
    ++now;
    const auto in = (size_t) (now & mask);
    inL[in] = l;
    inR[in] = r;
    lowRing[in] = lowFilter.process (0.5f * (l + r), lowCoeffs);
    yin.push (0.5f * (l + r));
    placeMarks();

    const auto out = (size_t) ((now - latencySamples) & mask);
    const bool started = now >= latencySamples;
    dryL = started ? inL[out] : 0.0f;
    dryR = started ? inR[out] : 0.0f;

    for (int k = 0; k < numReads; ++k)
    {
        auto& read = reads[(size_t) k];

        if (read.enabled)
            synthesize (read);

        wetL[k] = read.accL[out];
        wetR[k] = read.accR[out];
        read.accL[out] = read.accR[out] = 0.0f;
    }
}

//==============================================================================
// Tom da voz

namespace
{
    const char* const rangeNames[] = { "Ampla", "Grave", "Aguda" };

    const ParamInfo pitchParams[] = {
        { "semitones", "Tom",      -12, 12, 0,  "st", ParamKind::continuous, false, {},
          "Quantos semitons a voz sobe ou desce. -12 = uma oitava abaixo." },
        { "formant",   "Formante", -7, 7, 0,    "st", ParamKind::continuous, false, {},
          "O tamanho da voz, sem mudar a nota: negativo = voz maior e mais grossa; positivo = menor e mais fina." },
        { "range",     "Voz",      0, 2, 0,     "",   ParamKind::choice,     false, rangeNames,
          "Faixa de tom procurada: grave 60-500 Hz, aguda 120-1000 Hz, ampla 60-1000 Hz." },
        { "mix",       "Mistura",  0, 100, 100, "%",  ParamKind::continuous, false, {},
          "Abaixo de 100% a voz original fica junto: dobra uma oitava abaixo, por exemplo." },
    };

    static_assert (std::size (pitchParams) == VoicePitchProcessor::count);
}

const FxInfo& VoicePitchProcessor::staticInfo()
{
    static const FxInfo info { "voicepitch", "Tom da voz", "Tom", "Voz",
                               "Sobe ou desce a voz em semitons e muda o tamanho dela (formante) à parte: ad-lib grosso, fino ou "
                               "uma oitava abaixo, sem soar como fita acelerada.",
                               pitchParams };
    return info;
}

void VoicePitchProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = pitchParams[i].def;

    core.prepare (rate, 1);
    ratio.prepare (rate, 30.0f);
    formantFactor.prepare (rate, 30.0f);
    wet.prepare (rate, 20.0f);
    snapToTargets();
    reset();
}

void VoicePitchProcessor::snapToTargets() noexcept
{
    for (int i = 0; i < count; ++i)
        setParam (i, values[(size_t) i]);

    ratio.snap (ratio.getTarget());
    formantFactor.snap (formantFactor.getTarget());
    wet.snap (wet.getTarget());
}

void VoicePitchProcessor::reset() noexcept
{
    core.reset();
    setMeter (detectedHz, 0.0f);
    setMeter (voicedMeter, 0.0f);
}

void VoicePitchProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;

    if (index == range)
    {
        const int r = (int) value;
        core.setRange (r == 2 ? 120.0 : 60.0, r == 1 ? 500.0 : 1000.0);
    }

    ratio.setTarget (std::exp2 (values[semitones] / 12.0f));
    formantFactor.setTarget (std::exp2 (values[formant] / 12.0f));

    // Sem mudança de tom nem de formante, a saída é a entrada atrasada, exata (o PSOLA continua rodando por baixo).
    const bool shifting = std::abs (values[semitones]) > 0.005f || std::abs (values[formant]) > 0.005f;
    wet.setTarget (shifting ? values[mix] * 0.01f : 0.0f);
}

void VoicePitchProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;
    core.setShift (0, ratio.skip (block.numSamples), formantFactor.skip (block.numSamples), false);

    for (int i = 0; i < block.numSamples; ++i)
    {
        float wl = 0.0f, wr = 0.0f, dl = 0.0f, dr = 0.0f;
        core.process (block.left[i], block.right[i], &wl, &wr, dl, dr);
        const float w = wet.next();
        block.left[i] = dl + w * (wl - dl);
        block.right[i] = dr + w * (wr - dr);
    }

    setMeter (detectedHz, core.voiced() && core.periodSamples() > 0.0f ? (float) (sampleRate / core.periodSamples()) : 0.0f);
    setMeter (voicedMeter, core.voiced() ? 1.0f : 0.0f);
}

//==============================================================================
// Harmonia

namespace
{
    const char* const keyNames[] = { "Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si" };
    const char* const scaleNames[] = { "Menor", "Maior", "Menor harmônica", "Frígio", "Dórico", "Pentatônica menor", "Cromática" };
    const char* const voiceNames[] = { "Terça", "Quinta", "3ª + 5ª", "3ª baixo", "Oitava", "3ª + 8ª" };

    const ParamInfo harmonyParams[] = {
        { "key",     "Tom",     0, 11, 9,  "", ParamKind::choice, false, keyNames,
          "Tônica da música. Ao entrar no mixer, o efeito pega o tom do projeto." },
        { "scale",   "Escala",  0, 6, 0,   "", ParamKind::choice, false, scaleNames,
          "As vozes vão para as notas desta escala." },
        { "voices",  "Vozes",   0, 5, 0,   "", ParamKind::choice, false, voiceNames,
          "Terça e quinta acima, as duas juntas, uma terça abaixo, uma oitava abaixo, ou terça acima com oitava abaixo. "
          "Terças e quintas seguem a escala; a oitava é exata." },
        { "formant", "Formante", -7, 7, 0, "st", ParamKind::continuous, false, {},
          "O tamanho das vozes novas: negativo = mais grossas, positivo = mais finas (soam como outra pessoa)." },
        { "level",   "Nível das vozes", 0, 100, 60, "%", ParamKind::continuous, false, {},
          "O volume das vozes novas. A voz original continua igual." },
        { "width",   "Largura", 0, 100, 70, "%", ParamKind::continuous, false, {},
          "Com duas vozes, uma vai para cada lado." },
        { "follow",  "Seguir o tom do projeto", 0, 1, 1, "", ParamKind::toggle, false, {},
          "Ligado: quando o tom ou a escala do projeto mudam, a harmonia muda junto. Escolher o tom aqui desliga." },
    };

    static_assert (std::size (harmonyParams) == HarmonyProcessor::count);

    struct VoiceSpec { int degrees; bool octaveDown; };

    VoiceSpec voiceSpec (int choice, int voice) noexcept
    {
        switch (choice)
        {
            case 0:  return { 2, false };
            case 1:  return { 4, false };
            case 2:  return { voice == 0 ? 2 : 4, false };
            case 3:  return { -2, false };
            case 4:  return { 0, true };
            default: return voice == 0 ? VoiceSpec { 2, false } : VoiceSpec { 0, true };
        }
    }

    int pitchClassOf (int key) noexcept   { return ((key % 12) + 12) % 12; }
}

int HarmonyProcessor::voiceCount (int choice) noexcept
{
    return choice == 2 || choice == 5 ? 2 : 1;
}

int HarmonyProcessor::scaleStep (int note, int degrees, int keyRoot, int mask) noexcept
{
    // Cromática não tem graus: terça maior e quinta justa.
    if (mask == 0xFFF)
        return note + (degrees == 2 ? 4 : degrees == -2 ? -3 : degrees == 4 ? 7 : degrees == -4 ? -7 : degrees);

    const int dir = degrees > 0 ? 1 : -1;
    int n = note, left = std::abs (degrees);

    for (int guard = 0; left > 0 && guard < 24; ++guard)
    {
        n += dir;

        if (((mask >> pitchClassOf (n - keyRoot)) & 1) != 0)
            --left;
    }

    return n;
}

const FxInfo& HarmonyProcessor::staticInfo()
{
    static const FxInfo info { "harmony", "Harmonia", "Harm.", "Voz",
                               "Uma ou duas vozes a mais em terças, quintas ou oitavas da escala do projeto, já afinadas e com a "
                               "formante no lugar: refrão e ad-lib com coro.",
                               harmonyParams };
    return info;
}

void HarmonyProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = harmonyParams[i].def;

    core.prepare (rate, 2);

    for (auto& r : ratio)
    {
        r.prepare (rate, 15.0f);
        r.snap (1.0f);
    }

    formantFactor.prepare (rate, 30.0f);
    levelGain.prepare (rate, 20.0f);
    widthAmount.prepare (rate, 20.0f);
    snapToTargets();
    reset();
}

void HarmonyProcessor::snapToTargets() noexcept
{
    for (int i = 0; i < count; ++i)
        setParam (i, values[(size_t) i]);

    for (auto& r : ratio)
        r.snap (r.getTarget());

    formantFactor.snap (formantFactor.getTarget());
    levelGain.snap (levelGain.getTarget());
    widthAmount.snap (widthAmount.getTarget());
}

void HarmonyProcessor::reset() noexcept
{
    core.reset();
    lastNote = -1;
    lastHarmony = { -1, -1 };
    wasVoiced = false;
    setMeter (detectedNote, -1.0f);
    setMeter (voicedMeter, 0.0f);
    setMeter (harmony1, -1.0f);
    setMeter (harmony2, -1.0f);
}

void HarmonyProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        formantFactor.setTarget (std::exp2 (values[formant] / 12.0f));
        levelGain.setTarget (values[level] * 0.01f);
        widthAmount.setTarget (values[width] * 0.01f);
    }
}

void HarmonyProcessor::updateTargets() noexcept
{
    if (! core.voiced() || core.periodSamples() <= 0.0f)
    {
        wasVoiced = false;
        return;   // sem voz: as razões ficam onde estavam (as vozes estão caladas)
    }

    const float midi = 69.0f + 12.0f * std::log2 ((float) (sampleRate / core.periodSamples()) / 440.0f);
    const int keyRoot = (int) values[key];
    const int mask = TuneProcessor::scaleMask ((int) values[scale]);
    int note = TuneProcessor::nearestInScale (midi, keyRoot, mask);

    // Histerese, como na afinação: a nota não pula entre duas vizinhas.
    if (lastNote >= 0 && note != lastNote && ((mask >> pitchClassOf (lastNote - keyRoot)) & 1) != 0
        && std::abs (midi - (float) lastNote) < 0.65f)
        note = lastNote;

    lastNote = note;
    const int choice = (int) values[voices];

    for (int v = 0; v < voiceCount (choice); ++v)
    {
        const auto spec = voiceSpec (choice, v);
        const int target = spec.octaveDown ? note - 12 : scaleStep (note, spec.degrees, keyRoot, mask);
        lastHarmony[(size_t) v] = target;
        ratio[(size_t) v].setTarget (std::clamp (std::exp2 (((float) target - midi) / 12.0f), 0.3f, 3.0f));

        // Começo de uma frase: a voz nova entra já na nota (sem deslizar desde o uníssono).
        if (! wasVoiced)
            ratio[(size_t) v].snap (ratio[(size_t) v].getTarget());
    }

    wasVoiced = true;

    setMeter (detectedNote, (float) note);
    setMeter (harmony1, (float) lastHarmony[0]);
    setMeter (harmony2, voiceCount (choice) > 1 ? (float) lastHarmony[1] : -1.0f);
}

void HarmonyProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;
    const int voiceTotal = voiceCount ((int) values[voices]);
    constexpr int controlRate = 32;

    for (int start = 0; start < block.numSamples; start += controlRate)
    {
        const int n = std::min (controlRate, block.numSamples - start);
        updateTargets();
        const float f = formantFactor.skip (n);

        for (int v = 0; v < PsolaCore::maxReads; ++v)
            core.setShift (v, ratio[(size_t) v].skip (n), f, true);

        const float w = widthAmount.skip (n);

        // Duas vozes: uma para cada lado (potência constante); uma voz só fica no centro.
        const float a0 = (float) (0.25 * pi * (1.0 - (voiceTotal > 1 ? w : 0.0f)));
        const float a1 = (float) (0.25 * pi * (1.0 + w));
        const float g0l = std::cos (a0), g0r = std::sin (a0), g1l = std::cos (a1), g1r = std::sin (a1);

        for (int i = start; i < start + n; ++i)
        {
            float wl[PsolaCore::maxReads] {}, wr[PsolaCore::maxReads] {}, dl = 0.0f, dr = 0.0f;
            core.process (block.left[i], block.right[i], wl, wr, dl, dr);
            const float lvl = levelGain.next();
            const float v0 = 0.5f * (wl[0] + wr[0]), v1 = voiceTotal > 1 ? 0.5f * (wl[1] + wr[1]) : 0.0f;
            block.left[i] = dl + lvl * (v0 * g0l + v1 * g1l);
            block.right[i] = dr + lvl * (v0 * g0r + v1 * g1r);
        }
    }

    setMeter (voicedMeter, core.voiced() ? 1.0f : 0.0f);
}

} // namespace whynot::dsp
