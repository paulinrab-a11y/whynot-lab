#include "ExportDialogs.h"
#include "Theme.h"
#include "../app/AppController.h"
#include "../engine/Exporter.h"
#include "../engine/Mp3Encoder.h"

namespace whynot
{

namespace
{
    juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();
        for (auto& [k, v] : kv) o->setProperty (k, v);
        return juce::var (o);
    }

    constexpr int width = 540;
    constexpr int labelWidth = 110;
    constexpr int bitrates[] { 128, 192, 256, 320 };

    const char* extensionFor (ExportDialog::Format f)
    {
        switch (f)
        {
            case ExportDialog::Format::mp3:   return ".mp3";
            case ExportDialog::Format::midi:  return ".mid";
            case ExportDialog::Format::wav:
            case ExportDialog::Format::stems:
            case ExportDialog::Format::pack:  break;
        }

        return ".wav";
    }
}

ExportDialog::ExportDialog (AppController& a, Format initial) : app (a)
{
    using namespace theme;

    format.setItems ({ { "WAV", Icon::none, "WAV sem perdas, na taxa e nos bits que você escolher"_t },
                       { "MP3", Icon::none, "MP3 44,1 kHz com o LAME, para mandar e ouvir no celular"_t },
                       { "MIDI", Icon::none, "As notas, uma faixa por canal, para levar a outro programa"_t },
                       { "Stems", Icon::none, "Um WAV por trilha do mixer, para mixar em outro lugar"_t },
                       { "Para vender", Icon::none, "O pacote de quem vende beat: o WAV limpo, o MP3 com a sua tag, os stems e um zip, "
                                                    "com o BPM e o tom no nome"_t } });
    format.setSelected ((int) initial);
    format.onChange = [this] (int) { layoutChanged(); };

    // com um trecho selecionado na régua da playlist (Ctrl+arrastar), ele vira a terceira opção
    range = app.getTimeSelection();
    std::vector<SegmentedControl::Item> whatItems {
                     { "Padrão atual"_t, Icon::pattern, "O padrão que está no channel rack, repetido"_t },
                     { "Música inteira"_t, Icon::playlist, "A playlist do começo ao fim do último bloco"_t } };

    if (! range.isEmpty())
        whatItems.push_back ({ "Trecho"_t, Icon::select, "Só o trecho selecionado na régua da playlist"_t });

    what.setItems (whatItems);

    // Com blocos na playlist, o padrão é a música inteira, mesmo no modo PAT: quem pôs o beat e gravou a voz
    // quer o MP3 com tudo (revisão de iniciante, 22/10). O trecho fica a um clique: uma seleção de tempo
    // esquecida (a da gravação em loop, por exemplo) não pode cortar a música sem querer.
    what.setSelected (initialWhat (app.getModel().get()));
    what.onChange = [this] (int) { layoutChanged(); };

    quality.setItems ({ { "128" }, { "192" }, { "256" }, { "320 kbps" } });
    quality.setSelected (3);
    quality.onChange = [this] (int) { repaint(); };

    // WAV: 44,1 kHz é o que lojas de beat e distribuidoras pedem; 16 bits sai com dither (revisão profissional, 22/10)
    wavRate.setItems ({ { "44,1 kHz", Icon::none, "O padrão de CD, lojas de beat e distribuidoras"_t },
                        { "48 kHz", Icon::none, "O padrão de vídeo e o do app"_t },
                        { "96 kHz", Icon::none, "Alta resolução, arquivo maior"_t } });
    wavRate.setSelected (1);
    wavRate.onChange = [this] (int) { repaint(); };
    wavBits.setItems ({ { "16 bits", Icon::none, "Com dither: o que CD e muitas lojas pedem"_t },
                        { "24 bits", Icon::none, "O normal para mandar para mixagem e master"_t },
                        { "32 float", Icon::none, "Sem limite de volume dentro do arquivo"_t } });
    wavBits.setSelected (1);
    wavBits.onChange = [this] (int) { repaint(); };

    for (int n : { 1, 2, 4, 8, 16 })
        loopsBox.addItem (juce::String (n) + (n == 1 ? " vez" : " vezes"), n);

    loopsBox.setSelectedId (initial == Format::midi ? 1 : 4, juce::dontSendNotification);
    styleComboBox (loopsBox, Icon::loop);
    loopsBox.setTooltip ("Quantas vezes o padrão se repete no arquivo"_t);

    vbr.onClick = [this] { quality.setEnabled (! vbr.getToggleState()); repaint(); };

    masterChain.setTooltip ("Desligado (o normal para mandar stems): os efeitos do master ficam de fora e a soma dos stems dá a mixagem"_t);
    masterChain.onClick = [this] { repaint(); };

    // Stems: todas as trilhas de mixer; marcadas as que recebem som
    const auto defaults = app.defaultStemTracks();

    for (auto& m : app.getModel().get().mixer)
    {
        if (m.isMaster())
            continue;

        StemRow row;
        row.id = m.id;
        row.toggle = std::make_unique<juce::ToggleButton> (m.name);
        row.toggle->setToggleState (defaults.contains (m.id), juce::dontSendNotification);
        row.toggle->onClick = [this] { repaint(); };
        stemList.addAndMakeVisible (*row.toggle);
        stems.push_back (std::move (row));
    }

    // Para vender: a tag guardada (preferência, fora do projeto), a cada quantos segundos, e o que entra
    tagFile = app.producerTag();
    tagButton.setShowsMenuArrow (false);
    tagButton.setTooltip ("A tag do produtor (a vinheta com o seu nome, em WAV ou MP3, até 30 s): ela toca no MP3 de divulgação. "
                          "Fica guardada para as próximas vezes."_t);
    tagButton.onClick = [this] { chooseTag(); };
    noTagButton.setTooltip ("O MP3 sai sem tag"_t);
    noTagButton.onClick = [this]
    {
        tagFile = {};
        app.run ("export.setProducerTag", args ({ { "path", juce::String() } }));
        updateTagButton();
    };

    for (int s : { 15, 20, 30, 45, 60 })
        everyBox.addItem ("A cada "_t + juce::String (s) + " s", s);

    everyBox.setSelectedId (30, juce::dontSendNotification);
    styleComboBox (everyBox, Icon::timer);
    everyBox.setTooltip ("De quanto em quanto tempo a tag toca no MP3 (a primeira logo no começo)"_t);
    everyBox.onChange = [this] { repaint(); };

    for (auto* t : { &packWav, &packMp3, &packStems, &packZip })
        t->onClick = [this] { updateTagButton(); repaint(); };

    packWav.setToggleState (true, juce::dontSendNotification);
    packMp3.setToggleState (true, juce::dontSendNotification);
    packZip.setToggleState (true, juce::dontSendNotification);
    packWav.setTooltip ("A música inteira em WAV 44,1 kHz / 24 bits, sem tag: o arquivo que o comprador recebe"_t);
    packMp3.setTooltip ("MP3 320 kbps com a tag, para divulgar e mandar de amostra"_t);
    packStems.setTooltip ("Um WAV por trilha do mixer (os trackouts), para quem vai mixar a voz no beat"_t);
    packZip.setTooltip ("Um zip da pasta, pronto para mandar ao comprador"_t);
    updateTagButton();

    stemView.setViewedComponent (&stemList, false);
    stemView.setScrollBarsShown (true, false);

    allButton.setTooltip ("Marca todas as trilhas; de novo, desmarca"_t);
    allButton.onClick = [this]
    {
        const bool anyOff = std::any_of (stems.begin(), stems.end(), [] (const StemRow& r) { return ! r.toggle->getToggleState(); });

        for (auto& r : stems)
            r.toggle->setToggleState (anyOff, juce::dontSendNotification);

        repaint();
    };

    exportButton.onClick = [this]
    {
        if (finished)
        {
            // pronto: mostra o arquivo no Explorer (os stems são uma pasta: abre a própria pasta)
            if (doneFile.isDirectory())
                doneFile.startAsProcess();
            else
                doneFile.revealToUser();

            close();
            return;
        }

        startExport();
    };

    cancelButton.onClick = [this]
    {
        if (exporting)
            app.run ("export.cancel");   // o diálogo fica até o render parar
        else
            close();
    };

    progressBar.setPercentageDisplay (true);
    addChildComponent (progressBar);

    for (juce::Component* c : { (juce::Component*) &format, (juce::Component*) &what, (juce::Component*) &quality,
                                (juce::Component*) &loopsBox, (juce::Component*) &vbr, (juce::Component*) &masterChain, (juce::Component*) &stemView,
                                (juce::Component*) &allButton, (juce::Component*) &exportButton, (juce::Component*) &cancelButton,
                                (juce::Component*) &wavRate, (juce::Component*) &wavBits,
                                (juce::Component*) &tagButton, (juce::Component*) &noTagButton, (juce::Component*) &everyBox,
                                (juce::Component*) &packWav, (juce::Component*) &packMp3, (juce::Component*) &packStems, (juce::Component*) &packZip })
        addAndMakeVisible (c);

    layoutChanged();
}

