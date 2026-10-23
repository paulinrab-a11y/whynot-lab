#include "WhynotSynth.h"
#include "SynthState.h"

namespace whynot
{

const char* WhynotSynthPlugin::xmlTypeName = "whynotSynth";

WhynotSynthPlugin::WhynotSynthPlugin (te::PluginCreationInfo info) : te::Plugin (info)
{
}

WhynotSynthPlugin::~WhynotSynthPlugin()
{
    notifyListenersOfDeletion();
}

void WhynotSynthPlugin::applyChannel (const Channel& c, double projectBpm)
{
    engine.setAll (synthstate::valuesFor (c, projectBpm));
}

double WhynotSynthPlugin::getLatencySeconds()
{
    return engine.isPrepared() && preparedRate > 0.0 ? engine.latencySamples() / preparedRate : 0.0;
}

void WhynotSynthPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    // O Tracktion só chama initialise() sem nenhum grafo usando o plugin: dá para alocar aqui.
    // Mesma taxa e bloco: mantém o estado (notas e caudas não cortam).
    if (info.sampleRate != preparedRate || info.blockSizeSamples > preparedBlock || ! engine.isPrepared())
    {
        preparedRate = info.sampleRate;
        preparedBlock = juce::jmax (info.blockSizeSamples, 512);
        engine.prepare (preparedRate, preparedBlock);
        scratch.setSize (1, preparedBlock);
    }
}

void WhynotSynthPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& buffer = *fc.destBuffer;
    const int start = fc.bufferStartSample;
    const int num = fc.bufferNumSamples;
    const int channels = buffer.getNumChannels();

    if (num <= 0 || channels == 0)
        return;

    if (! engine.isPrepared())
    {
        for (int c = 0; c < channels; ++c)
            buffer.clear (c, start, num);

        return;
    }

    int numEvents = 0;

    if (auto* midi = fc.bufferForMidiMessages)
    {
        if (midi->isAllNotesOff)
            events[(size_t) numEvents++] = { 0, synth::Event::Type::allNotesOff, 0, 0.0f };

        for (auto& m : *midi)
        {
            if (numEvents >= maxEvents)
                break;

            synth::Event e;
            e.sampleOffset = juce::jlimit (0, num - 1, juce::roundToInt ((m.getTimeStamp() + fc.midiBufferOffset) * preparedRate));

            if (m.isNoteOn())
            {
                e.type = synth::Event::Type::noteOn;
                e.key = m.getNoteNumber();
                e.value = m.getFloatVelocity();
            }
            else if (m.isNoteOff())
            {
                e.type = synth::Event::Type::noteOff;
                e.key = m.getNoteNumber();
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                e.type = synth::Event::Type::allNotesOff;
            }
            else if (m.isPitchWheel())
            {
                e.type = synth::Event::Type::pitchBend;
                e.value = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            }
            else if (m.isController() && m.getControllerNumber() == 1)
            {
                e.type = synth::Event::Type::modWheel;
                e.value = (float) m.getControllerValue() / 127.0f;
            }
            else if (m.isChannelPressure())
            {
                e.type = synth::Event::Type::aftertouch;
                e.value = (float) m.getChannelPressureValue() / 127.0f;
            }
            else
            {
                continue;
            }

            events[(size_t) numEvents++] = e;
        }
    }

    float* left = buffer.getWritePointer (0, start);
    float* right = channels > 1 ? buffer.getWritePointer (1, start)
                                : (num <= scratch.getNumSamples() ? scratch.getWritePointer (0) : left);

    engine.process (left, right, num, events.data(), numEvents);

    for (int c = 2; c < channels; ++c)
        buffer.clear (c, start, num);

    float peak = 0.0f;

    for (int i = 0; i < num; ++i)
        peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));

    level.store (juce::jmax (peak, level.load (std::memory_order_relaxed) * 0.85f), std::memory_order_relaxed);
}

} // namespace whynot
