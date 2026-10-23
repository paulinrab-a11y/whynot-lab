#include "Synth.h"

#include "../dsp/Color.h"
#include "../dsp/EqProcessor.h"
#include "../dsp/Master.h"
#include "../dsp/Space.h"

#include <limits>
#include <numeric>

namespace whynot::synth
{

namespace
{
    constexpr float sqrt2 = 1.41421356f;
    constexpr float halfPi = 1.57079633f;
    constexpr float twoPiF = 6.28318531f;

    /** Ganho esquerdo e direito com potência constante; no centro, 1 e 1 exatos. */
    void panGains (float pan, float& left, float& right) noexcept
    {
        if (pan == 0.0f)
        {
            left = right = 1.0f;
            return;
        }

        const float angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * (halfPi * 0.5f);
        left = std::cos (angle) * sqrt2;
        right = std::sin (angle) * sqrt2;
    }

    /** Coeficientes do SVF sem a conta de ganho (o make faz um pow de 0 dB a cada chamada).
        Mesma resposta do SvfCoeffs::make para os quatro tipos do synth. */
    dsp::SvfCoeffs svfCoeffs (dsp::FilterType type, double hz, double q, double sampleRate) noexcept
    {
        const double k = 1.0 / std::max (q, 0.025);

        switch (type)
        {
            case dsp::FilterType::highpass: return dsp::SvfCoeffs::custom (hz, q, 1.0, -k, -1.0, sampleRate);
            case dsp::FilterType::bandpass: return dsp::SvfCoeffs::custom (hz, q, 0.0, k, 0.0, sampleRate);
            case dsp::FilterType::notch:    return dsp::SvfCoeffs::custom (hz, q, 1.0, -k, 0.0, sampleRate);
            default:                        return dsp::SvfCoeffs::custom (hz, q, 0.0, 0.0, 1.0, sampleRate);
        }
    }

    float noteToHz (float note) noexcept
    {
        return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f));
    }

    /** Ordem de abertura do unison: espalha os desvios de afinação pelos dois lados. */
    int panOrder (int i, int n) noexcept
    {
        const int k = (n % 3 == 0) ? 5 : 3;
        return (i * k) % n;
    }

    float lfoShapeValue (LfoShape shape, float phase, float held, float prevHeld) noexcept
    {
        switch (shape)
        {
            case LfoShape::sine:         return std::sin (twoPiF * phase);
            case LfoShape::triangle:     { float u = phase + 0.75f; u -= std::floor (u); return 4.0f * std::abs (u - 0.5f) - 1.0f; }
            case LfoShape::rampUp:       return 2.0f * phase - 1.0f;
            case LfoShape::rampDown:     return 1.0f - 2.0f * phase;
            case LfoShape::square:       return phase < 0.5f ? 1.0f : -1.0f;
            case LfoShape::stepRandom:   return held;
            case LfoShape::smoothRandom:
            {
                const float f = 0.5f - 0.5f * std::cos (3.14159265f * phase);
                return prevHeld + (held - prevHeld) * f;
            }
        }

        return 0.0f;
    }

    dsp::FilterType filterTypeFor (FilterMode m) noexcept
    {
        switch (m)
        {
            case FilterMode::highpass: return dsp::FilterType::highpass;
            case FilterMode::bandpass: return dsp::FilterType::bandpass;
            case FilterMode::notch:    return dsp::FilterType::notch;
            case FilterMode::lowpass:  break;
        }

        return dsp::FilterType::lowpass;
    }
}

//==============================================================================
Synth::Synth()
    : tables (Wavetables::get()), sine (SineTable::get())
{
    values = std::make_unique<std::atomic<float>[]> ((size_t) numParams);
    const auto defaults = defaultValues();

    for (int i = 0; i < numParams; ++i)
    {
        values[(size_t) i].store (defaults[(size_t) i]);
        applied[(size_t) i] = std::numeric_limits<float>::quiet_NaN();
    }

    sat = std::make_unique<dsp::FxHost> (std::make_unique<dsp::SaturatorProcessor>());
    eq = std::make_unique<dsp::FxHost> (std::make_unique<dsp::EqProcessor>());
    stereo = std::make_unique<dsp::FxHost> (std::make_unique<dsp::StereoProcessor>());
    delay = std::make_unique<dsp::FxHost> (std::make_unique<dsp::DelayProcessor>());
    reverb = std::make_unique<dsp::FxHost> (std::make_unique<dsp::ReverbProcessor>());

    std::uint32_t seed = 0x2545f491u;

    for (auto& v : voices)
        v.rng = Random (seed += 0x9e3779b9u);
}

Synth::~Synth() = default;

void Synth::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= numParams || ! std::isfinite (value))
        return;

    values[(size_t) index].store (param (index).clamp (value), std::memory_order_relaxed);
}

