#include "CommandRegistry.h"
#include "../automation/Macros.h"
#include "../dsp/FxCatalog.h"

// MIDI learn (bloco 4d, 23/10): um knob ou fader do controlador ligado a um controle do app. Os links ficam no projeto
// (Project::midiMap): o volume ou o pan de uma trilha, um parâmetro de efeito nativo ou um macro do rack. O CC que chega
// move o alvo (AppController::handleMidiControl); aqui ficam o aprender, ligar e esquecer à mão e a lista.

namespace whynot
{

namespace
{
    using params::schema;

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    /** O alvo dos parâmetros (kind, track, insert, param) num link sem CC; devolve o erro, ou vazio. */
    juce::String targetFrom (const Project& p, const juce::var& a, MidiLink& out)
    {
        out.kind = params::string (a, "kind");
        out.track = params::string (a, "track");
        out.insert = params::string (a, "insert");
        out.param = params::string (a, "param");

        const auto* m = p.findMixer (out.track);

        if (m == nullptr)
            return "Trilha de mixer não encontrada."_t;

        if (out.kind == "mixer")
        {
            out.insert = {};
            return out.param == "volume" || out.param == "pan" ? juce::String() : "No mixer, param é volume ou pan."_t;
        }

        if (out.kind == "macro")
        {
            out.insert = {};
            const int n = out.param.getIntValue();
            return n >= 1 && n <= maxMacros && out.param == juce::String (n) ? juce::String() : "No macro, param é o número dele, de 1 a 8."_t;
        }

        if (out.kind == "insert")
        {
            const auto* ins = m->findInsert (out.insert);

            if (ins == nullptr)
                return "Efeito não encontrado nesta trilha."_t;

            const auto* info = ins->kind == "whynot" ? dsp::findEffect (ins->type.toStdString()) : nullptr;

            if (info == nullptr)
                return "O controlador move só os efeitos nativos da WHYNOT; num VST3, use os controles do próprio plugin."_t;

            const int i = info->indexOf (out.param.toRawUTF8());
            return i >= 0 && ! info->params[(size_t) i].hidden ? juce::String () : "Parâmetro desconhecido em "_t + juce::String::fromUTF8 (info->name) + ": " + out.param;
        }

        return "kind é mixer (volume ou pan), insert (um parâmetro de efeito) ou macro."_t;
    }

    juce::var linkToVar (const Project& p, const MidiLink& l)
    {
        const auto* m = p.findMixer (l.track);
        auto out = object ({ { "cc", l.cc }, { "channel", l.channel }, { "kind", l.kind }, { "track", l.track },
                             { "trackName", m != nullptr ? m->name : juce::String() }, { "param", l.param } });

        if (l.insert.isNotEmpty())
            out.getDynamicObject()->setProperty ("insert", l.insert);

        return out;
    }

