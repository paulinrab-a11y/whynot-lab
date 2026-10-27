#pragma once

#include <JuceHeader.h>

#include "AutomationLanes.h"
#include "Controls.h"
#include "TimeGrid.h"
#include "../model/PlaylistEdit.h"
#include "../model/Project.h"

#include <map>
#include <set>

namespace whynot
{

class AppController;

/** A grade da playlist: raias à esquerda, régua de compassos em cima, blocos de padrão
    e clipes de áudio no meio. Edição só pelos comandos playlist.*, um gesto por arrasto.
*/
class PlaylistGrid : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     public juce::DragAndDropTarget,
                     public juce::TooltipClient,
                     private juce::ScrollBar::Listener,
                     private juce::ChangeListener
{
public:
    explicit PlaylistGrid (AppController&);
    ~PlaylistGrid() override;

    void refresh (const Project&);
    /** Ticks fixos (1 = sem grade) ou timegrid::snapAuto, snapBeat e snapBar. */
    void setSnap (int setting);
    /** O encaixe agora, em ticks (na grade automática, depende do zoom). */
    int getSnapTicks() const;
    /** A divisão da grade automática no zoom de agora ("tempo", "1/4 passo"...). */
    juce::String getAutoGridName() const;
    /** Zoom na horizontal em volta de `anchorX` (Ctrl+rodinha, ou a rodinha em cima da régua). */
    void zoomBy (float notches, float anchorX);
    float getPxPerTick() const noexcept   { return pxPerTick; }
    double getScrollTicks() const noexcept   { return scrollTicks; }
    void setPlayhead (double ticks);   // < 0 = parado
    void setNeedle (int ticks);        // a agulha com o transporte parado, no SONG; < 0 = não mostra
    juce::String getBrushName() const;

    /** Ferramentas, com as teclas do FL (pedido do usuário, 21/10): P desenhar, B pintar, C cortar,
        D apagar, E selecionar, T mudo, S slip (o conteúdo anda dentro do bloco), Y tocar daqui. */
    enum class Tool { draw, paint, slice, erase, select, mute, slip, play };
    void setTool (Tool);
    Tool getTool() const noexcept   { return tool; }
    std::function<void()> onToolChanged;

    /** Seleção de tempo (Ctrl+arrastar na régua): o intervalo do Ctrl+B e o trecho de exportar.
        Vazia = nenhuma. Duplo clique na régua ou Esc limpa. */
    juce::Range<int> getTimeSelection() const noexcept   { return timeSelection; }
    void setTimeSelection (juce::Range<int>);

    /** Voz na playlist (27/10). Ctrl+E: corta na agulha, ou nas bordas da seleção de tempo (só nos blocos
        selecionados, se houver). Ctrl+Delete: apaga o trecho selecionado e puxa o resto. Ctrl+Insert: abre um
        espaço do tamanho do trecho. Cada um é um passo de desfazer. */
    void cutAtNeedle();
    void removeTimeSelection();
    void insertTimeSelection();
    int needleTicks() const;

    /** A prévia do "tirar silêncio e respiração": os trechos (ticks) que sairiam do bloco, em vermelho. Vazio = nenhum. */
    void setSilencePreview (const juce::String& item, std::vector<juce::Range<int>> regions);
    /** Abre a ferramenta de silêncio do bloco (CallOutBox ao lado dele). */
    void showSilenceTool (const juce::String& item);

    /** Blocos selecionados (Ctrl+arrastar ou a ferramenta Selecionar faz a caixa; Shift soma). */
    const std::set<juce::String>& getSelection() const noexcept   { return selection; }
    void selectAll();

    /** false com o teclado-piano ligado (Ctrl+T): as letras tocam nota em vez de trocar a ferramenta. */
    static inline bool singleKeyShortcuts = true;

    std::function<void (const juce::String&, bool isError)> onStatus;
    std::function<void (const juce::String& pattern)> onOpenPattern;
    std::function<void()> onBrushChanged;
    std::function<void()> onGridChanged;   // a grade automática mudou de divisão (zoom ou compasso)
    std::function<void()> onViewChanged;   // rolou ou mudou o zoom: a automação embaixo acompanha

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;
    juce::String getTooltip() override;

    /** "×1,50": o esticar de um bloco de áudio, como aparece no cabeçalho e na barra de status. */
    static juce::String stretchLabel (double stretch);

    /** Takes e comping (gravar voz, 21/10): o grupo aberto mostra uma sub-raia por take embaixo da raia.
        A pílula "3 takes" no cabeçalho do bloco abre e fecha. */
    void setGroupOpen (const juce::String& group, bool open);
    bool isGroupOpen (const juce::String& group) const   { return openGroups.count (group) != 0; }

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    // som arrastado do navegador de sons ("sample:<caminho>"): vira clipe de áudio, como um arquivo do Explorer
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    // stretchEnd e stretchStart: segurar A e arrastar a borda direita ou esquerda de um áudio estica no tempo
    enum class Drag { none, move, resize, trimStart, erase, moveMany, select, slice, mute, slip, paint, timeSelect,
                      stretchEnd, stretchStart, fadeIn, fadeOut, gain, marker };

    /** Miniatura de um padrão: posição em ticks e faixa vertical (0..1) de cada nota ou passo. */
    struct Mark { int start, length; float y, h; };
    struct Preview { int lengthTicks = ticksPerBar4; std::vector<Mark> marks; };

    struct Block
    {
        PlaylistItem item;
        juce::String name;
        bool audio = false;
        int trackIndex = 0;
        bool takeRow = false;   // o pedaço de um take na sub-raia dele (o grupo está aberto); senão, no comp ou sozinho
    };

    struct Thumb
    {
        juce::File file;
        double seconds = 0.0;
        std::unique_ptr<juce::AudioThumbnail> thumbnail;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void updateScrollBars();
    void clampScroll();

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> headerArea() const;
    juce::Rectangle<int> rulerArea() const;
    float tickToX (double tick) const;
    double xToTick (float x) const;
    float trackToY (int index) const;
    int yToTrack (float y) const;
    juce::Rectangle<float> blockBounds (const Block&) const;
    timegrid::Meter meter() const noexcept   { return { stepsPerBeat, stepsPerBar }; }
    void notifyGridChange();
    int snapDown (double tick) const;
    int snapNearest (double tick) const;
    int contentTicks() const;
    double ticksToSeconds (double ticks) const;
    int offsetTicks (const PlaylistItem&) const;   // o corte do áudio na grade, no BPM atual
    int trackIndexOf (const juce::String& id) const;

    const Block* blockAt (juce::Point<float>, Drag& part) const;
    const Block* findBlock (const juce::String& id) const;
    bool stretchHeld() const;          // A segurado: a borda do áudio estica em vez de aparar
    void updateCursor();
    juce::Rectangle<float> menuButtonBounds (const Block&) const;   // a setinha do menu no cabeçalho do áudio
    void showBlockMenu (const juce::String& id);
    bool placeBrush (juce::Point<float>);
    void eraseAt (juce::Point<float>);
    void startDrag (const PlaylistItem&, Drag);
    void beginEdit (const juce::String&);
    void endEdit();
    void renameTrack (int index);
    void showLaneMenu (int index);   // botão direito no cabeçalho da raia: mudo, solo, cor, ordem e gravar nela

    // organizar a playlist (bloco 4, 23/10): o ponto do mudo no cabeçalho (clique = mudo, Ctrl+clique = solo)
    juce::Rectangle<float> laneLed (int index) const;
    void toggleLane (int index, bool solo);
    void laneMenuChoice (const juce::String& laneId, int index, int choice);
    juce::String laneSilence (const juce::String& laneId) const;   // "raia muda", "fora do solo" ou vazio (toca)

    // edição de vários blocos (um gesto, um passo de desfazer)
    std::vector<PlaylistItem> selectedItems() const;
    juce::String addCopy (const PlaylistItem&, int start, int trackIndex);
    void deleteSelection();
    void copySelection();
    void paste();
    void duplicateSelection();
    void moveSelectionBy (int deltaTicks, int deltaTracks);
    void sliceAt (juce::Point<float>);
    const PlaylistItem* modelItem (const juce::String& id) const;
    Preview buildPreview (const Project&, const Pattern&) const;

    void paintBlock (juce::Graphics&, const Block&, juce::Rectangle<float> clip);
    void paintTakePiece (juce::Graphics&, const Block&, juce::Rectangle<float> clip);

    // takes e comping: as sub-raias dos grupos abertos empurram as raias de baixo
    void layoutRows();
    int subRowHeight() const noexcept   { return juce::jmax (28, trackHeight * 6 / 10); }
    float laneSpan (int index) const;
    double contentHeight() const;
    juce::Rectangle<float> pillBounds (const Block&) const;
    const PlaylistItem* hoveredTakePiece() const;

    // edição do clipe (gravar voz, fatia 4): as alças de fade nos cantos de cima e a de ganho embaixo, no meio
    struct Handles { juce::Rectangle<float> fadeIn, fadeOut, gain; };
    Handles handlesOf (const Block&) const;

    // punch (3.5): a faixa vermelha na régua, onde a gravação vale
    enum class PunchEdge { none, in, out };
    PunchEdge punchEdgeAt (juce::Point<float>) const;
    void showRulerMenu (double tick);
    juce::String positionName (int tick) const;

    // marcadores na régua (bloco 1, 22/10): intro, verso, refrão. Clique pula a agulha, duplo clique seleciona
    // o trecho até o próximo, arrastar move, botão direito renomeia ou apaga.
    juce::Rectangle<float> markerBounds (const Marker&) const;
    const Marker* markerAt (juce::Point<float>) const;
    void showMarkerMenu (const Marker&);
    void renameMarker (const juce::String& id, const juce::String& current);
    void selectMarkerRange (const juce::String& id);
    void addMarkerMenu (juce::PopupMenu&, int at);

    AppController& app;
    std::vector<PlaylistTrack> tracks;
    std::vector<Block> blocks;
    std::map<juce::String, Preview> previews;
    juce::AudioThumbnailCache thumbCache { 32 };
    std::map<juce::String, Thumb> thumbs;   // por canal de áudio

    juce::String brushKind { "pattern" }, brushRef, brushName, currentPattern;
    juce::String selectedItem;               // o último bloco clicado (o pincel sai dele)
    std::set<juce::String> selection, selectionBefore;
    std::vector<PlaylistItem> originals;     // os blocos no começo de um arrasto de vários
    Tool tool = Tool::draw;
    juce::Rectangle<float> rubberBand;
    juce::Range<int> timeSelection;
    int timeAnchor = 0;
    bool muteTarget = false;
    int sliceTick = 0;
    std::set<juce::String> slicedTracks;     // cada raia corta uma vez por arrasto da tesoura
    int paintTrack = 0, paintNext = 0;       // o pincel: a raia e onde entra o próximo bloco
    static inline std::vector<PlaylistItem> clipboard;   // Ctrl+C/Ctrl+V (posições relativas)
    double bpm = 140.0;
    int stepsPerBar = 16, stepsPerBeat = 4;

    double scrollTicks = 0.0, scrollY = 0.0;
    float pxPerTick = 96.0f / (float) ticksPerBar4;
    int trackHeight = 54;
    int snapSetting = timegrid::snapAuto;
    juce::String lastAutoName;             // para avisar só quando a grade automática muda

    // de 6 px por compasso a 2 px por tick (1/16 de passo com 30 px)
    static constexpr float minPxPerTick = 6.0f / (float) ticksPerBar4, maxPxPerTick = 2.0f;
    static constexpr timegrid::Spacing spacing { 16.0f, 16.0f };

    Drag drag = Drag::none;
    bool editing = false;
    juce::Point<float> dragStart;
    PlaylistItem original;
    int lastStart = -1, lastTrack = -1, lastLength = -1;
    juce::Point<float> hoverPos { -1.0f, -1.0f };
    bool stretchKey = false;               // o A chegou pelo keyPressed e ainda não foi solto
    juce::MouseCursor stretchCursor;       // as setas da borda com um relógio: esticar no tempo
    bool dragOver = false;
    juce::Point<int> dropPos;

    double playhead = -1.0;
    int needle = -1;                       // a agulha parada (o play do SONG começa aqui)

    std::map<juce::String, playlistedit::Crossfade> crossfades;   // voz na playlist: o crossfade automático por bloco
    juce::String silenceItem;                      // voz na playlist: o bloco com a prévia do silêncio
    std::vector<juce::Range<int>> silenceRegions;
    void addVoiceMenu (juce::PopupMenu&, const PlaylistItem&);
    std::set<juce::String> openGroups;     // grupos de takes abertos (estado da tela)
    std::map<juce::String, int> groupTakes;            // quantos takes cada grupo tem
    std::map<juce::String, juce::String> groupPill;    // o pedaço do comp que leva a pílula "3 takes"
    std::vector<float> laneTops;           // o topo de cada raia, sem a rolagem, com as sub-raias das de cima
    std::vector<int> laneSubRows;          // quantas sub-raias cada raia mostra
    juce::String hoverTake;                // o pedaço de take (ou a pílula) com o mouse em cima
    juce::StringArray liveItems;

    bool punchOn = false;
    int punchIn = 0, punchOut = 0;
    PunchEdge punchDrag = PunchEdge::none;
    int dragIn = 0, dragOut = 0;           // o punch enquanto as pontas são arrastadas
    std::vector<Marker> markers;           // os do projeto, ordenados
    juce::String dragMarker;               // o marcador que está sendo arrastado
    double notifiedScroll = -1.0;          // o enquadramento que a automação já recebeu
    float notifiedZoom = -1.0f;
    juce::ScrollBar hScroll { false }, vScroll { true };
    bool updatingScrollBars = false;

    static constexpr int headerWidth = 150, rulerHeight = 28, scrollSize = 12;
};

/** Voz na playlist (27/10): tirar o silêncio e a respiração de um clipe. O limiar e o tamanho mínimo mudam a
    prévia na hora (os trechos em vermelho no bloco); Tirar apaga os trechos (ou abaixa, com a opção), um passo de desfazer. */
class SilenceTool : public juce::Component
{
public:
    SilenceTool (AppController&, PlaylistGrid&, const juce::String& item);
    ~SilenceTool() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Roda o audio.removeSilence com o que está na tela. Devolve se deu certo; `message` diz o que fez ou o erro. */
    bool apply (juce::String& message);
    void setThreshold (double db)   { threshold.setValue (db, juce::sendNotificationSync); }
    void setLowerInstead (bool on)  { lower.setToggleState (on, juce::dontSendNotification); }
    int regionCount() const noexcept   { return regions; }
    double previewSeconds() const noexcept   { return seconds; }

    std::function<void (bool ok, const juce::String& message)> onDone;

private:
    void updatePreview();

    AppController& app;
    juce::Component::SafePointer<PlaylistGrid> grid;
    juce::String item;
    juce::Label title, summary;
    juce::Slider threshold { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider minimum { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label thresholdLabel, minimumLabel;
    juce::ToggleButton lower { "Abaixar 15 dB em vez de apagar" };
    juce::TextButton cancel { "Cancelar" }, remove { "Tirar" };
    int regions = 0;
    double seconds = 0.0;
};

/** Playlist com a barra de cima: grade de encaixe, o que o clique coloca e o modo de tocar. */
class Playlist : public juce::Component
{
public:
    explicit Playlist (AppController&);

    void refresh (const Project&);
    void setPlayhead (double ticks)   { grid.setPlayhead (ticks); lanes.setPlayhead (ticks); }
    void setNeedle (int ticks)        { grid.setNeedle (ticks); }

    std::function<void (const juce::String&, bool isError)> onStatus;
    std::function<void (const juce::String& pattern)> onOpenPattern;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateSnapLabel();

    AppController& app;
    juce::ComboBox snapBox;
    SegmentedControl toolSwitch;   // P B C D E T S Y
    IconButton songButton { "Tocar a playlist", Icon::play, "Tocar a playlist (SONG)", IconButton::Style::filled };
    IconButton voiceButton { "Gravar voz", Icon::record, "Gravar voz" };   // arma a trilha Voz, conta e grava (fatia 8)
    PlaylistGrid grid;
    AutomationLanes lanes;
    bool songMode = false;

    static constexpr int toolbarHeight = theme::metric::toolbar;
};

} // namespace whynot
