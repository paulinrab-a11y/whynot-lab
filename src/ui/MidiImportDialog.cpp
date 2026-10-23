#include "MidiImportDialog.h"
#include "Theme.h"

namespace whynot
{

namespace
{
    juce::String bpmText (double bpm)   { return juce::String (bpm, bpm == std::round (bpm) ? 0 : 1); }
    juce::String notesText (int n)      { return juce::String (n) + (n == 1 ? " nota" : " notas"); }
}

MidiImportDialog::MidiImportDialog (const juce::File& f, const MidiFileInfo& i, const juce::String& name, double bpm,
                                    int needleTicks, int barTicks)
    : file (f), info (i), channelName (name), projectBpm (bpm), needle (juce::jmax (0, needleTicks))
{
    using namespace theme;
    setWantsKeyboardFocus (true);

    // uma trilha só: a lista mostra qual é e fica parada; várias: "Todas" e cada uma
    const int count = (int) info.tracks.size();

    if (count > 1)
    {
        trackBox.addItem ("Todas ("_t + juce::String (count) + " trilhas, " + notesText (info.totalNotes()) + ")", 1);
        trackBox.addSeparator();
    }

    for (int t = 0; t < count; ++t)
        trackBox.addItem (info.tracks[(size_t) t].name + ": " + notesText (info.tracks[(size_t) t].notes), count > 1 ? t + 2 : 1);

    trackBox.setSelectedId (1, juce::dontSendNotification);
    trackBox.setEnabled (count > 1);
    styleComboBox (trackBox, Icon::note);
    trackBox.setTooltip ("Quais trilhas do arquivo entram no canal"_t);

    mode.setItems ({ { "Trocar as notas"_t, Icon::none, "O que o canal toca neste padrão sai, e entram as notas do MIDI"_t },
                     { "Somar às notas"_t, Icon::plus, "As notas do MIDI entram junto com as que o canal já toca"_t } });
    mode.setSelected (lastReplace ? 0 : 1);
    mode.onChange = [this] (int) { repaint(); };

    const int bar = needle / juce::jmax (1, barTicks) + 1;
    start.setItems ({ { "No começo do padrão"_t, Icon::none, "O primeiro compasso com notas do arquivo entra no compasso 1"_t },
                      { "Na agulha: compasso "_t + juce::String (bar), Icon::none,
                        "O primeiro compasso com notas entra onde a agulha do padrão está (tocando, ou pausado com Ctrl+Espaço)"_t } });
    start.setSelected (lastAtNeedle && needle > 0 ? 1 : 0);

    importButton.onClick = [this] { accept(); };
    cancelButton.onClick = [this] { close(); };

    for (juce::Component* c : { (juce::Component*) &trackBox, (juce::Component*) &mode, (juce::Component*) &start,
                                (juce::Component*) &importButton, (juce::Component*) &cancelButton })
        addAndMakeVisible (c);
}

MidiImportDialog::Choice MidiImportDialog::getChoice() const
{
    Choice c;
    c.track = info.tracks.size() > 1 ? juce::jmax (0, trackBox.getSelectedId() - 1) : 0;
    c.replace = mode.getSelected() == 0;
    c.at = start.getSelected() == 1 ? needle : 0;
    return c;
}

void MidiImportDialog::setChoice (Choice c)
{
    if (info.tracks.size() > 1)
        trackBox.setSelectedId (juce::jlimit (0, (int) info.tracks.size(), c.track) + 1, juce::dontSendNotification);

    mode.setSelected (c.replace ? 0 : 1);
    start.setSelected (c.at > 0 ? 1 : 0);
    repaint();
}

void MidiImportDialog::accept()
{
    const auto choice = getChoice();
    lastReplace = choice.replace;
    lastAtNeedle = start.getSelected() == 1;

    // quem abriu fecha o cartão, e este componente pode sumir na chamada: nada daqui depois dela
    const auto f = file;

    if (onImport)
        onImport (f, choice);
}

void MidiImportDialog::close()
{
    if (onClose)
        onClose();
}

juce::Rectangle<int> MidiImportDialog::card() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (cardWidth, getWidth() - 16), juce::jmin (cardHeight, getHeight() - 16));
}