float Synth::getParam (int index) const noexcept
{
    return index >= 0 && index < numParams ? values[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
}

void Synth::setAll (std::span<const float> v) noexcept
{
    for (int i = 0; i < numParams && i < (int) v.size(); ++i)
        setParam (i, v[(size_t) i]);
}

std::vector<float> Synth::getAll() const
{
    std::vector<float> out ((size_t) numParams);

    for (int i = 0; i < numParams; ++i)
        out[(size_t) i] = getParam (i);

    return out;
}

//==============================================================================
void Synth::prepare (double newRate, int maxBlockSize)
{
    sampleRate = newRate > 0.0 ? newRate : 48000.0;
    maxBlock = std::max (32, maxBlockSize);

    // Valores atuais antes de preparar os efeitos: o FxHost os aplica sem rampa (render começa certo).
    for (int i = 0; i < numParams; ++i)
        applied[(size_t) i] = values[(size_t) i].load (std::memory_order_relaxed);

    rebuildCache();
    updateEffects();

    latency = 0;

    for (auto* host : { sat.get(), eq.get(), stereo.get(), delay.get(), reverb.get() })
    {
        host->prepare (sampleRate, maxBlock);
        latency += host->latencySamples();
    }

    volume.prepare (sampleRate, 20.0f);
    volume.snap (dsp::dbToGain (cache.volumeDb));

    for (auto& v : voices)
        v.active = false;

    numHeld = 0;
    monoVoice = monoKey = -1;
    lastNote = -1.0f;
    lastMono = cache.mono;
    bend = modWheel = aftertouch = 0.0f;
    freePhase = {};
    prepared = true;
}

double Synth::tailSeconds() const noexcept
{
    double tail = getParam (P::env (0, eRelease)) * 0.0015 + 0.05;

    if (getParam (P::dlyOn) >= 0.5f)
        tail += 1.0 + 4.0 * getParam (P::dlyFb) / 95.0;

    if (getParam (P::revOn) >= 0.5f)
        tail += getParam (P::revDecay) * 1.2;

    return tail;
}

int Synth::activeVoices() const noexcept
{
    int n = 0;

    for (auto& v : voices)
        n += v.active ? 1 : 0;

    return n;
}

void Synth::reset() noexcept
{
    for (auto& v : voices)
    {
        v.active = false;

        for (auto& section : v.filter)
            for (auto& f : section)
                f.reset();
    }

    numHeld = 0;
    monoVoice = monoKey = -1;
    lastNote = -1.0f;

    for (auto* host : { sat.get(), eq.get(), stereo.get(), delay.get(), reverb.get() })
        host->processor().reset();
}

//==============================================================================
void Synth::applyParams() noexcept
{
    bool changed = false;

    for (int i = 0; i < numParams; ++i)
    {
        const float v = values[(size_t) i].load (std::memory_order_relaxed);

        if (v != applied[(size_t) i])
        {
            applied[(size_t) i] = v;
            changed = true;
        }
    }

    if (changed)
    {
        rebuildCache();
        updateEffects();
    }
}

void Synth::rebuildCache() noexcept
{
    auto& c = cache;
    auto v = [this] (int index) { return applied[(size_t) index]; };
    auto on = [&v] (int index) { return v (index) >= 0.5f; };
    auto pick = [&v] (int index) { return (int) std::lround (v (index)); };

    for (int l = 0; l < numLayers; ++l)
    {
        auto& lc = c.layers[(size_t) l];
        auto lp = [l] (int p) { return P::layer (l, p); };

        lc.on = on (lp (lOn));
        lc.type = (LayerType) std::clamp (pick (lp (lType)), 0, 3);
        lc.gain = dsp::dbToGain (v (lp (lLevel)));
        lc.pan = v (lp (lPan)) * 0.01f;
        lc.offset = (float) ((pick (lp (lOct)) - 3) * 12 + (pick (lp (lSemi)) - 12)) + v (lp (lFine)) * 0.01f;
        lc.voices = std::clamp (pick (lp (lVoices)) + 1, 1, maxUnison);
        lc.detune = v (lp (lDetune));
        lc.width = v (lp (lWidth)) * 0.01f;
        lc.wave = (Wave) std::clamp (pick (lp (lWave)), 0, 3);
        lc.shape = v (lp (lShape)) * 0.01f;
        lc.table = std::clamp (pick (lp (lTable)), 0, Wavetables::numTables() - 1);
        lc.phaseRandom = v (lp (lPhase)) * 0.01f;
        lc.toFilter = on (lp (lFilter));
        lc.algo = (FmAlgo) std::clamp (pick (lp (lFmAlgo)), 0, (int) FmAlgo::count - 1);

        for (int k = 0; k < 3; ++k)
        {
            lc.ratio[(size_t) k] = v (lp (lFmRatio2 + k));
            lc.index[(size_t) k] = v (lp (lFmAmt2 + k)) * 0.01f * (8.0f / twoPiF);   // até 8 radianos, em ciclos
        }

        lc.feedback = v (lp (lFmFeedback)) * 0.01f * 0.25f;
        lc.fmDecayCoef = (float) std::exp (-1.0 / std::max (1.0, v (lp (lFmDecay)) * 0.001 * sampleRate));
        lc.fmDecayBlock = std::pow (lc.fmDecayCoef, (float) controlBlock);
        lc.fmSustain = v (lp (lFmSustain)) * 0.01f;

        const int n = lc.voices;

        for (int i = 0; i < maxUnison; ++i)
        {
            const float pos = n == 1 ? 0.0f : -1.0f + 2.0f * (float) i / (float) (n - 1);
            lc.spread[(size_t) i] = i < n ? pos : 0.0f;
        }

        for (int i = 0; i < n; ++i)
        {
            lc.panSpread[(size_t) i] = lc.spread[(size_t) panOrder (i, n)];
            panGains (lc.pan + lc.width * lc.panSpread[(size_t) i], lc.unisonPanL[(size_t) i], lc.unisonPanR[(size_t) i]);
        }

        // O ruído é uma voz só e a largura dele é a diferença entre os lados, não o pan: fica no pan da camada.
        if (lc.type == LayerType::noise)
            panGains (lc.pan, lc.unisonPanL[0], lc.unisonPanR[0]);

        // Uma voz só no centro (o ruído abre com a largura): esquerdo e direito saem iguais.
        lc.monoSource = lc.pan == 0.0f && (lc.width == 0.0f || (n == 1 && lc.type != LayerType::noise));
    }

    c.subOn = on (P::subOn);
    c.subGain = dsp::dbToGain (v (P::subLevel));
    c.subRatio = pick (P::subOct) == 0 ? 0.5f : 0.25f;
    c.subWave = (SubWave) std::clamp (pick (P::subWave), 0, 2);

    c.filterOn = on (P::fltOn);
    c.filterMode = (FilterMode) std::clamp (pick (P::fltMode), 0, 3);
    c.steep = pick (P::fltSlope) == 1;
    c.cutoffNote = 69.0f + 12.0f * std::log2 (std::max (1.0f, v (P::fltCutoff)) / 440.0f);
    c.reso = v (P::fltReso);
    c.drive = v (P::fltDrive);
    c.keyTrack = v (P::fltKey) * 0.01f;
    c.envOctaves = v (P::fltEnv);
    c.velToCutoff = v (P::fltVel) * 0.01f;

    for (int e = 0; e < numEnvs; ++e)
        c.env[(size_t) e].set (v (P::env (e, eAttack)), v (P::env (e, eDecay)), v (P::env (e, eSustain)) * 0.01f,
                               v (P::env (e, eRelease)), v (P::env (e, eCurve)) * 0.01f, sampleRate);

    for (int l = 0; l < numLfos; ++l)
    {
        c.lfoShape[(size_t) l] = (LfoShape) std::clamp (pick (P::lfo (l, lfoShape)), 0, 6);
        c.lfoSync[(size_t) l] = on (P::lfo (l, lfoSync));
        c.lfoRate[(size_t) l] = v (P::lfo (l, lfoRate));
        c.lfoBeats[(size_t) l] = lfoDivisionBeats (pick (P::lfo (l, lfoDiv)));
        c.lfoRetrig[(size_t) l] = pick (P::lfo (l, lfoRetrig)) == 1;
        c.lfoFadeSamples[(size_t) l] = (float) (v (P::lfo (l, lfoFade)) * 0.001 * sampleRate);
    }

    c.numSlotsUsed = 0;

    for (int s = 0; s < numSlots; ++s)
    {
        const int src = pick (P::slot (s, mSrc)), dst = pick (P::slot (s, mDst));
        const float amount = v (P::slot (s, mAmt)) * 0.01f;

        if (src > 0 && src < (int) Source::count && dst > 0 && dst < (int) Dest::count && amount != 0.0f)
            c.slots[(size_t) c.numSlotsUsed++] = { src, dst, amount };
    }

    // Envelopes 2 e 3 sem ninguém lendo ficam parados (uma rota nova no meio da nota começa o envelope dali).
    c.env2Used = c.filterOn && c.envOctaves != 0.0f;
    c.env3Used = false;
    c.lfoUsed = {};

    for (int s = 0; s < c.numSlotsUsed; ++s)
    {
        const int src = c.slots[(size_t) s].src;
        c.env2Used |= src == (int) Source::env2;
        c.env3Used |= src == (int) Source::env3;

        for (int l = 0; l < numLfos; ++l)
            c.lfoUsed[(size_t) l] = c.lfoUsed[(size_t) l] || src == (int) Source::lfo1 + l;
    }

    c.volumeDb = v (P::volume);
    c.poly = std::clamp (pick (P::poly) + 1, 1, maxPolyphony);
    c.legato = on (P::legato);
    c.glideAlways = pick (P::porta) == 1;
    c.velAmp = v (P::velAmp) * 0.01f;
    c.bendRange = v (P::bend);
    c.drift = v (P::drift) * 0.01f;
    c.mono = on (P::mono);
    c.glideMs = v (P::glide);
    c.bpm = std::max (20.0f, v (P::bpm));

    c.filterInputMono = true;

    for (auto& lc : c.layers)
        if (lc.on && lc.toFilter && ! lc.monoSource)
            c.filterInputMono = false;

    // O que cada voz guardou foi calculado com os valores antigos.
    for (auto& voice : voices)
    {
        voice.coeffsHz = -1.0;

        for (auto& lv : voice.layers)
            lv.ratioDetune = lv.noiseShape = -1.0f;
    }
}

void Synth::updateEffects() noexcept
{
    using Sat = dsp::SaturatorProcessor;
    using Eq = dsp::EqProcessor;
    using St = dsp::StereoProcessor;
    using Dl = dsp::DelayProcessor;
    using Rv = dsp::ReverbProcessor;
    auto v = [this] (int index) { return applied[(size_t) index]; };

    sat->setParam (Sat::mode, v (P::satMode));
    sat->setParam (Sat::drive, v (P::satDrive));
    sat->setParam (Sat::mix, v (P::satMix));
    sat->setBypassed (v (P::satOn) < 0.5f);

    eq->setParam (Eq::hpOn, v (P::eqHp) > 20.5f ? 1.0f : 0.0f);
    eq->setParam (Eq::hpFreq, v (P::eqHp));
    eq->setParam (Eq::lowFreq, 120.0f);
    eq->setParam (Eq::lowGain, v (P::eqLow));
    eq->setParam (Eq::b2Freq, v (P::eqMidFreq));
    eq->setParam (Eq::b2Gain, v (P::eqMid));
    eq->setParam (Eq::b2Q, 0.8f);
    eq->setParam (Eq::highFreq, 7000.0f);
    eq->setParam (Eq::highGain, v (P::eqHigh));
    eq->setBypassed (v (P::eqOn) < 0.5f);

    stereo->setParam (St::width, v (P::width));
    stereo->setBypassed (std::abs (v (P::width) - 100.0f) < 0.5f);

    delay->setParam (Dl::sync, 1.0f);
    delay->setParam (Dl::division, v (P::dlyDiv));
    delay->setParam (Dl::feedback, v (P::dlyFb));
    delay->setParam (Dl::mix, v (P::dlyMix));
    delay->setParam (Dl::bpm, v (P::bpm));
    delay->setBypassed (v (P::dlyOn) < 0.5f);

    reverb->setParam (Rv::size, v (P::revSize));
    reverb->setParam (Rv::decay, v (P::revDecay));
    reverb->setParam (Rv::predelay, 12.0f);
    reverb->setParam (Rv::lowCut, 250.0f);
    reverb->setParam (Rv::damping, 7000.0f);
    reverb->setParam (Rv::mix, v (P::revMix));
    reverb->setBypassed (v (P::revOn) < 0.5f);

    volume.setTarget (dsp::dbToGain (cache.volumeDb));
}

//==============================================================================
void Synth::process (float* left, float* right, int numSamples, const Event* events, int numEvents) noexcept
{
    std::fill_n (left, numSamples, 0.0f);
    std::fill_n (right, numSamples, 0.0f);

    if (! prepared || numSamples <= 0)
        return;

    dsp::DenormalGuard noDenormals;
    applyParams();

    // Mono ligado ou desligado com notas soando: a pilha recomeça (a próxima nota não desliza de uma tecla solta).
    if (cache.mono != lastMono)
    {
        lastMono = cache.mono;
        numHeld = 0;
        monoVoice = monoKey = -1;
    }

    int position = 0;

    for (int e = 0; e < numEvents; ++e)
    {
        const int when = std::clamp (events[e].sampleOffset, position, numSamples);
        render (left + position, right + position, when - position);
        position = when;
        handle (events[e]);
    }

    render (left + position, right + position, numSamples - position);

    // LFOs livres: andam junto com o tempo, toquem notas ou não.
    for (int l = 0; l < numLfos; ++l)
    {
        const double rate = cache.lfoSync[(size_t) l] ? cache.bpm / 60.0 / cache.lfoBeats[(size_t) l] : cache.lfoRate[(size_t) l];
        freePhase[(size_t) l] += rate * numSamples / sampleRate;
        freePhase[(size_t) l] -= std::floor (freePhase[(size_t) l]);
    }

    sat->process (left, right, numSamples);
    eq->process (left, right, numSamples);
    stereo->process (left, right, numSamples);
    delay->process (left, right, numSamples);
    reverb->process (left, right, numSamples);

    float peak = 0.0f, check = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = volume.next();
        const float l = left[i] * g, r = right[i] * g;
        peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        check += l * 0.0f + r * 0.0f;
        left[i] = softClip (l);
        right[i] = softClip (r);
    }

    peakBeforeClip = peak;

    if (check != check)
    {
        // NaN (não deveria acontecer): silencia e recomeça em vez de travar filtros e efeitos.
        reset();
        std::fill_n (left, numSamples, 0.0f);
        std::fill_n (right, numSamples, 0.0f);
    }
}

