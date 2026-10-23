#include "CommandRegistry.h"
#include "../engine/Exporter.h"
#include "../engine/MidiExport.h"

namespace whynot
{

namespace
{
    using params::schema;

    CommandResult fromResult (const juce::Result& r, juce::var value = {})
    {
        return r.wasOk() ? CommandResult::success (std::move (value)) : CommandResult::fail (r.getErrorMessage());
    }

    juce::var pathResult (const juce::File& f)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("path", f.getFullPathName());
        return juce::var (o);
    }

    // "what" e "loops" valem igual para WAV, MP3, MIDI e stems
    bool songFrom (CommandContext& c, const juce::var& a)
    {
        return params::string (a, "what", c.model.get().songMode ? "song" : "pattern") == "song";
    }

    int loopsFrom (const juce::var& a)   { return juce::jlimit (1, 64, (int) params::number (a, "loops", 4)); }

    // "from"/"to" (ticks): só um trecho da música; precisa de "background": true
    bool rangeFrom (const juce::var& a, AppServices::BackgroundExport& request)
    {
        if (! params::has (a, "from") && ! params::has (a, "to"))
            return false;

        request.fromTicks = juce::jmax (0, (int) params::number (a, "from", 0));
        request.toTicks = juce::jmax (0, (int) params::number (a, "to", 0));
        return true;
    }

    // "background": true começa a exportação e volta na hora; o andamento sai no export.status
    CommandResult startInBackground (CommandContext& c, AppServices::BackgroundExport request)
    {
        const auto r = c.app.startBackgroundExport (request);

        if (r.failed())
            return CommandResult::fail (r.getErrorMessage());

        return CommandResult::success (c.app.backgroundExportStatus());
    }
}

