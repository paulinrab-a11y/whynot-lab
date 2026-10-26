#include "CommandRegistry.h"

// Camadas por força (bloco 4f, 23/10), como os pads do Drum Rack: um canal do sampler toca outro sample a partir de uma
// força (o kick suave e o kick forte). O canal já é o pad: tem a trilha de mixer, os efeitos e o grupo de corte dele.

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

    /** O canal do sampler que aceita camadas, ou o erro. */
    Channel* layeredChannel (Project& p, const juce::var& a, juce::String& error)
    {
        auto* ch = p.findChannel (params::string (a, "channel"));

        if (ch == nullptr)
            error = "Canal não encontrado."_t;
        else if (ch->kind != "sampler" || ch->instrument.isNotEmpty())
            error = "Camadas por força são do sampler (um canal com sample)."_t;
        else if (! ch->slices.empty())
            error = "Num canal fatiado, cada tecla é uma fatia: as camadas por força não valem."_t;
        else if (ch->samplePath.isEmpty())
            error = "Ponha primeiro o sample do canal: as camadas tocam por cima dele nas notas mais fortes."_t;

        return error.isEmpty() ? ch : nullptr;
    }

    /** A camada pedida ("layer" de 1 a 3), ou nulo. */
    SampleLayer* layerOf (Channel& ch, const juce::var& a, juce::String& error)
    {
        const double n = params::number (a, "layer", 0.0);

        if (! (n >= 1.0 && n <= (double) ch.layers.size()) || n != std::floor (n))
        {
            error = ch.layers.empty() ? "O canal não tem camadas."_t
                                      : "Escolha a camada de 1 a "_t + juce::String ((int) ch.layers.size()) + ".";
            return nullptr;
        }

        return &ch.layers[(size_t) n - 1];
    }

    /** O caminho do arquivo de áudio (completo e existente), ou o erro. */
    juce::String fileFrom (const juce::var& a, juce::String& path)
    {
        path = params::string (a, "path");

        if (! juce::File::isAbsolutePath (path))
            return "path precisa ser o caminho completo do arquivo de áudio."_t;

        if (! juce::File (path).existsAsFile())
            return "Arquivo não encontrado: "_t + path;

        if (! juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return "A camada é um arquivo de áudio (WAV, AIFF, FLAC, OGG ou MP3)."_t;

        path = juce::File (path).getFullPathName().replaceCharacter ('\\', '/');
        return {};
    }

    juce::var layersOf (const Channel& ch)
    {
        juce::Array<juce::var> list;

        for (size_t i = 0; i < ch.layers.size(); ++i)
            list.add (object ({ { "layer", (int) i + 1 }, { "path", ch.layers[i].samplePath }, { "minVelocity", ch.layers[i].minVelocity } }));

        return object ({ { "channel", ch.id }, { "sample", ch.samplePath }, { "layers", list },
                         { "note", ch.layers.empty() ? juce::String ("Sem camadas: toda força toca o sample do canal."_t)
                                                     : "Abaixo da força "_t + juce::String (ch.layers.front().minVelocity) + ", toca o sample do canal." } });
    }
}

void registerLayerCommands (CommandRegistry& r)
{
    r.add ({ "channel.layers", "Ver as camadas por força"_t,
             schema ({ { "channel", "string", true, "Id do canal do sampler" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto* ch = c.model.get().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 return CommandResult::success (layersOf (*ch));
             } });

    r.add ({ "channel.addLayer", "Pôr camada por força"_t,
             schema ({ { "channel", "string", true, "Id do canal do sampler" },
                       { "path", "string", true, "Arquivo de áudio da camada (caminho completo)"_t },
                       { "minVelocity", "number", false, "A partir desta força (2 a 127) toca a camada; padrão: entre a última e 127"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 juce::String error, path;
                 auto* ch = layeredChannel (c.model.data(), a, error);

                 if (ch == nullptr)
                     return CommandResult::fail (error);

                 if (const auto e = fileFrom (a, path); e.isNotEmpty())
                     return CommandResult::fail (e);

                 if ((int) ch->layers.size() >= maxSampleLayers)
                     return CommandResult::fail ("O canal já tem "_t + juce::String (maxSampleLayers) + " camadas (com o sample, são 4 forças)."_t);

                 // sem força dada: a primeira em 100; as outras, no meio do caminho entre a mais forte e 127
                 const int last = ch->layers.empty() ? 0 : ch->layers.back().minVelocity;
                 const int velocity = params::has (a, "minVelocity") ? juce::jlimit (2, 127, (int) params::number (a, "minVelocity", 100))
                                                                     : (last == 0 ? 100 : juce::jmin (127, (last + 128) / 2));

                 if (std::any_of (ch->layers.begin(), ch->layers.end(), [velocity] (const SampleLayer& l) { return l.minVelocity == velocity; }))
                     return CommandResult::fail ("Já há uma camada a partir da força "_t + juce::String (velocity) + ".");

                 ch->layers.push_back ({ path, velocity });
                 std::stable_sort (ch->layers.begin(), ch->layers.end(), [] (const SampleLayer& x, const SampleLayer& y) { return x.minVelocity < y.minVelocity; });
                 return CommandResult::success (layersOf (*ch));
             } });

    r.add ({ "channel.setLayer", "Mudar camada por força"_t,
             schema ({ { "channel", "string", true, "Id do canal do sampler" }, { "layer", "number", true, "Qual camada, de 1 a 3" },
                       { "minVelocity", "number", false, "A força mínima nova (2 a 127)"_t },
                       { "path", "string", false, "Outro arquivo de áudio (caminho completo)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 juce::String error;
                 auto* ch = layeredChannel (c.model.data(), a, error);
                 auto* layer = ch != nullptr ? layerOf (*ch, a, error) : nullptr;

                 if (layer == nullptr)
                     return CommandResult::fail (error);

                 if (params::has (a, "path"))
                 {
                     juce::String path;

                     if (const auto e = fileFrom (a, path); e.isNotEmpty())
                         return CommandResult::fail (e);

                     layer->samplePath = path;
                 }

                 if (params::has (a, "minVelocity"))
                 {
                     const int velocity = juce::jlimit (2, 127, (int) params::number (a, "minVelocity", 100));

                     for (auto& other : ch->layers)
                         if (&other != layer && other.minVelocity == velocity)
                             return CommandResult::fail ("Já há uma camada a partir da força "_t + juce::String (velocity) + ".");

                     layer->minVelocity = velocity;
                     std::stable_sort (ch->layers.begin(), ch->layers.end(), [] (const SampleLayer& x, const SampleLayer& y) { return x.minVelocity < y.minVelocity; });
                 }

                 return CommandResult::success (layersOf (*ch));
             } });

    r.add ({ "channel.removeLayer", "Tirar camada por força"_t,
             schema ({ { "channel", "string", true, "Id do canal do sampler" }, { "layer", "number", true, "Qual camada, de 1 a 3" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 juce::String error;
                 auto* ch = layeredChannel (c.model.data(), a, error);
                 auto* layer = ch != nullptr ? layerOf (*ch, a, error) : nullptr;

                 if (layer == nullptr)
                     return CommandResult::fail (error);

                 ch->layers.erase (ch->layers.begin() + (layer - ch->layers.data()));
                 return CommandResult::success (layersOf (*ch));
             } });
}

} // namespace whynot