void Synth::handle (const Event& e) noexcept
{
    switch (e.type)
    {
        case Event::Type::noteOn:      noteOn (e.key, e.value); break;
        case Event::Type::noteOff:     noteOff (e.key); break;
        case Event::Type::allNotesOff: allNotesOff(); break;
        case Event::Type::pitchBend:   bend = std::clamp (e.value, -1.0f, 1.0f); break;
        case Event::Type::modWheel:    modWheel = std::clamp (e.value, 0.0f, 1.0f); break;
        case Event::Type::aftertouch:  aftertouch = std::clamp (e.value, 0.0f, 1.0f); break;
    }
}

void Synth::render (float* left, float* right, int numSamples) noexcept
{
    // Voz por voz, o trecho inteiro de cada uma (em passos de controle): o estado da voz (uns 3 KB)
    // fica no cache enquanto ela toca. Trocar de voz a cada 16 amostras relia tudo. A soma de cada
    // amostra continua na ordem das vozes: o resultado é o mesmo.
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];

        if (! v.active)
            continue;

        for (int start = 0; start < numSamples && v.active; start += controlBlock)
        {
            const int n = std::min (controlBlock, numSamples - start);
            controlUpdate (v, n);
            renderVoice (v, left + start, right + start, n);
        }

        if (! v.active && i == monoVoice)
            monoVoice = -1;
    }
}

