#pragma once

#include <JuceHeader.h>

#include "../audio/RtObjectSlot.h"

#include <atomic>

namespace whynot
{

/** Toca a prévia de um arquivo do navegador direto na saída de áudio, por fora do Edit.

    O áudio é decodificado na thread de mensagem (até 30 s) e entregue à thread de áudio por um
    RtObjectSlot: no callback não há alocação, trava nem I/O. O JUCE soma a saída deste callback
    com a do Tracktion, então a prévia toca junto com a música.
*/
class PreviewPlayer : public juce::AudioIODeviceCallback
{
public:
    PreviewPlayer() = default;

    /** Thread de mensagem: decodifica e começa a tocar. Devolve false se o arquivo não abriu. */
    bool play (const juce::File&, juce::AudioFormatManager&);

    /** Thread de mensagem: para a prévia (com uma rampa curta, sem estalo). */
    void stop() noexcept;

    /** Thread de mensagem: true enquanto a última prévia pedida ainda soa (ou nem começou). */
    bool isPlaying() const noexcept   { return lastId != 0 && finishedId.load() != lastId; }

    /** Thread de mensagem, de tempos em tempos (100 ms): devolve à memória as prévias que já saíram
        e reenvia a pendente, se o áudio estava atrasado. */
    void service() noexcept;

    // juce::AudioIODeviceCallback (thread de áudio)
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

private:
    struct Clip
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        juce::uint32 id = 0;
    };

    audio::RtObjectSlot<Clip> slot;
    std::unique_ptr<const Clip> pending;   // "o mais novo vence" se o slot estiver cheio
    std::atomic<juce::uint32> stopRequests { 0 }, finishedId { 0 };
    std::atomic<double> deviceRate { 48000.0 };
    juce::uint32 nextId = 1, lastId = 0;

    // só a thread de áudio
    juce::uint32 playingId = 0, seenStops = 0;
    double position = 0.0;
    float fade = 1.0f;
    bool stopping = false, finished = true;

    static constexpr float gain = 0.5f;          // -6 dB: a prévia não assusta
    static constexpr double maxSeconds = 30.0;
};

} // namespace whynot
