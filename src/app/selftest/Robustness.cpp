// Autoteste de robustez: sequências aleatórias de comandos (semente fixa) com invariantes checados a
// cada passo. Só roda com a variável de ambiente WHYNOT_SELFTEST_FUZZ (= número de comandos; vazio = 2000);
// WHYNOT_SELFTEST_SEED muda a semente. Assim o autoteste normal das frentes não muda. Dono: Sampler
// (caça de bugs).
//
// Invariantes, depois de cada comando (dê certo ou não):
//   1. o projeto vira JSON, volta e vira o mesmo JSON (nada some, nada muda, nenhum número inválido);
//   2. normalise() não muda um projeto que já passou por ele;
//   3. a cada 20 comandos: desfazer e refazer volta ao mesmo JSON.
// Na primeira falha a sequência que levou até ela vai para robustez-falha.txt, na pasta do autoteste;
// se o app cair, ela fica em robustez-andamento.txt.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"

namespace whynot::selftest
{

namespace
{
    juce::String jsonOf (const Project& p)   { return wnl::toJson (p); }

    juce::String compact (const juce::var& v)   { return juce::JSON::toString (v, true); }

    struct Pools
    {
        juce::StringArray channels, mixer, items, patterns, inserts, playlistTracks;
        juce::StringArray fxTypes, styles, roles, fxParams, kitFiles;

        void refresh (const Project& p)
        {
            channels.clear(); mixer.clear(); items.clear(); patterns.clear(); inserts.clear(); playlistTracks.clear();

            for (auto& c : p.channels)       channels.add (c.id);
            for (auto& m : p.mixer)
            {
                mixer.add (m.id);
                for (auto& i : m.inserts)    inserts.add (i.id);
            }
            for (auto& i : p.playlistItems)  items.add (i.id);
            for (auto& pt : p.patterns)      patterns.add (pt.id);
            for (auto& t : p.playlistTracks) playlistTracks.add (t.id);
        }
    };

    class Fuzzer
    {
    public:
        Fuzzer (SelfTestContext& c, juce::int64 seed) : ctx (c), rng (seed) {}

        juce::String pick (const juce::StringArray& a, const char* fallback = "nao_existe")
        {
            if (a.isEmpty() || rng.nextInt (12) == 0)
                return garbage (fallback);
            return a[rng.nextInt (a.size())];
        }

        juce::String garbage (const char* base)
        {
            static const char* odd[] = { "", " ", "ç√ ÿ", "../../x", "mx_master", "ch_", "\"", "null", "0" };
            return rng.nextBool() ? juce::String (base) : juce::String::fromUTF8 (odd[rng.nextInt ((int) std::size (odd))]);
        }

        double number()
        {
            static const double values[] = { -1e9, -1000, -24, -1, -0.5, 0, 0.25, 0.5, 1, 2, 3, 7, 12, 16, 36, 60, 64,
                                             100, 127, 128, 140, 256, 999, 1000, 3840, 15360, 1e6, 1e9, 2147483647.0 };
            return rng.nextInt (4) == 0 ? (rng.nextDouble() * 400.0 - 100.0) : values[rng.nextInt ((int) std::size (values))];
        }

        juce::var notes()
        {
            juce::Array<juce::var> list;
            const int n = rng.nextInt (6);

            for (int i = 0; i < n; ++i)
            {
                auto o = new juce::DynamicObject();
                o->setProperty ("t", number());
                o->setProperty ("len", rng.nextInt (3) == 0 ? number() : (double) (1 + rng.nextInt (8)));
                o->setProperty ("key", rng.nextInt (3) == 0 ? number() : (double) (24 + rng.nextInt (72)));
                o->setProperty ("vel", (double) rng.nextInt (140));
                if (rng.nextBool()) o->setProperty ("slide", true);
                if (rng.nextInt (4) == 0) o->setProperty ("id", (double) rng.nextInt (40));
                list.add (juce::var (o));
            }

            return list;
        }

