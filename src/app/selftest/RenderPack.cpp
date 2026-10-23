// Autoteste do bloco 2 da pesquisa das comunidades (pedido do usuário de 23/10):
// - renderizar no lugar (channel.renderInPlace): a música com o canal renderizado soa igual à de antes, o canal
//   original fica mudo, a trilha nova sai para o mesmo lugar com os mesmos envios, o seco, o desfazer e o que recusa;
// - exportar para vender (project.exportPack): a pasta com o BPM e o tom no nome, o WAV limpo, o MP3 com a tag do
//   produtor no lugar certo, os stems, o zip e a tag guardada (export.setProducerTag);
// - a aba "Para vender" do diálogo de exportação, numa captura sem janela (pacote-*.png).

#include "SelfTestKit.h"
#include "../../engine/Exporter.h"
#include "../../ui/ExportDialogs.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    double nowMs()   { return juce::Time::getMillisecondCounterHiRes(); }

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

    bool savePng (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();

        if (juce::FileOutputStream out (file); out.openedOk())
        {
            juce::PNGImageFormat png;
            return png.writeImageToStream (image, out) && image.getWidth() > 0;
        }

        return false;
    }

    /** A maior diferença entre dois renders, no trecho que os dois têm, em dBFS. */
    float diffDb (const Audio& a, const Audio& b)
    {
        if (! a.valid || ! b.valid || a.buffer.getNumChannels() != b.buffer.getNumChannels())
            return 0.0f;

        const int n = juce::jmin (a.buffer.getNumSamples(), b.buffer.getNumSamples());
        float d = 0.0f;

        for (int c = 0; c < a.buffer.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
                d = juce::jmax (d, std::abs (a.buffer.getSample (c, i) - b.buffer.getSample (c, i)));

        return juce::Decibels::gainToDecibels (d, -200.0f);
    }

    /** O canal de instrumento que mais toca na playlist (notas ou passos num padrão que está nela). */
    juce::String playingChannel (const Project& p)
    {
        juce::String best;
        int bestBlocks = 0;

        for (auto& ch : p.channels)
        {
            if (ch.isAudio() || ch.mute)
                continue;

            int blocks = 0;

            for (auto& item : p.playlistItems)
            {
                const auto* pat = item.kind == "pattern" ? p.findPattern (item.ref) : nullptr;
                const auto steps = pat != nullptr ? pat->steps.find (ch.id) : decltype (pat->steps.end()) {};
                const bool plays = pat != nullptr && (pat->notesFor (ch.id) != nullptr
                                                      || (steps != pat->steps.end() && std::any_of (steps->second.begin(), steps->second.end(),
                                                                                                    [] (const Step& s) { return s.on; })));
                blocks += plays ? 1 : 0;
            }

            if (blocks > bestBlocks)
            {
                best = ch.id;
                bestBlocks = blocks;
            }
        }

        return best;
    }

    /** Um bipe de 1 kHz de 0,6 s a 48 kHz (a taxa diferente da do MP3 testa a conversão): a "tag do produtor" do teste. */
    bool writeTag (const juce::File& file)
    {
        const double rate = 48000.0;
        juce::AudioBuffer<float> tone (1, (int) (0.6 * rate));

        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / rate));

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        juce::WavAudioFormat wav;
        auto writer = stream != nullptr ? wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                                                                        .withNumChannels (1)
                                                                                                        .withBitsPerSample (24))
                                        : nullptr;
        return writer != nullptr && writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
    }
}

void runRenderPackSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto folder = ctx.work.getChildFile ("Bloco2");
    folder.deleteRecursively();
    folder.createDirectory();

    //--------------------------------------------------------------------------
    rep.section ("Bloco 2: renderizar no lugar"_t);

    if (ctx.demoProject.existsAsFile() && app.run ("project.open", args ({ { "path", ctx.demoProject.getFullPathName() } })).ok)
    {
        app.run ("transport.setSongMode", args ({ { "on", true } }));
        const auto ch = playingChannel (model.get());
        const auto* source = model.get().findChannel (ch);
        const auto sourceName = source != nullptr ? source->name : juce::String();
        const auto sourceOutput = source != nullptr ? model.get().resolveOutput (source->output) : juce::String();
        const auto* sourceTrack = model.get().findMixer (sourceOutput);
        const auto trackOutput = sourceTrack != nullptr ? sourceTrack->output : juce::String();
        const auto trackSends = sourceTrack != nullptr ? sourceTrack->sends : std::vector<Send>();

        const auto before = folder.getChildFile ("antes.wav");
        app.run ("project.exportWav", args ({ { "path", before.getFullPathName() } }));
        const auto mixBefore = readWav (before);
        const auto channelsBefore = model.get().channels.size();

        const auto r = app.run ("channel.renderInPlace", args ({ { "channel", ch } }));
        const auto& p = model.get();
        const auto* original = p.findChannel (ch);
        const auto* bounced = p.findChannel (r.value["channel"].toString());
        const auto* newTrack = p.findMixer (r.value["track"].toString());
        const auto* item = p.findItem (r.value["item"].toString());
        const auto rendered = readWav (juce::File (r.value["path"].toString()));

        rep.check (r.ok && original != nullptr && original->mute && bounced != nullptr && bounced->isAudio() && item != nullptr
                       && p.channels.size() == channelsBefore + 1 && rendered.valid && peakDb (rendered) > -40.0f,
                   "renderizar \""_t + sourceName + "\": um canal de áudio novo, num bloco da playlist, com som ("_t
                       + juce::String (rendered.valid ? peakDb (rendered) : -200.0f, 1) + " dBFS), e o original mudo"_t
                       + (r.ok ? juce::String() : ": " + r.error));
        rep.check (newTrack != nullptr && bounced != nullptr && bounced->output == newTrack->id && newTrack->output == trackOutput
                       && newTrack->sends == trackSends && newTrack->inserts.empty(),
                   "a trilha nova é limpa, sai para o mesmo lugar e manda os mesmos envios"_t);

        const bool laneWasEmpty = item != nullptr
                                  && std::count_if (p.playlistItems.begin(), p.playlistItems.end(),
                                                    [item] (const PlaylistItem& i) { return i.track == item->track; }) == 1;
        rep.check (laneWasEmpty, "o bloco vai para uma raia vazia"_t);

        const auto after = folder.getChildFile ("depois.wav");
        app.run ("project.exportWav", args ({ { "path", after.getFullPathName() } }));
        const auto mixAfter = readWav (after);
        const float diff = diffDb (mixBefore, mixAfter);
        rep.check (mixBefore.valid && mixAfter.valid && diff < -50.0f,
                   "a música soa igual com o canal renderizado: diferença máxima "_t + juce::String (diff, 1) + " dBFS");

        app.run ("edit.undo");
        const auto* restored = model.get().findChannel (ch);
        rep.check (restored != nullptr && ! restored->mute && model.get().channels.size() == channelsBefore,
                   "desfazer volta o canal e tira o áudio, num passo"_t);

        const auto dry = app.run ("channel.renderInPlace", args ({ { "channel", ch }, { "effects", false } }));
        const auto* dryChannel = model.get().findChannel (dry.value["channel"].toString());
        rep.check (dry.ok && dryChannel != nullptr && dryChannel->output == model.get().findChannel (ch)->output
                       && dry.value["track"].toString().isEmpty(),
                   "seco: o áudio sai pela mesma trilha do canal, e os efeitos dela continuam ao vivo"_t);
        app.run ("edit.undo");

        // um canal fora da playlist, um mudo e um que não existe: recusados
        app.run ("channel.addSynth");
        const auto& now = model.get();
        const auto lonely = now.channels.back().id;
        const auto notPlaying = app.run ("channel.renderInPlace", args ({ { "channel", lonely } }));
        rep.check (! notPlaying.ok && notPlaying.error.contains ("playlist") && ! app.run ("channel.renderInPlace", args ({ { "channel", "ch_nao" } })).ok,
                   "canal fora da playlist e canal que não existe: recusados, com o motivo"_t);
    }
    else
    {
        rep.check (false, "sem o Demo: renderizar no lugar não testado"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Bloco 2: exportar para vender"_t);
    {
        const auto tag = folder.getChildFile ("tag.wav");
        rep.check (writeTag (tag), "uma tag de teste: bipe de 1 kHz, 0,6 s, 48 kHz"_t);

        const auto saved = app.run ("export.setProducerTag", args ({ { "path", tag.getFullPathName() } }));
        rep.check (saved.ok && juce::File (app.run ("export.producerTag").value["path"].toString()) == tag,
                   "a tag fica guardada (export.setProducerTag)"_t);
        rep.check (! app.run ("export.setProducerTag", args ({ { "path", folder.getChildFile ("nao.wav").getFullPathName() } })).ok,
                   "uma tag que não existe é recusada"_t);

        // o beat do Demo: o pacote inteiro, com stems
        if (ctx.demoProject.existsAsFile() && app.run ("project.open", args ({ { "path", ctx.demoProject.getFullPathName() } })).ok)
        {
            const auto packs = folder.getChildFile ("Pacotes");
            const auto started = app.run ("project.exportPack", args ({ { "folder", packs.getFullPathName() }, { "stems", true } }));
            const auto st = started.ok ? waitExport (app, 180.0) : juce::var();
            const juce::File pack (started.value["folder"].toString());
            const auto base = pack.getFileName();
            const auto wav = readWav (pack.getChildFile (base + ".wav"));
            const auto mp3 = pack.getChildFile (base + " (com tag).mp3"_t);
            const auto stems = pack.getChildFile ("Stems").findChildFiles (juce::File::findFiles, false, "*.wav");
            const auto zipFile = packs.getChildFile (base + ".zip");
            juce::ZipFile zip (zipFile);
            const int packFiles = (int) pack.findChildFiles (juce::File::findFiles, true).size();

            const auto& demo = model.get();
            const auto title = (demo.name.isEmpty() || demo.name == "Sem título"_t) ? model.file.getFileNameWithoutExtension() : demo.name;
            rep.check (started.ok && (bool) st["ok"] && base == packBaseName (demo, title) && base.contains ("bpm"),
                       "o pacote sai numa pasta com o BPM e o tom: \""_t + base + "\"" + (started.ok ? juce::String() : ": " + started.error));
            rep.check (wav.valid && juce::approximatelyEqual (wav.sampleRate, 44100.0) && peakDb (wav) > -20.0f && mp3.existsAsFile()
                           && mp3.getSize() > 10000,
                       "o WAV limpo (44,1 kHz) e o MP3 com a tag"_t);
            rep.check (stems.size() >= 2 && zipFile.existsAsFile() && zip.getNumEntries() == packFiles,
                       juce::String ((int) stems.size()) + " stems, e o zip com os "_t + juce::String (packFiles) + " arquivos da pasta"_t);
        }
        else
        {
            rep.check (false, "sem o Demo: o pacote do beat não testado"_t);
        }

        // um beat em silêncio: o MP3 é só a tag, que tem de tocar no começo e a cada 5 s
        app.run ("project.new");
        app.run ("pattern.add");
        app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "length", 4 }, { "unit", "bars" } }));
        const auto silent = folder.getChildFile ("Silencio");
        const auto started = app.run ("project.exportPack", args ({ { "folder", silent.getFullPathName() }, { "wav", false }, { "zip", false },
                                                                      { "tagEvery", 5 } }));
        const auto st = started.ok ? waitExport (app, 90.0) : juce::var();
        const juce::File pack (started.value["folder"].toString());
        const auto mp3 = readWav (pack.getChildFile (pack.getFileName() + " (com tag).mp3"_t));
        const float first = juce::Decibels::gainToDecibels (peakIn (mp3, 0.6, 1.0), -200.0f);
        const float gap = juce::Decibels::gainToDecibels (peakIn (mp3, 1.6, 5.2), -200.0f);
        const float second = juce::Decibels::gainToDecibels (peakIn (mp3, 5.6, 6.0), -200.0f);
        rep.check (started.ok && (bool) st["ok"] && mp3.valid && first > -12.0f && gap < -50.0f && second > -12.0f,
                   "a tag no MP3: no começo ("_t + juce::String (first, 1) + " dBFS), silêncio entre ("_t + juce::String (gap, 1)
                       + " dBFS) e de novo aos 5,5 s ("_t + juce::String (second, 1) + " dBFS)" + (started.ok ? juce::String() : ": " + started.error));

        const auto none = app.run ("project.exportPack", args ({ { "folder", silent.getFullPathName() }, { "wav", false }, { "mp3", false } }));
        rep.check (! none.ok, "pacote sem nenhum arquivo marcado é recusado"_t);

        app.run ("project.new");
        const auto empty = app.run ("project.exportPack", args ({ { "folder", silent.getFullPathName() } }));
        rep.check (! empty.ok && empty.error.contains ("playlist"), "sem nada na playlist, o pacote explica que leva a música inteira"_t);

        // a aba "Para vender", sem janela
        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        ExportDialog dialog (app, ExportDialog::Format::pack);
        host.setSize (dialog.getWidth(), dialog.getHeight());
        host.addAndMakeVisible (dialog);
        rep.check (savePng (dialog, folder.getChildFile ("pacote-dialogo.png")) && dialog.getHeight() > 200,
                   "a aba Para vender do Exportar, com a tag escolhida"_t);
        host.removeChildComponent (&dialog);
        host.setLookAndFeel (nullptr);

        app.run ("export.setProducerTag", args ({ { "path", juce::String() } }));
    }

    app.run ("project.new");
}

} // namespace whynot::selftest
