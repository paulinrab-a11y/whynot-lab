// Comandos da automação (entrega 3.5): uma curva por parâmetro, em ticks da música.
// Contrato do núcleo: automation.addLane { target }, automation.setPoints { lane, points } e
// automation.removeLane { lane }, cada um com desfazer. automation.list e automation.targets só leem.

#include "CommandRegistry.h"
#include "../automation/AutomationTarget.h"

namespace whynot
{

namespace
{
    using params::schema;

    constexpr double maxTicks = (double) maxTimelineTicks;   // o teto da linha do tempo (10.000 compassos)

    int toTicks (const juce::var& args, double value)
    {
        const auto unit = params::string (args, "unit", "ticks");
        const double scale = unit == "steps" ? ticksPerStep : unit == "beats" ? ticksPerBeat : unit == "bars" ? ticksPerBar4 : 1.0;
        const double ticks = value * scale;
        return std::isfinite (ticks) ? juce::roundToInt (juce::jlimit (0.0, maxTicks, ticks)) : 0;
    }

    double field (const juce::var& o, const char* key, double fallback)
    {
        if (! o.isObject() || ! o.hasProperty (key))
            return fallback;

        const auto& v = o[key];
        const double d = (v.isDouble() || v.isInt() || v.isInt64()) ? (double) v : v.toString().getDoubleValue();
        return std::isfinite (d) ? d : fallback;
    }

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    AutomationTarget targetFrom (const juce::var& o)
    {
        AutomationTarget t;
        t.kind = o["kind"].toString();
        t.track = o["track"].toString();
        t.channel = o["channel"].toString();
        t.insert = o["insert"].toString();
        t.param = o["param"].toString();
        return automation::tidy (t);
    }

    juce::var targetVar (const AutomationTarget& t)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("kind", t.kind);

        if (t.track.isNotEmpty())    o->setProperty ("track", t.track);
        if (t.channel.isNotEmpty())  o->setProperty ("channel", t.channel);
        if (t.insert.isNotEmpty())   o->setProperty ("insert", t.insert);

        o->setProperty ("param", t.param);
        return juce::var (o);
    }

    /** O plugin VST3 vivo de um alvo (para os nomes e o número de parâmetros), ou nullptr. */
    juce::AudioPluginInstance* pluginFor (AppServices& app, const AutomationTarget& t)
    {
        if (t.kind == "insert")   return app.insertPluginInstance (t.track, t.insert);
        if (t.kind == "channel")  return app.channelPlugin (t.channel);
        return nullptr;
    }

    /** Nome para a tela: "Voz · Volume", "Voz · Filtro · Frequência", "Lead · Cutoff". */
    juce::String describe (const Project& p, AppServices& app, const AutomationTarget& t)
    {
        juce::StringArray parts;

        if (t.kind == "channel")
        {
            if (auto* c = p.findChannel (t.channel))
                parts.add (c->name);
        }
        else if (auto* m = p.findMixer (t.track))
        {
            parts.add (m->name);

            if (t.kind == "mixer")
                parts.add (t.param == "volume" ? juce::String ("Volume") : juce::String ("Pan"));

            if (auto* ins = t.kind == "insert" ? m->findInsert (t.insert) : nullptr)
            {
                if (auto* info = dsp::findEffect (ins->type.toStdString()); info != nullptr && ins->kind == "whynot")
                {
                    parts.add (juce::String::fromUTF8 (info->name));

                    if (const int index = info->indexOf (t.param.toRawUTF8()); index >= 0)
                        parts.add (juce::String::fromUTF8 (info->params[(size_t) index].name));
                }
                else
                {
                    parts.add (ins->type);
                }
            }
        }

        if (auto* plugin = pluginFor (app, t))
        {
            const auto& list = plugin->getParameters();
            const int index = t.param.getIntValue();

            if (juce::isPositiveAndBelow (index, list.size()))
                parts.add (list[index]->getName (64));
        }

        return parts.joinIntoString (" · "_t);
    }

    juce::var targetInfo (const Project& p, AppServices& app, const AutomationTarget& t, const automation::Range& r,
                          float current, const juce::String& unit)
    {
        const char* scale = r.scale == automation::Range::Scale::log ? "log" : r.scale == automation::Range::Scale::fader ? "fader" : "linear";
        const auto* lane = p.laneFor (t);
        return object ({ { "target", targetVar (t) }, { "name", describe (p, app, t) }, { "min", r.min }, { "max", r.max },
                         { "value", current }, { "v", r.toNormalised (current) }, { "unit", unit }, { "scale", juce::String (scale) },
                         { "stepped", r.stepped }, { "lane", lane != nullptr ? juce::var (lane->id) : juce::var() } });
    }
}

