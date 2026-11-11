#include "WhynotSynth.h"
#include "SynthState.h"

#include "../audio/BlockSlices.h"

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
    float peak = 0.0f;

    if (channels > 1)
    {
        float* right = buffer.getWritePointer (1, start);
        engine.process (left, right, num, events.data(), numEvents);

        for (int c = 2; c < channels; ++c)
            buffer.clear (c, start, num);

        for (int i = 0; i < num; ++i)
            peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));
    }
    else
    {
        // Trilha mono: o lado direito vai para o rascunho. O Tracktion pode mandar um bloco maior que o do
        // initialise(): em pedaços do tamanho do rascunho (sem alocar), cada um com os eventos dele. Antes o direito
        // virava o próprio esquerdo e o motor somava os dois lados no mesmo buffer (som dobrado). O bloco que cabe
        // é um pedaço só, com todos os eventos.
        if (scratch.getNumChannels() == 0 || scratch.getNumSamples() == 0)
        {
            buffer.clear (0, start, num);
            return;
        }

        float* right = scratch.getWritePointer (0);
        int cursor = 0;

        audio::forEachSlice (num, scratch.getNumSamples(), [&] (int offset, int n)
        {
            const int first = cursor;
            const int count = audio::takeSliceEvents (events.data(), numEvents, cursor, offset, n, offset + n >= num,
                                                      &synth::Event::sampleOffset);
            engine.process (left + offset, right, n, events.data() + first, count);

            for (int i = 0; i < n; ++i)
                peak = juce::jmax (peak, std::abs (left[offset + i]), std::abs (right[i]));
        });
    }

    level.store (juce::jmax (peak, level.load (std::memory_order_relaxed) * 0.85f), std::memory_order_relaxed);
}

} // namespace whynot
