#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"
#include "../synth/Synth.h"

#include <array>

namespace whynot
{

namespace te = tracktion;

/** O WHYNOT Synth como plugin interno do Tracktion (instrumento de um canal "synth").

    O som mora em synth::Synth (sem JUCE, testado à parte); aqui só ligamos MIDI e buffers.
    - Thread de mensagem: applyChannel() (atomics), leitura do medidor.
    - initialise(): o Tracktion chama sem grafo usando o plugin; é onde o motor aloca.
    - applyToBuffer(): thread de áudio, sem alocação, trava ou I/O. */
class WhynotSynthPlugin : public te::Plugin
{
public:
    WhynotSynthPlugin (te::PluginCreationInfo);
    ~WhynotSynthPlugin() override;

    static const char* xmlTypeName;

    /** Valores do canal (parâmetros, mono, glide) e o BPM do projeto. */
    void applyChannel (const Channel&, double projectBpm);

    void setOwner (const juce::String& channelId)      { ownerChannel = channelId; }
    const juce::String& getOwnerChannel() const noexcept { return ownerChannel; }

    synth::Synth& getSynth() noexcept                  { return engine; }

    /** Nível de saída recente (0..1, pico), para a janela. */
    float outputLevel() const noexcept                 { return level.load (std::memory_order_relaxed); }

    juce::String getName() const override              { return "WHYNOT Synth"; }
    juce::String getPluginType() override              { return xmlTypeName; }
    juce::String getVendor() override                  { return "WHYNOT LAB"; }
    juce::String getSelectableDescription() override   { return getName(); }

    bool isSynth() override                            { return true; }
    bool takesMidiInput() override                     { return true; }
    bool takesAudioInput() override                    { return false; }
    bool producesAudioWhenNoAudioInput() override      { return true; }
    bool noTail() override                             { return false; }
    double getTailLength() const override              { return engine.tailSeconds(); }
    double getLatencySeconds() override;
    int getNumOutputChannelsGivenInputs (int) override { return 2; }
    BusLayout getBusses() const override               { return BusLayout::singleStereoInOut(); }
    bool canBeAddedToClip() override                   { return false; }
    bool canBeAddedToRack() override                   { return false; }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override                       {}
    void applyToBuffer (const te::PluginRenderContext&) override;

private:
    static constexpr int maxEvents = 1024;

    synth::Synth engine;
    juce::String ownerChannel;
    double preparedRate = 0.0;
    int preparedBlock = 0;
    std::array<synth::Event, maxEvents> events {};
    juce::AudioBuffer<float> scratch;
    std::atomic<float> level { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhynotSynthPlugin)
};

} // namespace whynot
