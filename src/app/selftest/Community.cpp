// Autoteste do bloco 1 da pesquisa das comunidades do FL e do Ableton (pedido do usuário de 22/10):
// - as ferramentas do piano roll (legato, juntar, mudo, humanizar, escala, carimbo de acorde), puras e pelo
//   comando pianoroll.transform, e na tela (atalhos, menu, clique com o carimbo e com o encaixe na escala);
// - importar MIDI (pianoroll.importMidi): um .mid escrito aqui, o arquivo ruim e o caminho que não existe;
// - os marcadores na régua (playlist.*Marker), no .wnl e na tela (clique e duplo clique);
// - 10 efeitos por trilha, o LUFS do master e o botão de comparar no mixer;
// - o tap tempo.
// As telas ficam num pai fora da área de trabalho (sem roubar o foco), com capturas em comunidade-*.png.

#include "SelfTestKit.h"
#include "../../engine/Projector.h"
#include "../../model/NoteTools.h"
#include "../../model/WnlFormat.h"
#include "../../ui/MixerView.h"
#include "../../ui/PianoRoll.h"
#include "../../ui/Playlist.h"
#include "../../ui/Theme.h"
#include "../../ui/TransportBar.h"

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

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, int mods = 0, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | mods),
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, p, now, clicks, false };
    }

    void click (juce::Component& c, juce::Point<float> p)
    {
        c.mouseDown (mouseAt (c, p));
        c.mouseUp (mouseAt (c, p));
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

    std::vector<Note> notesOf (const Project& p, const juce::String& channel)
    {
        const auto* pat = p.currentPatternPtr();
        const auto* list = pat != nullptr ? pat->notesFor (channel) : nullptr;
        return list != nullptr ? *list : std::vector<Note>();
    }

    juce::String keysText (const std::vector<int>& keys)
    {
        juce::StringArray s;

        for (int k : keys)
            s.add (juce::String (k));

        return s.joinIntoString (" ");
    }

    juce::String instrumentChannel (const Project& p)
    {
        for (auto& c : p.channels)
            if (! c.isAudio())
                return c.id;

        return {};
    }

    /** Um .mid de teste: 96 ticks por tempo, 90 BPM, três notas que começam no compasso 2. */
    bool writeTestMidi (const juce::File& file)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / 90.0)), 0.0);

        auto note = [&seq] (int key, int vel, double on, double off)
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) vel), on);
            seq.addEvent (juce::MidiMessage::noteOff (1, key), off);
        };

        note (60, 100, 384.0, 480.0);   // compasso 2, um tempo
        note (64, 90, 432.0, 528.0);    // meio tempo depois
        note (67, 1, 480.0, 576.0);
        seq.updateMatchedPairs();

        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (96);
        midi.addTrack (seq);
        file.deleteFile();

        juce::FileOutputStream out (file);
        return out.openedOk() && midi.writeTo (out);
    }
}

void runCommunitySelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: ferramentas do piano roll (puras)"_t);
    {
        using namespace notetools;

        std::vector<Note> chords { { 1, 0, 100, 60, 100, false }, { 2, 0, 100, 64, 100, false },
                                   { 3, 480, 100, 67, 100, false }, { 4, 960, 50, 60, 100, false } };
        const int legatoChanged = legato (chords, {});
        rep.check (legatoChanged == 3 && chords[0].length == 480 && chords[1].length == 480 && chords[2].length == 480 && chords[3].length == 50,
                   "legato: o acorde vai junto até a próxima nota e a última fica como está"_t);

        std::vector<Note> some = chords;
        legato (some, { 3 });
        rep.check (some[2].length == 480 && some[0].length == 480, "legato só na nota escolhida: as outras ficam"_t);

        std::vector<Note> touching { { 1, 0, 240, 60, 100, false }, { 2, 240, 240, 60, 80, false }, { 3, 600, 100, 60, 100, false },
                                     { 4, 0, 480, 62, 100, false }, { 5, 1000, 300, 70, 100, false }, { 6, 1200, 300, 70, 100, false } };
        const auto removed = glue (touching, {});
        const auto* first = std::find_if (touching.begin(), touching.end(), [] (const Note& n) { return n.id == 1; }).operator->();
        const auto* crossed = std::find_if (touching.begin(), touching.end(), [] (const Note& n) { return n.id == 5; }).operator->();
        rep.check (removed.size() == 2 && touching.size() == 4 && first->length == 480 && first->velocity == 100 && crossed->length == 500,
                   "juntar: as que se encostam e as que se cruzam viram uma; a que tem espaço e a de outra tecla ficam"_t);

        std::vector<Note> mutable_ { { 1, 0, 100, 60, 100, false }, { 2, 100, 100, 62, 100, false } };
        rep.check (setMuted (mutable_, {}, true) == 2 && mutable_[0].muted && setMuted (mutable_, { 2 }, false) == 1 && ! mutable_[1].muted
                       && mutable_[0].muted,
                   "mudo e tirar o mudo, em todas e só na escolhida"_t);

        std::vector<Note> base;

        for (int i = 0; i < 32; ++i)
            base.push_back ({ i + 1, 10 + i * 240, 120, 60, 100, false });

        auto a = base, b = base;
        juce::Random r1 (7), r2 (7);
        humanize (a, {}, 20, 10, r1);
        humanize (b, {}, 20, 10, r2);
        bool inRange = true, moved = false;

        for (size_t i = 0; i < a.size(); ++i)
        {
            inRange = inRange && std::abs (a[i].start - base[i].start) <= 20 && std::abs (a[i].velocity - base[i].velocity) <= 10
                      && a[i].start >= 0 && a[i].key == base[i].key && a[i].length == base[i].length;
            moved = moved || a[i].start != base[i].start;
        }

        rep.check (inRange && moved && a == b, "humanizar: até 20 ticks e 10 de força, nada antes do 0, e a mesma semente dá o mesmo"_t);

        const auto minor = Project::scaleDegrees ("minor");
        std::vector<Note> outside { { 1, 0, 100, 64, 100, false }, { 2, 0, 100, 71, 100, false }, { 3, 0, 100, 60, 100, false } };
        rep.check (toScale (outside, {}, 0, minor) == 2 && outside[0].key == 63 && outside[1].key == 70 && outside[2].key == 60,
                   "levar para a escala (Dó menor): Mi vai para Mi bemol, Si para Si bemol (empate: a de baixo), Dó fica"_t);

        const bool chordsOk = chordKeys ("scale3", 60, 0, minor) == std::vector<int> { 60, 63, 67 }
                              && chordKeys ("scale7", 60, 0, minor) == std::vector<int> { 60, 63, 67, 70 }
                              && chordKeys ("scale9", 60, 0, minor) == std::vector<int> { 60, 63, 67, 70, 74 }
                              && chordKeys ("scale3", 62, 0, minor) == std::vector<int> { 62, 65, 68 }
                              && chordKeys ("scale3", 61, 0, minor) == std::vector<int> { 60, 63, 67 }
                              && chordKeys ("maj", 60, 0, minor) == std::vector<int> { 60, 64, 67 }
                              && chordKeys ("power", 60, 0, minor) == std::vector<int> { 60, 67, 72 }
                              && chordKeys ("scale3", 60, 0, Project::scaleDegrees ("chromatic")) == std::vector<int> { 60, 64, 67 }
                              && chordKeys ("maj", 125, 0, minor) == std::vector<int> { 125 };
        rep.check (chordsOk, "carimbo: Dó menor da escala (C Eb G, com 7ª e 9ª), Ré da escala (D F Ab), formas fixas e as teclas fora de 0-127 saem ("_t
                                 + keysText (chordKeys ("scale9", 60, 0, minor)) + ")");
        rep.check (chordShapes().size() == 13, "13 acordes no carimbo"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: pianoroll.transform e o mudo no motor"_t);
    juce::String ch;
    {
        app.run ("project.new");
        app.run ("project.setKey", args ({ { "root", "C" }, { "scale", "minor" } }));
        ch = instrumentChannel (model.get());
        app.run ("pianoroll.clear", args ({ { "channel", ch } }));
        addNotes (app, ch, { noteVar (0.0, 0.25, 60, false), noteVar (0.0, 0.25, 64, false), noteVar (1.0, 0.25, 67, false),
                             noteVar (2.0, 0.5, 71, false) });

        auto r = app.run ("pianoroll.transform", args ({ { "channel", ch }, { "op", "legato" } }));
        auto notes = notesOf (model.get(), ch);
        rep.check (r.ok && (int) r.value["changed"] == 3 && notes.size() == 4 && notes[0].length == 960 && notes[2].length == 960 && notes[3].length == 480,
                   "legato pelo comando: o acorde e a nota vão até a próxima"_t);

        app.run ("edit.undo");
        rep.check (notesOf (model.get(), ch)[0].length == 240, "desfazer volta o legato num passo"_t);

        const int first = notesOf (model.get(), ch)[0].id;
        r = app.run ("pianoroll.transform", args ({ { "channel", ch }, { "op", "mute" }, { "ids", juce::Array<juce::var> { first } } }));
        const auto& p = model.get();
        const auto* pat = p.currentPatternPtr();
        const auto* channel = p.findChannel (ch);
        const auto played = pat != nullptr && channel != nullptr ? Projector::notesFor (p, *pat, *channel, pat->lengthTicks()) : std::vector<Note>();
        rep.check (r.ok && notesOf (p, ch)[0].muted && played.size() == 3,
                   "a nota muda fica no piano roll e sai do que toca (e do MIDI): "_t + juce::String ((int) played.size()) + " de 4"_t);

        const auto json = wnl::toJson (p);
        Project back;
        rep.check (json.contains ("\"muted\": true") && wnl::fromJson (json, back).wasOk() && notesOf (back, ch)[0].muted
                       && json == wnl::toJsonReference (p),
                   "a nota muda vai e volta no .wnl, igual nos dois escritores"_t);

        r = app.run ("pianoroll.transform", args ({ { "channel", ch }, { "op", "toScale" } }));
        notes = notesOf (model.get(), ch);
        rep.check (r.ok && (int) r.value["changed"] == 2 && notes[1].key == 63 && notes[3].key == 70,
                   "levar para a escala pelo comando (o tom do projeto, Dó menor)"_t);

        app.run ("pianoroll.transform", args ({ { "channel", ch }, { "op", "humanize" }, { "seed", 5 } }));
        const auto once = notesOf (model.get(), ch);
        app.run ("edit.undo");
        app.run ("pianoroll.transform", args ({ { "channel", ch }, { "op", "humanize" }, { "seed", 5 } }));
        rep.check (notesOf (model.get(), ch) == once && once != notes, "humanizar com semente: o mesmo resultado de novo"_t);

        rep.check (! app.run ("pianoroll.transform", args ({ { "channel", ch }, { "op", "inventado" } })).ok
                       && ! app.run ("pianoroll.transform", args ({ { "channel", "ch_nao_existe" }, { "op", "legato" } })).ok,
                   "ferramenta ou canal que não existe é recusado"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: importar MIDI"_t);
    {
        const auto midiFile = ctx.work.getChildFile ("comunidade-acordes.mid");
        rep.check (writeTestMidi (midiFile), "um .mid de teste (96 ticks por tempo, 90 BPM, começa no compasso 2)"_t);

        app.run ("pianoroll.clear", args ({ { "channel", ch } }));
        auto r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", midiFile.getFullPathName() } }));
        auto notes = notesOf (model.get(), ch);
        const bool shaped = notes.size() == 3 && notes[0].start == 0 && notes[0].length == 960 && notes[0].key == 60 && notes[0].velocity == 100
                            && notes[1].start == 480 && notes[1].key == 64 && notes[2].start == 960 && notes[2].velocity == 1;
        rep.check (r.ok && (int) r.value["notes"] == 3 && std::abs ((double) r.value["fileBpm"] - 90.0) < 0.01 && shaped,
                   "importar: 3 notas no lugar (o compasso vazio do começo sai), na duração e na força, e o BPM do arquivo (90)"_t);

        r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", midiFile.getFullPathName() }, { "replace", false } }));
        rep.check (r.ok && notesOf (model.get(), ch).size() == 6, "importar somando às notas que já estavam"_t);

        app.run ("edit.undo");
        rep.check (notesOf (model.get(), ch).size() == 3, "desfazer tira a importação num passo"_t);

        const auto junk = ctx.work.getChildFile ("comunidade-ruim.mid");
        junk.replaceWithText ("isto não é MIDI"_t);
        const auto bad = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", junk.getFullPathName() } }));
        rep.check (! bad.ok && bad.error.contains ("MIDI") && notesOf (model.get(), ch).size() == 3, "arquivo que não é MIDI: recusado e nada muda"_t);
        rep.check (! app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", ctx.work.getChildFile ("nao-existe.mid").getFullPathName() } })).ok
                       && ! app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", "relativo.mid" } })).ok,
                   "arquivo que não existe e caminho incompleto: recusados"_t);
    }

    theme::LookAndFeel laf;   // o visual da casa, só neste pai (nada global)
    juce::Component host;
    host.setLookAndFeel (&laf);
    host.setSize (1400, 700);

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: piano roll na tela (atalhos, carimbo, escala, MIDI)"_t);
    {
        app.run ("pianoroll.clear", args ({ { "channel", ch } }));
        addNotes (app, ch, { noteVar (0.0, 0.25, 60, false), noteVar (1.0, 0.25, 62, false) });

        PianoRoll roll (app);
        host.addAndMakeVisible (roll);
        roll.setBounds (host.getLocalBounds());
        roll.setChannel (ch);
        roll.refresh (model.get());
        auto& grid = roll.getGrid();

        // o getNumItems não conta os separadores: Editar tem 11 itens (3 submenus; o strum e o Capture MIDI vieram no
        // bloco 3), o carimbo 13 acordes, o desligado e 2 títulos
        rep.check (roll.makeEditMenu().getNumItems() == 11 && roll.makeChordMenu().getNumItems() == 16,
                   "o menu Editar e o submenu do carimbo montam (todas as ferramentas e os 13 acordes)"_t);

        const auto ctrlL = juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0);
        rep.check (grid.keyPressed (ctrlL) && notesOf (model.get(), ch)[0].length == 960, "Ctrl+L faz o legato"_t);
        rep.check (grid.keyPressed (juce::KeyPress ('m', juce::ModifierKeys::altModifier, 0))
                       && notesOf (model.get(), ch)[0].muted && notesOf (model.get(), ch)[1].muted,
                   "Alt+M deixa as notas mudas (sem seleção, todas)"_t);
        rep.check (grid.keyPressed (juce::KeyPress ('m', juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier, 0))
                       && ! notesOf (model.get(), ch)[0].muted,
                   "Alt+Shift+M tira o mudo"_t);

        grid.setChordStamp ("scale3");
        roll.refresh (model.get());
        const auto before = notesOf (model.get(), ch).size();
        click (grid, grid.pointAt (2880.0 + 10.0, 60));
        auto notes = notesOf (model.get(), ch);
        std::vector<int> stamped;

        for (auto& n : notes)
            if (n.start == 2880)
                stamped.push_back (n.key);

        std::sort (stamped.begin(), stamped.end());
        rep.check (notes.size() == before + 3 && stamped == std::vector<int> { 60, 63, 67 },
                   "carimbo: um clique põe a tríade da escala (Dó menor: "_t + keysText (stamped) + ")");
        rep.check (savePng (roll, ctx.work.getChildFile ("comunidade-piano-carimbo.png")), "captura com o aviso do carimbo"_t);

        grid.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));   // a primeira tira a seleção
        grid.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));   // a segunda desliga o carimbo
        rep.check (grid.getChordStamp().isEmpty(), "Esc desliga o carimbo"_t);

        grid.setScaleLock (true);
        click (grid, grid.pointAt (3840.0 + 10.0, 64));
        notes = notesOf (model.get(), ch);
        const auto drawn = std::find_if (notes.begin(), notes.end(), [] (const Note& n) { return n.start == 3840; });
        rep.check (drawn != notes.end() && drawn->key == 63, "encaixe na escala: o clique no Mi põe o Mi bemol (Dó menor)"_t);
        grid.setScaleLock (false);

        const auto midiFile = ctx.work.getChildFile ("comunidade-acordes.mid");
        juce::String status;
        grid.onStatus = [&status] (const juce::String& m, bool) { status = m; };
        rep.check (roll.isInterestedInFileDrag (juce::StringArray (midiFile.getFullPathName())) && ! roll.isInterestedInFileDrag (juce::StringArray ("beat.wav")),
                   "arrastar: o piano roll aceita .mid e deixa o resto para a janela"_t);
        roll.filesDropped (juce::StringArray (midiFile.getFullPathName()), 300, 300);

        // soltar abre o cartão de opções (Importar MIDI, 22/10): Importar com todas as trilhas, trocando, no começo
        auto* dialog = roll.getMidiImportDialog();

        if (dialog != nullptr)
        {
            dialog->setChoice ({});
            dialog->accept();
        }

        rep.check (dialog != nullptr && roll.getMidiImportDialog() == nullptr
                       && notesOf (model.get(), ch).size() == 3 && status.contains ("3 notas") && status.contains ("90 BPM"),
                   "soltar o .mid abre o cartão, e o Importar dele traz as notas e avisa do BPM do arquivo: "_t + status);
        rep.check (savePng (roll, ctx.work.getChildFile ("comunidade-piano-midi.png")), "captura depois do MIDI importado"_t);
        host.removeChildComponent (&roll);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: marcadores na régua"_t);
    {
        app.run ("project.new");
        auto r1 = app.run ("playlist.addMarker", args ({ { "at", 3840 }, { "name", "  Refrão  "_t } }));
        auto r2 = app.run ("playlist.addMarker", args ({ { "at", 0 } }));
        const auto longName = juce::String::repeatedString ("a", 60);
        auto r3 = app.run ("playlist.addMarker", args ({ { "at", 8 }, { "unit", "bars" }, { "name", longName } }));
        const auto& m = model.get().markers;
        rep.check (r1.ok && r2.ok && r3.ok && m.size() == 3 && m[0].t == 0 && m[0].name == "Marcador 2" && m[1].name == "Refrão"_t
                       && m[2].t == 8 * ticksPerBar4 && m[2].name.length() == maxMarkerName,
                   "adicionar: em ordem, nome limpo, \"Marcador N\" sem nome e o nome longo cortado em 40"_t);

        const auto refrao = r1.value["id"].toString();
        rep.check (model.get().markerEnd (m[1]) == 8 * ticksPerBar4 && model.get().markerEnd (m[2]) >= 9 * ticksPerBar4,
                   "o trecho de um marcador vai até o próximo; o do último, até o fim da música"_t);

        rep.check (app.run ("playlist.updateMarker", args ({ { "marker", refrao }, { "at", 12 }, { "unit", "bars" } })).ok
                       && model.get().markers.back().id == refrao,
                   "mover o marcador reordena"_t);
        rep.check (! app.run ("playlist.updateMarker", args ({ { "marker", refrao }, { "name", "   " } })).ok
                       && ! app.run ("playlist.removeMarker", args ({ { "marker", "mk_nao_existe" } })).ok,
                   "nome vazio e marcador que não existe: recusados"_t);

        const auto json = wnl::toJson (model.get());
        Project back;
        rep.check (json.contains ("\"markers\"") && wnl::fromJson (json, back).wasOk() && back.markers == model.get().markers
                       && json == wnl::toJsonReference (model.get()),
                   "os marcadores vão e voltam no .wnl, iguais nos dois escritores"_t);

        Project messy = model.get();
        messy.markers.push_back ({ "", -5, "x" });
        messy.markers.push_back ({ refrao, 960, "" });
        wnl::normalise (messy);
        std::set<juce::String> ids;

        for (auto& mk : messy.markers)
            ids.insert (mk.id);

        rep.check (messy.markers.size() == 4 && messy.markers[1].t == 960 && messy.markers[1].name == "Marcador" && ids.size() == 4,
                   "o normalise tira o t inválido, dá nome ao vazio e um id novo ao repetido"_t);

        rep.check (app.run ("playlist.removeMarker", args ({ { "marker", refrao } })).ok && model.get().markers.size() == 2, "apagar o marcador"_t);
        app.run ("edit.undo");

        Playlist playlist (app);
        host.addAndMakeVisible (playlist);
        playlist.setBounds (host.getLocalBounds());
        playlist.refresh (model.get());

        if (auto* grid = childOf<PlaylistGrid> (playlist))
        {
            // o marcador "Refrão" está no compasso 13; com 96 px por compasso e as raias a partir de 150 px
            const auto* refMarker = model.get().findMarker (refrao);
            const float x = 150.0f + (float) refMarker->t * 96.0f / (float) ticksPerBar4 + 6.0f;
            click (*grid, { x, 8.0f });
            rep.check (app.playStart() == refMarker->t, "clique no marcador leva a agulha para ele"_t);

            grid->mouseDoubleClick (mouseAt (*grid, { x, 8.0f }, 0, 2));
            const auto range = app.getTimeSelection();
            rep.check (range.getStart() == refMarker->t && range.getEnd() == model.get().markerEnd (*refMarker),
                       "duplo clique seleciona o trecho do marcador (o loop e a exportação)"_t);
            rep.check (savePng (playlist, ctx.work.getChildFile ("comunidade-marcadores.png")), "captura da régua com os marcadores"_t);
        }
        else
        {
            rep.check (false, "a playlist montou sem a grade"_t);
        }

        host.removeChildComponent (&playlist);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: mixer (10 efeitos, LUFS do master, comparar)"_t);
    {
        app.run ("project.new");
        const auto track = app.run ("mixer.addTrack", args ({ { "name", "Dez efeitos" } })).value["track"].toString();
        int added = 0;

        for (int i = 0; i < 12; ++i)
            if (app.run ("mixer.addInsert", args ({ { "track", track }, { "type", "eq" } })).ok)
                ++added;

        const auto* mt = model.get().findMixer (track);
        const auto refused = app.run ("mixer.addInsert", args ({ { "track", track }, { "type", "eq" } }));
        rep.check (mt != nullptr && (int) mt->inserts.size() == maxInserts && maxInserts == 10 && ! refused.ok && refused.error.contains ("10"),
                   "10 efeitos por trilha; o 11º é recusado com o número certo ("_t + juce::String (added) + " entraram)");

        // depois do Medidor ainda pode vir o ajuste de volume do Comparar (criado no teste da mixagem)
        auto& masterList = app.getEdit().getMasterPluginList();
        const int meterAt = masterList.indexOf (app.getProjector().meterFor (masterTrackId));
        auto* tap = meterAt >= 0 ? dynamic_cast<WhynotFxPlugin*> (masterList[meterAt + 1]) : nullptr;
        rep.check (app.getProjector().masterLoudness() != nullptr && tap != nullptr && tap->getFxType() == "meter",
                   "o LUFS do master: o Medidor escondido logo depois do medidor de pico"_t);

        MixerView mixer (app);
        host.addAndMakeVisible (mixer);
        mixer.setBounds (host.getLocalBounds());
        mixer.refresh (model.get());
        mixer.tick();
        rep.check (mixer.getLoudnessText().isNotEmpty(), "o LUFS na barra do mixer: \""_t + mixer.getLoudnessText() + "\"");
        rep.check (savePng (mixer, ctx.work.getChildFile ("comunidade-mixer.png")), "captura do mixer com o LUFS e o Comparar"_t);

        if (ctx.demoProject.existsAsFile() && app.run ("project.open", args ({ { "path", ctx.demoProject.getFullPathName() } })).ok)
        {
            mixer.refresh (model.get());
            mixer.setCompareMode ("matched");
            const bool matched = mixer.getCompareMode() == "matched";
            mixer.setCompareMode ("off");
            rep.check (matched && mixer.getCompareMode() == "off", "o botão Comparar liga o mesmo volume e volta ao normal"_t);
        }
        else
        {
            rep.check (false, "sem o Demo: o botão Comparar não testado"_t);
        }

        host.removeChildComponent (&mixer);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: tap tempo"_t);
    {
        TapTempo tap;
        rep.check (tap.tap (1000.0) == 0.0 && tap.tap (1500.0) == 120.0 && tap.tap (2000.0) == 120.0 && tap.tap (2500.0) == 120.0,
                   "batidas a cada 500 ms: 120 BPM desde a segunda"_t);
        rep.check (tap.tap (5000.0) == 0.0 && tap.count() == 1, "uma pausa de mais de 2 s recomeça a conta"_t);

        TapTempo drift;
        double last = 0.0;

        for (int i = 0; i < 12; ++i)
            last = drift.tap (i * (i < 4 ? 400.0 : 600.0) + (i < 4 ? 0.0 : -800.0));

        rep.check (drift.count() == 8 && last == 100.0, "só as 8 últimas batidas contam (100 BPM depois de mudar de ritmo)"_t);

        TapTempo fast;
        fast.tap (0.0);
        rep.check (fast.tap (10.0) == 999.0, "batidas absurdas ficam no teto de 999"_t);

        app.run ("project.new");
        const double bpmBefore = model.get().bpm;
        TransportBar bar (app);
        host.addAndMakeVisible (bar);
        bar.setBounds (0, 0, 1400, 56);
        juce::String status;
        bar.onStatus = [&status] (const juce::String& m, bool) { status = m; };
        bar.tapTempo();
        rep.check (model.get().bpm == bpmBefore && status.contains ("continue"), "o botão TAP: a primeira batida só pede mais ("_t + status + ")");
        rep.check (savePng (bar, ctx.work.getChildFile ("comunidade-transporte.png")), "captura da barra com o TAP"_t);
        host.removeChildComponent (&bar);
    }

    host.setLookAndFeel (nullptr);
    app.run ("project.new");
}

} // namespace whynot::selftest
