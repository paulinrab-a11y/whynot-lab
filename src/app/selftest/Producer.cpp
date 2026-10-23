// Autoteste da caça de bugs do produtor (noite de 21/10, agente "Portátil e bugs" da Recomendações):
// o que trava ou falha no PC de quem recebe a versão portátil, sem placa de som e sem janela.
// O roteiro com cada caso está em C:\Users\<usuário>\dev\revisoes\roteiro-bugs-produtor-2026-10-21.md.

#include "SelfTestKit.h"
#include "../../engine/Exporter.h"
#include "../../engine/Mp3Encoder.h"
#include "../../engine/PianoLibrary.h"
#include "../../engine/SynthFavorites.h"
#include "../../engine/UserMix.h"
#include "../../ui/ExportDialogs.h"

#include <thread>

#if JUCE_WINDOWS
 #include <fcntl.h>
 #include <io.h>
 #include <share.h>
 #include <sys/stat.h>
#endif

namespace whynot::selftest
{

namespace
{
    double nowMs()   { return juce::Time::getMillisecondCounterHiRes(); }

    /** Se o trecho guardado não acabar em `seconds`, escreve a falha no relatório e encerra o processo:
        um travamento vira uma falha com nome, e não um autoteste parado para sempre. */
    class Watchdog
    {
    public:
        Watchdog (const Report& r, juce::String what, double seconds)
            : reportFile (r.file), name (std::move (what))
        {
            thread = std::thread ([this, seconds]
            {
                if (done.wait ((int) (seconds * 1000.0)))
                    return;

                reportFile.appendText ("  FALHOU: TRAVOU (mais de "_t + juce::String (seconds, 0) + " s): " + name + "\n");
                juce::Process::terminate();
            });
        }

        ~Watchdog()
        {
            done.signal();
            thread.join();
        }

    private:
        juce::File reportFile;
        juce::String name;
        juce::WaitableEvent done;
        std::thread thread;
    };

    /** Um arquivo aberto em outro programa, como um player faria: `denyAll` não deixa ninguém mais ler nem
        escrever; sem ele, os outros podem ler e escrever, mas não apagar (o CRT não pede o FILE_SHARE_DELETE). */
    struct HeldFile
    {
        HeldFile (const juce::File& f, bool denyAll)
        {
           #if JUCE_WINDOWS
            if (_wsopen_s (&fd, f.getFullPathName().toWideCharPointer(), _O_RDONLY | _O_BINARY, denyAll ? _SH_DENYRW : _SH_DENYNO, _S_IREAD) != 0)
                fd = -1;
           #else
            juce::ignoreUnused (f, denyAll);
           #endif
        }

        ~HeldFile()
        {
           #if JUCE_WINDOWS
            if (fd >= 0)
                _close (fd);
           #endif
        }

        bool held() const noexcept   { return fd >= 0; }

        int fd = -1;
    };

    juce::AudioBuffer<float> sine (double rate, double seconds, int channels, double hz = 440.0, float gain = 0.5f)
    {
        juce::AudioBuffer<float> b (channels, (int) (rate * seconds));

        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (c, i, gain * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));

        return b;
    }

    bool writeAudio (juce::AudioFormat& format, const juce::File& f, const juce::AudioBuffer<float>& b, double rate, int bits,
                     bool floating = false)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        auto options = juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (b.getNumChannels()).withBitsPerSample (bits);

        if (floating)
            options = options.withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        auto w = format.createWriterFor (stream, options);
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    bool writeWavFile (const juce::File& f, const juce::AudioBuffer<float>& b, double rate, int bits, bool floating = false)
    {
        juce::WavAudioFormat wav;
        return writeAudio (wav, f, b, rate, bits, floating);
    }

    /** Um loop de pacote: WAV de 24 bits com o pedaço ACID (andamento e número de tempos). */
    bool writeAcidLoop (const juce::File& f, const juce::AudioBuffer<float>& b, double rate, int beats, double bpm)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (b.getNumChannels()).withBitsPerSample (24)
                                 .withMetadata (juce::WavAudioFormat::acidOneShot, "0")
                                 .withMetadata (juce::WavAudioFormat::acidRootSet, "0")
                                 .withMetadata (juce::WavAudioFormat::acidStretch, "1")
                                 .withMetadata (juce::WavAudioFormat::acidDiskBased, "1")
                                 .withMetadata (juce::WavAudioFormat::acidizerFlag, "1")
                                 .withMetadata (juce::WavAudioFormat::acidBeats, juce::String (beats))
                                 .withMetadata (juce::WavAudioFormat::acidDenominator, "4")
                                 .withMetadata (juce::WavAudioFormat::acidNumerator, "4")
                                 .withMetadata (juce::WavAudioFormat::acidTempo, juce::String (bpm));
        auto w = wav.createWriterFor (stream, options);
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    /** WAV de 16 bits montado à mão, como os de pacotes de samples: com pedaços que o leitor precisa pular
        (JUNK antes do fmt, um pedaço de tamanho ímpar com o byte de enchimento, LIST/INFO e smpl depois
        dos dados), ou no formato "extensible". `truncated` corta os dados pela metade sem corrigir o cabeçalho. */
    juce::MemoryBlock handMadeWav (const juce::AudioBuffer<float>& b, double rate, bool extraChunks, bool extensible, bool truncated)
    {
        const int channels = b.getNumChannels(), frames = b.getNumSamples();
        juce::MemoryOutputStream pcm;

        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c)
                pcm.writeShort ((short) juce::jlimit (-32768, 32767, juce::roundToInt (b.getSample (c, i) * 32767.0f)));

        juce::MemoryOutputStream out;
        auto chunk = [&out] (const char* id, const void* data, size_t size)
        {
            out.write (id, 4);
            out.writeInt ((int) size);
            out.write (data, size);

            if ((size & 1) != 0)
                out.writeByte (0);   // enchimento de pedaço ímpar
        };

        out.write ("RIFF", 4);
        out.writeInt (0);   // tamanho, corrigido no fim
        out.write ("WAVE", 4);

        if (extraChunks)
        {
            const char junk[28] = {};
            chunk ("JUNK", junk, sizeof (junk));
        }

        juce::MemoryOutputStream fmt;
        fmt.writeShort ((short) (extensible ? 0xfffe : 1));
        fmt.writeShort ((short) channels);
        fmt.writeInt ((int) rate);
        fmt.writeInt ((int) rate * channels * 2);
        fmt.writeShort ((short) (channels * 2));
        fmt.writeShort (16);

        if (extensible)
        {
            static const unsigned char pcmGuid[16] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                                        0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
            fmt.writeShort (22);
            fmt.writeShort (16);
            fmt.writeInt (channels == 1 ? 4 : 3);
            fmt.write (pcmGuid, sizeof (pcmGuid));
        }

        chunk ("fmt ", fmt.getData(), fmt.getDataSize());

        if (extraChunks)
        {
            const char odd[3] = { 'a', 'b', 'c' };
            chunk ("abcd", odd, sizeof (odd));
        }

        const auto dataSize = pcm.getDataSize();
        out.write ("data", 4);
        out.writeInt ((int) dataSize);
        out.write (pcm.getData(), truncated ? dataSize / 2 : dataSize);