//==============================================================================
// Notas

void Synth::pushHeld (int key) noexcept
{
    removeHeld (key);

    if (numHeld == (int) held.size())
    {
        for (int i = 1; i < numHeld; ++i)
            held[(size_t) i - 1] = held[(size_t) i];

        --numHeld;
    }

    held[(size_t) numHeld++] = key;
}

void Synth::removeHeld (int key) noexcept
{
    for (int i = 0; i < numHeld; ++i)
    {
        if (held[(size_t) i] == key)
        {
            for (int j = i + 1; j < numHeld; ++j)
                held[(size_t) j - 1] = held[(size_t) j];

            --numHeld;
            return;
        }
    }
}

void Synth::noteOn (int key, float velocity) noexcept
{
    key = std::clamp (key, 0, 127);

    if (! (velocity > 0.0f))
    {
        noteOff (key);
        return;
    }

    velocity = std::min (velocity, 1.0f);
    const bool canGlide = cache.glideMs > 0.0f;

    if (cache.mono)
    {
        auto* current = monoVoice >= 0 ? &voices[(size_t) monoVoice] : nullptr;
        const bool sounding = current != nullptr && current->active && ! current->stealing;

        // Legato: nota nova com outra ainda segurada (o slide do piano roll chega assim).
        if (sounding && numHeld > 0)
        {
            pushHeld (key);
            retrigger (*current, key, velocity, canGlide, ! cache.legato);
            monoKey = key;
            lastNote = (float) key;
            return;
        }

        pushHeld (key);

        // Nota separada: a anterior sai com um fade curto (uma nota por vez, sem somar fase) e
        // a nova começa noutra voz. Com glide "Sempre", ela parte da nota anterior.
        const float from = (cache.glideAlways && canGlide && lastNote >= 0.0f)
                             ? (sounding ? current->note : lastNote) : -1.0f;

        if (sounding)
            beginSteal (*current);

        const int v = allocateVoice();
        startVoice (voices[(size_t) v], key, velocity, from);
        monoVoice = v;
        monoKey = key;
        lastNote = (float) key;
        return;
    }

    // Poli: a mesma tecla ainda segurada solta antes (não empilha duas vozes iguais).
    for (auto& v : voices)
        if (v.active && v.held && ! v.stealing && v.key == key)
            releaseVoice (v);

    const float from = (cache.glideAlways && canGlide && lastNote >= 0.0f) ? lastNote : -1.0f;
    const int v = allocateVoice();
    startVoice (voices[(size_t) v], key, velocity, from);
    lastNote = (float) key;
}

void Synth::noteOff (int key) noexcept
{
    if (cache.mono)
    {
        removeHeld (key);

        if (key != monoKey || monoVoice < 0)
            return;   // soltou uma nota que já não soa (o fim do legato de um slide)

        auto& v = voices[(size_t) monoVoice];

        if (numHeld > 0)
        {
            // Ainda há tecla segurada: volta deslizando para ela.
            const int back = held[(size_t) numHeld - 1];
            retrigger (v, back, v.velocity, cache.glideMs > 0.0f, ! cache.legato);
            monoKey = back;
            lastNote = (float) back;
        }
        else
        {
            releaseVoice (v);
            monoKey = -1;
        }

        return;
    }

    for (auto& v : voices)
        if (v.active && v.held && v.key == key)
            releaseVoice (v);
}

void Synth::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.active)
            releaseVoice (v);

    numHeld = 0;
    monoKey = -1;
}

int Synth::allocateVoice() noexcept
{
    const int limit = cache.mono ? 1 : cache.poly;
    int playing = 0;

    for (auto& v : voices)
        playing += (v.active && ! v.stealing) ? 1 : 0;

    // Passou da polifonia: a mais antiga sai com fade (de preferência uma que já foi solta).
    while (playing >= limit)
    {
        Voice* victim = nullptr;

        for (int pass = 0; pass < 2 && victim == nullptr; ++pass)
            for (auto& v : voices)
                if (v.active && ! v.stealing && (pass == 1 || ! v.held) && (victim == nullptr || v.order < victim->order))
                    victim = &v;

        if (victim == nullptr)
            break;

        beginSteal (*victim);
        --playing;
    }

    for (int i = 0; i < maxVoices; ++i)
        if (! voices[(size_t) i].active)
            return i;

    // Todas ocupadas (muitas saindo com fade ao mesmo tempo): corta a que está mais perto do silêncio.
    int chosen = 0;
    float quietest = std::numeric_limits<float>::max();

    for (int i = 0; i < maxVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        const float level = v.stealing ? v.stealGain * v.env[0].getValue() : 1.0f + (float) (voiceCounter - v.order) * -1.0e-6f;

        if (level < quietest)
        {
            quietest = level;
            chosen = i;
        }
    }

    return chosen;
}

void Synth::startVoice (Voice& v, int key, float velocity, float glideFromNote) noexcept
{
    v.active = true;
    v.held = true;
    v.stealing = false;
    v.fresh = true;
    v.stealGain = 1.0f;
    v.stealStep = 0.0f;
    v.key = key;
    v.velocity = velocity;
    v.order = ++voiceCounter;

    if (glideFromNote >= 0.0f && glideFromNote != (float) key)
    {
        v.note = v.glideFrom = glideFromNote;
        v.glideDelta = (float) key - glideFromNote;
        v.glideTotal = v.glideLeft = std::max (1, (int) std::lround (cache.glideMs * 0.001 * sampleRate));
    }
    else
    {
        v.note = (float) key;
        v.glideLeft = 0;
    }

    for (auto& e : v.env)
    {
        e.reset();
        e.noteOn();
    }

    for (int l = 0; l < numLfos; ++l)
    {
        v.lfoPhase[(size_t) l] = cache.lfoRetrig[(size_t) l] ? 0.0f : (float) freePhase[(size_t) l];
        v.lfoHeld[(size_t) l] = v.rng.bipolar();
        v.lfoPrevHeld[(size_t) l] = v.rng.bipolar();
        v.lfoValue[(size_t) l] = lfoShapeValue (cache.lfoShape[(size_t) l], v.lfoPhase[(size_t) l],
                                                v.lfoHeld[(size_t) l], v.lfoPrevHeld[(size_t) l]);
    }

    v.age = 0;
    v.random = v.rng.bipolar();

    for (auto& section : v.filter)
        for (auto& f : section)
            f.reset();

    for (int l = 0; l < numLayers; ++l)
    {
        auto& lv = v.layers[(size_t) l];
        const auto& lc = cache.layers[(size_t) l];

        for (int i = 0; i < maxUnison; ++i)
        {
            lv.phase[(size_t) i] = lc.phaseRandom * v.rng.uniform();
            lv.drift[(size_t) i] = v.rng.bipolar() * cache.drift * 8.0f;   // até +-8 centésimos
            lv.fb1[(size_t) i] = lv.fb2[(size_t) i] = 0.0f;

            for (auto& ops : lv.opPhase)
                ops[(size_t) i] = lv.phase[(size_t) i];
        }

        lv.ratioDetune = -1.0f;   // o drift mudou
        lv.fmEnv = 1.0f;
        lv.noiseL = lv.noiseR = 0.0f;
    }

    v.subPhase = 0.0f;
}

