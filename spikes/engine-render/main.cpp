// Prova de conceito: cria um Edit do Tracktion a 140 BPM, toca 8 notas no
// sintetizador interno 4OSC, renderiza para WAV e mede o arquivo gerado.
// Uso: whynot_render_spike [saida.wav]

#include <JuceHeader.h>

namespace te = tracktion;

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto outFile = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                  : juce::File::getCurrentWorkingDirectory().getChildFile ("spike.wav");

    te::Engine engine { "WHYNOT LAB Spike" };

    auto edit = te::Edit::createSingleTrackEdit (engine);
    edit->tempoSequence.getTempos()[0]->setBpm (140.0);

    auto* track = te::getAudioTracks (*edit)[0];

    auto synth = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
    track->pluginList.insertPlugin (synth, 0, nullptr);

    const te::TimeRange twoBars { te::TimePosition(), edit->tempoSequence.toTime ({ 2, {} }) };
    auto* clip = dynamic_cast<te::MidiClip*> (track->insertNewClip (te::TrackItem::Type::midi, "spike", twoBars, nullptr));

    if (clip == nullptr)
    {
        std::fprintf (stderr, "ERRO: nao consegui criar o clipe MIDI\n");
        return 1;
    }

    // Dó menor, uma nota por tempo, oito tempos.
    const int notes[] = { 48, 51, 55, 58, 60, 58, 55, 51 };

    for (int i = 0; i < 8; ++i)
        clip->getSequence().addNote (notes[i],
                                     te::BeatPosition::fromBeats (i),
                                     te::BeatDuration::fromBeats (0.9),
                                     100, 0, nullptr);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const bool ok = te::Renderer::renderToFile (*edit, outFile, false);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    if (! ok || ! outFile.existsAsFile())
    {
        std::fprintf (stderr, "ERRO: render falhou\n");
        return 2;
    }

    // Confere que o arquivo tem som de verdade, não só silêncio.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (outFile));

    if (reader == nullptr)
    {
        std::fprintf (stderr, "ERRO: nao consegui ler o WAV gerado\n");
        return 3;
    }

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);

    float peak = 0.0f;
    double sumSquares = 0.0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
        const auto rms = buffer.getRMSLevel (ch, 0, buffer.getNumSamples());
        sumSquares += (double) rms * rms;
    }

    const auto rms = std::sqrt (sumSquares / juce::jmax (1, buffer.getNumChannels()));

    std::printf ("Arquivo: %s\n", outFile.getFullPathName().toRawUTF8());
    std::printf ("Formato: %d canais, %.0f Hz, %.2f s\n",
                 (int) reader->numChannels, reader->sampleRate,
                 (double) reader->lengthInSamples / reader->sampleRate);
    std::printf ("Pico: %.1f dBFS | RMS: %.1f dBFS | render: %.0f ms\n",
                 juce::Decibels::gainToDecibels (peak), juce::Decibels::gainToDecibels ((float) rms), ms);

    if (peak < 1.0e-4f)
    {
        std::fprintf (stderr, "ERRO: o WAV saiu em silencio\n");
        return 4;
    }

    std::printf ("OK: Tracktion Engine + JUCE renderizaram audio.\n");
    return 0;
}
