// Swing e começo do play no PAT (pedidos dos produtores, 29/10).
// - project.setSwing: o swing global, o botão no topo do channel rack. Soma ao swing de cada canal nos passos
//   (Project::stepSwingTicks), até 100 %.
// - channel.setGroove: o swing e o humanizar das notas de um canal (a barra do piano roll). Valem só na hora de
//   tocar (Projector::notesFor): as notas continuam na grade no modelo.
// - transport.setPatternStart: de onde o padrão começa a tocar no PAT (a régua do piano roll e o cabeçalho dos
//   passos); o stop volta para lá, como a agulha da playlist. O loop continua dando a volta no padrão inteiro.
// O desenho está em docs/swing.md.

#include "CommandRegistry.h"

namespace whynot
{

namespace
{
    using params::schema;

    double unitScale (const juce::var& a)
    {
        const auto unit = params::string (a, "unit", "ticks");
        return unit == "steps" ? ticksPerStep : unit == "beats" ? ticksPerBeat : unit == "bars" ? ticksPerBar4 : 1.0;
    }

    /** Um percentual de 0 a 100, com 1 casa (o que o .wnl guarda); nullopt se não é número. */
    std::optional<float> percentOf (const juce::var& a, const char* name)
    {
        const double v = params::number (a, name, 0.0);

        if (! std::isfinite (v))
            return std::nullopt;

        return (float) (std::round (juce::jlimit (0.0, 100.0, v) * 10.0) / 10.0);
    }
}

void registerGrooveCommands (CommandRegistry& r)
{
    r.add ({ "project.setSwing", "Swing do projeto"_t,
             schema ({ { "amount", "number", true, "0 a 100 %: atrasa os passos dos contratempos em todos os canais (100 % = sensação de "
                                                   "tercina); soma ao swing de cada canal, até 100 %"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto amount = percentOf (a, "amount");

                 if (! amount.has_value())
                     return CommandResult::fail ("Swing inválido."_t);

                 c.model.data().swing = *amount;
                 auto o = new juce::DynamicObject();
                 o->setProperty ("swing", *amount);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "channel.setGroove", "Groove das notas"_t,
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "swing", "number", false, "0 a 100 %: as notas do piano roll no contratempo dos 16 avos atrasam (como o swing dos "
                                                   "passos); só na hora de tocar, as notas ficam na grade"_t },
                       { "humanize", "number", false, "0 a 100 %: um pouco de tempo (até 40 ticks) e de força (até 20) ao acaso em cada nota "
                                                      "e passo, sempre o mesmo sorteio (o export sai igual); só na hora de tocar"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 if (ch->isAudio())
                     return CommandResult::fail ("Canal de áudio não tem notas para o groove."_t);

                 if (! params::has (a, "swing") && ! params::has (a, "humanize"))
                     return CommandResult::fail ("Diga o swing, o humanizar ou os dois."_t);

                 const auto swing = params::has (a, "swing") ? percentOf (a, "swing") : std::optional<float> (ch->noteSwing);
                 const auto humanize = params::has (a, "humanize") ? percentOf (a, "humanize") : std::optional<float> (ch->humanize);

                 if (! swing.has_value() || ! humanize.has_value())
                     return CommandResult::fail ("Valor inválido: use de 0 a 100."_t);

                 ch->noteSwing = *swing;
                 ch->humanize = *humanize;
                 auto o = new juce::DynamicObject();
                 o->setProperty ("swing", *swing);
                 o->setProperty ("humanize", *humanize);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "transport.setPatternStart", "Começo do play no padrão"_t,
             schema ({ { "at", "number", true, "De onde o padrão atual começa a tocar no modo PAT (0 = o começo); o stop volta para lá"_t },
                       { "unit", "string", false, "Unidade: ticks (padrão), steps, beats ou bars"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const double at = params::number (a, "at", 0.0) * unitScale (a);

                 if (! std::isfinite (at))
                     return CommandResult::fail ("Posição inválida."_t);

                 const auto* pat = c.model.get().currentPatternPtr();

                 if (pat == nullptr)
                     return CommandResult::fail ("Nenhum padrão aberto."_t);

                 // dentro do padrão: o último ponto possível é o último passo
                 const int length = juce::jmax (ticksPerStep, pat->lengthTicks());
                 c.app.setPatternStart (juce::jlimit (0, length - 1, (int) std::round (at)));
                 auto o = new juce::DynamicObject();
                 o->setProperty ("at", c.app.patternStart());
                 o->setProperty ("pattern", pat->id);
                 return CommandResult::success (juce::var (o));
             } });
}

} // namespace whynot
