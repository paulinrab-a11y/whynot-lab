#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "IdeasPanel.h"
#include "MidiImportDialog.h"
#include "TimeGrid.h"
#include "../engine/BeatKey.h"
#include "../model/Project.h"

#include <set>

namespace whynot
{

class AppController;

/** A grade do piano roll: teclado à esquerda, régua em cima, notas no meio e a raia de força
    embaixo. Num componente só, com a rolagem feita à mão, para teclado e régua ficarem parados.
    Toda edição passa pelos comandos pianoroll.*, dentro de um gesto (um passo de desfazer).
*/
class PianoRollGrid : public juce::Component,
                      public juce::TooltipClient,
                      private juce::ScrollBar::Listener
{
public:
    enum class Tool { draw, paint, select, slice };   // slice (C): corta a nota onde clicar

    /** false com o teclado-piano ligado (Ctrl+T): P, B, E e S tocam nota em vez de trocar a ferramenta. */
    static inline bool singleKeyShortcuts = true;

    explicit PianoRollGrid (AppController&);
    ~PianoRollGrid() override;

    void refresh (const Project&, const juce::String& channel);
    void setTool (Tool);
    Tool getTool() const noexcept            { return tool; }
    /** Ticks fixos (1 = sem grade) ou timegrid::snapAuto, snapBeat e snapBar. */
    void setSnap (int setting);
    /** O encaixe agora, em ticks (na grade automática, depende do zoom). */
    int getSnapTicks() const;
    /** A divisão da grade automática no zoom de agora ("1/4 passo", "tempo"...). */
    juce::String getAutoGridName() const;
    void setSlideMode (bool on)              { slideMode = on; }
    bool getSlideMode() const noexcept       { return slideMode; }
    void setPlayhead (double ticks);         // < 0 = parado

    /** Rola a grade até as notas do canal (depois de gerar uma ideia, por exemplo). */
    void revealNotes();
    /** Zoom na horizontal em volta de `anchorX` (Ctrl+rodinha, ou a rodinha em cima da régua). */
    void zoomBy (float notches, float anchorX);
    float getPxPerTick() const noexcept      { return pxPerTick; }
    /** O ponto da tela de um tick numa tecla (o meio da linha): para o autoteste clicar em notas. */
    juce::Point<float> pointAt (double tick, int key) const   { return { tickToX (tick), keyToY (key) + rowHeight * 0.5f }; }

    /** Dicas do beat (21/10, noite): em cada compasso, a linha da fundamental do acorde do beat (onde o 808
        casa) mais clara e as outras notas do acorde mais fracas; o nome do acorde na régua. */
    void setBeatHints (std::vector<BeatHint>, bool visible, bool flats = false);   // flats: a armadura do tom do beat
    const std::vector<BeatHint>& getBeatHints() const noexcept   { return beatHints; }
    bool getBeatHintFlats() const noexcept                       { return beatHintFlats; }
    bool areBeatHintsVisible() const noexcept                    { return showBeatHints && ! beatHints.empty(); }

    /** As ferramentas do FL (bloco 1, 22/10), pelo comando pianoroll.transform: legato, glue, mute, unmute,
        humanize e toScale. Valem na seleção; sem seleção, em todas as notas. Um passo de desfazer. */
    void runTransform (const juce::String& op, double amount = 0.5);
    /** Carimbo de acorde (como o Stamp do FL): com um acorde escolhido, o clique do Desenhar põe o acorde
        inteiro, com o baixo na nota clicada. "" = desligado. Ids em notetools::chordShapes(). */
    void setChordStamp (const juce::String& id);
    const juce::String& getChordStamp() const noexcept   { return chordStamp; }
    /** Encaixar na escala: desenhar, pintar, arrastar e as setas só caem em notas da escala do projeto. */
    void setScaleLock (bool on);
    bool getScaleLock() const noexcept                   { return scaleLock; }
    /** Importar um .mid no canal (o padrão atual), pelo comando pianoroll.importMidi: todas as trilhas (track 0)
        ou uma, trocando ou somando às notas do canal, a partir de `at` (ticks do padrão). */
    void importMidi (const juce::File&, int track = 0, bool replace = true, int at = 0);

    /** A raia embaixo da grade (26/10): a força das notas, o pitch bend ou o mod wheel (CC1) desenhados nelas. */
    enum class LaneMode { velocity, pitch, mod };
    void setLaneMode (LaneMode);
    LaneMode getLaneMode() const noexcept       { return laneMode; }

    std::function<void (const juce::String&, bool isError)> onStatus;
    std::function<void()> onModesChanged;    // Esc desligou o carimbo de acorde
    std::function<void()> onToolChanged;     // P, B ou E trocaram a ferramenta
    std::function<void()> onSlideChanged;    // a tecla S mudou o slide das notas
    std::function<void()> onGridChanged;     // a grade automática mudou de divisão (zoom ou compasso)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    juce::String getTooltip() override;

private:
    enum class Drag { none, keys, move, resize, erase, paint, select, velocity, stretch, curve, curveErase };

    struct Ghost { int start, length, key; };

    void scrollBarMoved (juce::ScrollBar*, double) override;
    void updateScrollBars();
    void clampScroll();
    void scrollToContent();

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> keyboardArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> velocityArea() const;
    juce::Rectangle<int> laneLabelArea() const;   // à esquerda da raia, embaixo do teclado: Força, Pitch, Mod e o alcance
    float tickToX (double tick) const;
    double xToTick (float x) const;
    float keyToY (int key) const;            // topo da linha da tecla
    int yToKey (float y) const;
    juce::Rectangle<float> noteBounds (const Note&) const;
    timegrid::Meter meter() const noexcept   { return { stepsPerBeat, stepsPerBar }; }
    void notifyGridChange();
    int snapDown (double tick) const;
    int snapNearest (double tick) const;
    int contentTicks() const;
    bool inScale (int key) const;
    int lockKey (int key) const;             // com o encaixe na escala ligado, a nota da escala mais perto
    int stepKey (int key, int direction) const;   // seta para cima ou para baixo: um semitom, ou a próxima nota da escala

    const Note* noteAt (juce::Point<float>, bool& onRightEdge) const;
    const Note* findNote (int id) const;
    void convertSteps();
    int addNote (int start, int key);
    std::vector<int> addChord (int start, int key);   // o carimbo: as notas do acorde, a do baixo primeiro
    void updateNotes (const std::vector<Note>&);
    void removeNotes (const std::vector<int>& ids);
    void eraseAt (juce::Point<float>);
    void paintAt (juce::Point<float>);
    void applyVelocity (float fromX, float toX, float y);
    void applyCurve (float fromX, float toX, float y, bool erase, bool free);   // a raia Pitch ou Mod (26/10)
    void clearCurveAt (float x);
    float laneValue (float y, bool free) const;
    float laneY (float value) const;
    void clickLaneLabel (juce::Point<float>);
    void nudgeSelection (int deltaTicks, int deltaKeys);
    void toggleSlideOnSelection();

    // edição da seleção, como no FL (pedido do usuário, 21/10): Ctrl+C/X/V, Ctrl+B, Alt+Q, cortar e esticar
    std::vector<Note> selectedNotes() const;
    std::vector<int> addNoteList (const std::vector<Note>&);
    void copySelection();
    void pasteNotes();
    void duplicateSelection();
    void quantizeSelection();
    void sliceNoteAt (juce::Point<float>, bool free);
    juce::Rectangle<float> stretchHandle() const;   // a alça na régua, no fim da seleção
    void preview (int key, int velocity, bool highlightKey = false);
    void beginEdit (const juce::String& name);
    void endEdit();

    void paintGrid (juce::Graphics&, juce::Rectangle<int>);
    void paintBeatHints (juce::Graphics&, juce::Rectangle<int>);
    void paintBeatHintNames (juce::Graphics&, juce::Rectangle<int> ruler);
    const BeatHint* beatHintAt (double tick) const;
    void paintNotes (juce::Graphics&, juce::Rectangle<int>);
    void paintKeyboard (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintVelocity (juce::Graphics&);
    void paintCurves (juce::Graphics&, juce::Rectangle<int> lane);
    void paintModes (juce::Graphics&);       // o aviso do carimbo e do encaixe na escala, no canto da grade

    AppController& app;
    juce::String channel, channelName;
    bool channelOk = false, channelMono = false;
    int channelRootKey = 60;
    std::vector<Note> notes;
    std::vector<Note> stepNotes;             // passos que ainda não viraram notas (mostrados igual)
    std::vector<Ghost> ghosts;               // notas dos outros canais no mesmo padrão
    int patternTicks = ticksPerBar4;
    int keyRoot = 0;
    int stepsPerBar = 16, stepsPerBeat = 4;
    std::vector<int> degrees;
    double glideTicks = 0.0;
    std::vector<BeatHint> beatHints;         // o acorde do beat em cada compasso do padrão
    bool showBeatHints = true;
    bool beatHintFlats = false;
    LaneMode laneMode = LaneMode::velocity;
    float bendRange = 2.0f;                  // o alcance do pitch wheel no instrumento do canal (curves::bendRangeFor)
    bool channelBends = false;               // o instrumento toca o pitch bend (o synth e o VST3; o sampler ainda não)

    std::set<int> selected;
    Tool tool = Tool::draw;
    int snapSetting = timegrid::snapAuto;
    juce::String lastAutoName;               // para avisar só quando a grade automática muda
    bool slideMode = false;
    juce::String chordStamp;                 // "" = sem carimbo
    bool scaleLock = false;
    int lastLength = ticksPerStep;
    int lastVelocity = 100;

    double scrollTicks = 0.0;                // tick na borda esquerda da grade
    double scrollRows = 40.0;                // linhas roladas a partir da tecla 127
    float pxPerTick = 0.1f;                  // 24 px por passo
    static constexpr float minPxPerTick = 0.008f, maxPxPerTick = 2.0f;   // 2 px por tick: 1/16 de passo com 30 px
    static constexpr timegrid::Spacing spacing { 7.0f, 14.0f };
    float rowHeight = 16.0f;

    Drag drag = Drag::none;
    bool editing = false;
    juce::Point<float> dragStart, lastDragPos;
    int anchorId = -1;
    std::vector<Note> originals;             // notas arrastadas, como estavam no começo
    int stretchFrom = 0, stretchEnd = 0;     // a seleção que a alça estica: do começo da primeira ao fim da última
    static inline std::vector<Note> clipboard;   // Ctrl+C/Ctrl+V (inícios relativos à primeira nota)
    int lastDeltaTicks = 0, lastDeltaKeys = 0, lastLengthDelta = 0;
    int previewKey = -1;
    int paintOrigin = 0;
    juce::Rectangle<float> rubberBand;
    std::set<int> selectionBefore;
    juce::Point<float> hoverPos { -1.0f, -1.0f };

    int centreKeyOnResize = -1;
    double playhead = -1.0;
    juce::ScrollBar hScroll { false }, vScroll { true };
    bool updatingScrollBars = false;

    static constexpr int keyboardWidth = 72, rulerHeight = 28, velocityHeight = 96, scrollSize = 12;
};

/** Piano roll com a barra de ferramentas: canal, ferramentas, snap, slide/glide e tonalidade. */
class PianoRoll : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    explicit PianoRoll (AppController&);

    void refresh (const Project&);
    void setChannel (const juce::String&);
    const juce::String& getChannel() const noexcept   { return channel; }
    void setPlayhead (double ticks)                   { grid.setPlayhead (ticks); }

    /** Painel do WHYNOT Ideias à direita da grade. */
    void setIdeasVisible (bool);
    bool isIdeasVisible() const                       { return ideas.isVisible(); }

    /** Dicas do beat: liga e desliga (vale para a sessão). O botão só aparece com um beat analisado no projeto. */
    void setBeatHintsOn (bool);
    static inline bool beatHintsOn = true;

    std::function<void (const juce::String&, bool isError)> onStatus;
    std::function<void (const juce::String& channel)> onChannelPicked;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // arrastar um .mid para o piano roll importa as notas no canal (bloco 1, 22/10)
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    /** O menu Editar e o submenu do carimbo de acorde (públicos para o autoteste abrir sem clicar). */
    juce::PopupMenu makeEditMenu();
    juce::PopupMenu makeChordMenu();
    void chooseMidiFile();
    /** O cartão de opções do MIDI (trilhas, trocar ou somar, onde começa) antes de importar: o seletor de arquivo,
        o soltar e o menu Arquivo passam por aqui. */
    void askMidiImport (const juce::File&);
    MidiImportDialog* getMidiImportDialog() noexcept  { return importDialog.get(); }

    /** Ctrl+C, X, V e B que chegaram à janela com o foco fora da grade (o canal escolhido no channel rack, por
        exemplo) valem nas notas, como no FL (vídeo do produtor, 22/10). true se a tecla era uma delas. */
    bool handleClipboardKey (const juce::KeyPress&);
    /** Dá o teclado à grade, ao mostrar o piano roll e ao trocar de canal: os atalhos dela valem sem clicar antes. */
    void focusGrid();
    PianoRollGrid& getGrid() noexcept                 { return grid; }

private:
    void syncToolButtons();
    void syncModeButtons();
    void setSlideMode (bool);
    void showGlideMenu();
    void updateSnapLabel();
    void updateBeatHints (const Project&);
    void closeMidiImport();

    AppController& app;
    juce::String channel;
    juce::StringArray channelIds, channelNames, scaleIds;
    juce::Array<int> dividers;
    bool refreshing = false;

    juce::ComboBox channelBox;
    SegmentedControl toolSwitch;   // desenhar | pintar | selecionar
    juce::ComboBox snapBox;
    IconButton slideButton { "Slide", Icon::slide, "Slide" };
    IconButton monoButton { "Mono", Icon::none, "Mono" };
    IconButton glideButton { "Glide", Icon::timer, "80 ms" };
    // legato, juntar, mudo, humanizar, escala, carimbo de acorde e importar MIDI (bloco 1, 22/10); um botão só,
    // para a barra caber numa tela de 1366
    IconButton editButton { "Editar", Icon::pattern, "Editar" };
    std::unique_ptr<juce::FileChooser> midiChooser;
    std::unique_ptr<MidiImportDialog> importDialog;
    bool midiDragOver = false;
    juce::ComboBox keyBox, scaleBox;
    IconButton ideasButton { "Ideias", Icon::sparkle, "Ideias" };
    IconButton beatHintsButton { "Dicas do beat", Icon::wave, "Dicas do beat" };
    PianoRollGrid grid;
    IdeasPanel ideas;

    static constexpr int toolbarHeight = theme::metric::toolbar;
};

} // namespace whynot
