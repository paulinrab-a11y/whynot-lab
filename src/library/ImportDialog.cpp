#include "ImportDialog.h"

#include "ProjectIndex.h"
#include "../ui/Theme.h"

namespace whynot
{

namespace
{
    constexpr int headerHeight = 60;    // título e a linha de baixo
    constexpr int captionHeight = 24;
    constexpr int maxVisibleRows = 6;
}

ImportDialog::ImportDialog (library::ImportSource s, RunImport r) : source (std::move (s)), run (std::move (r))
{
    for (auto& info : source.patterns)
    {
        juce::String text = info.name + "   ·   "_t + info.channels.joinIntoString (", ");
        juce::StringArray counts;

        if (info.steps > 0)
            counts.add (juce::String (info.steps) + (info.steps == 1 ? " passo" : " passos"));

        if (info.notes > 0)
            counts.add (juce::String (info.notes) + (info.notes == 1 ? " nota" : " notas"));

        if (! counts.isEmpty())
            text << "   ·   "_t << counts.joinIntoString (", ");

        auto* toggle = patternToggles.add (new juce::ToggleButton (text));
        toggle->setToggleState (true, juce::dontSendNotification);
        toggle->onClick = [this] { updateButton(); };
        listContent.addAndMakeVisible (toggle);
    }

    viewport.setViewedComponent (&listContent, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    mixerToggle.setToggleState (true, juce::dontSendNotification);
    mixerToggle.setTooltip ("Ligado: cada canal vem com a trilha dele e os efeitos. Desligado: só o volume e o pan."_t);
    transposeToggle.setTooltip ("As notas do piano roll andam para o tom do projeto aberto (o caminho mais curto). Os passos ficam."_t);

    importButton.setStyle (IconButton::Style::filled);
    importButton.onClick = [this] { runImport(); };
    cancelButton.onClick = [this]
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    };

    for (juce::Component* c : { (juce::Component*) &mixerToggle, (juce::Component*) &transposeToggle,
                                (juce::Component*) &importButton, (juce::Component*) &cancelButton })
        addAndMakeVisible (c);

    const int h = theme::metric::pad + headerHeight + captionHeight + listHeight() + theme::metric::gap
                + captionHeight + 2 * rowHeight + 28 + theme::metric::control + theme::metric::pad;
    setSize (width, h);
    updateButton();
}

void ImportDialog::show (juce::Component* parent, library::ImportSource s, RunImport r)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ImportDialog (std::move (s), std::move (r)));
    o.dialogTitle = "Importar de outro projeto";
    o.dialogBackgroundColour = theme::colour::panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.escapeKeyTriggersCloseButton = true;
    o.componentToCentreAround = parent;
    o.launchAsync();
}

int ImportDialog::listHeight() const
{
    return juce::jmax (1, juce::jmin (maxVisibleRows, patternToggles.size())) * rowHeight;
}

void ImportDialog::updateButton()
{
    bool any = false;

    for (auto* t : patternToggles)
        any = any || t->getToggleState();

    importButton.setEnabled (any);
}

void ImportDialog::runImport()
{
    library::ImportOptions options;
    options.mixer = mixerToggle.getToggleState();
    options.transpose = transposeToggle.getToggleState();

    for (int i = 0; i < patternToggles.size() && i < (int) source.patterns.size(); ++i)
        if (patternToggles[i]->getToggleState())
            options.patterns.add (source.patterns[(size_t) i].id);

    error = run != nullptr ? run (options) : juce::String ("Importar não está disponível aqui."_t);

    if (error.isEmpty())
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (1);

        return;
    }

    repaint();
}

void ImportDialog::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    auto r = getLocalBounds().reduced (metric::pad);
    auto header = r.removeFromTop (headerHeight);
    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Importar de "_t + source.file.getFileNameWithoutExtension(), header.removeFromTop (24), juce::Justification::centredLeft, true);

    const auto& p = source.project;
    library::ProjectEntry key;
    key.keyRoot = p.keyRoot;
    key.scale = p.scale;
    const int n = (int) source.patterns.size();
    g.setColour (colour::textDim);
    g.setFont (font (type::small, Weight::medium));
    g.drawText (juce::String (juce::roundToInt (p.bpm)) + " BPM  ·  "_t + key.keyName() + "  ·  "_t
                    + juce::String (n) + (n == 1 ? " padrão com conteúdo"_t : " padrões com conteúdo"_t),
                header.removeFromTop (18), juce::Justification::centredLeft, true);
    g.setColour (colour::textFaint);
    g.setFont (font (type::small));
    g.drawText ("Os canais vêm com os padrões; o que já existe aqui ganha \"(de "_t + source.file.getFileNameWithoutExtension() + ")\".",
                header.removeFromTop (18), juce::Justification::centredLeft, true);

    g.setColour (colour::textFaint);
    g.setFont (captionFont());
    g.drawText ("PADRÕES"_t, r.removeFromTop (captionHeight), juce::Justification::centredLeft, false);
    r.removeFromTop (listHeight() + metric::gap);
    g.drawText ("OPÇÕES"_t, r.removeFromTop (captionHeight), juce::Justification::centredLeft, false);
    r.removeFromTop (2 * rowHeight);

    if (error.isNotEmpty())
    {
        g.setColour (colour::meterHigh);
        g.setFont (font (type::small, Weight::medium));
        g.drawFittedText (error, r.removeFromTop (28), juce::Justification::centredLeft, 2, 0.9f);
    }
}

void ImportDialog::resized()
{
    using namespace theme;
    auto r = getLocalBounds().reduced (metric::pad);
    r.removeFromTop (headerHeight + captionHeight);

    viewport.setBounds (r.removeFromTop (listHeight()));
    const bool scrolls = patternToggles.size() > maxVisibleRows;
    listContent.setSize (viewport.getWidth() - (scrolls ? viewport.getScrollBarThickness() : 0), patternToggles.size() * rowHeight);

    for (int i = 0; i < patternToggles.size(); ++i)
        patternToggles[i]->setBounds (0, i * rowHeight, listContent.getWidth(), rowHeight);

    r.removeFromTop (metric::gap + captionHeight);
    mixerToggle.setBounds (r.removeFromTop (rowHeight));
    transposeToggle.setBounds (r.removeFromTop (rowHeight));
    r.removeFromTop (28);   // a linha do erro

    auto buttons = r.removeFromTop (metric::control);
    importButton.setBounds (buttons.removeFromRight (importButton.getIdealWidth (metric::control)));
    buttons.removeFromRight (metric::gap);
    cancelButton.setBounds (buttons.removeFromRight (cancelButton.getIdealWidth (metric::control)));
}

} // namespace whynot
