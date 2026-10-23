#pragma once

#include "AutomationMath.h"
#include "../dsp/FxCatalog.h"
#include "../model/Project.h"

#include <optional>

namespace whynot::automation
{

/** A faixa de um parâmetro de efeito nativo. */
inline Range rangeOf (const dsp::ParamInfo& p)
{
    Range r;
    r.min = p.min;
    r.max = p.max;
    r.scale = p.logScale && p.min > 0.0f ? Range::Scale::log : Range::Scale::linear;
    r.stepped = p.kind != dsp::ParamKind::continuous;
    return r;
}

/** Os parâmetros VST3 andam de 0 a 1 (o valor que o plugin chama de normalizado). */
inline Range pluginRange()   { return {}; }

/** O índice de um parâmetro VST3 ("0", "1"...). */
inline bool isIndex (const juce::String& s)   { return s.isNotEmpty() && s.containsOnly ("0123456789") && s.length() < 7; }

/** A faixa do alvo, pelo que existe no projeto; nullopt se ele não existe (trilha, efeito ou
    parâmetro que saiu, parâmetro escondido, canal que não é VST3). O índice de um VST3 não dá para
    conferir sem o plugin aberto: vale qualquer número. */
inline std::optional<Range> rangeFor (const Project& p, const AutomationTarget& t)
{
    if (t.kind == "mixer")
    {
        if (p.findMixer (t.track) != nullptr)
        {
            if (t.param == "volume")   return Range { -100.0f, 6.0f, Range::Scale::fader };
            if (t.param == "pan")      return Range { -1.0f, 1.0f };
        }
    }
    else if (t.kind == "insert")
    {
        if (auto* m = p.findMixer (t.track))
        {
            if (auto* ins = m->findInsert (t.insert))
            {
                if (ins->kind == "vst3")
                    return isIndex (t.param) ? std::optional<Range> (pluginRange()) : std::nullopt;

                if (auto* info = dsp::findEffect (ins->type.toStdString()))
                {
                    const int index = info->indexOf (t.param.toRawUTF8());

                    if (index >= 0 && ! info->params[(size_t) index].hidden)
                        return rangeOf (info->params[(size_t) index]);
                }
            }
        }
    }
    else if (t.kind == "channel")
    {
        if (auto* c = p.findChannel (t.channel); c != nullptr && c->isPlugin() && isIndex (t.param))
            return pluginRange();
    }

    return std::nullopt;
}

/** O alvo só com os campos do tipo dele (o resto vazio): dois alvos iguais têm a mesma chave. */
inline AutomationTarget tidy (AutomationTarget t)
{
    if (t.kind == "mixer")        { t.channel = {}; t.insert = {}; }
    else if (t.kind == "insert")  { t.channel = {}; }
    else if (t.kind == "channel") { t.track = {}; t.insert = {}; }

    return t;
}

} // namespace whynot::automation