        if (! truncated && extraChunks)
        {
            juce::MemoryOutputStream info;
            info.write ("INFO", 4);
            const char title[] = "Kick \xc3\xa7\xc3\xa3o";   // "Kick ção" em UTF-8: 11 bytes com o zero, ímpar e sem enchimento (como em pacotes reais)
            info.write ("INAM", 4);
            info.writeInt ((int) sizeof (title));
            info.write (title, sizeof (title));
            chunk ("LIST", info.getData(), info.getDataSize());

            const char smpl[60] = {};
            chunk ("smpl", smpl, sizeof (smpl));
        }

        juce::MemoryBlock block (out.getData(), out.getDataSize());

        if (! truncated)
        {
            const auto riffSize = (juce::uint32) (block.getSize() - 8);
            std::memcpy (static_cast<char*> (block.getData()) + 4, &riffSize, 4);
        }
        else
        {
            const auto riffSize = (juce::uint32) (12 + 24 + 8 + dataSize - 8);   // o cabeçalho diz que tem tudo
            std::memcpy (static_cast<char*> (block.getData()) + 4, &riffSize, 4);
        }

        return block;
    }

    /** A maior correlação entre `a` e `b` deslocado de até `maxOffset` amostras, num trecho do meio de `a`.
        Devolve o deslocamento em `offset` (positivo: `b` vem depois). Os decodificadores de MP3 não concordam
        no atraso do começo, então o clipe MP3 é comparado com o alinhamento achado. */
    double bestCorrelation (const Audio& a, const Audio& b, int maxOffset, int& offset)
    {
        offset = 0;

        if (! a.valid || ! b.valid || a.buffer.getNumSamples() < 4 * maxOffset || b.buffer.getNumSamples() < 4 * maxOffset)
            return 0.0;

        const int start = a.buffer.getNumSamples() / 4, length = juce::jmin (a.buffer.getNumSamples() / 2, 24000);
        const float* x = a.buffer.getReadPointer (0);
        const float* y = b.buffer.getReadPointer (0);
        double best = -2.0;

        for (int d = -maxOffset; d <= maxOffset; ++d)
        {
            if (start + d < 0 || start + d + length > b.buffer.getNumSamples())
                continue;

            double xy = 0, xx = 0, yy = 0;

            for (int i = 0; i < length; ++i)
            {
                const double u = x[start + i], v = y[start + d + i];
                xy += u * v; xx += u * u; yy += v * v;
            }

            const double c = (xx > 0 && yy > 0) ? xy / std::sqrt (xx * yy) : 0.0;

            if (c > best)
            {
                best = c;
                offset = d;
            }
        }

        return best;
    }

    /** Primeiro instante (s) em que o canal 0 passa de `threshold`, ou -1. */
    double onsetSeconds (const Audio& a, float threshold = 0.05f)
    {
        const float* x = a.buffer.getNumChannels() > 0 ? a.buffer.getReadPointer (0) : nullptr;

        for (int i = 0; x != nullptr && i < a.buffer.getNumSamples(); ++i)
            if (std::abs (x[i]) > threshold)
                return i / a.sampleRate;

        return -1.0;
    }

    juce::var waitExport (AppController& app, double maxSeconds)
    {
        const auto start = nowMs();
        juce::var st;

        do
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            st = app.run ("export.status").value;
        }
        while (! (bool) st["finished"] && nowMs() - start < maxSeconds * 1000.0);

        return st;
    }

    void pump (int ms)   { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
}

void runProducerSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto folder = ctx.work.getChildFile ("Produtor");
    folder.deleteRecursively();
    folder.createDirectory();

    //==========================================================================
    rep.section ("Produtor: MP3, FLAC e OGG na playlist (a exportação travava para sempre)"_t);
    {
        // Um áudio de fora, como o produtor arrasta para a playlist: um acorde de 2 s, a 48 kHz.
        constexpr double rate = 48000.0;
        auto source = sine (rate, 2.0, 2, 440.0, 0.3f);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < source.getNumSamples(); ++i)
                source.addSample (c, i, 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 660.0 * i / rate));

        const auto sourceWav = folder.getChildFile ("fora.wav");
        writeWavFile (sourceWav, source, rate, 24);

        juce::Array<juce::File> compressed;

       #if WHYNOT_HAS_LAME
        {
            const auto mp3 = folder.getChildFile ("fora.mp3");
            Mp3Settings mp3Settings;
            mp3Settings.title = "fora";

            if (encodeMp3 (sourceWav, mp3, mp3Settings).wasOk())
                compressed.add (mp3);
        }
       #endif
       #if JUCE_USE_FLAC
        {
            juce::FlacAudioFormat flac;
            const auto f = folder.getChildFile ("fora.flac");

            if (writeAudio (flac, f, source, rate, 24))
                compressed.add (f);
        }
       #endif
       #if JUCE_USE_OGGVORBIS
        {
            juce::OggVorbisAudioFormat ogg;
            const auto f = folder.getChildFile ("fora.ogg");

            if (writeAudio (ogg, f, source, rate, 16))
                compressed.add (f);
        }
       #endif

        rep.check (! compressed.isEmpty(), juce::String (compressed.size()) + " formatos comprimidos para testar ("_t
                                               + [&] { juce::StringArray s; for (auto& f : compressed) s.add (f.getFileExtension()); return s.joinIntoString (" "); }() + ")");

        for (auto& file : compressed)
        {
            const auto ext = file.getFileExtension().substring (1).toUpperCase();

            // o mesmo áudio, decodificado, num clipe WAV: a referência do que o clipe deve soar
            const auto decoded = readWav (file);
            const auto decodedWav = folder.getChildFile ("fora_" + ext + "_decodificado.wav");
            writeWavFile (decodedWav, decoded.buffer, decoded.sampleRate, 32, true);

            auto renderClip = [&] (const juce::File& clip, const juce::File& out, bool background)
            {
                app.run ("project.new");
                app.run ("transport.setSongMode", args ({ { "on", true } }));
                const auto added = app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "unit", "bars" },
                                                                        { "path", clip.getFullPathName() } }));

                if (! added.ok)
                    return added.error;

                if (! background)
                {
                    Watchdog guard (rep, "exportar a música com um clipe "_t + clip.getFileExtension() + " na playlist", 90.0);
                    const auto r = app.run ("project.exportWav", args ({ { "path", out.getFullPathName() }, { "what", "song" } }));
                    return r.ok ? juce::String() : r.error;
                }

                const auto started = app.run ("project.exportWav", args ({ { "path", out.getFullPathName() }, { "what", "song" },
                                                                           { "background", true } }));

                if (! started.ok)
                    return started.error;

                const auto st = waitExport (app, 90.0);
                return (bool) st["ok"] ? juce::String() : "status: " + st["message"].toString() + ((bool) st["finished"] ? juce::String() : " (não terminou)"_t);
            };

            const auto outSync = folder.getChildFile ("clipe_" + ext + ".wav");
            const auto t0 = nowMs();
            const auto errSync = renderClip (file, outSync, false);
            const auto seconds = (nowMs() - t0) / 1000.0;
            const auto rendered = readWav (outSync);
            rep.check (errSync.isEmpty() && rendered.valid,
                       "música com clipe "_t + ext + " exporta sem travar (" + juce::String (seconds, 1) + " s)"
                           + (errSync.isEmpty() ? juce::String() : ": " + errSync));

            const auto refFile = folder.getChildFile ("clipe_" + ext + "_ref.wav");
            renderClip (decodedWav, refFile, false);
            const auto reference = readWav (refFile);
            // FLAC e OGG saem iguais, amostra por amostra. No MP3, o JUCE e o Tracktion tiram do começo um atraso
            // diferente do codificador: a comparação é com o melhor alinhamento (até 3000 amostras, 62 ms)
            int offset = 0;
            const float diff = maxDiff (rendered, reference);
            const double corr = bestCorrelation (rendered, reference, ext == "MP3" ? 3000 : 0, offset);
            rep.check (rendered.valid && reference.valid && rmsDb (rendered) > -40.0f && corr > 0.99 && (ext == "MP3" || diff == 0.0f),
                       "o clipe "_t + ext + " soa como o mesmo áudio num clipe WAV (RMS "_t + juce::String (rmsDb (rendered), 1)
                           + " dB, correlação "_t + juce::String (corr, 5) + " com " + juce::String (offset) + " amostras de deslocamento"
                           + (ext == "MP3" ? juce::String() : ", diferença máx. "_t + juce::String (diff, 6)) + ")");

            const auto outBg = folder.getChildFile ("clipe_" + ext + "_fundo.wav");
            const auto errBg = renderClip (file, outBg, true);
            const float bgDiff = maxDiff (readWav (outBg), rendered);
            rep.check (errBg.isEmpty() && bgDiff == 0.0f, "em segundo plano sai igual, amostra por amostra"_t
                                                             + (errBg.isEmpty() ? juce::String() : ": " + errBg));
        }

        // Congelar a trilha de um clipe comprimido, e abrir o projeto com ela congelada: o render do
        // congelamento roda na thread de mensagem e também esperava o proxy para sempre.
        if (! compressed.isEmpty())
        {
            const auto clip = compressed.getFirst();
            const auto ext = clip.getFileExtension().substring (1).toUpperCase();
            app.run ("project.new");
            app.run ("transport.setSongMode", args ({ { "on", true } }));
            const auto added = app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "unit", "bars" },
                                                                    { "path", clip.getFullPathName() } }));
            const auto chId = added.value["ref"].toString();
            const auto* ch = model.get().findChannel (chId);
            const auto mixerId = ch != nullptr ? model.get().resolveOutput (ch->output) : juce::String();
            CommandResult frozen;

            {
                Watchdog guard (rep, "congelar a trilha de um clipe "_t + ext, 90.0);
                frozen = app.run ("channel.setFrozen", args ({ { "channel", chId }, { "on", true } }));
            }

            rep.check (added.ok && frozen.ok && app.getProjector().isFrozen (mixerId),
                       "congela a trilha de um clipe "_t + ext + " sem travar" + (frozen.ok ? juce::String() : ": " + frozen.error)
                           + app.getProjector().freezeProblems()[mixerId]);

            const auto frozenProject = folder.getChildFile ("Congelado").getChildFile ("Congelado.wnl");
            app.run ("project.save", args ({ { "path", frozenProject.getFullPathName() } }));
            app.run ("project.new");
            CommandResult reopened;

            {
                Watchdog guard (rep, "abrir um projeto com a trilha de um clipe "_t + ext + " congelada", 90.0);
                reopened = app.run ("project.open", args ({ { "path", frozenProject.getFullPathName() } }));
            }

            rep.check (reopened.ok && app.getProjector().isFrozen (mixerId),
                       "e abre o projeto com ela congelada sem travar"_t + (reopened.ok ? juce::String() : ": " + reopened.error));
        }
    }

    //==========================================================================
    rep.section ("Produtor: andamento no nome do arquivo e loop ACID (a exportação travava)"_t);
    {
        // Um beat de 2 compassos a 120 BPM (4 s), como os que vêm da internet, com um tique em cada tempo.
        constexpr double rate = 48000.0;
        auto beat = sine (rate, 4.0, 2, 110.0, 0.3f);

        for (int k = 0; k < 8; ++k)
            for (int i = 0; i < 2400; ++i)
                for (int c = 0; c < 2; ++c)
                    beat.addSample (c, (int) (k * 0.5 * rate) + i,
                                    0.4f * (1.0f - (float) i / 2400.0f) * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / rate));

        const auto beats = folder.getChildFile ("Batidas");
        const auto plain = beats.getChildFile ("beat sem nome.wav");
        writeWavFile (plain, beat, rate, 24);

        struct Named { juce::String label; juce::File file; };
        std::vector<Named> named;
        named.push_back ({ "\"Beat Teste 120bpm.wav\""_t, beats.getChildFile ("Beat Teste 120bpm.wav") });
        named.push_back ({ "\"Loop Am 120.wav\" (andamento e tom no nome)"_t, beats.getChildFile ("Loop Am 120.wav") });
        named.push_back ({ "loop de pacote com o pedaço ACID"_t, beats.getChildFile ("loop do pacote.wav") });
        plain.copyFileTo (named[0].file);
        plain.copyFileTo (named[1].file);
        writeAcidLoop (named[2].file, beat, rate, 8, 120.0);

        // exporta a música com o clipe e diz se o Tracktion ligou o auto-tempo ou o auto-pitch no clipe ao vivo
        auto renderAt = [&] (const juce::File& clip, double bpm, const juce::File& out, bool& automatic)
        {
            app.run ("project.new");
            app.run ("transport.setBpm", args ({ { "bpm", bpm } }));
            app.run ("transport.setSongMode", args ({ { "on", true } }));
            const auto added = app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "unit", "bars" },
                                                                    { "path", clip.getFullPathName() } }));
            automatic = false;

            if (auto* track = app.getProjector().trackFor (added.value["ref"].toString()))
                for (auto* c : track->getClips())
                    if (auto* a = dynamic_cast<te::AudioClipBase*> (c))
                        automatic = automatic || a->getAutoTempo() || a->getAutoPitch();

            Watchdog guard (rep, "exportar a música com "_t + clip.getFileName() + " na playlist", 90.0);
            const auto r = app.run ("project.exportWav", args ({ { "path", out.getFullPathName() }, { "what", "song" } }));
            return r.ok ? juce::String() : r.error;
        };

        for (const double bpm : { 120.0, 140.0 })
        {
            bool referenceAutomatic = false;
            const auto referenceFile = folder.getChildFile ("batida_ref_" + juce::String ((int) bpm) + ".wav");
            renderAt (plain, bpm, referenceFile, referenceAutomatic);
            const auto reference = readWav (referenceFile);

            for (auto& n : named)
            {
                bool automatic = true;
                const auto out = folder.getChildFile ("batida_" + juce::String ((int) (&n - named.data())) + "_" + juce::String ((int) bpm) + ".wav");
                const auto t0 = nowMs();
                const auto error = renderAt (n.file, bpm, out, automatic);
                const auto got = readWav (out);
                rep.check (error.isEmpty() && got.valid && reference.valid && ! automatic && maxDiff (got, reference) < 1.0e-6f,
                           n.label + " a " + juce::String ((int) bpm) + " BPM: exporta em " + juce::String ((nowMs() - t0) / 1000.0, 1)
                               + " s, sem esticar sozinho e igual ao mesmo WAV sem nome" + (error.isEmpty() ? juce::String() : ": " + error));
            }
        }
    }

    //==========================================================================
    rep.section ("Produtor: fechar o app no meio da exportação"_t);
    {
        const auto data2 = folder.getChildFile ("dados_fechar");
        const auto out = folder.getChildFile ("fechar_no_meio.wav");
        bool started = false;
        double progressSeen = 0.0, closeSeconds = -1.0;

        {
            auto second = std::make_unique<AppController> (true, data2);
            second->run ("project.new");
            setGrid (*second, channelId (second->getModel().get(), "Kick"), "x...x...x...x...");
            // uma música longa (uns 6 min, quase toda em silêncio): o render ainda está no meio quando o app fecha
            second->run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "unit", "bars" } }));
            second->run ("playlist.addItem", args ({ { "track", 1 }, { "start", 200 }, { "unit", "bars" } }));
            const auto r = second->run ("project.exportWav", args ({ { "path", out.getFullPathName() }, { "what", "song" },
                                                                     { "background", true } }));
            started = r.ok;

            for (const auto t0 = nowMs(); started && nowMs() - t0 < 30000.0;)
            {
                pump (2);
                const auto st = second->backgroundExportStatus();
                progressSeen = (double) st["progress"];

                if (progressSeen >= 0.05 || (bool) st["finished"])
                    break;
            }

            Watchdog guard (rep, "fechar o app no meio da exportação"_t, 60.0);
            const auto c0 = nowMs();
            second = nullptr;
            closeSeconds = (nowMs() - c0) / 1000.0;
        }

        // o segundo app trocou as pastas globais do piano e dos favoritos do synth: voltam para as do teste
        piano::setDataFolder (ctx.data);
        synthfavorites::setFolder (ctx.data);
        usermix::setDataFolder (ctx.data);   // e a das cadeias e presets do artista (Efeitos; integração de teste)

        rep.check (started && progressSeen >= 0.05 && progressSeen < 1.0 && closeSeconds >= 0.0 && closeSeconds < 10.0,
                   "fecha em "_t + juce::String (closeSeconds, 2) + " s com o render em " + juce::String (progressSeen * 100.0, 1)
                       + "% (antes, o app ficava preso para sempre, sem janela)");
        rep.check (! out.existsAsFile(), "e o WAV pela metade não fica no disco"_t);
        data2.deleteRecursively();
    }

    //==========================================================================
    rep.section ("Produtor: destino que não dá para gravar"_t);
    {
        app.run ("project.new");
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        setGrid (app, channelId (model.get(), "Kick"), "x...x...x...x...");
        auto exportTo = [&app] (const juce::File& f) { return app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 },
                                                                                                    { "what", "pattern" } })); };
        const auto fresh = folder.getChildFile ("destino_novo.wav");
        exportTo (fresh);
        const auto expected = readWav (fresh);

        // um player com o arquivo aberto, sem deixar nada
        {
            const auto locked = folder.getChildFile ("aberto_no_player.wav");
            writeWavFile (locked, sine (48000.0, 1.0, 2, 1000.0), 48000.0, 16);
            HeldFile held (locked, true);
            const auto t0 = nowMs();
            const auto r = exportTo (locked);
            rep.check (held.held() && ! r.ok && r.error.contains ("aberto em outro programa"_t) && nowMs() - t0 < 10000.0,
                       "arquivo aberto em outro programa: avisa em vez de falhar calado ("_t + r.error + ")");
        }

        // um player que deixa escrever mas não apagar: antes, o WAV novo ia para o fim do antigo e tocava o antigo
        {
            const auto shared = folder.getChildFile ("aberto_compartilhado.wav");
            writeWavFile (shared, sine (48000.0, 1.0, 2, 1000.0), 48000.0, 16);
            CommandResult r;
            bool held = false;

            {
                HeldFile h (shared, false);
                held = h.held();
                r = exportTo (shared);
            }

            const auto got = readWav (shared);
            rep.check (held && r.ok && got.valid && maxDiff (got, expected) < 1.0e-6f,
                       "arquivo aberto que deixa escrever: o WAV novo substitui o antigo ("_t
                           + juce::String (got.valid ? got.buffer.getNumSamples() / got.sampleRate : 0.0, 3) + " s; o novo tem "
                           + juce::String (expected.buffer.getNumSamples() / juce::jmax (1.0, expected.sampleRate), 3) + " s)"
                           + (r.ok ? juce::String() : ": " + r.error));
        }

        // caminho com mais de 259 caracteres
        {
            auto deep = folder;

            while (deep.getFullPathName().length() < 250)
                deep = deep.getChildFile ("pasta com nome comprido de beat");

            const auto r = exportTo (deep.getChildFile ("Beat final mixado e masterizado.wav"));
            rep.check (! r.ok && r.error.contains ("caracteres"_t), "caminho longo demais: explica o limite do Windows ("_t + r.error.substring (0, 90) + "...)");
        }

        // nome reservado do Windows
        {
            const auto r = exportTo (folder.getChildFile ("AUX.wav"));
            rep.check (! r.ok && r.error.contains ("reservado"_t), "nome reservado (AUX): avisa ("_t + r.error + ")");
            rep.check (safeFileName ("AUX") == "AUX_" && safeFileName ("con.final") == "con_.final" && safeFileName ("Beat.") == "Beat"
                           && safeFileName ("Beat 2 ") == "Beat 2" && safeFileName ("NUL - Kick") == "NUL - Kick" && safeFileName ("Lpt1") == "Lpt1_",
                       "o nome padrão foge dos reservados (AUX vira AUX_) e do ponto no fim ("_t + safeFileName ("AUX") + ", " + safeFileName ("con.final")
                           + ", " + safeFileName ("Beat.") + ")");
        }

        // arquivo só de leitura
        {
            const auto readOnly = folder.getChildFile ("so_leitura.wav");
            writeWavFile (readOnly, sine (48000.0, 0.5, 2), 48000.0, 16);
            readOnly.setReadOnly (true);
            const auto r = exportTo (readOnly);
            readOnly.setReadOnly (false);
            rep.check (! r.ok && r.error.contains ("só leitura"_t), "arquivo só de leitura: avisa ("_t + r.error + ")");
        }

        // pasta que não pode ser criada (no lugar dela há um arquivo)
        {
            const auto blocker = folder.getChildFile ("sou_um_arquivo");
            blocker.replaceWithText ("x");
            const auto r = exportTo (blocker.getChildFile ("renders").getChildFile ("beat.wav"));
            rep.check (! r.ok && r.error.contains ("criar a pasta"_t), "pasta que não pode ser criada: avisa ("_t + r.error.substring (0, 80) + "...)");
        }

       #if WHYNOT_HAS_LAME
        // MP3 para um arquivo aberto: avisa antes do render, e não depois de esperar a música toda
        {
            const auto locked = folder.getChildFile ("aberto_no_player.mp3");
            locked.replaceWithText ("mp3 velho");
            HeldFile held (locked, true);
            const auto t0 = nowMs();
            const auto r = app.run ("project.exportMp3", args ({ { "path", locked.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            const auto ms = nowMs() - t0;
            rep.check (held.held() && ! r.ok && r.error.contains ("aberto em outro programa"_t) && ms < 2000.0,
                       "MP3 para arquivo aberto: avisa em "_t + juce::String (ms, 0) + " ms, antes do render");
        }
       #endif

        // em segundo plano, o mesmo aviso chega pelo export.status
        {
            const auto locked = folder.getChildFile ("aberto_fundo.wav");
            locked.replaceWithText ("wav velho");
            HeldFile held (locked, true);
            const auto started = app.run ("project.exportWav", args ({ { "path", locked.getFullPathName() }, { "loops", 1 },
                                                                       { "what", "pattern" }, { "background", true } }));
            const auto st = started.ok ? waitExport (app, 30.0) : juce::var();
            rep.check (held.held() && (started.ok ? ((bool) st["finished"] && ! (bool) st["ok"] && st["message"].toString().contains ("aberto"))
                                                  : started.error.contains ("aberto")),
                       "em segundo plano: o status diz o porquê ("_t + (started.ok ? st["message"].toString() : started.error) + ")");
        }
    }

    //==========================================================================
    rep.section ("Produtor: projeto de outro PC (caminhos absolutos e samples fora do lugar)"_t);
    {
        app.run ("project.new");
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        setGrid (app, channelId (model.get(), "Kick"), "x...x...x...x...");
        setGrid (app, channelId (model.get(), "Clap"), "....x.......x...");
        const auto projectFile = folder.getChildFile ("De Outro PC").getChildFile ("De Outro PC.wnl");
        app.run ("project.save", args ({ { "path", projectFile.getFullPathName() } }));
        const auto original = folder.getChildFile ("outro_pc_original.wav");
        app.run ("project.exportWav", args ({ { "path", original.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));

        // Como chega do outro PC (outro usuário do Windows): o kick com o caminho absoluto de lá (a cópia está
        // em samples/), o clap sem a cópia (só o kit deste PC tem) e um hat que não existe em lugar nenhum.
        auto root = juce::JSON::parse (projectFile);
        const juce::String otherKit ("C:/Users/OutroPC/AppData/Roaming/WHYNOT LAB/Kit/");

        if (auto* channels = root["channels"].getArray())
        {
            for (auto& channel : *channels)
            {
                auto* sample = channel["sample"].getDynamicObject();
                const auto name = channel["name"].toString();

                if (sample == nullptr)
                    continue;

                if (name == "Kick")
                {
                    sample->setProperty ("path", "C:/Users/OutroPC/Music/Kit/WHYNOT Kick.wav");
                    sample->setProperty ("originalPath", otherKit + "WHYNOT Kick.wav");
                }
                else if (name == "Clap")
                {
                    sample->setProperty ("path", "D:/Beats/kit antigo/WHYNOT Clap.wav");
                    sample->setProperty ("originalPath", otherKit + "WHYNOT Clap.wav");
                }
                else if (name == "Hat")
                {
                    sample->setProperty ("path", "C:/Users/OutroPC/Music/Kit/Meu Hat Raro.wav");
                    sample->setProperty ("originalPath", "C:/Users/OutroPC/Downloads/Meu Hat Raro.wav");
                }
            }
        }

        projectFile.replaceWithText (juce::JSON::toString (root));
        projectFile.getSiblingFile ("samples").getChildFile ("WHYNOT Clap.wav").deleteFile();

        const auto opened = app.run ("project.open", args ({ { "path", projectFile.getFullPathName() } }));
        const auto* kick = model.get().findChannel (channelId (model.get(), "Kick"));
        const auto* clap = model.get().findChannel (channelId (model.get(), "Clap"));
        const auto* hat = model.get().findChannel (channelId (model.get(), "Hat"));
        rep.check (opened.ok && kick != nullptr && kick->samplePath == "samples/WHYNOT Kick.wav",
                   "caminho de outro PC: acha o sample pelo nome na pasta samples/ ("_t + (kick != nullptr ? kick->samplePath : juce::String()) + ")");
        rep.check (clap != nullptr && model.resolveSample (clap->samplePath) == app.getKitFolder().getChildFile ("WHYNOT Clap.wav"),
                   "o que não veio na pasta é achado no kit deste PC ("_t + (clap != nullptr ? clap->samplePath : juce::String()) + ")");
        rep.check (hat != nullptr && app.getProjector().sampleProblems()[hat->id].contains ("não encontrado"_t),
                   "o que não existe em lugar nenhum fica avisado: "_t + (hat != nullptr ? app.getProjector().sampleProblems()[hat->id] : juce::String()));

        const auto again = folder.getChildFile ("outro_pc_reaberto.wav");
        const auto r = app.run ("project.exportWav", args ({ { "path", again.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        rep.check (r.ok && maxDiff (readWav (again), readWav (original)) < 1.0e-6f,
                   "e exporta igual ao original (o hat não tocava no padrão)"_t + (r.ok ? juce::String() : ": " + r.error));
    }

    //==========================================================================
    rep.section ("Produtor: samples de vários formatos"_t);
    {
        const auto samples = folder.getChildFile ("Samples variados");
        struct Case { juce::String label; juce::File file; };
        std::vector<Case> cases;

        cases.push_back ({ "96 kHz, 24 bits"_t, samples.getChildFile ("seno 96k.wav") });
        writeWavFile (cases.back().file, sine (96000.0, 0.5, 2), 96000.0, 24);
        cases.push_back ({ "32 bits float"_t, samples.getChildFile ("seno 32f.wav") });
        writeWavFile (cases.back().file, sine (48000.0, 0.5, 2), 48000.0, 32, true);
        cases.push_back ({ "8 bits, 22 kHz"_t, samples.getChildFile ("seno 8bit.wav") });
        writeWavFile (cases.back().file, sine (22050.0, 0.5, 1), 22050.0, 8);
        cases.push_back ({ "mono, 44,1 kHz"_t, samples.getChildFile ("seno mono.wav") });
        writeWavFile (cases.back().file, sine (44100.0, 0.5, 1), 44100.0, 16);
        cases.push_back ({ "nome com acento, ideograma e emoji"_t, samples.getChildFile ("Kíck ção 日本 🥁.wav"_t) });
        writeWavFile (cases.back().file, sine (44100.0, 0.5, 2), 44100.0, 16);

        auto block = handMadeWav (sine (44100.0, 0.5, 2), 44100.0, true, false, false);
        cases.push_back ({ "WAV com JUNK, pedaço ímpar, LIST/INFO e smpl"_t, samples.getChildFile ("metadados.wav") });
        cases.back().file.replaceWithData (block.getData(), block.getSize());
        block = handMadeWav (sine (44100.0, 0.5, 1), 44100.0, false, true, false);
        cases.push_back ({ "WAV extensible (0xFFFE)"_t, samples.getChildFile ("extensible.wav") });
        cases.back().file.replaceWithData (block.getData(), block.getSize());
        block = handMadeWav (sine (44100.0, 1.0, 2), 44100.0, false, false, true);
        cases.push_back ({ "WAV cortado (o cabeçalho diz mais do que tem)"_t, samples.getChildFile ("cortado.wav") });
        cases.back().file.replaceWithData (block.getData(), block.getSize());

        for (auto& c : cases)
        {
            app.run ("project.new");
            app.run ("transport.setSongMode", args ({ { "on", false } }));
            const auto added = app.run ("channel.addSampler", args ({ { "path", c.file.getFullPathName() }, { "rootKey", 69 }, { "detectRoot", false } }));
            const auto ch = added.value["channel"].toString();
            setGrid (app, ch, "x...............");   // os outros canais do projeto novo não têm passos

            const auto out = folder.getChildFile ("formato_" + juce::String ((int) (&c - cases.data())) + ".wav");
            const auto r = app.run ("project.exportWav", args ({ { "path", out.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            const auto audio = readWav (out);
            const double hz = measureHz (audio, 0.05, 0.25);
            rep.check (added.ok && r.ok && std::abs (hz - 440.0) < 3.0 && peakDb (audio) > -20.0f,
                       c.label + ": toca no tom certo (" + juce::String (hz, 1) + " Hz)" + (added.ok ? juce::String() : ": " + added.error));
        }

        // arquivo estragado com nome de WAV: avisa e não derruba o app
        {
            const auto broken = samples.getChildFile ("corrompido.wav");
            juce::MemoryBlock junk (4096);
            juce::Random rnd (21);
            rnd.fillBitsRandomly (junk.getData(), junk.getSize());
            broken.replaceWithData (junk.getData(), junk.getSize());

            app.run ("project.new");
            const auto added = app.run ("channel.addSampler", args ({ { "path", broken.getFullPathName() } }));
            const auto problem = added.ok ? app.getProjector().sampleProblems()[added.value["channel"].toString()] : added.error;
            const auto out = folder.getChildFile ("com_corrompido.wav");
            const auto r = app.run ("project.exportWav", args ({ { "path", out.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            rep.check (problem.isNotEmpty() && r.ok, "sample corrompido: avisa ("_t + problem + ") e o projeto continua exportando");
        }

        // 10 minutos: entra; passou de 10, é recusado com o motivo
        {
            const auto longFile = samples.getChildFile ("musica inteira 9min59.wav");
            const auto tooLong = samples.getChildFile ("musica inteira 10min01.wav");
            writeWavFile (longFile, sine (22050.0, 599.0, 1, 110.0), 22050.0, 16);
            writeWavFile (tooLong, sine (22050.0, 601.0, 1, 110.0), 22050.0, 16);
            app.run ("project.new");
            const auto a = app.run ("channel.addSampler", args ({ { "path", longFile.getFullPathName() }, { "detectRoot", false } }));
            const auto aProblem = a.ok ? app.getProjector().sampleProblems()[a.value["channel"].toString()] : a.error;
            const auto b = app.run ("channel.addSampler", args ({ { "path", tooLong.getFullPathName() }, { "detectRoot", false } }));
            const auto bProblem = b.ok ? app.getProjector().sampleProblems()[b.value["channel"].toString()] : b.error;
            rep.check (a.ok && aProblem.isEmpty(), "sample de 9:59 carrega"_t + (aProblem.isEmpty() ? juce::String() : ": " + aProblem));
            rep.check (bProblem.contains ("10 minutos"), "sample de 10:01 é recusado com o motivo ("_t + bProblem + ")");
            app.run ("project.new");
            longFile.deleteFile();
            tooLong.deleteFile();
        }

       #if WHYNOT_HAS_LAME
        // MP3 de kit: o codificador põe silêncio no começo, e o sampler toca atrasado (frente Fatiar e 808)
        {
            const auto wavKick = samples.getChildFile ("kick do kit.wav");
            const auto mp3Kick = samples.getChildFile ("kick do kit.mp3");
            writeWavFile (wavKick, sine (44100.0, 0.3, 2, 60.0, 0.9f), 44100.0, 16);
            encodeMp3 (wavKick, mp3Kick, {});

            auto onsetOf = [&] (const juce::File& f, const juce::String& name)
            {
                app.run ("project.new");
                app.run ("transport.setSongMode", args ({ { "on", false } }));
                const auto ch = app.run ("channel.addSampler", args ({ { "path", f.getFullPathName() }, { "detectRoot", false } })).value["channel"].toString();
                setGrid (app, ch, "x...............");
                const auto out = folder.getChildFile (name);
                app.run ("project.exportWav", args ({ { "path", out.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
                return onsetSeconds (readWav (out));
            };

            const double wavOnset = onsetOf (wavKick, "kit_wav.wav"), mp3Onset = onsetOf (mp3Kick, "kit_mp3.wav");
            const double delayMs = (mp3Onset - wavOnset) * 1000.0;
            rep.check (wavOnset >= 0.0 && mp3Onset >= 0.0 && delayMs < 80.0,
                       "MP3 de kit toca "_t + juce::String (delayMs, 1) + " ms depois do WAV (o silêncio do codificador; a correção é da frente Fatiar e 808)"_t);
        }
       #endif
    }

    //==========================================================================
    rep.section ("Produtor: acentos, espaços e Unicode no caminho do projeto"_t);
    {
        const auto soundFolder = folder.getChildFile ("Sons ñ ü"_t);
        const auto sample = soundFolder.getChildFile ("Kíck ção 日本 🥁.wav"_t);
        writeWavFile (sample, sine (44100.0, 0.4, 2, 220.0), 44100.0, 24);

        app.run ("project.new");
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        const auto ch = app.run ("channel.addSampler", args ({ { "path", sample.getFullPathName() }, { "detectRoot", false } })).value["channel"].toString();
        setGrid (app, ch, "x.......x.......");

        const auto projectFile = folder.getChildFile ("Músicas ção 日本"_t).getChildFile ("Beat ação é"_t).getChildFile ("Beat ação é.wnl"_t);
        const auto saved = app.run ("project.save", args ({ { "path", projectFile.getFullPathName() } }));
        const auto reopened = app.run ("project.open", args ({ { "path", projectFile.getFullPathName() } }));
        const auto* c = model.get().findChannel (ch);
        rep.check (saved.ok && reopened.ok && c != nullptr && c->samplePath == "samples/Kíck ção 日本 🥁.wav"_t
                       && app.getProjector().sampleProblems()[ch].isEmpty(),
                   "salva, coleta o sample com o nome Unicode e reabre ("_t + (c != nullptr ? c->samplePath : juce::String()) + ")");

        const auto wav = projectFile.getSiblingFile ("renders").getChildFile ("Beat ação é.wav"_t);
        const auto r = app.run ("project.exportWav", args ({ { "path", wav.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        rep.check (r.ok && peakDb (readWav (wav)) > -30.0f, "exporta WAV na pasta com acento"_t + (r.ok ? juce::String() : ": " + r.error));

       #if WHYNOT_HAS_LAME
        const auto mp3 = projectFile.getSiblingFile ("renders").getChildFile ("Beat ação é.mp3"_t);
        const auto m = app.run ("project.exportMp3", args ({ { "path", mp3.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        rep.check (m.ok && readWav (mp3).valid, "e MP3 também"_t + (m.ok ? juce::String() : ": " + m.error));
       #endif

        // salvar com nome reservado, nome longo demais, numa pasta que não dá para criar e por cima de um .wnl só de leitura
        const auto aux = app.run ("project.save", args ({ { "path", folder.getChildFile ("AUX").getChildFile ("AUX.wnl").getFullPathName() } }));
        rep.check (! aux.ok && aux.error.contains ("reservado"_t), "salvar como AUX: explica que o nome é reservado ("_t + aux.error + ")");

        const auto longName = juce::String::repeatedString ("Beat trap pesado com 808 distorcido ", 7).trim();
        const auto tooLong = app.run ("project.save", args ({ { "path", folder.getChildFile (longName).getChildFile (longName + ".wnl").getFullPathName() } }));
        rep.check (! tooLong.ok && tooLong.error.contains ("caracteres"_t), "salvar com um nome de "_t + juce::String (longName.length())
                                                                               + " letras: explica o limite (" + tooLong.error.substring (0, 60) + "...)");

        const auto blocker = folder.getChildFile ("sou_um_arquivo_tambem");
        blocker.replaceWithText ("x");
        const auto noFolder = app.run ("project.save", args ({ { "path", blocker.getChildFile ("Beat").getChildFile ("Beat.wnl").getFullPathName() } }));
        rep.check (! noFolder.ok && noFolder.error.contains ("criar a pasta"_t), "salvar onde a pasta não pode ser criada: avisa ("_t
                                                                                     + noFolder.error.substring (0, 70) + "...)");

        const auto lockedWnl = folder.getChildFile ("Travado").getChildFile ("Travado.wnl");
        app.run ("project.save", args ({ { "path", lockedWnl.getFullPathName() } }));
        lockedWnl.setReadOnly (true);
        const auto overReadOnly = app.run ("project.save", args ({ { "path", lockedWnl.getFullPathName() } }));
        lockedWnl.setReadOnly (false);
        rep.check (! overReadOnly.ok && overReadOnly.error.contains ("só leitura"_t), "salvar por cima de um .wnl só de leitura: avisa ("_t
                                                                                         + overReadOnly.error.substring (0, 70) + "...)");
    }

    //==========================================================================
    rep.section ("Produtor: projeto vazio, abrir dois seguidos, desfazer e autosave"_t);
    {
        app.run ("project.new");
        app.run ("transport.setSongMode", args ({ { "on", true } }));
        const auto emptyFile = folder.getChildFile ("vazio.wav");
        const auto empty = app.run ("project.exportWav", args ({ { "path", emptyFile.getFullPathName() }, { "what", "song" } }));
        rep.check (empty.ok ? rmsDb (readWav (emptyFile)) < -90.0f : empty.error.contains ("nada para exportar"),
                   "música sem blocos: "_t + (empty.ok ? "exporta silêncio, sem travar"_t : "diz que não há nada ("_t + empty.error + ")"));

        app.run ("transport.setSongMode", args ({ { "on", false } }));
        const auto silentFile = folder.getChildFile ("padrao_vazio.wav");
        const auto silent = app.run ("project.exportWav", args ({ { "path", silentFile.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        rep.check (silent.ok && readWav (silentFile).valid && rmsDb (readWav (silentFile)) < -90.0f, "padrão vazio exporta silêncio, sem erro"_t);

        // projeto com um VST3 que não existe neste PC (o da seção de plugins): exporta sem travar
        if (const auto noPlugin = ctx.work.getChildFile ("Plugin Teste").getChildFile ("Sem Plugin.wnl"); noPlugin.existsAsFile())
        {
            app.run ("project.open", args ({ { "path", noPlugin.getFullPathName() } }));
            Watchdog guard (rep, "exportar projeto com VST3 ausente"_t, 60.0);
            const auto r = app.run ("project.exportWav", args ({ { "path", folder.getChildFile ("sem_plugin.wav").getFullPathName() },
                                                                 { "loops", 1 }, { "what", "pattern" } }));
            rep.check (r.ok, "projeto com VST3 ausente exporta (o canal fica mudo)"_t + (r.ok ? juce::String() : ": " + r.error));
        }

        // dois projetos: A com um sample só dele, B só com o kit
        const auto onlyA = folder.getChildFile ("so do A.wav");
        writeWavFile (onlyA, sine (44100.0, 3.0, 2, 330.0), 44100.0, 16);
        app.run ("project.new");
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        const auto chA = app.run ("channel.addSampler", args ({ { "path", onlyA.getFullPathName() }, { "detectRoot", false } })).value["channel"].toString();
        setGrid (app, chA, "x...............");
        const auto fileA = folder.getChildFile ("Projeto A").getChildFile ("Projeto A.wnl");
        app.run ("project.save", args ({ { "path", fileA.getFullPathName() } }));

        app.run ("project.new");
        app.run ("transport.setSongMode", args ({ { "on", false } }));
        setGrid (app, channelId (model.get(), "Kick"), "x.x.x.x.x.x.x.x.");
        const auto fileB = folder.getChildFile ("Projeto B").getChildFile ("Projeto B.wnl");
        app.run ("project.save", args ({ { "path", fileB.getFullPathName() } }));
        const auto aloneB = folder.getChildFile ("b_sozinho.wav");
        app.run ("project.exportWav", args ({ { "path", aloneB.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));

        app.run ("project.open", args ({ { "path", fileA.getFullPathName() } }));
        pump (1500);
        app.getSampleCache().prune();
        const auto withA = app.getSampleCache().size();
        app.run ("project.open", args ({ { "path", fileB.getFullPathName() } }));
        const auto quickB = folder.getChildFile ("b_depois_do_a.wav");
        app.run ("project.exportWav", args ({ { "path", quickB.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        rep.check (maxDiff (readWav (quickB), readWav (aloneB)) == 0.0f, "abrir A e logo B: B exporta igual, amostra por amostra"_t);
        pump (1500);
        app.getSampleCache().prune();
        const auto withB = app.getSampleCache().size();
        rep.check (withB < withA, "o sample só do A sai da memória ao abrir o B ("_t + juce::String ((int) withA) + " samples no cache com o A, "
                                      + juce::String ((int) withB) + " com o B)");

        // desfazer depois de exportar
        const double bpmBefore = model.get().bpm;
        app.run ("transport.setBpm", args ({ { "bpm", bpmBefore + 10.0 } }));
        app.run ("project.exportWav", args ({ { "path", folder.getChildFile ("antes_de_desfazer.wav").getFullPathName() }, { "loops", 1 },
                                              { "what", "pattern" }, { "background", true } }));
        waitExport (app, 30.0);
        app.run ("edit.undo");
        rep.check (std::abs (model.get().bpm - bpmBefore) < 1.0e-6, "Ctrl+Z depois de exportar volta o BPM (a exportação não vira passo de desfazer)"_t);

        // autosave com a pasta do projeto sem permissão: vai para a pasta de dados, em vez de não acontecer
        const auto blocked = folder.getChildFile ("Sem Permissao").getChildFile ("Sem Permissao.wnl");
        app.run ("project.save", args ({ { "path", blocked.getFullPathName() } }));
        blocked.getSiblingFile ("autosave").replaceWithText ("aqui era para ser a pasta");
        app.run ("transport.setBpm", args ({ { "bpm", 133.0 } }));
        const auto autosave = app.run ("project.autosave");
        const juce::File autosaveFile (autosave.value["path"].toString());
        rep.check ((bool) autosave.value["saved"] && autosaveFile.existsAsFile() && autosaveFile.isAChildOf (ctx.data),
                   "autosave com a pasta do projeto bloqueada vai para a pasta de dados ("_t + autosaveFile.getParentDirectory().getFileName() + ")");
        autosaveFile.deleteFile();
    }

    //==========================================================================
    rep.section ("Produtor: o que a revisão de iniciante pediu (22/10)"_t);
    {
        // O MP3 saía sem o beat: com o beat na playlist e o app no PAT, o Exportar abria em "Padrão atual".
        app.run ("project.new");
        rep.check (ExportDialog::initialWhat (model.get()) == 0, "projeto novo, playlist vazia: o Exportar propõe o padrão"_t);
        app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "unit", "bars" } }));
        rep.check (! model.get().songMode && ExportDialog::initialWhat (model.get()) == 1,
                   "com um bloco na playlist, mesmo no PAT, o Exportar propõe a música inteira"_t);

        // As raias saíam como "Track 1" e as da gravação como "Raia N"
        rep.check (! model.get().playlistTracks.empty() && model.get().playlistTracks.front().name == "Raia 1",
                   "as raias da playlist se chamam \"Raia 1\", \"Raia 2\"... ("_t
                       + (model.get().playlistTracks.empty() ? juce::String() : model.get().playlistTracks.front().name) + ")");

        // Salvar onde não dá: a mensagem diz o que fazer
        const auto notAFolder = folder.getChildFile ("isto e um arquivo.txt");
        notAFolder.replaceWithText ("x");
        const auto failed = app.run ("project.save", args ({ { "path", notAFolder.getChildFile ("Beat").getChildFile ("Beat.wnl").getFullPathName() } }));
        rep.check (! failed.ok && failed.error.contains ("Escolha outra pasta"),
                   "salvar numa pasta que não dá para criar explica o que fazer: \""_t + failed.error.substring (0, 90) + "...\"");

        // "Agora não" na recuperação não perde o autosave: Arquivo > Abrir um autosave o abre e volta para o projeto
        const auto project = folder.getChildFile ("Recuperar").getChildFile ("Recuperar.wnl");
        app.run ("project.save", args ({ { "path", project.getFullPathName() } }));
        app.run ("transport.setBpm", args ({ { "bpm", 151.0 } }));
        const auto autosave = app.run ("project.autosave");
        const juce::File autosaveFile (autosave.value["path"].toString());
        app.run ("project.new");
        const auto reopened = app.openAutosave (autosaveFile);
        rep.check (reopened.wasOk() && std::abs (model.get().bpm - 151.0) < 1.0e-6 && model.file == project && model.dirty,
                   "\"Abrir um autosave\" abre o autosave escolhido, e salvar volta para Recuperar.wnl"_t);
        rep.check (app.autosaveBrowseFolder() == project.getSiblingFile ("autosave"),
                   "o \"Abrir um autosave\" começa na pasta autosave do projeto aberto"_t);
        rep.check (app.openAutosave (folder.getChildFile ("nao existe.wnl")).failed(), "autosave que não existe é recusado com a mensagem"_t);
        notAFolder.deleteFile();
    }

    //==========================================================================
    rep.section ("Produtor: a cauda da música e o WAV que clipa (revisão profissional, 22/10)"_t);
    {
        // Um som de 3 s no primeiro passo de um padrão de 1 compasso (1,7 s a 140 BPM): ele toca além do último bloco.
        app.run ("project.new");
        const auto longFile = folder.getChildFile ("cauda 3s.wav");
        writeWavFile (longFile, sine (48000.0, 3.0, 2, 220.0, 0.5f), 48000.0, 24);
        const auto added = app.run ("channel.addSampler", args ({ { "path", longFile.getFullPathName() }, { "detectRoot", false } }));
        const auto ch = added.value["channel"].toString();
        app.run ("steps.set", args ({ { "channel", ch }, { "index", 0 }, { "on", true } }));
        app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "unit", "bars" } }));
        app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));

        auto exportSong = [&] (const juce::String& name)
        {
            const auto out = folder.getChildFile (name);
            app.run ("project.exportWav", args ({ { "path", out.getFullPathName() }, { "what", "song" }, { "background", true } }));
            const auto status = waitExport (app, 60.0);   // antes de ler: a ordem dos argumentos do make_pair não é garantida
            return std::make_pair (status, readWav (out));
        };

        const auto [st, tail] = exportSong ("com cauda.wav");
        const double seconds = tail.valid ? tail.buffer.getNumSamples() / tail.sampleRate : 0.0;
        const double bar = 4.0 * beat140;
        rep.check ((bool) st["ok"] && seconds > 2.95 && seconds < 3.2,
                   "a música inteira vai até o som acabar: "_t + juce::String (seconds, 2) + " s (o bloco tem "
                       + juce::String (bar, 2) + " s, o som 3 s)");
        rep.check (st["warning"].toString().isEmpty(), "sem clipe, sem aviso"_t);

        // Master a +6 dB com o som a -6 dBFS por trilha: o WAV de 24 bits passa de 0 dB e o app avisa
        app.run ("mixer.setVolume", args ({ { "track", "mx_master" }, { "db", 6.0 } }));
        app.run ("mixer.setVolume", args ({ { "track", added.value["mixerTrack"].toString() }, { "db", 6.0 } }));
        const auto [hot, hotWav] = exportSong ("estourado.wav");
        rep.check ((bool) hot["ok"] && hot["warning"].toString().contains ("passou de 0 dB"),
                   "WAV que clipou sai com o aviso: \""_t + hot["warning"].toString().substring (0, 70) + "...\"");
        juce::ignoreUnused (hotWav);

        // O padrão continua do tamanho exato: um loop vendido não pode ganhar cauda
        const auto loopOut = folder.getChildFile ("loop.wav");
        app.run ("mixer.setVolume", args ({ { "track", "mx_master" }, { "db", 0.0 } }));
        app.run ("project.exportWav", args ({ { "path", loopOut.getFullPathName() }, { "what", "pattern" }, { "loops", 1 }, { "background", true } }));
        waitExport (app, 60.0);
        const auto loop = readWav (loopOut);
        const double loopSeconds = loop.valid ? loop.buffer.getNumSamples() / loop.sampleRate : 0.0;
        rep.check (std::abs (loopSeconds - bar) < 0.01, "o padrão sai no tamanho exato, sem cauda: "_t + juce::String (loopSeconds, 3) + " s");

        // WAV a 44,1 kHz e 16 bits (com dither), o que lojas de beat e distribuidoras pedem
        const auto cdFile = folder.getChildFile ("cd.wav");
        app.run ("project.exportWav", args ({ { "path", cdFile.getFullPathName() }, { "what", "pattern" }, { "loops", 1 },
                                              { "sampleRate", 44100 }, { "bitDepth", 16 }, { "background", true } }));
        waitExport (app, 60.0);
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatReader> cd (wavFormat.createReaderFor (cdFile.createInputStream().release(), true));
        rep.check (cd != nullptr && cd->sampleRate == 44100.0 && cd->bitsPerSample == 16,
                   "o WAV sai a 44,1 kHz e 16 bits quando pedido ("_t
                       + (cd != nullptr ? juce::String (cd->sampleRate, 0) + " Hz, " + juce::String ((int) cd->bitsPerSample) + " bits" : juce::String ("não abriu"))
                       + ")");
        cd = nullptr;
        rep.check (! app.run ("project.exportWav", args ({ { "path", cdFile.getFullPathName() }, { "sampleRate", 22050 }, { "background", true } })).ok,
                   "taxa que não é de música (22050) é recusada com a explicação"_t);
    }

    app.run ("project.new");
    folder.getChildFile ("Samples variados").deleteRecursively();
}

} // namespace whynot::selftest