ExportDialog::~ExportDialog()
{
    stopTimer();

    if (exporting)
        app.run ("export.cancel");   // fechou a janela no meio: não deixa render solto
}

void ExportDialog::show (AppController& app, Format f, juce::Component* parent,
                         std::function<void (const juce::String&, bool)> status)
{
    auto dialog = std::make_unique<ExportDialog> (app, f);
    dialog->onStatus = std::move (status);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (dialog.release());
    o.dialogTitle = "Exportar";
    o.dialogBackgroundColour = theme::colour::panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.escapeKeyTriggersCloseButton = true;
    o.componentToCentreAround = parent;
    o.launchAsync();
}

int ExportDialog::loops() const
{
    return juce::jmax (1, loopsBox.getSelectedId());
}

void ExportDialog::layoutChanged()
{
    const auto f = currentFormat();
    const bool isStems = f == Format::stems;
    const bool isPack = f == Format::pack;

    // o pacote é sempre a música inteira: o "O que sai" some
    what.setVisible (! isPack);

    for (juce::Component* c : { (juce::Component*) &tagButton, (juce::Component*) &noTagButton, (juce::Component*) &everyBox,
                                (juce::Component*) &packWav, (juce::Component*) &packMp3, (juce::Component*) &packStems, (juce::Component*) &packZip })
        c->setVisible (isPack);

    quality.setVisible (f == Format::mp3);
    wavRate.setVisible (f == Format::wav);
    wavBits.setVisible (f == Format::wav);
    vbr.setVisible (f == Format::mp3);
    loopsBox.setVisible (! exportsSong() && ! isPack);
    stemView.setVisible (isStems);
    allButton.setVisible (isStems);
    masterChain.setVisible (isStems);
    exportButton.setEnabled (f != Format::mp3 || mp3EncoderAvailable());

    // a altura acompanha o formato: o diálogo cresce para a lista de trilhas
    int height = 48 + 3 * 44 + 92;

    if (isPack)
        height += 44;   // sem o "O que sai", com a tag e os arquivos

    if (f == Format::mp3)
        height += 44 + 32;

    if (f == Format::wav)
        height += 44;

    if (isStems)
        height += 44 + juce::jlimit (1, 7, (int) stems.size()) * rowHeight + 8;

    setSize (width, height);
    resized();
    repaint();
}

