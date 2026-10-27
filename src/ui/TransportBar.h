#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "LevelMeter.h"
#include "../model/Project.h"

namespace whynot
{

class AppController;

/** Tap tempo (bloco 1, 22/10): o BPM é a média dos intervalos das últimas batidas (até 8), arredondada no
    inteiro. Uma pausa de mais de 2 s recomeça a conta. */
class TapTempo
{
public:
    static constexpr double restartMs = 2000.0;

    /** Uma batida no instante `ms` (relógio em milissegundos); devolve o BPM, ou 0 com uma batida só. */
    double tap (double ms)
    {
        if (! times.empty() && (ms - times.back() > restartMs || ms <= times.back()))
            times.clear();

        times.push_back (ms);

        if (times.size() > 8)
            times.erase (times.begin());

        if (times.size() < 2)
            return 0.0;

        const double average = (times.back() - times.front()) / (double) (times.size() - 1);
        return juce::jlimit (20.0, 999.0, std::round (60000.0 / average));
    }

    int count() const noexcept               { return (int) times.size(); }
    bool idleSince (double ms) const noexcept { return times.empty() || ms - times.back() > restartMs; }

private:
    std::vector<double> times;
};

/** BPM no visor: arraste para cima/baixo, roda do mouse, duplo clique para digitar. */
class BpmDisplay : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    explicit BpmDisplay (AppController&);

    void setBpm (double);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void apply (double);

    AppController& app;
    double bpm = 140.0, dragStart = 140.0;
    bool dragging = false;
    juce::TextEditor editor;
};

/** Medidor de CPU e de falhas de buffer (3.5): a carga da thread de áudio, o pico e as falhas desde o
    último zerar. Acende em laranja com o aviso (CPU em 85% ou mais, ou falha nos últimos 3 s).
    Um clique zera as falhas. */
class PerformanceMeter : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    explicit PerformanceMeter (AppController&);

    void update (float cpu, float cpuPeak, int xruns, bool overloaded);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int idealWidth = 144;

private:
    AppController& app;
    float cpu = 0.0f, peak = 0.0f;
    int xruns = 0;
    bool overloaded = false;
};

/** Barra de cima, em três grupos:
      esquerda: Arquivo, desfazer, refazer, salvar;
      centro:   PAT | SONG, tocar, parar, gravar, o visor (posição, BPM, compasso), loop e metrônomo;
      direita:  o padrão que toca, novo padrão, a CPU e o medidor da saída master.
*/
class TransportBar : public juce::Component
{
public:
    explicit TransportBar (AppController&);
    ~TransportBar() override;

    void refresh (const Project&);
    void tick();

    /** Uma batida do tap tempo (o botão TAP, ou a tecla do editor de atalhos). As batidas seguidas viram um
        passo de desfazer só, fechado 2 s depois da última. */
    void tapTempo();

    std::function<void()> onFileMenu, onSave;
    std::function<void()> onTypingKeyboard;   // o botão do teclado-piano (Ctrl+T)
    std::function<void (const juce::String&, bool isError)> onStatus;

    /** Acende o botão do teclado-piano (quem liga e desliga é a janela). */
    void setTypingKeyboard (bool on);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // botão direito no REC: a contagem

private:
    void showCountInMenu();

    AppController& app;

    IconButton fileButton { "Arquivo", Icon::none, "Arquivo" };
    IconButton undoButton { "Desfazer", Icon::undo };
    IconButton redoButton { "Refazer", Icon::redo };
    IconButton saveButton { "Salvar", Icon::save };

    SegmentedControl modeSwitch { true };
    IconButton playButton { "Tocar", Icon::play, {}, IconButton::Style::filled };
    IconButton stopButton { "Parar", Icon::stop, {}, IconButton::Style::filled };
    IconButton recButton { "Gravar", Icon::record, {}, IconButton::Style::filled };
    BpmDisplay bpm;
    juce::ComboBox timeSig;
    IconButton autoRecButton { "Gravar automacao", Icon::automation };   // (26/10) gravar automação mexendo nos controles
    IconButton loopButton { "Loop", Icon::loop };
    IconButton metronomeButton { "Metronomo", Icon::metronome };
    IconButton typingButton { "Teclado-piano", Icon::keyboard };
    IconButton tapButton { "Tap tempo", Icon::none, "TAP" };
    TapTempo tapper;
    bool tapGesture = false;          // as batidas seguidas estão num passo de desfazer aberto

    // legendas curtas embaixo dos botões do centro (revisão de iniciante, 22/10): onde e o quê
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;

    juce::ComboBox patternBox;
    IconButton newPatternButton { "Novo padrao", Icon::plus };
    PerformanceMeter performance;
    LevelMeter masterMeter { false };

    juce::Rectangle<int> lcdArea, positionSection, bpmSection, signatureSection, speakerArea;
    juce::String position { "1.1.1" };
    bool countingIn = false;          // gravando com contagem: a POSIÇÃO mostra os tempos que faltam
    bool recMenuClick = false;        // o botão direito no REC abriu o menu: o clique dele não liga o REC
    juce::StringArray patternIds, patternNames;
    bool refreshing = false;
};

} // namespace whynot
