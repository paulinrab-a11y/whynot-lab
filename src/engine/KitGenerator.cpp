#include "KitGenerator.h"

namespace whynot
{

namespace
{
    constexpr double rate = 48000.0;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    /** Biquad simples (fórmulas do RBJ Audio EQ Cookbook), só para gerar o kit offline. */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        static Biquad make (bool highpass, bool bandpass, double freq, double q)
        {
            const double w0 = twoPi * freq / rate, cs = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
            Biquad f;
            double a0;

            if (bandpass)
            {
                f.b0 = alpha; f.b1 = 0; f.b2 = -alpha;
            }
            else if (highpass)
            {
                f.b0 = (1 + cs) / 2; f.b1 = -(1 + cs); f.b2 = (1 + cs) / 2;
            }
            else
            {
                f.b0 = (1 - cs) / 2; f.b1 = 1 - cs; f.b2 = (1 - cs) / 2;
            }

            a0 = 1 + alpha;
            f.a1 = -2 * cs / a0;
            f.a2 = (1 - alpha) / a0;
            f.b0 /= a0; f.b1 /= a0; f.b2 /= a0;
            return f;
        }

        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    juce::AudioBuffer<float> makeBuffer (double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        b.clear();
        return b;
    }

    float softClip (double x, double drive)  { return (float) (std::tanh (x * drive) / std::tanh (drive)); }

    double attack (double t, double ms)      { return juce::jmin (1.0, t / (ms * 0.001)); }

    juce::AudioBuffer<float> kick()
    {
        auto b = makeBuffer (0.6);
        auto* d = b.getWritePointer (0);
        double phase = 0.0;
        juce::Random noise (101);
        auto clickFilter = Biquad::make (true, false, 3000.0, 0.7);

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            const double freq = 46.0 + (170.0 - 46.0) * std::exp (-t / 0.032);
            phase += twoPi * freq / rate;
            const double body = std::sin (phase) * std::exp (-t / 0.2) * attack (t, 1.0);
            const double click = clickFilter.process (noise.nextFloat() * 2.0 - 1.0) * std::exp (-t / 0.003) * 0.35;
            d[i] = softClip (body + click, 1.6);
        }

        return b;
    }

    juce::AudioBuffer<float> clap()
    {
        auto b = makeBuffer (0.45);
        auto* d = b.getWritePointer (0);
        juce::Random noise (202);
        auto band = Biquad::make (false, true, 1250.0, 1.1);
        auto high = Biquad::make (true, false, 600.0, 0.7);

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            double env = 0.0;

            for (double burst : { 0.0, 0.009, 0.018 })
                if (t >= burst)
                    env += std::exp (-(t - burst) / 0.0035);

            if (t >= 0.022)
                env += 0.9 * std::exp (-(t - 0.022) / 0.11);

            const double x = high.process (band.process (noise.nextFloat() * 2.0 - 1.0));
            d[i] = (float) (x * env * 3.0);
        }