void ExportDialog::resized()
{
    using namespace theme;
    auto r = getLocalBounds().reduced (metric::pad + 4, 0);
    r.removeFromTop (48);   // título

    auto row = [&r] { auto line = r.removeFromTop (44); return line.withSizeKeepingCentre (line.getWidth(), metric::control); };

    auto line = row();
    line.removeFromLeft (labelWidth);
    format.setBounds (line.removeFromLeft (format.getIdealWidth()));

    if (currentFormat() == Format::pack)
    {
        line = row();
        line.removeFromLeft (labelWidth);
        tagButton.setBounds (line.removeFromLeft (juce::jmin (220, tagButton.getIdealWidth (metric::control))));
        line.removeFromLeft (metric::gap);
        noTagButton.setBounds (line.removeFromLeft (noTagButton.getIdealWidth (metric::control)));
        line.removeFromLeft (metric::gap);
        everyBox.setBounds (line.removeFromLeft (juce::jmin (line.getWidth(), 130)));

        line = row();
        line.removeFromLeft (labelWidth);

        for (auto* t : { &packWav, &packMp3, &packStems, &packZip })
        {
            t->setBounds (line.removeFromLeft (t == &packMp3 ? 130 : 96));
            line.removeFromLeft (metric::gap);
        }
    }
    else
    {
        line = row();
        line.removeFromLeft (labelWidth);
        what.setBounds (line.removeFromLeft (what.getIdealWidth()));
        line.removeFromLeft (metric::gap);
        loopsBox.setBounds (line.removeFromLeft (120));
    }

    if (currentFormat() == Format::wav)
    {
        line = row();
        line.removeFromLeft (labelWidth);
        wavRate.setBounds (line.removeFromLeft (wavRate.getIdealWidth()));
        line.removeFromLeft (metric::gap);
        wavBits.setBounds (line.removeFromLeft (wavBits.getIdealWidth()));
    }

    if (currentFormat() == Format::mp3)
    {
        line = row();
        line.removeFromLeft (labelWidth);
        quality.setBounds (line.removeFromLeft (quality.getIdealWidth()));
        auto v = r.removeFromTop (32);
        v.removeFromLeft (labelWidth);
        vbr.setBounds (v.removeFromLeft (320));
    }

    if (currentFormat() == Format::stems)
    {
        line = row();
        line.removeFromLeft (labelWidth);
        allButton.setBounds (line.removeFromLeft (allButton.getIdealWidth (metric::control)));
        line.removeFromLeft (metric::gap * 2);
        masterChain.setBounds (line.removeFromLeft (juce::jmin (line.getWidth(), 240)));

        auto list = r.removeFromTop (juce::jlimit (1, 7, (int) stems.size()) * rowHeight + 8);
        list.removeFromLeft (labelWidth);
        stemView.setBounds (list);
        stemList.setSize (list.getWidth() - (stems.size() > 7 ? stemView.getScrollBarThickness() : 0), (int) stems.size() * rowHeight);

        for (size_t i = 0; i < stems.size(); ++i)
            stems[i].toggle->setBounds (0, (int) i * rowHeight, stemList.getWidth(), rowHeight);
    }

    auto footer = getLocalBounds().removeFromBottom (64).reduced (metric::pad + 4, 16);
    // entre o resumo e o rodapé (sobram 32 px nos três tamanhos do diálogo)
    progressBar.setBounds (getLocalBounds().reduced (metric::pad + 4, 0).withTrimmedLeft (labelWidth)
                               .withY (getHeight() - 64 - 30).withHeight (24));
    exportButton.setBounds (footer.removeFromRight (exportButton.getIdealWidth (metric::control)));
    footer.removeFromRight (metric::gap);
    cancelButton.setBounds (footer.removeFromRight (cancelButton.getIdealWidth (metric::control)));
}

