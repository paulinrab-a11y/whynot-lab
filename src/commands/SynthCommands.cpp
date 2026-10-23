#include "CommandRegistry.h"
#include "../engine/SynthFavorites.h"
#include "../engine/SynthState.h"
#include "../synth/SynthPresets.h"

// WHYNOT Synth: canal, presets, parâmetros e "Variar". Tudo que muda o canal é um passo de
// desfazer (os botões da janela agrupam o arrasto num gesto só).

namespace whynot
{

namespace
{
    using params::schema;

    juce::String utf8 (const char* s)          { return juce::String::fromUTF8 (s); }
    juce::String utf8 (const std::string& s)   { return juce::String::fromUTF8 (s.c_str()); }

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    struct Target
    {
        Channel* channel = nullptr;
        juce::String error;
    };

    Target synthChannel (Project& p, const juce::var& a)
    {
        Target t;
        t.channel = p.findChannel (params::string (a, "channel"));

        if (t.channel == nullptr)
            t.error = "Canal não encontrado."_t;
        else if (! t.channel->isSynth())
        {
            t.error = "Este canal não é um WHYNOT Synth."_t;
            t.channel = nullptr;
        }

        return t;
    }

    /** Valor vindo da IA ou da tela: número, booleano ou rótulo de lista ("Serra", "Passa-baixas", "+1"). */
    bool parseValue (int index, const juce::var& v, float& out)
    {
        const auto& prm = synth::param (index);

        if (v.isBool())
        {
            out = (bool) v ? 1.0f : 0.0f;
            return true;
        }

        const bool numeric = v.isDouble() || v.isInt() || v.isInt64()
                             || (v.isString() && v.toString().trim().containsOnly ("-+.0123456789") && v.toString().trim().isNotEmpty());

        // Listas com rótulos numéricos (oitava, vozes, polifonia): o número é o rótulo, não a posição.
        if (numeric && prm.kind == synth::ParamKind::choice && synth::hasNumericChoices (index))
        {
            const int wanted = juce::roundToInt (v.isString() ? v.toString().getDoubleValue() : (double) v);

            for (size_t i = 0; i < prm.choices.size(); ++i)
                if (juce::String (prm.choices[i]).getIntValue() == wanted)
                {
                    out = (float) i;
                    return true;
                }

            return false;
        }

        if (numeric)
        {
            out = prm.clamp ((float) (v.isString() ? v.toString().getDoubleValue() : (double) v));
            return true;
        }

        if (v.isString() && prm.kind == synth::ParamKind::choice)
        {
            for (size_t i = 0; i < prm.choices.size(); ++i)
                if (utf8 (prm.choices[i]).equalsIgnoreCase (v.toString().trim()))
                {
                    out = (float) i;
                    return true;
                }
        }

        return false;
    }

    juce::var describePreset (const synth::Preset& p)
    {
        juce::Array<juce::var> genres;

        for (auto& g : p.genreIds())
            genres.add (utf8 (g));

        const auto* c = synth::findCategory (p.category);
        return object ({ { "id", utf8 (p.id) }, { "name", utf8 (p.name) }, { "category", utf8 (p.category) },
                         { "categoryName", c != nullptr ? utf8 (c->name) : juce::String() },
                         { "genres", genres }, { "description", utf8 (p.description) },
                         { "favorite", synthfavorites::isFavorite (utf8 (p.id)) } });
    }