void MidiImportDialog::resized()
{
    using namespace theme;
    auto r = card().reduced (metric::pad + 4, 0);
    r.removeFromTop (48 + 20 + 8);   // título e a linha do arquivo

    auto row = [&r]
    {
        auto line = r.removeFromTop (44).withTrimmedLeft (labelWidth);
        return line.withSizeKeepingCentre (line.getWidth(), metric::control);
    };

    trackBox.setBounds (row());
    auto line = row();
    mode.setBounds (line.removeFromLeft (juce::jmin (line.getWidth(), mode.getIdealWidth())));
    line = row();
    start.setBounds (line.removeFromLeft (juce::jmin (line.getWidth(), start.getIdealWidth())));

    auto footer = card().removeFromBottom (64).reduced (metric::pad + 4, 16);
    importButton.setBounds (footer.removeFromRight (importButton.getIdealWidth (metric::control)));
    footer.removeFromRight (metric::gap);
    cancelButton.setBounds (footer.removeFromRight (cancelButton.getIdealWidth (metric::control)));
}

void MidiImportDialog::paint (juce::Graphics& g)
{
    using namespace theme;

    // a grade atrás escurece: agora o que vale é o cartão
    g.fillAll (colour::shade.withAlpha (0.55f));

    const auto c = card();
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 18, { 0, 6 }).drawForRectangle (g, c);
    g.setColour (colour::floating);
    g.fillRoundedRectangle (c.toFloat(), metric::radiusCard);
    g.setColour (colour::floatingEdge);
    g.drawRoundedRectangle (c.toFloat().reduced (0.5f), metric::radiusCard, 1.0f);

    auto r = c.reduced (metric::pad + 4, 0);
    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Importar MIDI em "_t + channelName, r.removeFromTop (48), juce::Justification::centredLeft, true);

    // o arquivo numa linha: nome, tipo, andamento e compasso
    juce::String about = file.getFileName() + "  ·  tipo "_t + juce::String (info.format);

    if (info.bpm > 0.0)
        about << "  ·  "_t << bpmText (info.bpm) << " BPM";

    about << "  ·  "_t << info.timeSigNum << "/" << info.timeSigDen;
    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawText (about, r.removeFromTop (20), juce::Justification::centredLeft, true);
    r.removeFromTop (8);

    auto caption = [&g, &r] (const juce::String& text)
    {
        g.setColour (colour::textFaint);
        g.setFont (captionFont());
        g.drawText (text.toUpperCase(), r.removeFromTop (44).removeFromLeft (labelWidth), juce::Justification::centredLeft);
    };

    caption ("Trilhas");
    caption ("No canal");
    caption ("Começa"_t);

    // uma linha de aviso: o andamento do arquivo e o do projeto, senão o que a troca faz
    juce::String note;

    if (info.bpm > 0.0 && std::abs (info.bpm - projectBpm) >= 0.5)
        note = "O arquivo foi feito a "_t + bpmText (info.bpm) + " BPM; aqui toca no BPM do projeto ("_t + bpmText (projectBpm)
               + "), que não muda."_t;
    else if (mode.getSelected() == 0)
        note = "Trocar tira o que o canal toca neste padrão. Ctrl+Z desfaz a importação inteira."_t;
    else
        note = "Ctrl+Z desfaz a importação inteira."_t;

    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawFittedText (note, r.removeFromTop (36).withTrimmedLeft (labelWidth), juce::Justification::centredLeft, 2);
}

void MidiImportDialog::mouseUp (const juce::MouseEvent& e)
{
    // o clique fora do cartão fecha, como num menu
    if (! card().contains (e.getPosition()))
        close();
}

bool MidiImportDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        accept();
        return true;
    }

    return false;   // o resto (Espaço para ouvir, por exemplo) segue para a janela
}

} // namespace whynot