void ExportDialog::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    auto r = getLocalBounds().reduced (metric::pad + 4, 0);
    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Exportar " + app.projectTitle(), r.removeFromTop (48), juce::Justification::centredLeft, true);

    auto caption = [&g] (const juce::String& text, juce::Rectangle<int> line)
    {
        g.setColour (colour::textFaint);
        g.setFont (captionFont());
        g.drawText (text.toUpperCase(), line.removeFromLeft (labelWidth), juce::Justification::centredLeft);
    };

    caption ("Formato", r.removeFromTop (44));

    if (currentFormat() == Format::pack)
    {
        caption ("Tag", r.removeFromTop (44));
        caption ("Arquivos", r.removeFromTop (44));
    }
    else
    {
        caption ("O que sai", r.removeFromTop (44));
    }

    if (currentFormat() == Format::wav)
        caption ("Qualidade", r.removeFromTop (44));

    if (currentFormat() == Format::mp3)
    {
        caption ("Qualidade", r.removeFromTop (44));
        r.removeFromTop (32);
    }

    if (currentFormat() == Format::stems)
    {
        caption ("Trilhas", r.removeFromTop (44));
        r.removeFromTop (juce::jlimit (1, 7, (int) stems.size()) * rowHeight + 8);
    }

    // o que vai sair, numa linha
    juce::String summary;

    switch (currentFormat())
    {
        case Format::wav:
            summary = "WAV "_t + (wavSampleRate() == 44100.0 ? juce::String ("44,1 kHz") : juce::String (juce::roundToInt (wavSampleRate() / 1000.0)) + " kHz")
                      + " / " + (wavBitDepth() == 32 ? juce::String ("32 bits float") : juce::String (wavBitDepth()) + " bits")
                      + (wavBitDepth() == 16 ? ", com dither" : "") + ", com os efeitos do mixer e do master.";
            break;
        case Format::mp3:
            summary = ! mp3EncoderAvailable() ? "Esta versão do app saiu sem o codificador de MP3. Exporte em WAV, que toca em qualquer lugar."_t
                    : vbr.getToggleState()    ? "MP3 44,1 kHz, taxa variável (LAME V0)."_t
                                              : "MP3 44,1 kHz, " + juce::String (bitrates[juce::jlimit (0, 3, quality.getSelected())]) + " kbps (LAME).";
            break;
        case Format::midi:  summary = "Arquivo MIDI: uma faixa por canal, com o andamento e o compasso."; break;
        case Format::stems:
        {
            const auto n = std::count_if (stems.begin(), stems.end(), [] (const StemRow& s) { return s.toggle->getToggleState(); });
            summary = juce::String ((int) n) + (n == 1 ? " WAV" : " WAVs") + " 48 kHz / 24 bits, uma trilha em solo por arquivo, "
                      + (masterChain.getToggleState() ? "com os efeitos do master."_t : "sem os efeitos do master."_t);
            break;
        }
        case Format::pack:
        {
            juce::StringArray parts;
            if (packWav.getToggleState())   parts.add ("o WAV limpo");
            if (packMp3.getToggleState())   parts.add (tagFile != juce::File() ? "o MP3 com a tag a cada "_t + juce::String (everyBox.getSelectedId()) + " s"
                                                                              : "o MP3 (sem tag)"_t);
            if (packStems.getToggleState()) parts.add ("os stems");
            summary = parts.isEmpty() ? "Marque pelo menos um arquivo para o pacote."_t
                                      : "Numa pasta com o nome, o BPM e o tom do beat: "_t + parts.joinIntoString (", ")
                                            + (packZip.getToggleState() ? ", e um zip de tudo."_t : ".");
            break;
        }
    }

    if (exporting)
        summary = (stage.isNotEmpty() ? stage : juce::String ("Exportando")) + "... a tela continua livre, e Parar cancela."_t;

    if (finished)
        summary = "Pronto: "_t + doneFile.getFileName() + " em " + doneFile.getParentDirectory().getFullPathName()
                  + (doneWarning.isNotEmpty() ? ". " + doneWarning : juce::String());

    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawFittedText (summary, r.removeFromTop (40).withTrimmedLeft (labelWidth), juce::Justification::centredLeft, 2);
}

