// Autoteste do pitch bend e do mod wheel desenhados na nota (item 17 da pesquisa das comunidades, 26/10):
// - as contas das curvas (src/model/NoteCurves.h): valor, eventos de pitch wheel e CC1, a nota que manda no acorde;
// - os comandos (pianoroll.setNoteCurve, channel.setBendRange e as curvas no addNotes), com desfazer e erros;
// - o .wnl: campos opcionais, iguais nos dois escritores, e um projeto sem curva sai como antes;
// - o som: a nota com bend de +2 st no WHYNOT Synth toca a frequência certa no render (medida), igual à nota 2 st acima;
// - o MIDI: o glide do 808 sai como uma nota com pitch bend (e o RPN do alcance), e a curva desenhada vai e volta;
// - a tela: a raia Pitch e a Mod abertas, com um traço desenhado pelo mouse, em pitch-*.png.

#include "SelfTestKit.h"
#include "../../model/NoteCurves.h"
#include "../../model/NoteTools.h"
#include "../../model/WnlFormat.h"
#include "../../ui/PianoRoll.h"
#include "../../ui/Theme.h"

namespace whynot::selftest
{

namespace
{
    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, int mods = 0, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | mods),
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, p, now, clicks, false };
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

    juce::var points (std::initializer_list<std::pair<double, double>> pts)
    {
        juce::Array<juce::var> list;

        for (auto [t, v] : pts)
            list.add (args ({ { "t", t }, { "v", v } }));

        return list;
    }

    /** A maior diferença entre duas curvas, de 0 a `length` ticks, a cada 5 ticks. */
    float curveDiff (const std::vector<CurvePoint>& a, const std::vector<CurvePoint>& b, int length)
    {
        float worst = 0.0f;

        for (int t = 0; t < length; t += 5)
            worst = std::max (worst, std::abs (curves::valueAt (a, t) - curves::valueAt (b, t)));

        return worst;
    }

    double hzOfKey (double key)   { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0); }

    /** O id da primeira nota de um pianoroll.addNotes (-1 se ele falhou: o teste reprova em vez de cair). */
    int firstId (const CommandResult& r)
    {
        auto* ids = r.ok ? r.value["ids"].getArray() : nullptr;
        return ids != nullptr && ! ids->isEmpty() ? (int) (*ids)[0] : -1;
    }
}

void runPitchBendSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    //--------------------------------------------------------------------------
    rep.section ("Pitch bend: as contas das curvas"_t);
    {
        std::vector<CurvePoint> c { { 0, 0.0f }, { 480, 2.0f } };
        rep.check (curves::valueAt (c, -5) == 0.0f && curves::valueAt (c, 240) == 1.0f && curves::valueAt (c, 900) == 2.0f,
                   "o valor da curva: antes do primeiro ponto, no meio da reta e depois do último"_t);

        rep.check (curves::toWheel (0.0f, 2.0f) == 8192 && curves::toWheel (2.0f, 2.0f) == 16383 && curves::toWheel (-2.0f, 2.0f) == 0
                       && curves::toWheel (1.0f, 2.0f) == 12288 && std::abs (curves::fromWheel (12288, 2.0f) - 1.0f) < 1.0e-6f,
                   "semitons para o pitch wheel de 14 bits e de volta (8192 no meio)"_t);

        std::vector<CurvePoint> messy { { 300, 30.0f }, { 100, 1.234f }, { 100, 0.5f }, { -5, 0.0f } };
        curves::tidy (messy, curves::Kind::pitch);
        std::vector<CurvePoint> zeros { { 0, 0.0f }, { 50, 0.0f } };
        curves::tidy (zeros, curves::Kind::mod);
        rep.check (messy.size() == 3 && messy[0].t == 0 && messy[1].v == 0.5f && messy[2].v == 24.0f && zeros.empty(),
                   "arrumar a curva: em ordem, um ponto por t, até 24 st; toda em 0 some"_t);

        Note a;
        a.start = 0;
        a.length = 960;
        a.pitch = { { 0, 0.0f }, { 480, 2.0f } };
        auto events = curves::controlEvents ({ a }, curves::Kind::pitch, 2.0f);
        rep.check (! events.empty() && events.front().t == 0 && events.front().value == 8192 && events.back().t == 480 && events.back().value == 16383
                       && std::any_of (events.begin(), events.end(), [] (const curves::ControlEvent& e) { return e.t == 480 && e.value == 16383; }),
                   "os eventos: o começo da nota, a rampa a cada 10 ticks e o topo; no fim da nota o valor fica (a cauda não pula) ("_t + juce::String ((int) events.size()) + ")");

        // num acorde que começa junto, vale a nota com curva; a nota sem curva que começa depois volta o pitch ao meio
        Note b;
        b.start = 0;
        b.length = 960;
        b.key = 64;
        Note late;
        late.start = 720;
        late.length = 240;
        events = curves::controlEvents ({ b, a, late }, curves::Kind::pitch, 2.0f);
        const auto at = [&events] (int t)
        {
            int v = 8192;

            for (auto& e : events)
                if (e.t <= t)
                    v = e.value;

            return v;
        };

        rep.check (at (600) == 16383 && at (720) == 8192, "no acorde vale a nota com curva; a nota nova sem curva volta o pitch ao meio"_t);
        rep.check (curves::controlEvents ({ b }, curves::Kind::mod).empty(), "sem curva, nenhum evento (o canal não recebe nada a mais)"_t);

        // a volta do loop: a nota dobrada até o fim e, depois, uma nota sem curva; ela manda o meio no começo dela
        Note dive = a;
        dive.pitch = { { 0, 0.0f }, { 960, -2.0f } };
        Note next = b;
        next.start = 960;
        events = curves::controlEvents ({ next, dive }, curves::Kind::pitch, 2.0f);
        rep.check (at (959) < 8192 && at (960) == 8192 && events.back().t == 960, "a nota sem curva depois de um dive volta o pitch ao meio no começo dela"_t);

        // juntar (Ctrl+G) e strum levam a curva junto
        Note left = a, right = b;
        left.id = 1;
        right.id = 2;
        right.key = left.key;
        right.start = 960;
        right.pitch = { { 0, 0.0f }, { 480, 3.0f } };
        std::vector<Note> glued { left, right };
        notetools::glue (glued, {});
        rep.check (glued.size() == 1 && std::abs (curves::valueAt (glued[0].pitch, 700) - 2.0f) < 0.01f
                       && std::abs (curves::valueAt (glued[0].pitch, 960 + 480) - 3.0f) < 0.01f,
                   "juntar duas notas com curva: a da primeira e depois a da segunda"_t);

        Note low = a, high = b;
        low.id = 1;
        high.id = 2;
        high.pitch = { { 0, 0.0f }, { 480, 2.0f } };
        std::vector<Note> chord { low, high };
        notetools::strum (chord, {}, 120);
        rep.check (chord[1].start == 120 && std::abs (curves::valueAt (chord[1].pitch, 480 - 120) - 2.0f) < 0.01f,
                   "o strum atrasa a nota, e a curva fica no tempo em que estava"_t);

        std::vector<CurvePoint> line;

        for (int t = 0; t <= 480; t += 10)
            line.push_back ({ t, t / 480.0f * 3.0f });

        const auto simple = curves::simplify (line, 0.01f);
        rep.check (simple.size() == 2 && simple.back().v == 3.0f, "simplificar: a rampa de 49 pontos volta a ter só as duas pontas"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Pitch bend: comandos e o .wnl"_t);
    juce::String ch;
    {
        app.run ("project.new");
        const auto added = app.run ("channel.addSynth", args ({ { "preset", "3osc-sub" } }));
        ch = added.value["channel"].toString();
        const auto jsonPlain = wnl::toJson (model.get());

        auto r = addNotes (app, ch, { noteVar (0, 1, 45, false) });
        const int id = firstId (r);
        const auto plain = wnl::toJson (model.get());

        rep.check (r.ok && ! plain.contains ("\"pitch\"") && ! plain.contains ("\"mod\"") && ! plain.contains ("\"bendRange\"")
                       && jsonPlain.length() > 0,
                   "sem curva, o .wnl não ganha campo novo (sai como antes)"_t);

        r = app.run ("pianoroll.setNoteCurve", args ({ { "channel", ch }, { "id", id }, { "curve", "pitch" },
                                                       { "points", points ({ { 0, 0 }, { 0.5, 2 }, { 1, -1.257 } }) }, { "unit", "beats" } }));
        auto n = notesOf (model.get(), ch);
        rep.check (r.ok && n.size() == 1 && n[0].pitch.size() == 3 && n[0].pitch[1].t == 480 && n[0].pitch[2].v == -1.26f,
                   "pianoroll.setNoteCurve desenha a curva (t em tempos, v em semitons com 2 casas)"_t);

        r = app.run ("pianoroll.setNoteCurve", args ({ { "channel", ch }, { "id", id }, { "curve", "mod" },
                                                       { "points", points ({ { 0, 0 }, { 960, 0.75 } }) } }));
        n = notesOf (model.get(), ch);
        rep.check (r.ok && n[0].mod.size() == 2 && n[0].mod[1].v == 0.75f, "e a do mod wheel (de 0 a 1)"_t);

        const auto json = wnl::toJson (model.get());
        Project back;
        rep.check (json == wnl::toJsonReference (model.get()) && json.contains ("\"pitch\"") && json.contains ("\"mod\"")
                       && wnl::fromJson (json, back).wasOk() && notesOf (back, ch) == notesOf (model.get(), ch),
                   "as curvas vão e voltam no .wnl, iguais nos dois escritores"_t);

        const auto file = ctx.work.getChildFile ("pitch.wnl");
        Project saved = model.get(), loaded;
        rep.check (wnl::save (saved, file, false, {}).wasOk() && wnl::load (file, loaded).wasOk() && notesOf (loaded, ch) == notesOf (model.get(), ch),
                   "salvar e abrir o .wnl com as curvas"_t);

        rep.check (! app.run ("pianoroll.setNoteCurve", args ({ { "channel", ch }, { "id", id }, { "curve", "volume" }, { "points", points ({}) } })).ok
                       && ! app.run ("pianoroll.setNoteCurve", args ({ { "channel", ch }, { "id", 999 }, { "curve", "pitch" }, { "points", points ({}) } })).ok
                       && ! app.run ("pianoroll.setNoteCurve", args ({ { "channel", ch }, { "id", id }, { "curve", "pitch" },
                                                                      { "points", juce::Array<juce::var> { args ({ { "t", "x" } }) } } })).ok
                       && ! app.run ("pianoroll.setNoteCurve", args ({ { "channel", ch }, { "id", id }, { "curve", "pitch" } })).ok,
                   "recusa curva desconhecida, nota que não existe, ponto sem número e a falta dos pontos"_t);

        app.run ("edit.undo");
        n = notesOf (model.get(), ch);
        rep.check (n.size() == 1 && n[0].mod.empty() && n[0].pitch.size() == 3, "desfazer tira a curva do mod e deixa a do pitch"_t);

        r = app.run ("pianoroll.setNoteCurve", args ({ { "channel", ch }, { "id", id }, { "curve", "pitch" }, { "points", juce::Array<juce::var>() } }));
        rep.check (r.ok && notesOf (model.get(), ch)[0].pitch.empty(), "lista vazia tira a curva"_t);
        app.run ("edit.undo");

        // copiar a nota (addNotes com as curvas) leva o bend junto
        r = app.run ("pianoroll.addNotes", args ({ { "channel", ch }, { "notes", juce::Array<juce::var> {
                                                       args ({ { "t", 2 }, { "len", 1 }, { "key", 45 }, { "pitch", points ({ { 0, 1 } }) } }) } },
                                                   { "unit", "beats" } }));
        n = notesOf (model.get(), ch);
        rep.check (r.ok && n.size() == 2 && n[1].pitch.size() == 1 && n[1].pitch[0].v == 1.0f, "pianoroll.addNotes aceita a curva (copiar e colar levam o bend)"_t);
        app.run ("edit.undo");

        // o alcance: no synth é o parâmetro "bend" dele; num canal de sampler, o campo do canal
        r = app.run ("channel.setBendRange", args ({ { "channel", ch }, { "semitones", 12 } }));
        const auto* c = model.get().findChannel (ch);
        rep.check (r.ok && c != nullptr && c->synthParams.count ("bend") != 0 && c->synthParams.at ("bend") == 12.0f && curves::bendRangeFor (*c) == 12.0f,
                   "channel.setBendRange no synth muda o parâmetro Pitch bend dele (12 st)"_t);
        app.run ("edit.undo");

        juce::String sampler;   // um canal do kit do projeto novo

        for (auto& k : model.get().channels)
            if (k.kind == "sampler" && sampler.isEmpty())
                sampler = k.id;

        r = app.run ("channel.setBendRange", args ({ { "channel", sampler }, { "semitones", 7 } }));
        const auto jsonRange = wnl::toJson (model.get());
        rep.check (r.ok && model.get().findChannel (sampler) != nullptr && model.get().findChannel (sampler)->bendRange == 7
                       && jsonRange.contains ("\"bendRange\": 7") && jsonRange == wnl::toJsonReference (model.get()),
                   "no sampler (e no VST3), o bendRange do canal, opcional no .wnl"_t);
        rep.check (! app.run ("channel.setBendRange", args ({ { "channel", ch }, { "semitones", 0 } })).ok
                       && ! app.run ("channel.setBendRange", args ({ { "channel", ch }, { "semitones", 25 } })).ok,
                   "o alcance vai de 1 a 24"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Pitch bend: o som no render (WHYNOT Synth)"_t);
    {
        // o sub do 3 OSC é um seno: o Lá 2 (45) a 110 Hz; o bend de +2 st leva a 123,47 Hz
        app.run ("project.new");
        const auto added = app.run ("channel.addSynth", args ({ { "preset", "3osc-sub" } }));
        const auto synthCh = added.value["channel"].toString();
        app.run ("mixer.setSolo", args ({ { "track", added.value["mixerTrack"].toString() }, { "on", true } }));

        auto render = [&] (const juce::String& name)
        {
            const auto f = ctx.work.getChildFile (name);
            app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
            return readWav (f);
        };

        // 4 tempos (1,71 s): os 2 primeiros no tom, os 2 últimos com +2 st (um salto no meio)
        auto r = addNotes (app, synthCh, { noteVar (0, 4, 45, false) });
        const int id = firstId (r);
        app.run ("pianoroll.setNoteCurve", args ({ { "channel", synthCh }, { "id", id }, { "curve", "pitch" },
                                                   { "points", points ({ { 0, 0 }, { 1919, 0 }, { 1920, 2 } }) } }));
        const auto bent = render ("pitch-bend.wav");
        const double before = measureHz (bent, 0.15, 0.8), after = measureHz (bent, 1.0, 1.6);
        const double target = hzOfKey (47.0);
        rep.check (bent.valid && std::abs (before - 110.0) < 0.5 && std::abs (after - target) < 0.5,
                   "a nota com +2 st no meio: "_t + juce::String (before, 2) + " Hz e depois " + juce::String (after, 2) + " Hz ("
                       + juce::String (target, 2) + " esperado)");

        // a mesma frequência da nota 2 semitons acima, sem curva (a do bend sai 0,2 cent abaixo: o wheel vai até 8191/8192)
        app.run ("pianoroll.clear", args ({ { "channel", synthCh } }));
        addNotes (app, synthCh, { noteVar (0, 4, 47, false) });
        const auto plain = render ("pitch-plain.wav");
        const double reference = measureHz (plain, 1.0, 1.6);
        rep.check (plain.valid && std::abs (reference - after) < 0.1,
                   "igual à nota Si 2 sem bend: "_t + juce::String (reference, 2) + " Hz contra " + juce::String (after, 2) + " Hz");

        // com o alcance em 12, +7 st (a quinta) no começo da nota
        app.run ("pianoroll.clear", args ({ { "channel", synthCh } }));
        r = addNotes (app, synthCh, { noteVar (0, 4, 45, false) });
        app.run ("channel.setBendRange", args ({ { "channel", synthCh }, { "semitones", 12 } }));
        app.run ("pianoroll.setNoteCurve", args ({ { "channel", synthCh }, { "id", firstId (r) }, { "curve", "pitch" },
                                                   { "points", points ({ { 0, 7 } }) } }));
        const auto fifth = render ("pitch-quinta.wav");
        const double hz = measureHz (fifth, 0.2, 1.4);
        rep.check (fifth.valid && std::abs (hz - hzOfKey (52.0)) < 0.5,
                   "alcance de 12 st e +7 st: "_t + juce::String (hz, 2) + " Hz (" + juce::String (hzOfKey (52.0), 2) + " esperado)");
    }

    //--------------------------------------------------------------------------
    rep.section ("Pitch bend: MIDI exportado e importado"_t);
    {
        app.run ("project.new");
        const auto added = app.run ("channel.addSynth", args ({ { "preset", "3osc-sub" } }));
        const auto bass = added.value["channel"].toString();
        app.run ("channel.setMono", args ({ { "channel", bass }, { "on", true } }));

        // o glide do 808: Dó 2 e um slide para o Sol 2
        addNotes (app, bass, { noteVar (0, 2, 36, false), noteVar (2, 2, 43, true) });
        const auto midi = ctx.work.getChildFile ("pitch-808.mid");
        app.run ("project.exportMidi", args ({ { "path", midi.getFullPathName() }, { "what", "pattern" } }));

        // a faixa do canal (o kit do projeto novo pode ter outras), pelo nome
        const auto bassName = model.get().findChannel (bass)->name;
        juce::MidiFile file;
        int noteOns = 0, wheels = 0, top = 0, range = 0, rpn = -1;

        if (juce::FileInputStream in (midi); in.openedOk() && file.readFrom (in))
        {
            for (int t = 1; t < file.getNumTracks(); ++t)
            {
                bool mine = false;

                for (auto* e : *file.getTrack (t))
                    if (e->message.isTextMetaEvent() && e->message.getTextFromTextMetaEvent() == bassName)
                        mine = true;

                if (! mine)
                    continue;

                for (auto* e : *file.getTrack (t))
                {
                    const auto& m = e->message;

                    if (m.isNoteOn())          ++noteOns;
                    if (m.isPitchWheel())      { ++wheels; top = std::max (top, m.getPitchWheelValue()); }
                    if (m.isController() && m.getControllerNumber() == 101) rpn = m.getControllerValue();
                    if (m.isController() && m.getControllerNumber() == 6 && rpn == 0) range = m.getControllerValue();
                }
            }
        }

        rep.check (noteOns == 1 && wheels > 2 && range == 7 && top == 16383,
                   "o glide do 808 sai como uma nota só com pitch bend até +7 st (alcance 7 no RPN): "_t + juce::String (noteOns) + " nota(s), "
                       + juce::String (wheels) + " eventos de pitch, alcance " + juce::String (range));

        // importar só a faixa do canal (a linha do pianoroll.inspectMidi com o nome dele)
        auto trackOf = [&] (const juce::File& f, const juce::String& name)
        {
            const auto info = app.run ("pianoroll.inspectMidi", args ({ { "path", f.getFullPathName() } }));

            if (auto* tracks = info.value["tracks"].getArray())
                for (auto& t : *tracks)
                    if (t["name"].toString() == name)
                        return (int) t["track"];

            return -1;
        };

        const auto into = app.run ("channel.addSynth").value["channel"].toString();
        auto r = app.run ("pianoroll.importMidi", args ({ { "channel", into }, { "path", midi.getFullPathName() }, { "track", trackOf (midi, bassName) } }));
        auto back = notesOf (model.get(), into);
        rep.check (r.ok && back.size() == 1 && back[0].key == 36 && back[0].length == 3840 && ! back[0].pitch.empty()
                       && curves::valueAt (back[0].pitch, 0) == 0.0f && std::abs (curves::valueAt (back[0].pitch, 3800) - 7.0f) < 0.02f,
                   "importado de volta: uma nota com a curva do glide, de 0 a +7 st ("_t + juce::String ((int) (back.empty() ? 0 : back[0].pitch.size())) + " pontos)");

        // a curva desenhada (e o mod) vai e volta
        app.run ("project.new");
        const auto lead = app.run ("channel.addSynth").value["channel"].toString();
        r = addNotes (app, lead, { noteVar (0, 2, 60, false), noteVar (2, 1, 62, false) });
        const int first = firstId (r);
        app.run ("pianoroll.setNoteCurve", args ({ { "channel", lead }, { "id", first }, { "curve", "pitch" },
                                                   { "points", points ({ { 0, 0 }, { 480, 0 }, { 960, -3.5 }, { 1440, 1.25 } }) } }));
        app.run ("pianoroll.setNoteCurve", args ({ { "channel", lead }, { "id", first }, { "curve", "mod" },
                                                   { "points", points ({ { 0, 0.2 }, { 1920, 1 } }) } }));
        const auto drawn = notesOf (model.get(), lead);
        const auto midi2 = ctx.work.getChildFile ("pitch-curva.mid");
        app.run ("project.exportMidi", args ({ { "path", midi2.getFullPathName() }, { "what", "pattern" } }));

        const auto into2 = app.run ("channel.addSynth").value["channel"].toString();
        r = app.run ("pianoroll.importMidi", args ({ { "channel", into2 }, { "path", midi2.getFullPathName() },
                                                     { "track", trackOf (midi2, model.get().findChannel (lead)->name) } }));
        back = notesOf (model.get(), into2);
        const bool same = r.ok && back.size() == 2 && back[0].key == 60 && back[1].key == 62;
        const float pitchDiff = same ? curveDiff (drawn[0].pitch, back[0].pitch, drawn[0].length) : 99.0f;
        const float modDiff = same ? curveDiff (drawn[0].mod, back[0].mod, drawn[0].length) : 99.0f;
        rep.check (same && pitchDiff < 0.06f && modDiff < 0.02f && back[1].pitch.empty() && back[1].mod.empty()
                       && back[0].pitch.size() <= 8,
                   "a curva desenhada vai e volta pelo MIDI: diferença de "_t + juce::String (pitchDiff, 3) + " st no pitch e "
                       + juce::String (modDiff, 3) + " no mod; a nota sem curva volta sem ("_t
                       + juce::String ((int) (same ? back[0].pitch.size() : 0)) + " pontos)");
    }

    //--------------------------------------------------------------------------
    rep.section ("Pitch bend: a raia Pitch e Mod no piano roll"_t);
    {
        theme::LookAndFeel laf;   // o visual da casa, só neste pai (nada global)
        juce::Component host;
        host.setLookAndFeel (&laf);
        host.setSize (1280, 560);

        app.run ("project.new");
        const auto lead = app.run ("channel.addSynth").value["channel"].toString();
        auto r = addNotes (app, lead, { noteVar (0, 2, 60, false), noteVar (2, 1, 63, false), noteVar (3, 1, 67, false) });
        const int first = firstId (r);
        app.run ("pianoroll.setNoteCurve", args ({ { "channel", lead }, { "id", first }, { "curve", "pitch" },
                                                   { "points", points ({ { 0, 0 }, { 480, 0 }, { 720, 2 }, { 1200, 2 }, { 1440, -1 }, { 1919, 0 } }) } }));
        app.run ("pianoroll.setNoteCurve", args ({ { "channel", lead }, { "id", first }, { "curve", "mod" },
                                                   { "points", points ({ { 0, 0 }, { 1920, 0.8 } }) } }));

        PianoRoll roll (app);
        host.addAndMakeVisible (roll);
        roll.setBounds (host.getLocalBounds());
        roll.setChannel (lead);
        roll.refresh (model.get());
        auto& grid = roll.getGrid();

        // as abas ficam embaixo do teclado, na altura da raia (96 px acima da barra de rolagem de 12)
        const float laneTop = (float) grid.getHeight() - 12.0f - 96.0f;
        grid.mouseDown (mouseAt (grid, { 20.0f, laneTop + 6.0f + 20.0f + 9.0f }));
        grid.mouseUp (mouseAt (grid, { 20.0f, laneTop + 6.0f + 20.0f + 9.0f }));
        rep.check (grid.getLaneMode() == PianoRollGrid::LaneMode::pitch, "clicar na aba Pitch troca a raia"_t);

        // um traço na terceira nota (Sol 4, sem curva): de baixo até +1 st (o meio da raia é o tom; a borda, o alcance)
        const float x0 = grid.pointAt (3.0 * ticksPerBeat + 120.0, 67).x, x1 = grid.pointAt (3.0 * ticksPerBeat + 600.0, 67).x;
        const float centre = laneTop + 48.0f, half = (96.0f - 16.0f) * 0.5f;
        grid.mouseDown (mouseAt (grid, { x0, centre }));
        grid.mouseDrag (mouseAt (grid, { x1, centre - half * 0.5f }));
        grid.mouseUp (mouseAt (grid, { x1, centre - half * 0.5f }));
        auto n = notesOf (model.get(), lead);
        rep.check (n.size() == 3 && n[2].pitch.size() == 3 && n[2].pitch[0].t == 0 && n[2].pitch[1].v == 0.0f
                       && std::abs (n[2].pitch[1].t - 120) <= 10 && n[2].pitch.back().v == 1.0f && n[0].pitch.size() == 6,
                   "desenhar na raia segura o tom até onde o arrasto começou e dobra até +1 st, sem mexer nas outras notas ("_t
                       + juce::String ((int) (n.size() == 3 ? n[2].pitch.size() : 0)) + " pontos)");

        app.run ("edit.undo");
        rep.check (notesOf (model.get(), lead)[2].pitch.empty(), "o traço é um passo de desfazer"_t);
        roll.refresh (model.get());

        rep.check (savePng (roll, ctx.work.getChildFile ("pitch-raia.png")), "captura da raia Pitch (pitch-raia.png)"_t);

        grid.setLaneMode (PianoRollGrid::LaneMode::mod);
        rep.check (savePng (roll, ctx.work.getChildFile ("pitch-raia-mod.png")), "captura da raia Mod (pitch-raia-mod.png)"_t);

        grid.setLaneMode (PianoRollGrid::LaneMode::velocity);
        host.removeChildComponent (&roll);
        host.setLookAndFeel (nullptr);
    }

    app.run ("project.new");
}

} // namespace whynot::selftest
