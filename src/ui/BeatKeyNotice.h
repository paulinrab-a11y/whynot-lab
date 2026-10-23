#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "../common/Text.h"

namespace whynot
{

/** O aviso discreto da barra de status quando o tom do beat sai da análise e é outro que o do projeto:
    "Tom do beat: Lá menor (84%)" e as ações "Usar no projeto" e "Agora não". Cinza, sem laranja
    (docs/design.md, princípio 3: cor não dá importância), e some quando o tom do projeto já é esse.
    A dica do texto traz as alternativas e o relativo explicado. Dono: frente Tom do beat.
*/
class BeatKeyNotice : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    BeatKeyNotice();

    void show (const juce::String& text, const juce::String& details);

    std::function<void()> onUse, onDismiss;

    /** Largura que cabe o texto e os dois botões, na altura da barra de status. */
    int getIdealWidth() const;

    const juce::String& getText() const noexcept   { return text; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String text;
    IconButton useButton { "Usar no projeto", Icon::note, "Usar no projeto" };
    IconButton laterButton { "Agora não"_t, Icon::none, "Agora não"_t };

    static constexpr int buttonHeight = 22;
};

} // namespace whynot
