#include "CommandRegistry.h"
#include "../engine/PluginHost.h"

namespace whynot
{

namespace
{
    using params::schema;

    juce::var describe (const juce::PluginDescription& d)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("id", d.createIdentifierString());
        o->setProperty ("name", d.name);
        o->setProperty ("manufacturer", d.manufacturerName);
        o->setProperty ("kind", d.isInstrument ? "instrument" : "effect");
        o->setProperty ("format", d.pluginFormatName);
        o->setProperty ("category", d.category);
        o->setProperty ("version", d.version);
        o->setProperty ("file", d.fileOrIdentifier);
        return juce::var (o);
    }

    /** Pelo id (createIdentifierString) ou pelo nome, sem diferenciar maiúsculas. */
    std::optional<juce::PluginDescription> findPlugin (const AppServices& app, const juce::String& idOrName)
    {
        const auto known = app.knownPlugins();

        for (auto& d : known)
            if (d.createIdentifierString() == idOrName)
                return d;

        for (auto& d : known)
            if (d.name.equalsIgnoreCase (idOrName.trim()))
                return d;

        return std::nullopt;
    }

    juce::String notFound (const juce::String& idOrName)
    {
        return "Plugin não encontrado: "_t + idOrName + ". Veja os nomes com plugins.list (ou escaneie com plugins.scan).";
    }

    /** O plugin de { channel } (instrumento) ou de { track, insert } (insert VST3 do mixer). */
    juce::AudioPluginInstance* targetInstance (CommandContext& c, const juce::var& a)
    {
        if (params::has (a, "channel"))
            return c.app.channelPlugin (params::string (a, "channel"));

        return c.app.insertPluginInstance (params::string (a, "track"), params::string (a, "insert"));
    }

    juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& pi, const juce::var& which)
    {
        const auto& all = pi.getParameters();

        if (which.isInt() || which.isInt64() || which.isDouble())
            return juce::isPositiveAndBelow ((int) which, all.size()) ? all[(int) which] : nullptr;

        const auto name = which.toString().trim();

        for (auto* p : all)
            if (p->getName (256).equalsIgnoreCase (name))
                return p;

        return nullptr;
    }
}

