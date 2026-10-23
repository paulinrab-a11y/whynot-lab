#pragma once

// WHYNOT Synth: o motor de som (sem JUCE).
//
// Por voz: até 4 camadas (analógico e wavetable de banda limitada por oitava, FM de 2 a 4 operadores,
// ruído), cada uma com unison de até 8 vozes; um sub; filtro SVF (src/dsp/Svf.h) de 12 ou 24 dB
// com drive; 3 envelopes ADSR com curva; 2 LFOs (livres ou no BPM); matriz de 8 rotas.
// Depois da soma: saturação, EQ, estéreo, delay e reverb (os processadores de src/dsp), volume e
// soft clip.
//
// Threads:
// - qualquer thread: setParam() / getParam() (atomics, valem a partir do próximo bloco);
// - com o áudio parado: prepare() (o único lugar que aloca);
// - thread de áudio: process() e reset(). Sem alocação, trava ou I/O.

#include "SynthDsp.h"
#include "SynthParams.h"
#include "Wavetables.h"

#include "../dsp/FxHost.h"
#include "../dsp/Svf.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace whynot::synth
{

/** Um evento MIDI já traduzido, dentro do bloco (sampleOffset em amostras desde o início). */
struct Event
{
    enum class Type : std::uint8_t { noteOn, noteOff, allNotesOff, pitchBend, modWheel, aftertouch };

    int sampleOffset = 0;
    Type type = Type::noteOn;
    int key = 0;
    float value = 0.0f;   // força 0..1, pitch bend -1..1, mod wheel e aftertouch 0..1
};

class Synth
{
public:
    static constexpr int maxPolyphony = 32;
    static constexpr int maxVoices = 40;       // a polifonia limita as notas; as vozes a mais cobrem os fades de roubo
    static constexpr int controlBlock = 16;    // modulação, afinação e filtro andam a cada 16 amostras
    static constexpr float voiceScale = 0.5f;  // margem: um acorde de 4 notas cabe sem clipar

    Synth();
    ~Synth();

    //--------------------------------------------------------------------------
    // Qualquer thread
    void setParam (int index, float value) noexcept;
    float getParam (int index) const noexcept;
    void setAll (std::span<const float> values) noexcept;
    std::vector<float> getAll() const;

    //--------------------------------------------------------------------------
    // Com o áudio parado
    void prepare (double sampleRate, int maxBlockSize);
    bool isPrepared() const noexcept          { return prepared; }
    double getSampleRate() const noexcept     { return sampleRate; }

    /** Atraso fixo da saída (a saturação sobreamostrada), para a compensação do motor. */
    int latencySamples() const noexcept       { return latency; }

    /** Cauda depois da última nota solta (soltura + delay + reverb), pelos valores atuais. */
    double tailSeconds() const noexcept;

    //--------------------------------------------------------------------------
    // Thread de áudio
    void process (float* left, float* right, int numSamples, const Event* events = nullptr, int numEvents = 0) noexcept;

    /** Silencia na hora: vozes, filtros e caudas dos efeitos. */
    void reset() noexcept;

    /** Vozes soando agora (inclui as que estão saindo com fade). Para testes e medidores. */
    int activeVoices() const noexcept;

    /** Picos do último bloco antes do soft clip (o teste confere que os presets não dependem dele). */
    float lastPeakBeforeClip() const noexcept { return peakBeforeClip; }

private:
    //==========================================================================
    struct LayerCache
    {
        bool on = false, toFilter = true;
        LayerType type = LayerType::analog;
        Wave wave = Wave::saw;
        FmAlgo algo = FmAlgo::twoOp;
        float gain = 0.0f, pan = 0.0f, offset = 0.0f;   // offset em semitons (oitava + semitom + fino)
        int voices = 1, table = 0;
        float detune = 0.0f, width = 0.0f, shape = 0.5f, phaseRandom = 0.0f;
        std::array<float, 3> ratio {}, index {};        // FM: razão e índice (em ciclos) de B, C, D
        float feedback = 0.0f, fmDecayCoef = 0.0f, fmDecayBlock = 0.0f, fmSustain = 0.5f;   // fmDecayBlock: o decaimento de um passo de controle
        std::array<float, maxUnison> spread {}, panSpread {};
        std::array<float, maxUnison> unisonPanL {}, unisonPanR {};   // ganhos de pan de cada voz do unison (sem o volume)
        bool monoSource = true;   // esquerdo e direito saem iguais (centro, sem abertura): o filtro roda um canal só
    };

    struct Slot { int src = 0, dst = 0; float amount = 0.0f; };

    struct Cache
    {
        std::array<LayerCache, numLayers> layers;
        bool subOn = false; float subGain = 0.0f, subRatio = 0.5f; SubWave subWave = SubWave::sine;
        bool filterOn = true; FilterMode filterMode = FilterMode::lowpass; bool steep = true, filterInputMono = false;
        float cutoffNote = 135.0f, reso = 10.0f, drive = 0.0f, keyTrack = 0.0f, envOctaves = 0.0f, velToCutoff = 0.0f;
        std::array<EnvelopeCoeffs, numEnvs> env;
        bool env2Used = true, env3Used = true;   // alguém lê (filtro ou matriz)? Se não, não andam
        std::array<LfoShape, numLfos> lfoShape {};
        std::array<bool, numLfos> lfoSync {}, lfoRetrig {}, lfoUsed {};   // lfoUsed: alguma rota lê o LFO
        std::array<float, numLfos> lfoRate {}, lfoFadeSamples {};
        std::array<double, numLfos> lfoBeats {};
        std::array<Slot, numSlots> slots {};
        int numSlotsUsed = 0;
        float volumeDb = 0.0f, velAmp = 0.5f, bendRange = 2.0f, drift = 0.1f, glideMs = 80.0f, bpm = 120.0f;
        int poly = 16;
        bool legato = true, glideAlways = false, mono = false;
    };

    struct LayerVoice
    {
        std::array<float, maxUnison> phase {}, inc {}, incStep {}, gainL {}, gainR {}, gainLStep {}, gainRStep {}, drift {};
        std::array<std::array<float, maxUnison>, 3> opPhase {};
        std::array<float, maxUnison> fb1 {}, fb2 {};
        std::array<float, 3> index {};      // FM: índice efetivo (ciclos) de B, C, D
        float shape = 0.5f, shapeStep = 0.0f;
        float fmEnv = 1.0f, feedback = 0.0f;
        float noiseL = 0.0f, noiseR = 0.0f, noiseCoef = 0.5f;
        int level = 0;

        // Contas guardadas: refeitas só quando o valor muda (rebuildCache e startVoice zeram com -1).
        std::array<float, maxUnison> detuneRatio {};   // 2^(centésimos / 1200) de cada voz do unison
        float ratioDetune = -1.0f, noiseShape = -1.0f;
    };

    struct Voice
    {
        bool active = false, held = false, stealing = false, fresh = true;
        int key = 60;
        float velocity = 1.0f;
        std::uint32_t order = 0;

        float note = 60.0f, glideFrom = 60.0f, glideDelta = 0.0f;
        int glideTotal = 0, glideLeft = 0;

        std::array<Envelope, numEnvs> env;
        std::array<float, numLfos> lfoPhase {}, lfoValue {}, lfoHeld {}, lfoPrevHeld {};
        int age = 0;
        float random = 0.0f;

        std::array<std::array<dsp::Svf, 2>, 2> filter;   // [seção][canal]
        std::array<dsp::SvfCoeffs, 2> coeffs;
        double coeffsHz = -1.0;          // corte e ressonância dos coeffs atuais (-1: refazer)
        float coeffsReso = -1.0f;
        float driveGain = 1.0f, driveComp = 1.0f;

        float ampL = 0.0f, ampR = 0.0f, ampLStep = 0.0f, ampRStep = 0.0f;
        float stealGain = 1.0f, stealStart = 1.0f, stealPos = 0.0f, stealStep = 0.0f;

        std::array<LayerVoice, numLayers> layers;
        float subPhase = 0.0f, subInc = 0.0f, subIncStep = 0.0f, subGain = 0.0f, subGainStep = 0.0f;
        int subLevel = 0;
        Random rng;
    };

    //==========================================================================
    void applyParams() noexcept;
    void rebuildCache() noexcept;
    void updateEffects() noexcept;
    void handle (const Event&) noexcept;
    void render (float* left, float* right, int numSamples) noexcept;

    void noteOn (int key, float velocity) noexcept;
    void noteOff (int key) noexcept;
    void allNotesOff() noexcept;

    int allocateVoice() noexcept;
    void startVoice (Voice&, int key, float velocity, float glideFromNote) noexcept;
    void retrigger (Voice&, int key, float velocity, bool glide, bool restartEnvelopes) noexcept;
    void releaseVoice (Voice&) noexcept;
    void beginSteal (Voice&) noexcept;

    void pushHeld (int key) noexcept;
    void removeHeld (int key) noexcept;

    void controlUpdate (Voice&, int numSamples) noexcept;
    void renderVoice (Voice&, float* left, float* right, int numSamples) noexcept;
    void renderLayer (Voice&, int layerIndex, float* outL, float* outR, int numSamples) noexcept;

    /** Uma camada FM: um laço por algoritmo, sem decidir o algoritmo a cada amostra. */
    template <FmAlgo algo>
    void renderFm (LayerVoice&, const LayerCache&, float* outL, float* outR, int numSamples) const noexcept;

    //==========================================================================
    std::unique_ptr<std::atomic<float>[]> values;
    std::array<float, numParams> applied {};
    Cache cache;

    double sampleRate = 48000.0;
    int maxBlock = 512, latency = 0;
    bool prepared = false, firstBlock = true;

    std::array<Voice, maxVoices> voices;
    std::uint32_t voiceCounter = 0;
    float bend = 0.0f, modWheel = 0.0f, aftertouch = 0.0f;
    std::array<double, numLfos> freePhase {};
    Random rng { 0x51f15eedu };

    // Mono (808, baixos, leads): pilha das teclas seguradas, sem alocação.
    std::array<int, 16> held {};
    int numHeld = 0, monoVoice = -1, monoKey = -1;
    float lastNote = -1.0f;
    bool lastMono = false;

    // Efeitos (processadores de src/dsp, com bypass sem estalo do FxHost).
    std::unique_ptr<dsp::FxHost> sat, eq, stereo, delay, reverb;
    std::vector<float> fxLeft, fxRight;
    dsp::Smoothed volume;
    float peakBeforeClip = 0.0f;

    const Wavetables& tables;
    const SineTable& sine;
};

} // namespace whynot::synth
