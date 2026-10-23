// Autoteste do WHYNOT Synth dentro do app (headless): o caminho inteiro, do comando ao WAV. Dono: Synth.
// Canal, busca e favoritos; um preset de cada categoria renderizado (soa e não clipa); slide do piano
// roll virando glide; salvar e reabrir idêntico; desfazer, gestos e Variar; um beat de demonstração
// salvo em "Demo Synth" (para as capturas e para ouvir). Começa com project.new.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"
#include "../../synth/SynthPresets.h"

namespace whynot::selftest
{

namespace
{
    /** Nota com força 100 (o noteVar do kit usa 110): os picos medidos aqui contam com a força 100. */
    juce::var note (double t, double len, int key, bool slide = false)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("t", t);
        o->setProperty ("len", len);
        o->setProperty ("key", key);
        o->setProperty ("vel", 100);

        if (slide)
            o->setProperty ("slide", true);

        return juce::var (o);
    }

    juce::String dbText (float db)   { return juce::String (db, 1).replaceCharacter ('.', ','); }
}

void runSynthSelfTest (SelfTestContext& ctx)
{
    auto& app = ctx.app;
    auto& model = ctx.model;
    const auto& workFolder = ctx.work;
    auto check = [&ctx] (bool ok, const juce::String& what) { ctx.rep.check (ok, what); };
    auto section = [&ctx] (const juce::String& title) { ctx.rep.section (title); };

    //--------------------------------------------------------------------------
    section ("WHYNOT Synth: canal e presets");
    app.run ("project.new");
    const auto added = app.run ("channel.addSynth", args ({ { "preset", "808-limpo" } }));
    const auto ch = added.value["channel"].toString(), mx = added.value["mixerTrack"].toString();
    const auto* c = model.get().findChannel (ch);

    check (added.ok && c != nullptr && c->isSynth() && c->synthPreset == "808-limpo" && c->mono && c->glideMs > 0.0f,
           "canal WHYNOT Synth com o 808 Limpo (mono, glide de "_t + juce::String (c != nullptr ? juce::roundToInt (c->glideMs) : 0) + " ms)");
    check (app.getProjector().synthFor (ch) != nullptr, "o projetor pôs o WHYNOT Synth na trilha do canal"_t);
    check (c != nullptr && model.get().findMixer (mx) != nullptr && c->output == mx, "o canal ganhou a própria trilha de mixer"_t);

    const auto all = app.run ("synth.listPresets");
    check (all.ok && (int) all.value["total"] >= 150, "biblioteca com "_t + all.value["total"].toString() + " presets");
    check (app.run ("synth.listPresets", args ({ { "genre", "funk" } })).value["presets"].size() >= 12, "filtro por gênero (funk)"_t);
    check (app.run ("synth.listPresets", args ({ { "category", "808" } })).value["presets"].size() >= 10, "filtro por categoria (808)");
    auto listed = [] (const juce::var& list, const juce::String& id)
    {
        if (auto* a = list.getArray())
            for (auto& p : *a)
                if (p["id"].toString() == id)
                    return true;

        return false;
    };

    check (listed (app.run ("synth.listPresets", args ({ { "search", "FLAUTA" } })).value["presets"], "lead-flauta"),
           "busca sem diferenciar maiúsculas (\"FLAUTA\")"_t);
    check (! app.run ("synth.listPresets", args ({ { "category", "nada" } })).ok, "categoria desconhecida é recusada"_t);

    app.run ("synth.setFavorite", args ({ { "preset", "808-limpo" }, { "on", true } }));
    check (app.run ("synth.listPresets", args ({ { "favorites", true } })).value["presets"].size() == 1, "favorito marcado aparece no filtro de favoritos");
    app.run ("synth.setFavorite", args ({ { "preset", "808-limpo" }, { "on", false } }));

    //--------------------------------------------------------------------------
    section ("WHYNOT Synth: um preset de cada categoria no app");
    app.run ("mixer.setSolo", args ({ { "track", mx }, { "on", true } }));

    auto render = [&] (const juce::String& name)
    {
        const auto f = workFolder.getChildFile (name);
        app.run ("project.exportWav", args ({ { "path", f.getFullPathName() }, { "loops", 1 }, { "what", "pattern" } }));
        return readWav (f);
    };

    auto setNotes = [&] (const juce::Array<juce::var>& notes)
    {
        app.run ("pianoroll.clear", args ({ { "channel", ch } }));
        return app.run ("pianoroll.addNotes", args ({ { "channel", ch }, { "notes", notes }, { "unit", "beats" } })).ok;
    };

    for (auto& cat : synth::allCategories())
    {
        const auto presets = synth::findPresets (cat.id, "", "");

        if (presets.empty())
        {
            check (false, juce::String::fromUTF8 (cat.name) + ": nenhum preset");
            continue;
        }

        // o do meio da categoria (o primeiro já aparece nas capturas)
        const auto& preset = *presets[presets.size() / 2];
        app.run ("synth.loadPreset", args ({ { "channel", ch }, { "preset", juce::String (preset.id) } }));
        setNotes ({ note (0, 2, cat.testKey), note (2, 1.5, cat.testKey + 7) });
        const auto audio = render ("synth_" + juce::String (cat.id) + ".wav");
        const float pk = peakDb (audio);
        check (audio.valid && pk > -40.0f && pk < -1.0f,
               juce::String::fromUTF8 (cat.name) + " (" + juce::String::fromUTF8 (preset.name.c_str()) + "): soa e não clipa, pico "_t + dbText (pk) + " dBFS");
    }

    //--------------------------------------------------------------------------
    section ("WHYNOT Synth: slide do piano roll vira glide");
    app.run ("synth.loadPreset", args ({ { "channel", ch }, { "preset", "808-limpo" } }));
    setNotes ({ note (0, 1, 36), note (1, 1, 43, true) });
    const auto glide = render ("synth_glide.wav");
    const double beat = 60.0 / model.get().bpm;
    const double low = measureHz (glide, beat * 0.5, beat * 0.95), high = measureHz (glide, beat * 1.5, beat * 1.95);
    check (std::abs (low - 65.41) < 1.5, "antes do slide: Dó 1 ("_t + juce::String (low, 1) + " Hz)");
    check (std::abs (high - 98.0) < 2.0, "depois do slide: Sol 1, sem reatacar ("_t + juce::String (high, 1) + " Hz)");

    //--------------------------------------------------------------------------
    section ("WHYNOT Synth: salvar, reabrir e desfazer");
    app.run ("synth.loadPreset", args ({ { "channel", ch }, { "preset", "lead-serra-trap" } }));
    setNotes ({ note (0, 1, 60), note (1, 1, 63), note (2, 2, 67) });
    app.run ("synth.setParam", args ({ { "channel", ch }, { "params", args ({ { "flt.cutoff", 1234.56789 }, { "l1.voices", 4 },
                                                                              { "l2.on", true }, { "l2.type", "Wavetable" } }) } }));

    c = model.get().findChannel (ch);
    check (c != nullptr && std::abs (c->synthParams.at ("flt.cutoff") - 1234.5679f) < 0.001f
               && c->synthParams.at ("l1.voices") == 3.0f && c->synthParams.at ("l2.type") == 1.0f,
           "setParam: número (4 casas), rótulo de lista (\"Wavetable\") e lista numérica (4 vozes)"_t);

    app.run ("synth.randomize", args ({ { "channel", ch }, { "amount", 0.4 }, { "seed", 7 } }));

    const auto before = render ("synth_antes.wav");
    const auto file = workFolder.getChildFile ("Synth Teste").getChildFile ("Synth Teste.wnl");
    const auto saved = app.run ("project.save", args ({ { "path", file.getFullPathName() } }));
    const auto savedChannel = *model.get().findChannel (ch);
    const auto text = file.loadFileAsString();
    check (saved.ok && text.contains ("\"synth\"") && (int) juce::JSON::parse (text)["schemaVersion"] == wnl::currentSchemaVersion,
           "salvo no .wnl com o bloco \"synth\" (esquema "_t + juce::String (wnl::currentSchemaVersion) + ")");

    app.run ("project.new");
    const auto opened = app.run ("project.open", args ({ { "path", file.getFullPathName() } }));
    const auto* reopened = model.get().findChannel (ch);
    check (opened.ok && reopened != nullptr && *reopened == savedChannel, "reaberto: o canal volta idêntico (todos os parâmetros)"_t);

    const auto after = render ("synth_depois.wav");
    const float diff = maxDiff (before, after);
    check (before.valid && after.valid && diff < 1.0e-4f, "e o som também: diferença máxima "_t + juce::String (diff, 6));

    const auto snapshot = wnl::toJson (model.get());
    app.run ("synth.setParam", args ({ { "channel", ch }, { "param", "flt.cutoff" }, { "value", 500 } }));
    check (model.get().findChannel (ch)->synthParams.at ("flt.cutoff") == 500.0f, "setParam muda o canal");
    app.run ("edit.undo");
    check (wnl::toJson (model.get()) == snapshot, "desfazer volta o parâmetro"_t);

    {
        CommandRegistry::ScopedGesture gesture (app.getCommands(), "Corte");
        app.run ("synth.setParam", args ({ { "channel", ch }, { "param", "flt.cutoff" }, { "value", 600 } }));
        app.run ("synth.setParam", args ({ { "channel", ch }, { "param", "flt.cutoff" }, { "value", 700 } }));
    }

    app.run ("edit.undo");
    check (wnl::toJson (model.get()) == snapshot, "um arrasto de botão (gesto) é um passo só de desfazer"_t);

    app.run ("synth.loadPreset", args ({ { "channel", ch }, { "preset", "pad-quente" } }));
    app.run ("edit.undo");
    check (wnl::toJson (model.get()) == snapshot, "desfazer volta o preset anterior");

    app.run ("synth.randomize", args ({ { "channel", ch } }));
    const bool changed = wnl::toJson (model.get()) != snapshot;
    app.run ("edit.undo");
    check (changed && wnl::toJson (model.get()) == snapshot, "Variar muda o timbre e desfaz");

    check (! app.run ("synth.setParam", args ({ { "channel", ch }, { "param", "nada.disso" }, { "value", 1 } })).ok,
           "parâmetro desconhecido é recusado"_t);

    juce::String kitChannel;

    for (auto& other : model.get().channels)
        if (! other.isSynth())
            kitChannel = other.id;

    check (kitChannel.isEmpty() || ! app.run ("synth.loadPreset", args ({ { "channel", kitChannel }, { "preset", "808-limpo" } })).ok,
           "canal que não é synth é recusado"_t);

    //--------------------------------------------------------------------------
    // Um beat inteiro com o kit e três synths: fica salvo para as capturas e para ouvir.
    section ("WHYNOT Synth: beat de demonstração"_t);
    app.run ("project.new");
    app.run ("transport.setBpm", args ({ { "bpm", 140.0 } }));
    app.run ("pattern.setLength", args ({ { "steps", 32 } }));

    auto kitId = [&model] (const juce::String& name)
    {
        for (auto& k : model.get().channels)
            if (k.name == name)
                return k.id;

        return juce::String();
    };

    auto grid = [&] (const juce::String& name, const char* cells, int velocity)
    {
        app.run ("steps.setGrid", args ({ { "channel", kitId (name) }, { "grid", cells }, { "velocity", velocity } }));
    };

    grid ("Kick", "x.........x.....x.........x..x..", 110);
    grid ("Clap", "....x.......x.......x.......x...", 100);
    grid ("Hat",  "x.x.x.x.x.x.3.x.x.x.x.x.x.x.4.x.", 85);
    app.run ("channel.setMute", args ({ { "channel", kitId ("808") }, { "on", true } }));   // o 808 agora é o do synth

    auto addSynth = [&] (const char* preset, const char* name, const juce::Array<juce::var>& notes)
    {
        const auto r = app.run ("channel.addSynth", args ({ { "preset", preset }, { "name", juce::String::fromUTF8 (name) } }));
        const auto id = r.value["channel"].toString();
        app.run ("pianoroll.addNotes", args ({ { "channel", id }, { "notes", notes }, { "unit", "beats" } }));
        return r.ok;
    };

    const bool synths = addSynth ("808-limpo", "808 Synth", { note (0, 1.5, 36), note (1.5, 0.5, 36), note (2, 1, 43, true), note (3, 1, 39),
                                                             note (4, 1.5, 36), note (5.5, 0.5, 36), note (6, 1, 34), note (7, 1, 31, true) })
                        && addSynth ("lead-flauta", "Flauta", { note (0, 0.5, 72), note (0.5, 0.5, 75), note (1, 1, 79), note (2, 0.5, 77),
                                                                note (2.5, 0.5, 75), note (3, 1, 72), note (4, 0.5, 72), note (4.5, 0.5, 75),
                                                                note (5, 1, 79), note (6, 1, 82), note (7, 1, 79) })
                        && addSynth ("pad-escuro-trap", "Pad", { note (0, 4, 60), note (0, 4, 63), note (0, 4, 67),
                                                                 note (4, 4, 56), note (4, 4, 60), note (4, 4, 63) });
    check (synths, "três canais WHYNOT Synth (808, flauta e pad) com notas"_t);

    const auto demo = workFolder.getChildFile ("Demo Synth").getChildFile ("Demo Synth.wnl");
    check (app.run ("project.save", args ({ { "path", demo.getFullPathName() } })).ok, "salvo em "_t + demo.getFullPathName());

    const auto beatFile = workFolder.getChildFile ("synth_beat.wav");
    app.run ("project.exportWav", args ({ { "path", beatFile.getFullPathName() }, { "loops", 2 }, { "what", "pattern" } }));
    const auto beatAudio = readWav (beatFile);
    const float beatPeak = peakDb (beatAudio);
    check (beatAudio.valid && beatPeak < -0.5f && beatPeak > -24.0f, "beat inteiro sem clipar: pico "_t + dbText (beatPeak) + " dBFS");
}

} // namespace whynot::selftest
