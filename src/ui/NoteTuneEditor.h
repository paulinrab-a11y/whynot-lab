#pragma once

// Afinação nota a nota (frente C de 27/10): o editor da voz de um bloco de áudio da playlist como blocos de nota
// sobre um teclado, com a curva fina do tom por cima e a forma de onda embaixo. Abre pela setinha do bloco
// ("Afinar nota a nota..."). Cada gesto (arrastar, soltar um controle, um botão) vira um comando com desfazer;
// a análise roda numa thread do editor e fica no cache por arquivo (NoteTuneFiles). Dono: Afinação nota a nota.

#include <JuceHeader.h>

#include "../app/AppController.h"
#include "../record/NoteTuneFiles.h"
#include "Controls.h"

#include <set>

namespace whynot
{

/** Controle de um valor numa faixa, com o nome, o trilho e o valor (com a unidade). Avisa ao mexer e ao soltar. */
class NoteTuneSlider : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    NoteTuneSlider (const juce::String& label, double min, double max, double defaultValue, const juce::String& unit, int decimals);

    void setValue (double v);
    double getValue() const noexcept   { return value; }
    void setMixed (bool m)             { mixed = m; repaint(); }   // as notas escolhidas têm valores diferentes

    std::function<void (double)> onChange, onCommit;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void enablementChanged() override  { repaint(); }

private:
    juce::Rectangle<float> track() const;
    void setFromX (float x);

    juce::String label, unit;
    double min, max, defaultValue, value;
    int decimals;
    bool mixed = false;
};

class NoteTuneEditor : public juce::Component,
                       private ProjectModel::Listener,
                       private juce::Timer
{
public:
    NoteTuneEditor (AppController&, const juce::String& itemId);
    ~NoteTuneEditor() override;

    /** O bloco sumiu (apagado, projeto novo): a janela fecha. */
    std::function<void()> onItemGone;

    /** Para o autoteste: espera a análise terminar (até `timeoutMs`) e põe o resultado na tela. */
    bool waitForAnalysis (int timeoutMs);

    // ações da tela (também nos botões e atalhos), públicas para o autoteste
    bool moveSelected (float semitones, bool fine);
    bool splitAt (int note, double seconds);
    bool mergeSelected();
    bool tuneToScale();
    bool resetSelected();
    void selectNotes (std::set<int> which);
    const std::vector<TunedNote>& shownNotes() const noexcept   { return notes; }
    const std::set<int>& selectedNotes() const noexcept         { return selected; }
    juce::Rectangle<float> noteBounds (int note) const;
    NoteTuneSlider& centerSlider() noexcept                     { return center; }
    NoteTuneSlider& flattenSlider() noexcept                    { return flatten; }
    NoteTuneSlider& transitionSlider() noexcept                 { return transition; }
    NoteTuneSlider& formantSlider() noexcept                    { return formant; }
    void setFormantFollows (bool);
    juce::String statusText() const                             { return status; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void projectChanged (const ProjectModel&) override;
    void timerCallback() override;

    void startAnalysis();
    void analysisReady (std::shared_ptr<const dsp::NoteAnalysis>, std::vector<float> peaks, juce::String error);
    bool readModel();   // false se o bloco sumiu
    void fitView();
    void refreshCurve();
    void refreshSliders();
    bool commit (std::vector<TunedNote>, bool follows, const juce::String& done);
    void applyToSelected (const std::function<void (TunedNote&)>&, bool commitNow, const juce::String& done);
    void togglePlay();

    juce::Rectangle<int> toolbarArea() const;
    juce::Rectangle<int> controlsArea() const;
    juce::Rectangle<int> keyboardArea() const;
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> waveArea() const;
    float xFor (double seconds) const;
    double secondsAt (float x) const;
    float yFor (float midi) const;
    float midiAt (float y) const;
    float rowHeight() const;
    int noteAt (juce::Point<float>) const;

    AppController& app;
    const juce::String itemId;
    juce::String channelId;
    juce::File file;
    double clipFrom = 0.0, clipTo = 1.0;     // o trecho do arquivo que o bloco toca (segundos)
    double viewFrom = 0.0, viewTo = 1.0;     // o que está na tela
    float midiLow = 48.0f, midiHigh = 72.0f;
    int itemStart = 0;
    double itemStretch = 1.0;

    juce::ThreadPool worker { 1 };
    std::shared_ptr<const dsp::NoteAnalysis> analysis;
    juce::String analysisError;
    std::vector<float> peaks;                // pico de cada pedaço do arquivo (forma de onda)
    std::atomic<bool> analysing { false }, cancelled { false };

    std::vector<TunedNote> notes;
    bool follows = false;
    std::vector<float> corrected;            // a curva que sai, quadro a quadro
    std::set<int> selected;

    enum class Drag { none, move };
    Drag drag = Drag::none;
    std::vector<TunedNote> dragStart;
    juce::Point<float> dragFrom;
    bool dragChanged = false;
    int hoverNote = -1;
    double playheadSeconds = -1.0;
    juce::String status;

    juce::Label title;
    SegmentedControl tool;
    IconButton playButton { "Tocar", Icon::play, "Tocar"_t, IconButton::Style::filled };
    IconButton scaleButton { "Escala", Icon::magnet, "Escala do projeto"_t };
    IconButton mergeButton { "Juntar", Icon::slide, "Juntar"_t };
    IconButton resetButton { "Original", Icon::undo, "Voltar ao original"_t };
    juce::ToggleButton followButton { "Timbre junto com o tom"_t };
    NoteTuneSlider center { "Centralizar"_t, 0.0, 100.0, 0.0, "%", 0 };
    NoteTuneSlider flatten { "Achatar vibrato"_t, 0.0, 100.0, 0.0, "%", 0 };
    NoteTuneSlider transition { "Transição"_t, 0.0, 300.0, 40.0, "ms", 0 };
    NoteTuneSlider formant { "Formante"_t, -12.0, 12.0, 0.0, "st", 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteTuneEditor)
};

/** A janela solta do editor (uma por bloco; abrir de novo traz a mesma para a frente). */
class NoteTuneWindow : public juce::DocumentWindow
{
public:
    /** Abre (ou traz para a frente) o editor do bloco. `owner` é qualquer componente da janela principal. */
    static void open (AppController&, const juce::String& itemId, juce::Component* owner);
    static void closeAll();

    NoteTuneWindow (AppController&, const juce::String& itemId);
    void closeButtonPressed() override;

    const juce::String itemId;
};

} // namespace whynot
