// Autoteste do registro de comandos (10/11). Dono: comandos.
// - parâmetros conferidos no CommandRegistry::run e no lote, contra o schema do próprio comando: obrigatório,
//   lista ou objeto no lugar de valor simples (e o contrário), número não finito (em qualquer nível), enum,
//   minimum e maximum; e o que continua passando (número como texto, a raia da playlist como número);
// - params::number, integer e integer64: NaN, infinito e 1e300 nunca chegam a um (int);
// - desfazer e refazer com uma foto que não abre: devolve falha com o motivo, o projeto fica como estava e o
//   passo continua no histórico, no mesmo lugar.
//
// A maior parte roda num registro só do teste (modelo e serviços próprios): comandos de mentira com enum e
// faixa no schema, sem tocar no histórico nem na tabela de risco do app. O fim confere os comandos de verdade.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"

#include <limits>

namespace whynot::selftest
{

namespace
{
    // Os serviços que um registro precisa para existir; os comandos do teste não usam nenhum.
    struct NoServices : AppServices
    {
        void play() override {}
        void stop() override {}
        bool isPlaying() const override   { return false; }
        juce::Result newProject() override                              { return juce::Result::ok(); }
        juce::Result openProject (const juce::File&) override           { return juce::Result::ok(); }
        juce::Result saveProject (const juce::File&) override           { return juce::Result::ok(); }
        juce::Result exportWav (const juce::File&, int, bool) override  { return juce::Result::ok(); }
    };

    juce::var testSchema()
    {
        auto schema = params::schema ({ { "value", "number", true, "Valor de 0 a 10" },
                                        { "count", "integer", false, "Quantos" },
                                        { "mode", "string", false, "a ou b" },
                                        { "flag", "boolean", false, "Liga ou desliga" },
                                        { "list", "array", false, "Uma lista" },
                                        { "extra", "object", false, "Um objeto" } });

        if (auto* properties = schema["properties"].getDynamicObject())
        {
            if (auto* value = properties->getProperty ("value").getDynamicObject())
            {
                value->setProperty ("minimum", 0);
                value->setProperty ("maximum", 10);
            }

            if (auto* mode = properties->getProperty ("mode").getDynamicObject())
                mode->setProperty ("enum", juce::Array<juce::var> { juce::var ("a"), juce::var ("b") });
        }

        return schema;
    }
}

void runCommandParamsSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    rep.section ("Comandos: parâmetros conferidos no registro e desfazer com foto que não abre"_t);

    const double notANumber = std::numeric_limits<double>::quiet_NaN();
    const double infinite = std::numeric_limits<double>::infinity();

    //==========================================================================
    // Os leitores de número
    {
        const auto a = args ({ { "nan", notANumber }, { "inf", infinite }, { "big", 1.0e300 }, { "small", -1.0e300 },
                               { "half", 0.5 }, { "text", "12.5" }, { "junk", "abc" }, { "textInf", "-inf" },
                               { "seed", 3000000000.0 }, { "wide", (juce::int64) 9007199254740993LL } });

        rep.check (params::number (a, "nan", 7.0) == 7.0 && params::number (a, "inf", 7.0) == 7.0 && params::number (a, "textInf", 7.0) == 7.0,
                   "params::number: NaN, infinito e \"-inf\" (texto) viram o fallback"_t);
        rep.check (params::number (a, "big", 0.0) == 2147483647.0 && params::number (a, "small", 0.0) == -2147483648.0,
                   "params::number: 1e300 satura na faixa do int"_t);
        rep.check ((int) params::number (a, "big", 0.0) == std::numeric_limits<int>::max()
                       && (int) params::number (a, "small", 0.0) == std::numeric_limits<int>::min()
                       && (int) params::number (a, "nan", 3.0) == 3,
                   "(int) params::number é definido para 1e300, -1e300 e NaN"_t);
        rep.check (params::number (a, "half", 0.0) == 0.5 && params::number (a, "text", 0.0) == 12.5 && params::number (a, "junk", 9.0) == 0.0
                       && params::number (a, "ausente", 9.0) == 9.0,
                   "params::number: o que já funcionava continua igual (0,5; \"12.5\"; texto que não é número = 0; ausente = fallback)"_t);
        rep.check (params::number (a, "seed", 1.0) == 3000000000.0,
                   "params::number: a semente passa de 32 bits (synth.vary devolve e aceita sementes acima de 2^31)"_t);
        rep.check (params::integer (a, "big", 0) == std::numeric_limits<int>::max() && params::integer (a, "nan", 4) == 4
                       && params::integer (a, "text", 0) == 12 && params::integer (a, "ausente", -2) == -2,
                   "params::integer: satura, trunca e devolve o fallback"_t);
        rep.check (params::integer64 (a, "seed", 0) == 3000000000LL && params::integer64 (a, "wide", 0) == 9007199254740993LL
                       && params::integer64 (a, "big", 0) > 0 && params::integer64 (a, "nan", 5) == 5,
                   "params::integer64: 3e9 inteiro, 2^53 + 1 sem perder o último bit, 1e300 saturado"_t);
    }