    /** O CC (0 a 127) e o canal MIDI (1 a 16; 0 ou ausente = qualquer). */
    juce::String controlFrom (const juce::var& a, MidiLink& link)
    {
        const double cc = params::number (a, "cc", -1.0), channel = params::number (a, "channel", 0.0);

        if (! (cc >= 0.0 && cc <= 127.0) || cc != std::floor (cc))
            return "cc vai de 0 a 127."_t;

        if (! (channel >= 0.0 && channel <= 16.0) || channel != std::floor (channel))
            return "channel vai de 1 a 16 (0 ou ausente = qualquer canal)."_t;

        link.cc = (int) cc;
        link.channel = (int) channel;
        return {};
    }

}

void registerMidiLearnCommands (CommandRegistry& r)
{
    r.add ({ "midi.learn", "Aprender do controlador",
             schema ({ { "kind", "string", true, "mixer (volume ou pan), insert (parâmetro de efeito nativo) ou macro"_t },
                       { "track", "string", true, "Id da trilha de mixer" },
                       { "insert", "string", false, "Id do efeito (com kind insert)" },
                       { "param", "string", true, "volume ou pan; o id do parâmetro do efeito; ou o número do macro (1 a 8)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 MidiLink target;

                 if (const auto error = targetFrom (c.model.get(), a, target); error.isNotEmpty())
                     return CommandResult::fail (error);

                 c.app.learnMidi (target);
                 return CommandResult::success (object ({ { "learning", true },
                                                          { "note", "Mexa um knob ou fader do controlador em até 20 s: o primeiro que mexer fica ligado."_t } }));
             } });

    r.add ({ "midi.cancelLearn", "Parar de aprender", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 const bool was = c.app.isLearningMidi();
                 c.app.cancelMidiLearn();
                 return CommandResult::success (object ({ { "wasLearning", was } }));
             } });

    r.add ({ "midi.map", "Ligar ao controlador",
             schema ({ { "cc", "number", true, "O número do CC, de 0 a 127"_t },
                       { "channel", "number", false, "O canal MIDI, de 1 a 16 (0 ou ausente = qualquer)"_t },
                       { "kind", "string", true, "mixer (volume ou pan), insert (parâmetro de efeito nativo) ou macro"_t },
                       { "track", "string", true, "Id da trilha de mixer" },
                       { "insert", "string", false, "Id do efeito (com kind insert)" },
                       { "param", "string", true, "volume ou pan; o id do parâmetro do efeito; ou o número do macro (1 a 8)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 MidiLink link;

                 if (const auto error = targetFrom (p, a, link); error.isNotEmpty())
                     return CommandResult::fail (error);

                 if (const auto error = controlFrom (a, link); error.isNotEmpty())
                     return CommandResult::fail (error);

                 // um CC move um alvo só, e um alvo tem um CC só: o que havia sai
                 const auto replaced = std::erase_if (p.midiMap, [&link] (const MidiLink& l) { return l.sameControl (link) || l.sameTarget (link); });

                 if ((int) p.midiMap.size() >= maxMidiLinks)
                     return CommandResult::fail ("O projeto já tem "_t + juce::String (maxMidiLinks) + " controles ligados.");

                 p.midiMap.push_back (link);
                 auto out = linkToVar (p, link);
                 out.getDynamicObject()->setProperty ("replaced", (int) replaced);
                 return CommandResult::success (out);
             } });

    r.add ({ "midi.unmap", "Esquecer o controlador",
             schema ({ { "cc", "number", false, "Esquecer este CC (com channel, só nesse canal)"_t },
                       { "channel", "number", false, "O canal MIDI do cc" },
                       { "kind", "string", false, "Ou o alvo: mixer, insert ou macro" },
                       { "track", "string", false, "Id da trilha do alvo" },
                       { "insert", "string", false, "Id do efeito do alvo" },
                       { "param", "string", false, "volume, pan, o id do parâmetro ou o número do macro"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 size_t removed = 0;

                 if (params::has (a, "cc"))
                 {
                     MidiLink control;

                     if (const auto error = controlFrom (a, control); error.isNotEmpty())
                         return CommandResult::fail (error);

                     const bool anyChannel = ! params::has (a, "channel");
                     removed = std::erase_if (p.midiMap, [&] (const MidiLink& l)
                     {
                         return l.cc == control.cc && (anyChannel || l.channel == control.channel);
                     });
                 }
                 else
                 {
                     MidiLink target;

                     if (const auto error = targetFrom (p, a, target); error.isNotEmpty())
                         return CommandResult::fail (error);

                     removed = std::erase_if (p.midiMap, [&target] (const MidiLink& l) { return l.sameTarget (target); });
                 }

                 if (removed == 0)
                     return CommandResult::fail ("Nada do controlador está ligado aí."_t);

                 return CommandResult::success (object ({ { "removed", (int) removed } }));
             } });

    r.add ({ "midi.mappings", "Ver os controles ligados", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 const auto& p = c.model.get();
                 juce::Array<juce::var> list;

                 for (auto& l : p.midiMap)
                     list.add (linkToVar (p, l));

                 return CommandResult::success (object ({ { "mappings", list }, { "learning", c.app.isLearningMidi() } }));
             } });
}

} // namespace whynot
