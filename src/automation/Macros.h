#pragma once

// Os macros do rack de uma trilha (bloco 4c, 23/10): um knob que move vários parâmetros dos efeitos nativos dela
// de uma vez, cada um na sua faixa. A faixa é em posição do knob do parâmetro (de 0 a 1, a mesma curva da
// automação): numa frequência, o meio do macro cai no meio do knob, não no meio dos Hz.
//
// O macro escreve o valor nos parâmetros (Insert::params): o motor toca os efeitos como sempre, e o projeto abre
// igual numa versão sem macros. Mexer no parâmetro sozinho depois vale até o macro andar de novo.

#include "AutomationTarget.h"

#include <cmath>

namespace whynot::macros
{

/** O nome que aparece: o escolhido, ou "Macro N" (index de 0 a 7). */
inline juce::String displayName (const Macro& m, int index)
{
    return m.name.isNotEmpty() ? m.name : "Macro " + juce::String (index + 1);
}

/** A posição do knob do parâmetro (0 a 1) com o macro em value. */
inline float positionAt (const MacroLink& link, float value) noexcept
{
    value = std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f;
    return link.min + (link.max - link.min) * value;
}

/** O parâmetro nativo do link, ou nulo se o efeito saiu, não é nativo ou o parâmetro não existe (ou é escondido). */
inline const dsp::ParamInfo* paramOf (const MixerTrack& track, const MacroLink& link)
{
    const auto* ins = track.findInsert (link.insert);

    if (ins == nullptr || ins->kind != "whynot")
        return nullptr;

    const auto* info = dsp::findEffect (ins->type.toStdString());

    if (info == nullptr)
        return nullptr;

    const int index = info->indexOf (link.param.toRawUTF8());
    return index >= 0 && ! info->params[(size_t) index].hidden ? &info->params[(size_t) index] : nullptr;
}

/** O valor do parâmetro (na unidade dele) com o macro em value. */
inline float valueAt (const dsp::ParamInfo& param, const MacroLink& link, float value)
{
    return param.clamp (automation::rangeOf (param).fromNormalised (positionAt (link, value)));
}

/** Põe nos efeitos da trilha os valores do macro index. Devolve quantos parâmetros mudaram de valor. */
inline int apply (MixerTrack& track, int index)
{
    if (! juce::isPositiveAndBelow (index, (int) track.macros.size()))
        return 0;

    const auto& macro = track.macros[(size_t) index];
    int changed = 0;

    for (auto& link : macro.links)
    {
        const auto* param = paramOf (track, link);
        auto* ins = track.findInsert (link.insert);

        if (param == nullptr || ins == nullptr)
            continue;

        const float v = valueAt (*param, link, macro.value);
        auto it = ins->params.find (link.param);
        const float before = it != ins->params.end() ? it->second : param->def;

        if (v != before || it == ins->params.end())
        {
            ins->params[link.param] = v;
            changed += v != before ? 1 : 0;
        }
    }

    return changed;
}

/** 4 casas, como o .wnl grava: o projeto reaberto fica igual ao que estava na memória. */
inline float rounded4 (float v) noexcept
{
    return std::isfinite (v) ? std::round (v * 10000.0f) / 10000.0f : 0.0f;
}

/** Limpa o rack da trilha: no máximo maxMacros, o valor e as faixas entre 0 e 1, o nome sem espaços nas pontas e até
    24 letras, e só os links para efeitos nativos da trilha (um por parâmetro em cada macro, até maxMacroLinks). Os
    macros do fim sem nada (sem nome, em 0, sem links) saem. */
inline void tidy (MixerTrack& track)
{
    if ((int) track.macros.size() > maxMacros)
        track.macros.resize ((size_t) maxMacros);

    for (auto& macro : track.macros)
    {
        macro.name = macro.name.trim().substring (0, 24);
        macro.value = rounded4 (std::clamp (std::isfinite (macro.value) ? macro.value : 0.0f, 0.0f, 1.0f));

        std::vector<MacroLink> clean;

        for (auto link : macro.links)
        {
            if ((int) clean.size() >= maxMacroLinks || paramOf (track, link) == nullptr)
                continue;

            if (std::any_of (clean.begin(), clean.end(), [&link] (const MacroLink& l) { return l.insert == link.insert && l.param == link.param; }))
                continue;

            link.min = rounded4 (std::clamp (std::isfinite (link.min) ? link.min : 0.0f, 0.0f, 1.0f));
            link.max = rounded4 (std::clamp (std::isfinite (link.max) ? link.max : 1.0f, 0.0f, 1.0f));
            clean.push_back (link);
        }

        macro.links = std::move (clean);
    }

    while (! track.macros.empty() && track.macros.back().isDefault())
        track.macros.pop_back();
}

/** Algum macro da trilha move alguma coisa? */
inline bool hasLinks (const MixerTrack& track)
{
    return std::any_of (track.macros.begin(), track.macros.end(), [] (const Macro& m) { return ! m.links.empty(); });
}

} // namespace whynot::macros
