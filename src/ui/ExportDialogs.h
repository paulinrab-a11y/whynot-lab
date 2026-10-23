#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "../common/Text.h"

namespace whynot
{

class AppController;
struct Project;

/** Exportar (Ctrl+R WAV, Ctrl+Shift+R MP3, Ctrl+Shift+M MIDI): um diálogo só, como o render do FL.
    Escolhe o formato, o que sai (o padrão repetido ou a música inteira), a qualidade do MP3 e,
    nos stems, quais trilhas do mixer viram um WAV cada. "Exportar" pergunta onde gravar e roda o comando.
    WAV, MP3 e stems rodam em segundo plano: o diálogo mostra o andamento e o Cancelar para o render.
    Fechar o diálogo no meio cancela a exportação.
*/
class ExportDialog : public juce::Component,
                     private juce::Timer
{
public:
    enum class Format { wav, mp3, midi, stems, pack };   // pack: "Para vender" (bloco 2, 23/10)

    ExportDialog (AppController&, Format);
    ~ExportDialog() override;

    /** Abre o diálogo centrado em `parent`. As mensagens de resultado vão para `status`. */
    static void show (AppController&, Format, juce::Component* parent,
                      std::function<void (const juce::String&, bool isError)> status);

    std::function<void (const juce::String&, bool isError)> onStatus;

    /** O que o diálogo propõe ao abrir: 1 = a música inteira (há blocos na playlist ou o modo SONG), 0 = o padrão. */
    static int initialWhat (const Project&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct StemRow
    {
        juce::String id;
        std::unique_ptr<juce::ToggleButton> toggle;
    };

    Format currentFormat() const   { return (Format) format.getSelected(); }
    bool exportsSong() const       { return what.getSelected() >= 1; }    // a música inteira ou o trecho dela
    bool exportsRange() const      { return what.getSelected() == 2; }    // o trecho selecionado na régua da playlist
    int loops() const;
    void chooseTag();
    void updateTagButton();
    double wavSampleRate() const;
    int wavBitDepth() const;
    void layoutChanged();
    void startExport();
    void runExport (const juce::File&);
    juce::File defaultFolder() const;
    void close();
    void showProgress (bool on);
    void showFinished();
    void timerCallback() override;

    AppController& app;
    SegmentedControl format { true }, what, quality, wavRate, wavBits;
    juce::ComboBox loopsBox;
    juce::ToggleButton vbr { "Taxa variável (VBR, qualidade máxima)"_t };
    juce::ToggleButton masterChain { "Com os efeitos do master"_t };
    std::vector<StemRow> stems;
    juce::Component stemList;
    juce::Viewport stemView;
    IconButton allButton { "Todas", Icon::check, "Todas" };

    // Para vender (bloco 2, 23/10): a tag do produtor no MP3 e o que entra no pacote
    IconButton tagButton { "Tag do produtor", Icon::folder, "Escolher a tag..." };
    IconButton noTagButton { "Sem tag", Icon::close, "Sem tag" };
    juce::ComboBox everyBox;
    juce::ToggleButton packWav { "WAV limpo" }, packMp3 { "MP3 com a tag" }, packStems { "Stems" }, packZip { "Zip" };
    juce::File tagFile;
    std::unique_ptr<juce::FileChooser> tagChooser;
    IconButton exportButton { "Exportar", Icon::save, "Exportar...", IconButton::Style::filled };
    IconButton cancelButton { "Cancelar", Icon::none, "Cancelar" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Range<int> range;   // a seleção de tempo da playlist quando o diálogo abriu

    // exportação em andamento (export.status a cada 100 ms)
    bool exporting = false;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::String stage, doneMessage;

    // terminou: "Abrir a pasta" mostra o arquivo (ou a pasta dos stems)
    bool finished = false;
    juce::File doneFile;
    juce::String doneWarning;   // deu certo, mas com aviso (o WAV clipou)

    static constexpr int rowHeight = 30;
};

} // namespace whynot