void Synth::retrigger (Voice& v, int key, float velocity, bool glide, bool restartEnvelopes) noexcept
{
    v.key = key;
    v.held = true;

    if (glide)
    {
        v.glideFrom = v.note;
        v.glideDelta = (float) key - v.note;
        v.glideTotal = v.glideLeft = std::max (1, (int) std::lround (cache.glideMs * 0.001 * sampleRate));
    }
    else
    {
        v.note = (float) key;
        v.glideLeft = 0;
    }

    if (restartEnvelopes)
    {
        v.velocity = velocity;

        for (auto& e : v.env)
            e.noteOn();   // do valor atual: sem estalo

        for (auto& lv : v.layers)
            lv.fmEnv = 1.0f;
    }
}

void Synth::releaseVoice (Voice& v) noexcept
{
    v.held = false;

    for (auto& e : v.env)
        e.noteOff();
}

void Synth::beginSteal (Voice& v) noexcept
{
    if (! v.active || v.stealing)
        return;

    v.stealing = true;
    v.held = false;
    v.stealStart = v.stealGain;
    v.stealPos = 0.0f;
    v.stealStep = 1.0f / (float) std::max (1.0, 0.006 * sampleRate);   // some em 6 ms
}

//==============================================================================
// Controle (a cada 16 amostras)

