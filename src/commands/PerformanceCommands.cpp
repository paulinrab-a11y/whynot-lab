// Comandos de desempenho (entrega 3.5): medidor de CPU e de falhas de buffer, e congelar.

#include "CommandRegistry.h"

namespace whynot
{

namespace
{
    using params::schema;

    CommandResult freeze (Project& p, const juce::String& mixerId, const juce::var& a)
    {
        auto* m = p.findMixer (mixerId);

        if (m == nullptr)
            return CommandResult::fail ("Trilha do mixer não encontrada."_t);

        if (m->isMaster())
            return CommandResult::fail ("O master não congela."_t);

        m->frozen = params::boolean (a, "on", ! m->frozen);

        // Todos os canais que chegam nesta trilha congelam juntos: a tela mostra a lista.
        juce::Array<juce::var> channels;
        juce::StringArray names;

        for (auto& c : p.channels)
        {
            if (const auto out = p.resolveOutput (c.output); out == m->id || p.reaches (out, m->id, false))
            {
                channels.add (c.id);
                names.add (c.name);
            }
        }

        auto o = new juce::DynamicObject();
        o->setProperty ("track", m->id);
        o->setProperty ("frozen", m->frozen);
        o->setProperty ("channels", channels);

        if (names.size() > 1)
            o->setProperty ("message", (m->frozen ? "Congelados juntos (saem pela mesma trilha): "_t : "Descongelados juntos: "_t)
                                           + names.joinIntoString (", "));

        return CommandResult::success (juce::var (o));
    }
}

void registerPerformanceCommands (CommandRegistry& r)
{
    r.add ({ "audio.performance", "Desempenho do áudio"_t, schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 const auto info = c.app.performance();
                 return info.isObject() ? CommandResult::success (info) : CommandResult::fail ("Medidor indisponível neste modo."_t);
             } });

    r.add ({ "audio.resetXruns", "Zerar falhas de buffer", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 c.app.resetXruns();
                 return CommandResult::success (c.app.performance());
             } });

    r.add ({ "mixer.setFrozen", "Congelar trilha",
             schema ({ { "track", "string", true, "Id da trilha do mixer (o master não)"_t },
                       { "on", "boolean", false, "true = congela (o som vira um WAV e a CPU folga), false = descongela; ausente = alterna"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 return freeze (c.model.data(), params::string (a, "track"), a);
             } });

    r.add ({ "channel.setFrozen", "Congelar canal",
             schema ({ { "channel", "string", true, "Id do canal: congela a trilha do mixer por onde ele sai (com os canais que saem por ela)"_t },
                       { "on", "boolean", false, "true = congela, false = descongela; ausente = alterna"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* ch = p.findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 const auto out = p.resolveOutput (ch->output);

                 if (out == masterTrackId)
                     return CommandResult::fail ("Este canal sai direto no master: mande-o para uma trilha do mixer para congelar."_t);

                 return freeze (p, out, a);
             } });
}

} // namespace whynot
