#include "PluginWindow.h"
#include "Theme.h"

namespace whynot
{

PluginWindow::PluginWindow (te::Plugin& p)
    : juce::DocumentWindow (p.getName(), theme::colour::background, juce::DocumentWindow::closeButton, true),
      plugin (p)
{
    setUsingNativeTitleBar (true);

    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&plugin))
    {
        if (auto* pi = ext->getAudioPluginInstance())
        {
            editor.reset (pi->createEditorAndMakeActive());

            if (editor == nullptr)   // plugin sem interface: os parâmetros num editor genérico
                editor = std::make_unique<juce::GenericAudioProcessorEditor> (*pi);
        }
    }
    else if (nativeEditorFactory)
    {
        editor = nativeEditorFactory (plugin);
    }

    if (editor != nullptr)
        setContentNonOwned (editor.get(), true);   // a janela acompanha o tamanho do editor

    setResizable (false, false);
    getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
    setTopLeftPosition (plugin.windowState->choosePositionForPluginWindow());
    storeBounds = true;
}

PluginWindow::~PluginWindow()
{
    storeBounds = false;
    plugin.edit.flushPluginStateIfNeeded (plugin);

    if (onClosed)
        onClosed (plugin);

    clearContentComponent();
    editor = nullptr;
}

std::unique_ptr<juce::Component> PluginWindow::create (te::Plugin& plugin)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&plugin); ext != nullptr && ext->getAudioPluginInstance() == nullptr)
        return {};

    auto w = std::make_unique<PluginWindow> (plugin);

    if (w->editor == nullptr)
        return {};

    w->setVisible (true);
    w->toFront (true);
    return w;
}

void PluginWindow::closeButtonPressed()
{
    plugin.windowState->closeWindowExplicitly();
}

void PluginWindow::moved()
{
    if (storeBounds)
        plugin.windowState->lastWindowBounds = getBounds();
}

} // namespace whynot
