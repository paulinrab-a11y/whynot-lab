// Tom do beat (pedido do usuário de 21/10, noite): audio.detectKey diz o tom de um beat do projeto (ou de
// um WAV/MP3), a confiança, as alternativas com o relativo explicado e o acorde de cada compasso, que é
// onde o 808 casa. Só lê: quem muda o tom do projeto é o project.setKey. Dono: frente Tom do beat.

#include "CommandRegistry.h"
#include "../engine/BeatKey.h"

namespace whynot
{

void registerBeatCommands (CommandRegistry& r)
{
    using params::schema;

    r.add ({ "audio.detectKey", "Tom do beat"_t,
             schema ({ { "channel", "string", false, "Canal de áudio (ou sampler com sample de 8 s ou mais); padrão = o beat principal do projeto"_t },
                       { "path", "string", false, "Um arquivo WAV ou MP3 no lugar de um canal"_t },
                       { "start", "number", false, "Com path: onde o arquivo começa na música, em ticks (padrão 0), para os acordes por compasso"_t },
                       { "wait", "boolean", false, "Esperar a análise acabar (padrão true, até 20 s). false volta na hora, com state \"analyzing\" se ainda não acabou"_t } }),
             false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* analyzer = c.app.beatKey();

                 if (analyzer == nullptr)
                     return CommandResult::fail ("O tom do beat não está disponível neste modo."_t);

                 const auto& p = c.model.get();
                 BeatSource source;

                 if (params::has (a, "path"))
                 {
                     source.file = juce::File (params::string (a, "path"));

                     if (! source.file.existsAsFile())
                         return CommandResult::fail ("Arquivo não encontrado: "_t + source.file.getFullPathName());

                     source.name = source.file.getFileNameWithoutExtension();
                     source.startTicks = juce::jlimit (0, maxTimelineTicks, (int) params::number (a, "start", 0));
                     source.lengthTicks = maxTimelineTicks;
                 }
                 else
                 {
                     const auto beats = analyzer->beatsOf (c.model);
                     const auto wanted = params::string (a, "channel");

                     if (wanted.isNotEmpty())
                     {
                         auto it = std::find_if (beats.begin(), beats.end(), [&] (const BeatSource& b) { return b.channel == wanted; });

                         if (it == beats.end())
                             return CommandResult::fail (p.findChannel (wanted) == nullptr
                                                           ? "Canal não encontrado."_t
                                                           : "Esse canal não tem um beat: precisa ser um canal de áudio, ou um sampler com um sample de 8 s ou mais."_t);
                         source = *it;
                     }
                     else if (beats.empty())
                     {
                         return CommandResult::fail ("Nenhum beat no projeto. Ponha um WAV ou MP3 na playlist (ou num canal de áudio) e peça de novo."_t);
                     }
                     else
                     {
                         source = beats.front();
                     }
                 }

                 analyzer->request (source.file);
                 const auto result = params::boolean (a, "wait", true) ? analyzer->waitFor (source.file, 20000)
                                                                       : (analyzer->isPending (source.file) ? nullptr : analyzer->find (source.file));

                 if (result == nullptr)
                 {
                     auto o = new juce::DynamicObject();
                     o->setProperty ("state", "analyzing");
                     o->setProperty ("file", source.file.getFullPathName());
                     o->setProperty ("channel", source.channel);
                     return CommandResult::success (juce::var (o));
                 }

                 if (! result->ok)
                     return CommandResult::fail (result->error.isNotEmpty() ? result->error : "A análise foi interrompida."_t);

                 return CommandResult::success (describeBeatKey (*result, p, source));
             } });
}

} // namespace whynot
