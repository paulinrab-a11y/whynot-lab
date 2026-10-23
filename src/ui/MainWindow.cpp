#include "MainWindow.h"
#include "MainComponent.h"
#include "fx/FxEditor.h"
#include "synth/SynthEditor.h"
#include "../app/AppController.h"

namespace whynot
{

MainWindow::MainWindow (AppController& a)
    : juce::DocumentWindow ("WHYNOT LAB", juce::Colours::black, juce::DocumentWindow::allButtons),
      app (a)
{
    setUsingNativeTitleBar (true);
    setIcon (theme::iconImage (256));
    content = new MainComponent (app);
    setContentOwned (content, true);
    setResizable (true, true);
    setResizeLimits (960, 600, 10000, 10000);

    // Cabe na área útil do monitor (notebook 1366x768 ou monitor grande de estúdio).
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1440, 900);
    centreWithSize (juce::jmin (1600, area.getWidth() - 40), juce::jmin (1000, area.getHeight() - 40));

    if (area.getWidth() < 1500 || area.getHeight() < 900)
        setFullScreen (true);   // em tela pequena, maximiza (o Windows mantém a barra de título)

    setVisible (true);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

bool MainWindow::confirmDiscardChanges()
{
    app.capturePluginStates();   // mexer num plugin também é alteração não salva

    if (! app.getModel().dirty)
        return true;

    const int choice = juce::NativeMessageBox::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon,
                                                                   "Salvar alterações?"_t,
                                                                   "O projeto tem alterações que ainda não foram salvas."_t,
                                                                   this, nullptr);
    if (choice == 0)   // cancelar
        return false;

    if (choice == 1)   // sim
        return content->saveProject (false);

    return true;       // não: descarta
}

bool MainWindow::saveFxScreenshot (const juce::File& file, const juce::String& trackName, int slot)
{
    for (auto& m : app.getModel().get().mixer)
    {
        if (m.name != trackName || slot < 0 || slot >= (int) m.inserts.size())
            continue;

        auto* fx = app.getProjector().fxFor (m.id, m.inserts[(size_t) slot].id);

        if (fx == nullptr)
            return false;

        FxEditor editor (app, *fx);
        auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (image, out);
    }

    return false;
}

bool MainWindow::saveSynthScreenshot (const juce::File& file, const juce::String& channelName, int page, int layer)
{
    for (auto& c : app.getModel().get().channels)
    {
        if (c.name != channelName || ! c.isSynth())
            continue;

        auto* synthPlugin = app.getProjector().synthFor (c.id);

        if (synthPlugin == nullptr)
            return false;

        SynthEditor editor (app, *synthPlugin);
        editor.showPage (page, layer);
        auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (image, out);
    }

    return false;
}

void MainWindow::showView (const juce::String& panel)
{
    if (panel == "playlist")         content->showView (View::playlist);
    else if (panel == "pianoRoll")   content->showView (View::pianoRoll);
    else if (panel == "projects")    content->showView (View::projects);
    else if (panel == "plugins")     { content->showView (View::channelRack); content->setPluginsVisible (true); }
    else if (panel == "sounds")      { content->showView (View::channelRack); content->setSoundsVisible (true); }
    else if (panel.startsWith ("ideias:"))
    {
        // "ideias:<canal>[+plugins|+sons][@LxA]": piano roll do canal com o painel do Ideias, um navegador
        // aberto e a janela num tamanho fixo (ex.: @1366x768, a tela pequena onde a barra precisa caber)
        auto rest = panel.fromFirstOccurrenceOf (":", false, false);

        if (rest.contains ("@"))
        {
            const auto size = rest.fromLastOccurrenceOf ("@", false, false);
            rest = rest.upToLastOccurrenceOf ("@", false, false);
            const int w = size.upToFirstOccurrenceOf ("x", false, true).getIntValue();
            const int h = size.fromFirstOccurrenceOf ("x", false, true).getIntValue();

            if (w >= 640 && h >= 480)
            {
                setResizeLimits (640, 480, 10000, 10000);
                setSize (w, h);
            }
        }

        const bool withPlugins = rest.endsWith ("+plugins"), withSounds = rest.endsWith ("+sons");
        content->showIdeas (withPlugins ? rest.dropLastCharacters (8) : withSounds ? rest.dropLastCharacters (5) : rest);

        if (withPlugins)
            content->setPluginsVisible (true);

        if (withSounds)
            content->setSoundsVisible (true);
    }
    else if (panel.startsWith ("mixer:"))
    {
        // "mixer:<id ou nome da trilha>": channel rack em cima e o mixer com a trilha escolhida no painel de efeitos
        content->showView (View::channelRack);
        content->selectMixerTrack (panel.fromFirstOccurrenceOf (":", false, false));
    }
    else                             content->showView (View::channelRack);
}

bool MainWindow::saveScreenshot (const juce::File& file)
{
    auto image = content->createComponentSnapshot (content->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}

} // namespace whynot
