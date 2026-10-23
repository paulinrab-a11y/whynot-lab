#pragma once

#include <JuceHeader.h>

#include "Theme.h"
#include "../model/Project.h"

namespace whynot
{

class AppController;

/** Channel rack: um canal por linha, com mute, armar (notas ao vivo), nome, saída e os passos.
    Canal com notas no piano roll mostra a miniatura delas no lugar dos passos.
    Desenhado num componente só (hit-test por região) para ficar leve com muitos canais.
*/
class ChannelRack : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    public juce::DragAndDropTarget,
                    public juce::TooltipClient
{
public:
    explicit ChannelRack (AppController&);

    void refresh (const Project&);
    void setPlayheadStep (int step);
    void setSelectedChannel (const juce::String&);
    int getIdealHeight() const;

    std::function<void (const juce::String& message, bool isError)> onStatus;
    std::function<void (const juce::String& channel)> onChannelSelected;
    std::function<void (const juce::String& channel)> onOpenPianoRoll;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override   { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

    // instrumento arrastado do navegador de plugins ("plugin:<id>") ou som do navegador de sons ("sample:<caminho>")
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override   { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override    { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    juce::String getTooltip() override;

    /** O menu do "+" (o mesmo do clique): os favoritos primeiro (instrumentos VST3 e presets do WHYNOT Synth
        marcados com a estrela), sample, WHYNOT Synth e os instrumentos VST3 com estrela. Cada item escolhido roda
        actions[id - 1]. Público para o autoteste conferir a ordem. */
    juce::PopupMenu createAddMenu (std::vector<std::function<void()>>& actions);

private:
    struct Hit
    {
        enum class Kind { none, mute, arm, tape, output, step, notes, addButton, lengthBox } kind = Kind::none;
        int row = -1;
        int step = -1;
    };

    struct Row
    {
        juce::String id, name, output, outputName;
        juce::Colour colour;
        bool mute = false;
        bool cutItself = false;
        bool mono = false;
        bool oneShot = true;
        int chokeGroup = 0;       // grupo de corte (0 = nenhum)
        float swing = 0.0f;       // swing dos passos, em %
        bool isAudio = false;
        bool isPlugin = false;    // instrumento VST3
        bool isSynth = false;     // WHYNOT Synth
        bool isPiano = false;     // WHYNOT Piano
        int slices = 0;           // fatiador: quantas fatias (0 = o sample inteiro)
        juce::String pianoPreset;
        bool armed = false;       // gravação (3.5): as notas tocadas ao vivo entram neste canal
        bool frozen = false;      // a trilha do mixer por onde ele sai está congelada
        bool ownTrack = false;    // sai pela trilha que o mixer criou para ele (o padrão): a pílula fica cinza
        bool problem = false;
        juce::String problemText;
        std::vector<Step> steps;
        std::vector<Note> notes;   // do piano roll, no padrão atual

        bool operator== (const Row&) const = default;   // refresh repinta só as linhas que mudaram
    };

    void layoutRows();
    Hit hitTest (juce::Point<int>) const;
    juce::Rectangle<int> rowBounds (int row) const;
    juce::Rectangle<int> muteBounds (int row) const;
    juce::Rectangle<int> armBounds (int row) const;
    juce::Rectangle<int> tapeBounds (int row) const;
    juce::Rectangle<int> outputBounds (int row) const;
    juce::Rectangle<int> stepBounds (int row, int step) const;
    juce::Rectangle<int> notesBounds (int row) const;
    void paintNotesPreview (juce::Graphics&, int row);
    juce::Rectangle<int> addButtonBounds() const;
    juce::Rectangle<int> lengthBounds() const;

    void setStep (int row, int step, bool on);
    void showTapeMenu (int row);
    void showSamplerPanel (int row);
    void showOutputMenu (int row);
    void showLengthMenu();
    void chooseSamples (int row);
    void showAddMenu();
    void addSynth (const juce::String& preset = {});
    void addPluginChannel (const juce::String& pluginId);
    void addPiano();
    void chooseBeatToSlice();
    void slice (const juce::String& channel, const juce::var& how);
    void renameChannel (int row);

    AppController& app;
    std::vector<Row> rows;
    int numSteps = 16;
    int patternTicks = 16 * ticksPerStep;
    int playhead = -1;
    int stepsLeft = 0, stepSize = 30;
    Hit hover;
    int selectedRow = -1;
    juce::String selectedChannel;

    // pintura de passos com arrasto
    int paintRow = -1, lastPaintedStep = -1;
    bool paintValue = true, painting = false;
    bool dragOver = false;
    bool recording = false;   // gravando agora: o armado fica vermelho

    std::unique_ptr<juce::FileChooser> chooser;

    static constexpr int headerHeight = theme::metric::toolbar, rowHeight = 40, footerHeight = 48;
    static constexpr int muteWidth = 28, armWidth = 20, tapeWidth = 150, outputWidth = 112;
    static constexpr int armLeft = 16 + muteWidth + 2, tapeLeft = armLeft + armWidth + 6;
};

} // namespace whynot