juce::File ExportDialog::defaultFolder() const
{
    const auto& model = app.getModel();
    return model.file != juce::File() ? model.projectFolder().getChildFile ("renders") : app.getDefaultProjectsFolder();
}

void ExportDialog::startExport()
{
    const auto f = currentFormat();
    const auto folder = defaultFolder();
    folder.createDirectory();
    juce::Component::SafePointer<ExportDialog> safe (this);

    if (f == Format::stems || f == Format::pack)
    {
        chooser = std::make_unique<juce::FileChooser> (f == Format::pack ? "Pasta do pacote para vender"_t : juce::String ("Pasta dos stems"), folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult() != juce::File())
                                      safe->runExport (fc.getResult());
                              });
        return;
    }

    const auto ext = extensionFor (f);
    chooser = std::make_unique<juce::FileChooser> ("Exportar " + juce::String (ext).substring (1).toUpperCase(),
                                                   folder.getChildFile (safeFileName (app.projectTitle()) + ext),   // "AUX" vira "AUX_": nome reservado do Windows
                                                   juce::String ("*") + ext);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult() != juce::File())
                                  safe->runExport (fc.getResult());
                          });
}

void ExportDialog::runExport (const juce::File& target)
{
    const auto f = currentFormat();
    const auto whatText = exportsSong() ? juce::String ("song") : juce::String ("pattern");
    const auto& p = app.getModel().get();
    const int barTicks = juce::jmax (1, juce::jmax (1, p.timeSigNum) * juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen)) * ticksPerStep);
    const auto describeWhat = exportsRange() ? "o trecho dos compassos "_t + juce::String (range.getStart() / barTicks + 1) + " a "
                                                   + juce::String ((range.getEnd() - 1) / barTicks + 1)
                            : exportsSong()  ? juce::String ("a playlist inteira")
                                             : juce::String (loops()) + (loops() == 1 ? " vez o padrão"_t : " repetições do padrão"_t);
    CommandResult result;
    juce::String done;

    // só o trecho: o mesmo comando com from/to (em ticks), na música inteira
    auto withRange = [this] (juce::var a)
    {
        if (exportsRange())
        {
            a.getDynamicObject()->setProperty ("from", range.getStart());
            a.getDynamicObject()->setProperty ("to", range.getEnd());
        }

        return a;
    };

    switch (f)
    {
        case Format::wav:
            result = app.run ("project.exportWav", withRange (args ({ { "path", target.getFullPathName() }, { "what", whatText }, { "loops", loops() },
                                                                      { "sampleRate", wavSampleRate() }, { "bitDepth", wavBitDepth() },
                                                                      { "background", true } })));
            done = "WAV exportado: " + target.withFileExtension ("wav").getFileName() + " (" + describeWhat + ")";
            break;

        case Format::mp3:
        {
            const int kbps = bitrates[juce::jlimit (0, 3, quality.getSelected())];
            result = app.run ("project.exportMp3", withRange (args ({ { "path", target.getFullPathName() }, { "what", whatText }, { "loops", loops() },
                                                                      { "bitrate", kbps }, { "vbr", vbr.getToggleState() }, { "background", true } })));
            done = "MP3 exportado: " + target.withFileExtension ("mp3").getFileName() + " (" + describeWhat + ", "
                   + (vbr.getToggleState() ? juce::String ("VBR") : juce::String (kbps) + " kbps") + ")";
            break;
        }

        case Format::midi:
            result = app.run ("project.exportMidi", args ({ { "path", target.getFullPathName() }, { "what", whatText }, { "loops", loops() } }));
            done = "MIDI exportado: " + target.withFileExtension ("mid").getFileName() + " ("
                   + (exportsRange() ? "a música inteira: o trecho vale para WAV, MP3 e stems"_t : describeWhat) + ")";
            break;

        case Format::stems:
        {
            juce::Array<juce::var> ids;

            for (auto& s : stems)
                if (s.toggle->getToggleState())
                    ids.add (s.id);

            if (ids.isEmpty())
            {
                if (onStatus) onStatus ("Marque pelo menos uma trilha para exportar stems.", true);
                return;
            }

            result = app.run ("project.exportStems", withRange (args ({ { "folder", target.getFullPathName() }, { "tracks", ids },
                                                                        { "masterChain", masterChain.getToggleState() },
                                                                        { "what", whatText }, { "loops", loops() }, { "background", true } })));
            const int n = result.ok ? (int) result.value["steps"] : 0;
            done = juce::String (n) + (n == 1 ? " stem exportado em " : " stems exportados em ") + target.getFullPathName();
            break;
        }

        case Format::pack:
        {
            if (! packWav.getToggleState() && ! packMp3.getToggleState() && ! packStems.getToggleState())
            {
                if (onStatus) onStatus ("Marque pelo menos um arquivo para o pacote: o WAV, o MP3 ou os stems."_t, true);
                return;
            }

            result = app.run ("project.exportPack", args ({ { "folder", target.getFullPathName() },
                                                            { "wav", packWav.getToggleState() }, { "mp3", packMp3.getToggleState() },
                                                            { "stems", packStems.getToggleState() }, { "zip", packZip.getToggleState() },
                                                            { "tag", tagFile.getFullPathName() }, { "tagEvery", everyBox.getSelectedId() } }));
            const juce::File pack (result.value["folder"].toString());
            done = "Pacote para vender pronto: "_t + pack.getFileName() + (packZip.getToggleState() ? " (e o zip ao lado)"_t : juce::String());
            break;
        }
    }

    if (! result.ok)
    {
        if (onStatus) onStatus (result.error, true);
        return;
    }

    // o que "Abrir a pasta" mostra no fim
    doneFile = f == Format::stems ? target
             : f == Format::pack  ? juce::File (result.value["folder"].toString())
             : target.withFileExtension (f == Format::wav ? "wav" : f == Format::mp3 ? "mp3" : "mid");

    // MIDI sai na hora; os outros rodam em segundo plano e o diálogo acompanha
    if (f == Format::midi)
    {
        if (onStatus) onStatus (done, false);
        showFinished();
        return;
    }

    doneMessage = done;
    stage = result.value["stage"].toString();
    progressValue = 0.0;
    showProgress (true);
    startTimer (100);
}

