#include "MidiLearnMenu.h"
#include "../app/AppController.h"
#include "fx/FxWidgets.h"

namespace whynot::midilearn
{

namespace
{
    juce::var targetArgs (const MidiLink& t)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", t.kind);
        o->setProperty ("track", t.track);

        if (t.insert.isNotEmpty())
            o->setProperty ("insert", t.insert);

        o->setProperty ("param", t.param);
        return juce::var (o);
    }
}

const MidiLink* linkFor (const Project& p, const MidiLink& target)
{
    for (auto& l : p.midiMap)
        if (l.sameTarget (target))
            return &l;

    return nullptr;
}

void addItems (juce::PopupMenu& menu, AppController& app, const MidiLink& target, std::vector<std::function<void()>>& actions)
{
    auto add = [&actions] (std::function<void()> f) { actions.push_back (std::move (f)); return (int) actions.size(); };
    const auto* link = linkFor (app.getModel().get(), target);
    const bool waiting = app.isLearningMidi() && app.midiLearnTarget().sameTarget (target);

    const auto learnText = link != nullptr ? "Controlador: CC "_t + juce::String (link->cc)
                                                 + (link->channel > 0 ? " (canal " + juce::String (link->channel) + ")" : juce::String())
                                                 + " · trocar pelo próximo knob"_t
                                           : "Aprender do controlador MIDI"_t;
    menu.addItem (add ([&app, target] { app.run ("midi.learn", targetArgs (target)); }), learnText, ! waiting);

    if (waiting)
        menu.addItem (add ([&app] { app.run ("midi.cancelLearn", juce::var (new juce::DynamicObject())); }),
                      "Parar de aprender (esperando um knob do controlador)"_t);

    if (link != nullptr)
        menu.addItem (add ([&app, target] { app.run ("midi.unmap", targetArgs (target)); }), "Esquecer o controlador"_t);
}

void showMenu (AppController& app, const MidiLink& target, const juce::String& title, juce::Component* anchor)
{
    auto actions = std::make_shared<std::vector<std::function<void()>>>();
    juce::PopupMenu menu;
    menu.addSectionHeader (fxui::caps (title));
    addItems (menu, app, target, *actions);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [actions] (int chosen)
    {
        if (chosen > 0 && chosen <= (int) actions->size())
            (*actions)[(size_t) chosen - 1]();
    });
}

} // namespace whynot::midilearn
