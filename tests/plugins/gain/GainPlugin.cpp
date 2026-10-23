// WHYNOT Teste Ganho: efeito VST3 mínimo para o autoteste da WHYNOT LAB.
// Um parâmetro (Ganho, em dB), estado salvo, editor genérico. Nada de alocação ou trava no processBlock.

#include <JuceHeader.h>

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Ganho",
                                                                 juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("dB")));
        return layout;
    }
}

class WhynotTestGain final : public juce::AudioProcessor
{
public:
    WhynotTestGain()
        : juce::AudioProcessor (BusesProperties().withInput ("Entrada", juce::AudioChannelSet::stereo(), true)
                                                 .withOutput (juce::String::fromUTF8 ("Saída"), juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "WHYNOT_TEST_GAIN", makeLayout())
    {
        gainDb = state.getRawParameterValue ("gain");
    }

    const juce::String getName() const override   { return "WHYNOT Teste Ganho"; }

    void prepareToPlay (double sampleRate, int) override
    {
        smoothed.reset (sampleRate, 0.02);
        smoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
               && layouts.getMainInputChannelSet() == out;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        smoothed.setTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = smoothed.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }

    bool hasEditor() const override                   { return true; }
    juce::AudioProcessorEditor* createEditor() override   { return new juce::GenericAudioProcessorEditor (*this); }

    bool acceptsMidi() const override                 { return false; }
    bool producesMidi() const override                { return false; }
    double getTailLengthSeconds() const override      { return 0.0; }

    int getNumPrograms() override                     { return 1; }
    int getCurrentProgram() override                  { return 0; }
    void setCurrentProgram (int) override             {}
    const juce::String getProgramName (int) override  { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = state.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
    }

private:
    juce::AudioProcessorValueTreeState state;
    std::atomic<float>* gainDb = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhynotTestGain)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WhynotTestGain();
}
