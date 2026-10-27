#include "CommandRegistry.h"
#include "../automation/Macros.h"
#include "../dsp/FxCatalog.h"
#include "../engine/UserMix.h"

#include <optional>
#include <set>

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

    /** Id novo de insert, único no projeto inteiro. */
    juce::String newInsertId (const Project& p)
    {
        for (;;)
        {
            const auto id = makeId ("fx");
            const bool used = std::any_of (p.mixer.begin(), p.mixer.end(), [&id] (const MixerTrack& m) { return m.findInsert (id) != nullptr; });

            if (! used)
                return id;
        }
    }

    /** Valores do efeito (padrão + preset), sem os parâmetros que o app controla (BPM). */
    std::map<juce::String, float> valuesFor (const dsp::FxInfo& info, const dsp::Preset* preset)
    {
        const auto values = dsp::presetValues (info, preset);
        std::map<juce::String, float> out;

        for (size_t i = 0; i < info.params.size(); ++i)
            if (! info.params[i].hidden)
                out[info.params[i].id] = values[i];

        return out;
    }

    /** Os efeitos que levam o tom e a escala do projeto: a afinação e a harmonia. */
    bool usesProjectKey (const juce::String& type)   { return type == "tune" || type == "harmony"; }

    /** A afinação e a harmonia usam o tom e a escala do projeto (mesma ordem de escalas do Project). */
    void useProjectKey (const Project& p, Insert& ins)
    {
        if (! usesProjectKey (ins.type))
            return;

        ins.params["key"] = (float) p.keyRoot;
        ins.params["scale"] = (float) juce::jmax (0, Project::scaleNames().indexOf (p.scale));
    }

    Insert makeInsert (const Project& p, const dsp::FxInfo& info, const dsp::Preset* preset)
    {
        Insert ins;
        ins.id = newInsertId (p);
        ins.type = info.type;
        ins.params = valuesFor (info, preset);
        ins.preset = preset != nullptr ? juce::String (preset->id) : juce::String();
        useProjectKey (p, ins);
        return ins;
    }

    /** Os valores de um preset salvo pelo artista: os padrões do efeito por baixo (um parâmetro novo de uma
        versão futura entra no padrão) e só os parâmetros que o efeito conhece, dentro da faixa. */
    std::map<juce::String, float> valuesForUser (const dsp::FxInfo& info, const usermix::UserPreset& user)
    {
        auto out = valuesFor (info, nullptr);

        for (auto& [id, v] : user.params)
            if (const int i = info.indexOf (id.toRawUTF8()); i >= 0 && ! info.params[(size_t) i].hidden)
                out[id] = info.params[(size_t) i].clamp (v);

        return out;
    }

    /** Cadeia salva → inserts deste projeto: ids novos, sem disparo, a afinação no tom do projeto. */
    std::vector<Insert> adoptChain (const Project& p, std::vector<Insert> chain, std::map<juce::String, juce::String>* renamed = nullptr)
    {
        std::vector<Insert> taken;

        for (auto& ins : chain)
        {
            const auto oldId = ins.id;

            // id novo, único no projeto e entre os que acabaram de entrar
            do
                ins.id = newInsertId (p);
            while (std::any_of (taken.begin(), taken.end(), [&ins] (const Insert& t) { return t.id == ins.id; }));

            if (renamed != nullptr)
                (*renamed)[oldId] = ins.id;   // os links do rack da cadeia (bloco 4c) seguem o efeito

            ins.sidechain = {};

            if (ins.kind == "whynot")
            {
                const auto* info = dsp::findEffect (ins.type.toStdString());

                if (info == nullptr)
                    continue;   // efeito de uma versão mais nova do app

                if (auto f = ins.params.find ("follow"); usesProjectKey (ins.type) && (f == ins.params.end() || f->second >= 0.5f))
                    useProjectKey (p, ins);
            }

            taken.push_back (ins);
        }

        return taken;
    }

    /** Papel efetivo: o escolhido, ou o adivinhado pelo nome. */
    juce::String effectiveRole (const MixerTrack& m)
    {
        return m.role.isNotEmpty() ? m.role : juce::String (dsp::guessRole (m.name.toStdString()));
    }

    /** Converte um valor vindo da IA ou da interface: número, booleano ou rótulo de uma lista. */
    bool parseValue (const dsp::ParamInfo& param, const juce::var& v, float& out)
    {
        if (v.isBool())
        {
            out = (bool) v ? 1.0f : 0.0f;
            return true;
        }

        if (v.isDouble() || v.isInt() || v.isInt64())
        {
            out = param.clamp ((float) (double) v);
            return true;
        }

        if (v.isString() && param.kind == dsp::ParamKind::choice)
        {
            for (size_t i = 0; i < param.choices.size(); ++i)
                if (utf8 (param.choices[i]).equalsIgnoreCase (v.toString()))
                {
                    out = (float) i;
                    return true;
                }
        }

        if (v.isString() && v.toString().containsOnly ("-+.0123456789"))
        {
            out = param.clamp (v.toString().getFloatValue());
            return true;
        }

        return false;
    }

    struct Target
    {
        MixerTrack* track = nullptr;
        Insert* insert = nullptr;
        const dsp::FxInfo* info = nullptr;
        juce::String error;
    };

    Target findTarget (Project& p, const juce::var& a, bool needInsert = true)
    {
        Target t;
        t.track = p.findMixer (params::string (a, "track"));

        if (t.track == nullptr)
        {
            t.error = "Trilha de mixer não encontrada."_t;
            return t;
        }

        if (! needInsert)
            return t;

        t.insert = t.track->findInsert (params::string (a, "insert"));

        if (t.insert == nullptr)
        {
            t.error = "Efeito não encontrado nesta trilha."_t;
            return t;
        }

        if (t.insert->kind == "whynot")
            t.info = dsp::findEffect (t.insert->type.toStdString());

        return t;
    }

    //==========================================================================
    // Estilos

    const juce::String reverbBusName { "Reverb" }, delayBusName { "Delay" };

    MixerTrack* findByRole (Project& p, const juce::String& role)
    {
        for (auto& m : p.mixer)
            if (! m.isMaster() && effectiveRole (m) == role)
                return &m;

        return nullptr;
    }

    MixerTrack& ensureBus (Project& p, const juce::String& role, const juce::String& name)
    {
        if (auto* existing = findByRole (p, role))
            return *existing;

        MixerTrack bus;
        bus.id = makeId ("mx");
        bus.name = name;
        bus.colour = "#6E6E6E";
        bus.role = role;

        auto it = std::find_if (p.mixer.begin(), p.mixer.end(), [] (const MixerTrack& m) { return m.isMaster(); });
        return *p.mixer.insert (it, bus);
    }

    std::vector<Insert> buildChain (const Project& p, std::span<const dsp::ChainItem> chain)
    {
        std::vector<Insert> out;

        for (auto& item : chain)
        {
            if (auto* info = dsp::findEffect (item.type))
            {
                const auto* preset = item.preset != nullptr ? dsp::findPreset (item.type, item.preset) : nullptr;
                auto ins = makeInsert (p, *info, preset);

                // ids distintos também dentro da cadeia que ainda não está no projeto
                while (std::any_of (out.begin(), out.end(), [&ins] (const Insert& x) { return x.id == ins.id; }))
                    ins.id = newInsertId (p);

                out.push_back (ins);
            }
        }

        return out;
    }

    void setSend (MixerTrack& m, const juce::String& to, float db, std::optional<bool> preFader = {})
    {
        if (auto* s = m.findSend (to))
        {
            s->levelDb = db;

            if (preFader.has_value())
                s->preFader = *preFader;
        }
        else
        {
            m.sends.push_back ({ to, db, preFader.value_or (false) });
        }
    }

    void removeSend (MixerTrack& m, const juce::String& to)
    {
        std::erase_if (m.sends, [&to] (const Send& s) { return s.to == to; });
    }

    /** Aplica um estilo. tracks vazio = todas as trilhas (e o master, se includeMaster). */
    CommandResult applyStyle (Project& p, const dsp::MixStyle& style, const juce::StringArray& tracks, bool includeMaster)
    {
        const bool all = tracks.isEmpty();
        juce::Array<juce::var> report;

        // Quem precisa de envio? Só cria os buses se alguém for usar.
        bool wantsReverb = false, wantsDelay = false;

        for (auto& m : p.mixer)
        {
            if (m.isMaster() || (! all && ! tracks.contains (m.id)))
                continue;

            if (auto* recipe = style.recipeFor (effectiveRole (m).toStdString()))
            {
                wantsReverb |= recipe->reverbSendDb > dsp::noSend;
                wantsDelay |= recipe->delaySendDb > dsp::noSend;
            }
        }

        juce::String reverbId, delayId;

        if (wantsReverb)
        {
            auto& bus = ensureBus (p, "reverbBus", reverbBusName);
            bus.inserts = buildChain (p, style.reverbBus);
            reverbId = bus.id;
        }
        else if (auto* existing = findByRole (p, "reverbBus"))
        {
            reverbId = existing->id;
        }

        if (wantsDelay)
        {
            auto& bus = ensureBus (p, "delayBus", delayBusName);
            bus.inserts = buildChain (p, style.delayBus);
            delayId = bus.id;
        }
        else if (auto* existing = findByRole (p, "delayBus"))
        {
            delayId = existing->id;
        }

        // Cadeias por papel. O sidechain se resolve depois que todas as trilhas existem.
        struct PendingSidechain { juce::String track, insert, role; };
        std::vector<PendingSidechain> sidechains;

        for (auto& m : p.mixer)
        {
            if (m.isMaster() || (! all && ! tracks.contains (m.id)))
                continue;

            const auto role = effectiveRole (m);
            const auto* recipe = style.recipeFor (role.toStdString());

            if (recipe == nullptr)
                continue;   // buses e trilhas sem receita ficam como estão

            m.inserts = buildChain (p, recipe->chain);

            for (size_t i = 0; i < recipe->chain.size() && i < m.inserts.size(); ++i)
                if (recipe->chain[i].sidechainRole != nullptr)
                    sidechains.push_back ({ m.id, m.inserts[i].id, recipe->chain[i].sidechainRole });

            if (reverbId.isNotEmpty())
            {
                if (recipe->reverbSendDb > dsp::noSend) setSend (m, reverbId, recipe->reverbSendDb);
                else                                     removeSend (m, reverbId);
            }

            if (delayId.isNotEmpty())
            {
                if (recipe->delaySendDb > dsp::noSend) setSend (m, delayId, recipe->delaySendDb);
                else                                    removeSend (m, delayId);
            }

            report.add (object ({ { "track", m.id }, { "role", role }, { "inserts", (int) m.inserts.size() } }));
        }

        for (auto& sc : sidechains)
        {
            auto* source = findByRole (p, sc.role);
            auto* m = p.findMixer (sc.track);

            if (m == nullptr)
                continue;

            if (auto* ins = m->findInsert (sc.insert))
            {
                if (source != nullptr && source->id != m->id && ! p.reaches (m->id, source->id))
                {
                    ins->sidechain = source->id;
                }
                else if (ins->type == "comp")
                {
                    // Sem trilha que dispare: o ducking vira um controle de grave comum.
                    ins->params = valuesFor (*dsp::findEffect ("comp"), dsp::findPreset ("comp", "bass-control"));
                    ins->preset = "bass-control";
                }
                else
                {
                    // A multibanda do beat sem voz no projeto (ou outro efeito com disparo): sem quem dispare, sai.
                    const auto id = ins->id;
                    std::erase_if (m->inserts, [&id] (const Insert& i) { return i.id == id; });
                }
            }
        }

        if (includeMaster)
            if (auto* master = p.findMixer (masterTrackId))
                master->inserts = buildChain (p, style.master);

        return CommandResult::success (object ({ { "style", utf8 (style.id) },
                                                 { "tracks", report },
                                                 { "reverbBus", reverbId },
                                                 { "delayBus", delayId },
                                                 { "targetLufs", style.targetLufs } }));
    }

    juce::var describeEffects()
    {
        juce::Array<juce::var> effects;

        for (auto* e : dsp::allEffects())
        {
            juce::Array<juce::var> params, presets;

            for (auto& prm : e->params)
            {
                if (prm.hidden)
                    continue;

                juce::Array<juce::var> choices;

                for (auto* c : prm.choices)
                    choices.add (utf8 (c));

                params.add (object ({ { "id", prm.id }, { "name", utf8 (prm.name) }, { "min", prm.min }, { "max", prm.max },
                                      { "default", prm.def }, { "unit", utf8 (prm.unit) },
                                      { "kind", prm.kind == dsp::ParamKind::choice ? "choice"
                                                  : (prm.kind == dsp::ParamKind::toggle ? "toggle" : "continuous") },
                                      { "choices", choices }, { "help", utf8 (prm.help) } }));
            }

            for (auto* preset : dsp::presetsFor (e->type))
                presets.add (object ({ { "id", preset->id }, { "name", utf8 (preset->name) }, { "description", utf8 (preset->description) } }));

            juce::Array<juce::var> mine;

            for (auto& user : usermix::presetsFor (e->type))
                mine.add (object ({ { "id", user.id }, { "name", user.name } }));

            effects.add (object ({ { "type", e->type }, { "name", utf8 (e->name) }, { "category", utf8 (e->category) },
                                   { "description", utf8 (e->description) }, { "sidechain", e->sidechain },
                                   { "params", params }, { "presets", presets }, { "userPresets", mine } }));
        }

        juce::Array<juce::var> styles, roles;

        for (auto& s : dsp::allStyles())
            styles.add (object ({ { "id", s.id }, { "name", utf8 (s.name) }, { "description", utf8 (s.description) },
                                  { "targetLufs", s.targetLufs }, { "loudness", utf8 (s.loudnessNote) } }));

        for (auto& r : dsp::allRoles())
            roles.add (object ({ { "id", r.id }, { "name", utf8 (r.name) } }));

        return object ({ { "effects", effects }, { "styles", styles }, { "roles", roles }, { "maxInserts", maxInserts } });
    }

    //==========================================================================
    // Macros do rack (bloco 4c)

    /** O macro pedido ("macro" de 1 a 8), criando os de antes dele se faltarem (o normalise tira os vazios do fim). */
    Macro* macroFor (MixerTrack& m, const juce::var& a, int& index, juce::String& error)
    {
        const double n = params::number (a, "macro", 0.0);

        if (! (n >= 1.0 && n <= (double) maxMacros) || n != std::floor (n))
        {
            error = "Escolha o macro de 1 a 8."_t;
            return nullptr;
        }

        index = (int) n - 1;

        if ((int) m.macros.size() <= index)
            m.macros.resize ((size_t) index + 1);

        return &m.macros[(size_t) index];
    }

    /** Um macro para a IA: o nome, a posição e, para cada parâmetro ligado, o efeito, o nome e os valores com o
        macro em 0, em 1 e agora (na unidade do parâmetro). */
    juce::var macroToVar (const MixerTrack& m, int index)
    {
        static const Macro none;
        const auto& mac = index < (int) m.macros.size() ? m.macros[(size_t) index] : none;
        juce::Array<juce::var> links;

        for (auto& link : mac.links)
        {
            const auto* param = macros::paramOf (m, link);
            const auto* ins = m.findInsert (link.insert);
            const auto* info = ins != nullptr ? dsp::findEffect (ins->type.toStdString()) : nullptr;

            if (param == nullptr || info == nullptr)
                continue;

            links.add (object ({ { "insert", link.insert }, { "effect", utf8 (info->name) }, { "param", link.param },
                                 { "paramName", utf8 (param->name) }, { "unit", utf8 (param->unit) },
                                 { "min", macros::rounded4 (macros::valueAt (*param, link, 0.0f)) },
                                 { "max", macros::rounded4 (macros::valueAt (*param, link, 1.0f)) },
                                 { "now", macros::rounded4 (macros::valueAt (*param, link, mac.value)) } }));
        }

        return object ({ { "macro", index + 1 }, { "name", macros::displayName (mac, index) }, { "value", mac.value }, { "links", links } });
    }
}

