// Autoteste do bloco por canal (16/11): o bloco de padrão da playlist que toca um canal só (PlaylistItem::channel),
// pedido do usuário ("colocar cada channel rack separadamente na playlist... só o kick ou só uma melodia").
// - o motor: o bloco com channel = Kick soa igual ao bloco de um padrão clonado que só tem o Kick (o WAV, amostra
//   a amostra, com a diferença máxima medida), e continua uma referência (mudar o kick do padrão muda o bloco;
//   mudar o hat não);
// - playlist.splitByChannel: um bloco por canal com conteúdo, em raias distintas, sem pisar no que já está lá,
//   criando raias se faltar; a soma dá o render do bloco inteiro; um passo de desfazer;
// - as recusas dos comandos (canal que não existe, canal de áudio, bloco de áudio, bloco travado, padrão vazio);
// - o .wnl: o campo só quando preenchido, o recurso "playlist.itemChannel" em app.features, os dois escritores
//   iguais, salvar e reabrir; sem o campo, o arquivo sai sem "channel" nos blocos e sem "features";
// - remover o canal tira os blocos que só tocavam ele (o normalise), e o desfazer traz o canal e os blocos;
// - o export MIDI e os stems respeitam o filtro;
// - as telas, sem janela: o nome "Padrão · Canal", o menu para vários blocos num passo, a ação do KeyMap, duplicar,
//   copiar e colar levando o canal, e o "Pôr na playlist (só este canal)" do rack.
// Capturas: bloco-canal/bloco-canal-playlist.png. Dono: bloco-canal.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"
#include "../../ui/ChannelRack.h"
#include "../../ui/Playlist.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    constexpr double toneRate = 48000.0;
    constexpr float sameSound = 1.0e-4f;   // -80 dB: dois renders do mesmo som (medido: 0 quando o motor é o mesmo)

    juce::String num (double v, int places = 6)   { return juce::String (v, places).replaceCharacter ('.', ','); }

    juce::File writeTone (const juce::File& folder)
    {
        // meio segundo de 220 Hz: um bloco de áudio, para as recusas
        juce::AudioBuffer<float> b (1, (int) (toneRate / 2));

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / toneRate));

        const auto f = folder.getChildFile ("bloco-canal-tom.wav");
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
        juce::WavAudioFormat wav;

        if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (toneRate).withNumChannels (1).withBitsPerSample (24)))
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());

        return f;
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

    /** Uma tela sem janela, como nas outras seções: dentro de um pai que não vai para a área de trabalho. */
    template <typename View>
    struct Offscreen
    {
        Offscreen (AppController& app, int width, int height) : view (app)
        {
            host.setLookAndFeel (&laf);
            host.setBounds (0, 0, width, height);
            host.addAndMakeVisible (view);
            view.setBounds (host.getLocalBounds());
        }

        ~Offscreen()
        {
            host.removeChildComponent (&view);
            host.setLookAndFeel (nullptr);
        }

        theme::LookAndFeel laf;
        juce::Component host;
        View view;
    };

    /** Quantas notas (note on) a faixa com este nome tem no .mid; 0 se a faixa não existe. */
    int noteOnsOf (const juce::MidiFile& midi, const juce::String& trackName)
    {
        for (int t = 0; t < midi.getNumTracks(); ++t)
        {
            const auto* track = midi.getTrack (t);
            bool named = false;
            int notes = 0;

            for (auto* e : *track)
            {
                named = named || (e->message.isTrackNameEvent() && e->message.getTextFromTextMetaEvent() == trackName);
                notes += e->message.isNoteOn() ? 1 : 0;
            }

            if (named)
                return notes;
        }

        return 0;
    }
}

void runItemChannelSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto work = ctx.work.getChildFile ("bloco-canal");
    work.createDirectory();

    rep.section ("Bloco por canal: o padrão na playlist tocando um canal só (16/11)"_t);

    static const PlaylistItem missing;
    auto find = [&] (const juce::String& id) { const auto* f = model.get().findItem (id); return f != nullptr ? f : &missing; };
    auto exists = [&] (const juce::String& id) { return model.get().findItem (id) != nullptr; };
    auto render = [&] (const juce::String& name)
    {
        const auto f = work.getChildFile (name);
        f.deleteFile();
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
        return readWav (f);
    };
    auto laneOf = [&] (const juce::String& itemId)
    {
        const auto& tracks = model.get().playlistTracks;

        for (int t = 0; t < (int) tracks.size(); ++t)
            if (tracks[(size_t) t].id == find (itemId)->track)
                return t;

        return -1;
    };
    auto countWith = [&] (const juce::String& channel)
    {
        int n = 0;

        for (auto& i : model.get().playlistItems)
            n += i.channel == channel ? 1 : 0;

        return n;
    };
    auto place = [&] (int track, int startBars, const juce::String& channel = {}, const juce::String& ref = {})
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("track", track);
        o->setProperty ("start", startBars);
        o->setProperty ("unit", "bars");

        if (channel.isNotEmpty())  o->setProperty ("channel", channel);
        if (ref.isNotEmpty())      o->setProperty ("ref", ref);

        return app.run ("playlist.addItem", juce::var (o)).value["item"].toString();
    };

    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("transport.setTimeSignature", args ({ { "numerator", 4 }, { "denominator", 4 } }));

    const auto kick = channelId (model.get(), "Kick"), hat = channelId (model.get(), "Hat"), bass = channelId (model.get(), "808");
    const auto p1 = model.get().currentPattern;
    app.run ("steps.clear");
    setGrid (app, kick, "x...x...x...x...");
    setGrid (app, hat, "x.x.x.x.x.x.x.x.", 90);
    addNotes (app, bass, { noteVar (0.0, 1.5, 36, false), noteVar (2.0, 1.75, 39, false) });
    app.run ("transport.setSongMode", args ({ { "on", true } }));

    {
        juce::StringArray withContent;

        if (const auto* pat = model.get().findPattern (p1))
            for (auto& c : model.get().channels)
                if (pat->hasContent (c.id))
                    withContent.add (c.id);

        rep.check (kick.isNotEmpty() && hat.isNotEmpty() && bass.isNotEmpty() && withContent.size() == 3
                       && withContent.contains (kick) && withContent.contains (hat) && withContent.contains (bass),
                   "o padrão do teste tem conteúdo em 3 canais: Kick e Hat nos passos, 808 no piano roll ("_t + juce::String (withContent.size()) + ")");
    }

    //==========================================================================
    // o motor: só o Kick, igual ao padrão clonado que só tem o Kick
    const auto whole = place (1, 0);
    const auto rWhole = render ("inteiro.wav");
    const auto only = app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", kick } }));
    const auto rKick = render ("so-kick.wav");

    const auto clone = app.run ("pattern.clone", args ({ { "pattern", p1 } })).value["pattern"].toString();
    app.run ("steps.clear", args ({ { "channel", hat }, { "pattern", clone } }));
    app.run ("pianoroll.clear", args ({ { "channel", bass }, { "pattern", clone } }));
    app.run ("pattern.select", args ({ { "pattern", p1 } }));
    app.run ("playlist.updateItem", args ({ { "item", whole }, { "muted", true } }));
    const auto cloneItem = place (2, 0, {}, clone);
    const auto rClone = render ("clone-so-kick.wav");

    {
        const float sameAsClone = maxDiff (rKick, rClone), fromWhole = maxDiff (rKick, rWhole);
        rep.check (only.ok && find (whole)->channel == kick && find (whole)->ref == p1,
                   "playlist.updateItem com channel: o bloco continua no mesmo padrão e passa a tocar só o Kick"_t
                       + (only.ok ? juce::String() : ": " + only.error));
        rep.check (rWhole.valid && rKick.valid && rClone.valid && peakDb (rKick) > -40.0f && sameAsClone <= sameSound,
                   "o bloco com channel = Kick soa igual ao bloco de um padrão clonado só com o Kick (diferença máxima "_t
                       + num (sameAsClone) + ", pico " + num (peakDb (rKick), 1) + " dB)");
        rep.check (fromWhole > 0.01f, "e diferente do bloco inteiro, que toca o Hat e o 808 também (diferença "_t + num (fromWhole, 3) + ")");
    }

    app.run ("playlist.removeItem", args ({ { "item", cloneItem } }));
    app.run ("playlist.updateItem", args ({ { "item", whole }, { "muted", false } }));

    {
        // continua referência: o kick do padrão muda o bloco; o hat, que o bloco não toca, não muda nada
        const auto before = render ("kick-antes.wav");
        setGrid (app, kick, "x.x.x.x.x.x.x.x.");
        const auto after = render ("kick-depois.wav");
        setGrid (app, hat, "xxxxxxxxxxxxxxxx", 90);
        const auto hatChanged = render ("kick-hat-mudou.wav");
        const float d0 = maxDiff (before, rKick), d1 = maxDiff (after, before), d2 = maxDiff (hatChanged, after);
        rep.check (d0 <= sameSound && d1 > 0.01f && d2 <= sameSound && find (whole)->channel == kick,
                   "editar o padrão muda o que o bloco de um canal toca (kick novo: "_t + num (d1, 3) + "), e mexer no hat não muda ("
                       + num (d2) + "; o mesmo render de novo: " + num (d0) + ")");
        setGrid (app, kick, "x...x...x...x...");
        setGrid (app, hat, "x.x.x.x.x.x.x.x.", 90);
    }

    //==========================================================================
    // os comandos: addItem com channel, as recusas, o desfazer
    {
        const auto before = model.get().playlistItems.size();
        const auto added = place (3, 2, hat);
        rep.check (exists (added) && find (added)->channel == hat && find (added)->kind == "pattern",
                   "playlist.addItem com channel cria o bloco de um canal só"_t);
        app.run ("edit.undo");
        rep.check (! exists (added) && model.get().playlistItems.size() == before, "Ctrl+Z do addItem: o bloco sai"_t);

        app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", "" } }));
        rep.check (find (whole)->channel.isEmpty(), "updateItem com channel \"\": o bloco volta a tocar todos os canais"_t);
        app.run ("edit.undo");
        rep.check (find (whole)->channel == kick, "Ctrl+Z do updateItem: o canal do bloco volta"_t);

        const auto tone = app.run ("playlist.addItem", args ({ { "track", 5 }, { "start", 0 }, { "path", writeTone (work).getFullPathName() } }));
        const auto audioItem = tone.value["item"].toString(), audioChannel = tone.value["ref"].toString();
        const auto noChannel = app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", "ch_nao_existe" } }));
        const auto onAudioItem = app.run ("playlist.updateItem", args ({ { "item", audioItem }, { "channel", kick } }));
        const auto audioAsChannel = app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", audioChannel } }));
        const auto addBad = app.run ("playlist.addItem", args ({ { "track", 6 }, { "start", 0 }, { "channel", "ch_nao_existe" } }));
        const auto addAudio = app.run ("playlist.addItem", args ({ { "kind", "audio" }, { "ref", audioChannel }, { "track", 6 }, { "start", 0 }, { "channel", kick } }));
        const auto splitSingle = app.run ("playlist.splitByChannel", args ({ { "item", whole } }));
        const auto splitAudio = app.run ("playlist.splitByChannel", args ({ { "item", audioItem } }));
        rep.check (tone.ok && ! noChannel.ok && ! onAudioItem.ok && ! audioAsChannel.ok && ! addBad.ok && ! addAudio.ok && ! splitSingle.ok && ! splitAudio.ok
                       && find (whole)->channel == kick && find (audioItem)->channel.isEmpty(),
                   "recusas: canal que não existe, canal de áudio, bloco de áudio, separar bloco que já é de um canal ou de áudio (\""_t
                       + noChannel.error + "\")");
        app.run ("playlist.removeItem", args ({ { "item", audioItem } }));
        app.run ("channel.remove", args ({ { "channel", audioChannel } }));   // o canal de áudio do teste sai (e a trilha dele)

        // cortar leva o canal para os dois lados (a struct inteira é copiada)
        const auto cut = app.run ("playlist.splitItem", args ({ { "item", whole }, { "at", 2 }, { "unit", "beats" } }));
        rep.check (cut.ok && find (cut.value["left"].toString())->channel == kick && find (cut.value["right"].toString())->channel == kick,
                   "cortar o bloco (splitItem): as duas metades continuam só com o Kick"_t);
        app.run ("edit.undo");
    }

    //==========================================================================
    // separar por canal: raias distintas, sem pisar, a soma igual, um passo de desfazer
    app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", "" } }));
    const auto obstacle = place (2, 0);
    app.run ("playlist.updateItem", args ({ { "item", obstacle }, { "muted", true } }));   // ocupa a raia 2 e não soa

    {
        const auto itemsBefore = model.get().playlistItems.size();
        const auto split = app.run ("playlist.splitByChannel", args ({ { "item", whole } }));
        const auto* ids = split.value["items"].getArray();
        const bool three = split.ok && ids != nullptr && ids->size() == 3;
        const auto second = three ? (*ids)[1].toString() : juce::String(), third = three ? (*ids)[2].toString() : juce::String();
        juce::StringArray channels;
        bool sameShape = three;

        if (three)
            for (auto& v : *ids)
            {
                const auto* i = find (v.toString());
                channels.add (i->channel);
                sameShape = sameShape && i->ref == p1 && i->start == find (whole)->start && i->length == find (whole)->length
                            && i->patternOffset == find (whole)->patternOffset && ! i->muted;
            }

        rep.check (three && (*ids)[0].toString() == whole && sameShape && model.get().playlistItems.size() == itemsBefore + 2,
                   "playlist.splitByChannel: o bloco vira 3, um por canal, no mesmo trecho (o primeiro é o próprio bloco)"_t
                       + (split.ok ? juce::String() : ": " + split.error));

        // na ordem dos canais do rack
        juce::StringArray rackOrder;

        for (auto& c : model.get().channels)
            if (c.id == kick || c.id == hat || c.id == bass)
                rackOrder.add (c.id);

        rep.check (channels == rackOrder, "um bloco para cada canal com conteúdo, na ordem do rack"_t);
        rep.check (three && laneOf (whole) == 0 && laneOf (obstacle) == 1 && laneOf (second) == 2 && laneOf (third) == 3,
                   "as raias: o primeiro fica na raia do bloco, os outros descem pulando a raia ocupada (raias "_t
                       + juce::String (laneOf (whole) + 1) + ", " + juce::String (laneOf (second) + 1) + " e " + juce::String (laneOf (third) + 1)
                       + "; o bloco que já estava, na " + juce::String (laneOf (obstacle) + 1) + ")");

        const auto rSplit = render ("separado.wav");
        const float d = maxDiff (rSplit, rWhole);
        rep.check (rSplit.valid && d <= sameSound, "a soma dos blocos separados dá o render do bloco inteiro (diferença máxima "_t + num (d) + ")");

        app.run ("edit.undo");
        rep.check (model.get().playlistItems.size() == itemsBefore && find (whole)->channel.isEmpty() && ! exists (second) && ! exists (third),
                   "Ctrl+Z do separar: um passo só, e volta o bloco inteiro"_t);
        app.run ("edit.redo");
        rep.check (model.get().playlistItems.size() == itemsBefore + 2 && find (whole)->channel == channels[0] && exists (second) && exists (third),
                   "Ctrl+Y refaz os 3 blocos"_t);
        app.run ("edit.undo");
    }

    {
        // na última raia: as que faltam são criadas, e o desfazer tira
        const int last = (int) model.get().playlistTracks.size();
        const auto low = place (last, 4);
        const int tracksBefore = (int) model.get().playlistTracks.size();
        const auto split = app.run ("playlist.splitByChannel", args ({ { "item", low } }));
        const auto* ids = split.value["items"].getArray();
        bool lanesOk = split.ok && ids != nullptr && ids->size() == 3;

        if (lanesOk)
            for (int k = 0; k < 3; ++k)
                lanesOk = lanesOk && laneOf ((*ids)[k].toString()) == last - 1 + k;

        rep.check (lanesOk && (int) model.get().playlistTracks.size() > tracksBefore,
                   "separar na última raia cria as raias que faltam ("_t + juce::String (tracksBefore) + " -> "
                       + juce::String ((int) model.get().playlistTracks.size()) + " raias)"
                       + (split.ok ? juce::String() : ": " + split.error));
        app.run ("edit.undo");
        rep.check ((int) model.get().playlistTracks.size() == tracksBefore && find (low)->channel.isEmpty(),
                   "Ctrl+Z: as raias criadas saem junto"_t);

        // travado e padrão vazio
        const bool locked = app.run ("audio.setProperties", args ({ { "item", low }, { "locked", true } })).ok && find (low)->locked;
        const auto splitLocked = locked ? app.run ("playlist.splitByChannel", args ({ { "item", low } })) : CommandResult::fail ({});
        app.run ("audio.setProperties", args ({ { "item", low }, { "locked", false } }));
        app.run ("steps.clear", args ({ { "pattern", clone } }));
        const auto emptyItem = place (last, 8, {}, clone);
        const auto splitEmpty = app.run ("playlist.splitByChannel", args ({ { "item", emptyItem } }));
        rep.check (locked && ! splitLocked.ok && ! splitEmpty.ok && exists (emptyItem) && find (low)->channel.isEmpty(),
                   "separar recusa o bloco travado e o padrão vazio (\""_t + splitEmpty.error + "\")");
        app.run ("playlist.removeItem", args ({ { "item", emptyItem } }));
        app.run ("playlist.removeItem", args ({ { "item", low } }));
    }

    //==========================================================================
    // o .wnl
    app.run ("playlist.removeItem", args ({ { "item", obstacle } }));

    {
        const auto without = wnl::toJson (model.get());
        bool anyChannelKey = false;

        if (auto* items = juce::JSON::parse (without)["playlist"]["items"].getArray())
            for (auto& io : *items)
                anyChannelKey = anyChannelKey || io.hasProperty ("channel");

        rep.check (wnl::usedFeatures (model.get()).isEmpty() && ! without.contains ("\"features\"") && ! anyChannelKey
                       && without == wnl::toJsonReference (model.get()),
                   "arquivo sem bloco de um canal: sem \"channel\" nos blocos e sem \"features\", igual nos dois escritores"_t);

        app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", kick } }));
        const auto with = wnl::toJson (model.get());
        Project back;
        wnl::LoadReport report;
        const auto read = wnl::fromJson (with, back, &report);
        const auto* again = back.findItem (whole);
        rep.check (wnl::usedFeatures (model.get()) == juce::StringArray ("playlist.itemChannel") && with.contains ("\"playlist.itemChannel\"")
                       && with.contains ("\"channel\": \"" + kick + "\"") && with == wnl::toJsonReference (model.get()),
                   "arquivo com o recurso: \"channel\" no bloco e \"playlist.itemChannel\" em app.features, igual nos dois escritores"_t);
        rep.check (read.wasOk() && again != nullptr && again->channel == kick && report.unknownFeatures.isEmpty() && wnl::toJson (back) == with,
                   "o fromJson devolve o mesmo projeto, e este app conhece o recurso (sem aviso de app mais novo)"_t);

        app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", "" } }));
        rep.check (wnl::toJson (model.get()) == without, "limpar o canal: o arquivo volta a sair igual, byte a byte, ao de antes"_t);
        app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", kick } }));

        const auto file = work.getChildFile ("bloco-canal.wnl");
        const auto saved = app.run ("project.save", args ({ { "path", file.getFullPathName() } }));
        const auto text = file.loadFileAsString();
        app.run ("project.new");
        const auto opened = app.run ("project.open", args ({ { "path", file.getFullPathName() } }));
        rep.check (saved.ok && opened.ok && text.contains ("\"playlist.itemChannel\"") && find (whole)->channel == kick && find (whole)->ref == p1,
                   "salvar e reabrir preserva o canal do bloco"_t + (saved.ok ? juce::String() : ": " + saved.error)
                       + (opened.ok ? juce::String() : ": " + opened.error));
        app.run ("transport.setSongMode", args ({ { "on", true } }));
    }

    //==========================================================================
    // o export MIDI e os stems (só o bloco do Kick na música)
    {
        auto nameOf = [&] (const juce::String& id) { const auto* c = model.get().findChannel (id); return c != nullptr ? c->name : juce::String(); };
        auto exportMidi = [&] (const juce::String& name, juce::MidiFile& midi)
        {
            const auto f = work.getChildFile (name);
            f.deleteFile();
            const auto r = app.run ("project.exportMidi", args ({ { "path", f.getFullPathName() }, { "what", "song" } }));
            juce::FileInputStream in (f);
            return r.ok && in.openedOk() && midi.readFrom (in);
        };

        juce::MidiFile onlyKick, all;
        const bool wrote = exportMidi ("so-kick.mid", onlyKick);
        const int k = noteOnsOf (onlyKick, nameOf (kick)), h = noteOnsOf (onlyKick, nameOf (hat)), b = noteOnsOf (onlyKick, nameOf (bass));
        app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", "" } }));
        const bool wroteAll = exportMidi ("inteiro.mid", all);
        const int k2 = noteOnsOf (all, nameOf (kick)), h2 = noteOnsOf (all, nameOf (hat)), b2 = noteOnsOf (all, nameOf (bass));
        app.run ("playlist.updateItem", args ({ { "item", whole }, { "channel", kick } }));
        rep.check (wrote && wroteAll && k == 4 && h == 0 && b == 0 && k2 == 4 && h2 > 0 && b2 > 0,
                   "export MIDI: com o bloco só do Kick saem "_t + juce::String (k) + " notas do Kick, " + juce::String (h) + " do Hat e "
                       + juce::String (b) + " do 808 (com o bloco inteiro: " + juce::String (k2) + ", " + juce::String (h2) + " e " + juce::String (b2) + ")");

        const auto stems = app.run ("project.exportStems", args ({ { "folder", work.getChildFile ("stems").getFullPathName() }, { "what", "song" } }));
        const auto* files = stems.value["files"].getArray();
        const auto* kickChannel = model.get().findChannel (kick);
        const auto* hatChannel = model.get().findChannel (hat);
        const auto kickTrack = kickChannel != nullptr ? kickChannel->output : juce::String();
        bool kickLoud = false, othersQuiet = files != nullptr && files->size() >= 2;
        float loudest = -200.0f, quietest = -200.0f;

        if (files != nullptr)
            for (auto& f : *files)
            {
                const auto audio = readWav (juce::File (f["path"].toString()));
                const float peak = audio.valid ? peakDb (audio) : 0.0f;

                if (f["track"].toString() == kickTrack)
                {
                    kickLoud = audio.valid && peak > -40.0f;
                    loudest = peak;
                }
                else
                {
                    othersQuiet = othersQuiet && audio.valid && peak < -80.0f;
                    quietest = juce::jmax (quietest, peak);
                }
            }

        rep.check (stems.ok && hatChannel != nullptr && kickTrack != hatChannel->output && kickLoud && othersQuiet,
                   "stems: o do Kick tem som ("_t + num (loudest, 1) + " dB) e os dos outros canais saem em silêncio (o mais alto: "
                       + num (quietest, 1) + " dB)" + (stems.ok ? juce::String() : ": " + stems.error));
    }

    //==========================================================================
    // remover o canal: os blocos que só tocavam ele saem (como o bloco de áudio sem o canal); o desfazer traz tudo
    {
        const auto hatBlock = place (3, 0, hat);
        const auto allBlock = place (4, 0);
        const auto removed = app.run ("channel.remove", args ({ { "channel", hat } }));
        rep.check (removed.ok && ! exists (hatBlock) && exists (allBlock) && exists (whole) && find (whole)->channel == kick
                       && model.get().findChannel (hat) == nullptr,
                   "remover o canal Hat: o bloco que só tocava o Hat sai; o do Kick e o inteiro ficam"_t);
        app.run ("edit.undo");
        rep.check (model.get().findChannel (hat) != nullptr && exists (hatBlock) && find (hatBlock)->channel == hat && laneOf (hatBlock) == 2,
                   "Ctrl+Z: o canal volta, e o bloco dele também, no mesmo lugar"_t);
        app.run ("playlist.removeItem", args ({ { "item", hatBlock } }));
        app.run ("playlist.removeItem", args ({ { "item", allBlock } }));
    }

    //==========================================================================
    // a playlist, sem janela
    {
        Offscreen<PlaylistGrid> screen (app, 1200, 600);
        auto& grid = screen.view;
        juce::String status;
        grid.onStatus = [&status] (const juce::String& text, bool) { status = text; };
        const auto second = place (2, 2);
        grid.refresh (model.get());

        const auto* pat = model.get().findPattern (p1);
        const auto* kickChannel = model.get().findChannel (kick);
        const auto expected = (pat != nullptr ? pat->name : juce::String()) + " · "_t + (kickChannel != nullptr ? kickChannel->name : juce::String());
        rep.check (grid.blockName (whole) == expected && grid.blockName (second) == (pat != nullptr ? pat->name : juce::String()),
                   "o bloco de um canal mostra \""_t + grid.blockName (whole) + "\"; o inteiro, só o nome do padrão");
        rep.check (savePng (screen.host, work.getChildFile ("bloco-canal-playlist.png")), "captura: bloco-canal-playlist.png"_t);

        // o menu "Tocar só o canal" com dois blocos selecionados: os dois, num passo de desfazer
        grid.selectAll();
        grid.setBlockChannel (whole, hat);
        rep.check (find (whole)->channel == hat && find (second)->channel == hat, "\"Tocar só o canal\" com 2 blocos selecionados: os dois passam para o Hat"_t);
        app.run ("edit.undo");
        rep.check (find (whole)->channel == kick && find (second)->channel.isEmpty(), "um Ctrl+Z desfaz os dois"_t);

        // a ação do KeyMap: separa os selecionados que ainda tocam tudo (o do Kick fica como está)
        grid.refresh (model.get());
        grid.selectAll();
        const auto itemsBefore = model.get().playlistItems.size();
        const bool ran = grid.runShortcut ("playlist.splitByChannel", juce::KeyPress());
        rep.check (ran && model.get().playlistItems.size() == itemsBefore + 2 && find (whole)->channel == kick && find (second)->channel.isNotEmpty()
                       && grid.getSelection().size() == 3 && status.contains ("3 blocos"),
                   "a ação \"Separar por canal\" do KeyMap: o bloco inteiro vira 3, que ficam selecionados (\""_t + status + "\")");
        app.run ("edit.undo");
        rep.check (model.get().playlistItems.size() == itemsBefore && find (second)->channel.isEmpty(), "um Ctrl+Z desfaz o separar da tela"_t);

        // duplicar, copiar e colar levam o canal
        grid.refresh (model.get());
        grid.selectAll();
        const int kicksBefore = countWith (kick);
        grid.runShortcut ("playlist.duplicate", juce::KeyPress());
        const int afterDuplicate = countWith (kick);
        grid.refresh (model.get());
        grid.selectAll();
        grid.runShortcut ("playlist.copy", juce::KeyPress());
        grid.runShortcut ("playlist.paste", juce::KeyPress());
        const int afterPaste = countWith (kick);
        rep.check (kicksBefore == 1 && afterDuplicate == 2 && afterPaste == 4,
                   "duplicar (Ctrl+B) e copiar e colar levam o canal do bloco ("_t + juce::String (kicksBefore) + " -> " + juce::String (afterDuplicate)
                       + " -> " + juce::String (afterPaste) + " blocos só do Kick)");
        app.run ("edit.undo");
        app.run ("edit.undo");
        app.run ("playlist.removeItem", args ({ { "item", second } }));
    }

    //==========================================================================
    // o rack: "Pôr na playlist (só este canal)"
    {
        Offscreen<ChannelRack> screen (app, 1180, 300);
        auto& rack = screen.view;
        juce::String status;
        rack.onStatus = [&status] (const juce::String& text, bool) { status = text; };
        rack.refresh (model.get());
        app.run ("pattern.select", args ({ { "pattern", p1 } }));
        app.run ("transport.setPosition", args ({ { "at", 0 } }));

        std::set<juce::String> before;

        for (auto& i : model.get().playlistItems)
            before.insert (i.id);

        const int start = app.getTimeSelection().isEmpty() ? app.playStart() : app.getTimeSelection().getStart();
        const bool placed = rack.placeInPlaylist (bass);
        const PlaylistItem* fresh = nullptr;

        for (auto& i : model.get().playlistItems)
            if (before.count (i.id) == 0)
                fresh = &i;

        bool lonely = fresh != nullptr;   // nenhum outro bloco na raia dele, naquele trecho

        if (fresh != nullptr)
            for (auto& i : model.get().playlistItems)
                lonely = lonely && (i.id == fresh->id || i.track != fresh->track || i.end() <= fresh->start || i.start >= fresh->end());

        rep.check (placed && fresh != nullptr && fresh->channel == bass && fresh->ref == p1 && fresh->start == start && lonely
                       && fresh->track != find (whole)->track && model.get().playlistItems.size() == before.size() + 1,
                   "rack, \"Pôr na playlist (só este canal)\": um bloco do padrão atual só com o 808, na agulha, numa raia livre (\""_t + status + "\")");
        app.run ("edit.undo");
        rep.check (model.get().playlistItems.size() == before.size(), "Ctrl+Z tira o bloco que o rack pôs"_t);

        app.run ("steps.clear", args ({ { "channel", hat }, { "pattern", p1 } }));
        const bool refused = ! rack.placeInPlaylist (hat);
        rep.check (refused && model.get().playlistItems.size() == before.size(),
                   "canal sem notas nem passos no padrão: nada vai para a playlist (\""_t + status + "\")");
        app.run ("edit.undo");
    }
}

} // namespace whynot::selftest
