#include "CommandRegistry.h"
#include "../audio/PianoPresets.h"
#include "../engine/PianoLibrary.h"

// WHYNOT Piano (pedido do usuário de 21/10): o canal, o timbre e a biblioteca de samples.
// O canal é um sampler com instrument "piano"; o som vem do Salamander convertido (tools/piano).

namespace whynot
{

namespace
{
    using params::schema;

    juce::String utf8 (const char* s)   { return juce::String::fromUTF8 (s); }

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    juce::String presetIds()
    {
        juce::StringArray ids;

        for (auto& p : audio::pianoPresets)
            ids.add (p.id);

        return ids.joinIntoString (", ");
    }

    const audio::PianoPreset* presetFrom (const juce::var& a, juce::String& error)
    {
        const auto id = params::string (a, "preset", "grand");
        const auto* p = audio::findPianoPreset (id.toRawUTF8());

        if (p == nullptr)
            error = "Timbre do piano desconhecido: "_t + id + ". Use " + presetIds() + ".";

        return p;
    }

    juce::String channelName (const audio::PianoPreset& p)
    {
        return juce::String (p.id) == "grand" ? juce::String ("Piano") : "Piano " + utf8 (p.name);
    }
}

void registerPianoCommands (CommandRegistry& r)
{
    r.add ({ "channel.addPiano", "Adicionar WHYNOT Piano",
             schema ({ { "preset", "string", false, "Timbre: grand (padrão), suave ou lofi"_t },
                       { "name", "string", false, "Nome do canal; padrão = Piano"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 juce::String error;
                 const auto* preset = presetFrom (a, error);

                 if (preset == nullptr)
                     return CommandResult::fail (error);

                 auto& p = c.model.data();
                 Channel ch;
                 ch.id = makeId ("ch");
                 ch.kind = "sampler";
                 ch.instrument = "piano";
                 ch.instrumentPreset = preset->id;
                 ch.name = params::string (a, "name", channelName (*preset)).trim();
                 ch.colour = channelColourForIndex ((int) p.channels.size());
                 ch.oneShot = false;   // o piano respeita a duração da nota (e o pedal)
                 ch.rootKey = 60;

                 if (ch.name.isEmpty())
                     ch.name = "Piano";

                 MixerTrack mx;
                 mx.id = makeId ("mx");
                 mx.name = ch.name;
                 mx.colour = ch.colour;
                 mx.autoFor = ch.id;
                 mx.volumeDb = -6.0f;   // headroom, como os outros canais
                 ch.output = mx.id;

                 p.channels.push_back (ch);
                 auto at = std::find_if (p.mixer.begin(), p.mixer.end(), [] (const MixerTrack& m) { return m.isMaster(); });
                 p.mixer.insert (at, mx);

                 return CommandResult::success (object ({ { "channel", ch.id }, { "mixerTrack", mx.id }, { "preset", preset->id } }));
             } });

    r.add ({ "piano.setPreset", "Trocar timbre do piano",
             schema ({ { "channel", "string", true, "Id do canal WHYNOT Piano" },
                       { "preset", "string", true, "grand, suave ou lofi" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 if (! ch->isPiano())
                     return CommandResult::fail ("Este canal não é um WHYNOT Piano."_t);

                 juce::String error;
                 const auto* preset = presetFrom (a, error);

                 if (preset == nullptr)
                     return CommandResult::fail (error);

                 ch->instrumentPreset = preset->id;
                 return CommandResult::success (object ({ { "preset", preset->id }, { "name", utf8 (preset->name) } }));
             } });

    r.add ({ "piano.listPresets", "Listar timbres do piano",
             schema ({}), false,
             [] (CommandContext&, const juce::var&)
             {
                 juce::Array<juce::var> list;

                 for (auto& p : audio::pianoPresets)
                     list.add (object ({ { "id", p.id }, { "name", utf8 (p.name) }, { "toneHz", p.toneHz },
                                         { "width", p.width }, { "hammer", p.hammer } }));

                 return CommandResult::success (object ({ { "presets", list } }));
             } });

    r.add ({ "piano.status", "Biblioteca do piano",
             schema ({}), false,
             [] (CommandContext&, const juce::var&)
             {
                 juce::String error;
                 const auto lib = piano::load (error);

                 if (lib == nullptr)
                     return CommandResult::success (object ({ { "found", false }, { "error", error } }));

                 int attacks = 0;

                 for (auto& z : lib->set.zones)
                     if (! z.release)
                         ++attacks;

                 return CommandResult::success (object ({ { "found", true },
                                                          { "folder", lib->folder.getFullPathName() },
                                                          { "name", lib->name },
                                                          { "credit", lib->credit },
                                                          { "license", lib->license },
                                                          { "zones", (int) lib->set.zones.size() },
                                                          { "noteZones", attacks },
                                                          { "skippedZones", lib->skippedZones },
                                                          { "memoryMB", juce::roundToInt ((double) lib->headBytes() / (1024.0 * 1024.0)) } }));
             } });
}

} // namespace whynot
