#include "CommandRegistry.h"
#include "../app/Favorites.h"
#include "../dsp/FxCatalog.h"
#include "../synth/SynthPresets.h"

// Favoritos (as estrelas dos navegadores e dos menus): preferência do usuário, no favoritos.json.
// Nenhum destes comandos mexe no projeto, então não entram no desfazer.

namespace whynot
{

namespace
{
    using params::schema;
    using Kind = Favorites::Kind;

    juce::String utf8 (const char* s)          { return juce::String::fromUTF8 (s); }
    juce::String utf8 (const std::string& s)   { return juce::String::fromUTF8 (s.c_str()); }

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    constexpr const char* kindHelp = "sample (arquivo de som), folder (pasta de kit), plugin (VST3), effect (efeito nativo), "
                                     "effectPreset (\"<efeito>/<preset>\") ou synthPreset (preset do WHYNOT Synth)";

    /** Pelo id (createIdentifierString) ou pelo nome, sem diferenciar maiúsculas. */
    std::optional<juce::PluginDescription> findPlugin (const juce::Array<juce::PluginDescription>& known, const juce::String& idOrName)
    {
        for (auto& d : known)
            if (d.createIdentifierString() == idOrName)
                return d;

        for (auto& d : known)
            if (d.name.equalsIgnoreCase (idOrName.trim()))
                return d;

        return std::nullopt;
    }

    struct Resolved
    {
        juce::String id, name;
        bool available = false;   // o arquivo existe, o plugin está no catálogo, o preset existe
    };

    /** Confere o id e devolve a forma gravada e um nome para mostrar. */
    Resolved resolve (Kind kind, const juce::String& rawId, const juce::Array<juce::PluginDescription>& known)
    {
        const auto id = rawId.trim();
        Resolved r { id, id, false };

        switch (kind)
        {
            case Kind::sample:
            case Kind::folder:
            {
                if (! juce::File::isAbsolutePath (id))
                    return r;

                const juce::File f (id);
                r.id = f.getFullPathName();
                r.name = kind == Kind::sample ? f.getFileNameWithoutExtension() : f.getFileName();
                r.available = kind == Kind::sample ? f.existsAsFile() : f.isDirectory();
                return r;
            }

            case Kind::plugin:
                if (const auto d = findPlugin (known, id))
                {
                    r.id = d->createIdentifierString();
                    r.name = d->name;
                    r.available = true;
                }

                return r;

            case Kind::effect:
                if (const auto* info = dsp::findEffect (id.toStdString()))
                {
                    r.name = utf8 (info->name);
                    r.available = true;
                }

                return r;

            case Kind::effectPreset:
            {
                const auto type = id.upToFirstOccurrenceOf ("/", false, false);
                const auto preset = id.fromFirstOccurrenceOf ("/", false, false);
                const auto* info = dsp::findEffect (type.toStdString());
                const auto* p = info != nullptr ? dsp::findPreset (info->type, preset.toStdString()) : nullptr;

                if (p != nullptr)
                {
                    r.name = utf8 (info->name) + ": " + utf8 (p->name);
                    r.available = true;
                }

                return r;
            }

            case Kind::synthPreset:
                if (const auto* p = synth::findPreset (id.toStdString()); p != nullptr && p->id != "init")
                {
                    r.name = utf8 (p->name);
                    r.available = true;
                }

                return r;
        }

        return r;
    }

    struct Request
    {
        Kind kind = Kind::sample;
        Resolved item;
        juce::String error;
    };