void ExportDialog::showProgress (bool on)
{
    exporting = on;

    for (juce::Component* c : { (juce::Component*) &format, (juce::Component*) &what, (juce::Component*) &quality,
                                (juce::Component*) &loopsBox, (juce::Component*) &vbr, (juce::Component*) &masterChain,
                                (juce::Component*) &stemView, (juce::Component*) &allButton,
                                (juce::Component*) &wavRate, (juce::Component*) &wavBits,
                                (juce::Component*) &tagButton, (juce::Component*) &noTagButton, (juce::Component*) &everyBox,
                                (juce::Component*) &packWav, (juce::Component*) &packMp3, (juce::Component*) &packStems, (juce::Component*) &packZip })
        c->setEnabled (! on);

    exportButton.setEnabled (! on && (currentFormat() != Format::mp3 || mp3EncoderAvailable()));
    cancelButton.setLabel (on ? "Parar" : "Cancelar");
    cancelButton.setTooltip (on ? "Para a exportação; o arquivo pela metade é apagado"_t : juce::String());
    progressBar.setVisible (on);
    resized();
    repaint();
}

void ExportDialog::timerCallback()
{
    const auto status = app.run ("export.status").value;
    progressValue = (double) status["progress"];
    stage = status["stage"].toString();
    repaint();

    if (! (bool) status["finished"])
        return;

    stopTimer();
    exporting = false;

    if ((bool) status["ok"])
    {
        // deu certo, mas o WAV clipou: o aviso vai junto, como erro, para não passar despercebido
        doneWarning = status["warning"].toString();

        if (onStatus) onStatus (doneWarning.isNotEmpty() ? doneMessage + ". " + doneWarning : doneMessage, doneWarning.isNotEmpty());
        showFinished();
        return;
    }

    if (onStatus)
        onStatus ((bool) status["cancelled"] ? "Exportação cancelada."_t : status["message"].toString(), ! (bool) status["cancelled"]);

    showProgress (false);
}

