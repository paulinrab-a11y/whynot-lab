// Autoteste do desempenho (passada de 29/10, frente Desempenho): um projeto pesado de voz, com 300 blocos de áudio
// sobrepostos numa raia (a voz picotada, com crossfade e pan) e um canal mono com 2000 notas. Mede o que a interface
// e o motor fazem a cada mudança do projeto: reconciliar o motor (Projector::sync), os crossfades automáticos, atualizar
// e pintar a playlist e o piano roll. Os números vão para o relatório; os "antes e depois" ficam no docs/desempenho-ui.md.

#include "SelfTestKit.h"
#include "../../model/PlaylistEdit.h"
#include "../../ui/PianoRoll.h"
#include "../../ui/Playlist.h"
#include "../../ui/Theme.h"

#include <algorithm>
#include <vector>

namespace whynot::selftest
{

namespace
{
    double nowMs()   { return juce::Time::getMillisecondCounterHiRes(); }

    template <typename F>
    double medianMs (int n, F&& f)
    {
        std::vector<double> t;

        for (int i = 0; i < n; ++i)
        {
            const double t0 = nowMs();
            f (i);
            t.push_back (nowMs() - t0);
        }

        std::sort (t.begin(), t.end());
        return t[t.size() / 2];
    }

    juce::String ms (double v)   { return juce::String (v, v < 10.0 ? 2 : 1) + " ms"; }

    void pump (int milliseconds)   { juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds); }

    /** Uma voz de teste: 1,5 s mono (o pan do bloco faz o Projector tocar a cópia estéreo ao lado). */
    bool writeVoice (const juce::File& f)
    {
        const double rate = 44100.0;
        const int n = (int) (1.5 * rate);
        juce::AudioBuffer<float> b (1, n);

        for (int i = 0; i < n; ++i)
            b.setSample (0, i, (float) (0.25 * std::sin (juce::MathConstants<double>::twoPi * (180.0 + 40.0 * i / n) * i / rate)));

        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (16));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, n);
    }
}

void runHeavyProjectSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    rep.section ("Desempenho: voz picotada (300 blocos sobrepostos com pan) e 2000 notas num canal mono"_t);

    const auto folder = ctx.work.getChildFile ("pesado");
    const auto voice = folder.getChildFile ("voz-mono.wav");
    rep.check (writeVoice (voice), "WAV mono de 1,5 s para a voz"_t);

    if (! app.run ("project.new").ok)
    {
        rep.check (false, "projeto novo"_t);
        return;
    }

    // o projeto fica salvo na pasta do teste: a cópia estéreo do pan e os caminhos relativos saem dali
    app.run ("project.save", args ({ { "path", folder.getChildFile ("Pesado.wnl").getFullPathName() } }));

    juce::String monoId;

    for (auto& c : model.get().channels)
        if (! c.isAudio() && monoId.isEmpty())
            monoId = c.id;

    constexpr int numBlocks = 300, numNotes = 2000;
    const double t0 = nowMs();
    {
        CommandRegistry::ScopedGesture gesture (app.getCommands(), "Montar o projeto pesado");

        // a cada 3 passos um bloco de 1 tempo: cada um entra no fim do anterior (crossfade); metade com pan
        for (int k = 0; k < numBlocks; ++k)
            app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "path", voice.getFullPathName() }, { "track", 1 + (k / 100) },
                                                 { "start", (k % 100) * 3 * ticksPerStep }, { "length", 4 * ticksPerStep },
                                                 { "pan", k % 2 == 0 ? 0.0 : (k % 4 == 1 ? -0.5 : 0.5) } }));

        app.run ("channel.setMono", args ({ { "channel", monoId }, { "on", true } }));
        juce::Array<juce::var> notes;

        for (int n = 0; n < numNotes; ++n)
            notes.add (noteVar (n * 0.25, 0.25, 36 + (n * 7) % 24, false));

        addNotes (app, monoId, notes);
    }
    pump (120);
    app.run ("project.save");   // fica para o --render que compara o som antes e depois

    const auto& p = model.get();
    int audioBlocks = 0, panned = 0;

    for (auto& i : p.playlistItems)
        if (i.kind == "audio")
        {
            ++audioBlocks;
            panned += i.pan != 0.0f ? 1 : 0;
        }

    const auto* mono = p.findChannel (monoId);
    rep.check (audioBlocks == numBlocks && panned == numBlocks / 2 && mono != nullptr && mono->mono,
               juce::String (audioBlocks) + " blocos de áudio (" + juce::String (panned) + " com pan), canal mono "_t
                   + (mono != nullptr ? mono->name : juce::String ("?")) + "; montado em " + ms (nowMs() - t0));

    //--------------------------------------------------------------------------
    // O motor e os crossfades
    const double tSync = medianMs (9, [&] (int) { app.getProjector().sync (model.get(), model.projectFolder()); });
    rep.check (true, "motor: reconciliar o projeto pesado sem mudança "_t + ms (tSync));

    // o que um passo de arraste custa no motor: um bloco da voz anda um passo e o motor reconcilia (ida e volta)
    {
        Project a = model.get(), b = model.get();
        a.songMode = b.songMode = true;   // os blocos de áudio só entram no motor no SONG
        auto audioItem = std::find_if (b.playlistItems.begin(), b.playlistItems.end(), [] (const PlaylistItem& i) { return i.kind == "audio"; });

        if (audioItem != b.playlistItems.end())
            audioItem->start += ticksPerStep;

        const double tMove = medianMs (9, [&] (int i) { app.getProjector().sync (i % 2 == 0 ? b : a, model.projectFolder()); });
        app.getProjector().sync (model.get(), model.projectFolder());
        rep.check (true, "motor: mover um bloco da voz e reconciliar "_t + ms (tMove));
    }

    int crossed = 0;
    const double tCross = medianMs (5, [&] (int)
    {
        crossed = 0;

        for (auto& i : model.get().playlistItems)
            if (const auto x = playlistedit::autoCrossfade (model.get(), i); x.in > 0.0 || x.out > 0.0)
                ++crossed;
    });

    rep.check (crossed >= numBlocks - 3, "crossfades automáticos de todos os blocos, um a um: "_t + ms (tCross) + " (" + juce::String (crossed) + " com crossfade)");

    // todos de uma vez (o que a playlist e o motor usam desde 29/10): o mesmo resultado, bit a bit
    std::vector<playlistedit::Crossfade> batch;
    const double tBatch = medianMs (5, [&] (int) { batch = playlistedit::autoCrossfades (model.get()); });

    auto sameAsOneByOne = [] (const Project& q)
    {
        const auto all = playlistedit::autoCrossfades (q);

        if (all.size() != q.playlistItems.size())
            return false;

        for (size_t k = 0; k < all.size(); ++k)
        {
            const auto one = playlistedit::autoCrossfade (q, q.playlistItems[k]);

            if (one.in != all[k].in || one.out != all[k].out)
                return false;
        }

        return true;
    };

    // e num projeto torto: blocos mudos, uma raia em solo, blocos longos que cobrem vários, inícios iguais, um dentro
    // do outro (camada) e blocos de padrão no meio
    Project tricky = model.get();
    juce::Random random (29);

    for (size_t k = 0; k < tricky.playlistItems.size(); ++k)
    {
        auto& item = tricky.playlistItems[k];
        item.muted = random.nextInt (9) == 0;

        if (random.nextInt (7) == 0)   item.length *= 1 + random.nextInt (6);
        if (random.nextInt (8) == 0)   item.start = tricky.playlistItems[k / 2].start;
        if (random.nextInt (10) == 0)  item.kind = "pattern";
    }

    const bool sameHeavy = sameAsOneByOne (model.get());
    const bool sameTricky = sameAsOneByOne (tricky);

    if (! tricky.playlistTracks.empty())
        tricky.playlistTracks.front().solo = true;

    rep.check (sameHeavy && sameTricky && sameAsOneByOne (tricky),
               "crossfades todos de uma vez: "_t + ms (tBatch) + ", iguais aos de um em um, bit a bit (voz picotada, projeto torto e com uma raia em solo)");

    //--------------------------------------------------------------------------
    // A interface, numa área de 1920x1080 fora da tela
    theme::LookAndFeel laf;
    juce::Component host;
    host.setLookAndFeel (&laf);
    host.setSize (1920, 1080);

    struct ViewTimes { double refresh = 0, full = 0, column = 0; };

    auto timeView = [&] (juce::Component& view, std::function<void()> refresh)
    {
        ViewTimes t;
        host.addAndMakeVisible (view);
        view.setBounds (host.getLocalBounds());
        refresh();
        pump (30);
        (void) view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);   // aquece os caches de fonte

        t.refresh = medianMs (7, [&] (int) { refresh(); });
        t.full = medianMs (5, [&] (int) { (void) view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f); });
        t.column = medianMs (15, [&] (int i) { (void) view.createComponentSnapshot ({ 700 + 9 * i, 0, 5, view.getHeight() }, true, 1.0f); });
        host.removeChildComponent (&view);
        return t;
    };

    const auto& heavy = model.get();
    Playlist list (app);
    PianoRoll roll (app);
    roll.setChannel (monoId);

    const auto tList = timeView (list, [&] { list.refresh (heavy); });
    const auto tRoll = timeView (roll, [&] { roll.refresh (heavy); });

    auto line = [&] (const char* name, const ViewTimes& t)
    {
        return juce::String::fromUTF8 (name) + ": atualizar " + ms (t.refresh) + ", pintar inteira " + ms (t.full) + ", coluna da agulha " + ms (t.column);
    };

    rep.check (true, line ("playlist com a voz picotada", tList));
    rep.check (true, line ("piano roll do canal mono com 2000 notas", tRoll));

    host.setLookAndFeel (nullptr);
    app.run ("project.new");
}

} // namespace whynot::selftest