    /** Lê kind e id e confere. Marcar pede algo que existe; desmarcar aceita o que sumiu (arquivo apagado). */
    Request readRequest (CommandContext& c, const juce::var& a, std::optional<bool> on)
    {
        Request req;
        const auto kindText = params::string (a, "kind");
        const auto kind = Favorites::kindFromName (kindText);

        if (! kind)
        {
            req.error = "Tipo de favorito desconhecido: "_t + kindText + ". Use " + kindHelp + ".";
            return req;
        }

        req.kind = *kind;
        const auto id = params::string (a, "id").trim();

        if (id.isEmpty())
        {
            req.error = "Falta o id do favorito."_t;
            return req;
        }

        if ((*kind == Kind::sample || *kind == Kind::folder) && ! juce::File::isAbsolutePath (id))
        {
            req.error = "Use o caminho completo do "_t + (*kind == Kind::sample ? "arquivo: " : "da pasta: ") + id;
            return req;
        }

        req.item = resolve (*kind, id, c.app.knownPlugins());

        // toggle: só vira "marcar" se ainda não está marcado
        const bool marking = on.has_value() ? *on : ! Favorites::get().contains (*kind, req.item.id);

        if (marking && ! req.item.available)
        {
            switch (*kind)
            {
                case Kind::sample:        req.error = "Arquivo não encontrado: "_t + id; break;
                case Kind::folder:        req.error = "Pasta não encontrada: "_t + id; break;
                case Kind::plugin:        req.error = "Plugin não encontrado (veja plugins.list): "_t + id; break;
                case Kind::effect:        req.error = "Efeito nativo desconhecido (veja fx.list): "_t + id; break;
                case Kind::effectPreset:  req.error = "Preset de efeito desconhecido (use \"<efeito>/<preset>\", veja fx.list): "_t + id; break;
                case Kind::synthPreset:   req.error = "Preset do synth não encontrado (veja synth.listPresets): "_t + id; break;
            }
        }

        return req;
    }

    CommandResult apply (const Request& req, bool on)
    {
        auto& favorites = Favorites::get();
        const bool changed = favorites.set (req.kind, req.item.id, on);
        auto result = object ({ { "kind", Favorites::kindName (req.kind) }, { "id", req.item.id }, { "name", req.item.name },
                                { "favorite", favorites.contains (req.kind, req.item.id) }, { "changed", changed } });

        if (changed && favorites.lastSaveFailed())
            result.getDynamicObject()->setProperty ("warning", "Não consegui gravar "_t + favorites.getFile().getFullPathName()
                                                                   + ": a marca vale até fechar o app."_t);

        return CommandResult::success (result);
    }
}

void registerFavoritesCommands (CommandRegistry& r)
{
    r.add ({ "favorites.list", "Listar favoritos"_t,
             schema ({ { "kind", "string", false, "Só um tipo: "_t + kindHelp } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 std::vector<Kind> kinds;

                 if (params::has (a, "kind"))
                 {
                     const auto kind = Favorites::kindFromName (params::string (a, "kind"));

                     if (! kind)
                         return CommandResult::fail ("Tipo de favorito desconhecido: "_t + params::string (a, "kind") + ". Use " + kindHelp + ".");

                     kinds.push_back (*kind);
                 }
                 else
                 {
                     for (int k = 0; k < Favorites::numKinds; ++k)
                         kinds.push_back ((Kind) k);
                 }

                 const auto known = c.app.knownPlugins();
                 auto& favorites = Favorites::get();
                 juce::Array<juce::var> items;
                 auto* counts = new juce::DynamicObject();

                 for (auto kind : kinds)
                 {
                     const auto ids = favorites.list (kind);
                     counts->setProperty (Favorites::kindName (kind), ids.size());

                     for (auto& id : ids)
                     {
                         const auto item = resolve (kind, id, known);
                         items.add (object ({ { "kind", Favorites::kindName (kind) }, { "id", id }, { "name", item.name },
                                              { "available", item.available } }));
                     }
                 }

                 return CommandResult::success (object ({ { "favorites", items }, { "counts", juce::var (counts) },
                                                          { "file", favorites.getFile().getFullPathName() } }));
             } });

    r.add ({ "favorites.set", "Marcar favorito"_t,
             schema ({ { "kind", "string", true, kindHelp },
                       { "id", "string", true, "Caminho (sample, folder), id ou nome do plugin (plugins.list), tipo do efeito (fx.list), "
                                               "\"<efeito>/<preset>\" ou id do preset do synth (synth.listPresets)"_t },
                       { "on", "boolean", false, "true = marca a estrela (padrão); false = tira"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const bool on = params::boolean (a, "on", true);
                 const auto req = readRequest (c, a, on);

                 if (req.error.isNotEmpty())
                     return CommandResult::fail (req.error);

                 return apply (req, on);
             } });

    r.add ({ "favorites.toggle", "Alternar favorito"_t,
             schema ({ { "kind", "string", true, kindHelp },
                       { "id", "string", true, "O mesmo id do favorites.set"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto req = readRequest (c, a, std::nullopt);

                 if (req.error.isNotEmpty())
                     return CommandResult::fail (req.error);

                 return apply (req, ! Favorites::get().contains (req.kind, req.item.id));
             } });
}

} // namespace whynot