void registerAutomationCommands (CommandRegistry& r)
{
    r.add ({ "automation.addLane", "Nova automação"_t,
             schema ({ { "target", "object", true, "O que a curva move: { kind: mixer | insert | channel, track | channel, insert?, param } (ver automation.targets)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto t = targetFrom (a["target"]);

                 if (! automation::rangeFor (p, t).has_value())
                     return CommandResult::fail ("Alvo de automação não encontrado (veja automation.targets)."_t);

                 if (auto* plugin = pluginFor (c.app, t); plugin != nullptr && t.param.getIntValue() >= plugin->getParameters().size())
                     return CommandResult::fail ("Esse plugin não tem o parâmetro "_t + t.param + ".");

                 // uma curva por parâmetro: se já existe, devolve a mesma
                 if (auto* existing = p.laneFor (t))
                     return CommandResult::success (object ({ { "lane", existing->id }, { "existed", true } }));

                 AutomationLane lane;
                 lane.id = makeId ("au");
                 lane.target = t;
                 p.automation.push_back (lane);
                 return CommandResult::success (object ({ { "lane", lane.id }, { "existed", false } }));
             } });

    r.add ({ "automation.setPoints", "Automação"_t,
             schema ({ { "lane", "string", true, "Id da curva (de automation.addLane)"_t },
                       { "points", "array", true, "Lista de { t, v }: tempo e posição do knob, 0 a 1. Em vez de v, pode mandar value, na unidade do parâmetro (dB, Hz...)"_t },
                       { "unit", "string", false, "Unidade de t: ticks (960 por tempo, padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* lane = p.findLane (params::string (a, "lane"));
                 auto* list = a["points"].getArray();

                 if (lane == nullptr)
                     return CommandResult::fail ("Curva de automação não encontrada."_t);

                 const auto range = automation::rangeFor (p, lane->target);

                 if (list == nullptr || ! range.has_value())
                     return CommandResult::fail ("Informe os pontos em \"points\"."_t);

                 lane->points.clear();

                 for (auto& pt : *list)
                 {
                     const float v = pt.hasProperty ("v") ? (float) field (pt, "v", 0.0)
                                                          : range->toNormalised ((float) field (pt, "value", range->min));
                     lane->points.push_back ({ toTicks (a, field (pt, "t", 0.0)), v });   // o normalise ordena, limita e tira repetidos
                 }

                 return CommandResult::success();
             } });

    r.add ({ "automation.removeLane", "Apagar automação"_t,
             schema ({ { "lane", "string", true, "Id da curva"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& lanes = c.model.data().automation;
                 const auto id = params::string (a, "lane");
                 const auto before = lanes.size();
                 std::erase_if (lanes, [&id] (const AutomationLane& l) { return l.id == id; });
                 return lanes.size() < before ? CommandResult::success() : CommandResult::fail ("Curva de automação não encontrada."_t);
             } });

    r.add ({ "automation.list", "Curvas de automação"_t, schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 const auto& p = c.model.get();
                 juce::Array<juce::var> lanes;

                 for (auto& lane : p.automation)
                 {
                     const auto range = automation::rangeFor (p, lane.target).value_or (automation::Range());
                     juce::Array<juce::var> points;

                     for (auto& pt : lane.points)
                         points.add (object ({ { "t", pt.t }, { "v", pt.v }, { "value", range.fromNormalised (pt.v) } }));

                     lanes.add (object ({ { "id", lane.id }, { "target", targetVar (lane.target) },
                                          { "name", describe (p, c.app, lane.target) }, { "points", points } }));
                 }

                 return CommandResult::success (object ({ { "lanes", lanes } }));
             } });

    r.add ({ "automation.targets", "Parâmetros automatizáveis"_t,
             schema ({ { "track", "string", false, "Id da trilha do mixer: volume, pan e os parâmetros dos efeitos dela"_t },
                       { "channel", "string", false, "Id do canal: os parâmetros do instrumento VST3 dele"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto& p = c.model.get();
                 juce::Array<juce::var> list;

                 if (params::has (a, "track"))
                 {
                     auto* m = p.findMixer (params::string (a, "track"));

                     if (m == nullptr)
                         return CommandResult::fail ("Trilha do mixer não encontrada."_t);

                     AutomationTarget t;
                     t.kind = "mixer";
                     t.track = m->id;
                     t.param = "volume";
                     list.add (targetInfo (p, c.app, t, *automation::rangeFor (p, t), m->volumeDb, "dB"));
                     t.param = "pan";
                     list.add (targetInfo (p, c.app, t, *automation::rangeFor (p, t), m->pan, {}));

                     for (auto& ins : m->inserts)
                     {
                         t.kind = "insert";
                         t.insert = ins.id;

                         if (auto* info = dsp::findEffect (ins.type.toStdString()); info != nullptr && ins.kind == "whynot")
                         {
                             for (auto& param : info->params)
                             {
                                 if (param.hidden)
                                     continue;

                                 t.param = param.id;
                                 auto it = ins.params.find (param.id);
                                 list.add (targetInfo (p, c.app, t, automation::rangeOf (param), it != ins.params.end() ? it->second : param.def,
                                                       juce::String::fromUTF8 (param.unit)));
                             }
                         }
                         else if (auto* plugin = c.app.insertPluginInstance (m->id, ins.id))
                         {
                             const auto& pluginParams = plugin->getParameters();

                             for (int i = 0; i < pluginParams.size(); ++i)
                             {
                                 t.param = juce::String (i);
                                 list.add (targetInfo (p, c.app, t, automation::pluginRange(), pluginParams[i]->getValue(), {}));
                             }
                         }
                     }
                 }
                 else if (params::has (a, "channel"))
                 {
                     auto* ch = p.findChannel (params::string (a, "channel"));
                     auto* plugin = ch != nullptr ? c.app.channelPlugin (ch->id) : nullptr;

                     if (ch == nullptr || ! ch->isPlugin())
                         return CommandResult::fail ("Canal de instrumento VST3 não encontrado."_t);

                     if (plugin == nullptr)
                         return CommandResult::fail ("O plugin desse canal não está aberto."_t);

                     AutomationTarget t;
                     t.kind = "channel";
                     t.channel = ch->id;
                     const auto& pluginParams = plugin->getParameters();

                     for (int i = 0; i < pluginParams.size(); ++i)
                     {
                         t.param = juce::String (i);
                         list.add (targetInfo (p, c.app, t, automation::pluginRange(), pluginParams[i]->getValue(), {}));
                     }
                 }
                 else
                 {
                     return CommandResult::fail ("Informe \"track\" ou \"channel\"."_t);
                 }

                 return CommandResult::success (object ({ { "targets", list } }));
             } });
}

} // namespace whynot
