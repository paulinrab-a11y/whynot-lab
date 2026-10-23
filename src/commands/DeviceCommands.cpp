#include "CommandRegistry.h"
#include "../app/KeyMap.h"

namespace whynot
{

namespace
{
    using params::schema;
}

void registerDeviceCommands (CommandRegistry& r)
{
    r.add ({ "audio.devices", "Placa de som", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 return CommandResult::success (c.app.audioDeviceInfo());
             } });

    r.add ({ "audio.setDevice", "Escolher placa de som",
             schema ({ { "type", "string", false, "Tipo de driver: \"Windows Audio\", \"ASIO\"... (padrão = o atual)"_t },
                       { "device", "string", false, "Nome do aparelho de saída (padrão = o primeiro do tipo)"_t },
                       { "sampleRate", "number", false, "Taxa de amostragem em Hz (padrão = a atual)"_t },
                       { "bufferSize", "integer", false, "Buffer em amostras: menor = menos atraso, mais CPU (padrão = o atual)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto res = c.app.setAudioDevice (params::string (a, "type"), params::string (a, "device"),
                                                        params::number (a, "sampleRate", 0.0), (int) params::number (a, "bufferSize", 0));
                 return res.wasOk() ? CommandResult::success (c.app.audioDeviceInfo()) : CommandResult::fail (res.getErrorMessage());
             } });

    r.add ({ "midi.inputs", "Entradas MIDI", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto o = new juce::DynamicObject();
                 o->setProperty ("inputs", c.app.midiInputs());
                 o->setProperty ("liveChannel", c.app.liveChannel());
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "midi.setInputEnabled", "Ligar entrada MIDI",
             schema ({ { "input", "string", true, "Nome ou id da entrada MIDI"_t },
                       { "on", "boolean", false, "true = liga (padrão)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto name = params::string (a, "input");
                 return c.app.setMidiInputEnabled (name, params::boolean (a, "on", true))
                          ? CommandResult::success()
                          : CommandResult::fail ("Entrada MIDI não encontrada: "_t + name);
             } });

    r.add ({ "midi.setLiveChannel", "Canal que toca ao vivo",
             schema ({ { "channel", "string", true, "Id do canal que recebe o controlador e o teclado do computador"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto id = params::string (a, "channel");
                 const auto* ch = c.model.get().findChannel (id);

                 if (ch == nullptr || ch->isAudio())
                     return CommandResult::fail ("Canal não encontrado (canais de áudio não tocam notas)."_t);

                 c.app.setLiveChannel (id);
                 return CommandResult::success();
             } });

    r.add ({ "live.note", "Tocar nota ao vivo",
             schema ({ { "key", "integer", true, "Nota MIDI (60 = Dó central)"_t },
                       { "on", "boolean", false, "true = aperta (padrão), false = solta"_t },
                       { "velocity", "integer", false, "Força 1-127 (padrão 100)"_t },
                       { "channel", "string", false, "Id do canal; padrão = o canal ao vivo"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const int key = (int) params::number (a, "key", -1);

                 if (! juce::isPositiveAndBelow (key, 128))
                     return CommandResult::fail ("A nota precisa estar entre 0 e 127.");

                 const bool ok = c.app.liveNote (params::string (a, "channel"), key,
                                                 juce::jlimit (1, 127, (int) params::number (a, "velocity", 100)),
                                                 params::boolean (a, "on", true));
                 return ok ? CommandResult::success() : CommandResult::fail ("Esse canal não toca notas agora."_t);
             } });

    //==========================================================================
    // Atalhos de teclado
    r.add ({ "shortcuts.list", "Atalhos de teclado", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* keys = c.app.keyMap();
                 return keys != nullptr ? CommandResult::success (keys->toVar()) : CommandResult::fail ("Sem atalhos neste modo.");
             } });

    r.add ({ "shortcuts.setPreset", "Escolher preset de atalhos",
             schema ({ { "preset", "string", true, "fl (estilo FL Studio) ou whynot (Ctrl+N novo projeto)" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* keys = c.app.keyMap();
                 const auto name = params::string (a, "preset");

                 if (keys == nullptr || ! KeyMap::presetNames().contains (name))
                     return CommandResult::fail ("Preset desconhecido: "_t + name + " (use fl ou whynot).");

                 keys->setPreset (name);
                 c.app.shortcutsChanged();
                 return CommandResult::success (keys->toVar());
             } });

    r.add ({ "shortcuts.set", "Trocar atalho",
             schema ({ { "action", "string", true, "Id da ação (ver shortcuts.list)"_t },
                       { "keys", "array", false, "Teclas, como \"Ctrl+Shift+R\" ou \"Alt+F8\"; vazio = sem atalho; ausente = volta ao preset"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* keys = c.app.keyMap();
                 const auto action = params::string (a, "action");

                 if (keys == nullptr || KeyMap::findAction (action) == nullptr)
                     return CommandResult::fail ("Ação desconhecida: "_t + action);

                 if (! params::has (a, "keys"))
                 {
                     keys->resetAction (action);
                 }
                 else
                 {
                     juce::Array<juce::KeyPress> list;

                     if (auto* items = a["keys"].getArray())
                     {
                         for (auto& k : *items)
                         {
                             const auto kp = KeyMap::parse (k.toString());

                             if (! kp.isValid())
                                 return CommandResult::fail ("Não entendi a tecla \""_t + k.toString() + "\".");

                             list.add (kp);
                         }
                     }

                     keys->setKeys (action, list);
                 }

                 c.app.shortcutsChanged();
                 return CommandResult::success (keys->toVar());
             } });
}

} // namespace whynot
