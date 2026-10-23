#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "../common/Text.h"

namespace whynot
{

class AppController;

/** Atalhos de teclado: escolhe o preset (estilo FL Studio ou WHYNOT) e troca a tecla de qualquer ação.
    Clique numa ação e aperte a tecla nova; Esc cancela. Botão direito: outra tecla, sem atalho, padrão.
*/
class ShortcutEditor : public juce::Component,
                       private juce::ListBoxModel
{
public:
    explicit ShortcutEditor (AppController&);
    ~ShortcutEditor() override;

    static void show (AppController&, juce::Component* parent);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Row
    {
        bool header = false;
        juce::String id, text;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void rebuild();
    void startCapture (int row, bool adding);
    void setKeys (const juce::String& action, const juce::Array<juce::KeyPress>&);
    void showRowMenu (int row);

    AppController& app;
    SegmentedControl preset;
    juce::ListBox list { "atalhos", this };
    std::vector<Row> rows;
    int capturing = -1;
    bool captureAdds = false;
    juce::String message;

    static constexpr int rowHeight = 30;
};

} // namespace whynot
