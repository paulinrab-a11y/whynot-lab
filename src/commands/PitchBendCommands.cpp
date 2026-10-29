// Pitch bend e mod wheel desenhados na nota (item 17 da pesquisa das comunidades, 26/10): a curva de uma nota
// (pianoroll.setNoteCurve) e o alcance do pitch wheel do canal (channel.setBendRange). As contas ficam em
// src/model/NoteCurves.h; a raia Pitch/Mod do piano roll e a IA passam por aqui.

#include "CommandRegistry.h"
#include "../model/NoteCurves.h"

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
}

void registerPitchBendCommands (CommandRegistry& r)
{
    r.add ({ "pianoroll.setNoteCurve", "Desenhar pitch bend ou mod wheel"_t,
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "id", "integer", true, "Id da nota"_t },
                       { "curve", "string", true, "pitch (pitch bend, em semitons de -24 a 24) ou mod (mod wheel, de 0 a 1)"_t },
                       { "points", "array", true, "Lista de { t, v }: o tempo desde o começo da nota e o valor. Entre dois pontos a curva anda reta; "
                                                  "antes do primeiro vale o primeiro, depois do último, o último. Lista vazia tira a curva"_t },
                       { "unit", "string", false, "Unidade de t: ticks (960 por tempo, padrão), steps, beats ou bars"_t },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto patternId = params::string (a, "pattern");
                 auto* pat = patternId.isEmpty() ? p.currentPatternPtr() : p.findPattern (patternId);
                 const auto channel = params::string (a, "channel");
                 bool kindOk = false;
                 const auto kind = curves::kindFromName (params::string (a, "curve"), kindOk);

                 if (pat == nullptr)                        return CommandResult::fail ("Padrão não encontrado."_t);
                 if (p.findChannel (channel) == nullptr)    return CommandResult::fail ("Canal não encontrado: "_t + channel);
                 if (! kindOk)                              return CommandResult::fail ("Use curve \"pitch\" ou \"mod\"."_t);
                 if (! a["points"].isArray())               return CommandResult::fail ("Informe os pontos em \"points\" (lista de { t, v })."_t);

                 auto& notes = pat->notes[channel];
                 const int id = (int) params::number (a, "id", -1);
                 auto it = std::find_if (notes.begin(), notes.end(), [id] (const Note& n) { return n.id == id; });

                 if (it == notes.end())
                     return CommandResult::fail ("Nota não encontrada: "_t + juce::String (id));

                 int bad = 0;
                 auto pts = curves::pointsFromVar (a["points"], unitScale (a), kind, bad);

                 if (bad > 0)
                     return CommandResult::fail ("Cada ponto precisa de t (0 ou mais) e v numéricos."_t);

                 curves::of (*it, kind) = std::move (pts);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("points", (int) curves::of (*it, kind).size());
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "channel.setBendRange", "Alcance do pitch bend"_t,
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "semitones", "integer", true, "Quantos semitons o pitch wheel inteiro dobra, de 1 a 24. No WHYNOT Synth muda o parâmetro "
                                                       "Pitch bend dele; no sampler e no WHYNOT Piano, o alcance do canal; num VST3, use o mesmo valor do plugin"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr || ch->isAudio())
                     return CommandResult::fail ("Canal não encontrado (ou é de áudio, sem notas)."_t);

                 const double st = params::number (a, "semitones", -1.0);

                 if (! std::isfinite (st) || st < 1.0 || st > 24.0)
                     return CommandResult::fail ("O alcance vai de 1 a 24 semitons."_t);

                 if (ch->isSynth())
                     ch->synthParams["bend"] = (float) juce::roundToInt (st);
                 else
                     ch->bendRange = juce::roundToInt (st);

                 return CommandResult::success();
             } });
}

} // namespace whynot
