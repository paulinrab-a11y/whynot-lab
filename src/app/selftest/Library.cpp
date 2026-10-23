// Autoteste da tela Projetos (pedidos de 21/10, item 3.3; frente Projetos): a biblioteca lê os .wnl como
// dado, a prévia em FLAC sai sem abrir o projeto e igual ao WAV exportado, salvar deixa a prévia velha
// "desatualizada" e põe a nova na fila, o play pausa o render, o deck toca a prévia amostra por amostra
// e a tela desenha sem placa nem janela na área de trabalho.

#include "SelfTestKit.h"

#include "../../dsp/LoudnessMeter.h"
#include "../../engine/Exporter.h"
#include "../../engine/PianoLibrary.h"
#include "../../engine/SynthFavorites.h"
#include "../../engine/UserMix.h"
#include "../../library/ImportDialog.h"
#include "../../library/ProjectImport.h"
#include "../../library/ProjectLibrary.h"
#include "../../library/ProjectsView.h"
#include "../../model/WnlFormat.h"
#include "../../ui/Theme.h"
#include "../../ui/ViewRail.h"

#include <thread>

namespace whynot::selftest
{

namespace
{
    using library::PreviewState;
    using library::previewAudio;
    using library::previewFolder;

    /** Se o trecho guardado não acabar em `seconds`, escreve a falha no relatório e encerra o processo
        (o mesmo vigia da caça de bugs do produtor): um travamento vira uma falha com nome. */
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

    /** Roda a fila de mensagens até `done` ou o tempo acabar (os timers da fila e da biblioteca andam). */
    bool pumpUntil (const std::function<bool()>& done, int timeoutMs)
    {
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (! done())
        {
            if (juce::Time::getMillisecondCounter() > until)
                return false;

            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }

        return true;
    }

    float lufsOf (const Audio& a)
    {
        dsp::LoudnessMeter meter;
        meter.prepare (a.sampleRate);
        const int n = a.buffer.getNumSamples();
        meter.process (a.buffer.getReadPointer (0), a.buffer.getReadPointer (juce::jmin (1, a.buffer.getNumChannels() - 1)), n);
        return meter.integrated();
    }

    /** O deck tocando pelo callback da placa, sem placa: blocos de 512 até o fim (ou o limite). */
    juce::AudioBuffer<float> playDeck (library::ProjectLibrary& lib, double rate, int maxBlocks,
                                       const std::function<void (int block)>& everyBlock = {}, bool stopAtEnd = true)
    {
        auto& decks = lib.getDecks();
        decks.prepareForTest (rate);
        constexpr int block = 512;
        juce::AudioBuffer<float> out (2, maxBlocks * block);
        out.clear();
        juce::AudioBuffer<float> io (2, block);
        juce::AudioIODeviceCallbackContext context;
        int written = 0;

        for (int b = 0; b < maxBlocks; ++b)
        {
            decks.audioDeviceIOCallbackWithContext (nullptr, 0, io.getArrayOfWritePointers(), 2, block, context);
            out.copyFrom (0, written, io, 0, 0, block);
            out.copyFrom (1, written, io, 1, 0, block);
            written += block;

            if (everyBlock)
                everyBlock (b);

            if ((b & 3) == 3)
            {
                decks.service();
                juce::Thread::sleep (1);   // a thread de leitura enche o anel (um bloco de 512 dura 10 ms de verdade)
            }

            if (stopAtEnd && decks.reachedEnd())
                break;
        }

        out.setSize (2, written, true);
        return out;
    }

    /** Sem placa, ninguém chama o callback com o player parado. Dois blocos de "placa parada" deixam o player
        no estado em que a placa de verdade o deixaria (o master já desceu, as cenas novas já entraram). */
    void settle (library::ProjectLibrary& lib)
    {
        playDeck (lib, 48000.0, 2, {}, false);
    }

    double rms (const juce::AudioBuffer<float>& b, int from, int count)
    {
        double sum = 0.0;
        count = juce::jmin (count, b.getNumSamples() - from);

        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = from; i < from + count; ++i)
                sum += (double) b.getSample (c, i) * b.getSample (c, i);

        return count > 0 ? std::sqrt (sum / (count * b.getNumChannels())) : 0.0;
    }

    juce::Image snapshotOffDesktop (juce::Component& c, int w, int h)
    {
        // num pai fora da área de trabalho: nada aparece na tela nem rouba o foco do usuário
        theme::LookAndFeel lf;
        juce::Component stage;
        stage.setLookAndFeel (&lf);
        stage.setSize (w, h);
        stage.addAndMakeVisible (c);
        c.setBounds (stage.getLocalBounds());
        auto image = juce::SoftwareImageType().convert (stage.createComponentSnapshot (stage.getLocalBounds(), true, 1.0f));
        stage.removeChildComponent (&c);
        stage.setLookAndFeel (nullptr);
        return image;
    }

    int countPixels (const juce::Image& image, const std::function<bool (juce::Colour)>& match)
    {
        int n = 0;
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (match (data.getPixelColour (x, y)))
                    ++n;

        return n;
    }

    bool isSignal (juce::Colour c)
    {
        return c.getRed() > 200 && c.getGreen() > 90 && c.getGreen() < 150 && c.getBlue() < 70;
    }

    bool savePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (image, out);
    }
}

void runLibrarySelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    rep.section ("Projetos: biblioteca, prévia em FLAC e deck (pedidos de 21/10, item 3.3)"_t);
    auto* lib = app.library();
    rep.check (lib != nullptr, "a biblioteca de projetos existe"_t);

    if (lib == nullptr)
        return;

    lib->getRenderer().minFreeMemoryMB = 0;   // o teste não espera memória livre
    const auto root = ctx.work.getChildFile ("projetos");
    root.deleteRecursively();

    //--------------------------------------------------------------------------
    // Dois projetos salvos, cada um na sua pasta: A (padrão, 140 BPM) e B (música de 128 compassos, 150 BPM, Lá menor)
    const auto fileA = root.getChildFile ("Beat A").getChildFile ("Beat A.wnl");
    const auto fileB = root.getChildFile ("Beat B").getChildFile ("Beat B.wnl");

    app.run ("project.new");
    setGrid (app, channelId (model.get(), "Kick"), "x.....x...x.....");
    setGrid (app, channelId (model.get(), "Clap"), "....x.......x...");
    setGrid (app, channelId (model.get(), "Hat"), "x.x.x.x.x.x.x.x.", 90);
    rep.check (app.saveProject (fileA).wasOk(), "salvou o projeto A"_t);

    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 150.0 } }));
    app.run ("project.setKey", args ({ { "root", "A" }, { "scale", "minor" } }));
    setGrid (app, channelId (model.get(), "Kick"), "x.........x.....");
    setGrid (app, channelId (model.get(), "808"), "x.........x.....", 110);
    app.run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "length", 128 }, { "unit", "bars" } }));
    rep.check (app.saveProject (fileB).wasOk(), "salvou o projeto B (com a playlist)"_t);
    rep.check (! previewAudio (fileA).exists() && ! previewAudio (fileB).exists(),
               "sem janela, salvar não renderiza a prévia (o render a cada Ctrl+S é do app com janela)"_t);

    //--------------------------------------------------------------------------
    // A varredura acha os dois e lê BPM e tom sem abrir
    lib->setRoots ({ root });
    lib->refresh();
    rep.check (pumpUntil ([lib] { return ! lib->isScanning(); }, 15000), "a varredura das pastas termina"_t);

    const auto* a = lib->find (fileA);
    const auto* b = lib->find (fileB);
    rep.check (a != nullptr && b != nullptr, "a lista tem os dois projetos ("_t + juce::String ((int) lib->items().size()) + ")");

    if (a == nullptr || b == nullptr)
        return;

    rep.check (std::abs (a->entry.bpm - 140.0) < 0.01 && std::abs (b->entry.bpm - 150.0) < 0.01, "BPM lido do .wnl (140 e 150)"_t);
    rep.check (b->entry.keyName() == "Am" && b->entry.hasSong && ! a->entry.hasSong, "tom de B = Am; B tem playlist, A não"_t);
    rep.check (a->state == PreviewState::none && b->state == PreviewState::none, "os dois começam sem prévia"_t);
    rep.check (a->entry.hash == library::hashFile (fileA), "o hash da entrada é o do arquivo"_t);

    // a lista também traz os recentes (os projetos que as outras seções salvaram)
    const auto listed = app.run ("library.list");
    int listedAB = 0;

    if (auto* projects = listed.value["projects"].getArray())
        for (auto& p : *projects)
            if (p["path"].toString() == fileA.getFullPathName() || p["path"].toString() == fileB.getFullPathName())
                ++listedAB;

    rep.check (listed.ok && listedAB == 2, "library.list devolve os dois projetos"_t);

    //--------------------------------------------------------------------------
    // Prévia de A sem abrir A (o B é o projeto aberto)
    rep.check (app.run ("library.renderPreview", args ({ { "path", fileA.getFullPathName() } })).ok, "library.renderPreview aceita o A"_t);
    rep.check (pumpUntil ([lib] { return lib->getRenderer().isIdle(); }, 60000), "a prévia de A ficou pronta"_t);
    rep.check (model.file == fileB, "o projeto aberto continua o B (só um projeto vivo no motor)"_t);

    a = lib->find (fileA);
    auto infoA = library::readPreviewInfo (fileA);
    rep.check (a != nullptr && a->state == PreviewState::ok, "estado da prévia de A: pronta"_t);
    rep.check (previewAudio (fileA).existsAsFile() && library::previewJson (fileA).existsAsFile(),
               "preview/Beat A.flac e Beat A.preview.json na pasta do projeto"_t);
    rep.check (infoA.valid && infoA.wnlHash == library::hashFile (fileA), "o json guarda o hash do .wnl"_t);

    const double barsA = 4.0 * 4.0 * 60.0 / 140.0;   // o padrão de 1 compasso, 4 vezes
    rep.check (std::abs (infoA.seconds - barsA) < 0.5, "duração da prévia de A: "_t + juce::String (infoA.seconds, 2) + " s (o padrão 4 vezes)");
    rep.check (infoA.what == "pattern" && (int) infoA.peaks.size() == library::PreviewRenderer::numPeaks,
               "A sem playlist: a prévia é o padrão; a onda tem os picos"_t);

    auto flacA = readWav (previewAudio (fileA));
    rep.check (flacA.valid && flacA.sampleRate == 48000.0 && flacA.buffer.getNumChannels() == 2,
               "o FLAC abre: 48 kHz, estéreo"_t);
    const float measured = flacA.valid ? lufsOf (flacA) : -100.0f;
    rep.check (infoA.lufs > -40.0f && std::abs (measured - infoA.lufs) < 0.1f,
               "LUFS do json ("_t + juce::String (infoA.lufs, 2) + ") = o medido no FLAC (" + juce::String (measured, 2) + ")");

    // soa igual: o FLAC de 16 bits contra o WAV de 24 bits que o Exportar faria do mesmo .wnl
    {
        Project projectA;
        wnl::load (fileA, projectA);
        const auto wav = ctx.work.getChildFile ("projetos-a-export.wav");
        const auto r = renderToWav (app.getEngine(), app.getSampleCache(), projectA, fileA.getParentDirectory(), false, 4, wav,
                                    &app.getPluginCatalog(), 48000.0, 24);
        const auto exported = readWav (wav);
        const float diff = r.wasOk() && exported.valid && flacA.valid ? maxDiff (flacA, exported) : 1.0f;
        rep.check (diff <= 0.51f / 32768.0f,
                   "a prévia soa igual ao WAV exportado (diferença máxima "_t + juce::String (diff * 32768.0f, 2) + " LSB de 16 bits)");
    }

    //--------------------------------------------------------------------------
    // Salvar de novo: a prévia velha fica desatualizada e a nova entra na fila (decisão: a cada Ctrl+S)
    lib->renderOnSave = true;
    app.openProject (fileA);
    setGrid (app, channelId (model.get(), "Hat"), "x.x.x.x.x.x.3.x.", 90);
    rep.check (app.saveProject ({}).wasOk(), "salvou o A mudado"_t);
    rep.check (library::readPreviewInfo (fileA).wnlHash != library::hashFile (fileA), "a prévia velha não bate com o .wnl novo"_t);
    a = lib->find (fileA);
    rep.check (a != nullptr && (a->state == PreviewState::queued || a->state == PreviewState::rendering),
               "salvar pôs a prévia de A na fila"_t);
    rep.check (pumpUntil ([lib] { return lib->getRenderer().isIdle(); }, 60000), "a prévia nova de A ficou pronta"_t);
    a = lib->find (fileA);
    rep.check (a != nullptr && a->state == PreviewState::ok && library::readPreviewInfo (fileA).wnlHash == library::hashFile (fileA),
               "depois do render, a prévia de A está em dia"_t);
    lib->renderOnSave = false;
    flacA = readWav (previewAudio (fileA));   // o deck toca a prévia nova: é com ela que se compara
    infoA = library::readPreviewInfo (fileA);

    //--------------------------------------------------------------------------
    // O play pausa o render: uma fila só do teste, com o "tocando" na mão
    {
        bool playing = true;
        library::PreviewRenderer queue ({ app.getEngine(), app.getSampleCache(), &app.getPluginCatalog(),
                                          [&playing] { return playing; }, {} });
        queue.minFreeMemoryMB = 0;
        queue.enqueue (fileB);
        rep.check (queue.stateOf (fileB) == PreviewState::queued && queue.waitingReason().isNotEmpty(),
                   "tocando, a prévia espera na fila: \""_t + queue.waitingReason() + "\"");

        playing = false;
        rep.check (pumpUntil ([&queue, &fileB] { return queue.stateOf (fileB) == PreviewState::rendering; }, 5000),
                   "parou de tocar: o render de B começa"_t);
        playing = true;
        rep.check (pumpUntil ([&queue, &fileB] { return queue.stateOf (fileB) == PreviewState::queued; }, 10000),
                   "deu play no meio: o render cancela e B volta para a fila"_t);
        rep.check (! previewFolder (fileB).getChildFile ("Beat B.render.wav").exists()
                       && ! previewFolder (fileB).getChildFile ("Beat B.flac.part").exists(),
                   "o render cancelado não deixa arquivo pela metade"_t);

        playing = false;
        rep.check (pumpUntil ([&queue] { return queue.isIdle(); }, 120000), "parou de novo: a prévia de B termina"_t);
        const auto infoB = library::readPreviewInfo (fileB);
        const double barsB = 128.0 * 4.0 * 60.0 / 150.0;
        rep.check (infoB.valid && infoB.what == "song" && std::abs (infoB.seconds - barsB) < 1.0,
                   "B com playlist: a prévia é a música ("_t + juce::String (infoB.seconds, 1) + " s de " + juce::String (barsB, 1) + ")");
    }

    lib->refresh();
    pumpUntil ([lib] { return ! lib->isScanning(); }, 15000);

    //--------------------------------------------------------------------------
    // O deck toca a prévia de A pelo callback da placa, sem placa
    {
        const auto r = app.run ("library.play", args ({ { "path", fileA.getFullPathName() } }));
        rep.check (r.ok && (bool) r.value["playing"], "library.play põe A no deck e toca"_t);

        const auto out = playDeck (*lib, 48000.0, 4000);
        auto& decks = lib->getDecks();
        rep.check (decks.reachedEnd() && decks.underruns() == 0,
                   "o deck tocou até o fim sem falta de áudio (faltas: "_t + juce::String (decks.underruns()) + ")");

        // depois da subida de 5 ms, a saída é o arquivo, amostra por amostra
        const int skip = 512;
        float diff = flacA.valid ? 0.0f : 1.0f;
        const int n = flacA.valid ? juce::jmin (out.getNumSamples(), flacA.buffer.getNumSamples()) : 0;

        for (int c = 0; c < 2; ++c)
            for (int i = skip; i < n; ++i)
                diff = juce::jmax (diff, std::abs (out.getSample (c, i) - flacA.buffer.getSample (c, i)));

        rep.check (n > flacA.buffer.getNumSamples() - 600 && diff == 0.0f,
                   "na mesma taxa, o deck devolve a prévia sem mudar uma amostra (diferença "_t + juce::String (diff) + ")");

        decks.service();
        rep.check (! lib->isDeckPlaying() && std::abs (lib->deckPosition() - infoA.seconds) < 0.05,
                   "no fim o deck para, na posição do fim"_t);
    }

    // Placa a 44,1 kHz: a mesma prévia, interpolada, com a mesma duração e o mesmo nível
    {
        lib->playPreview (fileA);
        const auto out = playDeck (*lib, 44100.0, 4000);
        const double seconds = out.getNumSamples() / 44100.0;
        const double levelDeck = juce::Decibels::gainToDecibels (rms (out, 512, out.getNumSamples()));
        const double levelFile = juce::Decibels::gainToDecibels (flacA.valid ? rms (flacA.buffer, 512, flacA.buffer.getNumSamples()) : 0.0);
        rep.check (std::abs (seconds - infoA.seconds) < 0.05 && std::abs (levelDeck - levelFile) < 0.2,
                   "a 44,1 kHz: "_t + juce::String (seconds, 2) + " s e " + juce::String (levelDeck - levelFile, 2) + " dB de diferença no nível");
    }

    // A prévia é refeita com o deck tocando: o deck larga o arquivo, a troca acontece e ele volta no mesmo ponto
    {
        lib->playPreview (fileA, 2.0);
        playDeck (*lib, 48000.0, 20);   // ~0,2 s tocando
        const double before = lib->deckPosition();
        lib->renderPreview (fileA);

        // a placa continua chamando o callback (mesmo com o deck parado no fim) enquanto a fila renderiza e troca o arquivo
        auto& decks = lib->getDecks();
        juce::AudioBuffer<float> io (2, 512);
        juce::AudioIODeviceCallbackContext context;
        const auto until = juce::Time::getMillisecondCounter() + 60000u;

        for (int n = 1; ! lib->getRenderer().isIdle() && juce::Time::getMillisecondCounter() < until; ++n)
        {
            decks.audioDeviceIOCallbackWithContext (nullptr, 0, io.getArrayOfWritePointers(), 2, 512, context);

            if (n % 4 == 0)
            {
                decks.service();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            }
        }

        const bool idle = lib->getRenderer().isIdle();
        const auto* fa = lib->find (fileA);
        rep.check (idle && fa != nullptr && fa->state == PreviewState::ok,
                   "com o deck tocando a prévia, a prévia nova entra no lugar"_t);
        rep.check (lib->getDecks().fileOf (0) == previewAudio (fileA) && before > 1.9,
                   "o deck voltou para a prévia de A (estava em "_t + juce::String (before, 2) + " s)");
    }

    //--------------------------------------------------------------------------
    // Fatia 2: A/B. O B é o A com o master 6 dB abaixo: com o volume igualado, trocar não muda nada.
    {
        const auto fileA6 = root.getChildFile ("Beat A6").getChildFile ("Beat A6.wnl");
        app.openProject (fileA);
        app.run ("mixer.setVolume", args ({ { "track", masterTrackId }, { "db", -6.0 } }));
        rep.check (app.saveProject (fileA6).wasOk(), "salvou o A6 (o A com o master em -6 dB)"_t);
        lib->renderPreview (fileA6);
        rep.check (pumpUntil ([lib] { return lib->getRenderer().isIdle(); }, 60000), "a prévia do A6 ficou pronta"_t);
        const auto infoA6 = library::readPreviewInfo (fileA6);
        rep.check (std::abs ((infoA.lufs - 6.0f) - infoA6.lufs) < 0.1f,
                   "o A6 está 6 dB abaixo do A ("_t + juce::String (infoA.lufs, 2) + " e " + juce::String (infoA6.lufs, 2) + " LUFS)");

        lib->stopDeck();
        lib->setSync (library::ProjectLibrary::Sync::bar);
        lib->setLoopBars (0);
        lib->setActiveDeck (0);
        lib->setMatchLoudness (true);
        rep.check (lib->loadDeck (0, fileA, 0.0).wasOk() && app.run ("library.loadDeck", args ({ { "deck", "B" }, { "path", fileA6.getFullPathName() },
                                                                                               { "from", 0.0 } })).ok,
                   "A no deck A e A6 no deck B"_t);
        rep.check (std::abs (lib->matchGainDb (0) + 6.0f) < 0.1f && std::abs (lib->matchGainDb (1)) < 0.01f,
                   "igualar pelo mais baixo: A desce "_t + juce::String (lib->matchGainDb (0), 2) + " dB, B fica");

        // toca 300 blocos e troca A → B no bloco 100 e B → A no 200 (o Tab); a saída tem de ser o A igualado
        settle (*lib);
        lib->toggleDeck();
        const auto out = playDeck (*lib, 48000.0, 300, [&] (int b)
        {
            if (b == 100) app.run ("library.switchDeck");
            if (b == 200) app.run ("library.setActive", args ({ { "deck", "A" } }));
        }, false);

        const float gA = juce::Decibels::decibelsToGain (lib->matchGainDb (0));
        float worst = 0.0f;

        for (int c = 0; c < 2; ++c)
            for (int i = 512; i < juce::jmin (out.getNumSamples(), flacA.buffer.getNumSamples()); ++i)
                worst = juce::jmax (worst, std::abs (out.getSample (c, i) - flacA.buffer.getSample (c, i) * gA));

        rep.check (worst < 0.004f, "trocar A → B → A com o volume igualado não muda o som (diferença máxima "_t
                                       + juce::String (juce::Decibels::gainToDecibels (worst, -120.0f), 1) + " dB)");
        rep.check (lib->getDecks().underruns() == 0, "os dois decks juntos, sem falta de áudio"_t);

        // sem igualar: a troca anda em 20 ms (o maior salto entre amostras não passa o do próprio A)
        lib->stopDeck();
        lib->setMatchLoudness (false);
        settle (*lib);
        lib->toggleDeck();
        const auto raw = playDeck (*lib, 48000.0, 160, [&] (int b) { if (b == 100) lib->setActiveDeck (1); }, false);
        lib->setActiveDeck (0);

        auto maxStep = [] (const juce::AudioBuffer<float>& x, int from, int to)
        {
            float m = 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = juce::jmax (1, from); i < juce::jmin (to, x.getNumSamples()); ++i)
                    m = juce::jmax (m, std::abs (x.getSample (c, i) - x.getSample (c, i - 1)));
            return m;
        };

        const float stepAtSwitch = maxStep (raw, 100 * 512, 100 * 512 + 2048);
        const float stepInA = maxStep (flacA.buffer, 0, flacA.buffer.getNumSamples());
        const double before = juce::Decibels::gainToDecibels (rms (raw, 60 * 512, 40 * 512));
        const double after = juce::Decibels::gainToDecibels (rms (raw, 120 * 512, 40 * 512));
        const double refBefore = juce::Decibels::gainToDecibels (rms (flacA.buffer, 60 * 512, 40 * 512));
        const double refAfter = juce::Decibels::gainToDecibels (rms (flacA.buffer, 120 * 512, 40 * 512));
        rep.check (stepAtSwitch <= stepInA * 1.01f,
                   "sem igualar, a troca não pula (maior salto "_t + juce::String (stepAtSwitch, 4) + ", o do A " + juce::String (stepInA, 4) + ")");
        rep.check (std::abs ((after - refAfter) - (before - refBefore) + 6.0) < 0.2,
                   "sem igualar, o B soa 6 dB abaixo ("_t + juce::String ((after - refAfter) - (before - refBefore), 2) + " dB)");

        // mesmo compasso com andamentos diferentes: A a 140 BPM e B a 150 BPM
        lib->stopDeck();
        lib->setMatchLoudness (true);
        lib->loadDeck (0, fileA, 0.0);
        lib->unloadDeck (1);
        settle (*lib);
        lib->toggleDeck();
        playDeck (*lib, 48000.0, 100, {}, false);
        rep.check (lib->loadDeck (1, fileB).wasOk(), "B (150 BPM) entra no deck B com o A tocando"_t);
        const double barA = 4.0 * 60.0 / 140.0, barB = 4.0 * 60.0 / 150.0;
        playDeck (*lib, 48000.0, 2, {}, false);   // a cena nova entra no áudio
        const double posA = lib->deckPosition (0), posB = lib->deckPosition (1);
        rep.check (std::abs (posB - posA / barA * barB) < 0.03,
                   "mesmo compasso: A em "_t + juce::String (posA / barA, 3) + " compassos, B em " + juce::String (posB / barB, 3));

        playDeck (*lib, 48000.0, 150, {}, false);
        lib->setActiveDeck (1);
        playDeck (*lib, 48000.0, 2, {}, false);
        const double posA2 = lib->deckPosition (0), posB2 = lib->deckPosition (1);
        rep.check (std::abs (posB2 / barB - posA2 / barA) < 0.02,
                   "na troca, o B volta para o compasso do A ("_t + juce::String (posA2 / barA, 3) + " e " + juce::String (posB2 / barB, 3) + ")");
        lib->setActiveDeck (0);

        // loop de 1 compasso: depois da primeira volta, a saída se repete amostra a amostra
        lib->stopDeck();
        lib->unloadDeck (1);
        rep.check (app.run ("library.loop", args ({ { "bars", 1 } })).ok, "library.loop liga o loop de 1 compasso"_t);
        const auto loop = lib->getDecks().loopOf (0);
        const int barFrames = (int) (std::llround (loop.end * 48000.0) - std::llround (loop.start * 48000.0));
        rep.check (loop.active() && loop.start == 0.0 && std::abs (loop.end - barA) < 0.001, "o loop é o primeiro compasso do A"_t);
        settle (*lib);
        lib->toggleDeck();
        const auto looped = playDeck (*lib, 48000.0, (4 * barFrames) / 512 + 2, {}, false);
        float loopDiff = 0.0f;

        for (int c = 0; c < 2; ++c)
            for (int i = barFrames; i < 2 * barFrames && i + barFrames < looped.getNumSamples(); ++i)
                loopDiff = juce::jmax (loopDiff, std::abs (looped.getSample (c, i) - looped.getSample (c, i + barFrames)));

        rep.check (barFrames > 0 && loopDiff == 0.0f && lib->deckPosition (0) < loop.end,
                   "o loop repete o compasso amostra a amostra (diferença "_t + juce::String (loopDiff) + ")");
        lib->setLoopBars (0);
        lib->stopDeck();

        // referência externa no B: o LUFS é medido numa thread
        const auto reference = ctx.work.getChildFile ("projetos-a-export.wav");
        rep.check (lib->loadDeck (1, reference).wasOk(), "uma referência (WAV) entra no deck B"_t);
        const bool referenceMeasured = pumpUntil ([lib] { return lib->deckInfo (1).lufsKnown; }, 15000);
        rep.check (referenceMeasured && lib->deckInfo (1).lufs > -40.0f,
                   "o LUFS da referência foi medido ("_t + juce::String (lib->deckInfo (1).lufs, 2) + ")");
    }

    //--------------------------------------------------------------------------
    // Fatia 3: organizar. Estrela, cor, tags e status no biblioteca.json; a busca; as versões empilhadas; as pastas.
    {
        using S = library::ProjectLibrary::Status;
        const auto fileC = root.getChildFile ("Beat C").getChildFile ("Beat C.wnl");

        // decks vazios: uma pilha com uma versão num deck abre sozinha, e aqui se testa a pilha fechada
        lib->stopDeck();
        lib->unloadDeck (0);
        lib->unloadDeck (1);
        const auto fileA2 = root.getChildFile ("Beat A").getChildFile ("Beat A_2.wnl");

        app.run ("project.new");
        app.run ("transport.setBpm", args ({ { "bpm", 72.0 } }));
        rep.check (app.saveProject (fileC).wasOk(), "salvou o C (72 BPM, o meio-tempo de 144)"_t);
        app.openProject (fileA);
        rep.check (app.run ("project.saveNewVersion").ok && fileA2.existsAsFile(), "Salvar nova versão criou o Beat A_2"_t);
        lib->refresh();
        pumpUntil ([lib] { return ! lib->isScanning(); }, 15000);

        rep.check (app.run ("library.setMeta", args ({ { "path", fileA.getFullPathName() }, { "favorite", true }, { "colour", "#FF751F" },
                                                       { "tags", "sombrio, 808" }, { "status", "mix" } })).ok
                       && app.run ("library.setMeta", args ({ { "path", fileB.getFullPathName() }, { "status", "pronto" }, { "tags", "#Sombrio" } })).ok,
                   "library.setMeta marca o A (estrela, cor, tags, mix) e o B (pronto, #Sombrio)"_t);
        const auto mA = lib->metaOf (fileA);
        rep.check (mA.favorite && mA.colour == "#FF751F" && mA.tags.joinIntoString (",") == "sombrio,808" && mA.status == S::mix,
                   "os metadados do A ficaram ("_t + mA.tags.joinIntoString (",") + ")");
        rep.check (lib->metaOf (fileB).tags.joinIntoString (",") == "sombrio", "a tag perde o # e fica em minúsculas"_t);
        rep.check (! app.run ("library.setMeta", args ({ { "path", fileA.getFullPathName() }, { "status", "talvez" } })).ok,
                   "status desconhecido é recusado"_t);

        // os nomes das linhas (só as cabeças das pilhas) para uma busca
        auto names = [&app] (const juce::String& query, const juce::String& view = "all")
        {
            juce::StringArray out;
            const auto r = app.run ("library.search", args ({ { "query", query }, { "view", view } }));

            if (auto* rows = r.value["rows"].getArray())
                for (auto& row : *rows)
                    if (! (bool) row["child"])
                        out.add (row["name"].toString());

            return out;
        };

        const auto bpm72 = names ("bpm:72");
        rep.check (bpm72.contains ("Beat C") && ! bpm72.contains ("Beat B") && ! bpm72.contains ("Beat A_2"), "bpm:72 acha o C e mais ninguém daqui"_t);
        const auto bpm140 = names ("bpm:140-150");
        rep.check (bpm140.contains ("Beat B") && bpm140.contains ("Beat A_2") && bpm140.contains ("Beat C"),
                   "bpm:140-150 acha o B (150), o A (140) e o C (72, o meio-tempo de 144)"_t);
        const auto am = names ("tom:Am");
        rep.check (am.contains ("Beat B") && ! am.contains ("Beat C"), "tom:Am acha o B e não o C (Cm)"_t);
        const auto dark = names ("tag:sombrio");
        rep.check (dark.contains ("Beat A") && dark.contains ("Beat B") && ! dark.contains ("Beat C"), "tag:sombrio acha o A e o B"_t);
        rep.check (names ("status:mix") == juce::StringArray ("Beat A") && names ("fav") == juce::StringArray ("Beat A"),
                   "status:mix e fav acham só o A"_t);
        rep.check (names ("", "favorites") == juce::StringArray ("Beat A") && names ("", "status:pronto") == juce::StringArray ("Beat B"),
                   "as vistas Favoritos e Pronto da barra lateral"_t);
        rep.check (names ("sombrio beat").contains ("Beat B") && ! names ("sombrio beat").contains ("Beat C"),
                   "texto livre procura no nome e nas tags, e cada palavra precisa bater"_t);

        // versões empilhadas: A e A_2 numa linha só (a mais nova em cima); aberta, a mais velha aparece embaixo
        auto rowFor = [&app] (const juce::File& f) -> juce::var
        {
            const auto r = app.run ("library.search", args ({ { "query", "" } }));

            if (auto* rows = r.value["rows"].getArray())
                for (auto& row : *rows)
                    if (row["path"].toString() == f.getFullPathName())
                        return row;

            return {};
        };

        const auto head = rowFor (fileA2);
        rep.check ((int) head["versions"] == 2 && ! (bool) head["child"] && rowFor (fileA).isVoid(),
                   "Beat A e Beat A_2 numa linha só, com o A_2 em cima"_t);
        app.run ("library.toggleVersions", args ({ { "path", fileA.getFullPathName() } }));
        rep.check ((bool) rowFor (fileA)["child"], "abrir a pilha mostra o Beat A embaixo do A_2"_t);
        app.run ("library.toggleVersions", args ({ { "path", fileA.getFullPathName() } }));
        lib->loadDeck (1, fileA, 0.0);
        rep.check ((bool) rowFor (fileA)["child"], "a pilha com uma versão num deck abre sozinha (o que toca não fica escondido)"_t);
        lib->unloadDeck (1);
        rep.check (rowFor (fileA).isVoid(), "tirada do deck, a pilha volta a fechar"_t);

        // outra biblioteca na mesma pasta de dados lê o mesmo biblioteca.json
        {
            library::ProjectLibrary other ({ app.getEngine(), app.getSampleCache(), &app.getPluginCatalog(), ctx.data, {}, {}, {}, nullptr });
            const auto m = other.metaOf (fileA);
            rep.check (m.favorite && m.status == S::mix && m.tags.contains ("808") && m.colour == "#FF751F",
                       "a estrela, a cor, as tags e o status ficam no biblioteca.json"_t);
        }

        // uma pasta acrescentada entra na varredura (o D é uma cópia, então não está nos recentes)
        const auto extra = ctx.work.getChildFile ("outra pasta");
        const auto fileD = extra.getChildFile ("Beat D").getChildFile ("Beat D.wnl");
        extra.deleteRecursively();
        fileD.getParentDirectory().createDirectory();
        fileC.copyFileTo (fileD);
        lib->refresh();
        pumpUntil ([lib] { return ! lib->isScanning(); }, 15000);
        const bool outside = lib->find (fileD) == nullptr;
        rep.check (app.run ("library.addFolder", args ({ { "path", extra.getFullPathName() } })).ok, "library.addFolder acrescenta a pasta"_t);
        pumpUntil ([lib] { return ! lib->isScanning(); }, 15000);
        rep.check (outside && lib->find (fileD) != nullptr && names ("", "folder:" + extra.getFullPathName()) == juce::StringArray ("Beat D"),
                   "o D aparece com a pasta nova, e a vista da pasta mostra só ele"_t);
        app.run ("library.removeFolder", args ({ { "path", extra.getFullPathName() } }));
        pumpUntil ([lib] { return ! lib->isScanning(); }, 15000);
        rep.check (lib->find (fileD) == nullptr && lib->getExtraFolders().isEmpty(), "tirar a pasta tira o D da lista"_t);

        app.run ("library.search", args ({ { "query", "" } }));
    }

    //--------------------------------------------------------------------------
    // Fatia 4: combos. Listas com ordem e nota por item, num .wnlcombo na pasta de dados; tocar em sequência.
    {
        const auto fileA2 = root.getChildFile ("Beat A").getChildFile ("Beat A_2.wnl");
        const auto fileA6 = root.getChildFile ("Beat A6").getChildFile ("Beat A6.wnl");
        const auto reference = ctx.work.getChildFile ("projetos-a-export.wav");
        const juce::String name ("Teste do autoteste");
        lib->stopDeck();
        lib->unloadDeck (0);
        lib->unloadDeck (1);
        lib->setLoopBars (0);

        rep.check (app.run ("library.comboCreate", args ({ { "combo", name } })).ok
                       && lib->comboFolder().getChildFile (name + ".wnlcombo").existsAsFile(),
                   "o combo vira um .wnlcombo na pasta de dados"_t);
        rep.check (! app.run ("library.comboCreate", args ({ { "combo", name.toLowerCase() } })).ok, "nome repetido é recusado"_t);

        app.run ("library.comboAdd", args ({ { "combo", name }, { "path", fileA.getFullPathName() }, { "latest", true } }));
        app.run ("library.comboAdd", args ({ { "combo", name }, { "path", fileA6.getFullPathName() }, { "note", "a versão -6 dB"_t } }));
        app.run ("library.comboAdd", args ({ { "combo", name }, { "path", reference.getFullPathName() } }));
        const auto* combo = lib->findCombo (name);
        rep.check (combo != nullptr && combo->items.size() == 3 && combo->items[1].note == "a versão -6 dB"_t,
                   "três itens no combo, com a nota no segundo"_t);
        rep.check (combo != nullptr && lib->resolve (combo->items[0]) == fileA2, "\"a mais nova\": o item do Beat A toca o Beat A_2"_t);

        rep.check (app.run ("library.comboMove", args ({ { "combo", name }, { "from", 3 }, { "to", 1 } })).ok
                       && lib->findCombo (name)->items[0].file == reference,
                   "library.comboMove põe a referência em primeiro"_t);

        // a vista do combo: na ordem dele, com a referência, a versão mais nova e a nota
        {
            const auto r = app.run ("library.search", args ({ { "view", "combo:" + name } }));
            const auto* rows = r.value["rows"].getArray();
            const bool ok = rows != nullptr && rows->size() == 3
                            && (bool) (*rows)[0]["reference"] && (*rows)[1]["path"].toString() == fileA2.getFullPathName()
                            && (*rows)[2]["note"].toString() == "a versão -6 dB"_t;
            rep.check (ok, "a vista do combo mostra a referência, o A_2 (a mais nova) e o A6 com a nota, nessa ordem"_t);
        }

        {
            library::ProjectLibrary other ({ app.getEngine(), app.getSampleCache(), &app.getPluginCatalog(), ctx.data, {}, {}, {}, nullptr });
            const auto* c = other.findCombo (name);
            rep.check (c != nullptr && c->items.size() == 3 && c->items[0].file == reference && c->items[1].latest,
                       "outra biblioteca lê o mesmo combo do .wnlcombo"_t);
        }

        // tocar em sequência: a referência, o A_2 (sem prévia: pulado) e o A6
        rep.check (app.run ("library.comboPlay", args ({ { "combo", name } })).ok && lib->playingCombo() == name,
                   "library.comboPlay começa o combo"_t);
        auto& decks = lib->getDecks();
        decks.prepareForTest (48000.0);
        juce::AudioBuffer<float> io (2, 512);
        juce::AudioIODeviceCallbackContext context;
        juce::StringArray played;
        played.add (lib->deckInfo (lib->getActiveDeck()).audio.getFileName());

        for (int block = 0; block < 4000 && lib->playingCombo().isNotEmpty(); ++block)
        {
            decks.audioDeviceIOCallbackWithContext (nullptr, 0, io.getArrayOfWritePointers(), 2, 512, context);

            if ((block & 3) == 3)
            {
                decks.service();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            }

            const auto now = lib->deckInfo (lib->getActiveDeck()).audio.getFileName();

            if (now.isNotEmpty() && now != played[played.size() - 1])
                played.add (now);
        }

        rep.check (played == juce::StringArray ("projetos-a-export.wav", "Beat A6.flac"),
                   "tocou a referência e depois o A6, pulando o A_2 sem prévia ("_t + played.joinIntoString (", ") + ")");
        rep.check (lib->playingCombo().isEmpty(), "no fim do último item, o combo para"_t);

        const auto renamed = juce::String ("Teste do autoteste 2");
        rep.check (app.run ("library.comboRename", args ({ { "combo", name }, { "name", renamed } })).ok
                       && lib->findCombo (renamed) != nullptr && ! lib->comboFolder().getChildFile (name + ".wnlcombo").exists()
                       && lib->comboFolder().getChildFile (renamed + ".wnlcombo").existsAsFile(),
                   "renomear troca o nome e o arquivo"_t);
        rep.check (app.run ("library.comboDelete", args ({ { "combo", renamed } })).ok && lib->findCombo (renamed) == nullptr
                       && ! lib->comboFolder().getChildFile (renamed + ".wnlcombo").exists(),
                   "apagar tira o combo (o arquivo vai para a lixeira)"_t);

        // um combo para a captura: B, A6 com nota e a referência
        lib->createCombo ("Show de sexta");
        lib->addToCombo ("Show de sexta", fileB);
        lib->addToCombo ("Show de sexta", fileA6, false, "fecha o show"_t);
        lib->addToCombo ("Show de sexta", reference);
        lib->stopDeck();
    }

    //--------------------------------------------------------------------------
    // Fatia 5: importar de outro projeto. O E (Lá menor) tem o Kick em passos e o 808 em notas, com a saturação
    // na trilha do 808; ele vai para um projeto novo (Dó menor), com as notas no tom daqui.
    {
        const auto fileE = root.getChildFile ("Beat E").getChildFile ("Beat E.wnl");
        app.run ("project.new");
        app.run ("project.setKey", args ({ { "root", "A" }, { "scale", "minor" } }));
        const auto kickE = channelId (model.get(), "Kick"), bassE = channelId (model.get(), "808");
        setGrid (app, kickE, "x...x...x...x...");
        addNotes (app, bassE, { noteVar (0.0, 1.0, 45, false), noteVar (2.0, 1.0, 48, true) });
        app.run ("mixer.addInsert", args ({ { "track", model.get().findChannel (bassE)->output }, { "type", "sat" } }));
        rep.check (app.saveProject (fileE).wasOk(), "salvou o E (Lá menor, Kick em passos, 808 em notas, saturação no 808)"_t);

        library::ImportSource source;
        library::readImportSource (fileE, source);
        rep.check (source.patterns.size() == 1 && source.patterns[0].channels.joinIntoString (",") == "Kick,808"
                       && source.patterns[0].steps == 4 && source.patterns[0].notes == 2,
                   "o E lido só como dado: um padrão, com o Kick (4 passos) e o 808 (2 notas)"_t);

        app.run ("project.new");   // Dó menor, o kit de sempre
        const auto before = model.get();
        const auto r = app.run ("project.importFrom", args ({ { "path", fileE.getFullPathName() }, { "transpose", true } }));
        const bool counts = r.ok && r.value["patterns"].size() == 1 && r.value["channels"].size() == 2 && (int) r.value["semitones"] == 3;
        rep.check (counts, "project.importFrom trouxe 1 padrão e 2 canais, com as notas +3 semitons (Lá → Dó)"_t);

        if (counts)
        {
            const auto& p = model.get();
            const auto* pattern = p.findPattern (r.value["patterns"][0].toString());
            const auto* kick = p.findChannel (r.value["channels"][0].toString());
            const auto* bass = p.findChannel (r.value["channels"][1].toString());
            const bool found = pattern != nullptr && kick != nullptr && bass != nullptr;
            rep.check (found && pattern->name == "Padrão 1 (de Beat E)"_t && kick->name == "Kick (de Beat E)" && bass->name == "808 (de Beat E)",
                       "nomes repetidos ganham \"(de Beat E)\""_t);

            if (found)
            {
                rep.check (kick->id != kickE && bass->id != bassE && pattern->id != source.patterns[0].id, "tudo com ids novos"_t);

                const auto* notes = pattern->notesFor (bass->id);
                rep.check (notes != nullptr && notes->size() == 2 && (*notes)[0].key == 48 && (*notes)[1].key == 51 && (*notes)[1].slide,
                           "as notas do 808 subiram 3 semitons (45 → 48, 48 → 51) e o slide ficou"_t);

                int on = 0;

                if (auto it = pattern->steps.find (kick->id); it != pattern->steps.end())
                    for (auto& s : it->second)
                        on += s.on ? 1 : 0;

                rep.check (on == 4, "os 4 passos do Kick vieram"_t);

                const auto* bassTrack = p.findMixer (bass->output);
                rep.check (bassTrack != nullptr && bassTrack->autoFor == bass->id && bassTrack->inserts.size() == 1
                               && bassTrack->inserts[0].type == "sat",
                           "o 808 sai por uma trilha nova, com a saturação"_t);
                rep.check (juce::File::isAbsolutePath (kick->samplePath) && juce::File (kick->samplePath).existsAsFile()
                               && app.getProjector().sampleProblems().size() == 0,
                           "os samples apontam para o arquivo do E e carregam (o próximo salvar copia para cá)"_t);
                rep.check (p.findPattern (before.patterns[0].id) != nullptr && p.findPattern (before.patterns[0].id)->steps.count (kick->id) == 1,
                           "o padrão que já existia ganhou a raia vazia dos canais novos"_t);
            }
        }

        rep.check (app.run ("edit.undo").ok && model.get().channels.size() == before.channels.size()
                       && model.get().patterns.size() == before.patterns.size() && model.get().mixer.size() == before.mixer.size(),
                   "Ctrl+Z desfaz a importação inteira num passo só"_t);
        rep.check (app.run ("edit.redo").ok && model.get().patterns.size() == before.patterns.size() + 1, "e o refazer traz de volta"_t);
        app.run ("edit.undo");

        const auto r2 = app.run ("project.importFrom", args ({ { "path", fileE.getFullPathName() }, { "patterns", "Padrão 1"_t }, { "mixer", false } }));
        bool plain = r2.ok && (int) r2.value["semitones"] == 0;

        if (plain)
        {
            const auto& p = model.get();
            const auto* pattern = p.findPattern (r2.value["patterns"][0].toString());
            const auto* bass = p.findChannel (r2.value["channels"][1].toString());
            const auto* notes = pattern != nullptr && bass != nullptr ? pattern->notesFor (bass->id) : nullptr;
            const auto* track = bass != nullptr ? p.findMixer (bass->output) : nullptr;
            plain = notes != nullptr && (*notes)[0].key == 45 && track != nullptr && track->inserts.empty();
        }

        rep.check (plain, "sem transpor e sem os efeitos: as notas ficam no tom do E e a trilha vem só com o volume e o pan"_t);

        const auto channelsBefore = model.get().channels.size();
        rep.check (! app.run ("project.importFrom", args ({ { "path", fileE.getFullPathName() }, { "patterns", "Não existe"_t } })).ok
                       && model.get().channels.size() == channelsBefore,
                   "padrão que não existe é recusado, e nada muda"_t);

        // a janela de importar, desenhada fora da área de trabalho
        ImportDialog dialog (source, {});
        const auto image = snapshotOffDesktop (dialog, dialog.getWidth(), dialog.getHeight());
        const auto file = ctx.work.getChildFile ("projetos-importar.png");
        rep.check (savePng (image, file), "captura da janela de importar em "_t + file.getFullPathName());

        app.run ("project.new");
    }

    //--------------------------------------------------------------------------
    // Tela: a lista com os projetos e os dois decks (A tocando, B carregado, loop ligado), fora da área de trabalho
    {
        lib->setActiveDeck (0);
        lib->playPreview (fileA, 1.0);
        lib->loadDeck (1, fileB);
        lib->setLoopBars (2);
        playDeck (*lib, 48000.0, 40);

        ProjectsView view (lib);
        view.selectProject (fileB);
        const auto image = snapshotOffDesktop (view, 1280, 700);
        const auto file = ctx.work.getChildFile ("projetos-tela.png");
        rep.check (savePng (image, file), "captura da tela Projetos em "_t + file.getFullPathName());

        const int orange = countPixels (image, isSignal);
        rep.check (orange > 20, "o que toca acende em laranja (o ▶ da linha, o nome e a agulha do deck: "_t + juce::String (orange) + " px)");

        const auto panel = image.getPixelAt (6, image.getHeight() - 6);
        rep.check (panel.getRed() < 0x30 && panel.getGreen() < 0x30 && panel.getBlue() < 0x30, "o cartão é escuro (panel)"_t);

        // a vista do combo, com a barra lateral mostrando o combo escolhido
        {
            library::ProjectLibrary::Filter f;
            f.view = library::ProjectLibrary::Filter::View::combo;
            f.combo = "Show de sexta";
            lib->setFilter (f);
            ProjectsView comboView (lib);
            const auto comboImage = snapshotOffDesktop (comboView, 1280, 700);
            const auto comboFile = ctx.work.getChildFile ("projetos-combo.png");
            rep.check (savePng (comboImage, comboFile) && lib->rows().size() == 3, "captura da vista do combo em "_t + comboFile.getFullPathName());
            lib->setFilter ({});
        }

        lib->setLoopBars (0);
        lib->stopDeck();
        lib->unloadDeck (1);

        ViewRail rail;
        rail.setState (View::projects, true, false, false, false);
        const auto railImage = snapshotOffDesktop (rail, theme::metric::rail, 600);
        savePng (railImage, ctx.work.getChildFile ("projetos-trilho.png"));
        rep.check (countPixels (railImage, [] (juce::Colour c) { return c.getRed() > 200 && c.getGreen() > 200 && c.getBlue() > 200; }) > 30,
                   "o trilho tem o botão Projetos aceso"_t);
    }

    //--------------------------------------------------------------------------
    // Salvar e fechar logo em seguida (revisão de 22/10): a prévia que o Ctrl+S começou não pode prender o app.
    // Um segundo app sem janela salva uma música longa (uns 6 min, quase toda em silêncio), a prévia começa,
    // e o app fecha com ela no meio. Antes do RenderThread, a thread de mensagem esperava no join e o render
    // esperava por ela: o processo ficava preso.
    {
        const auto data2 = ctx.work.getChildFile ("dados_projetos_fechar");
        const auto song = root.getChildFile ("Fechar").getChildFile ("Fechar.wnl");
        bool rendering = false;
        float progress = 0.0f;
        double closeSeconds = -1.0;

        {
            auto second = std::make_unique<AppController> (true, data2);
            auto* lib2 = second->library();
            lib2->renderOnSave = true;                 // como o app com janela: cada salvar põe a prévia na fila
            lib2->getRenderer().minFreeMemoryMB = 0;
            second->run ("project.new");
            setGrid (*second, channelId (second->getModel().get(), "Kick"), "x...x...x...x...");
            second->run ("playlist.addItem", args ({ { "track", 1 }, { "start", 0 }, { "unit", "bars" } }));
            second->run ("playlist.addItem", args ({ { "track", 1 }, { "start", 200 }, { "unit", "bars" } }));
            second->saveProject (song);

            for (const auto t0 = juce::Time::getMillisecondCounterHiRes(); juce::Time::getMillisecondCounterHiRes() - t0 < 30000.0;)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
                rendering = lib2->getRenderer().stateOf (song) == PreviewState::rendering;
                lib2->getRenderer().renderingNow (&progress);

                if (rendering && progress >= 0.05f)
                    break;
            }

            Watchdog guard (rep, "fechar o app logo depois de salvar, com a prévia no meio"_t, 60.0);
            const auto c0 = juce::Time::getMillisecondCounterHiRes();
            second = nullptr;
            closeSeconds = (juce::Time::getMillisecondCounterHiRes() - c0) / 1000.0;
        }

        // o segundo app trocou as pastas globais do piano e dos favoritos do synth: voltam para as do teste
        piano::setDataFolder (ctx.data);
        synthfavorites::setFolder (ctx.data);
        usermix::setDataFolder (ctx.data);   // e a das cadeias e presets do artista (Efeitos; integração de teste)

        rep.check (rendering && progress >= 0.05f && progress < 1.0f && closeSeconds >= 0.0 && closeSeconds < 10.0,
                   "salvar e fechar logo: fecha em "_t + juce::String (closeSeconds, 2) + " s com a prévia em "
                       + juce::String (progress * 100.0f, 1) + "% (o RenderThread interrompe as esperas do render)");
        rep.check (! previewFolder (song).getChildFile ("Fechar.render.wav").exists()
                       && ! previewFolder (song).getChildFile ("Fechar.flac.part").exists() && ! previewAudio (song).exists(),
                   "e nada pela metade fica na pasta preview"_t);
        data2.deleteRecursively();
    }

    lib->setRoots ({});
    app.run ("project.new");
}

} // namespace whynot::selftest
