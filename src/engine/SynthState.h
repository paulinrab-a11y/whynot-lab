#pragma once

// Ponte entre o canal "synth" do modelo e os valores do motor (src/synth).
// O canal guarda os parâmetros por id (4 casas, como no .wnl); mono e glide são os campos do
// próprio canal (o piano roll e o channel rack mexem neles); o BPM vem do projeto.

#include "../model/Project.h"
#include "../synth/SynthPresets.h"

#include <cmath>

namespace whynot::synthstate
{

inline float round4 (float v)
{
    return (float) (std::round ((double) v * 10000.0) / 10000.0);
}

/** Todos os valores do motor (na ordem de synth::allParams) para um canal. */
inline std::vector<float> valuesFor (const Channel& c, double bpm)
{
    auto values = synth::defaultValues();

    for (auto& [id, value] : c.synthParams)
        if (const int i = synth::indexOf (id.toStdString()); i >= 0)
            values[(size_t) i] = synth::param (i).clamp (value);

    values[(size_t) synth::P::mono] = c.mono ? 1.0f : 0.0f;
    values[(size_t) synth::P::glide] = synth::param (synth::P::glide).clamp (c.glideMs);
    values[(size_t) synth::P::bpm] = synth::param (synth::P::bpm).clamp ((float) bpm);
    return values;
}

/** Grava no canal os valores do motor que o usuário controla (os do app ficam de fora). */
inline void store (Channel& c, const std::vector<float>& values)
{
    c.synthParams.clear();

    for (int i = 0; i < synth::numParams && i < (int) values.size(); ++i)
        if (! synth::param (i).hidden)
            c.synthParams[juce::String (synth::param (i).id)] = round4 (synth::param (i).clamp (values[(size_t) i]));
}

/** Carrega um preset no canal: parâmetros, mono e tempo de glide. */
inline void loadPreset (Channel& c, const synth::Preset& p)
{
    store (c, p.values);
    c.synthPreset = juce::String::fromUTF8 (p.id.c_str());
    c.mono = p.values[(size_t) synth::P::mono] >= 0.5f;
    c.glideMs = p.values[(size_t) synth::P::glide];
}

/** O canal ainda soa como o preset que diz ter carregado? (falso depois de mexer num botão) */
inline bool matchesPreset (const Channel& c)
{
    const auto* p = synth::findPreset (c.synthPreset.toStdString());

    if (p == nullptr)
        return false;

    const auto now = valuesFor (c, 120.0);

    for (int i = 0; i < synth::numParams; ++i)
        if (! synth::param (i).hidden && std::abs (round4 (p->values[(size_t) i]) - now[(size_t) i]) > 1.0e-4f)
            return false;

    return true;
}

} // namespace whynot::synthstate