    //==========================================================================
    // Um registro só do teste
    ProjectModel model;
    wnl::normalise (model.data());
    NoServices services;
    CommandRegistry reg (model, services);
    int runs = 0;

    reg.add ({ "t.set", "Teste: valor", testSchema(), true,
               [&runs] (CommandContext& c, const juce::var& a)
               {
                   ++runs;
                   c.model.data().bpm = 100.0 + params::number (a, "value", 0.0);
                   return CommandResult::success();
               } });

    reg.add ({ "t.fail", "Teste: falha", params::schema ({}), true,
               [] (CommandContext& c, const juce::var&)
               {
                   c.model.data().bpm = 333.0;
                   return CommandResult::fail ("falhou de propósito"_t);
               } });

    reg.add ({ "t.read", "Teste: leitura", params::schema ({ { "name", "string", true, "Nome" } }), false,
               [&runs] (CommandContext&, const juce::var&)
               {
                   ++runs;
                   return CommandResult::success();
               } });

    const double bpm0 = model.get().bpm;
    const auto refused = [&] (const juce::var& p, const char* piece)
    {
        const int before = runs;
        const auto r = reg.run ("t.set", p);
        return ! r.ok && runs == before && r.error.contains (juce::String::fromUTF8 (piece)) && model.get().bpm == bpm0 && ! reg.canUndo();
    };

    rep.check (refused ({}, "\"value\"") && refused (args ({ { "count", 2 } }), "Falta") && refused (args ({ { "value", juce::var() } }), "Falta"),
               "obrigatório ausente (sem params, sem a chave, ou com a chave vazia) é recusado antes de o comando rodar"_t);
    rep.check (refused (juce::var (3.0), "objeto") && refused (juce::var ("value"), "objeto") && refused (juce::var (juce::Array<juce::var>()), "objeto"),
               "params que não é um objeto é recusado"_t);
    rep.check (refused (args ({ { "value", notANumber } }), "finito") && refused (args ({ { "value", infinite } }), "finito")
                   && refused (args ({ { "value", -infinite } }), "finito") && refused (args ({ { "value", "nan" } }), "finito")
                   && refused (args ({ { "value", 1 }, { "count", notANumber } }), "\"count\""),
               "NaN e infinito são recusados (também escritos como texto)"_t);
    rep.check (refused (args ({ { "value", 1 }, { "foraDoSchema", notANumber } }), "finito"),
               "número não finito é recusado até numa chave fora do schema"_t);

    {
        auto* note = new juce::DynamicObject();
        note->setProperty ("t", 0);
        note->setProperty ("len", notANumber);
        const juce::var bad (juce::Array<juce::var> { juce::var (1.0), juce::var (note) });

        auto* extra = new juce::DynamicObject();
        extra->setProperty ("deep", juce::Array<juce::var> { juce::var (infinite) });

        rep.check (refused (args ({ { "value", 1 }, { "list", bad } }), "\"list\"") && refused (args ({ { "value", 1 }, { "extra", juce::var (extra) } }), "\"extra\""),
                   "número não finito dentro de uma lista ou de um objeto é recusado"_t);
    }

    rep.check (refused (args ({ { "value", juce::Array<juce::var> { juce::var (1) } } }), "lista")
                   && refused (args ({ { "value", juce::var (new juce::DynamicObject()) } }), "objeto")
                   && refused (args ({ { "value", 1 }, { "mode", juce::Array<juce::var>() } }), "\"mode\"")
                   && refused (args ({ { "value", 1 }, { "flag", juce::var (new juce::DynamicObject()) } }), "\"flag\""),
               "lista ou objeto num campo simples é recusado"_t);
    rep.check (refused (args ({ { "value", 1 }, { "list", "a,b" } }), "lista") && refused (args ({ { "value", 1 }, { "list", 3 } }), "lista")
                   && refused (args ({ { "value", 1 }, { "extra", juce::Array<juce::var>() } }), "objeto")
                   && refused (args ({ { "value", 1 }, { "extra", "x" } }), "objeto"),
               "valor simples num campo de lista ou de objeto é recusado"_t);
    rep.check (refused (args ({ { "value", -0.5 } }), "mínimo"_t.toRawUTF8()) && refused (args ({ { "value", 10.5 } }), "máximo"_t.toRawUTF8())
                   && refused (args ({ { "value", 1.0e300 } }), "máximo"_t.toRawUTF8()),
               "minimum e maximum do schema valem"_t);
    rep.check (refused (args ({ { "value", 1 }, { "mode", "c" } }), "a, b") && refused (args ({ { "value", 1 }, { "mode", 5 } }), "a, b"),
               "valor fora do enum do schema é recusado, e a mensagem lista os aceitos"_t);

