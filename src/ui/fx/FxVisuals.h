#pragma once

#include <JuceHeader.h>

#include "../../engine/WhynotFx.h"

namespace whynot
{

/** A parte "viva" da janela de um efeito: curva do EQ com o espectro, curva do compressor,
    medidores de loudness... Lê os medidores do plugin (atomics) a 30 Hz e desenha a partir
    dos valores do modelo. Pode mexer em parâmetros (arrastar um ponto do EQ). */
class FxVisual : public juce::Component
{
public:
    explicit FxVisual (WhynotFxPlugin& p) : plugin (p) {}

    /** Valores atuais, na ordem dos parâmetros do efeito. */
    virtual void setValues (const std::vector<float>& v)   { values = v; repaint(); }

    /** 30 vezes por segundo, para medidores. */
    virtual void tick() {}

    std::function<void (int index, float value)> onParam;
    std::function<void()> onGestureStart, onGestureEnd;

    /** Altura que o visual pede para cada efeito (0 = sem visual). */
    static int heightFor (const juce::String& type);
    static std::unique_ptr<FxVisual> create (WhynotFxPlugin&);

protected:
    float value (const char* id) const;
    int indexOf (const char* id) const   { return plugin.getInfo().indexOf (id); }

    WhynotFxPlugin& plugin;
    std::vector<float> values;
};

} // namespace whynot
