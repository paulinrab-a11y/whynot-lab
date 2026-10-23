// WHYNOT Teste Synth: instrumento VST3 mínimo para o autoteste da WHYNOT LAB.
// Seno afinado em A4 = 440 Hz, envelope curto, 8 vozes e um parâmetro de volume.

#include <JuceHeader.h>

namespace
{
    struct SineSound final : juce::SynthesiserSound
    {
        bool appliesToNote (int) override      { return true; }
        bool appliesToChannel (int) override   { return true; }
    };

    struct SineVoice final : juce::SynthesiserVoice
    {
        bool canPlaySound (juce::SynthesiserSound* s) override   { return dynamic_cast<SineSound*> (s) != nullptr; }

        void setCurrentPlaybackSampleRate (double rate) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (rate);

            if (rate > 0.0)
            {
                adsr.setSampleRate (rate);
                adsr.setParameters ({ 0.005f, 0.0f, 1.0f, 0.05f });
            }
        }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            const double hz = juce::MidiMessage::getMidiNoteInHertz (note);
            delta = juce::MathConstants<double>::twoPi * hz / getSampleRate();
            phase = 0.0;
            level = velocity;
            adsr.noteOn();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
                adsr.noteOff();
            else
            {
                adsr.reset();
                clearCurrentNote();
            }
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive())
                return;

            for (int i = start; i < start + num; ++i)
            {
                const float s = (float) std::sin (phase) * level * adsr.getNextSample();
                phase += delta;

                if (phase > juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;

                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, i, s);

                if (! adsr.isActive())
                {
                    clearCurrentNote();
                    break;
                }
            }
        }

        juce::ADSR adsr;
        double phase = 0.0, delta = 0.0;
        float level = 0.0f;
    };

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "volume", 1 }, "Volume",
                                                                 juce::NormalisableRange<float> (-24.0f, 0.0f, 0.01f), -6.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("dB")));
        return layout;
    }
}

class WhynotTestSynth final : public juce::AudioProcessor
{
public:
    WhynotTestSynth()
        : juce::AudioProcessor (BusesProperties().withOutput (juce::String::fromUTF8 ("Saída"), juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "WHYNOT_TEST_SYNTH", makeLayout())
    {
        volumeDb = state.getRawParameterValue ("volume");

        for (int i = 0; i < 8; ++i)
            synth.addVoice (new SineVoice());

        synth.addSound (new SineSound());
    }

    const juce::String getName() const override   { return "WHYNOT Teste Synth"; }

    void prepareToPlay (double sampleRate, int) override
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
        synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
        buffer.applyGain (juce::Decibels::decibelsToGain (volumeDb->load()));
    }

    bool hasEditor() const override                   { return true; }
    juce::AudioProcessorEditor* createEditor() override   { return new juce::GenericAudioProcessorEditor (*this); }

    bool acceptsMidi() const override                 { return true; }
    bool producesMidi() const override                { return false; }
    double getTailLengthSeconds() const override      { return 0.05; }

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
    std::atomic<float>* volumeDb = nullptr;
    juce::Synthesiser synth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhynotTestSynth)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WhynotTestSynth();
}