    // o que passa
    {
        const auto ok1 = reg.run ("t.set", args ({ { "value", 0 }, { "mode", "a" }, { "flag", true }, { "list", juce::Array<juce::var>() },
                                                  { "extra", juce::var (new juce::DynamicObject()) }, { "count", 3 }, { "outra", "x" } }));
        rep.check (ok1.ok && model.get().bpm == 100.0 && reg.canUndo() && reg.undoDescription() == "Teste: valor",
                   "parâmetros certos (com uma chave fora do schema) rodam e viram um passo de desfazer"_t);

        const auto ok2 = reg.run ("t.set", args ({ { "value", "10" }, { "flag", 1 }, { "count", 2.5 }, { "mode", juce::var() } }));
        rep.check (ok2.ok && model.get().bpm == 110.0,
                   "valor simples de outro tipo continua passando (número como texto, 1 como sim, inteiro com casas) e o null é como ausente"_t);

        rep.check (reg.checkParams ("t.set", args ({ { "value", 5 } })).isEmpty() && reg.checkParams ("t.set", {}).contains ("value")
                       && reg.checkParams ("nao.existe", {}).contains ("desconhecido") && reg.checkParams ("t.fail", {}).isEmpty(),
                   "checkParams diz o mesmo que o run, sem rodar"_t);

        const int before = runs;
        rep.check (! reg.run ("t.read").ok && ! reg.run ("t.read", args ({ { "name", juce::Array<juce::var>() } })).ok && runs == before
                       && reg.run ("t.read", args ({ { "name", 7 } })).ok && runs == before + 1,
                   "comando que não muda o projeto também é conferido"_t);
    }

    // o lote confere todos os passos antes de rodar o primeiro
    {
        const auto step = [] (const char* id, const juce::var& p)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", id);
            o->setProperty ("params", p);
            return juce::var (o);
        };

        const int before = runs;
        const auto bad = reg.runBatch (juce::Array<juce::var> { step ("t.set", args ({ { "value", 4 } })), step ("t.set", args ({ { "value", notANumber } })) }, "Lote");
        rep.check (! bad.ok && bad.error.contains ("Passo 2") && bad.error.contains ("finito") && runs == before && model.get().bpm == 110.0
                       && reg.undoDescription() == "Teste: valor",
                   "lote com parâmetro inválido no passo 2 é recusado inteiro, sem rodar o passo 1"_t);

        const auto failed = reg.runBatch (juce::Array<juce::var> { step ("t.set", args ({ { "value", 4 } })), step ("t.fail", {}) }, "Lote");
        rep.check (! failed.ok && model.get().bpm == 110.0 && reg.undoDescription() == "Teste: valor",
                   "lote em que um passo falha volta ao projeto de antes e não entra no desfazer"_t);