        juce::var value (const juce::String& name, const juce::String& type, const juce::String& command)
        {
            if (type == "boolean")  return rng.nextBool();
            if (type == "integer")  return (int) juce::jlimit (-2147483647.0, 2147483647.0, number());
            if (type == "number")   return number();
            if (type == "array")    return name == "notes" ? notes() : juce::var (juce::Array<juce::var> { (double) rng.nextInt (40), (double) rng.nextInt (40) });
            if (type == "object")   return juce::var (new juce::DynamicObject());

            // texto: pelo nome do parâmetro
            if (name == "channel")                         return pick (pools.channels);
            if (name == "item")                            return pick (pools.items);
            if (name == "insert")                          return pick (pools.inserts);
            if (name == "pattern")                         return pick (pools.patterns);
            if (name == "to" || name == "source" || name == "output" || name == "sidechain")
                                                           return pick (pools.mixer);
            if (name == "track")
                return command.startsWith ("playlist.") ? juce::var (rng.nextBool() ? pick (pools.playlistTracks) : juce::String (1 + rng.nextInt (14)))
                                                        : juce::var (pick (pools.mixer));
            if (name == "ref")                             return rng.nextBool() ? pick (pools.patterns) : pick (pools.channels);
            if (name == "type")                            return pick (pools.fxTypes);
            if (name == "style")                           return pick (pools.styles);
            if (name == "role")                            return pick (pools.roles);
            if (name == "param")                           return pick (pools.fxParams);
            if (name == "kind")                            return rng.nextBool() ? "pattern" : (rng.nextBool() ? "audio" : "x");
            if (name == "unit")                            { static const char* u[] = { "ticks", "steps", "beats", "bars", "x" }; return u[rng.nextInt (5)]; }
            if (name == "root")                            { static const char* r[] = { "C", "F#", "Bb", "H", "" }; return r[rng.nextInt (5)]; }
            if (name == "scale")                           { static const char* s[] = { "minor", "major", "dorian", "phrygian", "nada" }; return s[rng.nextInt (5)]; }
            if (name == "grid")                            { static const char* g[] = { "x...x...x...x...", "x", "", "3.3.8.x.2", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" }; return g[rng.nextInt (5)]; }
            if (name == "path")                            return pools.kitFiles.isEmpty() || rng.nextInt (5) == 0 ? juce::String ("Z:/nao/existe.wav") : pools.kitFiles[rng.nextInt (pools.kitFiles.size())];
            if (name == "name" || name == "preset")        return rng.nextInt (6) == 0 ? juce::String::repeatedString ("é", 300) : garbage ("Nome");

            return rng.nextBool() ? pick (pools.channels) : garbage ("x");
        }

        juce::var paramsFor (const juce::var& info)
        {
            const auto id = info["id"].toString();
            auto o = new juce::DynamicObject();
            const auto& schema = info["params"];
            const auto* props = schema["properties"].getDynamicObject();
            juce::StringArray required;

            if (auto* req = schema["required"].getArray())
                for (auto& r : *req)
                    required.add (r.toString());

            if (props != nullptr)
                for (auto& prop : props->getProperties())
                {
                    const auto name = prop.name.toString();

                    if (required.contains (name) || rng.nextInt (3) == 0)
                        o->setProperty (prop.name, value (name, prop.value["type"].toString(), id));
                }

            return juce::var (o);
        }

        SelfTestContext& ctx;
        juce::Random rng;
        Pools pools;
    };

    bool skipped (const juce::String& id)
    {
        // Arquivos, telas, aparelhos, plugins de verdade e o próprio desfazer ficam de fora.
        static const char* prefixes[] = { "project.open", "project.save", "project.export", "project.autosave", "plugins.",
                                          "plugin.", "audio.", "midi.", "live.", "shortcuts.", "view.", "edit.",
                                          "transport.play", "transport.togglePlay", "transport.togglePause",
                                          "mixer.showInsert", "channel.addPlugin", "mixer.addPluginInsert" };
        for (auto* p : prefixes)
            if (id.startsWith (p))
                return true;
        return false;
    }
}

void runRobustnessSelfTest (SelfTestContext& ctx)
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("WHYNOT_SELFTEST_FUZZ", "<off>");

    if (env == "<off>")
        return;

    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    const int total = juce::jmax (1, env.getIntValue() > 0 ? env.getIntValue() : 2000);
    const auto seed = (juce::int64) juce::SystemStats::getEnvironmentVariable ("WHYNOT_SELFTEST_SEED", "1234").getLargeIntValue();

    rep.section ("Robustez: " + juce::String (total) + " comandos aleatórios (semente "_t + juce::String (seed) + ")");
    app.run ("project.new");

