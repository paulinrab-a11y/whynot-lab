// Autoteste do bloco 1 da pesquisa das comunidades do FL e do Ableton (pedido do usuário de 22/10):
// - as ferramentas do piano roll (legato, juntar, mudo, humanizar, escala, carimbo de acorde), puras e pelo
//   comando pianoroll.transform, e na tela (atalhos, menu, clique com o carimbo e com o encaixe na escala);
// - importar MIDI (pianoroll.importMidi): um .mid escrito aqui, o arquivo ruim e o caminho que não existe;
// - os marcadores na régua (playlist.*Marker), no .wnl e na tela (clique e duplo clique);
// - 10 efeitos por trilha, o LUFS do master e o botão de comparar no mixer;
// - o tap tempo.
// Do bloco 4 (23/10): as raias da playlist (cor, mudo, solo), o navegador com BPM e tom (4b), o rack com 8 macros (4c) e o
// MIDI learn (4d, com o CC entrando direto no AppController::handleMidiControl, sem controlador de verdade) e o áudio
// para MIDI (4e, com um beatbox e uma melodia gerados aqui) e as camadas por força (4f, renderizando o padrão).
// As telas ficam num pai fora da área de trabalho (sem roubar o foco), com capturas em comunidade-*.png.

#include "SelfTestKit.h"
#include "../../dsp/FxCatalog.h"
#include "../../engine/Projector.h"
#include "../../engine/SampleAnalysis.h"
#include "../../model/NoteTools.h"
#include "../../model/WnlFormat.h"
#include "../../ui/ChannelRack.h"
#include "../../ui/ControlMenu.h"
#include "../../ui/MidiLearnMenu.h"
#include "../../ui/MixerView.h"
#include "../../ui/PianoRoll.h"
#include "../../ui/SamplerPanel.h"
#include "../../ui/synth/SynthEditor.h"
#include "../../synth/SynthPresets.h"
#include "../../ui/fx/FxWidgets.h"
#include "../../ui/Playlist.h"
#include "../../ui/SampleBrowser.h"
#include "../../engine/SoundInfo.h"
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

    /** A captura sobre o cartão do app: um painel que não pinta o fundo (o pai pinta) sai como na tela. */
    bool savePngOnPanel (juce::Component& c, const juce::File& file)
    {
        juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()), juce::jmax (1, c.getHeight()), true);
        {
            juce::Graphics g (image);
            g.fillAll (theme::colour::panel);
            g.drawImageAt (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), 0, 0);
        }
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

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: organizar a playlist (bloco 4: cor, mudo, solo e a ordem das raias)"_t);
    {
        app.run ("project.new");
        const auto laneCh = model.get().channels.front().id;
        const auto patternA = model.get().currentPattern;
        app.run ("pianoroll.addNotes", args ({ { "channel", laneCh }, { "notes", juce::Array<juce::var> { noteVar (0, 1, 50, false) } }, { "unit", "beats" } }));
        app.run ("pattern.add", args ({ { "name", "B" } }));
        const auto patternB = model.get().currentPattern;
        app.run ("pianoroll.addNotes", args ({ { "channel", laneCh }, { "notes", juce::Array<juce::var> { noteVar (0, 1, 80, false) } }, { "unit", "beats" } }));
        app.run ("playlist.addItem", args ({ { "ref", patternA }, { "track", 1 }, { "start", 0 } }));
        app.run ("playlist.addItem", args ({ { "ref", patternB }, { "track", 2 }, { "start", 0 } }));
        const auto lane1 = model.get().playlistTracks[0].id, lane2 = model.get().playlistTracks[1].id;

        // as notas de teste (50 no padrão A, na raia 1; 80 no B, na raia 2) que a música exporta no MIDI
        const auto midi = ctx.work.getChildFile ("raias.mid");
        auto songKeys = [&]
        {
            juce::StringArray keys;
            midi.deleteFile();
            app.run ("project.exportMidi", args ({ { "path", midi.getFullPathName() }, { "what", "song" } }));
            juce::MidiFile file;

            if (juce::FileInputStream in (midi); in.openedOk() && file.readFrom (in))
                for (int t = 0; t < file.getNumTracks(); ++t)
                    for (auto* e : *file.getTrack (t))
                        if (e->message.isNoteOn() && (e->message.getNoteNumber() == 50 || e->message.getNoteNumber() == 80))
                            keys.addIfNotAlreadyThere (juce::String (e->message.getNoteNumber()));

            keys.sort (false);
            return keys.joinIntoString (" ");
        };

        rep.check (songKeys() == "50 80", "as duas raias tocam: o MIDI da música tem a nota de cada uma ("_t + songKeys() + ")");

        app.run ("playlist.setTrack", args ({ { "track", lane2 }, { "muted", true } }));
        rep.check (songKeys() == "50" && ! model.get().laneSounds (lane2), "raia 2 muda: só a raia 1 sai no MIDI da música ("_t + songKeys() + ")");

        app.run ("playlist.setTrack", args ({ { "track", lane2 }, { "muted", false }, { "solo", true } }));
        rep.check (songKeys() == "80" && ! model.get().laneSounds (lane1), "raia 2 em solo: só ela toca ("_t + songKeys() + ")");

        app.run ("playlist.setTrack", args ({ { "track", lane2 }, { "muted", true } }));
        rep.check (songKeys().isEmpty(), "em solo e muda ao mesmo tempo: o mudo vence, e nada toca ("_t + songKeys() + ")");
        app.run ("playlist.setTrack", args ({ { "track", lane2 }, { "muted", false }, { "solo", false } }));

        const auto coloured = app.run ("playlist.setTrack", args ({ { "track", 1 }, { "color", "#5b8def" } }));
        rep.check (coloured.ok && model.get().playlistTracks[0].colour == "#5B8DEF"
                     && ! app.run ("playlist.setTrack", args ({ { "track", 1 }, { "color", "azul" } })).ok,
                   "a cor da raia: \"#5b8def\" fica \"#5B8DEF\"; \"azul\" é recusado"_t);

        // o .wnl: os dois escritores levam a cor, o mudo e o solo, e a leitura devolve
        app.run ("playlist.setTrack", args ({ { "track", lane2 }, { "muted", true } }));
        const auto json = wnl::toJson (model.get());
        Project back;
        rep.check (json == wnl::toJsonReference (model.get()) && json.contains ("\"colour\": \"#5B8DEF\"") && wnl::fromJson (json, back).wasOk()
                     && back.playlistTracks[0].colour == "#5B8DEF" && back.playlistTracks[1].muted && ! back.playlistTracks[1].solo,
                   ".wnl: a cor e o mudo da raia vão e voltam, iguais nos dois escritores"_t);
        app.run ("playlist.setTrack", args ({ { "track", lane2 }, { "muted", false } }));

        // a ordem: a raia 2 sobe para a primeira, e os blocos vão junto (eles guardam o id da raia)
        const auto before = wnl::toJson (model.get());
        const auto moved = app.run ("playlist.moveTrack", args ({ { "track", lane2 }, { "to", 1 } }));
        bool itemsKept = true;

        for (auto& i : model.get().playlistItems)
            itemsKept = itemsKept && (i.ref == patternA ? i.track == lane1 : i.track == lane2);

        rep.check (moved.ok && model.get().playlistTracks[0].id == lane2 && model.get().playlistTracks[1].id == lane1 && itemsKept
                     && songKeys() == "50 80",
                   "mover a raia 2 para a primeira: a ordem muda, os blocos vão junto e a música soa igual"_t);
        app.run ("edit.undo");
        rep.check (wnl::toJson (model.get()) == before, "um Ctrl+Z volta a ordem das raias"_t);

        // na tela: a faixa de cor, o ponto do mudo e o clique nele
        Playlist playlist (app);
        host.addAndMakeVisible (playlist);
        playlist.setBounds (host.getLocalBounds());
        playlist.refresh (model.get());

        if (auto* grid = childOf<PlaylistGrid> (playlist))
        {
            // o ponto fica na direita do cabeçalho (150 px), no meio da raia (54 px, depois da régua de 28 px)
            const juce::Point<float> ledLane2 { 150.0f - 17.0f, 28.0f + 54.0f + 27.0f };
            click (*grid, ledLane2);
            playlist.refresh (model.get());
            const bool mutedByClick = model.get().playlistTracks[1].muted;

            grid->mouseDown (mouseAt (*grid, ledLane2, juce::ModifierKeys::ctrlModifier));
            grid->mouseUp (mouseAt (*grid, ledLane2, juce::ModifierKeys::ctrlModifier));
            playlist.refresh (model.get());
            rep.check (mutedByClick && model.get().playlistTracks[1].solo, "na tela: clique no ponto da raia = mudo; Ctrl+clique = solo"_t);
            rep.check (savePng (playlist, ctx.work.getChildFile ("comunidade-raias.png")), "captura das raias com a cor, o mudo e o solo"_t);
        }
        else
        {
            rep.check (false, "a playlist montou sem a grade"_t);
        }

        host.removeChildComponent (&playlist);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: navegador de sons com BPM e tom (bloco 4b)"_t);
    {
        using soundinfo::BpmFilter;
        using soundinfo::KeyFilter;
        using soundinfo::KindFilter;

        // o tom pelo nome: o jeito dos produtores ("Am", "C#min", "F minor", "Lá menor"); letra sozinha e "Amen" não
        auto keyOf = [] (const char* name)
        {
            const auto k = soundinfo::keyFromName (juce::String::fromUTF8 (name));
            return k ? juce::String (k->first) + (k->second ? "M" : "m") : juce::String ("-");
        };

        const std::pair<const char*, const char*> nameCases[] {
            { "Loop Am 92bpm", "9m" }, { "Dark_C#min_140", "1m" }, { "F minor pad", "5m" }, { "Bbmaj7 chords", "10M" },
            { "E-minor guitar", "4m" }, { "Lá menor - piano", "9m" }, { "Sol# maior", "8M" }, { "F#m7 stab", "6m" },
            { "loop em Lá menor", "9m" }, { "Amen break", "-" }, { "Kick 01", "-" }, { "Take A 2", "-" }, { "Cm", "0m" },
            // a revisão de 23/10: a forma por extenso vence a curta, o "M" maiúsculo só num nome todo em maiúsculas
            { "Loop Em Lá Menor 90bpm", "9m" }, { "LOOP_AM_120", "9m" }, { "LOOP EM LA MENOR", "9m" }, { "Loop AM 120", "-" },
            { "FM Bass", "-" }, { "GM_Piano", "-" } };
        juce::StringArray wrong;

        for (auto& [name, expected] : nameCases)
            if (keyOf (name) != expected)
                wrong.add (juce::String::fromUTF8 (name) + " deu " + keyOf (name));

        rep.check (wrong.isEmpty(), "o tom pelo nome: 19 nomes (Am, C#min, Bbmaj7, F minor, E-minor, Lá menor, Sol# maior, LOOP_AM_120...; "_t
                                    "Amen, Kick 01, Take A, FM Bass e GM Piano não são tom; em \"Loop Em Lá Menor\", o Em é a preposição)"_t
                                    + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));

        // o que combina com Lá menor
        auto keyed = [] (int root, bool major, bool note = false)
        {
            SoundInfo i;
            i.keyRoot = root;
            i.keyMajor = major;
            i.isNote = note;
            return i;
        };

        {
            // os modos do projeto (a revisão de 23/10): Ré dórico tem as notas de Dó maior
            const auto dorian = Project::scaleDegrees ("dorian"), harmonic = Project::scaleDegrees ("harmonicMinor"),
                       penta = Project::scaleDegrees ("pentatonicMinor"), chromatic = Project::scaleDegrees ("chromatic");
            const bool modes = soundinfo::keyFits (keyed (0, true), 2, dorian, KeyFilter::project) && soundinfo::keyFits (keyed (9, false), 2, dorian, KeyFilter::project)
                               && ! soundinfo::keyFits (keyed (2, false), 2, dorian, KeyFilter::project)
                               && soundinfo::keyFits (keyed (11, false, true), 2, dorian, KeyFilter::project)
                               && ! soundinfo::keyFits (keyed (10, false, true), 2, dorian, KeyFilter::project)
                               && soundinfo::keyFits (keyed (9, false), 9, harmonic, KeyFilter::project) && soundinfo::keyFits (keyed (0, true), 9, harmonic, KeyFilter::project) && ! soundinfo::keyFits (keyed (4, false), 9, harmonic, KeyFilter::project)
                               && soundinfo::keyFits (keyed (9, false), 9, penta, KeyFilter::project) && soundinfo::keyFits (keyed (0, true), 9, penta, KeyFilter::project)
                               && ! soundinfo::keyFits (keyed (7, true), 9, penta, KeyFilter::project) && soundinfo::keyFits (keyed (7, true), 9, penta, KeyFilter::compatible)
                               && soundinfo::keyFits (keyed (6, true), 0, chromatic, KeyFilter::project);
            const auto same = soundinfo::keysThatFit (2, dorian, KeyFilter::project);
            rep.check (modes && same.size() == 2 && soundinfo::projectKeyText (2, "dorian") == "Ré dórico"_t,
                       "os modos do projeto: em Ré dórico, Dó maior e Lá menor (as mesmas notas) e o 808 em Si; Ré menor e o 808 em Si bemol não; "_t
                       "na menor harmônica, o tom menor da tônica e o relativo (Mi menor não); na pentatônica, o tom e o relativo (e os vizinhos que têm as 5 notas); "_t
                       "na cromática, todos ("_t + soundinfo::projectKeyText (2, "dorian") + ")");
        }

        rep.check (soundinfo::keyFits (keyed (9, false), 9, false, KeyFilter::project) && soundinfo::keyFits (keyed (0, true), 9, false, KeyFilter::project)
                     && ! soundinfo::keyFits (keyed (4, false), 9, false, KeyFilter::project) && soundinfo::keyFits (keyed (4, false), 9, false, KeyFilter::compatible)
                     && soundinfo::keyFits (keyed (2, false), 9, false, KeyFilter::compatible) && ! soundinfo::keyFits (keyed (6, false), 9, false, KeyFilter::compatible)
                     && soundinfo::keyFits (keyed (5, false, true), 9, false, KeyFilter::project) && ! soundinfo::keyFits (keyed (6, false, true), 9, false, KeyFilter::project)
                     && ! soundinfo::keyFits (SoundInfo(), 9, false, KeyFilter::project),
                   "no tom de Lá menor: Lá menor e Dó maior (o relativo); com os vizinhos, também Mi menor e Ré menor; Fá# menor não; "_t
                   "a nota Fá de um 808 cabe, a Fá# não"_t);
        rep.check (soundinfo::bpmFits (140, 140, BpmFilter::project) && soundinfo::bpmFits (70, 140, BpmFilter::project)
                     && soundinfo::bpmFits (280, 140, BpmFilter::project) && soundinfo::bpmFits (145, 140, BpmFilter::project)
                     && ! soundinfo::bpmFits (150, 140, BpmFilter::project) && ! soundinfo::bpmFits (0, 140, BpmFilter::project),
                   "BPM perto de 140: 140, 145, a metade (70) e o dobro (280); 150 e sem BPM não"_t);

        // os sons do teste: um loop de 2 compassos a 120 BPM (batidas e um acorde de Lá menor), um pad sem nome, um 808 em
        // Fá, um kick e um nome que já diz tudo
        const auto dir = ctx.work.getChildFile ("sons-4b");
        dir.deleteRecursively();
        dir.createDirectory();
        constexpr double rate = 44100.0;
        const double twoPi = juce::MathConstants<double>::twoPi;

        auto writeWav = [&] (const juce::String& name, double seconds, std::function<float (double t)> sample)
        {
            const auto f = dir.getChildFile (name + ".wav");
            const int length = (int) (seconds * rate);
            juce::AudioBuffer<float> buffer (1, length);

            for (int i = 0; i < length; ++i)
                buffer.setSample (0, i, sample (i / rate));

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (f);

            if (auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
                w->writeFromAudioSampleBuffer (buffer, 0, length);

            return f;
        };

        auto chord = [twoPi] (double t) { return 0.2 * std::sin (twoPi * 110.0 * t) + 0.1 * (std::sin (twoPi * 220.0 * t) + std::sin (twoPi * 261.63 * t) + std::sin (twoPi * 329.63 * t)); };
        juce::Random noise (7);

        const auto loopFile = writeWav ("Loop teste", 4.0, [&] (double t)
        {
            const double sinceBeat = std::fmod (t, 0.5);   // 120 BPM: uma batida a cada 0,5 s
            const double hit = sinceBeat < 0.012 ? 0.8 * (noise.nextFloat() * 2.0 - 1.0) * (1.0 - sinceBeat / 0.012) : 0.0;
            return (float) (chord (t) + hit);
        });
        const auto padFile = writeWav ("Pad sem nome", 8.5, [&] (double t) { return (float) (chord (t) * std::min (1.0, t / 0.3)); });
        const auto bassFile = writeWav ("808 longo", 1.6, [&] (double t) { return (float) (0.8 * std::sin (twoPi * 43.65 * t) * std::exp (-t * 1.5)); });
        const auto kickFile = writeWav ("Kick 01", 0.35, [&] (double t)
        {
            return (float) (0.9 * std::sin (twoPi * (55.0 + 120.0 * std::exp (-t * 40.0)) * t) * std::exp (-t * 12.0));
        });
        const auto trapFile = writeWav ("Trap 140bpm Cm", 2.0, [&] (double) { return (float) (0.3 * (noise.nextFloat() * 2.0 - 1.0)); });

        {
            SoundInfoCache cache (dir.getChildFile ("cache.json"));
            const auto loop = cache.waitFor (loopFile, 30000);
            const auto pad = cache.waitFor (padFile, 30000);
            const auto bass = cache.waitFor (bassFile, 30000);
            const auto kick = cache.waitFor (kickFile, 30000);
            const auto trap = cache.waitFor (trapFile, 30000);

            rep.check (loop.analysed && std::abs (loop.bpm - 120.0) < 1.0 && loop.bpmSource == "análise"_t && loop.isLoop()
                         && soundinfo::keyFits (loop, 9, false, KeyFilter::project),
                       "o loop de 2 compassos sem nada no nome: "_t + juce::String (loop.bpm, 1) + " BPM pela leitura (repetido até o "_t
                           "detector achar o pulso), tom " + loop.keyText() + " (cabe em Lá menor): \""_t + loop.tags() + "\"");
            rep.check (pad.analysed && ! pad.isLoop() && soundinfo::keyFits (pad, 9, false, KeyFilter::project),
                       "o pad sem ataques: sem BPM, tom "_t + pad.keyText() + " pela leitura");
            rep.check (bass.isNote && bass.keyRoot == 5 && ! bass.isLoop() && bass.tags() == "Fá"_t,
                       "o 808 de 1,6 s: one-shot, a nota "_t + bass.keyText() + " (Fá)"_t);
            rep.check (! kick.hasKey() && ! kick.isLoop() && kick.role == "kick" && kick.tags().isEmpty(),
                       "o kick: sem nota e sem BPM (bateria), papel "_t + kick.role);
            rep.check (trap.bpm == 140.0 && trap.bpmSource == "nome" && trap.keyRoot == 0 && ! trap.keyMajor && trap.keySource == "nome",
                       "o \"Trap 140bpm Cm\": 140 BPM e Dó menor pelo nome, sem trocar pela leitura: \""_t + trap.tags() + "\"");
        }

        {
            // o cache em disco: o mesmo arquivo, com o mesmo tamanho e a mesma data, não é lido de novo
            SoundInfoCache again (dir.getChildFile ("cache.json"));
            const auto before = again.find (loopFile);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto loop = again.waitFor (loopFile, 5000);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            rep.check (before.has_value() && before->analysed && std::abs (loop.bpm - 120.0) < 1.0 && ms < 400.0,
                       "o cache reabre do disco: o loop já vem lido, e conferir a data levou "_t + juce::String (ms, 1) + " ms");
        }

        // os comandos da IA, no tom e no BPM do projeto
        app.run ("project.new");
        app.run ("project.setKey", args ({ { "root", "A" }, { "scale", "minor" } }));
        app.run ("transport.setBpm", args ({ { "bpm", 120 } }));
        const auto info = app.run ("browser.info", args ({ { "path", loopFile.getFullPathName() } }));
        rep.check (info.ok && std::abs ((double) info.value["bpm"] - 120.0) < 1.0 && (bool) info.value["loop"] && info.value["key"].toString().isNotEmpty(),
                   "browser.info: "_t + juce::JSON::toString (info.value, true).substring (0, 160));

        for (auto& f : { padFile, bassFile, kickFile, trapFile })
            app.run ("browser.info", args ({ { "path", f.getFullPathName() } }));

        auto names = [] (const CommandResult& r)
        {
            juce::StringArray found;

            if (auto* list = r.value["sounds"].getArray())
                for (auto& sound : *list)
                    found.add (sound["name"].toString());

            found.sort (true);
            return found.joinIntoString (", ");
        };

        const auto loopsInKey = app.run ("browser.find", args ({ { "folder", dir.getFullPathName() }, { "key", "project" }, { "kind", "loop" } }));
        const auto nearBpm = app.run ("browser.find", args ({ { "folder", dir.getFullPathName() }, { "bpm", "project" } }));
        const auto compatible = app.run ("browser.find", args ({ { "folder", dir.getFullPathName() }, { "key", "compatible" } }));
        rep.check (loopsInKey.ok && names (loopsInKey) == "Loop teste" && (int) loopsInKey.value["pending"] == 0,
                   "browser.find { key: project, kind: loop } em Lá menor: só o loop ("_t + names (loopsInKey) + ")");
        rep.check (names (nearBpm) == "Loop teste", "browser.find { bpm: project } a 120 BPM: o loop; o Trap de 140 não ("_t + names (nearBpm) + ")");
        rep.check (names (compatible) == "808 longo, Loop teste, Pad sem nome",
                   "browser.find { key: compatible }: o loop, o pad e o 808 em Fá; o kick (sem nota) e o Trap em Dó menor não ("_t
                       + names (compatible) + ")");
        rep.check (! app.run ("browser.find", args ({ { "key", "qualquer" } })).ok, "browser.find com key errado: recusado"_t);

        // na tela: as etiquetas e o filtro (o índice do teste, sem as suas pastas de verdade)
        host.setSize (320, 420);
        SampleBrowser browser (app);
        host.addAndMakeVisible (browser);
        browser.setBounds (host.getLocalBounds());
        browser.setIndexForTest ({ loopFile, padFile, bassFile, kickFile, trapFile });
        browser.setFilters (KeyFilter::project, BpmFilter::any, KindFilter::loops);
        const bool onlyLoop = browser.rowOf (loopFile) >= 0 && browser.rowOf (padFile) < 0 && browser.rowOf (trapFile) < 0;

        browser.setFilters (KeyFilter::compatible, BpmFilter::any, KindFilter::any);
        const bool three = browser.rowOf (loopFile) >= 0 && browser.rowOf (padFile) >= 0 && browser.rowOf (bassFile) >= 0
                           && browser.rowOf (kickFile) < 0 && browser.rowOf (trapFile) < 0 && browser.isFiltering();
        rep.check (onlyLoop && three, "o filtro do navegador: os loops no tom do projeto (só o loop); os que combinam (o loop, o pad e o 808)"_t);
        rep.check (savePng (browser, ctx.work.getChildFile ("comunidade-sons-tom-bpm.png")), "captura do navegador com o filtro e as etiquetas"_t);

        browser.setFilters (KeyFilter::any, BpmFilter::any, KindFilter::any);   // a preferência do autoteste volta ao normal
        host.removeChildComponent (&browser);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: rack com 8 macros (bloco 4c)"_t);
    {
        app.run ("project.new");
        const auto track = app.run ("mixer.addTrack", args ({ { "name", "Rack" } })).value["track"].toString();
        const auto eq = app.run ("mixer.addInsert", args ({ { "track", track }, { "type", "eq" } })).value["insert"].toString();
        const auto sat = app.run ("mixer.addInsert", args ({ { "track", track }, { "type", "sat" } })).value["insert"].toString();

        auto valueOf = [&model] (const juce::String& trackId, const juce::String& insertId, const char* id) -> float
        {
            const auto* m = model.get().findMixer (trackId);
            const auto* ins = m != nullptr ? m->findInsert (insertId) : nullptr;
            const auto* info = ins != nullptr ? dsp::findEffect (ins->type.toStdString()) : nullptr;

            if (info == nullptr)
                return -999.0f;

            if (auto it = ins->params.find (id); it != ins->params.end())
                return it->second;

            const int i = info->indexOf (id);
            return i >= 0 ? info->params[(size_t) i].def : -999.0f;
        };
        auto param = [&] (const juce::String& insertId, const char* id) { return valueOf (track, insertId, id); };
        auto closeTo = [] (float a, float b, float tolerance) { return std::abs (a - b) <= tolerance; };
        auto macroArgs = [&track] (int macro, std::initializer_list<std::pair<const char*, juce::var>> more)
        {
            auto a = args ({ { "track", track }, { "macro", macro } });

            for (auto& [k, v] : more)
                a.getDynamicObject()->setProperty (k, v);

            return a;
        };

        // ligar: sem faixa, ela começa onde o parâmetro está (0 dB no agudo) e vai ao máximo; com faixa, vale na hora
        const auto mapped = app.run ("mixer.mapMacro", macroArgs (1, { { "insert", eq }, { "param", "highGain" } }));
        const auto mappedSat = app.run ("mixer.mapMacro", macroArgs (1, { { "insert", sat }, { "param", "drive" }, { "min", 0 }, { "max", 24 } }));
        rep.check (mapped.ok && mappedSat.ok && closeTo (param (eq, "highGain"), 0.0f, 0.001f) && closeTo (param (sat, "drive"), 0.0f, 0.001f),
                   "ligar ao macro: o agudo fica em 0 dB (a faixa padrão começa nele); o drive, com a faixa de 0 a 24 dB, vai a 0 na hora"_t);

        const auto half = app.run ("mixer.setMacro", macroArgs (1, { { "value", 0.5 } }));
        rep.check (half.ok && (int) half.value["changed"] == 2 && closeTo (param (eq, "highGain"), 9.0f, 0.01f) && closeTo (param (sat, "drive"), 12.0f, 0.01f),
                   "o macro 1 no meio: o agudo em +9 dB e o drive em 12 dB, os dois de uma vez ("_t
                       + juce::String (param (eq, "highGain"), 2) + " e " + juce::String (param (sat, "drive"), 2) + ")");

        app.run ("mixer.mapMacro", macroArgs (2, { { "insert", eq }, { "param", "highFreq" }, { "min", 2000 }, { "max", 16000 } }));
        app.run ("mixer.mapMacro", macroArgs (2, { { "insert", sat }, { "param", "mix" }, { "min", 100 }, { "max", 40 } }));
        app.run ("mixer.setMacro", macroArgs (2, { { "value", 0.5 } }));
        rep.check (closeTo (param (eq, "highFreq"), 5656.85f, 3.0f) && closeTo (param (sat, "mix"), 70.0f, 0.05f),
                   "o macro 2 no meio: a frequência anda no knob (de 2 a 16 kHz, o meio dá "_t + juce::String (param (eq, "highFreq"), 0)
                       + " Hz) e a faixa invertida desce a mistura (de 100% a 40%, no meio "_t + juce::String (param (sat, "mix"), 1) + "%)");

        app.run ("mixer.setMacro", macroArgs (1, { { "name", "Brilho" } }));
        const auto seen = app.run ("mixer.macros", args ({ { "track", track } }));
        const auto* list = seen.value["macros"].getArray();
        const bool listed = seen.ok && list != nullptr && list->size() == maxMacros && (*list)[0]["name"].toString() == "Brilho"
                            && (*list)[0]["links"].size() == 2 && (*list)[2]["name"].toString() == "Macro 3" && (*list)[2]["links"].size() == 0
                            && (*list)[1]["links"][0]["param"].toString() == "highFreq"
                            && closeTo ((float) (double) (*list)[1]["links"][0]["min"], 2000.0f, 1.0f)
                            && closeTo ((float) (double) (*list)[1]["links"][0]["now"], 5656.85f, 3.0f);
        rep.check (listed, "mixer.macros: os 8, com o nome (\"Brilho\", \"Macro 3\"), e cada parâmetro com o valor em 0, em 1 e agora"_t);

        const bool refused = ! app.run ("mixer.mapMacro", macroArgs (9, { { "insert", eq }, { "param", "highGain" } })).ok
                             && ! app.run ("mixer.mapMacro", macroArgs (1, { { "insert", eq }, { "param", "nada" } })).ok
                             && ! app.run ("mixer.setMacro", macroArgs (0, { { "value", 1 } })).ok
                             && ! app.run ("mixer.unmapMacro", macroArgs (3, {})).ok;
        rep.check (refused, "recusados: o macro 9 e o 0, um parâmetro que não existe e desligar um macro sem nada"_t);

        {
            const auto json = wnl::toJson (model.get());
            Project back;
            const bool read = wnl::fromJson (json, back).wasOk();
            const auto* m = back.findMixer (track);
            rep.check (json == wnl::toJsonReference (model.get()) && json.contains ("\"macros\"") && read && m != nullptr
                           && m->macros == model.get().findMixer (track)->macros,
                       "o .wnl: os dois escritores iguais, e o rack volta igual do arquivo"_t);
        }

        app.run ("mixer.removeInsert", args ({ { "track", track }, { "insert", eq } }));
        const auto* afterRemove = model.get().findMixer (track);
        const bool dropped = afterRemove != nullptr && afterRemove->macros.size() >= 2 && afterRemove->macros[0].links.size() == 1
                             && afterRemove->macros[1].links.size() == 1 && afterRemove->macros[0].links[0].insert == sat;
        app.run ("edit.undo");
        const auto* afterUndo = model.get().findMixer (track);
        rep.check (dropped && afterUndo != nullptr && afterUndo->macros[0].links.size() == 2 && closeTo (param (eq, "highGain"), 9.0f, 0.01f),
                   "tirar o EQ tira os links dele (cada macro fica com o da saturação); o Ctrl+Z volta o EQ e os links"_t);

        app.getCommands().beginGesture ("Mexer no macro"_t);

        for (double v : { 0.6, 0.7, 0.85, 1.0 })
            app.run ("mixer.setMacro", macroArgs (1, { { "value", v } }));

        app.getCommands().endGesture();
        const bool atTop = closeTo (param (eq, "highGain"), 18.0f, 0.01f) && closeTo (param (sat, "drive"), 24.0f, 0.01f);
        app.run ("edit.undo");
        rep.check (atTop && closeTo (param (eq, "highGain"), 9.0f, 0.01f) && closeTo (param (sat, "drive"), 12.0f, 0.01f),
                   "arrastar o macro até o fim leva o agudo a +18 dB e o drive a 24 dB; um Ctrl+Z volta ao meio (um passo só)"_t);

        // a cadeia salva leva o rack, ligado aos efeitos novos
        const juce::String chainName ("Rack do autoteste 4c");
        const auto saved = app.run ("mixer.saveChain", args ({ { "track", track }, { "name", chainName } }));
        const auto other = app.run ("mixer.addTrack", args ({ { "name", "Outra" } })).value["track"].toString();
        const auto loaded = app.run ("mixer.loadChain", args ({ { "track", other }, { "name", chainName } }));
        const auto* ot = model.get().findMixer (other);
        const bool chainOk = saved.ok && loaded.ok && (int) loaded.value["macros"] == 2 && ot != nullptr && ot->inserts.size() == 2
                             && ot->macros.size() >= 2 && ot->macros[0].name == "Brilho" && ot->macros[0].links.size() == 2
                             && ot->macros[0].links[0].insert == ot->inserts[0].id && ot->inserts[0].id != eq;
        rep.check (chainOk, "a cadeia salva leva o rack: na outra trilha, o \"Brilho\" move os efeitos dela (ids novos)"_t);

        if (chainOk)
        {
            const auto otherEq = ot->inserts[0].id;
            app.run ("mixer.setMacro", args ({ { "track", other }, { "macro", 1 }, { "value", 0.0 } }));
            rep.check (closeTo (valueOf (other, otherEq, "highGain"), 0.0f, 0.01f) && closeTo (param (eq, "highGain"), 9.0f, 0.01f),
                       "o macro da outra trilha a 0: o agudo dela volta a 0 dB, e o da primeira fica em +9 dB"_t);

            const auto appended = app.run ("mixer.loadChain", args ({ { "track", other }, { "name", chainName }, { "mode", "append" } }));
            const auto* oa = model.get().findMixer (other);
            rep.check (appended.ok && (int) appended.value["macros"] == 2 && oa != nullptr && oa->inserts.size() == 4 && oa->macros.size() >= 4
                           && oa->macros[2].name == "Brilho" && ! oa->macros[2].links.empty() && oa->macros[2].links[0].insert == oa->inserts[2].id
                           && oa->macros[0].links[0].insert == oa->inserts[0].id,
                       "somar a mesma cadeia: o rack dela vai para os macros 3 e 4 (o 1 e o 2 já movem alguma coisa)"_t);
        }

        app.run ("mixer.deleteChain", args ({ { "name", chainName } }));

        // a tela: o rack abre sozinho na trilha com macros, e o menu do macro liga um parâmetro
        InsertPanel panel (app);
        host.addAndMakeVisible (panel);
        panel.setTrack (track);
        panel.refresh (model.get());
        panel.setSize (InsertPanel::idealWidth, panel.getIdealHeight());
        const bool opened = panel.areMacrosOpen();

        std::vector<std::function<void()>> actions;
        const auto menu = panel.createMacroMenu (2, actions);
        int linkItem = 0, renameItem = 0;

        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        {
            if (it.getItem().text == "Ganho agudo" && linkItem == 0)
                linkItem = it.getItem().itemID;

            if (it.getItem().text == "Renomear...")
                renameItem = it.getItem().itemID;
        }

        if (linkItem > 0 && linkItem <= (int) actions.size())
            actions[(size_t) linkItem - 1]();

        const auto* linkedNow = model.get().findMixer (track);
        rep.check (opened && renameItem > 0 && linkedNow != nullptr && linkedNow->macros.size() >= 3 && linkedNow->macros[2].links.size() == 1
                       && linkedNow->macros[2].links[0].param == "highGain",
                   "na tela: o rack abre sozinho na trilha com macros, e o menu do macro 3 liga o Ganho agudo do EQ"_t);

        actions.clear();
        int ticked = 0;
        bool movesNow = false;

        const auto brilho = panel.createMacroMenu (0, actions);   // o iterador guarda uma referência: o menu precisa viver

        for (juce::PopupMenu::MenuItemIterator it (brilho, true); it.next();)
        {
            ticked += it.getItem().isTicked ? 1 : 0;
            movesNow |= it.getItem().isTicked && it.getItem().text.startsWith ("EQ") && it.getItem().text.contains ("Ganho agudo");
        }

        rep.check (ticked == 4 && movesNow, "o menu do Brilho marca o Ganho agudo e o Drive nas listas dos efeitos e mostra os dois no que ele move agora ("_t
                                                + juce::String (ticked) + " marcados)");

        panel.refresh (model.get());
        panel.setMacrosOpen (false);
        const int closedHeight = panel.getIdealHeight();
        panel.setMacrosOpen (true);
        rep.check (panel.getIdealHeight() - closedHeight == 2 * 58, "o título MACROS fecha e abre os 8 knobs (duas linhas de 4)"_t);

        panel.setSize (InsertPanel::idealWidth, panel.getIdealHeight());
        rep.check (savePngOnPanel (panel, ctx.work.getChildFile ("comunidade-macros.png")), "captura do painel de efeitos com o rack"_t);
        host.removeChildComponent (&panel);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: MIDI learn (bloco 4d)"_t);
    {
        app.run ("project.new");
        const auto track = app.run ("mixer.addTrack", args ({ { "name", "Controle" } })).value["track"].toString();
        const auto eq = app.run ("mixer.addInsert", args ({ { "track", track }, { "type", "eq" } })).value["insert"].toString();
        app.run ("mixer.mapMacro", args ({ { "track", track }, { "macro", 1 }, { "insert", eq }, { "param", "lowGain" }, { "min", -6 }, { "max", 6 } }));

        auto param = [&model, &track, &eq] (const char* id) -> float
        {
            const auto* m = model.get().findMixer (track);
            const auto* ins = m != nullptr ? m->findInsert (eq) : nullptr;
            const auto it = ins != nullptr ? ins->params.find (id) : std::map<juce::String, float>::const_iterator();
            return ins != nullptr && it != ins->params.end() ? it->second : 0.0f;
        };
        auto mixerTrack = [&model, &track] { return model.get().findMixer (track); };
        auto closeTo = [] (float a, float b, float tolerance) { return std::abs (a - b) <= tolerance; };
        const MidiLink highGain { 0, 0, "insert", track, eq, "highGain" };

        // aprender: o alvo arma; o primeiro CC que chega liga e não mexe no parâmetro
        const auto learn = app.run ("midi.learn", args ({ { "kind", "insert" }, { "track", track }, { "insert", eq }, { "param", "highGain" } }));
        const bool armed = learn.ok && app.isLearningMidi();
        app.handleMidiControl (1, 21, 100);
        const auto* bound = midilearn::linkFor (model.get(), highGain);
        rep.check (armed && ! app.isLearningMidi() && bound != nullptr && bound->cc == 21 && bound->channel == 1 && closeTo (param ("highGain"), 0.0f, 0.001f),
                   "aprender: o midi.learn arma, o CC 21 do canal 1 liga ao Ganho agudo do EQ, e o CC que ligou não mexe nele"_t);

        // mexer: a rajada move o agudo de -18 a +18 dB e vira um passo de desfazer só
        for (int v : { 0, 32, 64, 96, 127 })
            app.handleMidiControl (1, 21, v);

        app.finishMidiControlGesture();
        const bool top = closeTo (param ("highGain"), 18.0f, 0.01f);
        app.run ("edit.undo");
        rep.check (top && closeTo (param ("highGain"), 0.0f, 0.01f) && midilearn::linkFor (model.get(), highGain) != nullptr,
                   "o CC 21 leva o agudo até +18 dB no 127; um Ctrl+Z desfaz a rajada inteira (o link fica)"_t);

        // Ctrl+Z logo depois de girar (o gesto do controlador ainda aberto): ele fecha antes, e o desfazer tira a mexida
        app.handleMidiControl (1, 21, 127);
        app.run ("edit.undo");
        const bool undoneOpen = closeTo (param ("highGain"), 0.0f, 0.01f) && ! app.getCommands().isInGesture();
        app.run ("edit.redo");
        rep.check (undoneOpen && closeTo (param ("highGain"), 18.0f, 0.01f),
                   "Ctrl+Z com o knob acabado de girar: o passo do controlador fecha antes e sai; o refazer volta"_t);
        app.run ("edit.undo");

        app.handleMidiControl (2, 21, 127);   // outro canal MIDI: o link é do canal 1
        app.finishMidiControlGesture();
        rep.check (closeTo (param ("highGain"), 0.0f, 0.01f), "o mesmo CC no canal 2 não move nada (o link é do canal 1)"_t);

        // o volume, o pan e o macro
        const bool mapped = app.run ("midi.map", args ({ { "cc", 7 }, { "kind", "mixer" }, { "track", track }, { "param", "volume" } })).ok
                            && app.run ("midi.map", args ({ { "cc", 10 }, { "kind", "mixer" }, { "track", track }, { "param", "pan" } })).ok
                            && app.run ("midi.map", args ({ { "cc", 22 }, { "kind", "macro" }, { "track", track }, { "param", "1" } })).ok;
        app.handleMidiControl (1, 7, 0);
        const bool silent = mixerTrack() != nullptr && mixerTrack()->volumeDb <= -99.0f;
        app.handleMidiControl (1, 7, 127);
        const bool loud = mixerTrack() != nullptr && closeTo (mixerTrack()->volumeDb, 6.0f, 0.05f);
        app.handleMidiControl (1, 10, 64);
        const bool centre = mixerTrack() != nullptr && closeTo (mixerTrack()->pan, 0.0f, 0.001f);
        app.handleMidiControl (1, 10, 127);
        const bool right = mixerTrack() != nullptr && closeTo (mixerTrack()->pan, 1.0f, 0.001f);
        app.handleMidiControl (5, 22, 127);   // o link do midi.map sem canal vale em qualquer um
        app.finishMidiControlGesture();
        const bool macroMoved = mixerTrack() != nullptr && ! mixerTrack()->macros.empty() && closeTo (mixerTrack()->macros[0].value, 1.0f, 0.001f)
                                && closeTo (param ("lowGain"), 6.0f, 0.01f);
        rep.check (mapped && silent && loud && centre && right && macroMoved,
                   "o CC 7 no volume (0 = mudo, 127 = +6 dB), o CC 10 no pan (64 = centro) e o CC 22 no macro 1, que leva o grave a +6 dB"_t);

        // um CC move um alvo só, e um alvo tem um CC só
        app.run ("midi.map", args ({ { "cc", 21 }, { "kind", "mixer" }, { "track", track }, { "param", "pan" } }));
        const auto& links = model.get().midiMap;
        const bool single = links.size() == 3 && midilearn::linkFor (model.get(), highGain) == nullptr
                            && std::none_of (links.begin(), links.end(), [] (const MidiLink& l) { return l.cc == 10; });
        rep.check (single, "ligar o CC 21 ao pan tira o link do agudo (o CC 21) e o do pan antigo (o CC 10)"_t);

        {
            const auto json = wnl::toJson (model.get());
            Project back;
            rep.check (json == wnl::toJsonReference (model.get()) && json.contains ("\"midiMap\"") && wnl::fromJson (json, back).wasOk()
                           && back.midiMap == model.get().midiMap,
                       "o .wnl: os dois escritores iguais, e os controles ligados voltam iguais do arquivo"_t);
        }

        app.run ("mixer.removeTrack", args ({ { "track", track } }));
        const bool gone = model.get().midiMap.empty();
        app.run ("edit.undo");
        rep.check (gone && model.get().midiMap.size() == 3, "tirar a trilha tira os controles dela; o Ctrl+Z volta os dois"_t);

        const bool refused = ! app.run ("midi.learn", args ({ { "kind", "insert" }, { "track", track }, { "insert", eq }, { "param", "nada" } })).ok
                             && ! app.run ("midi.learn", args ({ { "kind", "mixer" }, { "track", track }, { "param", "eco" } })).ok
                             && ! app.run ("midi.map", args ({ { "cc", 200 }, { "kind", "mixer" }, { "track", track }, { "param", "volume" } })).ok
                             && ! app.run ("midi.map", args ({ { "cc", 3 }, { "kind", "macro" }, { "track", track }, { "param", "9" } })).ok
                             && ! app.run ("midi.unmap", args ({ { "cc", 99 } })).ok;
        rep.check (refused, "recusados: um parâmetro que não existe, o mixer sem volume ou pan, o CC 200, o macro 9 e esquecer o que não está ligado"_t);

        app.run ("midi.learn", args ({ { "kind", "mixer" }, { "track", track }, { "param", "volume" } }));
        const bool waiting = app.isLearningMidi();
        std::vector<std::function<void()>> actions;
        juce::PopupMenu menu;
        midilearn::addItems (menu, app, MidiLink { 0, 0, "mixer", track, {}, "pan" }, actions);
        bool showsCc = false;

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            showsCc |= it.getItem().text.startsWith ("Controlador: CC 21");

        const auto mappings = app.run ("midi.mappings");
        app.run ("midi.cancelLearn");
        rep.check (waiting && ! app.isLearningMidi() && showsCc && mappings.ok && mappings.value["mappings"].size() == 3,
                   "o menu do pan mostra o CC 21; midi.mappings lista os 3; midi.cancelLearn para o aprender"_t);

        const auto unmapped = app.run ("midi.unmap", args ({ { "kind", "mixer" }, { "track", track }, { "param", "pan" } }));
        rep.check (unmapped.ok && model.get().midiMap.size() == 2, "esquecer o controlador do pan (pelo alvo)"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: áudio para MIDI (bloco 4e)"_t);
    {
        app.run ("project.new");
        app.run ("transport.setBpm", args ({ { "bpm", 120 } }));
        const double rate = 44100.0, stepSeconds = 0.125;   // 1/16 a 120 BPM
        const auto dir = ctx.work.getChildFile ("audio-midi-4e");
        dir.createDirectory();

        auto writeWav = [&rate] (const juce::File& f, const std::vector<float>& x)
        {
            f.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (f);
            auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (16));

            if (writer == nullptr)
                return false;

            const float* chans[] { x.data() };
            return writer->writeFromFloatArrays (chans, 1, (int) x.size());
        };

        // o beatbox: kick (grave que cai), caixa (corpo em 250 Hz e ruído) e chimbal (ruído agudo e curto)
        const int kicks[] { 0, 6, 8 }, snares[] { 4, 12 }, hats[] { 2, 10, 14 };
        std::vector<float> beat ((size_t) (2.2 * rate), 0.0f);
        juce::Random noise (42);
        const double twoPi = juce::MathConstants<double>::twoPi;
        auto put = [&] (int step, double seconds, auto sample)
        {
            const auto at = (size_t) (step * stepSeconds * rate);

            for (size_t i = 0; i < (size_t) (seconds * rate) && at + i < beat.size(); ++i)
                beat[at + i] += (float) sample ((double) i / rate);
        };

        for (int st : kicks)
            put (st, 0.2, [&] (double t) { return 0.8 * std::exp (-t / 0.07) * std::sin (twoPi * (55.0 * t + 40.0 * 0.03 * (1.0 - std::exp (-t / 0.03)))); });

        for (int st : snares)
            put (st, 0.15, [&] (double t) { return std::exp (-t / 0.05) * (0.45 * std::sin (twoPi * 250.0 * t) + 0.25 * (noise.nextFloat() * 2.0f - 1.0f)); });

        double hp = 0.0, last = 0.0;

        for (int st : hats)
            put (st, 0.04, [&] (double t)
            {
                const double x = noise.nextFloat() * 2.0f - 1.0f;   // ruído menos ele mesmo suavizado: só o agudo
                hp = 0.75 * (hp + x - last);
                last = x;
                return 0.6 * std::exp (-t / 0.012) * hp;
            });

        const auto beatFile = dir.getChildFile ("beatbox.wav");
        const bool wroteBeat = writeWav (beatFile, beat);
        const auto drums = app.run ("audio.toMidi", args ({ { "path", beatFile.getFullPathName() }, { "mode", "drums" } }));
        const auto* pat = model.get().currentPatternPtr();

        auto stepsOf = [&model, pat] (const char* role)
        {
            juce::StringArray on;

            for (auto& ch : model.get().channels)
                if (pat != nullptr && roleFromName (ch.name) == (juce::String (role) == "kick" ? SampleRole::kick : juce::String (role) == "hat" ? SampleRole::hat : SampleRole::clap))
                    if (auto it = pat->steps.find (ch.id); it != pat->steps.end())
                        for (size_t i = 0; i < it->second.size(); ++i)
                            if (it->second[i].on)
                                on.add (juce::String ((int) i));

            return on.joinIntoString (" ");
        };

        const auto kickSteps = stepsOf ("kick"), clapSteps = stepsOf ("clap"), hatSteps = stepsOf ("hat");
        rep.check (wroteBeat && drums.ok && pat != nullptr && pat->id == drums.value["pattern"].toString() && kickSteps == "0 6 8" && clapSteps == "4 12"
                       && hatSteps == "2 10 14",
                   "o beatbox vira o kit num padrão novo: kick em "_t + kickSteps + ", palma (a caixa do kit) em " + clapSteps + ", chimbal em " + hatSteps
                       + (drums.ok ? juce::String() : " (" + drums.error + ")"));

        // a melodia: Lá 3, Dó 4, Mi 4 e Sol 4 (meio segundo, meio, um quarto e meio), com um respiro entre as notas
        const struct { int key; double start, length; } tune[] { { 57, 0.0, 0.48 }, { 60, 0.5, 0.48 }, { 64, 1.0, 0.23 }, { 67, 1.5, 0.48 } };
        std::vector<float> sung ((size_t) (2.2 * rate), 0.0f);

        for (auto& n : tune)
        {
            const double hz = 440.0 * std::pow (2.0, (n.key - 69) / 12.0);

            for (size_t i = 0; i < (size_t) (n.length * rate); ++i)
            {
                const double t = (double) i / rate, fade = juce::jmin (1.0, t / 0.01, (n.length - t) / 0.01);
                sung[(size_t) (n.start * rate) + i] = (float) (0.5 * fade * std::sin (twoPi * hz * t));
            }
        }

        const auto tuneFile = dir.getChildFile ("melodia.wav");
        const bool wroteTune = writeWav (tuneFile, sung);
        const auto synth = app.run ("channel.addSynth");
        const auto synthId = synth.value["channel"].toString();
        const auto melody = app.run ("audio.toMidi", args ({ { "path", tuneFile.getFullPathName() }, { "mode", "melody" }, { "channel", synthId } }));
        const auto notes = notesOf (model.get(), synthId);
        juce::StringArray got;

        for (auto& n : notes)
            got.add (juce::String (n.key) + "@" + juce::String (n.start / ticksPerStep) + "+" + juce::String (n.length / ticksPerStep));

        rep.check (wroteTune && melody.ok && got.joinIntoString (" ") == "57@0+4 60@4+4 64@8+2 67@12+4",
                   "a melodia vira notas no synth, encaixadas no 1/16 (nota@passo+passos): "_t + got.joinIntoString (" ")
                       + (melody.ok ? juce::String() : " (" + melody.error + ")"));

        const bool refused = ! app.run ("audio.toMidi", args ({ { "path", tuneFile.getFullPathName() }, { "mode", "baixo" } })).ok
                             && ! app.run ("audio.toMidi", args ({ { "path", "relativo.wav" }, { "mode", "drums" } })).ok
                             && ! app.run ("audio.toMidi", args ({ { "mode", "drums" } })).ok;
        const int before = (int) model.get().patterns.size();
        app.run ("edit.undo");
        rep.check (refused && (int) model.get().patterns.size() == before - 1,
                   "recusados: o modo errado, o caminho incompleto e sem áudio; o Ctrl+Z tira o padrão da melodia"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Comunidade: pads com camadas por força (bloco 4f)"_t);
    {
        app.run ("project.new");
        const double rate = 44100.0;
        const auto dir = ctx.work.getChildFile ("camadas-4f");
        dir.createDirectory();

        auto sineWav = [&] (const juce::String& name, double hz)
        {
            const auto f = dir.getChildFile (name + ".wav");
            f.deleteFile();
            juce::AudioBuffer<float> buffer (1, (int) rate);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (f);

            if (auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
                w->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());

            return f;
        };

        const auto soft = sineWav ("Pad suave", 220.0), hard = sineWav ("Pad forte", 440.0), harder = sineWav ("Pad muito forte", 880.0);
        const auto added = app.run ("channel.addSampler", args ({ { "path", soft.getFullPathName() }, { "name", "Pad" } }));
        const auto pad = added.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));

        const auto layered = app.run ("channel.addLayer", args ({ { "channel", pad }, { "path", hard.getFullPathName() } }));
        const auto* padCh = model.get().findChannel (pad);
        rep.check (added.ok && layered.ok && padCh != nullptr && padCh->layers.size() == 1 && padCh->layers[0].minVelocity == 100,
                   "uma camada no pad: o sample forte a partir da força 100 (o padrão da primeira)"_t);

        int renders = 0;
        auto hzAt = [&] (int velocity)
        {
            setGrid (app, pad, "x...............", velocity);
            const auto f = dir.getChildFile ("camadas-" + juce::String (++renders) + ".wav");
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            return measureHz (readWav (f), 0.05, 0.5);
        };

        const double softHz = hzAt (60), hardHz = hzAt (120), edgeHz = hzAt (100), belowHz = hzAt (99);
        rep.check (std::abs (softHz - 220.0) < 2.0 && std::abs (hardHz - 440.0) < 3.0 && std::abs (edgeHz - 440.0) < 3.0 && std::abs (belowHz - 220.0) < 2.0,
                   "a força escolhe o sample: 60 toca o suave ("_t + juce::String (softHz, 1) + " Hz), 120 e 100 o forte ("
                       + juce::String (hardHz, 1) + " e " + juce::String (edgeHz, 1) + " Hz), 99 o suave (" + juce::String (belowHz, 1) + " Hz)");

        app.run ("channel.addLayer", args ({ { "channel", pad }, { "path", harder.getFullPathName() }, { "minVelocity", 120 } }));
        const double topHz = hzAt (127), middleHz = hzAt (110);
        rep.check (std::abs (topHz - 880.0) < 5.0 && std::abs (middleHz - 440.0) < 3.0,
                   "três forças: 127 toca o muito forte ("_t + juce::String (topHz, 1) + " Hz), 110 o forte (" + juce::String (middleHz, 1) + " Hz)");

        {
            const auto json = wnl::toJson (model.get());
            Project back;
            const bool read = wnl::fromJson (json, back).wasOk();
            const auto* backCh = back.findChannel (pad);
            rep.check (json == wnl::toJsonReference (model.get()) && json.contains ("\"layers\"") && read && backCh != nullptr
                           && backCh->layers == model.get().findChannel (pad)->layers,
                       "o .wnl: os dois escritores iguais, e as camadas voltam iguais do arquivo"_t);

            // salvar coletando: as camadas vão para samples/, como o sample do canal
            auto copy = model.get();
            const auto folder = dir.getChildFile ("projeto");
            folder.deleteRecursively();
            const auto file = folder.getChildFile ("Camadas.wnl");
            const bool saved = wnl::save (copy, file, true).wasOk();
            const auto* savedCh = copy.findChannel (pad);
            rep.check (saved && savedCh != nullptr && savedCh->layers.size() == 2 && savedCh->layers[0].samplePath.startsWith ("samples/")
                           && folder.getChildFile ("samples").getChildFile ("Pad forte.wav").existsAsFile()
                           && folder.getChildFile ("samples").getChildFile ("Pad muito forte.wav").existsAsFile(),
                       "salvar com os samples copia as camadas para samples/ (\""_t + (savedCh != nullptr && ! savedCh->layers.empty() ? savedCh->layers[0].samplePath : juce::String()) + "\")");
        }

        const auto audioCh = app.run ("channel.addAudio", args ({ { "path", soft.getFullPathName() } })).value["channel"].toString();
        const bool refused = ! app.run ("channel.addLayer", args ({ { "channel", pad }, { "path", soft.getFullPathName() }, { "minVelocity", 100 } })).ok
                             && ! app.run ("channel.addLayer", args ({ { "channel", pad }, { "path", dir.getChildFile ("nao-existe.wav").getFullPathName() } })).ok
                             && ! app.run ("channel.addLayer", args ({ { "channel", audioCh }, { "path", hard.getFullPathName() } })).ok
                             && ! app.run ("channel.setLayer", args ({ { "channel", pad }, { "layer", 2 }, { "minVelocity", 100 } })).ok
                             && ! app.run ("channel.removeLayer", args ({ { "channel", pad }, { "layer", 3 } })).ok;
        rep.check (refused, "recusados: a mesma força de outra camada, o arquivo que não existe, um canal de áudio e a camada 3 que não há"_t);

        // a tela: a linha das camadas no painel do sampler
        SamplerPanel panel (app, pad);
        host.addAndMakeVisible (panel);
        const auto labels = panel.layerLabels();
        rep.check (labels.size() == 3 && labels[0].contains ("1 a 99") && labels[1].contains ("100 a 119") && labels[2].contains ("120+"),
                   "o painel do sampler mostra as faixas de força: "_t + labels.joinIntoString (" | "));
        rep.check (savePngOnPanel (panel, ctx.work.getChildFile ("comunidade-camadas.png")), "captura do painel do sampler com as camadas"_t);
        host.removeChildComponent (&panel);

        const auto removed = app.run ("channel.removeLayer", args ({ { "channel", pad }, { "layer", 1 } }));
        const auto* after = model.get().findChannel (pad);
        const size_t left = after != nullptr ? after->layers.size() : 99;   // antes do Ctrl+Z: ele troca o projeto inteiro
        app.run ("edit.undo");
        const auto* back = model.get().findChannel (pad);
        rep.check (removed.ok && left == 1 && back != nullptr && back->layers.size() == 2, "tirar uma camada e o Ctrl+Z devolve"_t);

        const auto sliced = app.run ("channel.slice", args ({ { "channel", pad }, { "mode", "grid" }, { "division", "1/4" } }));
        const auto* slicedCh = model.get().findChannel (pad);
        const size_t afterSlice = slicedCh != nullptr ? slicedCh->layers.size() : 99;
        app.run ("edit.undo");
        const auto* unsliced = model.get().findChannel (pad);
        rep.check (! sliced.ok || (afterSlice == 0 && unsliced != nullptr && unsliced->layers.size() == 2),
                   "fatiar o canal tira as camadas (fatiado, cada tecla é uma fatia); o Ctrl+Z devolve"_t
                       + (sliced.ok ? juce::String() : " (o fatiar recusou: " + sliced.error + ")"));
    }

    //--------------------------------------------------------------------------
    rep.section ("Pedidos de 26/10: raias sem teto e o botão direito dos controles"_t);
    {
        app.run ("project.new");
        const auto& lanes = model.get().playlistTracks;
        const bool twelve = lanes.size() == 12;
        app.run ("playlist.addItem", args ({ { "track", 12 }, { "start", 0 } }));
        const bool grew = lanes.size() == 13 && lanes.back().name == "Raia 13";
        const auto added = app.run ("playlist.addTrack", args ({ { "count", 50 } }));
        const bool fifty = added.ok && lanes.size() == 63;
        app.run ("playlist.addItem", args ({ { "track", 63 }, { "start", 0 } }));
        const bool grewAgain = lanes.size() == 64;
        rep.check (twelve && grew && fifty && grewAgain,
                   "a playlist cresce: 12 raias no projeto novo, a 13ª aparece quando a 12ª ganha um bloco, +50 pelo comando, e de novo no fim ("_t
                       + juce::String ((int) lanes.size()) + " raias)");

        const auto inserted = app.run ("playlist.addTrack", args ({ { "at", 1 }, { "name", "Bateria" } }));
        const bool first = inserted.ok && lanes.front().name == "Bateria";
        const auto removed = app.run ("playlist.removeTrack", args ({ { "track", 64 } }));   // a que tem o bloco
        const bool gone = removed.ok && (int) removed.value["itemsRemoved"] == 1;
        app.run ("edit.undo");
        rep.check (first && gone && model.get().playlistItems.size() == 2,
                   "inserir uma raia no topo e apagar uma raia com o bloco dela; o Ctrl+Z devolve o bloco"_t);

        const auto many = app.run ("playlist.addTrack", args ({ { "count", 100 } }));
        rep.check (many.ok && ! app.run ("playlist.addTrack", args ({ { "count", 100 } })).ok == ((int) lanes.size() + 100 > maxPlaylistTracks),
                   "o teto é só uma proteção do arquivo: "_t + juce::String (maxPlaylistTracks) + " raias (agora "
                       + juce::String ((int) lanes.size()) + ")");

        // o botão direito: automatizar o volume de uma trilha e um knob de efeito, já no valor de agora
        const auto track = app.run ("mixer.addTrack", args ({ { "name", "Auto" } })).value["track"].toString();
        app.run ("mixer.setVolume", args ({ { "track", track }, { "db", -6.0 } }));
        const auto eq = app.run ("mixer.addInsert", args ({ { "track", track }, { "type", "eq" } })).value["insert"].toString();
        app.run ("mixer.setInsertParam", args ({ { "track", track }, { "insert", eq }, { "param", "highGain" }, { "value", 9.0 } }));

        const AutomationTarget volume { "mixer", track, {}, {}, "volume" }, high { "insert", track, {}, eq, "highGain" };
        const auto volumeLane = controlmenu::automate (app, volume);
        const auto highLane = controlmenu::automate (app, high);
        const auto* vl = model.get().findLane (volumeLane);
        const auto* hl = model.get().findLane (highLane);
        const bool seeded = vl != nullptr && hl != nullptr && vl->points.size() == 1 && hl->points.size() == 1
                            && std::abs (hl->points[0].v - 0.75f) < 0.001f
                            && std::abs (vl->points[0].v - controlmenu::currentPosition (model.get(), volume)) < 0.001f;
        const auto again = controlmenu::automate (app, high);
        rep.check (seeded && again == highLane && model.get().automation.size() == 2,
                   "Automatizar cria a curva com um ponto no valor de agora (o agudo em +9 dB fica em 0,75 do knob); de novo, a mesma curva"_t);

        std::vector<std::function<void()>> actions;
        juce::PopupMenu menu;
        controlmenu::addAutomation (menu, app, high, actions);
        bool offersRemove = false;

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            offersRemove |= it.getItem().text.startsWith ("Apagar a automação"_t);

        if (! actions.empty())
            actions.front()();

        rep.check (offersRemove && model.get().automation.size() == 1, "com a curva criada, o menu oferece apagar a automação, e apaga"_t);

        // voltar ao padrão num knob qualquer (o ParamControl do sampler, do synth e dos efeitos)
        const auto& info = dsp::findEffect ("eq")->params[(size_t) dsp::findEffect ("eq")->indexOf ("highGain")];
        ParamControl control (info);
        float sent = -999.0f;
        int gestures = 0;
        control.onChange = [&sent] (float v) { sent = v; };
        control.onGestureStart = [&gestures] { ++gestures; };
        control.onGestureEnd = [&gestures] { ++gestures; };
        control.setValue (9.0f);
        control.resetToDefault();
        rep.check (sent == info.def && control.getValue() == info.def && gestures == 2 && control.resetLabel().contains ("Voltar ao padrão"_t),
                   "Voltar ao padrão leva o knob ao valor de fábrica num gesto só: \""_t + control.resetLabel() + "\"");
    }

    //--------------------------------------------------------------------------
    rep.section ("Pedidos de 26/10: o WHYNOT Synth 3 OSC"_t);
    {
        app.run ("project.new");
        juce::StringArray ids;

        for (auto& preset : synth::allPresets())
            if (juce::String (preset.id).startsWith ("3osc"))
                ids.add (juce::String (preset.id));

        rep.check (ids.size() == 6, "os presets 3 OSC: "_t + ids.joinIntoString (", "));

        // o sub: um seno no Lá 2 (110 Hz), renderizado sozinho
        const auto added = app.run ("channel.addSynth", args ({ { "preset", "3osc-sub" } }));
        const auto synthCh = added.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));
        addNotes (app, synthCh, juce::Array<juce::var> { noteVar (0, 2, 45, false) });
        const auto f = ctx.work.getChildFile ("3osc-sub.wav");
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        const auto audio = readWav (f);
        const double hz = measureHz (audio, 0.2, 0.8);
        rep.check (added.ok && std::abs (hz - 110.0) < 1.0 && peakDb (audio) > -30.0f,
                   "o 3 OSC Sub toca o Lá 2 em "_t + juce::String (hz, 2) + " Hz (110), pico " + juce::String (peakDb (audio), 1) + " dB");

        // o editor abre na página 3 OSC para um timbre da família
        if (auto* plugin = app.getProjector().synthFor (synthCh))
        {
            SynthEditor editor (app, *plugin);
            host.addAndMakeVisible (editor);
            rep.check (editor.getPage() == SynthEditor::simplePage, "o editor abre na página 3 OSC para um timbre 3 OSC"_t);
            rep.check (savePngOnPanel (editor, ctx.work.getChildFile ("synth-3osc.png")), "captura da página 3 OSC"_t);
            host.removeChildComponent (&editor);
        }
        else
        {
            rep.check (false, "o synth do canal não abriu"_t);
        }
    }

    //--------------------------------------------------------------------------
    rep.section ("Pedidos de 26/10: passos mais ricos no rack (chance, tom e pan)"_t);
    {
        app.run ("project.new");
        const auto dir = ctx.work.getChildFile ("passos");
        dir.createDirectory();
        const double rate = 48000.0;
        const auto tone = dir.getChildFile ("Tom 220.wav");
        tone.deleteFile();

        {
            juce::AudioBuffer<float> buffer (1, (int) (0.4 * rate));   // curto: um passo não vaza no seguinte

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / rate));

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (tone);

            if (auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
                w->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }

        const auto added = app.run ("channel.addSampler", args ({ { "path", tone.getFullPathName() }, { "name", "Tom" } }));
        const auto stepCh = added.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));
        setGrid (app, stepCh, "x...............", 100);

        int renders = 0;
        auto render = [&]
        {
            const auto f = dir.getChildFile ("passo-" + juce::String (++renders) + ".wav");
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            return readWav (f);
        };
        auto channelRms = [] (const Audio& a, int c)
        {
            double acc = 0.0;
            const int n = juce::jmin (a.buffer.getNumSamples(), (int) (0.3 * a.sampleRate));

            for (int i = 0; i < n; ++i)
                acc += (double) a.buffer.getSample (c, i) * a.buffer.getSample (c, i);

            return std::sqrt (acc / juce::jmax (1, n));
        };

        // Tom: +12 dobra a frequência (220 → 440 Hz); -12 divide.
        const auto up = app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 0 }, { "pitch", 12 } }));
        const double hzUp = measureHz (render(), 0.05, 0.3);
        app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 0 }, { "pitch", -12 } }));
        const double hzDown = measureHz (render(), 0.05, 0.3);
        rep.check (up.ok && std::abs (hzUp - 440.0) < 3.0 && std::abs (hzDown - 110.0) < 1.5,
                   "o tom do passo: +12 toca "_t + juce::String (hzUp, 1) + " Hz e -12 toca " + juce::String (hzDown, 1) + " Hz (o sample é 220)"_t);

        // Pan: toda à esquerda cala a direita; no centro os dois lados saem iguais (bit a bit).
        app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 0 }, { "pitch", 0 }, { "pan", -100 } }));
        const auto left = render();
        app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 0 }, { "pan", 0 } }));
        const auto centre = render();
        const double lL = channelRms (left, 0), lR = channelRms (left, 1);
        const bool sameSides = centre.valid && centre.buffer.getNumChannels() == 2 && channelRms (centre, 0) > 0.01
                               && channelRms (centre, 0) == channelRms (centre, 1);
        rep.check (left.valid && lL > 0.01 && lR < lL * 1.0e-3 && sameSides,
                   "o pan do passo: toda à esquerda, a direita fica "_t + juce::String (juce::Decibels::gainToDecibels (lR / lL, -200.0), 0)
                       + " dB abaixo; no centro, os dois lados iguais");

        // Pan só no passo dele: o passo 2 no centro, depois de um passo à direita, não herda o lado.
        setGrid (app, stepCh, "x.......x.......", 100);
        app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 0 }, { "pan", 100 } }));
        const auto two = render();
        double secondL = 0.0, secondR = 0.0;

        for (int i = (int) (2.0 * 60.0 / model.get().bpm * two.sampleRate) + 100; i < two.buffer.getNumSamples(); ++i)
        {
            secondL += std::abs (two.buffer.getSample (0, i));
            secondR += std::abs (two.buffer.getSample (1, i));
        }

        rep.check (two.valid && secondL > 0.0 && std::abs (secondL - secondR) < 1.0e-3 * secondL,
                   "o pan vale só para o passo dele: o passo seguinte, no centro, sai igual nos dois lados"_t);

        // Toda à direita cala a esquerda.
        setGrid (app, stepCh, "x...............", 100);
        app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 0 }, { "pan", 100 } }));
        const auto right = render();
        rep.check (right.valid && channelRms (right, 1) > 0.01 && channelRms (right, 0) < channelRms (right, 1) * 1.0e-3,
                   "toda à direita, a esquerda fica calada"_t);

        // O centro de um canal que usa pan sai bit a bit igual ao de um canal sem pan nenhum.
        setGrid (app, stepCh, "x.......x.......", 100);
        const auto plain = render();
        app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 8 }, { "pan", -100 } }));
        const auto mixed = render();
        float worst = 0.0f;
        const int firstStep = (int) (0.4 * rate);

        for (int c = 0; c < 2 && plain.valid && mixed.valid; ++c)
            for (int i = 0; i < juce::jmin (firstStep, plain.buffer.getNumSamples(), mixed.buffer.getNumSamples()); ++i)
                worst = juce::jmax (worst, std::abs (plain.buffer.getSample (c, i) - mixed.buffer.getSample (c, i)));

        rep.check (plain.valid && mixed.valid && worst == 0.0f,
                   "o passo no centro sai bit a bit igual com e sem pan nos outros passos (diferença "_t + juce::String (worst) + ")");

        // Duas notas no mesmo instante: o passo (+12, toda à esquerda) e uma nota do piano roll (220 Hz, centro).
        // A direita só pode ter a nota do piano roll.
        setGrid (app, stepCh, "x...............", 100);
        app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 0 }, { "pitch", 12 }, { "pan", -100 } }));
        addNotes (app, stepCh, juce::Array<juce::var> { noteVar (0, 4, model.get().findChannel (stepCh)->rootKey, false) });
        const auto both = render();
        Audio rightOnly;
        rightOnly.sampleRate = both.sampleRate;
        rightOnly.valid = both.valid;
        rightOnly.buffer.setSize (1, both.buffer.getNumSamples());

        if (both.valid)
            rightOnly.buffer.copyFrom (0, 0, both.buffer, 1, 0, both.buffer.getNumSamples());

        const double rightHz = measureHz (rightOnly, 0.05, 0.3);
        rep.check (both.valid && std::abs (rightHz - 220.0) < 3.0,
                   "passo e nota no mesmo instante: cada um com o seu pan (a direita toca só a nota do piano roll, "_t
                       + juce::String (rightHz, 1) + " Hz)");
        app.run ("pianoroll.clear", args ({ { "channel", stepCh } }));

        // Chance: 50% em 16 passos, 8 voltas. O sorteio é fixo (sai igual de novo) e muda de volta em volta.
        setGrid (app, stepCh, "xxxxxxxxxxxxxxxx", 100);
        app.getCommands().beginGesture ("Chance");

        for (int i = 0; i < 16; ++i)
            app.run ("steps.set", args ({ { "channel", stepCh }, { "index", i }, { "chance", 50 } }));

        app.getCommands().endGesture();
        const auto& p = model.get();
        const auto* pat = p.currentPatternPtr();
        const auto* chp = p.findChannel (stepCh);
        std::vector<Note> a, b, other;

        if (pat != nullptr && chp != nullptr)
        {
            a = Projector::notesFor (p, *pat, *chp, pat->lengthTicks() * 8);
            b = Projector::notesFor (p, *pat, *chp, pat->lengthTicks() * 8);
            other = Projector::notesFor (p, *pat, *chp, pat->lengthTicks() * 8, 0, 3840);
        }

        auto loopSet = [&] (const std::vector<Note>& notes, int loop)
        {
            juce::String sig;

            for (auto& n : notes)
                if (n.start / pat->lengthTicks() == loop)
                    sig << (n.start % pat->lengthTicks()) / ticksPerStep << ",";

            return sig;
        };

        const bool varies = pat != nullptr && (loopSet (a, 0) != loopSet (a, 1) || loopSet (a, 1) != loopSet (a, 2));
        rep.check (a.size() >= 40 && a.size() <= 88 && a == b && a != other && varies,
                   "chance de 50%: "_t + juce::String ((int) a.size()) + " de 128 notas em 8 voltas, o mesmo sorteio de novo, outro bloco sorteia diferente e cada volta muda"_t);

        // No 808 (canal que respeita a duração), o passo que não saiu não corta o anterior.
        if (pat != nullptr && chp != nullptr)
        {
            auto held = *chp;
            held.oneShot = false;
            Pattern half = *pat;
            auto& lane = half.steps[stepCh];
            std::fill (lane.begin(), lane.end(), Step{});
            lane[0].on = lane[8].on = true;
            lane[8].chance = 30;
            const auto notes808 = Projector::notesFor (p, half, held, half.lengthTicks() * 8);
            bool ok = true;
            int alone = 0;

            for (auto& n : notes808)
                if (n.start % half.lengthTicks() == 0)
                {
                    const bool secondPlays = std::any_of (notes808.begin(), notes808.end(),
                                                          [&] (const Note& m) { return m.start == n.start + 8 * ticksPerStep; });
                    ok &= n.length == (secondPlays ? 8 * ticksPerStep : half.lengthTicks());
                    alone += secondPlays ? 0 : 1;
                }

            rep.check (ok && alone > 0,
                       "808 com chance: quando o passo 9 não sai, o passo 1 segura até o fim do padrão ("_t + juce::String (alone) + " de 8 voltas)");
        }

        // No PAT ao vivo (com o loop ligado) o laço vira 8 voltas, para cada volta poder sair diferente.
        app.getProjector().sync (model.get(), model.projectFolder());   // o que o Housekeeping do app faz quando vence
        const double patternSeconds = pat != nullptr ? pat->lengthTicks() / (double) ticksPerBeat * 60.0 / p.bpm : 0.0;
        rep.check (std::abs (app.getProjector().lengthSeconds() - 8.0 * patternSeconds) < 1.0e-3,
                   "no PAT, um padrão com chance toca 8 voltas antes de repetir: "_t + juce::String (app.getProjector().lengthSeconds(), 2) + " s"
                       + " (o padrão tem "_t + juce::String (patternSeconds, 2) + " s)");

        // .wnl: os três campos vão e voltam, iguais nos dois escritores.
        app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 3 }, { "chance", 25 }, { "pitch", 7 }, { "pan", -40 } }));
        const auto json = wnl::toJson (model.get());
        Project back;
        const auto* backPat = wnl::fromJson (json, back).wasOk() ? back.currentPatternPtr() : nullptr;
        const Step* s3 = backPat != nullptr && backPat->steps.count (stepCh) > 0 ? &backPat->steps.at (stepCh)[3] : nullptr;
        rep.check (s3 != nullptr && s3->chance == 25 && s3->pitch == 7 && s3->pan == -40 && json == wnl::toJsonReference (model.get()),
                   "chance, tom e pan vão e voltam no .wnl, iguais nos dois escritores"_t);

        // Passos para o piano roll: o tom vai junto.
        setGrid (app, stepCh, "x...............", 100);
        app.run ("steps.set", args ({ { "channel", stepCh }, { "index", 0 }, { "pitch", 5 } }));
        const auto converted = app.run ("pianoroll.stepsToNotes", args ({ { "channel", stepCh } }));
        const auto* notes = model.get().currentPatternPtr()->notesFor (stepCh);
        const auto* chNow = model.get().findChannel (stepCh);
        rep.check (converted.ok && (notes != nullptr && ! notes->empty() && chNow != nullptr && notes->front().key == chNow->rootKey + 5),
                   "passos para o piano roll levam o tom do passo"_t);

        // A tela: o rack com as marquinhas (tom, chance e pan), num canal que ainda usa passos (o Tom virou notas acima).
        juce::String rackCh = stepCh;

        for (auto& c : model.get().channels)
            if (c.name == "Kick")
                rackCh = c.id;

        setGrid (app, rackCh, "x...x...x...x...", 100);
        app.run ("steps.set", args ({ { "channel", rackCh }, { "index", 0 }, { "pitch", 7 } }));
        app.run ("steps.set", args ({ { "channel", rackCh }, { "index", 4 }, { "chance", 50 } }));
        app.run ("steps.set", args ({ { "channel", rackCh }, { "index", 8 }, { "pan", -100 } }));
        app.run ("steps.set", args ({ { "channel", rackCh }, { "index", 12 }, { "pitch", -12 }, { "chance", 75 }, { "pan", 60 } }));
        ChannelRack rack (app);
        rack.setSize (1180, 420);
        host.addAndMakeVisible (rack);
        rack.refresh (model.get());
        rep.check (savePngOnPanel (rack, ctx.work.getChildFile ("passos-rack.png")), "captura do rack com chance, tom e pan nos passos"_t);
        host.removeChildComponent (&rack);
    }

    host.setLookAndFeel (nullptr);
    app.run ("project.new");
}

} // namespace whynot::selftest