void Synth::controlUpdate (Voice& v, int n) noexcept
{
    const auto& c = cache;
    const float invN = 1.0f / (float) n;
    const float rate = (float) sampleRate;

    if (v.glideLeft > 0)
    {
        v.glideLeft = std::max (0, v.glideLeft - n);
        v.note = v.glideLeft == 0 ? v.glideFrom + v.glideDelta
                                  : v.glideFrom + v.glideDelta * (1.0f - (float) v.glideLeft / (float) v.glideTotal);
    }

    // Fontes e matriz.
    std::array<float, (size_t) Source::count> src {};
    std::array<float, (size_t) Dest::count> dst {};

    if (c.numSlotsUsed > 0)
    {
        for (int l = 0; l < numLfos; ++l)
        {
            const float fadeSamples = c.lfoFadeSamples[(size_t) l];
            const float fade = fadeSamples > 0.0f ? std::min (1.0f, (float) v.age / fadeSamples) : 1.0f;
            src[(size_t) Source::lfo1 + (size_t) l] = v.lfoValue[(size_t) l] * fade;
        }

        src[(size_t) Source::env1] = v.env[0].getValue();
        src[(size_t) Source::env2] = v.env[1].getValue();
        src[(size_t) Source::env3] = v.env[2].getValue();
        src[(size_t) Source::velocity] = v.velocity;
        src[(size_t) Source::note] = (v.note - 60.0f) / 60.0f;
        src[(size_t) Source::modWheel] = modWheel;
        src[(size_t) Source::aftertouch] = aftertouch;
        src[(size_t) Source::random] = v.random;

        for (int s = 0; s < c.numSlotsUsed; ++s)
            dst[(size_t) c.slots[(size_t) s].dst] += c.slots[(size_t) s].amount * src[(size_t) c.slots[(size_t) s].src];
    }

    // LFOs andam (o valor novo vale no próximo passo).
    for (int l = 0; l < numLfos; ++l)
    {
        float hz = c.lfoSync[(size_t) l] ? (float) (c.bpm / 60.0 / c.lfoBeats[(size_t) l]) : c.lfoRate[(size_t) l];
        const float rateMod = dst[(size_t) Dest::lfo1Rate + (size_t) l];

        if (rateMod != 0.0f)
            hz *= std::exp2 (std::clamp (rateMod, -1.0f, 1.0f) * 4.0f);

        float& phase = v.lfoPhase[(size_t) l];
        phase += hz * (float) n / rate;

        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            v.lfoPrevHeld[(size_t) l] = v.lfoHeld[(size_t) l];
            v.lfoHeld[(size_t) l] = v.rng.bipolar();
        }

        // Sem rota lendo este LFO, a forma (um seno, no padrão) não precisa ser calculada; a fase e o
        // sorteio andam igual, para o resto da voz (ruído) seguir a mesma sequência.
        if (c.lfoUsed[(size_t) l])
            v.lfoValue[(size_t) l] = lfoShapeValue (c.lfoShape[(size_t) l], phase, v.lfoHeld[(size_t) l], v.lfoPrevHeld[(size_t) l]);
    }

    v.age = std::min (v.age + n, 1 << 30);   // nota segurada por horas não estoura o contador

    // Afinação e camadas.
    const float pitch = v.note + bend * c.bendRange + dst[(size_t) Dest::pitch] * 24.0f;
    const float maxInc = 0.49f;

    for (int l = 0; l < numLayers; ++l)
    {
        const auto& lc = c.layers[(size_t) l];

        if (! lc.on)
            continue;

        auto& lv = v.layers[(size_t) l];
        const float layerPitch = pitch + lc.offset + dst[(size_t) Dest::pitch1 + (size_t) l] * 24.0f;
        const float baseHz = noteToHz (layerPitch);
        const int numUnison = lc.type == LayerType::noise ? 1 : lc.voices;
        const float detune = std::clamp (lc.detune + dst[(size_t) Dest::detune] * 100.0f, 0.0f, 200.0f);
        const float gain = lc.gain * std::clamp (1.0f + dst[(size_t) Dest::level1 + (size_t) l], 0.0f, 4.0f)
                           / std::sqrt ((float) numUnison);
        float highestInc = 0.0f;

        // O drift é fixo na nota: as potências de 2 só mudam junto com o detune.
        if (detune != lv.ratioDetune)
        {
            for (int i = 0; i < numUnison; ++i)
            {
                const float cents = detune * lc.spread[(size_t) i] + lv.drift[(size_t) i];
                lv.detuneRatio[(size_t) i] = cents != 0.0f ? std::exp2 (cents * (1.0f / 1200.0f)) : 1.0f;
            }

            lv.ratioDetune = detune;
        }

        for (int i = 0; i < numUnison; ++i)
        {
            const float inc = std::min (maxInc, baseHz * lv.detuneRatio[(size_t) i] / rate);
            highestInc = std::max (highestInc, inc);

            const float gl = lc.unisonPanL[(size_t) i] * gain;
            const float gr = lc.unisonPanR[(size_t) i] * gain;

            if (v.fresh)
            {
                lv.inc[(size_t) i] = inc;
                lv.gainL[(size_t) i] = gl;
                lv.gainR[(size_t) i] = gr;
                lv.incStep[(size_t) i] = lv.gainLStep[(size_t) i] = lv.gainRStep[(size_t) i] = 0.0f;
            }
            else
            {
                lv.incStep[(size_t) i] = (inc - lv.inc[(size_t) i]) * invN;
                lv.gainLStep[(size_t) i] = (gl - lv.gainL[(size_t) i]) * invN;
                lv.gainRStep[(size_t) i] = (gr - lv.gainR[(size_t) i]) * invN;
            }
        }

        const float shape = std::clamp (lc.shape + dst[(size_t) Dest::shape1 + (size_t) l], 0.0f, 1.0f);

        if (v.fresh)
        {
            lv.shape = shape;
            lv.shapeStep = 0.0f;
        }
        else
        {
            lv.shapeStep = (shape - lv.shape) * invN;
        }

        lv.level = Wavetables::levelFor (highestInc);

        if (lc.type == LayerType::fm)
        {
            lv.fmEnv *= n == controlBlock ? lc.fmDecayBlock : std::pow (lc.fmDecayCoef, (float) n);
            const float scale = (lc.fmSustain + (1.0f - lc.fmSustain) * lv.fmEnv) * 2.0f * shape;
            const float carrierHz = baseHz;

            for (int k = 0; k < 3; ++k)
            {
                // Sem passar de Nyquist (regra de Carson): nota aguda com índice alto perde brilho em vez de dobrar.
                const float modHz = baseHz * lc.ratio[(size_t) k];
                const float limitRad = std::max (0.0f, (0.45f * rate - carrierHz) / std::max (1.0f, modHz) - 2.0f);
                lv.index[(size_t) k] = std::min (lc.index[(size_t) k] * scale, limitRad / twoPiF);
            }

            // Realimentação gera uma série de harmônicos do operador de cima: some perto de Nyquist.
            float topRatio = lc.ratio[0];

            switch (lc.algo)
            {
                case FmAlgo::threeSerial: case FmAlgo::threeParallel: topRatio = lc.ratio[1]; break;
                case FmAlgo::fourSerial: case FmAlgo::fourParallel:   topRatio = lc.ratio[2]; break;
                case FmAlgo::twoOp: case FmAlgo::twoPairs: case FmAlgo::count: break;
            }

            lv.feedback = lc.feedback * std::clamp (0.45f * rate / std::max (1.0f, baseHz * topRatio) / 12.0f, 0.0f, 1.0f);
        }
        else if (lc.type == LayerType::noise)
        {
            // Cor: abaixo de 50% um passa-baixas (150 Hz a 20 kHz); acima, um passa-altas (20 Hz a 6 kHz).
            if (shape != lv.noiseShape)
            {
                const float hz = shape < 0.5f ? 150.0f * std::pow (20000.0f / 150.0f, shape * 2.0f)
                                              : 20.0f * std::pow (6000.0f / 20.0f, (shape - 0.5f) * 2.0f);
                lv.noiseCoef = 1.0f - std::exp (-twoPiF * std::min (hz, 0.45f * rate) / rate);
                lv.noiseShape = shape;
            }
        }
    }

    // Sub
    if (c.subOn)
    {
        const float inc = std::min (maxInc, noteToHz (pitch) * c.subRatio / rate);
        const float gain = c.subGain * std::clamp (1.0f + dst[(size_t) Dest::subLevel], 0.0f, 4.0f);
        v.subLevel = Wavetables::levelFor (inc);

        if (v.fresh)
        {
            v.subInc = inc;
            v.subGain = gain;
            v.subIncStep = v.subGainStep = 0.0f;
        }
        else
        {
            v.subIncStep = (inc - v.subInc) * invN;
            v.subGainStep = (gain - v.subGain) * invN;
        }
    }

    // Filtro
    if (c.filterOn)
    {
        const float cut = c.cutoffNote + c.keyTrack * (v.note - 60.0f) + c.envOctaves * 12.0f * v.env[1].getValue()
                          + c.velToCutoff * (v.velocity - 1.0f) * 36.0f + dst[(size_t) Dest::cutoff] * 96.0f;
        const double hz = std::clamp ((double) noteToHz (std::min (cut, 140.0f)), 16.0, 0.47 * sampleRate);
        const float reso = std::clamp (c.reso + dst[(size_t) Dest::resonance] * 100.0f, 0.0f, 100.0f);

        // Corte parado (sustentação, sem modulação): os coeficientes são os mesmos do passo anterior.
        if (hz != v.coeffsHz || reso != v.coeffsReso)
        {
            const double q = 0.5 * std::pow (40.0, reso * 0.01);
            const auto type = filterTypeFor (c.filterMode);

            if (c.steep)
            {
                v.coeffs[0] = svfCoeffs (type, hz, type == dsp::FilterType::lowpass || type == dsp::FilterType::highpass ? 0.541 : q, sampleRate);
                v.coeffs[1] = svfCoeffs (type, hz, q * 1.85, sampleRate);
            }
            else
            {
                v.coeffs[0] = svfCoeffs (type, hz, q, sampleRate);
            }

            v.coeffsHz = hz;
            v.coeffsReso = reso;
        }

        const float drive = std::clamp (c.drive + dst[(size_t) Dest::drive] * 100.0f, 0.0f, 100.0f);
        v.driveGain = 1.0f + drive * 0.15f;
        v.driveComp = 1.0f / std::sqrt (v.driveGain);
    }

    // Volume e pan da voz
    const float velGain = 1.0f - c.velAmp * (1.0f - v.velocity);
    const float ampMod = std::clamp (1.0f + dst[(size_t) Dest::volume], 0.0f, 2.0f);
    float pl, pr;
    panGains (dst[(size_t) Dest::pan], pl, pr);
    const float targetL = velGain * ampMod * pl * voiceScale, targetR = velGain * ampMod * pr * voiceScale;

    if (v.fresh)
    {
        v.ampL = targetL;
        v.ampR = targetR;
        v.ampLStep = v.ampRStep = 0.0f;
    }
    else
    {
        v.ampLStep = (targetL - v.ampL) * invN;
        v.ampRStep = (targetR - v.ampR) * invN;
    }

    v.fresh = false;
}

//==============================================================================
// Áudio

namespace
{
    /** Roda as vozes de unison de uma camada com um gerador gen (fase, incremento, forma). */
    template <typename Gen>
    void runUnison (std::array<float, maxUnison>& phase, std::array<float, maxUnison>& inc, const std::array<float, maxUnison>& incStep,
                    std::array<float, maxUnison>& gainL, std::array<float, maxUnison>& gainR,
                    const std::array<float, maxUnison>& gainLStep, const std::array<float, maxUnison>& gainRStep,
                    float shape0, float shapeStep, int numUnison, float* outL, float* outR, int n, Gen&& gen) noexcept
    {
        for (int i = 0; i < numUnison; ++i)
        {
            float ph = phase[(size_t) i], dt = inc[(size_t) i], gl = gainL[(size_t) i], gr = gainR[(size_t) i];
            const float ds = incStep[(size_t) i], gls = gainLStep[(size_t) i], grs = gainRStep[(size_t) i];
            float shape = shape0;

            for (int s = 0; s < n; ++s)
            {
                const float x = gen (ph, dt, shape);
                outL[s] += x * gl;
                outR[s] += x * gr;
                ph += dt;

                if (ph >= 1.0f)
                    ph -= 1.0f;

                dt += ds;
                gl += gls;
                gr += grs;
                shape += shapeStep;
            }

            phase[(size_t) i] = ph;
            inc[(size_t) i] = dt;
            gainL[(size_t) i] = gl;
            gainR[(size_t) i] = gr;
        }
    }

