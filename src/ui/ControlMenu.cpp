#include "ControlMenu.h"
#include "../app/AppController.h"
#include "../automation/AutomationTarget.h"

namespace whynot::controlmenu
{

namespace
{
    juce::var targetVar (const AutomationTarget& t)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", t.kind);

        if (t.track.isNotEmpty())    o->setProperty ("track", t.track);
        if (t.channel.isNotEmpty())  o->setProperty ("channel", t.channel);
        if (t.insert.isNotEmpty())   o->setProperty ("insert", t.insert);

        o->setProperty ("param", t.param);
        return juce::var (o);
    }

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto* o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }
}

float currentPosition (const Project& p, const AutomationTarget& t)
{
    const auto range = automation::rangeFor (p, t);

    if (! range.has_value())
        return -1.0f;

    if (t.kind == "mixer")
    {
        if (const auto* m = p.findMixer (t.track))
            return range->toNormalised (t.param == "pan" ? m->pan : m->volumeDb);
    }
    else if (t.kind == "insert")
    {
        if (const auto* m = p.findMixer (t.track))
            if (const auto* ins = m->findInsert (t.insert))
                if (const auto* info = dsp::findEffect (ins->type.toStdString()))
                    if (const int i = info->indexOf (t.param.toRawUTF8()); i >= 0)
                    {
                        const auto it = ins->params.find (t.param);
                        return range->toNormalised (it != ins->params.end() ? it->second : info->params[(size_t) i].def);
                    }
    }

    return 0.5f;   // um VST3 (a posição vem do plugin): o meio
}

juce::String automate (AppController& app, const AutomationTarget& t)
{
    const float now = currentPosition (app.getModel().get(), t);

    if (now < 0.0f)
        return {};

    CommandRegistry::ScopedGesture gesture (app.getCommands(), "Automatizar"_t);   // a curva e o ponto: um passo só
    const auto added = app.run ("automation.addLane", object ({ { "target", targetVar (t) } }));

    if (! added.ok)
        return {};

    const auto lane = added.value["lane"].toString();

    if (! (bool) added.value["existed"])
    {
        juce::Array<juce::var> points;
        points.add (object ({ { "t", 0 }, { "v", now } }));
        app.run ("automation.setPoints", object ({ { "lane", lane }, { "points", points } }));
    }

    return lane;
}

void addAutomation (juce::PopupMenu& menu, AppController& app, const AutomationTarget& t, std::vector<std::function<void()>>& actions)
{
    const auto* lane = app.getModel().get().laneFor (automation::tidy (t));
    auto add = [&actions] (std::function<void()> f) { actions.push_back (std::move (f)); return (int) actions.size(); };

    if (lane == nullptr)
    {
        menu.addItem (add ([&app, t]
                           {
                               const bool ok = automate (app, t).isNotEmpty();
                               app.notice (ok ? "Automação criada embaixo da playlist (F5): clique na curva para pôr pontos; ela toca no modo SONG."_t
                                              : "Este controle não aceita automação."_t,
                                           ! ok);
                           }),
                      "Automatizar (curva embaixo da playlist)"_t);
    }
    else
    {
        const auto id = lane->id;
        menu.addItem (add ([&app, id] { app.run ("automation.removeLane", object ({ { "lane", id } })); }),
                      "Apagar a automação ("_t + juce::String ((int) lane->points.size()) + (lane->points.size() == 1 ? " ponto)" : " pontos)"));
    }
}

} // namespace whynot::controlmenu