void registerMixCommands (CommandRegistry& r)
{
    r.add ({ "mixer.compare", "Comparar sem efeitos"_t,
             schema ({ { "mode", "string", true,
                         "off = normal; bypass = todos os inserts em bypass e envios desligados; "
                         "matched = como está, no mesmo LUFS do bypass (para comparar sem o volume enganar)"_t },
                       { "song", "boolean", false, "Medir a playlist; padrão = o modo PAT/SONG atual"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto mode = params::string (a, "mode");

                 if (mode == "off")
                 {
                     c.app.setAudition (false, 0.0f);
                     return CommandResult::success (object ({ { "mode", "off" } }));
                 }

                 if (mode != "bypass" && mode != "matched")
                     return CommandResult::fail ("Modo desconhecido. Use off, bypass ou matched."_t);

                 const auto& p = c.model.get();
                 const bool song = params::boolean (a, "song", p.songMode);
                 const float without = c.app.renderLoudness (p, song, true, 0.0f);
                 const float with = c.app.renderLoudness (p, song, false, 0.0f);

                 if (without <= -70.0f || with <= -70.0f)
                     return CommandResult::fail ("Não há som para comparar: ponha notas no padrão ou blocos na playlist."_t);

                 // O lado com efeitos desce (ou sobe) até o LUFS do lado sem: compara o timbre, não o volume.
                 const float trim = juce::jlimit (-24.0f, 24.0f, without - with);

                 if (mode == "bypass")
                     c.app.setAudition (true, 0.0f);
                 else
                     c.app.setAudition (false, trim);

                 auto round1 = [] (float v) { return juce::roundToInt (v * 10.0f) / 10.0; };
                 return CommandResult::success (object ({ { "mode", mode }, { "lufsWithout", round1 (without) },
                                                          { "lufsWith", round1 (with) }, { "trimDb", round1 (trim) } }));
             } });

    r.add ({ "fx.list", "Listar efeitos, presets e estilos", schema ({}), false,
             [] (CommandContext&, const juce::var&) { return CommandResult::success (describeEffects()); } });

    //==========================================================================
    // Inserts

    r.add ({ "mixer.addInsert", "Adicionar efeito",
             schema ({ { "track", "string", true, "Id da trilha de mixer (mx_master = master)" },
                       { "type", "string", true, "Efeito (ver fx.list): tune, denoise, leveler, console, doubler, eq, vintageEq, gate, comp, opto, "
                                                 "fet, multiband, deesser, transient, sat, bassplus, filter, reverb, delay, stereo, limiter, meter" },
                       { "preset", "string", false, "Id de um preset do efeito, ou \"user:...\" de um preset salvo (ver fx.list)"_t },
                       { "slot", "integer", false, "Posição 0 a 7 (ausente = no fim)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto t = findTarget (p, a, false);

                 if (t.track == nullptr)
                     return CommandResult::fail (t.error);

                 const auto type = params::string (a, "type");
                 const auto* info = dsp::findEffect (type.toStdString());

                 if (info == nullptr)
                     return CommandResult::fail ("Efeito desconhecido: "_t + type);

                 if ((int) t.track->inserts.size() >= maxInserts)
                     return CommandResult::fail ("Esta trilha já tem os "_t + juce::String (maxInserts) + " efeitos. Remova um antes."_t);

                 const auto presetId = params::string (a, "preset");
                 const auto user = usermix::isUserPresetId (presetId) ? usermix::findPreset (type, presetId) : std::nullopt;
                 const auto* preset = presetId.isEmpty() || user ? nullptr : dsp::findPreset (type.toStdString(), presetId.toStdString());

                 if (presetId.isNotEmpty() && preset == nullptr && ! user)
                     return CommandResult::fail ("Preset não encontrado: "_t + presetId);

                 auto ins = makeInsert (p, *info, preset);

                 if (user)
                 {
                     ins.params = valuesForUser (*info, *user);
                     ins.preset = presetId;
                     useProjectKey (p, ins);
                 }
                 const int slot = juce::jlimit (0, (int) t.track->inserts.size(),
                                                (int) params::number (a, "slot", (double) t.track->inserts.size()));
                 t.track->inserts.insert (t.track->inserts.begin() + slot, ins);
                 return CommandResult::success (object ({ { "insert", ins.id } }));
             } });

    r.add ({ "mixer.showInsert", "Abrir janela do efeito",
             schema ({ { "track", "string", true, "Id da trilha" }, { "insert", "string", true, "Id do efeito" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a);

                 if (t.insert == nullptr)
                     return CommandResult::fail (t.error);

                 return c.app.showInsertEditor (t.track->id, t.insert->id)
                            ? CommandResult::success()
                            : CommandResult::fail ("Este efeito não tem janela agora (o áudio está desligado?)."_t);
             } });

    r.add ({ "mixer.removeInsert", "Remover efeito",
             schema ({ { "track", "string", true, "Id da trilha" }, { "insert", "string", true, "Id do efeito (fx_...)" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a);

                 if (t.insert == nullptr)
                     return CommandResult::fail (t.error);

                 const auto id = t.insert->id;
                 std::erase_if (t.track->inserts, [&id] (const Insert& i) { return i.id == id; });
                 return CommandResult::success();
             } });

    r.add ({ "mixer.moveInsert", "Mover efeito",
             schema ({ { "track", "string", true, "Id da trilha" }, { "insert", "string", true, "Id do efeito" },
                       { "slot", "integer", true, "Nova posição (0 = primeiro)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a);

                 if (t.insert == nullptr)
                     return CommandResult::fail (t.error);

                 auto& list = t.track->inserts;
                 const auto it = std::find_if (list.begin(), list.end(), [&t] (const Insert& i) { return i.id == t.insert->id; });
                 const auto moved = *it;
                 list.erase (it);
                 const int slot = juce::jlimit (0, (int) list.size(), (int) params::number (a, "slot", 0));
                 list.insert (list.begin() + slot, moved);
                 return CommandResult::success();
             } });

    r.add ({ "mixer.setInsertParam", "Ajustar efeito",
             schema ({ { "track", "string", true, "Id da trilha" }, { "insert", "string", true, "Id do efeito" },
                       { "param", "string", false, "Id do parâmetro (ver fx.list)"_t },
                       { "value", "number", false, "Valor (número; listas aceitam o rótulo)"_t },
                       { "params", "object", false, "Vários de uma vez: { \"threshold\": -20, \"ratio\": 4 }"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a);

                 if (t.insert == nullptr)
                     return CommandResult::fail (t.error);

                 if (t.info == nullptr)
                     return CommandResult::fail ("Este efeito não tem parâmetros conhecidos por esta versão."_t);

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
                     const int index = t.info->indexOf (id.toRawUTF8());

                     if (index < 0 || t.info->params[(size_t) index].hidden)
                         return CommandResult::fail ("Parâmetro desconhecido em "_t + utf8 (t.info->name) + ": " + id);

                     float v = 0.0f;

                     if (! parseValue (t.info->params[(size_t) index], value, v))
                         return CommandResult::fail ("Valor inválido para "_t + id + ": " + value.toString());

                     t.insert->params[id] = v;
                 }

                 if (usesProjectKey (t.insert->type))
                 {
                     bool keyChanged = false, followGiven = false;

                     for (auto& change : changes)
                     {
                         keyChanged |= change.first == "key" || change.first == "scale";
                         followGiven |= change.first == "follow";
                     }

                     if (keyChanged && ! followGiven)
                         t.insert->params["follow"] = 0.0f;
                 }

                 return CommandResult::success();
             } });

    r.add ({ "mixer.setInsertBypass", "Bypass de efeito",
             schema ({ { "track", "string", true, "Id da trilha" }, { "insert", "string", true, "Id do efeito" },
                       { "on", "boolean", false, "true = em bypass; ausente = alterna" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a);

                 if (t.insert == nullptr)
                     return CommandResult::fail (t.error);

                 t.insert->bypass = params::boolean (a, "on", ! t.insert->bypass);
                 return CommandResult::success();
             } });

    r.add ({ "mixer.loadInsertPreset", "Carregar preset",
             schema ({ { "track", "string", true, "Id da trilha" }, { "insert", "string", true, "Id do efeito" },
                       { "preset", "string", true, "Id do preset (vazio = valores padrão)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a);

                 if (t.insert == nullptr)
                     return CommandResult::fail (t.error);

                 if (t.info == nullptr)
                     return CommandResult::fail ("Este efeito não tem presets nesta versão."_t);

                 const auto presetId = params::string (a, "preset");
                 const auto user = usermix::isUserPresetId (presetId) ? usermix::findPreset (t.info->type, presetId) : std::nullopt;
                 const auto* preset = presetId.isEmpty() || user ? nullptr : dsp::findPreset (t.info->type, presetId.toStdString());

                 if (presetId.isNotEmpty() && preset == nullptr && ! user)
                     return CommandResult::fail ("Preset não encontrado: "_t + presetId);

                 // Preset da afinação não troca o tom, a escala nem o "seguir o tom do projeto".
                 const auto keepKey = t.insert->params.find ("key"), keepScale = t.insert->params.find ("scale");
                 const auto keepFollow = t.insert->params.find ("follow");
                 const float key = keepKey != t.insert->params.end() ? keepKey->second : -1.0f;
                 const float scale = keepScale != t.insert->params.end() ? keepScale->second : -1.0f;
                 const float follow = keepFollow != t.insert->params.end() ? keepFollow->second : -1.0f;
                 const auto before = t.insert->params;

                 t.insert->params = user ? valuesForUser (*t.info, *user) : valuesFor (*t.info, preset);
                 t.insert->preset = presetId;

                 if (usesProjectKey (t.insert->type) && key >= 0.0f && scale >= 0.0f)
                 {
                     t.insert->params["key"] = key;
                     t.insert->params["scale"] = scale;

                     if (follow >= 0.0f)
                         t.insert->params["follow"] = follow;

                     // As notas da escala Personalizada da afinação (27/10) também ficam.
                     for (auto& [id, v] : before)
                         if (id.startsWith ("note_"))
                             t.insert->params[id] = v;
                 }
                 return CommandResult::success();
             } });

    //==========================================================================
    // Presets e cadeias do artista (21/10): na pasta de dados do app, valem para todos os projetos.

    r.add ({ "mixer.saveInsertPreset", "Salvar meu preset",
             schema ({ { "track", "string", true, "Id da trilha" }, { "insert", "string", true, "Id do efeito nativo" },
                       { "name", "string", true, "Nome do preset (o mesmo nome no mesmo efeito substitui)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a);

                 if (t.insert == nullptr)
                     return CommandResult::fail (t.error);

                 if (t.info == nullptr)
                     return CommandResult::fail ("Só efeitos nativos têm preset salvo aqui; o VST3 guarda o estado dele no projeto."_t);

                 // Sem o tom, a escala e o "seguir": o preset da afinação não leva a música dele junto.
                 auto values = t.insert->params;

                 if (usesProjectKey (t.insert->type))
                 {
                     for (auto* id : { "key", "scale", "follow" })
                         values.erase (id);

                     for (auto it = values.begin(); it != values.end();)
                         it = it->first.startsWith ("note_") ? values.erase (it) : std::next (it);   // as notas da escala também não
                 }

                 juce::String id;
                 const auto result = usermix::savePreset (t.insert->type, params::string (a, "name"), values, id);

                 if (result.failed())
                     return CommandResult::fail (result.getErrorMessage());

                 return CommandResult::success (object ({ { "preset", id }, { "file", usermix::presetsFile().getFullPathName() } }));
             } });

    r.add ({ "mixer.deleteInsertPreset", "Apagar meu preset",
             schema ({ { "type", "string", true, "Efeito (eq, comp...)" }, { "preset", "string", true, "Id \"user:...\"" } }), false,
             [] (CommandContext&, const juce::var& a)
             {
                 const auto result = usermix::deletePreset (params::string (a, "type"), params::string (a, "preset"));
                 return result.wasOk() ? CommandResult::success() : CommandResult::fail (result.getErrorMessage());
             } });

    r.add ({ "mixer.listChains", "Minhas cadeias", schema ({}), false,
             [] (CommandContext&, const juce::var&)
             {
                 juce::Array<juce::var> names;

                 for (auto& n : usermix::chainNames())
                     names.add (n);

                 return CommandResult::success (object ({ { "chains", names }, { "folder", usermix::chainsFolder().getFullPathName() } }));
             } });

    r.add ({ "mixer.saveChain", "Salvar minha cadeia",
             schema ({ { "track", "string", true, "Trilha cujos efeitos viram a cadeia" },
                       { "name", "string", true, "Nome da cadeia (o mesmo nome substitui)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a, false);

                 if (t.track == nullptr)
                     return CommandResult::fail (t.error);

                 if (t.track->inserts.empty())
                     return CommandResult::fail ("Esta trilha não tem efeitos para salvar."_t);

                 const auto name = params::string (a, "name").trim();
                 const auto result = usermix::saveChain (name, t.track->inserts, t.track->macros);

                 if (result.failed())
                     return CommandResult::fail (result.getErrorMessage());

                 return CommandResult::success (object ({ { "name", name }, { "inserts", (int) t.track->inserts.size() } }));
             } });

    r.add ({ "mixer.loadChain", "Carregar minha cadeia",
             schema ({ { "track", "string", true, "Trilha que recebe" }, { "name", "string", true, "Nome da cadeia (ver mixer.listChains)" },
                       { "mode", "string", false, "replace (padrão: troca os efeitos) ou append (põe depois dos que já existem)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto t = findTarget (p, a, false);

                 if (t.track == nullptr)
                     return CommandResult::fail (t.error);

                 std::vector<Insert> chain;
                 std::vector<Macro> rack;
                 const auto result = usermix::loadChain (params::string (a, "name"), chain, &rack);

                 if (result.failed())
                     return CommandResult::fail (result.getErrorMessage());

                 const bool append = params::string (a, "mode", "replace") == "append";
                 std::map<juce::String, juce::String> renamed;
                 auto taken = adoptChain (p, std::move (chain), &renamed);

                 if (! append)
                     t.track->inserts.clear();

                 const int room = maxInserts - (int) t.track->inserts.size();
                 const int left = juce::jmax (0, (int) taken.size() - room);

                 for (int i = 0; i < (int) taken.size() && i < room; ++i)
                     t.track->inserts.push_back (taken[(size_t) i]);

                 // O rack da cadeia (bloco 4c), com os ids novos dos efeitos. Trocando, ele substitui o da trilha (se a
                 // cadeia tiver um); somando, cada macro da cadeia ocupa o mesmo número, se lá não houver nada ligado,
                 // ou o primeiro livre. Link para um efeito que não coube sai no normalise.
                 int macrosIn = 0, macrosLeft = 0;
                 std::set<juce::String> placed;

                 for (int i = 0; i < (int) taken.size() && i < room; ++i)
                     placed.insert (taken[(size_t) i].id);

                 // os links seguem o id novo; o de um efeito que não coube (ou de tipo desconhecido) sai já aqui
                 for (auto& mac : rack)
                 {
                     for (auto& link : mac.links)
                         link.insert = renamed.count (link.insert) > 0 ? renamed[link.insert] : juce::String();

                     std::erase_if (mac.links, [&placed] (const MacroLink& l) { return placed.count (l.insert) == 0; });
                 }

                 if (! append && std::any_of (rack.begin(), rack.end(), [] (const Macro& m) { return ! m.links.empty(); }))
                 {
                     t.track->macros = rack;
                     macrosIn = (int) std::count_if (rack.begin(), rack.end(), [] (const Macro& m) { return ! m.links.empty(); });
                 }
                 else if (append)
                 {
                     t.track->macros.resize ((size_t) maxMacros);

                     for (int i = 0; i < (int) rack.size() && i < maxMacros; ++i)
                     {
                         if (rack[(size_t) i].links.empty())
                             continue;

                         // livre = sem nada: sem links, sem nome e em 0 (um macro só com nome é do artista)
                         int slot = t.track->macros[(size_t) i].isDefault() ? i : -1;

                         for (int k = 0; slot < 0 && k < maxMacros; ++k)
                             if (t.track->macros[(size_t) k].isDefault())
                                 slot = k;

                         if (slot < 0)
                         {
                             ++macrosLeft;
                             continue;
                         }

                         t.track->macros[(size_t) slot] = rack[(size_t) i];
                         ++macrosIn;
                     }
                 }

                 return CommandResult::success (object ({ { "inserts", (int) t.track->inserts.size() }, { "skipped", left },
                                                          { "macros", macrosIn }, { "macrosSkipped", macrosLeft } }));
             } });

    r.add ({ "mixer.deleteChain", "Apagar minha cadeia",
             schema ({ { "name", "string", true, "Nome da cadeia" } }), false,
             [] (CommandContext&, const juce::var& a)
             {
                 const auto result = usermix::deleteChain (params::string (a, "name"));
                 return result.wasOk() ? CommandResult::success () : CommandResult::fail (result.getErrorMessage());
             } });

    r.add ({ "mixer.setInsertSidechain", "Sidechain do efeito",
             schema ({ { "track", "string", true, "Id da trilha do efeito" }, { "insert", "string", true, "Id do efeito (compressor, gate, multibanda...)" },
                       { "source", "string", true, "Trilha que dispara (vazio = desliga)" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto t = findTarget (p, a);

                 if (t.insert == nullptr)
                     return CommandResult::fail (t.error);

                 const auto source = params::string (a, "source");

                 if (source.isEmpty())
                 {
                     t.insert->sidechain = {};
                     return CommandResult::success();
                 }

                 if (t.insert->kind == "vst3")
                 {
                     // VST3 com entrada de sidechain (um segundo bus de entrada, ligado pelo Tracktion ao abrir):
                     // o motor manda o disparo para ela. Sem ela, ligar somaria o kick na entrada do plugin.
                     auto* instance = c.app.insertPluginInstance (t.track->id, t.insert->id);

                     if (instance == nullptr)
                         return CommandResult::fail ("O plugin ainda não abriu: ligue o áudio e tente de novo."_t);

                     if (instance->getBusCount (true) < 2 || instance->getTotalNumInputChannels() <= instance->getChannelCountOfBus (true, 0))
                         return CommandResult::fail ("Este plugin não tem entrada de sidechain."_t);
                 }
                 else if (t.info == nullptr || ! t.info->sidechain)
                 {
                     return CommandResult::fail ("Este efeito não aceita disparo de outra trilha (use um compressor, o gate ou a multibanda)."_t);
                 }

                 if (p.findMixer (source) == nullptr || source == masterTrackId)
                     return CommandResult::fail ("Trilha de disparo não encontrada (o master não dispara sidechain)."_t);

                 if (source == t.track->id || p.reaches (t.track->id, source))
                     return CommandResult::fail ("Essa trilha recebe o som desta: o sidechain criaria um laço."_t);

                 t.insert->sidechain = source;
                 return CommandResult::success();
             } });

    //==========================================================================
    // Envios, papéis e trilhas de bus

    r.add ({ "mixer.setSend", "Envio",
             schema ({ { "track", "string", true, "Trilha que envia" }, { "to", "string", true, "Trilha que recebe (bus de reverb, delay...)" },
                       { "db", "number", false, "Nível do envio em dB (-100 a +6; padrão -12)"_t },
                       { "pre", "boolean", false, "true = pré-fader (sai antes do volume e do pan da trilha); false = pós-fader (padrão)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* m = p.findMixer (params::string (a, "track"));
                 const auto to = params::string (a, "to");

                 if (m == nullptr || m->isMaster())
                     return CommandResult::fail ("Trilha não encontrada (o master não tem envios)."_t);

                 if (p.findMixer (to) == nullptr || to == masterTrackId || to == m->id)
                     return CommandResult::fail ("Destino do envio inválido (escolha um bus, não o master nem a própria trilha)."_t);

                 if (p.reaches (to, m->id))
                     return CommandResult::fail ("Esse envio criaria um laço: o som voltaria para a própria trilha."_t);

                 const float db = (float) juce::jlimit (-100.0, 6.0, params::number (a, "db", m->findSend (to) != nullptr ? m->findSend (to)->levelDb : -12.0));
                 setSend (*m, to, db, params::has (a, "pre") ? std::optional<bool> (params::boolean (a, "pre", false)) : std::nullopt);
                 return CommandResult::success();
             } });

    r.add ({ "mixer.removeSend", "Remover envio",
             schema ({ { "track", "string", true, "Trilha que envia" }, { "to", "string", true, "Trilha que recebe" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* m = c.model.data().findMixer (params::string (a, "track"));

                 if (m == nullptr || m->findSend (params::string (a, "to")) == nullptr)
                     return CommandResult::fail ("Envio não encontrado."_t);

                 removeSend (*m, params::string (a, "to"));
                 return CommandResult::success();
             } });

    r.add ({ "mixer.setRole", "Papel da trilha",
             schema ({ { "track", "string", true, "Id da trilha" },
                       { "role", "string", true, "kick, bass, snare, hats, melody, vocal, backing, fx, reverbBus, delayBus, bus (vazio = adivinhar pelo nome)" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* m = c.model.data().findMixer (params::string (a, "track"));
                 const auto role = params::string (a, "role");

                 if (m == nullptr || m->isMaster())
                     return CommandResult::fail ("Trilha não encontrada (o master não tem papel)."_t);

                 if (role.isNotEmpty() && dsp::findRole (role.toStdString()) == nullptr)
                     return CommandResult::fail ("Papel desconhecido: "_t + role);

                 m->role = role;
                 return CommandResult::success (object ({ { "role", effectiveRole (*m) } }));
             } });

    r.add ({ "mixer.addTrack", "Nova trilha de mixer",
             schema ({ { "name", "string", true, "Nome (ex.: Reverb, Bateria)" },
                       { "role", "string", false, "Papel (ver mixer.setRole)" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto name = params::string (a, "name").trim();
                 const auto role = params::string (a, "role");

                 if (name.isEmpty())
                     return CommandResult::fail ("Dê um nome para a trilha."_t);

                 if (role.isNotEmpty() && dsp::findRole (role.toStdString()) == nullptr)
                     return CommandResult::fail ("Papel desconhecido: "_t + role);

                 MixerTrack m;
                 m.id = makeId ("mx");
                 m.name = name;
                 m.colour = "#6E6E6E";
                 m.role = role;
                 auto it = std::find_if (p.mixer.begin(), p.mixer.end(), [] (const MixerTrack& x) { return x.isMaster(); });
                 p.mixer.insert (it, m);
                 return CommandResult::success (object ({ { "track", m.id } }));
             } });

    r.add ({ "mixer.removeTrack", "Remover trilha de mixer",
             schema ({ { "track", "string", true, "Id da trilha (não o master)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto id = params::string (a, "track");

                 if (p.findMixer (id) == nullptr || id == masterTrackId)
                     return CommandResult::fail ("Trilha não encontrada (o master não sai)."_t);

                 // Quem mandava som para ela passa a ir para o master; envios e sidechains somem.
                 for (auto& ch : p.channels)
                     if (ch.output == id)
                         ch.output = masterTrackId;

                 for (auto& m : p.mixer)
                 {
                     if (m.output == id)
                         m.output = {};

                     removeSend (m, id);

                     for (auto& ins : m.inserts)
                         if (ins.sidechain == id)
                             ins.sidechain = {};
                 }

                 std::erase_if (p.mixer, [&id] (const MixerTrack& m) { return m.id == id; });
                 return CommandResult::success();
             } });

    //==========================================================================
    // Macros do rack (bloco 4c, 23/10): 8 knobs por trilha, cada um move vários parâmetros dos efeitos nativos dela

    r.add ({ "mixer.macros", "Ver os macros",
             schema ({ { "track", "string", true, "Id da trilha" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto* m = c.model.get().findMixer (params::string (a, "track"));

                 if (m == nullptr)
                     return CommandResult::fail ("Trilha de mixer não encontrada."_t);

                 juce::Array<juce::var> list;

                 for (int i = 0; i < maxMacros; ++i)
                     list.add (macroToVar (*m, i));

                 return CommandResult::success (object ({ { "track", m->id }, { "macros", list } }));
             } });

    r.add ({ "mixer.setMacro", "Mexer no macro",
             schema ({ { "track", "string", true, "Id da trilha" }, { "macro", "number", true, "Qual macro, de 1 a 8" },
                       { "value", "number", false, "Posição de 0 a 1: move os parâmetros ligados"_t },
                       { "name", "string", false, "Nome do macro (vazio = Macro N)" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a, false);

                 if (t.track == nullptr)
                     return CommandResult::fail (t.error);

                 if (! params::has (a, "value") && ! params::has (a, "name"))
                     return CommandResult::fail ("Informe value (de 0 a 1) ou name."_t);

                 const double value = params::number (a, "value", 0.0);

                 if (params::has (a, "value") && ! std::isfinite (value))
                     return CommandResult::fail ("value vai de 0 a 1."_t);

                 int index = 0;
                 juce::String error;
                 auto* mac = macroFor (*t.track, a, index, error);

                 if (mac == nullptr)
                     return CommandResult::fail (error);

                 if (params::has (a, "name"))
                     mac->name = params::string (a, "name").trim().substring (0, 24);

                 int changed = 0;

                 if (params::has (a, "value"))
                 {
                     mac->value = macros::rounded4 ((float) juce::jlimit (0.0, 1.0, value));
                     changed = macros::apply (*t.track, index);
                 }

                 auto out = macroToVar (*t.track, index);
                 out.getDynamicObject()->setProperty ("changed", changed);
                 return CommandResult::success (out);
             } });

    r.add ({ "mixer.mapMacro", "Ligar ao macro",
             schema ({ { "track", "string", true, "Id da trilha" }, { "macro", "number", true, "Qual macro, de 1 a 8" },
                       { "insert", "string", true, "Id do efeito nativo, na mesma trilha" },
                       { "param", "string", true, "Id do parâmetro (ver fx.list)"_t },
                       { "min", "number", false, "Valor do parâmetro com o macro em 0 (padrão: o de agora)"_t },
                       { "max", "number", false, "Valor com o macro em 1 (padrão: o máximo do parâmetro; se ele já está no máximo, o mínimo)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a);

                 if (t.insert == nullptr)
                     return CommandResult::fail (t.error);

                 if (t.insert->kind != "whynot" || t.info == nullptr)
                     return CommandResult::fail ("O macro move só os efeitos nativos da WHYNOT; um VST3 fica de fora."_t);

                 const auto paramId = params::string (a, "param");
                 const int pi = t.info->indexOf (paramId.toRawUTF8());

                 if (pi < 0 || t.info->params[(size_t) pi].hidden)
                     return CommandResult::fail ("Parâmetro desconhecido em "_t + utf8 (t.info->name) + ": " + paramId);

                 const auto& param = t.info->params[(size_t) pi];
                 int index = 0;
                 juce::String error;
                 auto* mac = macroFor (*t.track, a, index, error);

                 if (mac == nullptr)
                     return CommandResult::fail (error);

                 const auto range = automation::rangeOf (param);
                 const auto it = t.insert->params.find (paramId);
                 const float now = macros::rounded4 (range.toNormalised (it != t.insert->params.end() ? it->second : param.def));
                 auto found = std::find_if (mac->links.begin(), mac->links.end(),
                                            [&] (const MacroLink& l) { return l.insert == t.insert->id && l.param == paramId; });
                 const bool isNew = found == mac->links.end();
                 MacroLink link = isNew ? MacroLink { t.insert->id, paramId, now, now < 0.9999f ? 1.0f : 0.0f } : *found;

                 if (isNew && (int) mac->links.size() >= maxMacroLinks)
                     return CommandResult::fail ("Este macro já move "_t + juce::String (maxMacroLinks) + " parâmetros."_t);

                 for (auto [key, end] : { std::pair<const char*, float*> { "min", &link.min }, { "max", &link.max } })
                 {
                     if (! params::has (a, key))
                         continue;

                     float v = 0.0f;

                     if (! parseValue (param, a[key], v))
                         return CommandResult::fail ("Valor inválido para "_t + key + ": " + a[key].toString());

                     *end = macros::rounded4 (range.toNormalised (param.clamp (v)));
                 }

                 if (isNew)
                     mac->links.push_back (link);
                 else
                     *found = link;

                 // Com o macro em 0 e a faixa padrão (que começa nele), o parâmetro fica onde está. Com o macro em outro
                 // ponto, ou com a faixa dada, o parâmetro vai para o lugar do macro, como num rack de hardware.
                 const bool rangeGiven = params::has (a, "min") || params::has (a, "max");

                 if (mac->value > 0.0f || ! isNew || rangeGiven)
                     macros::apply (*t.track, index);

                 return CommandResult::success (macroToVar (*t.track, index));
             } });

    r.add ({ "mixer.unmapMacro", "Desligar do macro",
             schema ({ { "track", "string", true, "Id da trilha" }, { "macro", "number", true, "Qual macro, de 1 a 8" },
                       { "insert", "string", false, "Só deste efeito (ausente = todos)"_t },
                       { "param", "string", false, "Só este parâmetro (com insert)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto t = findTarget (c.model.data(), a, false);

                 if (t.track == nullptr)
                     return CommandResult::fail (t.error);

                 int index = 0;
                 juce::String error;
                 auto* mac = macroFor (*t.track, a, index, error);

                 if (mac == nullptr)
                     return CommandResult::fail (error);

                 const auto insert = params::string (a, "insert"), param = params::string (a, "param");
                 const auto removed = std::erase_if (mac->links, [&] (const MacroLink& l)
                 {
                     return (insert.isEmpty() || l.insert == insert) && (param.isEmpty() || l.param == param);
                 });

                 if (removed == 0)
                     return CommandResult::fail ("Nada ligado a este macro com esse efeito ou parâmetro."_t);

                 return CommandResult::success (object ({ { "macro", index + 1 }, { "removed", (int) removed } }));
             } });

    //==========================================================================
    // Estilos de mixagem por gênero

    r.add ({ "mixer.applyStyle", "Aplicar estilo de mixagem",
             schema ({ { "style", "string", true, "trap, funk, rap ou pop" },
                       { "tracks", "array", false, "Só estas trilhas (ids); ausente = todas"_t },
                       { "master", "boolean", false, "Também a cadeia do master (padrão: sim, quando aplica em todas)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto id = params::string (a, "style");
                 const auto* style = dsp::findStyle (id.toStdString());

                 if (style == nullptr)
                     return CommandResult::fail ("Estilo desconhecido: "_t + id + ". Use trap, funk, rap ou pop.");

                 juce::StringArray tracks;

                 if (auto* list = a["tracks"].getArray())
                     for (auto& t : *list)
                         tracks.add (t.toString());

                 for (auto& t : tracks)
                     if (c.model.get().findMixer (t) == nullptr)
                         return CommandResult::fail ("Trilha de mixer não encontrada: "_t + t);

                 const bool includeMaster = params::boolean (a, "master", tracks.isEmpty()) || tracks.contains (masterTrackId);
                 tracks.removeString (masterTrackId);

                 if (! tracks.isEmpty() || ! params::has (a, "tracks"))
                     return applyStyle (c.model.data(), *style, tracks, includeMaster);

                 // Só o master ("tracks": [] ou ["mx_master"]); com "master": false não há o que aplicar.
                 if (! includeMaster)
                     return CommandResult::fail ("Nada para aplicar: a lista de trilhas está vazia e o master ficou de fora."_t);

                 if (auto* master = c.model.data().findMixer (masterTrackId))
                     master->inserts = buildChain (c.model.get(), style->master);

                 return CommandResult::success();
             } });
}

} // namespace whynot
