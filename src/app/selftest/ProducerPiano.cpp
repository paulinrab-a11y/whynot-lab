// Autoteste dos pedidos do produtor no piano roll (vídeos de 22/10). O importar MIDI, que completa o do bloco 1:
// - a leitura (engine/MidiImport) e o pianoroll.inspectMidi: tipo 0 aberto por canal MIDI, tipo 1 com o maestro e
//   trilha vazia, SMPTE, a nota de força zero (que é o fim da nota anterior) e notas iguais em duas trilhas;
// - o comando com track, at, trocar e somar, o teto do padrão e o desfazer num passo só;
// - o cartão de opções no piano roll (soltar, Esc, escolher a trilha e somar, a agulha) e a ação do menu Arquivo.
// E os outros três: Ctrl+V em outro canal com o Ctrl+V chegando pela janela, Ctrl+cima/baixo na oitava e as linhas
// da grade com a cor das teclas, com a escala num fio na beira.
// As telas ficam num pai fora da área de trabalho (sem roubar o foco), com as capturas importar-midi-cartao.png e
// piano-linhas-das-teclas.png.

#include "SelfTestKit.h"
#include "../../engine/MidiImport.h"
#include "../../ui/MidiImportDialog.h"
#include "../../ui/PianoRoll.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    using Track = juce::MidiMessageSequence;

    /** Uma nota com o note-off normal, ou com o note-on de força 0 que muitos programas gravam no lugar dele. */
    void addNote (Track& t, int channel, int key, int velocity, double on, double off, bool zeroVelocityOff = false)
    {
        t.addEvent (juce::MidiMessage::noteOn (channel, key, (juce::uint8) velocity), on);
        t.addEvent (zeroVelocityOff ? juce::MidiMessage (0x90 + channel - 1, key, 0) : juce::MidiMessage::noteOff (channel, key), off);
    }

    Track named (const juce::String& name)
    {
        Track t;
        t.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);
        return t;
    }

    void addTempo (Track& t, double bpm, int num, int den)
    {
        t.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)), 0.0);
        t.addEvent (juce::MidiMessage::timeSignatureMetaEvent (num, den), 0.0);
    }

    /** ppq <= 0 = SMPTE de 25 quadros com 40 ticks (1000 por segundo). */
    bool writeMidi (const juce::File& file, int format, int ppq, std::vector<Track> tracks)
    {
        juce::MidiFile midi;

        if (ppq > 0)
            midi.setTicksPerQuarterNote (ppq);
        else
            midi.setSmpteTimeFormat (25, 40);

        for (auto& t : tracks)
        {
            t.updateMatchedPairs();
            midi.addTrack (t);
        }

        file.deleteFile();
        juce::FileOutputStream out (file);
        return out.openedOk() && midi.writeTo (out, format);
    }

    std::vector<Note> notesOf (const Project& p, const juce::String& channel)
    {
        const auto* pat = p.currentPatternPtr();
        const auto* list = pat != nullptr ? pat->notesFor (channel) : nullptr;
        return list != nullptr ? *list : std::vector<Note>();
    }

    bool has (const std::vector<Note>& notes, int start, int length, int key, int velocity = -1)
    {
        return std::any_of (notes.begin(), notes.end(), [=] (const Note& n)
        {
            return n.start == start && n.length == length && n.key == key && (velocity < 0 || n.velocity == velocity);
        });
    }

    juce::String describe (const std::vector<Note>& notes)
    {
        juce::StringArray s;

        for (auto& n : notes)
            s.add (juce::String (n.key) + "@" + juce::String (n.start) + "+" + juce::String (n.length) + "v" + juce::String (n.velocity));

        return s.joinIntoString (" ");
    }

    juce::String instrumentChannel (const Project& p)
    {
        for (auto& c : p.channels)
            if (! c.isAudio())
                return c.id;

        return {};
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
}

void runProducerPianoSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    // Tipo 0 (uma trilha, dois canais MIDI), 480 PPQ e 85 BPM: os acordes de C e Am no canal 1, o segundo desligado
    // por note-on de força 0, e o baixo no canal 2, com a última nota em força 1.
    const auto type0 = ctx.work.getChildFile ("importar-tipo0.mid");
    {
        auto t = named ("Dream Acordes");
        addTempo (t, 85.0, 4, 4);

        for (int key : { 60, 64, 67 })
            addNote (t, 1, key, 100, 0.0, 1920.0);

        for (int key : { 57, 60, 64 })
            addNote (t, 1, key, 96, 1920.0, 3840.0, true);

        addNote (t, 2, 36, 110, 0.0, 960.0);
        addNote (t, 2, 33, 1, 1920.0, 2880.0);
        rep.section ("Importar MIDI: leitura dos arquivos"_t);
        rep.check (writeMidi (type0, 0, 480, { t }), "um .mid tipo 0 escrito aqui (480 PPQ, 85 BPM, dois canais MIDI)"_t);
    }

    // Tipo 1, 96 PPQ, 120 BPM em 3/4: o maestro, "Acordes" no compasso 3, "Baixo" no compasso 4 e uma trilha só com nome.
    const auto type1 = ctx.work.getChildFile ("importar-tipo1.mid");
    {
        auto conductor = named ("Maestro");
        addTempo (conductor, 120.0, 3, 4);
        auto chords = named ("Acordes");
        addNote (chords, 1, 60, 90, 576.0, 672.0);
        auto bass = named ("Baixo");
        addNote (bass, 2, 36, 100, 864.0, 1056.0);
        rep.check (writeMidi (type1, 1, 96, { conductor, chords, bass, named ("Vazia") }),
                   "um .mid tipo 1 escrito aqui (96 PPQ, 120 BPM, 3/4, quatro trilhas)"_t);
    }

    {
        MidiFileInfo info;
        const auto read = readMidiInfo (type0, info);
        rep.check (read.wasOk() && info.format == 0 && info.ticksPerBeat == 480 && std::abs (info.bpm - 85.0) < 0.01 && info.tracks.size() == 2
                       && info.tracks[0].name == "Dream Acordes, canal MIDI 1" && info.tracks[0].notes == 6 && info.tracks[0].midiChannel == 1
                       && info.tracks[1].notes == 2 && info.tracks[1].lowKey == 33 && info.tracks[1].highKey == 36 && info.totalNotes() == 8,
                   "tipo 0: uma linha por canal MIDI, 8 notas (a força 0 não conta como nota)"_t);

        const auto r = app.run ("pianoroll.inspectMidi", args ({ { "path", type1.getFullPathName() } }));
        const auto* tracks = r.value["tracks"].getArray();
        rep.check (r.ok && (int) r.value["format"] == 1 && (int) r.value["ticksPerBeat"] == 96 && std::abs ((double) r.value["bpm"] - 120.0) < 0.01
                       && (int) r.value["timeSigNum"] == 3 && (int) r.value["timeSigDen"] == 4 && tracks != nullptr && tracks->size() == 2
                       && (*tracks)[0]["name"].toString() == "Acordes" && (int) (*tracks)[0]["track"] == 1
                       && (*tracks)[1]["name"].toString() == "Baixo" && (int) (*tracks)[1]["notes"] == 1,
                   "pianoroll.inspectMidi no tipo 1: o maestro e a trilha vazia ficam de fora; Acordes e Baixo, em 3/4 a 120 BPM"_t);

        const auto junk = ctx.work.getChildFile ("importar-ruim.mid");
        junk.replaceWithText ("isto não é MIDI"_t);
        rep.check (readMidiInfo (junk, info).failed() && ! app.run ("pianoroll.inspectMidi", args ({ { "path", junk.getFullPathName() } })).ok
                       && ! app.run ("pianoroll.inspectMidi", args ({ { "path", "relativo.mid" } })).ok,
                   "inspecionar arquivo ruim ou caminho incompleto: recusado"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Importar MIDI: o comando (trilha, trocar e somar, onde começa, desfazer)"_t);
    app.run ("project.new");
    const auto ch = instrumentChannel (model.get());
    rep.check (ch.isNotEmpty(), "o projeto novo tem um canal de instrumento"_t);

    {
        auto r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type0.getFullPathName() } }));
        auto notes = notesOf (model.get(), ch);
        const bool zeroOk = std::none_of (notes.begin(), notes.end(), [] (const Note& n) { return n.velocity < 1; });
        rep.check (r.ok && (int) r.value["notes"] == 8 && notes.size() == 8 && (int) r.value["tracks"] == 2 && zeroOk
                       && has (notes, 0, 3840, 60, 100) && has (notes, 0, 3840, 67) && has (notes, 3840, 3840, 57, 96) && has (notes, 3840, 3840, 64)
                       && has (notes, 0, 1920, 36, 110) && has (notes, 3840, 1920, 33, 1),
                   "tipo 0, todas as trilhas: 480 PPQ vira 960, a nota desligada por força 0 dura até ali, força 1 fica 1: "_t + describe (notes));

        r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type0.getFullPathName() }, { "track", 2 } }));
        notes = notesOf (model.get(), ch);
        rep.check (r.ok && notes.size() == 2 && has (notes, 0, 1920, 36) && has (notes, 3840, 1920, 33)
                       && r.value["track"].toString() == "Dream Acordes, canal MIDI 2",
                   "só a trilha 2 (o canal MIDI 2): o baixo, trocando o que havia"_t);

        r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type1.getFullPathName() } }));
        notes = notesOf (model.get(), ch);
        rep.check (r.ok && notes.size() == 2 && has (notes, 0, 960, 60, 90) && has (notes, 2880, 1920, 36, 100),
                   "tipo 1: os dois compassos vazios (de 3/4) saem e o Baixo fica um compasso depois dos Acordes: "_t + describe (notes));

        r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type1.getFullPathName() }, { "track", 2 } }));
        notes = notesOf (model.get(), ch);
        rep.check (r.ok && notes.size() == 1 && has (notes, 2880, 1920, 36),
                   "só o Baixo do tipo 1: continua alinhado com o arquivo inteiro (o compasso vazio conta pelo arquivo)"_t);

        // somar e trocar: os passos do canal também saem na troca
        app.run ("pianoroll.clear", args ({ { "channel", ch } }));
        setGrid (app, ch, "x...x...x...x...");
        r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type1.getFullPathName() }, { "replace", false } }));
        const auto* steps = &model.get().currentPatternPtr()->steps.at (ch);
        const bool stepsKept = std::count_if (steps->begin(), steps->end(), [] (const Step& s) { return s.on; }) == 4;
        rep.check (r.ok && notesOf (model.get(), ch).size() == 2 && stepsKept, "somar: os 4 passos do canal ficam e as 2 notas entram"_t);

        r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type0.getFullPathName() }, { "track", 1 }, { "replace", false } }));
        rep.check (r.ok && notesOf (model.get(), ch).size() == 8, "somar de novo: 2 + 6 notas"_t);

        const auto before = notesOf (model.get(), ch);
        const auto stepsBefore = model.get().currentPatternPtr()->steps.at (ch);
        r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type1.getFullPathName() }, { "track", 1 } }));
        steps = &model.get().currentPatternPtr()->steps.at (ch);
        rep.check (r.ok && notesOf (model.get(), ch).size() == 1 && std::none_of (steps->begin(), steps->end(), [] (const Step& s) { return s.on; }),
                   "trocar: as 8 notas e os 4 passos saem, entra o acorde dos Acordes"_t);

        app.run ("edit.undo");
        rep.check (notesOf (model.get(), ch) == before && model.get().currentPatternPtr()->steps.at (ch) == stepsBefore,
                   "um Ctrl+Z traz de volta as 8 notas e os 4 passos, como estavam"_t);

        // onde começa: em ticks ou em compassos (o at do comando)
        r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type0.getFullPathName() }, { "track", 1 }, { "at", 2 }, { "unit", "bars" } }));
        notes = notesOf (model.get(), ch);
        rep.check (r.ok && notes.size() == 6 && notes.front().start == 7680 && has (notes, 7680 + 3840, 3840, 57),
                   "at 2 em compassos: o primeiro acorde no compasso 3 (7680 ticks)"_t);

        r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type0.getFullPathName() }, { "track", 1 }, { "at", 15 }, { "unit", "bars" } }));
        notes = notesOf (model.get(), ch);
        rep.check (r.ok && (int) r.value["dropped"] == 3 && notes.size() == 3 && (int) r.value["lengthSteps"] == 256 && notes.front().start == 57600,
                   "no compasso 16: o acorde que passaria do fim do padrão fica de fora (3 notas) e o padrão cresce até 16 compassos"_t);

        const auto kept = notesOf (model.get(), ch);
        rep.check (! app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type0.getFullPathName() }, { "at", 16 }, { "unit", "bars" } })).ok
                       && ! app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type0.getFullPathName() }, { "at", -1 } })).ok,
                   "começo fora do padrão (compasso 17 ou antes do 1): recusado"_t);

        const auto bad = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", type0.getFullPathName() }, { "track", 3 } }));
        rep.check (! bad.ok && bad.error.contains ("2 trilhas") && notesOf (model.get(), ch) == kept,
                   "trilha que não existe: recusada, com quantas há, e nada muda: "_t + bad.error);
    }

    {
        // SMPTE (por segundos): 1 s e meio segundo no BPM do projeto (140) viram 2240 e 1120 ticks
        const auto smpte = ctx.work.getChildFile ("importar-smpte.mid");
        auto t = named ("Por segundos");
        addNote (t, 1, 62, 80, 1000.0, 1500.0);
        rep.check (writeMidi (smpte, 0, 0, { t }), "um .mid em SMPTE (25 quadros, 1000 ticks por segundo)"_t);
        const auto r = app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", smpte.getFullPathName() } }));
        const auto notes = notesOf (model.get(), ch);
        rep.check (r.ok && notes.size() == 1 && has (notes, 2240, 1120, 62, 80), "SMPTE: os segundos no BPM do projeto: "_t + describe (notes));

        // a mesma nota em duas trilhas (dobradas): entra uma só, a mais longa
        const auto doubled = ctx.work.getChildFile ("importar-dobrada.mid");
        auto a = named ("Piano");
        addNote (a, 1, 60, 100, 0.0, 96.0);
        auto b = named ("Cordas");
        addNote (b, 2, 60, 80, 0.0, 192.0);
        writeMidi (doubled, 1, 96, { a, b });
        app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", doubled.getFullPathName() } }));
        auto both = notesOf (model.get(), ch);
        app.run ("pianoroll.importMidi", args ({ { "channel", ch }, { "path", doubled.getFullPathName() }, { "track", 1 } }));
        rep.check (both.size() == 1 && has (both, 0, 1920, 60, 80) && has (notesOf (model.get(), ch), 0, 960, 60, 100),
                   "a mesma nota em duas trilhas vira uma, a mais longa; só a primeira trilha, a dela"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Importar MIDI: o cartão de opções no piano roll"_t);
    {
        theme::LookAndFeel laf;   // o visual da casa, só neste pai (nada global)
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1400, 700);

        app.run ("pianoroll.clear", args ({ { "channel", ch } }));
        addNotes (app, ch, { noteVar (0.0, 1.0, 72, false) });

        PianoRoll roll (app);
        host.addAndMakeVisible (roll);
        roll.setBounds (host.getLocalBounds());
        roll.setChannel (ch);
        roll.refresh (model.get());

        juce::String status;
        bool statusError = false;
        roll.onStatus = [&] (const juce::String& m, bool e) { status = m; statusError = e; };
        roll.getGrid().onStatus = roll.onStatus;

        roll.filesDropped (juce::StringArray (type0.getFullPathName()), 300, 300);
        auto* dialog = roll.getMidiImportDialog();
        rep.check (dialog != nullptr && dialog->isVisible() && dialog->getBounds() == roll.getLocalBounds() && notesOf (model.get(), ch).size() == 1,
                   "soltar o .mid abre o cartão de opções por cima do piano roll, e nada entra ainda"_t);
        rep.check (savePng (roll, ctx.work.getChildFile ("importar-midi-cartao.png")), "captura do cartão (importar-midi-cartao.png)"_t);

        if (dialog != nullptr)
            dialog->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));

        rep.check (roll.getMidiImportDialog() == nullptr && notesOf (model.get(), ch).size() == 1, "Esc fecha o cartão sem importar"_t);

        roll.askMidiImport (type0);
        dialog = roll.getMidiImportDialog();

        if (dialog != nullptr)
        {
            dialog->setChoice ({ 2, false, 0 });   // só o baixo, somando
            dialog->accept();
        }

        auto notes = notesOf (model.get(), ch);
        rep.check (dialog != nullptr && roll.getMidiImportDialog() == nullptr && notes.size() == 3 && has (notes, 0, 960, 72)
                       && has (notes, 0, 1920, 36) && ! statusError && status.contains ("2 notas") && status.contains ("canal MIDI 2")
                       && status.contains ("85 BPM"),
                   "escolher a trilha 2 e Somar: o baixo entra junto da nota que havia, e o aviso diz o quê: "_t + status);

        roll.askMidiImport (ctx.work.getChildFile ("importar-ruim.mid"));
        rep.check (roll.getMidiImportDialog() == nullptr && statusError && status.contains ("MIDI"),
                   "arquivo que não é MIDI: o cartão não abre e o erro vai para o pé"_t);

        // a agulha: o cartão recebe o compasso dela (o piano roll mede no PAT) e o Importar leva o at
        MidiFileInfo info;
        readMidiInfo (type0, info);
        MidiImportDialog atNeedle (type0, info, "Teste", 140.0, 2 * ticksPerBar4, ticksPerBar4);
        MidiImportDialog::Choice chosen;
        atNeedle.onImport = [&chosen] (const juce::File&, MidiImportDialog::Choice c) { chosen = c; };
        atNeedle.setChoice ({ 0, true, 1 });
        atNeedle.accept();
        rep.check (chosen.at == 2 * ticksPerBar4 && chosen.replace && chosen.track == 0,
                   "Na agulha (compasso 3): o Importar manda at = 7680"_t);

        MidiImportDialog again (type0, info, "Teste", 140.0, ticksPerBar4, ticksPerBar4);
        rep.check (again.getChoice().at == ticksPerBar4, "o cartão lembra a escolha da agulha na próxima vez"_t);

        host.removeChildComponent (&roll);
    }

    //--------------------------------------------------------------------------
    rep.section ("Piano roll: colar em outro canal, a oitava e as linhas das teclas"_t);
    {
        theme::LookAndFeel laf;
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1400, 700);

        app.run ("project.new");
        juce::String a, b;   // dois canais de instrumento: o Nexus e o Piano do vídeo

        for (auto& c : model.get().channels)
        {
            if (c.isAudio())
                continue;

            if (a.isEmpty())        a = c.id;
            else if (b.isEmpty())   b = c.id;
        }

        rep.check (a.isNotEmpty() && b.isNotEmpty() && a != b, "o projeto novo tem dois canais de instrumento"_t);

        setGrid (app, b, "................");   // o B sem passos: o colar só traz as 3 notas

        // três notas com durações e espaços diferentes (meio tempo, um tempo, um passo)
        addNotes (app, a, { noteVar (1.0, 0.5, 60, false), noteVar (1.5, 1.0, 64, false), noteVar (3.0, 0.25, 67, false) });

        PianoRoll roll (app);
        host.addAndMakeVisible (roll);
        roll.setBounds (host.getLocalBounds());
        roll.setChannel (a);
        roll.refresh (model.get());
        auto& grid = roll.getGrid();
        const auto ctrl = juce::ModifierKeys::commandModifier;

        grid.keyPressed (juce::KeyPress ('a', ctrl, 0));
        grid.keyPressed (juce::KeyPress ('c', ctrl, 0));

        // o caminho do vídeo: o canal muda pelo channel rack (o MainComponent chama o setChannel) e o Ctrl+V chega na janela,
        // não na grade, porque ninguém clicou nela; a janela repassa ao piano roll que está na tela
        roll.setChannel (b);
        roll.refresh (model.get());
        const bool pasted = roll.handleClipboardKey (juce::KeyPress ('v', ctrl, 0));
        auto copied = notesOf (model.get(), b);
        bool same = copied.size() == 3;

        if (same)
        {
            const int base = copied[0].start;
            same = copied[0].key == 60 && copied[0].length == 480
                   && copied[1].key == 64 && copied[1].start - base == 480 && copied[1].length == 960
                   && copied[2].key == 67 && copied[2].start - base == 1920 && copied[2].length == 240;
        }

        rep.check (pasted && same && notesOf (model.get(), a).size() == 3,
                   "Ctrl+C no canal A, Ctrl+V no B pela janela: as 3 notas com as mesmas durações e o mesmo espaço: "_t + describe (copied));

        rep.check (! roll.handleClipboardKey (juce::KeyPress (juce::KeyPress::deleteKey)) && ! roll.handleClipboardKey (juce::KeyPress ('v'))
                       && notesOf (model.get(), b).size() == 3,
                   "a janela só repassa Ctrl+C/X/V/B (o Delete com o foco em outro lugar não apaga notas)"_t);

        // a oitava: Ctrl+cima/baixo, como no FL; o Shift continua. Depois de cada tecla, o refresh que a janela faz
        // quando o projeto muda (aqui não há janela)
        roll.refresh (model.get());
        auto press = [&] (const juce::KeyPress& k) { const bool used = grid.keyPressed (k); roll.refresh (model.get()); return used; };
        auto keysOf = [&] { juce::StringArray k; for (auto& n : notesOf (model.get(), b)) k.add (juce::String (n.key)); return k.joinIntoString (" "); };
        const auto startsBefore = notesOf (model.get(), b);
        // o texto de cada linha sai depois da tecla (a ordem dos argumentos de uma chamada não é garantida)
        const bool up = press (juce::KeyPress (juce::KeyPress::upKey, ctrl, 0));
        const auto afterUp = keysOf();
        rep.check (up && afterUp == "72 76 79", "Ctrl+cima sobe uma oitava: "_t + afterUp);

        const bool down = press (juce::KeyPress (juce::KeyPress::downKey, ctrl, 0));
        const auto afterDown = keysOf();
        rep.check (down && afterDown == "60 64 67", "Ctrl+baixo desce uma oitava: "_t + afterDown);

        const bool shiftDown = press (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0));
        const auto afterShiftDown = keysOf();
        const bool shiftUp = press (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
        const auto afterShiftUp = keysOf();
        rep.check (shiftDown && afterShiftDown == "48 52 55" && shiftUp && afterShiftUp == "60 64 67",
                   "Shift+baixo e Shift+cima continuam dando a oitava: "_t + afterShiftDown + ", " + afterShiftUp);
        press (juce::KeyPress (juce::KeyPress::leftKey, ctrl, 0));
        rep.check (notesOf (model.get(), b) == startsBefore, "Ctrl+esquerda não mexe as notas (só a oitava ganhou o Ctrl)"_t);

        // as linhas da grade: Dó menor (o projeto novo); Ré# (63) e Fá# (66) são pretas, Ré# na escala e Fá# fora;
        // Ré (62) e Lá (69) são brancas, Ré na escala e Lá fora. As pretas escuras, as brancas claras, a escala não troca.
        app.run ("project.setKey", args ({ { "root", "C" }, { "scale", "minor" } }));
        app.run ("pianoroll.clear", args ({ { "channel", b } }));
        roll.refresh (model.get());
        const auto image = juce::SoftwareImageType().convert (grid.createComponentSnapshot (grid.getLocalBounds(), true, 1.0f));

        auto rowBrightness = [&] (int key)
        {
            // mediana de 21 pixels no meio da grade: uma linha vertical da grade no caminho não muda a conta
            const int y = juce::roundToInt (grid.pointAt (0.0, key).y);
            std::vector<float> v;

            for (int x = grid.getWidth() / 2 - 10; x <= grid.getWidth() / 2 + 10; ++x)
                v.push_back (image.getPixelAt (x, y).getPerceivedBrightness());

            std::sort (v.begin(), v.end());
            return v[v.size() / 2];
        };

        auto orangeNearKeyboard = [&] (int key)
        {
            const int y = juce::roundToInt (grid.pointAt (0.0, key).y);

            for (int x = 72; x < 110; ++x)   // da beira da grade (o teclado tem 72 px, e a tecla que soou fica laranja)
                if (const auto c = image.getPixelAt (x, y); c.getRed() > 200 && c.getGreen() > 70 && c.getBlue() < 110)
                    return true;

            return false;
        };

        const auto visible = [&] (int key) { const float y = grid.pointAt (0.0, key).y; return y > 30.0f && y < (float) grid.getHeight() - 110.0f; };
        rep.check (visible (60) && visible (72), "as linhas de Dó 4 a Dó 5 estão na tela da grade"_t);

        const float black63 = rowBrightness (63), black66 = rowBrightness (66), white62 = rowBrightness (62), white69 = rowBrightness (69);
        rep.check (black63 < white62 - 0.01f && black66 < white69 - 0.01f && std::abs (black63 - black66) < 0.005f && std::abs (white62 - white69) < 0.005f,
                   "linhas como no teclado: Ré# e Fá# escuras, Ré e Lá claras, na escala ou fora ("_t
                       + juce::String (black63, 3) + " " + juce::String (black66, 3) + " / " + juce::String (white62, 3) + " " + juce::String (white69, 3) + ")");
        rep.check (orangeNearKeyboard (60) && orangeNearKeyboard (72) && ! orangeNearKeyboard (62) && ! orangeNearKeyboard (61),
                   "a tônica (Dó) ganha o fio laranja na beira da grade; Ré e Dó# não"_t);
        rep.check (savePng (roll, ctx.work.getChildFile ("piano-linhas-das-teclas.png")), "captura das linhas (piano-linhas-das-teclas.png)"_t);
        host.removeChildComponent (&roll);
    }

    {
        // Arquivo > Importar MIDI: uma ação de atalho sem tecla (o Ctrl+M segue no metrônomo)
        auto& keys = app.getKeyMap();
        rep.check (KeyMap::findAction ("file.importMidi") != nullptr && keys.keysFor ("file.importMidi").isEmpty()
                       && keys.actionFor (juce::KeyPress ('M', juce::ModifierKeys::commandModifier, 0)) == "transport.metronome",
                   "a ação Importar MIDI existe nos atalhos, sem tecla, e o Ctrl+M continua no metrônomo"_t);
    }
}

} // namespace whynot::selftest
