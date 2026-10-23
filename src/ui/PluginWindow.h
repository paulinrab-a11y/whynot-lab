#pragma once

#include <JuceHeader.h>

namespace whynot
{

namespace te = tracktion;

/** Janela com a interface de um plugin. Aberta pelo Tracktion (UIBehaviour::createPluginWindow),
    que também a fecha quando o plugin sai do projeto.

    Plugins VST3 mostram o editor deles (ou um editor genérico com os parâmetros, se não tiverem);
    os plugins nativos da WHYNOT registram o próprio editor em nativeEditorFactory.
*/
class PluginWindow : public juce::DocumentWindow
{
public:
    explicit PluginWindow (te::Plugin&);
    ~PluginWindow() override;

    static std::unique_ptr<juce::Component> create (te::Plugin&);

    /** Editor dos plugins nativos (não VST3); devolve nullptr para os que não conhece. */
    static inline std::function<std::unique_ptr<juce::Component> (te::Plugin&)> nativeEditorFactory;

    /** Chamado quando qualquer janela de plugin fecha (o app guarda o estado do plugin). */
    static inline std::function<void (te::Plugin&)> onClosed;

private:
    void closeButtonPressed() override;
    void moved() override;
    float getDesktopScaleFactor() const override   { return 1.0f; }

    te::Plugin& plugin;
    std::unique_ptr<juce::Component> editor;
    bool storeBounds = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace whynot
