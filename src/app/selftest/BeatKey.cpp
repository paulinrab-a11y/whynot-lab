// Autoteste do tom do beat (pedido do usuário de 21/10, noite). Um beat em tom conhecido é feito no app
// (WHYNOT Synth com os acordes, o 808 do kit nas fundamentais, bumbo e chimbal), vira WAV e MP3, e volta
// como beat de um projeto novo: a detecção roda sozinha em segundo plano, o audio.detectKey acerta o tom e
// os acordes por compasso, o project.setKey usa o tom, e o piano roll mostra onde o 808 casa. Dono: Tom do beat.

#include "SelfTestKit.h"
#include "../../ui/BeatKeyNotice.h"
#include "../../ui/PianoRoll.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    template <typename T>
    T* childOf (juce::Component& parent)
    {
        for (auto* c : parent.getChildren())
            if (auto* t = dynamic_cast<T*> (c))
                return t;

        return nullptr;
    }

    bool savePng (juce::Component& c, const juce::File& file)
    {
        const auto image = juce::SoftwareImageType().convert (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f));
        file.deleteFile();

        if (juce::FileOutputStream out (file); out.openedOk())
        {
            juce::PNGImageFormat png;
            return png.writeImageToStream (image, out) && image.getWidth() > 0;
        }

        return false;
    }

    struct ChordSpec { int root; bool minor; };

    /** Um beat de 4 compassos no tom pedido: acordes no WHYNOT Synth, o 808 nas fundamentais, bumbo e chimbal. */
    bool makeBeat (AppController& app, ProjectModel& model, const std::vector<ChordSpec>& chords)
    {
        app.run ("project.new");
        app.run ("playlist.clear");
        app.run ("pattern.setLength", args ({ { "steps", 64 } }));

        const auto synth = app.run ("channel.addSynth", args ({ { "name", "Acordes" } }));
        const auto pad = synth.value["channel"].toString();
        const auto bass = channelId (model.get(), "808");
        juce::Array<juce::var> padNotes, bassNotes;

        for (int bar = 0; bar < 4; ++bar)
        {
            const auto& c = chords[(size_t) bar % chords.size()];
            const int root = 57 + ((c.root - 57) % 12 + 12) % 12;   // Lá 3 a Sol# 4

            for (int iv : { 0, c.minor ? 3 : 4, 7 })
                padNotes.add (noteVar (bar * 4.0, 3.9, root + iv, false));

            bassNotes.add (noteVar (bar * 4.0, 3.5, 33 + ((c.root - 33) % 12 + 12) % 12, false));   // Lá 1 a Sol# 2
        }

        const bool ok = synth.ok && bass.isNotEmpty() && addNotes (app, pad, padNotes).ok && addNotes (app, bass, bassNotes).ok;
        setGrid (app, channelId (model.get(), "Kick"), "x...x...x...x...x...x...x...x...x...x...x...x...x...x...x...x...");
        setGrid (app, channelId (model.get(), "Hat"), "..x...x...x...x...x...x...x...x...x...x...x...x...x...x...x...x.");
        return ok;
    }

    juce::String chordsText (const juce::var& bars)
    {
        juce::StringArray names;

        for (auto& b : *bars.getArray())
            names.add ((bool) b["found"] ? b["chord"].toString() : juce::String ("-"));

        return names.joinIntoString (" ");
    }

    /** Quantos compassos da resposta batem com a progressão (o compasso 1 é o primeiro acorde). */
    int barsRight (const juce::var& bars, const std::vector<ChordSpec>& chords, int count)
    {
        int right = 0;

        if (auto* list = bars.getArray())
            for (int i = 0; i < juce::jmin (count, list->size()); ++i)
            {
                const auto& b = list->getReference (i);
                const auto& want = chords[(size_t) i % chords.size()];
                right += (bool) b["found"] && (int) b["rootIndex"] == want.root && (b["quality"].toString() == "minor") == want.minor ? 1 : 0;
            }

        return right;
    }
}

void runBeatKeySelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    auto* analyzer = app.beatKey();

    // i VI III VII em Lá menor (Am F C G) e I V vi IV em Ré maior (D A Bm G)
    const std::vector<ChordSpec> aMinor { { 9, true }, { 5, false }, { 0, false }, { 7, false } };
    const std::vector<ChordSpec> dMajor { { 2, false }, { 9, false }, { 11, true }, { 7, false } };
    const auto wav = ctx.work.getChildFile ("tom-beat-la-menor.wav");
    const auto mp3 = ctx.work.getChildFile ("tom-beat-la-menor.mp3");
    const auto wavD = ctx.work.getChildFile ("tom-beat-re-maior.wav");

    rep.section ("Tom do beat: um beat feito no app, em tom conhecido"_t);
    {
        rep.check (analyzer != nullptr, "o app tem o analisador de tom (thread de fundo)"_t);
        app.run ("project.new");
        app.run ("playlist.clear");
        const auto noBeat = app.run ("audio.detectKey");
        rep.check (! noBeat.ok && noBeat.error.contains ("Nenhum beat"), "sem beat no projeto, o audio.detectKey explica que falta o beat"_t);

        rep.check (makeBeat (app, model, aMinor), "beat em Lá menor: Am F C G no WHYNOT Synth, 808 nas fundamentais, bumbo e chimbal"_t);
        const auto exported = app.run ("project.exportWav", args ({ { "path", wav.getFullPathName() }, { "what", "pattern" }, { "loops", 2 } }));
        rep.check (exported.ok && wav.existsAsFile(), "exportado em WAV: 8 compassos a 140 BPM"_t);

        const auto mp3Export = app.run ("project.exportMp3", args ({ { "path", mp3.getFullPathName() }, { "what", "pattern" }, { "loops", 2 } }));
        rep.check (mp3Export.ok && mp3.existsAsFile(), "e em MP3 (o beat da internet costuma vir assim)"_t);

        rep.check (makeBeat (app, model, dMajor), "outro beat, em Ré maior: D A Bm G"_t);
        app.run ("project.exportWav", args ({ { "path", wavD.getFullPathName() }, { "what", "pattern" }, { "loops", 2 } }));
        rep.check (wavD.existsAsFile(), "exportado em WAV"_t);
    }

    rep.section ("Tom do beat: o beat entra no projeto e a detecção roda sozinha"_t);
    {
        app.run ("project.new");
        app.run ("playlist.clear");
        app.run ("project.setKey", args ({ { "root", "C" }, { "scale", "minor" } }));

        const auto added = app.run ("playlist.addItem", args ({ { "path", wav.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
        const auto beats = analyzer != nullptr ? analyzer->beatsOf (model) : std::vector<BeatSource> {};
        const bool queued = analyzer != nullptr && (analyzer->isPending (wav) || analyzer->find (wav) != nullptr);
        rep.check (added.ok && beats.size() == 1 && beats.front().file == wav && beats.front().inPlaylist,
                   "o WAV na playlist vira o beat do projeto (canal de áudio com bloco)"_t);
        rep.check (queued, "a análise foi pedida sozinha, sem comando, e roda em segundo plano"_t);

        const auto now = app.run ("audio.detectKey", args ({ { "wait", false } }));
        rep.check (now.ok && (now.value["state"] == "analyzing" || now.value["state"] == "ready"),
                   "audio.detectKey sem esperar volta na hora: "_t + now.value["state"].toString());

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto r = app.run ("audio.detectKey", args ({ { "channel", beats.empty() ? juce::String() : beats.front().channel } }));
        const auto waited = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto v = r.value;
        rep.check (r.ok && v["state"] == "ready" && v["root"] == "A" && v["scale"] == "minor",
                   "o tom do beat: "_t + v["name"].toString() + " (" + juce::String (juce::roundToInt ((double) v["confidence"] * 100.0)) + "%)"
                   + (r.ok ? juce::String() : " " + r.error));
        rep.check ((double) v["confidence"] >= 0.5 && (double) v["notesConfidence"] >= (double) v["confidence"],
                   "confiança "_t + v["confidence"].toString() + ", das notas (com o relativo) " + v["notesConfidence"].toString());
        rep.check (std::abs ((double) v["tuningCents"]) < 5.0, "afinação perto de 440: "_t + v["tuningCents"].toString() + " cents");
        rep.check (v["alternatives"].size() == 3 && v["relative"]["name"].toString() == "Dó maior"_t
                       && v["relative"]["explain"].toString().contains ("mesmas 7 notas"),
                   "3 alternativas e o relativo explicado: "_t + v["relative"]["explain"].toString());

        const int right = barsRight (v["bars"], aMinor, 8);
        rep.check (v["bars"].size() >= 8 && right >= 7, "acordes por compasso: "_t + chordsText (v["bars"]) + " (" + juce::String (right) + " de 8 certos)");
        rep.check ((int) v["bars"][0]["start"] == 0 && (int) v["bars"][1]["start"] == 4 * ticksPerBeat, "cada compasso com o início em ticks"_t);
        rep.check (waited < 20000.0, "a espera do comando: "_t + juce::String (juce::roundToInt (waited)) + " ms");

        if (const auto result = analyzer != nullptr ? analyzer->find (wav) : nullptr)
            rep.check (result->millis < 5000.0, "a análise de "_t + juce::String (result->analysis.seconds, 1) + " s de áudio levou "_t
                                                + juce::String (juce::roundToInt (result->millis)) + " ms, fora da tela");

        // o aviso da barra de status e o clique em "Usar no projeto" (que é o project.setKey)
        BeatKeyNotice notice;
        notice.setSize (420, theme::metric::status);
        const auto text = analyzer != nullptr && analyzer->find (wav) != nullptr ? beatKeyText (analyzer->find (wav)->analysis) : juce::String();
        notice.show (text, {});
        rep.check (text.startsWith ("Tom do beat: Lá menor ("_t) && notice.getIdealWidth() <= 420,
                   "o aviso: \""_t + text + "\" em " + juce::String (notice.getIdealWidth()) + " px");

        const auto setKey = app.run ("project.setKey", v["setKey"]);
        rep.check (setKey.ok && model.get().keyRoot == 9 && model.get().scale == "minor", "usar no projeto: o projeto fica em Lá menor"_t);

        // o mesmo beat em MP3, por caminho; e o outro beat, em Ré maior
        const auto fromMp3 = app.run ("audio.detectKey", args ({ { "path", mp3.getFullPathName() } }));
        rep.check (fromMp3.ok && fromMp3.value["root"] == "A" && fromMp3.value["scale"] == "minor" && barsRight (fromMp3.value["bars"], aMinor, 8) >= 7,
                   "pelo MP3: "_t + fromMp3.value["name"].toString() + ", " + chordsText (fromMp3.value["bars"]) + (fromMp3.ok ? juce::String() : " " + fromMp3.error));

        const auto fromD = app.run ("audio.detectKey", args ({ { "path", wavD.getFullPathName() } }));
        rep.check (fromD.ok && fromD.value["root"] == "D" && fromD.value["scale"] == "major" && barsRight (fromD.value["bars"], dMajor, 8) >= 7,
                   "o beat em Ré maior: "_t + fromD.value["name"].toString() + " (" + fromD.value["confidence"].toString() + "), "
                   + chordsText (fromD.value["bars"]));

        // o bloco começando no compasso 3: os acordes contam da música, não do arquivo
        const auto withStart = app.run ("audio.detectKey", args ({ { "path", wav.getFullPathName() }, { "start", 8 * ticksPerBeat } }));
        rep.check (withStart.ok && (int) withStart.value["bars"][0]["bar"] == 3 && withStart.value["bars"][0]["chord"] == "Am",
                   "com start no compasso 3, o primeiro acorde é o do compasso 3: "_t + withStart.value["bars"][0]["chord"].toString());

        const auto kick = channelId (model.get(), "Kick");
        rep.check (! app.run ("audio.detectKey", args ({ { "channel", kick } })).ok, "um kick curto não é beat: o comando recusa e explica"_t);
    }

    rep.section ("Tom do beat: dicas do 808 no piano roll"_t);
    {
        juce::Component host;
        theme::LookAndFeel laf;
        host.setLookAndFeel (&laf);
        host.setSize (1400, 640);

        const auto bass = channelId (model.get(), "808");
        app.run ("pattern.setLength", args ({ { "steps", 64 } }));
        addNotes (app, bass, { noteVar (0.0, 3.5, 33, false), noteVar (4.0, 3.5, 29, false) });

        {
            PianoRoll::beatHintsOn = true;
            PianoRoll roll (app);
            host.addAndMakeVisible (roll);
            roll.setBounds (host.getLocalBounds());
            roll.setChannel (bass);
            roll.refresh (model.get());
            auto* grid = childOf<PianoRollGrid> (roll);
            const auto hints = grid != nullptr ? grid->getBeatHints() : std::vector<BeatHint> {};
            juce::StringArray roots;

            for (auto& h : hints)
                roots.add (Project::keyName (h.root + 12).dropLastCharacters (1) + (h.minor ? "m" : ""));

            const bool rightRoots = hints.size() == 4 && hints[0].root == 9 && hints[0].minor && hints[1].root == 5
                                    && hints[2].root == 0 && hints[3].root == 7 && hints[3].start == 3 * 4 * ticksPerBeat;
            rep.check (grid != nullptr && rightRoots && grid->areBeatHintsVisible(),
                       "o padrão de 4 compassos mostra onde o 808 casa: "_t + roots.joinIntoString (" "));

            juce::Component* hintsButton = nullptr;

            for (auto* c : roll.getChildren())
                if (auto* b = dynamic_cast<IconButton*> (c); b != nullptr && b->getName() == "Dicas do beat")
                    hintsButton = b;

            rep.check (hintsButton != nullptr && hintsButton->isVisible() && hintsButton->getWidth() > 0,
                       "o botão Dicas do beat aparece (só com um beat analisado no projeto)"_t);
            rep.check (savePng (roll, ctx.work.getChildFile ("tom-pianoroll.png")), "captura do piano roll com as dicas"_t);

            roll.setBeatHintsOn (false);
            rep.check (grid != nullptr && ! grid->areBeatHintsVisible(), "o botão desliga as dicas"_t);
            roll.setBeatHintsOn (true);

            // o padrão no compasso 3 da playlist: as dicas seguem a música (C G Am F)
            app.run ("playlist.addItem", args ({ { "kind", "pattern" }, { "track", 2 }, { "start", 2 }, { "unit", "bars" } }));
            roll.refresh (model.get());
            const auto moved = grid != nullptr ? grid->getBeatHints() : std::vector<BeatHint> {};
            rep.check (moved.size() == 4 && moved[0].root == 0 && moved[1].root == 7 && moved[2].root == 9 && moved[3].root == 5
                           && moved[0].songBar == 2,
                       "com o padrão no compasso 3 da playlist, as dicas seguem a música: Dó, Sol, Lá, Fá"_t);
            host.removeChildComponent (&roll);
        }

        {
            // sem beat no projeto, o botão some (princípio 6: à vista só o que se usa)
            app.run ("project.new");
            PianoRoll roll (app);
            host.addAndMakeVisible (roll);
            roll.setBounds (host.getLocalBounds());
            roll.setChannel (channelId (model.get(), "808"));
            roll.refresh (model.get());
            bool buttonVisible = false;

            for (auto* c : roll.getChildren())
                if (auto* b = dynamic_cast<IconButton*> (c); b != nullptr && b->getName() == "Dicas do beat")
                    buttonVisible = b->isVisible();

            rep.check (! buttonVisible, "sem beat, o botão Dicas do beat não aparece"_t);
            host.removeChildComponent (&roll);
        }

        host.setLookAndFeel (nullptr);
    }

    // um projeto com o beat para a captura de verdade (--screenshot <png> <projeto> pianoRoll)
    {
        app.run ("project.new");
        app.run ("playlist.clear");
        app.run ("playlist.addItem", args ({ { "path", wav.getFullPathName() }, { "track", 1 }, { "start", 0 } }));
        app.run ("pattern.setLength", args ({ { "steps", 64 } }));
        app.run ("playlist.addItem", args ({ { "kind", "pattern" }, { "track", 2 }, { "start", 0 } }));
        addNotes (app, channelId (model.get(), "808"), { noteVar (0.0, 3.5, 33, false), noteVar (4.0, 3.5, 29, false),
                                                         noteVar (8.0, 3.5, 36, false), noteVar (12.0, 3.5, 31, false) });
        const auto saved = app.run ("project.save", args ({ { "path", ctx.work.getChildFile ("tom-beat.wnl").getFullPathName() } }));
        rep.check (saved.ok, "projeto com o beat salvo para a captura: tom-beat.wnl"_t);
    }
}

} // namespace whynot::selftest
