#pragma once

#include <JuceHeader.h>

#include "SynthBrowser.h"
#include "SynthVisuals.h"
#include "../Controls.h"
#include "../../common/Text.h"
#include "../fx/FxWidgets.h"
#include "../../engine/WhynotSynth.h"
#include "../../model/ProjectModel.h"

namespace whynot
{

class AppController;

/** A janela do WHYNOT Synth: navegador de presets à esquerda e o timbre à direita, em quatro
    páginas (camadas; filtro e envelopes; modulação; efeitos e voz). Tudo o que muda passa por
    comandos (synth.setParam e companhia): desfazer, salvar e a IA veem o mesmo que a tela. */
class SynthEditor : public juce::Component,
                    private ProjectModel::Listener
{
public:
    SynthEditor (AppController&, WhynotSynthPlugin&);
    ~SynthEditor() override;

    static constexpr int editorWidth = 1180, editorHeight = 700;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Abre numa página (0..3) e camada (0..3; 4 = sub). Para capturas e atalhos. */
    void showPage (int page, int layer = 0);

private:
    struct Cell  { int param; juce::String caption; int width = ParamControl::cellWidth; };
    struct Group { juce::String title; std::vector<Cell> cells; };
    using Row = std::vector<Group>;

    struct PlacedGroup
    {
        juce::String title;
        juce::Rectangle<int> area;
    };

    void projectChanged (const ProjectModel&) override;

    const Channel* channel() const;
    std::vector<float> currentValues() const;
    void refresh();
    void sendParam (int index, float value);
    void beginGesture (const juce::String& name);
    void endGesture();

    void choosePreset (const juce::String& presetId);
    void stepPreset (int delta);
    void showPresetMenu();
    void showVariarMenu();
    void variar (float amount);
    void preview();

    std::vector<Row> rowsFor (int page, int layer) const;
    void layoutPage();

    AppController& app;
    te::Plugin::Ptr keepAlive;
    WhynotSynthPlugin& plugin;
    const juce::String channelId;

    synthui::SynthBrowser browser;
    IconButton prevButton { "Preset anterior da lista", Icon::chevronLeft };
    IconButton nextButton { "Próximo preset da lista"_t, Icon::chevronRight };
    IconButton starButton { "Favorito: marca o preset para achar depois", Icon::star };   // aceso: estrela cheia, no laranja da casa
    IconButton variarButton { "Variar: muda o timbre dentro de limites musicais (desfaz com Ctrl+Z)", Icon::dice, "Variar",
                              IconButton::Style::filled };
    juce::TextButton presetButton;
    IconButton previewButton { "Prévia"_t, Icon::play, "Prévia"_t };
    IconButton variarMenuButton { "Quanto variar", Icon::chevronDown };
    SegmentedControl pages, layers;

    std::vector<std::unique_ptr<ParamControl>> controls;   // um por parâmetro (mono e glide incluídos)
    synthui::WaveView wave;
    synthui::FilterView filterView;
    synthui::EnvView envViews[3];
    synthui::LfoView lfoViews[2];

    std::vector<PlacedGroup> placed;
    int page = 0, layer = 0, gestureDepth = 0;
    float variation = 0.3f;
    juce::String channelName, presetName, presetId, presetInfo;
    bool modified = false;
    int previewKey = -1, previewSerial = 0;

    static constexpr int browserWidth = 290, headerHeight = 64, tabsHeight = 44, groupTitle = 18;
};

} // namespace whynot