    /** Um ciclo de tabela (tableSize + guarda) lido com interpolação linear; t em [0, 1). */
    float readTable (const float* table, float t) noexcept
    {
        const float pos = t * (float) Wavetables::tableSize;
        const int i = (int) pos;
        const float frac = pos - (float) i;
        return table[i] + frac * (table[i + 1] - table[i]);
    }

}

template <FmAlgo algo>
void Synth::renderFm (LayerVoice& lv, const LayerCache& lc, float* outL, float* outR, int n) const noexcept
{
    // Operadores que o algoritmo não ouve ficam parados (a fase deles não muda o som).
    constexpr bool usesC = algo != FmAlgo::twoOp;
    constexpr bool usesD = algo == FmAlgo::fourSerial || algo == FmAlgo::fourParallel || algo == FmAlgo::twoPairs;

    const float iB = lv.index[0], iC = lv.index[1], iD = lv.index[2];
    const float rB = lc.ratio[0], rC = lc.ratio[1], rD = lc.ratio[2];
    const float fb = lv.feedback;
    const auto& osc = sine;

    for (int i = 0; i < lc.voices; ++i)
    {
        float pA = lv.phase[(size_t) i], pB = lv.opPhase[0][(size_t) i], pC = lv.opPhase[1][(size_t) i], pD = lv.opPhase[2][(size_t) i];
        float dt = lv.inc[(size_t) i], gl = lv.gainL[(size_t) i], gr = lv.gainR[(size_t) i];
        float h1 = lv.fb1[(size_t) i], h2 = lv.fb2[(size_t) i];
        const float ds = lv.incStep[(size_t) i], gls = lv.gainLStep[(size_t) i], grs = lv.gainRStep[(size_t) i];

        for (int s = 0; s < n; ++s)
        {
            const float feedback = fb * (h1 + h2);
            float out, top;

            if constexpr (algo == FmAlgo::twoOp)
            {
                top = osc (wrap (pB + feedback));
                out = osc (wrap (pA + iB * top));
            }
            else if constexpr (algo == FmAlgo::threeSerial)
            {
                top = osc (wrap (pC + feedback));
                const float b = osc (wrap (pB + iC * top));
                out = osc (wrap (pA + iB * b));
            }
            else if constexpr (algo == FmAlgo::threeParallel)
            {
                top = osc (wrap (pC + feedback));
                const float b = osc (pB);
                out = osc (wrap (pA + iB * b + iC * top));
            }
            else if constexpr (algo == FmAlgo::fourSerial)
            {
                top = osc (wrap (pD + feedback));
                const float c = osc (wrap (pC + iD * top));
                const float b = osc (wrap (pB + iC * c));
                out = osc (wrap (pA + iB * b));
            }
            else if constexpr (algo == FmAlgo::fourParallel)
            {
                top = osc (wrap (pD + feedback));
                out = osc (wrap (pA + iB * osc (pB) + iC * osc (pC) + iD * top));
            }
            else
            {
                top = osc (wrap (pB + feedback));
                const float d = osc (pD);
                out = 0.7f * (osc (wrap (pA + iB * top)) + osc (wrap (pC + iD * d)));
            }

            h2 = h1;
            h1 = top;
            outL[s] += out * gl;
            outR[s] += out * gr;

            pA += dt;            pA -= (float) (int) pA;
            pB += dt * rB;       pB -= (float) (int) pB;

            if constexpr (usesC) { pC += dt * rC;   pC -= (float) (int) pC; }
            if constexpr (usesD) { pD += dt * rD;   pD -= (float) (int) pD; }

            dt += ds;
            gl += gls;
            gr += grs;
        }

        lv.phase[(size_t) i] = pA;
        lv.opPhase[0][(size_t) i] = pB;
        lv.opPhase[1][(size_t) i] = pC;
        lv.opPhase[2][(size_t) i] = pD;
        lv.inc[(size_t) i] = dt;
        lv.gainL[(size_t) i] = gl;
        lv.gainR[(size_t) i] = gr;
        lv.fb1[(size_t) i] = h1;
        lv.fb2[(size_t) i] = h2;
    }
}