void ExportDialog::chooseTag()
{
    const auto start = tagFile != juce::File() ? tagFile.getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    tagChooser = std::make_unique<juce::FileChooser> ("A tag do produtor (WAV ou MP3)"_t, start, "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");
    juce::Component::SafePointer<ExportDialog> safe (this);

    tagChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        const auto result = safe->app.run ("export.setProducerTag", args ({ { "path", fc.getResult().getFullPathName() } }));

        if (! result.ok)
        {
            if (safe->onStatus) safe->onStatus (result.error, true);
            return;
        }

        safe->tagFile = fc.getResult();
        safe->updateTagButton();
    });
}

void ExportDialog::updateTagButton()
{
    tagButton.setLabel (tagFile != juce::File() ? tagFile.getFileName() : "Escolher a tag..."_t);
    noTagButton.setEnabled (tagFile != juce::File());
    everyBox.setEnabled (tagFile != juce::File() && packMp3.getToggleState());
    packMp3.setButtonText (tagFile != juce::File() ? "MP3 com a tag"_t : juce::String ("MP3"));
    resized();
    repaint();
}

double ExportDialog::wavSampleRate() const
{
    return wavRate.getSelected() == 0 ? 44100.0 : wavRate.getSelected() == 2 ? 96000.0 : 48000.0;
}

int ExportDialog::wavBitDepth() const
{
    return wavBits.getSelected() == 0 ? 16 : wavBits.getSelected() == 2 ? 32 : 24;
}

int ExportDialog::initialWhat (const Project& p)
{
    return ! p.playlistItems.empty() || p.songMode ? 1 : 0;
}

void ExportDialog::showFinished()
{
    // O diálogo fica aberto com "Pronto" e a pasta: o iniciante não sabe onde o arquivo foi parar.
    showProgress (false);
    finished = true;

    for (juce::Component* c : { (juce::Component*) &format, (juce::Component*) &what, (juce::Component*) &quality,
                                (juce::Component*) &loopsBox, (juce::Component*) &vbr, (juce::Component*) &masterChain,
                                (juce::Component*) &stemView, (juce::Component*) &allButton,
                                (juce::Component*) &wavRate, (juce::Component*) &wavBits,
                                (juce::Component*) &tagButton, (juce::Component*) &noTagButton, (juce::Component*) &everyBox,
                                (juce::Component*) &packWav, (juce::Component*) &packMp3, (juce::Component*) &packStems, (juce::Component*) &packZip })
        c->setEnabled (false);

    exportButton.setLabel ("Abrir a pasta");
    exportButton.setIcon (Icon::folder);
    exportButton.setTooltip ("Mostra o arquivo exportado no Explorer"_t);
    exportButton.setEnabled (true);
    cancelButton.setLabel ("Fechar");
    resized();
    repaint();
}

void ExportDialog::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}

} // namespace whynot
