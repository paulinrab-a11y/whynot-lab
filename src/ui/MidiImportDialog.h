#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "../engine/MidiImport.h"

namespace whynot
{

/** Importar MIDI (pedido do produtor, 22/10): as opções curtas antes das notas entrarem, como a janela de
    importar MIDI do FL, num cartão por cima do piano roll (sem janela solta, que tiraria o foco):
    - quais trilhas entram (todas ou uma);
    - trocar as notas do canal neste padrão ou somar a elas;
    - onde começa: no começo do padrão ou no compasso da agulha.
    Importar (ou Enter) chama onImport; Cancelar, Esc ou o clique fora do cartão chamam onClose.
    Somar/trocar e onde começa ficam lembrados na sessão, como a última pasta do arquivo.
*/
class MidiImportDialog : public juce::Component
{
public:
    struct Choice
    {
        int track = 0;          // 0 = todas; N = a N-ª de MidiFileInfo::tracks
        bool replace = true;
        int at = 0;             // ticks do padrão
    };

    /** needleTicks: o começo do compasso da agulha no padrão (0 = o primeiro compasso). */
    MidiImportDialog (const juce::File&, const MidiFileInfo&, const juce::String& channelName, double projectBpm, int needleTicks, int barTicks);

    std::function<void (const juce::File&, Choice)> onImport;
    std::function<void()> onClose;

    Choice getChoice() const;
    const juce::File& getFile() const noexcept   { return file; }

    /** Escolhe por código (o autoteste e o comando não passam pelo mouse). */
    void setChoice (Choice);

    /** O mesmo que o botão Importar. */
    void accept();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int cardWidth = 540, cardHeight = 320;

private:
    juce::Rectangle<int> card() const;
    void close();

    juce::File file;
    MidiFileInfo info;
    juce::String channelName;
    double projectBpm;
    int needle;

    juce::ComboBox trackBox;
    SegmentedControl mode, start;
    IconButton importButton { "Importar", Icon::check, "Importar", IconButton::Style::filled };
    IconButton cancelButton { "Cancelar", Icon::none, "Cancelar" };

    static inline bool lastReplace = true, lastAtNeedle = false;
    static constexpr int labelWidth = 110;
};

} // namespace whynot