void Synth::renderLayer (Voice& v, int l, float* outL, float* outR, int n) noexcept
{
    const auto& lc = cache.layers[(size_t) l];
    auto& lv = v.layers[(size_t) l];
    const int numUnison = lc.voices;
    const float shape0 = lv.shape, shapeStep = lv.shapeStep;

    auto run = [&] (auto&& gen)
    {
        runUnison (lv.phase, lv.inc, lv.incStep, lv.gainL, lv.gainR, lv.gainLStep, lv.gainRStep,
                   shape0, shapeStep, numUnison, outL, outR, n, gen);
    };

    switch (lc.type)
    {
        case LayerType::analog:
        {
            // Ondas de banda limitada por oitava: o polyBLEP de 2 pontos media -34 dB de aliasing em Dó7;
            // as tabelas ficam abaixo de -80 dB. O pulso é a diferença de duas serras (PWM sem DC).
            const float* sawTable = tables.basic (Wavetables::basicSaw, lv.level);
            const float* triTable = tables.basic (Wavetables::basicTriangle, lv.level);

            switch (lc.wave)
            {
                case Wave::saw:      run ([sawTable] (float t, float, float) { return readTable (sawTable, t); }); break;
                case Wave::triangle: run ([triTable] (float t, float, float) { return readTable (triTable, t); }); break;
                case Wave::sine:     run ([this] (float t, float, float) { return sine (t); }); break;
                case Wave::pulse:
                    run ([sawTable] (float t, float, float s)
                    {
                        const float width = std::clamp (s, 0.02f, 0.98f);
                        float t2 = t + 1.0f - width;
                        t2 -= (float) (int) t2;
                        return readTable (sawTable, t2) - readTable (sawTable, t);
                    });
                    break;
            }
            break;
        }

        case LayerType::wavetable:
        {
            // Os dois quadros vizinhos ficam fixos no passo de controle (a forma anda pouco em 16 amostras);
            // a mistura entre eles acompanha a forma amostra a amostra.
            constexpr int frames = Wavetables::framesPerTable;
            const float middle = std::clamp (shape0 + shapeStep * (float) n * 0.5f, 0.0f, 1.0f) * (float) (frames - 1);
            const int f0 = std::min ((int) middle, frames - 2);
            const float* a = tables.frame (lc.table, f0, lv.level);
            const float* b = tables.frame (lc.table, f0 + 1, lv.level);

            run ([a, b, f0] (float t, float, float s)
            {
                const float pos = t * (float) Wavetables::tableSize;
                const int i = (int) pos;
                const float fr = pos - (float) i;
                const float va = a[i] + fr * (a[i + 1] - a[i]);
                const float vb = b[i] + fr * (b[i + 1] - b[i]);
                const float mix = std::clamp (s * (float) (frames - 1) - (float) f0, 0.0f, 1.0f);
                return va + mix * (vb - va);
            });
            break;
        }

        case LayerType::fm:
        {
            switch (lc.algo)
            {
                case FmAlgo::twoOp:         renderFm<FmAlgo::twoOp> (lv, lc, outL, outR, n); break;
                case FmAlgo::threeSerial:   renderFm<FmAlgo::threeSerial> (lv, lc, outL, outR, n); break;
                case FmAlgo::threeParallel: renderFm<FmAlgo::threeParallel> (lv, lc, outL, outR, n); break;
                case FmAlgo::fourSerial:    renderFm<FmAlgo::fourSerial> (lv, lc, outL, outR, n); break;
                case FmAlgo::fourParallel:  renderFm<FmAlgo::fourParallel> (lv, lc, outL, outR, n); break;
                case FmAlgo::twoPairs:
                case FmAlgo::count:         renderFm<FmAlgo::twoPairs> (lv, lc, outL, outR, n); break;
            }
            break;
        }

        case LayerType::noise:
        {
            const float a = lv.noiseCoef;
            const bool highpass = shape0 >= 0.5f;
            const float stereoMix = lc.width;
            float yl = lv.noiseL, yr = lv.noiseR, gl = lv.gainL[0], gr = lv.gainR[0];
            const float gls = lv.gainLStep[0], grs = lv.gainRStep[0];

            for (int s = 0; s < n; ++s)
            {
                const float xl = v.rng.bipolar();
                const float xr = stereoMix > 0.0f ? xl + stereoMix * (v.rng.bipolar() - xl) : xl;
                yl += a * (xl - yl);
                yr += a * (xr - yr);
                outL[s] += (highpass ? xl - yl : yl) * gl;
                outR[s] += (highpass ? xr - yr : yr) * gr;
                gl += gls;
                gr += grs;
            }

            lv.noiseL = yl;
            lv.noiseR = yr;
            lv.gainL[0] = gl;
            lv.gainR[0] = gr;
            break;
        }
    }

    lv.shape = shape0 + shapeStep * (float) n;
}

void Synth::renderVoice (Voice& v, float* outL, float* outR, int n) noexcept
{
    const auto& c = cache;
    std::array<float, controlBlock> fl {}, fr {}, dl {}, dr {};
    bool anyFiltered = false;

    for (int l = 0; l < numLayers; ++l)
    {
        const auto& lc = c.layers[(size_t) l];

        if (! lc.on)
            continue;

        const bool filtered = lc.toFilter && c.filterOn;
        anyFiltered |= filtered;
        renderLayer (v, l, filtered ? fl.data() : dl.data(), filtered ? fr.data() : dr.data(), n);
    }

    if (c.subOn)
    {
        float ph = v.subPhase, dt = v.subInc, g = v.subGain;
        const float* sawTable = tables.basic (Wavetables::basicSaw, v.subLevel);
        const float* triTable = tables.basic (Wavetables::basicTriangle, v.subLevel);

        for (int s = 0; s < n; ++s)
        {
            float x = 0.0f;

            switch (c.subWave)
            {
                case SubWave::sine:     x = sine (ph); break;
                case SubWave::triangle: x = readTable (triTable, ph); break;
                case SubWave::square:
                {
                    float half = ph + 0.5f;
                    half -= (float) (int) half;
                    x = readTable (sawTable, half) - readTable (sawTable, ph);
                    break;
                }
            }

            dl[(size_t) s] += x * g;
            dr[(size_t) s] += x * g;
            ph += dt;

            if (ph >= 1.0f)
                ph -= 1.0f;

            dt += v.subIncStep;
            g += v.subGainStep;
        }

        v.subPhase = ph;
        v.subInc = dt;
        v.subGain = g;
    }

    if (anyFiltered && c.filterInputMono)
    {
        // Entrada igual nos dois lados: filtra um canal e copia (metade das contas do filtro).
        const bool drive = v.driveGain > 1.001f;

        for (int s = 0; s < n; ++s)
        {
            float x = fl[(size_t) s];

            if (drive)
                x = fastTanh (x * v.driveGain) * v.driveComp;

            x = v.filter[0][0].process (x, v.coeffs[0]);

            if (c.steep)
                x = v.filter[1][0].process (x, v.coeffs[1]);

            dl[(size_t) s] += x;
            dr[(size_t) s] += x;
        }

        // O direito acompanha: se a entrada deixar de ser mono, ele continua do mesmo ponto, sem estalo.
        v.filter[0][1] = v.filter[0][0];
        v.filter[1][1] = v.filter[1][0];
    }
    else if (anyFiltered)
    {
        const bool drive = v.driveGain > 1.001f;

        for (int s = 0; s < n; ++s)
        {
            float l = fl[(size_t) s], r = fr[(size_t) s];

            if (drive)
            {
                l = fastTanh (l * v.driveGain) * v.driveComp;
                r = fastTanh (r * v.driveGain) * v.driveComp;
            }

            l = v.filter[0][0].process (l, v.coeffs[0]);
            r = v.filter[0][1].process (r, v.coeffs[0]);

            if (c.steep)
            {
                l = v.filter[1][0].process (l, v.coeffs[1]);
                r = v.filter[1][1].process (r, v.coeffs[1]);
            }

            dl[(size_t) s] += l;
            dr[(size_t) s] += r;
        }
    }

    for (int s = 0; s < n; ++s)
    {
        const float amp = v.env[0].next (c.env[0]);

        if (c.env2Used)
            v.env[1].next (c.env[1]);

        if (c.env3Used)
            v.env[2].next (c.env[2]);

        const float g = amp * v.stealGain;
        outL[s] += dl[(size_t) s] * v.ampL * g;
        outR[s] += dr[(size_t) s] * v.ampR * g;
        v.ampL += v.ampLStep;
        v.ampR += v.ampRStep;

        if (v.stealing)
        {
            // Meio cosseno: começa e termina sem quina (um fade reto deixa um canto audível no fim).
            v.stealPos = std::min (1.0f, v.stealPos + v.stealStep);
            v.stealGain = v.stealStart * (0.5f + 0.5f * std::cos (3.14159265f * v.stealPos));

            if (v.stealPos >= 1.0f)
                v.stealGain = 0.0f;
        }
    }

    const bool silentSustain = v.env[0].getStage() == Envelope::Stage::sustain && v.env[0].getValue() < 1.0e-4f;

    if (! v.env[0].isActive() || (v.stealing && v.stealGain <= 0.0f) || silentSustain)
        v.active = false;
}

} // namespace whynot::synth