    Fuzzer fz (ctx, seed);
    juce::Array<juce::var> commands;

    // o var precisa viver no laço todo: o getArray() de um temporário aponta para memória já solta
    const auto described = app.getCommands().describe();

    if (auto* list = described.getArray())
        for (auto& c : *list)
            if (! skipped (c["id"].toString()))
                commands.add (c);

    if (const auto fx = app.run ("fx.list"); fx.ok)
    {
        if (auto* effects = fx.value["effects"].getArray())
            for (auto& e : *effects)
            {
                fz.pools.fxTypes.addIfNotAlreadyThere (e["type"].toString());
                if (auto* ps = e["params"].getArray())
                    for (auto& p : *ps)
                        fz.pools.fxParams.addIfNotAlreadyThere (p["id"].toString());
            }
        if (auto* styles = fx.value["styles"].getArray())
            for (auto& s : *styles)
                fz.pools.styles.add (s.isObject() ? s["id"].toString() : s.toString());
        if (auto* roles = fx.value["roles"].getArray())
            for (auto& r : *roles)
                fz.pools.roles.add (r.isObject() ? r["id"].toString() : r.toString());
    }

    for (auto& c : model.get().channels)
        if (c.samplePath.isNotEmpty())
            fz.pools.kitFiles.addIfNotAlreadyThere (model.resolveSample (c.samplePath).getFullPathName());

    juce::StringArray history;
    juce::String failure;
    int ran = 0, ok = 0, undoChecks = 0;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto trail = ctx.work.getChildFile ("robustez-andamento.txt");

    for (int step = 0; step < total && failure.isEmpty(); ++step)
    {
        if (juce::Time::getMillisecondCounterHiRes() - started > 180000.0)   // no máximo 3 min
            break;

        fz.pools.refresh (model.get());
        const auto& info = commands.getReference (fz.rng.nextInt (commands.size()));
        const auto id = info["id"].toString();
        const auto params = fz.paramsFor (info);
        history.add (id + " " + compact (params));

        if (history.size() > 40)
            history.remove (0);

        // Se o app cair no comando, a sequência fica neste arquivo (some no fim de uma rodada inteira).
        trail.replaceWithText ("Semente " + juce::String (seed) + ", passo " + juce::String (step + 1)
                               + " (o último caiu)\n\n"_t + history.joinIntoString ("\n") + "\n");

        const auto result = app.run (id, params);
        ++ran;
        ok += result.ok ? 1 : 0;

        const auto j1 = jsonOf (model.get());
        Project back;

        if (const auto r = wnl::fromJson (j1, back); r.failed())
            failure = "o projeto não volta do próprio JSON ("_t + r.getErrorMessage() + ")";
        else if (jsonOf (back) != j1)
            failure = "o JSON muda ao ler e gravar de novo"_t;
        else
        {
            Project again = model.get();
            wnl::normalise (again);

            if (jsonOf (again) != j1)
                failure = "normalise() muda um projeto já normalizado"_t;
        }

        if (failure.isEmpty() && step % 20 == 19 && app.getCommands().canUndo())
        {
            ++undoChecks;
            const auto before = jsonOf (model.get());
            app.getCommands().undo();
            app.getCommands().redo();

            if (jsonOf (model.get()) != before)
                failure = "desfazer e refazer não volta ao mesmo projeto"_t;
        }

        if (failure.isNotEmpty())
            failure += ", no passo " + juce::String (step + 1) + ", depois de: " + history[history.size() - 1];
    }

    const auto seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    trail.deleteFile();

    if (failure.isNotEmpty())
    {
        const auto file = ctx.work.getChildFile ("robustez-falha.txt");
        file.replaceWithText ("Semente " + juce::String (seed) + "\n" + failure + "\n\nÚltimos comandos:\n"_t
                              + history.joinIntoString ("\n") + "\n\nProjeto:\n" + jsonOf (model.get()));
        rep.check (false, failure + " (sequência em " + file.getFullPathName() + ")");
    }
    else
    {
        rep.check (true, juce::String (ran) + " comandos (" + juce::String (ok) + " aceitos), " + juce::String (undoChecks)
                             + " checagens de desfazer, em " + juce::String (seconds, 1) + " s: o projeto volta igual do JSON a cada passo"_t);
    }

    app.run ("project.new");
}

} // namespace whynot::selftest