    juce::var describeParams (const Channel* ch, double bpm)
    {
        const auto values = ch != nullptr ? synthstate::valuesFor (*ch, bpm) : synth::defaultValues();
        juce::Array<juce::var> list;

        for (int i = 0; i < synth::numParams; ++i)
        {
            const auto& prm = synth::param (i);

            if (prm.hidden)
                continue;

            juce::Array<juce::var> choices;

            for (auto* c : prm.choices)
                choices.add (utf8 (c));

            list.add (object ({ { "id", prm.id }, { "name", utf8 (prm.name) }, { "section", utf8 (synth::sectionOf (i)) },
                                { "min", prm.min }, { "max", prm.max }, { "default", prm.def }, { "value", values[(size_t) i] },
                                { "unit", utf8 (prm.unit) },
                                { "kind", prm.kind == synth::ParamKind::choice ? "choice"
                                            : (prm.kind == synth::ParamKind::toggle ? "toggle" : "continuous") },
                                { "choices", choices }, { "help", utf8 (prm.help) } }));
        }

        return object ({ { "params", list },
                         { "mono", ch != nullptr && ch->mono }, { "glideMs", ch != nullptr ? ch->glideMs : 80.0f },
                         { "note", "Mono e glide são do canal: channel.setMono e channel.setGlide (ou mono/glide aqui)."_t } });
    }
}

void registerSynthCommands (CommandRegistry& r)
{
    r.add ({ "channel.addSynth", "Adicionar WHYNOT Synth",
             schema ({ { "preset", "string", false, "Id do preset (synth.listPresets); padrão = Init"_t },
                       { "name", "string", false, "Nome do canal; padrão = nome do preset"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto presetId = params::string (a, "preset", "init");
                 const auto* preset = synth::findPreset (presetId.toStdString());

                 if (preset == nullptr)
                     return CommandResult::fail ("Preset não encontrado: "_t + presetId + ". Veja os ids com synth.listPresets.");

                 auto& p = c.model.data();
                 Channel ch;
                 ch.id = makeId ("ch");
                 ch.kind = "synth";
                 ch.name = params::string (a, "name", preset->id == "init" ? juce::String ("WHYNOT Synth") : utf8 (preset->name)).trim();
                 ch.colour = channelColourForIndex ((int) p.channels.size());
                 ch.oneShot = false;   // o synth respeita a duração da nota
                 synthstate::loadPreset (ch, *preset);

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

                 return CommandResult::success (object ({ { "channel", ch.id }, { "mixerTrack", mx.id }, { "preset", utf8 (preset->id) } }));
             } });

    r.add ({ "synth.loadPreset", "Carregar preset do synth",
             schema ({ { "channel", "string", true, "Id do canal WHYNOT Synth" },
                       { "preset", "string", true, "Id do preset (synth.listPresets; \"init\" = timbre inicial)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = synthChannel (c.model.data(), a);

                 if (t.channel == nullptr)
                     return CommandResult::fail (t.error);

                 const auto presetId = params::string (a, "preset");
                 const auto* preset = synth::findPreset (presetId.toStdString());

                 if (preset == nullptr)
                     return CommandResult::fail ("Preset não encontrado: "_t + presetId);

                 synthstate::loadPreset (*t.channel, *preset);
                 return CommandResult::success (object ({ { "preset", utf8 (preset->id) }, { "mono", t.channel->mono },
                                                          { "glideMs", t.channel->glideMs } }));
             } });

    r.add ({ "synth.setParam", "Ajustar synth",
             schema ({ { "channel", "string", true, "Id do canal WHYNOT Synth" },
                       { "param", "string", false, "Id do parâmetro (ver synth.params), ex.: flt.cutoff"_t },
                       { "value", "number", false, "Valor (número; listas aceitam o rótulo, ex.: \"Serra\")"_t },
                       { "params", "object", false, "Vários de uma vez: { \"flt.cutoff\": 800, \"l1.wave\": \"Pulso\" }"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = synthChannel (c.model.data(), a);

                 if (t.channel == nullptr)
                     return CommandResult::fail (t.error);

                 std::vector<std::pair<juce::String, juce::var>> changes;

                 if (params::has (a, "param"))
                     changes.push_back ({ params::string (a, "param"), a["value"] });

                 if (auto* many = a["params"].getDynamicObject())
                     for (auto& prop : many->getProperties())
                         changes.push_back ({ prop.name.toString(), prop.value });

                 if (changes.empty())
                     return CommandResult::fail ("Informe param e value, ou params."_t);

                 for (auto& [id, value] : changes)
                 {
                     // Mono e glide moram no canal (o piano roll e o channel rack usam os mesmos).
                     if (id == "mono")
                     {
                         t.channel->mono = value.isBool() ? (bool) value : (double) value >= 0.5;
                         continue;
                     }

                     if (id == "glide")
                     {
                         t.channel->glideMs = (float) juce::jlimit (0.0, 2000.0, (double) value);
                         continue;
                     }

                     const int index = synth::indexOf (id.toStdString());

                     if (index < 0 || synth::param (index).hidden)
                         return CommandResult::fail ("Parâmetro desconhecido no WHYNOT Synth: "_t + id);

                     float v = 0.0f;

                     if (! parseValue (index, value, v))
                         return CommandResult::fail ("Valor inválido para "_t + id + ": " + value.toString());

                     t.channel->synthParams[id] = synthstate::round4 (v);
                 }

                 return CommandResult::success();
             } });

    r.add ({ "synth.randomize", "Variar timbre",
             schema ({ { "channel", "string", true, "Id do canal WHYNOT Synth" },
                       { "amount", "number", false, "Quanto variar, de 0 a 1 (padrão 0,3: sutil; 1 = bem diferente)"_t },
                       { "seed", "integer", false, "Semente (repete a mesma variação); ausente = sorteada"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto t = synthChannel (p, a);

                 if (t.channel == nullptr)
                     return CommandResult::fail (t.error);

                 const float amount = (float) juce::jlimit (0.0, 1.0, params::number (a, "amount", 0.3));
                 const auto seed = params::has (a, "seed") ? (std::uint32_t) (juce::int64) params::number (a, "seed", 1)
                                                           : (std::uint32_t) juce::Random::getSystemRandom().nextInt();
                 const auto values = synth::varied (synthstate::valuesFor (*t.channel, p.bpm), amount, seed);
                 synthstate::store (*t.channel, values);
                 return CommandResult::success (object ({ { "seed", (juce::int64) seed }, { "amount", amount } }));
             } });

    r.add ({ "synth.listPresets", "Listar presets do synth",
             schema ({ { "category", "string", false, "808, bass, lead, pad, pluck, keys, bell, brass, strings, vox, arp, fx, texture" },
                       { "genre", "string", false, "trap, funk, rap, pop, drill, rnb, phonk, lofi" },
                       { "search", "string", false, "Palavras do nome ou da descrição (sem diferenciar acento)"_t },
                       { "favorites", "boolean", false, "true = só os favoritos"_t } }), false,
             [] (CommandContext&, const juce::var& a)
             {
                 const auto category = params::string (a, "category"), genre = params::string (a, "genre");

                 if (category.isNotEmpty() && synth::findCategory (category.toStdString()) == nullptr)
                     return CommandResult::fail ("Categoria desconhecida: "_t + category);

                 if (genre.isNotEmpty() && synth::findGenre (genre.toStdString()) == nullptr)
                     return CommandResult::fail ("Gênero desconhecido: "_t + genre);

                 const bool onlyFavorites = params::boolean (a, "favorites", false);
                 juce::Array<juce::var> list, categories, genres;

                 for (auto* preset : synth::findPresets (category.toStdString(), genre.toStdString(), params::string (a, "search").toStdString()))
                     if (! onlyFavorites || synthfavorites::isFavorite (utf8 (preset->id)))
                         list.add (describePreset (*preset));

                 for (auto& cat : synth::allCategories())
                     categories.add (object ({ { "id", cat.id }, { "name", utf8 (cat.name) },
                                               { "count", (int) synth::findPresets (cat.id, "", "").size() } }));

                 for (auto& g : synth::allGenres())
                     genres.add (object ({ { "id", g.id }, { "name", utf8 (g.name) } }));

                 return CommandResult::success (object ({ { "presets", list }, { "total", (int) synth::allPresets().size() },
                                                          { "categories", categories }, { "genres", genres } }));
             } });

    r.add ({ "synth.params", "Parâmetros do synth"_t,
             schema ({ { "channel", "string", false, "Id de um canal WHYNOT Synth (mostra os valores atuais); ausente = padrões"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const Channel* ch = nullptr;

                 if (params::has (a, "channel"))
                 {
                     auto t = synthChannel (p, a);

                     if (t.channel == nullptr)
                         return CommandResult::fail (t.error);

                     ch = t.channel;
                 }

                 return CommandResult::success (describeParams (ch, p.bpm));
             } });

    r.add ({ "synth.setFavorite", "Favoritar preset",
             schema ({ { "preset", "string", true, "Id do preset" },
                       { "on", "boolean", false, "true = favorito; ausente = alterna" } }), false,
             [] (CommandContext&, const juce::var& a)
             {
                 const auto id = params::string (a, "preset");

                 if (synth::findPreset (id.toStdString()) == nullptr)
                     return CommandResult::fail ("Preset não encontrado: "_t + id);

                 const bool on = params::boolean (a, "on", ! synthfavorites::isFavorite (id));
                 synthfavorites::set (id, on);
                 return CommandResult::success (object ({ { "preset", id }, { "favorite", on } }));
             } });
}

} // namespace whynot
