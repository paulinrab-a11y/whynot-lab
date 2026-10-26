#pragma once

#include <JuceHeader.h>

#include "FxVisuals.h"
#include "FxWidgets.h"
#include "../../model/ProjectModel.h"

namespace whynot
{

class AppController;

/** A janela de um efeito nativo: cabeçalho (nome, trilha, preset, ligado/bypass, sidechain),
    o visual do efeito e os parâmetros agrupados. Tudo o que muda passa por comandos
    (mixer.setInsertParam e companhia): desfazer, salvar e a IA veem o mesmo que a tela. */
class FxEditor : public juce::Component,
                 private ProjectModel::Listener,
                 private juce::Timer
{
public:
    FxEditor (AppController&, WhynotFxPlugin&);
    ~FxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Group
    {
        juce::String title;
        std::vector<ParamControl*> controls;
    };

    void projectChanged (const ProjectModel&) override;
    void timerCallback() override;

    const Insert* currentInsert() const;
    void refresh();
    void sendParam (int index, float value);
    void beginGesture (const juce::String& name);
    void endGesture();
    void showPresetMenu();
    void showParamMenu (int index);
    void askPresetName();
    void buildGroups();
    void updateWindowTitle();

    AppController& app;
    te::Plugin::Ptr keepAlive;
    WhynotFxPlugin& plugin;
    const dsp::FxInfo& info;
    const juce::String trackId, insertId;

    juce::TextButton presetButton, bypassButton;
    juce::ComboBox sidechainBox;
    juce::StringArray sidechainIds;
    juce::OwnedArray<ParamControl> controls;   // na ordem dos parâmetros (ocultos ficam nulos)
    std::vector<ParamControl*> byIndex;
    std::vector<Group> groups;
    std::unique_ptr<FxVisual> visual;
    std::unique_ptr<juce::AlertWindow> nameDialog;   // "Salvar como meu preset"
    juce::String trackName, presetName;
    bool bypassed = false;
    int gestureDepth = 0;

    static constexpr int headerHeight = 56, groupTitle = 20;
};

} // namespace whynot
