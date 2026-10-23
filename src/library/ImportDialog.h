#pragma once

#include <JuceHeader.h>

#include "ProjectImport.h"
#include "../ui/Controls.h"

namespace whynot
{

/** "Importar daqui": os padrões do outro projeto (cada um com os canais que tocam nele) para marcar, e as
    opções (as trilhas do mixer com os efeitos; as notas no tom do projeto aberto). Importar roda o comando
    project.importFrom, que vira um passo de desfazer. O visual segue o docs/design.md (cartão `panel`). */
class ImportDialog : public juce::Component
{
public:
    /** Importa com as opções; devolve o erro ("" = deu certo e a janela fecha). */
    using RunImport = std::function<juce::String (const library::ImportOptions&)>;

    ImportDialog (library::ImportSource, RunImport);

    /** Abre a janela, centrada em `parent`. */
    static void show (juce::Component* parent, library::ImportSource, RunImport);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 520;

private:
    void runImport();
    void updateButton();
    int listHeight() const;

    library::ImportSource source;
    RunImport run;
    juce::Viewport viewport;
    juce::Component listContent;
    juce::OwnedArray<juce::ToggleButton> patternToggles;
    juce::ToggleButton mixerToggle { "Trazer as trilhas do mixer com os efeitos"_t },
                       transposeToggle { "Levar as notas para o tom do projeto aberto"_t };
    IconButton importButton { "Trazer os padrões marcados para o projeto aberto (Ctrl+Z desfaz)"_t, Icon::plus, "Importar" },
               cancelButton { "Fechar sem importar"_t, Icon::none, "Cancelar" };
    juce::String error;

    static constexpr int rowHeight = 30;
};

} // namespace whynot