        const auto good = reg.runBatch (juce::Array<juce::var> { step ("t.set", args ({ { "value", 4 } })), step ("t.set", args ({ { "value", 5 } })) }, "Lote");
        rep.check (good.ok && model.get().bpm == 105.0 && reg.undoDescription() == "Lote", "lote certo vira um passo só"_t);
        rep.check (reg.undo() && model.get().bpm == 110.0 && reg.lastHistoryError().isEmpty(), "e o desfazer do lote volta tudo"_t);
    }

    //==========================================================================
    // Desfazer e refazer com uma foto que não abre. Aqui: bpm 110, dois passos "Teste: valor" para desfazer
    // (100 e 110) e o lote para refazer.
    {
        const auto broken = [] (const juce::String&, Project&)   { return juce::Result::fail ("foto estragada (teste)"); };

        reg.readSnapshot = broken;
        const bool undone = reg.undo();
        rep.check (! undone && model.get().bpm == 110.0, "desfazer com a foto estragada devolve falha e o projeto fica como estava"_t);
        rep.check (reg.lastHistoryError().contains ("foto estragada"), "o motivo sai no lastHistoryError: "_t + reg.lastHistoryError());
        rep.check (reg.canUndo() && reg.undoDescription() == "Teste: valor" && reg.canRedo() && reg.redoDescription() == "Lote",
                   "o passo continua no histórico, no mesmo lugar (desfazer e refazer apontam para os mesmos passos)"_t);

        const bool redone = reg.redo();
        rep.check (! redone && model.get().bpm == 110.0 && reg.lastHistoryError().contains ("foto estragada")
                       && reg.canRedo() && reg.redoDescription() == "Lote" && reg.undoDescription() == "Teste: valor",
                   "refazer com a foto estragada: a mesma coisa"_t);

        const auto rolled = reg.run ("t.fail");
        rep.check (! rolled.ok && rolled.error.contains ("falhou de propósito"_t) && rolled.error.contains ("não voltou"_t),
                   "comando que falha e não consegue voltar avisa no erro: "_t + rolled.error);

        reg.readSnapshot = nullptr;
        model.data().bpm = 110.0;   // o t.fail acima deixou 333 (era o que o teste queria ver avisado)

        rep.check (reg.undo() && model.get().bpm == 100.0 && reg.lastHistoryError().isEmpty(), "com a foto boa, o mesmo passo desfaz"_t);
        rep.check (reg.undo() && model.get().bpm == bpm0 && ! reg.canUndo(), "e o anterior também"_t);
        rep.check (! reg.undo() && reg.lastHistoryError().isEmpty(), "sem passo para desfazer: false, sem mensagem de erro"_t);
        rep.check (reg.redo() && model.get().bpm == 100.0 && reg.redo() && model.get().bpm == 110.0 && reg.redo() && model.get().bpm == 105.0
                       && ! reg.canRedo(),
                   "refazer percorre os três passos de volta"_t);

        const auto back = reg.run ("t.fail");
        rep.check (! back.ok && back.error == "falhou de propósito"_t && model.get().bpm == 105.0 && reg.undoDescription() == "Lote",
                   "comando que falha volta ao projeto de antes e não mexe no histórico"_t);
    }

    //==========================================================================
    // Os comandos de verdade
    {
        auto& app = ctx.app;
        app.run ("project.new");
        const double bpm = ctx.model.get().bpm;
        const bool couldUndo = app.getCommands().canUndo();

        const auto missing = app.run ("transport.setBpm");
        const auto nan = app.run ("transport.setBpm", args ({ { "bpm", notANumber } }));
        const auto list = app.run ("transport.setBpm", args ({ { "bpm", juce::Array<juce::var> { juce::var (120) } } }));
        rep.check (! missing.ok && missing.error.contains ("bpm") && ! nan.ok && nan.error.contains ("finito") && ! list.ok
                       && ctx.model.get().bpm == bpm && app.getCommands().canUndo() == couldUndo,
                   "transport.setBpm sem bpm, com NaN e com uma lista: recusado, projeto e histórico intactos"_t);

        const auto text = app.run ("transport.setBpm", args ({ { "bpm", "128" } }));
        rep.check (text.ok && ctx.model.get().bpm == 128.0, "transport.setBpm com \"128\" (texto) continua valendo"_t);

        const auto huge = app.run ("pattern.setLength", args ({ { "steps", 1.0e300 } }));
        const auto tiny = app.run ("pattern.setLength", args ({ { "steps", -1.0e300 } }));
        Project again;
        rep.check (wnl::fromJson (wnl::toJson (ctx.model.get()), again).wasOk(),
                   juce::String ("pattern.setLength com 1e300 e -1e300 (") + (huge.ok ? "aceito" : "recusado") + ", " + (tiny.ok ? "aceito" : "recusado")
                       + "): o app não cai e o projeto continua válido"_t);

        int withSchema = 0, total = 0;
        const auto described = app.getCommands().describe();

        if (auto* commands = described.getArray())
            for (auto& c : *commands)
            {
                ++total;

                // sem parâmetros: ou o comando não tem obrigatórios (e a conferência passa), ou a recusa cita um deles
                const auto problem = app.getCommands().checkParams (c["id"].toString(), {});
                const auto* required = c["params"]["required"].getArray();
                const bool needs = required != nullptr && ! required->isEmpty();

                if (needs == problem.isNotEmpty() && (! needs || problem.contains ("\"" + required->getReference (0).toString() + "\"")))
                    ++withSchema;
            }

        rep.check (total > 0 && withSchema == total,
                   juce::String (withSchema) + " de " + juce::String (total) + " comandos: sem parâmetros, a conferência só recusa quem tem obrigatório, e cita o primeiro"_t);

        app.run ("project.new");
    }
}

} // namespace whynot::selftest
