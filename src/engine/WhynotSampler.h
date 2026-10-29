#pragma once

#include <JuceHeader.h>

#include "../audio/ChannelPitch.h"
#include "../audio/RtObjectSlot.h"
#include "../audio/SampleVoice.h"

#include <array>
#include <atomic>

namespace whynot
{

namespace te = tracktion;

/** Forma da nota de um canal do sampler (afinação, envelope, queda de tom, início). */
struct SamplerShape
{
    float tuneSemis = 0.0f;       // afinação do canal menos o desvio do sample (rootCents)
    float attackMs = 1.0f;
    float decayMs = 300.0f;
    float sustain = 1.0f;
    float pitchDropSemis = 0.0f;
    float pitchDropMs = 60.0f;
    float start = 0.0f;
    bool loop = false;            // laço entre loopStart e loopEnd (frações do sample) enquanto a nota soa
    float loopStart = 0.0f;
    float loopEnd = 1.0f;
    float loopXfadeMs = 20.0f;
    float releaseMs = 30.0f;      // rampa de saída quando a nota acaba (num canal one-shot, 5 ms fixos)
    float bendRange = 2.0f;       // semitons do pitch wheel inteiro (o bendRange do canal, 27/10); o RPN 0 também muda
};

/** O sampler nativo da WHYNOT, como plugin interno do Tracktion.

    Thread de áudio: applyToBuffer() — sem alocação, sem trava, sem I/O.
    Thread de mensagem: setSample(), setChannel() e o Timer de coleta.

    O sample chega por RtObjectSlot: a thread de áudio só troca ponteiros, e o
    sample antigo é liberado aqui, na thread de mensagem. Com vozes soando, a troca
    espera: a thread de áudio as tira em 3 ms (fadeForSwap) e avisa quando ficou em
    silêncio (voicesSilent); o Timer publica o sample novo. Sem isso as vozes passavam
    a ler o sample novo na posição do velho, com um corte seco.

    Pitch bend e mod wheel desenhados na nota (27/10): o pitch wheel (no bendRange do canal, ou no RPN 0 do MIDI)
    e o CC1 (um vibrato leve) viram um desvio de tom do canal (audio::ChannelPitch) que todas as vozes somam; sem
    bend nem roda, as vozes não recebem nada e soam como antes, inclusive o glide do 808.

    Premissa: o Tracktion processa cada nó do grafo uma vez por bloco, então
    applyToBuffer() nunca roda em duas threads ao mesmo tempo (é o consumidor
    único da fila do RtObjectSlot).
*/
class WhynotSamplerPlugin : public te::Plugin,
                            private juce::Timer
{
public:
    WhynotSamplerPlugin (te::PluginCreationInfo);
    ~WhynotSamplerPlugin() override;

    static const char* xmlTypeName;

    juce::String getName() const override              { return "WHYNOT Sampler"; }
    juce::String getPluginType() override              { return xmlTypeName; }
    juce::String getVendor() override                  { return "WHYNOT LAB"; }
    juce::String getSelectableDescription() override   { return getName(); }

    bool isSynth() override                            { return true; }
    bool takesMidiInput() override                     { return true; }
    bool takesAudioInput() override                    { return false; }
    bool producesAudioWhenNoAudioInput() override      { return true; }
    bool noTail() override                             { return false; }
    double getTailLength() const override              { return 4.0; }
    int getNumOutputChannelsGivenInputs (int) override { return 2; }
    BusLayout getBusses() const override               { return BusLayout::singleStereoInOut(); }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** Uma camada por força (bloco 4f): o sample e a força mínima, de 0 a 1 (a força MIDI / 127). */
    struct Layer
    {
        std::shared_ptr<const audio::SampleData> data;
        float minVelocity = 1.0f;
    };

    static constexpr int maxLayers = 3;

    // Thread de mensagem
    /** O sample do canal e as camadas por força (em ordem de força; as que passam de maxLayers ficam de fora). A troca
        é uma só: as vozes saem do conjunto velho antes. */
    void setSample (std::shared_ptr<const audio::SampleData>, std::vector<Layer> layers = {});
    void setChannel (int rootKey, bool oneShot, bool cutItself, bool mono, float glideMs);
    void setShape (const SamplerShape&);

    /** Fatiador: o começo de cada fatia (frações do sample, em ordem) e a tecla da primeira.
        Vazio = o sample inteiro em qualquer tecla, como sempre. */
    void setSlices (const std::vector<float>& starts, int firstKey);
    bool hasSample() const noexcept   { return sampleLoaded; }

private:
    struct SampleRef
    {
        std::shared_ptr<const audio::SampleData> data;
        std::array<Layer, maxLayers> layers {};
        int numLayers = 0;

        /** O sample da força: a camada mais forte que a nota alcança, ou o do canal. */
        const audio::SampleData& forVelocity (float velocity) const noexcept
        {
            const audio::SampleData* chosen = data.get();

            for (int i = 0; i < numLayers; ++i)
                if (velocity + 1.0e-4f >= layers[(size_t) i].minVelocity && layers[(size_t) i].data != nullptr)
                    chosen = layers[(size_t) i].data.get();

            return *chosen;
        }
    };

    static constexpr int maxSlices = 128;

    // 32 vozes tornam raro o roubo (que corta a nota antiga seco); roubo com fade fica para a 3.2.
    static constexpr int numVoices = 32;

    static_assert (std::atomic<double>::is_always_lock_free, "o atomic<double> viraria uma trava escondida na thread de audio");

    void timerCallback() override;
    void tryPublishPending();

    audio::VoiceParams voiceParams() const noexcept;
    // Thread de áudio: os pans (CC 10) que chegaram, com a amostra em que chegaram. Uma nota usa o da frente
    // só se ele chegou no mesmo instante (até 8 amostras antes); o que ficou para trás é jogado fora.
    struct PendingPan { std::int64_t at = 0; float pan = 0.0f; };
    std::array<PendingPan, 16> pendingPans {};
    int pendingCount = 0;
    std::int64_t sampleClock = 0;
    float takePan (std::int64_t now) noexcept;
    void noteOn (int key, float velocity, const SampleRef&, float pan = 0.0f) noexcept;
    void noteOff (int key, const SampleRef&) noexcept;
    int allocateVoice() noexcept;
    void pushHeld (int key, float velocity) noexcept;
    void removeHeld (int key) noexcept;
    void releaseAll() noexcept;
    void chokeAll() noexcept;
    void renderVoices (const audio::SampleData&, float* const* out, int numChannels, int start, int num) noexcept;
    bool anyVoiceActive() const noexcept;

    // Só a thread de áudio
    std::array<audio::SampleVoice, numVoices> voices;
    std::array<int, numVoices> voiceKey {};
    std::array<const audio::SampleData*, numVoices> voiceData {};   // a camada que cada voz lê (nulo = a do canal)
    std::array<juce::uint32, numVoices> voiceStart {};
    juce::uint32 voiceCounter = 0;
    double audioSampleRate = 48000.0;

    // Pitch bend e mod wheel desenhados na nota (27/10): o desvio de tom do canal, em pedaços de até 256 amostras.
    audio::ChannelPitch pitch;
    std::array<float, 256> pitchOffsets {};
    float appliedBendRange = -1.0f;   // o bendRange do canal que a thread de áudio já aplicou

    // Mono (808): pilha das teclas seguradas, sem alocação. Nota nova com outra segurada = glide.
    // A força de cada uma fica guardada: voltar deslizando para ela usa a força dela, não 127.
    std::array<int, 16> held {};
    std::array<float, 16> heldVelocity {};
    int numHeld = 0;
    int monoVoice = -1;
    int monoKey = -1;
    bool lastMono = false;

    // Mensagem → áudio
    audio::RtObjectSlot<SampleRef> sampleSlot;
    std::unique_ptr<const SampleRef> pendingSample;   // só a thread de mensagem
    std::atomic<double> requestedSampleRate { 48000.0 };
    std::atomic<bool> needsPrepare { true };
    std::atomic<int> rootKey { 60 };
    std::atomic<bool> oneShot { true };
    std::atomic<bool> cutItself { false };
    std::atomic<bool> mono { false };
    std::atomic<float> glideMs { 80.0f };
    std::atomic<float> tuneSemis { 0.0f }, attackMs { 1.0f }, decayMs { 300.0f }, sustain { 1.0f };
    std::atomic<float> pitchDropSemis { 0.0f }, pitchDropMs { 60.0f }, startFraction { 0.0f };
    std::atomic<bool> loop { false };
    std::atomic<float> loopStart { 0.0f }, loopEnd { 1.0f }, loopXfadeMs { 20.0f }, releaseMs { 30.0f };
    std::atomic<float> bendRange { 2.0f };

    // Troca de sample com vozes soando (mensagem <-> áudio).
    std::atomic<bool> fadeForSwap { false };     // mensagem -> áudio: tire as vozes, um sample novo espera
    std::atomic<bool> voicesSilent { true };     // áudio -> mensagem: no fim do último bloco, nenhuma voz soava
    std::atomic<juce::uint32> blocksRendered { 0 };
    juce::uint32 blocksSeen = 0;                 // só a thread de mensagem: o contador no último Timer
    bool audioRunning = false;                   // só a thread de mensagem: os blocos andaram desde o último Timer
    bool swapFading = false;                     // só a thread de áudio: as vozes já estão saindo
    const SampleRef* lastRef = nullptr;          // só a thread de áudio: o sample do bloco anterior

    // Fatias: a thread de mensagem escreve numSlices = 0, os começos e depois o número novo; a de áudio
    // lê o número e só então os começos. Uma nota no meio da troca toca a fatia velha ou a nova, nunca lixo.
    std::array<std::atomic<float>, maxSlices> sliceAt {};
    std::atomic<int> numSlices { 0 }, sliceKey { 60 };
    std::vector<float> publishedSlices;   // só a thread de mensagem: o que já foi escrito
    int publishedSliceKey = 60;
    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free,
                   "os parametros do canal precisam ser atomics sem trava");
    bool sampleLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhynotSamplerPlugin)
};

} // namespace whynot