void registerPluginCommands (CommandRegistry& r)
{
    //==========================================================================
    // Catálogo
    r.add ({ "plugins.list", "Listar plugins",
             schema ({ { "kind", "string", false, "instrument (instrumentos) ou effect (efeitos); ausente = todos" },
                       { "search", "string", false, "Parte do nome ou do fabricante" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto kind = params::string (a, "kind");
                 const auto search = params::string (a, "search").trim();
                 juce::Array<juce::var> list;

                 for (auto& d : c.app.knownPlugins())
                 {
                     if (kind == "instrument" && ! d.isInstrument) continue;
                     if (kind == "effect" && d.isInstrument)       continue;
                     if (search.isNotEmpty() && ! d.name.containsIgnoreCase (search) && ! d.manufacturerName.containsIgnoreCase (search))
                         continue;

                     list.add (describe (d));
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("plugins", list);
                 o->setProperty ("blocked", c.app.blockedPlugins().size());
                 o->setProperty ("folders", c.app.pluginFolders());
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "plugins.scan", "Escanear plugins",
             schema ({ { "wait", "boolean", false, "true = espera terminar e devolve o relatório; padrão: roda em segundo plano"_t },
                       { "timeoutSeconds", "number", false, "Tempo máximo para cada plugin responder (padrão 30)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const int timeout = juce::jlimit (2, 300, (int) params::number (a, "timeoutSeconds", 30));
                 const auto report = c.app.scanPlugins (params::boolean (a, "wait", false), timeout);

                 if (report.hasProperty ("error"))
                     return CommandResult::fail (report["error"].toString());

                 return CommandResult::success (report);
             } });

    r.add ({ "plugins.setFolders", "Pastas de plugins",
             schema ({ { "folders", "array", true, "Pastas onde procurar arquivos .vst3"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* list = a["folders"].getArray();

                 if (list == nullptr)
                     return CommandResult::fail ("Informe as pastas em \"folders\"."_t);

                 juce::StringArray folders;

                 for (auto& f : *list)
                     folders.add (f.toString());

                 c.app.setPluginFolders (folders);
                 return CommandResult::success (juce::var (c.app.pluginFolders()));
             } });

    r.add ({ "plugins.blocked", "Plugins bloqueados", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto o = new juce::DynamicObject();
                 o->setProperty ("blocked", c.app.blockedPlugins());
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "plugins.unblock", "Desbloquear plugin",
             schema ({ { "file", "string", true, "Arquivo .vst3 bloqueado (como aparece em plugins.blocked)" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 if (! c.app.unblockPlugin (params::string (a, "file")))
                     return CommandResult::fail ("Esse arquivo não está na lista de bloqueados."_t);

                 return CommandResult::success();
             } });

    //==========================================================================
    // Instrumento no channel rack
    r.add ({ "channel.addPlugin", "Adicionar instrumento",
             schema ({ { "plugin", "string", true, "Id (de plugins.list) ou nome do instrumento VST3" },
                       { "name", "string", false, "Nome do canal; padrão = nome do plugin"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto which = params::string (a, "plugin");
                 const auto desc = findPlugin (c.app, which);

                 if (! desc)
                     return CommandResult::fail (notFound (which));

                 if (! desc->isInstrument)
                     return CommandResult::fail (desc->name + " é um efeito: ele vai num insert do mixer, não no channel rack."_t);

                 auto& p = c.model.data();

                 Channel ch;
                 ch.id = makeId ("ch");
                 ch.kind = "plugin";
                 ch.name = params::string (a, "name", desc->name).trim();
                 ch.colour = channelColourForIndex ((int) p.channels.size());
                 ch.plugin = plugin_host::refFrom (*desc);
                 ch.oneShot = false;   // instrumento respeita a duração da nota

                 MixerTrack mx;
                 mx.id = makeId ("mx");
                 mx.name = ch.name;
                 mx.colour = ch.colour;
                 mx.autoFor = ch.id;
                 mx.volumeDb = -6.0f;
                 ch.output = mx.id;

                 p.channels.push_back (ch);
                 auto at = std::find_if (p.mixer.begin(), p.mixer.end(), [] (const MixerTrack& m) { return m.isMaster(); });
                 p.mixer.insert (at, mx);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("channel", ch.id);
                 o->setProperty ("mixerTrack", mx.id);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "plugin.openEditor", "Abrir plugin",
             schema ({ { "channel", "string", false, "Id do canal com instrumento VST3" },
                       { "track", "string", false, "Ou: id da trilha de mixer (mx_master = master)..." },
                       { "insert", "string", false, "...e id do insert VST3 (fx_...)" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const bool shown = params::has (a, "channel") ? c.app.showChannelPluginEditor (params::string (a, "channel"))
                                                               : c.app.showInsertEditor (params::string (a, "track"), params::string (a, "insert"));
                 if (! shown)
                     return CommandResult::fail ("Não há um plugin aberto para mostrar aí."_t);

                 return CommandResult::success();
             } });

    //==========================================================================
    // Efeito VST3 num insert do mixer
    r.add ({ "mixer.addPluginInsert", "Adicionar efeito VST3",
             schema ({ { "track", "string", true, "Id da trilha de mixer (mx_master = master)" },
                       { "plugin", "string", true, "Id (de plugins.list) ou nome do efeito VST3" },
                       { "slot", "integer", false, "Posição 0 a 7 (ausente = no fim)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* track = c.model.data().findMixer (params::string (a, "track"));

                 if (track == nullptr)
                     return CommandResult::fail ("Trilha de mixer não encontrada."_t);

                 const auto which = params::string (a, "plugin");
                 const auto desc = findPlugin (c.app, which);

                 if (! desc)
                     return CommandResult::fail (notFound (which));

                 if (desc->isInstrument)
                     return CommandResult::fail (desc->name + " é um instrumento: ele vai no channel rack (channel.addPlugin)."_t);

                 if ((int) track->inserts.size() >= maxInserts)
                     return CommandResult::fail ("Esta trilha já tem os "_t + juce::String (maxInserts) + " efeitos. Remova um antes."_t);

                 Insert ins;
                 ins.id = makeId ("fx");
                 ins.kind = "vst3";
                 ins.type = desc->name;
                 ins.plugin = plugin_host::refFrom (*desc);

                 const int slot = juce::jlimit (0, (int) track->inserts.size(),
                                                (int) params::number (a, "slot", (double) track->inserts.size()));
                 track->inserts.insert (track->inserts.begin() + slot, ins);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("insert", ins.id);
                 return CommandResult::success (juce::var (o));
             } });

    //==========================================================================
    // Parâmetros (para a IA e para o autoteste; não entram no desfazer, como no FL)
    r.add ({ "plugin.parameters", "Parâmetros do plugin"_t,
             schema ({ { "channel", "string", false, "Id do canal com instrumento VST3" },
                       { "track", "string", false, "Ou: id da trilha de mixer..." },
                       { "insert", "string", false, "...e id do insert VST3" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* pi = targetInstance (c, a);

                 if (pi == nullptr)
                     return CommandResult::fail ("Não há um plugin carregado aí."_t);

                 juce::Array<juce::var> list;
                 const auto& all = pi->getParameters();

                 for (int i = 0; i < all.size(); ++i)
                 {
                     auto o = new juce::DynamicObject();
                     o->setProperty ("index", i);
                     o->setProperty ("name", all[i]->getName (256));
                     o->setProperty ("value", all[i]->getValue());
                     o->setProperty ("text", all[i]->getCurrentValueAsText());
                     o->setProperty ("label", all[i]->getLabel());
                     list.add (juce::var (o));
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("plugin", pi->getName());
                 o->setProperty ("parameters", list);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "plugin.setParameter", "Mudar parâmetro do plugin"_t,
             schema ({ { "channel", "string", false, "Id do canal com instrumento VST3" },
                       { "track", "string", false, "Ou: id da trilha de mixer..." },
                       { "insert", "string", false, "...e id do insert VST3" },
                       { "parameter", "string", true, "Índice ou nome do parâmetro (de plugin.parameters)"_t },
                       { "value", "number", true, "Valor normalizado, de 0 a 1" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* pi = targetInstance (c, a);

                 if (pi == nullptr)
                     return CommandResult::fail ("Não há um plugin carregado aí."_t);

                 auto* param = findParameter (*pi, a["parameter"]);

                 if (param == nullptr)
                     return CommandResult::fail ("Parâmetro não encontrado: "_t + a["parameter"].toString());

                 param->setValueNotifyingHost ((float) juce::jlimit (0.0, 1.0, params::number (a, "value", param->getValue())));
                 c.app.pluginStateTouched();

                 auto o = new juce::DynamicObject();
                 o->setProperty ("value", param->getValue());
                 o->setProperty ("text", param->getCurrentValueAsText());
                 return CommandResult::success (juce::var (o));
             } });
}

} // namespace whynot