        return b;
    }

    juce::AudioBuffer<float> hat (double decay, double seconds, int seed)
    {
        auto b = makeBuffer (seconds);
        auto* d = b.getWritePointer (0);
        juce::Random noise (seed);
        auto hp1 = Biquad::make (true, false, 7500.0, 0.8);
        auto hp2 = Biquad::make (true, false, 7500.0, 0.8);
        auto peak = Biquad::make (false, true, 10500.0, 1.4);
        // seis osciladores quadrados com razões inarmônicas dão o brilho metálico
        const double freqs[] = { 317.0, 449.0, 531.0, 683.0, 787.0, 1033.0 };
        double phases[6] = {};

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            double metal = 0.0;

            for (int k = 0; k < 6; ++k)
            {
                phases[k] = std::fmod (phases[k] + freqs[k] / rate, 1.0);
                metal += phases[k] < 0.5 ? 1.0 : -1.0;
            }

            const double x = 0.6 * (noise.nextFloat() * 2.0 - 1.0) + 0.12 * metal;
            const double y = peak.process (hp2.process (hp1.process (x)));
            d[i] = (float) (y * std::exp (-t / decay) * attack (t, 0.3) * 2.0);
        }

        return b;
    }

    juce::AudioBuffer<float> bass808()
    {
        auto b = makeBuffer (2.2);
        auto* d = b.getWritePointer (0);
        constexpr double f0 = 65.40639;   // Dó 2: o canal usa a nota 36 como raiz
        double phase = 0.0;

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            const double freq = f0 * (1.0 + 0.55 * std::exp (-t / 0.018));
            phase += twoPi * freq / rate;
            const double env = std::exp (-t / 1.05) * attack (t, 2.0) * (t > 2.1 ? (2.2 - t) / 0.1 : 1.0);
            d[i] = softClip (std::sin (phase) * env * 1.1, 2.2);
        }

        return b;
    }

    juce::AudioBuffer<float> bell()
    {
        // Sino suave para melodias: parciais quase harmônicas, os agudos morrendo antes.
        auto b = makeBuffer (2.0);
        auto* d = b.getWritePointer (0);
        constexpr double f0 = 523.2511;   // Dó 5: o canal usa a nota 72 como raiz

        struct Partial { double ratio, gain, decay; };
        const Partial partials[] = { { 1.0, 1.0, 0.8 }, { 2.0, 0.32, 0.42 }, { 3.0, 0.16, 0.22 },
                                     { 4.07, 0.10, 0.11 }, { 0.5, 0.06, 1.1 } };
        juce::Random noise (505);
        auto strike = Biquad::make (false, true, 3200.0, 1.5);

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            double x = 0.0;

            for (auto& p : partials)
                x += p.gain * std::sin (twoPi * f0 * p.ratio * t) * std::exp (-t / p.decay);

            x += strike.process (noise.nextFloat() * 2.0 - 1.0) * std::exp (-t / 0.004) * 0.25;
            const double fade = t > 1.9 ? (2.0 - t) / 0.1 : 1.0;
            d[i] = softClip (x * attack (t, 2.0) * fade * 0.7, 1.2);
        }

        return b;
    }

    void normalise (juce::AudioBuffer<float>& b, float targetDb)
    {
        const float peak = b.getMagnitude (0, 0, b.getNumSamples());

        if (peak > 0.0f)
            b.applyGain (juce::Decibels::decibelsToGain (targetDb) / peak);
    }

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& b)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (rate)
                                                     .withNumChannels (1)
                                                     .withBitsPerSample (24));

        return writer != nullptr && writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

juce::Array<KitGenerator::Sound> KitGenerator::ensureKit (const juce::File& folder, juce::String& error)
{
    struct Def { const char* name; const char* file; int rootKey; float peakDb; std::function<juce::AudioBuffer<float>()> make; };

    // Nível de pico de cada som: kick e 808 na frente, o resto abaixo, como num kit mixado.
    // Com os faders em -6 dB, o beat inteiro (kick, 808, hats e o sino juntos) não chega ao teto.
    const Def defs[] = {
        { "Kick",     "WHYNOT Kick.wav",     60, -3.0f,  kick },
        { "Clap",     "WHYNOT Clap.wav",     60, -6.0f,  clap },
        { "Hat",      "WHYNOT Hat.wav",      60, -11.0f, [] { return hat (0.028, 0.15, 303); } },
        { "Open hat", "WHYNOT Open Hat.wav", 60, -12.0f, [] { return hat (0.24, 0.7, 404); } },
        { "808",      "WHYNOT 808.wav",      36, -3.0f,  bass808 },   // a nota que aparece no piano roll é a que soa
        { "Bell",     "WHYNOT Bell.wav",     72, -10.0f, bell },
    };

    juce::Array<Sound> sounds;

    // Mudou a síntese ou o nível? Sobe a versão e o kit é regerado na próxima abertura.
    constexpr const char* kitVersion = "4";   // v2: pico de -3 dBFS; v3: sino; v4: nível por som
    const auto versionFile = folder.getChildFile ("kit-version.txt");
    const bool regenerate = versionFile.loadFileAsString().trim() != kitVersion;

    for (auto& def : defs)
    {
        const auto file = folder.getChildFile (def.file);

        if (regenerate || ! file.existsAsFile())
        {
            auto buffer = def.make();
            normalise (buffer, def.peakDb);

            if (! writeWav (file, buffer))
            {
                error = "Não consegui gravar o kit em "_t + file.getFullPathName();
                return {};
            }
        }

        sounds.add ({ def.name, file, def.rootKey });
    }

    if (regenerate)
        versionFile.replaceWithText (kitVersion);

    return sounds;
}

} // namespace whynot
