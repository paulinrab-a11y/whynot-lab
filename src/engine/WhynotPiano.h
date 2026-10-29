#pragma once

#include <JuceHeader.h>

#include "../audio/ChannelPitch.h"
#include "../audio/PianoPresets.h"
#include "../model/Project.h"
#include "PianoLibrary.h"

#include <array>
#include <mutex>

namespace whynot
{

namespace te = tracktion;

/** O WHYNOT Piano como plugin interno do Tracktion: o instrumento de um canal com instrument "piano".

    O som de cada nota mora em audio::PianoVoice (sem JUCE, testado à parte). Aqui ficam o MIDI, o
    pedal, a escolha das vozes e o streamer:
    - Thread de mensagem: applyChannel() (atomics) e os medidores.
    - Pitch bend e mod wheel desenhados na nota (27/10): o pitch wheel (no bendRange do canal, ou no RPN 0) e o
      vibrato do CC1 viram um desvio de tom do canal (audio::ChannelPitch) que cada voz soma, como no sampler.
    - Streamer (thread própria): enche o anel de cada voz com o resto do sample, lendo os arquivos
      mapeados. Uma passada a cada ~2 ms; o começo de 0,37 s na memória cobre a espera.
    - applyToBuffer(): thread de áudio, sem alocação, trava ou I/O. Só no render offline (exportar)
      ela mesma enche os anéis, com a trava: ali não há tempo real e nenhuma nota pode faltar. */
class WhynotPianoPlugin : public te::Plugin
{
public:
    WhynotPianoPlugin (te::PluginCreationInfo);
    ~WhynotPianoPlugin() override;

    static const char* xmlTypeName;
    static constexpr int maxVoices = 64;

    /** Timbre (preset), afinação e alcance do pitch bend do canal. */
    void applyChannel (const Channel&);

    void setOwner (const juce::String& channelId)        { ownerChannel = channelId; }
    const juce::String& getOwnerChannel() const noexcept   { return ownerChannel; }

    bool hasLibrary() const noexcept                      { return library != nullptr; }
    const juce::String& getLoadError() const noexcept     { return loadError; }

    /** Quadros que faltaram no anel desde a criação (0 = o streamer sempre chegou a tempo). */
    std::uint32_t underruns() const noexcept              { return missed.load (std::memory_order_relaxed); }
    int activeVoices() const noexcept                     { return voicesInUse.load (std::memory_order_relaxed); }
    float outputLevel() const noexcept                    { return level.load (std::memory_order_relaxed); }

    juce::String getName() const override                 { return "WHYNOT Piano"; }
    juce::String getPluginType() override                 { return xmlTypeName; }
    juce::String getVendor() override                     { return "WHYNOT LAB"; }
    juce::String getSelectableDescription() override      { return getName(); }

    bool isSynth() override                               { return true; }
    bool takesMidiInput() override                        { return true; }
    bool takesAudioInput() override                       { return false; }
    bool producesAudioWhenNoAudioInput() override         { return true; }
    bool noTail() override                                { return false; }
    double getTailLength() const override                 { return 8.0; }   // as cordas sem abafador (5 s) e as ressonâncias
    int getNumOutputChannelsGivenInputs (int) override    { return 2; }
    BusLayout getBusses() const override                  { return BusLayout::singleStereoInOut(); }
    bool canBeAddedToClip() override                      { return false; }
    bool canBeAddedToRack() override                      { return false; }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override                          {}
    void applyToBuffer (const te::PluginRenderContext&) override;

private:
    struct Voice
    {
        audio::PianoVoice voice;
        audio::VoiceStream stream;
        int key = -1;
        float velocity01 = 0.0f;    // depois da curva do preset
        int layerVelocity = 0;      // 1 a 127: escolhe a camada e o release
        bool keyDown = false;       // a tecla ainda está apertada
        bool held = false;          // solta, mas o pedal segura
        bool isRelease = false;     // sample de release: toca até o fim
        bool fading = false;        // saindo por excesso de vozes
        std::int64_t startedAt = 0; // em amostras deste plugin
        std::uint64_t order = 0;    // para achar a mais antiga
    };

    struct Event
    {
        enum class Type { noteOn, noteOff, sustain, allNotesOff, allSoundOff, pitchWheel, controller };
        int offset = 0, index = 0;
        Type type = Type::noteOn;
        int key = 0, value = 0;
    };

    struct Settings
    {
        float velocityCurve = 1.0f, toneHz = 0.0f, width = 1.0f, releaseScale = 1.0f, drive = 0.0f, tune = 0.0f;
        bool hammer = true;
    };

    class Streamer;

    void service();   // com streamLock
    void handle (const Event&, std::int64_t now, const Settings&);
    void noteOn (int key, int velocity127, std::int64_t now, const Settings&);
    void noteOff (int key, std::int64_t now, const Settings&);
    void damp (Voice&, std::int64_t now, const Settings&, bool withReleaseSamples);
    void startReleases (int key, int layer, float velocity01, double heldSeconds, std::int64_t now, const Settings&);
    int allocate();
    void limitPolyphony (int justStarted);
    void renderVoices (float* left, float* right, int from, int to);
    void postProcess (float* left, float* right, int num, const Settings&);

    std::shared_ptr<const piano::Library> library;
    juce::String loadError, ownerChannel;

    std::unique_ptr<Voice[]> voices;
    std::vector<audio::StreamCursor> cursors;   // do streamer (ou do render offline, com a trava)
    juce::AudioBuffer<float> chunk;             // idem
    std::mutex streamLock;
    std::unique_ptr<Streamer> streamer;

    // thread de mensagem → áudio
    std::atomic<float> velocityCurve { 1.0f }, toneHz { 0.0f }, width { 1.0f }, releaseScale { 1.0f }, drive { 0.0f }, tune { 0.0f };
    std::atomic<bool> hammer { true };
    std::atomic<float> bendRange { 2.0f };

    // thread de áudio
    static constexpr int maxEvents = 1024;
    std::array<Event, maxEvents> events {};
    double preparedRate = 0.0;
    bool prepared = false, rendering = false, pedalDown = false;
    std::int64_t clock = 0;
    std::uint64_t voiceOrder = 0;
    std::uint32_t generation = 0, blockMissed = 0;
    float lowpass[2][2] {};                     // dois polos por canal
    float lastWidth = 1.0f, lastDrive = 0.0f;
    audio::ChannelPitch pitch;                  // o bend e o vibrato do canal (27/10)
    std::array<float, 1024> pitchOffsets {};    // um pedaço do render (renderChunk)
    float appliedBendRange = -1.0f;

    std::atomic<std::uint32_t> missed { 0 };
    std::atomic<int> voicesInUse { 0 };
    std::atomic<float> level { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhynotPianoPlugin)
};

} // namespace whynot