void registerExportCommands (CommandRegistry& r)
{
    r.add ({ "project.exportMp3", "Exportar MP3",
             schema ({ { "path", "string", true, "Arquivo .mp3 de saída"_t },
                       { "what", "string", false, "pattern (padrão atual) ou song (playlist inteira); padrão = o modo PAT/SONG atual"_t },
                       { "loops", "integer", false, "Padrão: quantas vezes repetir (padrão 4)"_t },
                       { "bitrate", "integer", false, "kbps em taxa constante: 128, 192, 256 ou 320 (padrão 320)"_t },
                       { "vbr", "boolean", false, "Taxa variável (qualidade máxima do LAME) em vez de constante"_t },
                       { "background", "boolean", false, "true = começa e volta na hora; o andamento sai no export.status"_t },
                       { "from", "number", false, "Só um trecho da música (SONG): início em ticks; precisa de background"_t },
                       { "to", "number", false, "Fim do trecho, em ticks"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const juce::File file (params::string (a, "path"));

                 if (file == juce::File())
                     return CommandResult::fail ("Escolha onde salvar o MP3.");

                 const int bitrate = juce::jlimit (32, 320, (int) params::number (a, "bitrate", 320));
                 const auto out = file.withFileExtension ("mp3");

                 if (params::boolean (a, "background", false))
                 {
                     AppServices::BackgroundExport request;
                     request.kind = "mp3";
                     request.target = out;
                     request.loops = loopsFrom (a);
                     request.song = songFrom (c, a);
                     request.bitrateKbps = bitrate;
                     request.vbr = params::boolean (a, "vbr", false);
                     rangeFrom (a, request);
                     return startInBackground (c, request);
                 }

                 if (params::has (a, "from") || params::has (a, "to"))
                     return CommandResult::fail ("Exportar um trecho precisa de \"background\": true."_t);

                 return fromResult (c.app.exportMp3 (out, loopsFrom (a), songFrom (c, a), bitrate, params::boolean (a, "vbr", false)),
                                    pathResult (out));
             } });

    r.add ({ "project.exportMidi", "Exportar MIDI",
             schema ({ { "path", "string", true, "Arquivo .mid de saída"_t },
                       { "what", "string", false, "pattern (padrão atual) ou song (playlist inteira); padrão = o modo PAT/SONG atual"_t },
                       { "loops", "integer", false, "Padrão: quantas vezes repetir (padrão 1)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const juce::File file (params::string (a, "path"));

                 if (file == juce::File())
                     return CommandResult::fail ("Escolha onde salvar o MIDI.");

                 const auto out = file.withFileExtension ("mid");
                 const int loops = juce::jlimit (1, 64, (int) params::number (a, "loops", 1));
                 return fromResult (writeMidiFile (c.model.get(), songFrom (c, a), loops, out), pathResult (out));
             } });

    r.add ({ "project.exportStems", "Exportar stems",
             schema ({ { "folder", "string", true, "Pasta onde gravar um WAV por trilha"_t },
                       { "tracks", "array", false, "Ids ou nomes das trilhas de mixer; vazio = todas que recebem som"_t },
                       { "what", "string", false, "pattern ou song; padrão = o modo PAT/SONG atual"_t },
                       { "loops", "integer", false, "Padrão: quantas vezes repetir (padrão 4)"_t },
                       { "masterChain", "boolean", false, "true = cada stem passa pelos efeitos do master; padrão false (master em bypass, a soma dá a mixagem)"_t },
                       { "background", "boolean", false, "true = começa e volta na hora; o andamento sai no export.status"_t },
                       { "from", "number", false, "Só um trecho da música (SONG): início em ticks; precisa de background"_t },
                       { "to", "number", false, "Fim do trecho, em ticks"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const juce::File folder (params::string (a, "folder"));

                 if (folder == juce::File())
                     return CommandResult::fail ("Escolha a pasta dos stems.");

                 // aceita id (mx_...) ou nome da trilha
                 juce::StringArray tracks;

                 if (auto* list = a["tracks"].getArray())
                 {
                     for (auto& t : *list)
                     {
                         const auto key = t.toString();
                         const MixerTrack* found = c.model.get().findMixer (key);

                         for (auto& m : c.model.get().mixer)
                             if (found == nullptr && m.name.equalsIgnoreCase (key))
                                 found = &m;

                         if (found == nullptr || found->isMaster())
                             return CommandResult::fail ("Trilha de mixer não encontrada: "_t + key);

                         tracks.addIfNotAlreadyThere (found->id);
                     }
                 }

                 if (params::boolean (a, "background", false))
                 {
                     AppServices::BackgroundExport request;
                     request.kind = "stems";
                     request.target = folder;
                     request.loops = loopsFrom (a);
                     request.song = songFrom (c, a);
                     request.tracks = tracks;
                     request.masterChain = params::boolean (a, "masterChain", false);
                     rangeFrom (a, request);
                     return startInBackground (c, request);
                 }

                 if (params::has (a, "from") || params::has (a, "to"))
                     return CommandResult::fail ("Exportar um trecho precisa de \"background\": true."_t);

                 auto result = c.app.exportStems (folder, loopsFrom (a), songFrom (c, a), tracks, params::boolean (a, "masterChain", false));

                 if (result.hasProperty ("error"))
                     return CommandResult::fail (result["error"].toString());

                 return CommandResult::success (result);
             } });

    // Exportar para vender (bloco 2, 23/10): o pacote de quem vende beat, numa ação só e em segundo plano.
    r.add ({ "project.exportPack", "Exportar para vender"_t,
             schema ({ { "folder", "string", true, "Pasta onde o pacote entra: uma pasta \"Título 140bpm Cm\" com os arquivos, e o zip ao lado"_t },
                       { "wav", "boolean", false, "O WAV limpo, 44,1 kHz / 24 bits (padrão true)"_t },
                       { "mp3", "boolean", false, "O MP3 320 kbps de divulgação, com a tag do produtor (padrão true)"_t },
                       { "stems", "boolean", false, "Os stems (trackouts), um WAV por trilha do mixer que recebe som (padrão false)"_t },
                       { "zip", "boolean", false, "Um zip da pasta, para mandar ao comprador (padrão true)"_t },
                       { "tag", "string", false, "Caminho da tag (WAV ou MP3); ausente = a tag guardada (export.setProducerTag); \"\" = sem tag"_t },
                       { "tagEvery", "number", false, "A cada quantos segundos a tag toca no MP3 (padrão 30, mínimo 5)"_t },
                       { "tagGainDb", "number", false, "Volume da tag em dB (padrão 0)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const juce::File folder (params::string (a, "folder"));

                 if (folder == juce::File())
                     return CommandResult::fail ("Escolha a pasta do pacote."_t);

                 AppServices::BackgroundExport request;
                 request.kind = "pack";
                 request.target = folder;
                 request.song = true;
                 request.packWav = params::boolean (a, "wav", true);
                 request.packMp3 = params::boolean (a, "mp3", true);
                 request.packStems = params::boolean (a, "stems", false);
                 request.packZip = params::boolean (a, "zip", true);
                 request.tag = params::has (a, "tag") ? (params::string (a, "tag").isEmpty() ? juce::File() : juce::File (params::string (a, "tag")))
                                                      : c.app.producerTag();
                 request.tagEverySeconds = juce::jlimit (5.0, 600.0, params::number (a, "tagEvery", 30.0));
                 request.tagGainDb = (float) juce::jlimit (-24.0, 12.0, params::number (a, "tagGainDb", 0.0));

                 if (request.tag != juce::File() && ! juce::File::isAbsolutePath (request.tag.getFullPathName()))
                     return CommandResult::fail ("Informe o caminho completo da tag."_t);

                 const auto r = c.app.startBackgroundExport (request);

                 if (r.failed())
                     return CommandResult::fail (r.getErrorMessage());

                 // o mesmo nome que o app deu à pasta, para quem chamou abrir no fim
                 const auto& p = c.model.get();
                 const auto title = (p.name.isEmpty() || p.name == "Sem título"_t) && c.model.file != juce::File()
                                        ? c.model.file.getFileNameWithoutExtension() : p.name;
                 auto status = c.app.backgroundExportStatus();

                 if (auto* o = status.getDynamicObject())
                 {
                     o->setProperty ("folder", folder.getChildFile (packBaseName (p, title)).getFullPathName());
                     o->setProperty ("tagged", request.packMp3 && request.tag != juce::File());
                 }

                 return CommandResult::success (status);
             } });

    // A tag do produtor: preferência do usuário (fora do projeto e do desfazer), usada pelo project.exportPack.
    r.add ({ "export.setProducerTag", "Tag do produtor"_t,
             schema ({ { "path", "string", false, "Arquivo da tag (WAV ou MP3, até 30 s); vazio = sem tag"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto path = params::string (a, "path");

                 if (path.isEmpty())
                 {
                     c.app.setProducerTag ({});
                     return CommandResult::success();
                 }

                 const juce::File file (path);

                 if (! juce::File::isAbsolutePath (path) || ! file.existsAsFile())
                     return CommandResult::fail ("Arquivo da tag não encontrado: "_t + path);

                 c.app.setProducerTag (file);
                 return CommandResult::success (pathResult (file));
             } });

    r.add ({ "export.producerTag", "Ver a tag do produtor"_t, schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 return CommandResult::success (pathResult (c.app.producerTag()));
             } });

    r.add ({ "export.status", "Andamento da exportação"_t, schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 return CommandResult::success (c.app.backgroundExportStatus());
             } });

    r.add ({ "export.cancel", "Cancelar a exportação"_t, schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 c.app.cancelBackgroundExport();
                 return CommandResult::success (c.app.backgroundExportStatus());
             } });

    r.add ({ "project.saveNewVersion", "Salvar nova versão"_t, schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 const auto res = c.app.saveNewVersion();
                 return fromResult (res, pathResult (c.model.file));
             } });

    r.add ({ "project.autosave", "Autosave agora", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 const auto file = c.app.autosaveNow();
                 auto o = new juce::DynamicObject();
                 o->setProperty ("saved", file != juce::File());
                 o->setProperty ("path", file.getFullPathName());
                 return CommandResult::success (juce::var (o));
             } });
}

} // namespace whynot
