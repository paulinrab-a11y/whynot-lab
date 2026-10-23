#include "ProjectsView.h"

#include "ImportDialog.h"
#include "../model/Project.h"
#include "../ui/Theme.h"

namespace whynot
{

using library::PreviewState;
using library::ProjectLibrary;

namespace
{
    constexpr int headerRow = 24;   // legendas das colunas
    constexpr int footerRow = 28;   // o que a fila de prévias está fazendo

    juce::String minutes (double seconds)
    {
        const int s = juce::jmax (0, (int) std::floor (seconds));
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    juce::String decimal (double v, int places)
    {
        return juce::String (v, places).replaceCharacter ('.', ',');
    }

    juce::String bpmText (double bpm)
    {
        return std::abs (bpm - std::round (bpm)) < 0.005 ? juce::String (juce::roundToInt (bpm)) : decimal (bpm, 2);
    }

    juce::String whenText (juce::Time t)
    {
        const auto now = juce::Time::getCurrentTime();
        const auto day = [] (juce::Time x) { return x.getYear() * 1000 + x.getDayOfYear(); };
        const auto clock = t.formatted ("%H:%M");

        if (day (t) == day (now))
            return "hoje "_t + clock;

        if (day (t) == day (now - juce::RelativeTime::days (1)))
            return "ontem "_t + clock;

        return t.getYear() == now.getYear() ? t.formatted ("%d/%m %H:%M") : t.formatted ("%d/%m/%Y");
    }

    void drawWave (juce::Graphics& g, juce::Rectangle<float> area, const std::vector<float>& peaks,
                   juce::Colour ink, float playedFraction = -1.0f, juce::Colour played = {})
    {
        if (peaks.empty() || area.getWidth() < 2.0f)
            return;

        const int columns = juce::jmax (1, (int) area.getWidth());
        const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;

        for (int x = 0; x < columns; ++x)
        {
            const auto from = (size_t) ((size_t) x * peaks.size() / (size_t) columns);
            const auto to = juce::jmax (from + 1, (size_t) ((size_t) (x + 1) * peaks.size() / (size_t) columns));
            float peak = 0.0f;

            for (auto i = from; i < to && i < peaks.size(); ++i)
                peak = juce::jmax (peak, peaks[i]);

            const float h = juce::jmax (0.5f, peak * half);
            const bool isPlayed = playedFraction >= 0.0f && (float) x / (float) columns < playedFraction;
            g.setColour (isPlayed ? played : ink);
            g.fillRect (area.getX() + (float) x, mid - h, 1.0f, h * 2.0f);
        }
    }
}

//==============================================================================
/** Os decks A e B, no pé da tela. Em cima, os controles: tocar e parar, qual deck soa (A | B), a sincronia,
    igualar o volume, o loop e a referência externa. Embaixo, uma raia por deck: a letra, o nome, os números,
    a onda inteira (clique leva ao ponto) e o tempo. Laranja só no deck que soa enquanto toca. */
class ProjectsView::DeckStrip : public juce::Component
{
public:
    explicit DeckStrip (ProjectsView& v) : view (v)
    {
        playButton.setStyle (IconButton::Style::filled);
        stopButton.setStyle (IconButton::Style::filled);
        playButton.onClick = [this] { if (view.lib != nullptr) view.lib->toggleDeck(); refresh(); };
        stopButton.onClick = [this] { if (view.lib != nullptr) view.lib->stopDeck(); refresh(); };

        abSwitch.setItems ({ { "A", Icon::none, "Ouvir o deck A (Tab troca)"_t },
                             { "B", Icon::none, "Ouvir o deck B (Tab troca)"_t } });
        abSwitch.onChange = [this] (int index) { if (view.lib != nullptr) view.lib->setActiveDeck (index); refresh(); };

        syncBox.addItem ("Mesmo compasso", 1);
        syncBox.addItem ("Mesmo segundo", 2);
        syncBox.addItem ("Livre", 3);
        theme::styleComboBox (syncBox, Icon::magnet);
        syncBox.setTooltip ("Onde o outro deck entra: no mesmo compasso (pelo BPM de cada projeto), no mesmo segundo ou livre"_t);
        syncBox.onChange = [this]
        {
            using S = library::ProjectLibrary::Sync;
            if (view.lib != nullptr)
                view.lib->setSync (syncBox.getSelectedId() == 2 ? S::second : (syncBox.getSelectedId() == 3 ? S::free : S::bar));
        };

        matchButton.setClickingTogglesState (true);
        matchButton.onClick = [this] { if (view.lib != nullptr) view.lib->setMatchLoudness (matchButton.getToggleState()); refresh(); };

        loopBox.addItem ("Sem loop", 1);

        for (int bars : { 1, 2, 4, 8, 16 })
            loopBox.addItem ("Loop de "_t + juce::String (bars) + (bars == 1 ? " compasso" : " compassos"), bars + 1);

        theme::styleComboBox (loopBox, Icon::loop);
        loopBox.setTooltip ("Repete N compassos a partir do compasso de agora, nos dois decks"_t);
        loopBox.onChange = [this] { if (view.lib != nullptr) view.lib->setLoopBars (juce::jmax (0, loopBox.getSelectedId() - 1)); };

        referenceButton.onClick = [this] { chooseReference(); };

        for (juce::Component* c : { (juce::Component*) &playButton, (juce::Component*) &stopButton, (juce::Component*) &abSwitch,
                                    (juce::Component*) &syncBox, (juce::Component*) &matchButton, (juce::Component*) &loopBox,
                                    (juce::Component*) &referenceButton })
            addAndMakeVisible (c);

        refresh();
    }

    void refresh()
    {
        auto* pl = view.lib;
        const bool loaded = pl != nullptr && (pl->getDecks().isLoaded (0) || pl->getDecks().isLoaded (1));
        const bool playing = loaded && pl->isDeckPlaying();
        playButton.setIcon (playing ? Icon::pause : Icon::play);
        playButton.setTooltip (playing ? "Pausar os decks"_t : "Tocar os decks"_t);
        playButton.setToggleState (playing, juce::dontSendNotification);
        playButton.setEnabled (loaded);
        stopButton.setEnabled (loaded);

        if (pl != nullptr)
        {
            using S = library::ProjectLibrary::Sync;
            abSwitch.setSelected (pl->getActiveDeck());
            syncBox.setSelectedId (pl->getSync() == S::second ? 2 : (pl->getSync() == S::free ? 3 : 1), juce::dontSendNotification);
            matchButton.setToggleState (pl->getMatchLoudness(), juce::dontSendNotification);
            loopBox.setSelectedId (pl->getLoopBars() + 1, juce::dontSendNotification);
        }

        repaint();
    }

    void resized() override
    {
        using namespace theme;
        auto row = controlsRow();
        playButton.setBounds (row.removeFromLeft (40));
        row.removeFromLeft (2);
        stopButton.setBounds (row.removeFromLeft (40));
        row.removeFromLeft (metric::pad);
        abSwitch.setBounds (row.removeFromLeft (juce::jmax (88, abSwitch.getIdealWidth())));
        row.removeFromLeft (metric::gap);
        syncBox.setBounds (row.removeFromLeft (178));
        row.removeFromLeft (metric::gap);
        matchButton.setBounds (row.removeFromLeft (matchButton.getIdealWidth (metric::control)));
        row.removeFromLeft (metric::gap);
        loopBox.setBounds (row.removeFromLeft (190));
        referenceButton.setBounds (row.removeFromRight (referenceButton.getIdealWidth (metric::control)));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.setColour (colour::line);
        g.fillRect (getLocalBounds().removeFromTop (1).reduced (metric::pad, 0));

        for (int d = 0; d < library::DeckPlayer::numDecks; ++d)
            paintLane (g, d);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int d = 0; d < library::DeckPlayer::numDecks; ++d)
        {
            if (! lane (d).contains (e.getPosition()))
                continue;

            if (labelArea (d).contains (e.getPosition()))
            {
                if (view.lib != nullptr)
                    view.lib->setActiveDeck (d);

                refresh();
            }
            else
            {
                draggingDeck = d;
                seekTo (d, e.position);
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingDeck >= 0)
            seekTo (draggingDeck, e.position);
    }

    void mouseUp (const juce::MouseEvent&) override   { draggingDeck = -1; }

private:
    juce::Rectangle<int> content() const       { return getLocalBounds().reduced (theme::metric::pad, 0).withTrimmedTop (1); }
    juce::Rectangle<int> controlsRow() const   { return content().removeFromTop (48).withSizeKeepingCentre (content().getWidth(), theme::metric::control); }

    juce::Rectangle<int> lane (int d) const
    {
        auto r = content().withTrimmedTop (48);
        return r.removeFromTop (laneHeight * (d + 1)).removeFromBottom (laneHeight).withTrimmedBottom (4);
    }

    juce::Rectangle<int> labelArea (int d) const   { return lane (d).withWidth (juce::jmin (280, lane (d).getWidth() / 3)); }

    juce::Rectangle<float> waveArea (int d) const
    {
        auto r = lane (d);
        r.removeFromLeft (labelArea (d).getWidth() + theme::metric::gap);
        r.removeFromRight (112);
        return r.toFloat();
    }

    void paintLane (juce::Graphics& g, int d)
    {
        using namespace theme;
        auto* pl = view.lib;

        if (pl == nullptr)
            return;

        const auto& info = pl->deckInfo (d);
        const bool loaded = pl->getDecks().isLoaded (d);
        const bool isActive = pl->getActiveDeck() == d;
        const bool sounding = isActive && loaded && pl->isDeckPlaying();
        auto label = labelArea (d);

        // a letra: laranja no deck que soa, pílula clara no que vai soar quando tocar, apagada no outro
        auto badge = label.removeFromLeft (28).withSizeKeepingCentre (24, 24).toFloat();

        if (sounding)
        {
            g.setColour (colour::signalTint);
            g.fillRoundedRectangle (badge, metric::radiusSmall);
        }
        else if (isActive)
        {
            g.setColour (colour::raisedHigh);
            g.fillRoundedRectangle (badge, metric::radiusSmall);
        }

        g.setColour (sounding ? colour::signal : (isActive ? colour::text : colour::textFaint));
        g.setFont (font (type::body, Weight::bold));
        g.drawText (d == 0 ? "A" : "B", badge, juce::Justification::centred, false);
        label.removeFromLeft (8);

        if (! loaded)
        {
            g.setColour (colour::textFaint);
            g.setFont (font (type::small));
            g.drawFittedText (d == 0 ? "Vazio. O \u25B6 de uma linha toca aqui."_t
                                     : "Vazio. Shift+\u25B6 numa linha carrega aqui, ou Referência."_t,
                              label, juce::Justification::centredLeft, 2, 0.9f);
            return;
        }

        auto text = label.withSizeKeepingCentre (label.getWidth(), 36);
        g.setColour (isActive ? colour::text : colour::textDim);
        g.setFont (font (type::body, Weight::semibold));
        g.drawText (info.name, text.removeFromTop (18), juce::Justification::centredLeft, true);

        juce::StringArray facts;

        if (info.bpm > 0.0)
            facts.add (bpmText (info.bpm) + " BPM");
        else
            facts.add ("referência"_t);

        if (info.lufsKnown)
            facts.add (decimal (info.lufs, 1) + " LUFS");
        else if (info.project == juce::File())
            facts.add ("medindo..."_t);

        if (const float adjust = pl->matchGainDb (d); std::abs (adjust) >= 0.05f)
            facts.add (decimal (adjust, 1) + " dB");

        g.setColour (colour::textFaint);
        g.setFont (font (type::small, Weight::medium));
        g.drawText (facts.joinIntoString ("  \u00B7  "_t), text, juce::Justification::centredLeft, true);

        // a onda: fundo de controle, o trecho do loop em laranja bem leve, o que já tocou mais claro e a agulha
        const auto wave = waveArea (d);
        const auto inner = wave.reduced (8.0f, 6.0f);
        const double length = pl->deckLength (d);
        const double position = pl->deckPosition (d);
        const float fraction = length > 0.0 ? (float) juce::jlimit (0.0, 1.0, position / length) : 0.0f;

        g.setColour (colour::raised);
        g.fillRoundedRectangle (wave, metric::radius);

        if (const auto loop = pl->getDecks().loopOf (d); loop.active() && length > 0.0)
        {
            const float x0 = inner.getX() + (float) (loop.start / length) * inner.getWidth();
            const float x1 = inner.getX() + (float) (loop.end / length) * inner.getWidth();
            g.setColour (colour::signal.withAlpha (0.10f));   // o loop ligado: um laranja bem leve, a onda continua legível
            g.fillRect (juce::Rectangle<float> (x0, wave.getY(), juce::jmax (1.0f, x1 - x0), wave.getHeight()));
        }

        const auto* item = info.project != juce::File() ? pl->find (info.project) : nullptr;

        if (item != nullptr && ! item->preview.peaks.empty())
            drawWave (g, inner, item->preview.peaks, colour::textFaint, fraction, isActive ? colour::textDim.brighter (0.3f) : colour::textDim);
        else
            drawWave (g, inner, flatPeaks(), colour::textFaint.withAlpha (0.5f), fraction, colour::textDim);

        const float x = inner.getX() + fraction * inner.getWidth();
        g.setColour (sounding ? colour::signal : (isActive ? colour::text : colour::textDim));
        g.fillRect (juce::Rectangle<float> (x - 1.0f, wave.getY() + 2.0f, 2.0f, wave.getHeight() - 4.0f));

        auto time = lane (d).removeFromRight (104).toFloat();
        g.setColour (isActive ? colour::text : colour::textDim);
        drawTabularText (g, minutes (position) + " / " + minutes (length), time, numberFont (13.0f), juce::Justification::centredRight);
    }

    static const std::vector<float>& flatPeaks()
    {
        // referência sem a onda pronta: uma linha discreta
        static const std::vector<float> flat (64, 0.08f);
        return flat;
    }

    void seekTo (int d, juce::Point<float> p)
    {
        const auto inner = waveArea (d).reduced (8.0f, 6.0f);

        if (view.lib == nullptr || ! view.lib->getDecks().isLoaded (d))
            return;

        view.lib->seekDeck (d, juce::jlimit (0.0f, 1.0f, (p.x - inner.getX()) / inner.getWidth()) * view.lib->deckLength (d));
        repaint();
    }

    void chooseReference()
    {
        chooser = std::make_unique<juce::FileChooser> ("Música de referência para o deck B"_t, juce::File(),
                                                       "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg");
        juce::Component::SafePointer<DeckStrip> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || safe->view.lib == nullptr || fc.getResult() == juce::File())
                                      return;

                                  const auto r = safe->view.lib->loadDeck (1, fc.getResult());
                                  safe->view.status (r.wasOk() ? "Referência no deck B: "_t + fc.getResult().getFileName() + ". Tab troca entre A e B."
                                                               : r.getErrorMessage(), r.failed());
                                  safe->refresh();
                              });
    }

    static constexpr int laneHeight = 52;

    ProjectsView& view;
    IconButton playButton { "Tocar os decks"_t, Icon::play },
               stopButton { "Parar e voltar ao início"_t, Icon::stop },
               matchButton { "Igualar o volume pelo LUFS: o deck mais alto desce até o outro"_t, Icon::speaker, "Igualar volume" },
               referenceButton { "Carregar uma música de referência no deck B (WAV, MP3, FLAC)"_t, Icon::folder, "Referência"_t };
    SegmentedControl abSwitch { true };
    juce::ComboBox syncBox, loopBox;
    std::unique_ptr<juce::FileChooser> chooser;
    int draggingDeck = -1;
};

//==============================================================================
/** A barra lateral: as vistas da lista (Todos, Recentes, Favoritos), os status, as tags e as pastas.
    Linhas de 28 px; a escolhida ganha a pílula clara; a contagem fica à direita em textFaint. */
class ProjectsView::Sidebar : public juce::Component,
                              public juce::DragAndDropTarget
{
public:
    explicit Sidebar (ProjectsView& v) : view (v)   {}

    void rebuild()
    {
        using F = library::ProjectLibrary::Filter;
        using S = library::ProjectLibrary::Status;
        entries.clear();
        auto* pl = view.lib;

        if (pl == nullptr)
            return;

        auto add = [this] (Kind kind, const juce::String& label, Icon icon, F f = {}, int count = -1, bool removable = false)
        {
            entries.push_back ({ kind, label, icon, f, count, removable });
        };

        int recentCount = 0;

        for (auto& item : pl->items())
            recentCount += item.recent ? 1 : 0;

        F f;
        add (Kind::view, "Todos", Icon::playlist, f, (int) pl->items().size());
        f.view = F::View::recent;
        add (Kind::view, "Recentes", Icon::timer, f, recentCount);
        f.view = F::View::favorites;
        add (Kind::view, "Favoritos", Icon::starFilled, f, pl->countFavorites());

        add (Kind::caption, "STATUS", Icon::none);

        for (auto s : { S::idea, S::production, S::mix, S::done })
        {
            F fs;
            fs.view = F::View::status;
            fs.status = s;
            add (Kind::view, library::ProjectLibrary::statusLabel (s), Icon::none, fs, pl->countWithStatus (s));
        }

        add (Kind::caption, "TAGS", Icon::none);
        const auto tags = pl->allTags();

        if (tags.empty())
            add (Kind::hint, "Botão direito numa linha: Tags"_t, Icon::none);

        for (auto& [tag, count] : tags)
        {
            F ft;
            ft.view = F::View::tag;
            ft.tag = tag;
            add (Kind::view, "#" + tag, Icon::none, ft, count);
        }

        add (Kind::caption, "PASTAS", Icon::none);

        for (auto& folder : pl->getRoots())
        {
            F ff;
            ff.view = F::View::folder;
            ff.folder = folder;
            add (Kind::view, folder.getFileName(), Icon::folder, ff);
        }

        for (auto& folder : pl->getExtraFolders())
        {
            F ff;
            ff.view = F::View::folder;
            ff.folder = folder;
            add (Kind::view, folder.getFileName(), Icon::folder, ff, -1, true);
        }

        add (Kind::add, "Adicionar pasta"_t, Icon::plus);

        add (Kind::caption, "COMBOS", Icon::none);

        for (auto& combo : pl->combos())
        {
            F fc;
            fc.view = F::View::combo;
            fc.combo = combo.name;
            add (Kind::view, combo.name, Icon::pattern, fc, (int) combo.items.size(), true);
        }

        add (Kind::addCombo, "Novo combo", Icon::plus);
        repaint();
    }

    int getIdealHeight() const
    {
        int h = 8;

        for (auto& e : entries)
            h += e.kind == Kind::caption ? captionHeight : entryHeight;

        return h;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto r = getLocalBounds().reduced (8, 0);

        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            auto row = r.removeFromTop (e.kind == Kind::caption ? captionHeight : entryHeight);

            if (e.kind == Kind::caption)
            {
                g.setColour (colour::textFaint);
                g.setFont (captionFont());
                g.drawText (e.label, row.withTrimmedLeft (8).withTrimmedTop (10), juce::Justification::centredLeft, false);
                continue;
            }

            if (e.kind == Kind::hint)
            {
                g.setColour (colour::textFaint);
                g.setFont (font (type::small));
                g.drawFittedText (e.label, row.withTrimmedLeft (8), juce::Justification::centredLeft, 2, 0.9f);
                continue;
            }

            const bool chosen = e.kind == Kind::view && isChosen (e.filter);
            const bool over = (int) i == hover;
            const bool dropHere = (int) i == dropTarget;
            const bool playingCombo = e.filter.view == library::ProjectLibrary::Filter::View::combo
                                      && view.lib->playingCombo().equalsIgnoreCase (e.filter.combo);

            if (chosen || over)
            {
                g.setColour (chosen ? colour::raisedHigh : colour::raised);
                g.fillRoundedRectangle (row.toFloat().reduced (0.0f, 1.0f), metric::radius);
            }

            // o alvo de soltar acende em laranja (o mesmo sinal do contorno do rack ao arrastar)
            if (dropHere)
            {
                g.setColour (colour::signalTint);
                g.fillRoundedRectangle (row.toFloat().reduced (0.0f, 1.0f), metric::radius);
                g.setColour (colour::signal);
                g.drawRoundedRectangle (row.toFloat().reduced (0.5f, 1.5f), metric::radius, 1.0f);
            }

            auto content = row.reduced (8, 0);

            if (e.icon != Icon::none)
            {
                const bool starOn = e.icon == Icon::starFilled && e.count > 0;
                icons::draw (g, playingCombo ? Icon::play : e.icon, content.removeFromLeft (16).toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                             starOn || playingCombo ? colour::signal : (chosen ? colour::text : colour::textDim));
                content.removeFromLeft (8);
            }

            if (e.count >= 0)
            {
                g.setColour (colour::textFaint);
                g.setFont (numberFont (12.0f));
                g.drawText (juce::String (e.count), content.removeFromRight (28), juce::Justification::centredRight, false);
            }

            g.setColour (chosen ? colour::text : (e.kind == Kind::add ? colour::textDim : colour::textDim.brighter (0.2f)));
            g.setFont (font (type::body, chosen ? Weight::semibold : Weight::medium));
            g.drawText (e.label, content, juce::Justification::centredLeft, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = entryAt (e.y);

        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override   { hover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = entryAt (e.y);

        if (i < 0 || view.lib == nullptr)
            return;

        const auto entry = entries[(size_t) i];

        if (entry.kind == Kind::add)
        {
            chooseFolder();
            return;
        }

        if (entry.kind == Kind::addCombo)
        {
            view.askNewCombo ({});
            return;
        }

        if (entry.kind != Kind::view)
            return;

        if (e.mods.isPopupMenu() && entry.filter.view == library::ProjectLibrary::Filter::View::combo)
        {
            juce::PopupMenu menu;
            juce::Component::SafePointer<Sidebar> safe (this);
            const auto name = entry.filter.combo;
            const bool playing = view.lib->playingCombo().equalsIgnoreCase (name);
            menu.addItem (playing ? "Parar o combo"_t : "Tocar em sequência"_t, [safe, name, playing]
            {
                if (safe == nullptr || safe->view.lib == nullptr)
                    return;

                if (playing)
                {
                    safe->view.lib->stopDeck();
                    return;
                }

                const auto r = safe->view.lib->playCombo (name);
                safe->view.status (r.wasOk() ? "Tocando o combo "_t + name + "." : r.getErrorMessage(), r.failed());
            });
            menu.addItem ("Renomear..."_t, [safe, name] { if (safe != nullptr) safe->view.askRenameCombo (name); });
            menu.addSeparator();
            menu.addItem ("Apagar o combo (vai para a lixeira)"_t, [safe, name]
            {
                if (safe != nullptr && safe->view.lib != nullptr)
                {
                    const auto r = safe->view.lib->deleteCombo (name);
                    safe->view.status (r.wasOk() ? "Combo na lixeira: "_t + name : r.getErrorMessage(), r.failed());
                }
            });
            menu.showMenuAsync (juce::PopupMenu::Options());
            return;
        }

        if (e.mods.isPopupMenu() && entry.removable)
        {
            juce::PopupMenu menu;
            juce::Component::SafePointer<Sidebar> safe (this);
            const auto folder = entry.filter.folder;
            menu.addItem ("Tirar a pasta da lista"_t, [safe, folder] { if (safe != nullptr && safe->view.lib != nullptr) safe->view.lib->removeFolder (folder); });
            menu.showMenuAsync (juce::PopupMenu::Options());
            return;
        }

        auto f = entry.filter;
        f.query = view.lib->getFilter().query;   // a busca continua valendo na vista nova
        view.lib->setFilter (f);
    }

    // soltar uma linha arrastada da lista num combo
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.toString().startsWith (dragPrefix);
    }

    void itemDragMove (const SourceDetails& d) override
    {
        const int i = entryAt (d.localPosition.y);
        const int target = i >= 0 && entries[(size_t) i].filter.view == library::ProjectLibrary::Filter::View::combo ? i : -1;

        if (target != dropTarget)
        {
            dropTarget = target;
            repaint();
        }
    }

    void itemDragExit (const SourceDetails&) override   { dropTarget = -1; repaint(); }

    void itemDropped (const SourceDetails& d) override
    {
        const int target = std::exchange (dropTarget, -1);
        repaint();

        if (target < 0 || view.lib == nullptr)
            return;

        const auto combo = entries[(size_t) target].filter.combo;
        const juce::File file (d.description.toString().fromFirstOccurrenceOf (dragPrefix, false, false));
        const auto r = view.lib->addToCombo (combo, file);
        view.status (r.wasOk() ? file.getFileNameWithoutExtension() + " no combo "_t + combo + "." : r.getErrorMessage(), r.failed());
    }

private:
    enum class Kind { view, caption, hint, add, addCombo };

    struct Entry
    {
        Kind kind;
        juce::String label;
        Icon icon;
        library::ProjectLibrary::Filter filter;
        int count = -1;
        bool removable = false;
    };

    bool isChosen (const library::ProjectLibrary::Filter& f) const
    {
        const auto& now = view.lib->getFilter();

        if (f.view != now.view)
            return false;

        using V = library::ProjectLibrary::Filter::View;
        return f.view == V::status ? f.status == now.status
             : f.view == V::tag    ? f.tag == now.tag
             : f.view == V::folder ? f.folder == now.folder
             : f.view == V::combo  ? f.combo.equalsIgnoreCase (now.combo)
             : true;
    }

    int entryAt (int y) const
    {
        int top = 0;

        for (size_t i = 0; i < entries.size(); ++i)
        {
            const int h = entries[i].kind == Kind::caption ? captionHeight : entryHeight;

            if (y >= top && y < top + h)
                return (int) i;

            top += h;
        }

        return -1;
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Pasta com projetos (.wnl)"_t, juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        juce::Component::SafePointer<Sidebar> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && safe->view.lib != nullptr && fc.getResult().isDirectory())
                                  {
                                      safe->view.lib->addFolder (fc.getResult());
                                      safe->view.status ("Pasta acrescentada: "_t + fc.getResult().getFullPathName());
                                  }
                              });
    }

    static constexpr int entryHeight = 28, captionHeight = 30;

    ProjectsView& view;
    std::vector<Entry> entries;
    int hover = -1, dropTarget = -1;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
ProjectsView::ProjectsView (library::ProjectLibrary* l) : lib (l)
{
    list.setModel (this);
    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);

    for (auto* b : { &refreshButton, &pendingButton, &renderButton, &openButton })
        addAndMakeVisible (b);

    refreshButton.onClick = [this] { if (lib != nullptr) lib->refresh(); };
    pendingButton.onClick = [this]
    {
        if (lib == nullptr)
            return;

        const int n = lib->renderPending();
        status (n == 0 ? "Todas as prévias estão em dia."_t
                       : juce::String (n) + (n == 1 ? " prévia na fila."_t : " prévias na fila."_t));
    };
    renderButton.onClick = [this] { renderSelected(); };
    openButton.onClick = [this] { openRow (list.getSelectedRow()); };

    deck = std::make_unique<DeckStrip> (*this);
    addAndMakeVisible (*deck);
    sidebar = std::make_unique<Sidebar> (*this);
    sidebarViewport.setViewedComponent (sidebar.get(), false);
    sidebarViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (sidebarViewport);

    // a busca: fundo de controle com a lupa (desenhados no paint), o TextEditor transparente por cima
    search.setTextToShowWhenEmpty ("Buscar: nome, bpm:140-150, tom:Am, tag:sombrio, status:mix, fav"_t, theme::colour::textFaint);
    search.setFont (theme::font (theme::type::body));
    search.setIndents (32, 8);
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.onTextChange = [this]
    {
        if (lib == nullptr)
            return;

        auto f = lib->getFilter();
        f.query = search.getText().trim();
        lib->setFilter (f);
    };
    search.onEscapeKey = [this] { search.clear(); search.onTextChange(); };
    addAndMakeVisible (search);

    if (lib != nullptr)
        lib->addChangeListener (this);

    sidebar->rebuild();
    updateButtons();
}

ProjectsView::~ProjectsView()
{
    stopTimer();

    if (lib != nullptr)
        lib->removeChangeListener (this);

    list.setModel (nullptr);
}

void ProjectsView::status (const juce::String& text, bool isError)
{
    if (onStatus)
        onStatus (text, isError);
}

const ProjectLibrary::Row* ProjectsView::rowAt (int row) const
{
    if (lib == nullptr || ! juce::isPositiveAndBelow (row, (int) lib->rows().size()))
        return nullptr;

    return &lib->rows()[(size_t) row];
}

const ProjectLibrary::Item* ProjectsView::itemAt (int row) const
{
    const auto* r = rowAt (row);

    if (r == nullptr || (r->extra < 0 && ! juce::isPositiveAndBelow (r->index, (int) lib->items().size())))
        return nullptr;

    return &lib->itemFor (*r);
}

juce::var ProjectsView::getDragSourceDescription (const juce::SparseSet<int>& selection)
{
    if (selection.isEmpty())
        return {};

    const auto* item = itemAt (selection[0]);
    return item != nullptr ? juce::var (juce::String (dragPrefix) + item->entry.file.getFullPathName()) : juce::var();
}

const ProjectLibrary::Item* ProjectsView::selectedItem() const
{
    return itemAt (list.getSelectedRow());
}

void ProjectsView::selectProject (const juce::File& f)
{
    if (lib == nullptr)
        return;

    for (int i = 0; i < getNumRows(); ++i)
        if (const auto* item = itemAt (i); item != nullptr && item->entry.file == f)
            list.selectRow (i);
}

void ProjectsView::updateButtons()
{
    const auto* item = selectedItem();
    renderButton.setEnabled (item != nullptr && item->entry.error.isEmpty());
    openButton.setEnabled (item != nullptr && item->entry.error.isEmpty());
    pendingButton.setEnabled (lib != nullptr && ! lib->items().empty());
    refreshButton.setEnabled (lib != nullptr);
}

//==============================================================================
ProjectsView::Columns ProjectsView::columnsFor (int width, int height)
{
    Columns c;
    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);
    c.play = r.removeFromLeft (32);
    c.star = r.removeFromLeft (24);
    c.state = r.removeFromRight (150);
    c.modified = r.removeFromRight (104);
    c.status = r.removeFromRight (100);

    if (r.getWidth() > 560)
        c.wave = r.removeFromRight (140).reduced (8, 0);

    if (r.getWidth() > 400)
        c.lufs = r.removeFromRight (64);

    c.length = r.removeFromRight (64);
    c.key = r.removeFromRight (48);
    c.bpm = r.removeFromRight (56);
    c.name = r.withTrimmedRight (8);
    return c;
}

int ProjectsView::getNumRows()
{
    return lib != nullptr ? (int) lib->rows().size() : 0;
}

void ProjectsView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    using namespace theme;
    const auto* item = itemAt (row);

    if (item == nullptr)
        return;

    const auto& e = item->entry;
    const bool inDeck = lib->deckProject() == e.file;
    const bool sounding = inDeck && lib->isDeckPlaying();
    const auto c = columnsFor (width, height);

    const auto* rowInfo = rowAt (row);
    const auto meta = lib->metaOf (e.file);

    if (selected)
    {
        g.setColour (colour::raisedHigh);
        g.fillRoundedRectangle (juce::Rectangle<int> (0, 0, width, height).reduced (4, 2).toFloat(), metric::radius);
    }

    // a cor do projeto: um fio na borda esquerda, como a pílula de cor das placas dos canais
    if (meta.colour.isNotEmpty())
    {
        g.setColour (theme::fromHex (meta.colour));
        g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, 10.0f, 3.0f, (float) height - 20.0f), 1.5f);
    }

    // estrela: cheia e laranja no favorito; o contorno só aparece na linha escolhida
    if (meta.favorite || selected)
        icons::draw (g, meta.favorite ? Icon::starFilled : Icon::star, c.star.toFloat().withSizeKeepingCentre (15.0f, 15.0f),
                     meta.favorite ? colour::signal : colour::textDim);

    const bool hasAudio = item->preview.valid || (item->reference && item->entry.error.isEmpty());
    const auto playIcon = sounding ? Icon::pause : Icon::play;
    icons::draw (g, playIcon, c.play.toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                 sounding ? colour::signal : (hasAudio ? colour::textDim : colour::textFaint.withAlpha (0.5f)));

    // a linha que está num deck mostra a letra dele no fim do nome (laranja no que soa)
    auto nameArea = c.name;

    for (int d = library::DeckPlayer::numDecks - 1; d >= 0; --d)
    {
        if (lib->deckInfo (d).project != e.file)
            continue;

        const bool deckSounding = d == lib->getActiveDeck() && lib->isDeckPlaying();
        auto tag = nameArea.removeFromRight (22).withSizeKeepingCentre (18, 18).toFloat();
        g.setColour (deckSounding ? colour::signalTint : colour::raised);
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (deckSounding ? colour::signal : colour::textDim);
        g.setFont (font (type::small, Weight::bold));
        g.drawText (d == 0 ? "A" : "B", tag, juce::Justification::centred, false);
        nameArea.removeFromRight (4);
    }

    // no combo, a posição antes do nome
    if (rowInfo != nullptr && rowInfo->comboIndex >= 0)
    {
        g.setColour (colour::textFaint);
        g.setFont (numberFont (12.0f));
        g.drawText (juce::String (rowInfo->comboIndex + 1) + ".", nameArea.removeFromLeft (24), juce::Justification::centredLeft, false);
    }

    // a pilha de versões: a seta abre e fecha; a versão mais velha fica recuada
    if (rowInfo != nullptr && rowInfo->child)
        nameArea.removeFromLeft (20);
    else if (rowInfo != nullptr && rowInfo->versions > 1)
        icons::draw (g, rowInfo->expanded ? Icon::chevronDown : Icon::chevronRight,
                     nameArea.removeFromLeft (18).toFloat().withSizeKeepingCentre (12.0f, 12.0f), colour::textDim);

    const auto nameFont = font (type::body, Weight::medium);
    g.setColour (e.error.isNotEmpty() ? colour::textFaint : (sounding ? colour::signal : (rowInfo != nullptr && rowInfo->child ? colour::textDim : colour::text)));
    g.setFont (nameFont);
    const int nameWidth = juce::jmin (nameArea.getWidth(), juce::GlyphArrangement::getStringWidthInt (nameFont, e.name) + 2);
    g.drawText (e.name, nameArea.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);

    juce::StringArray extras;
    const auto* combo = rowInfo != nullptr && rowInfo->comboIndex >= 0 ? lib->findCombo (lib->getFilter().combo) : nullptr;

    if (combo != nullptr)
    {
        const auto& ci = combo->items[(size_t) rowInfo->comboIndex];

        if (ci.latest)
            extras.add ("a mais nova"_t);

        if (ci.note.isNotEmpty())
            extras.add ("— "_t + ci.note);
    }

    if (rowInfo != nullptr && rowInfo->versions > 1)
        extras.add (juce::String (rowInfo->versions) + " "_t + "versões"_t);

    for (auto& tag : meta.tags)
        extras.add ("#" + tag);

    if (! extras.isEmpty() && nameArea.getWidth() > 24)
    {
        g.setColour (colour::textFaint);
        g.setFont (font (type::small));
        g.drawText (extras.joinIntoString ("  "), nameArea.withTrimmedLeft (10), juce::Justification::centredLeft, true);
    }

    if (item->reference)
    {
        g.setColour (colour::textFaint);
        g.setFont (font (type::small, Weight::medium));
        g.drawText ("Referência"_t, c.status.withTrimmedLeft (12), juce::Justification::centredLeft, true);
    }
    else if (meta.status != ProjectLibrary::Status::none)
    {
        g.setColour (colour::textDim);
        g.setFont (font (type::small, Weight::medium));
        g.drawText (ProjectLibrary::statusLabel (meta.status), c.status.withTrimmedLeft (12), juce::Justification::centredLeft, true);
    }

    const auto numbers = numberFont (13.0f);
    g.setColour (colour::textDim);

    if (e.error.isEmpty() && ! item->reference)
    {
        drawTabularText (g, bpmText (e.bpm), c.bpm.toFloat(), numbers, juce::Justification::centredRight);
        g.setFont (font (type::body, Weight::medium));
        g.drawText (e.keyName(), c.key.withTrimmedLeft (12), juce::Justification::centredLeft, false);
    }

    if (item->preview.valid)
    {
        drawTabularText (g, minutes (item->preview.seconds), c.length.toFloat(), numbers, juce::Justification::centredRight);

        if (! c.lufs.isEmpty())
            drawTabularText (g, decimal (item->preview.lufs, 1), c.lufs.toFloat(), numbers, juce::Justification::centredRight);

        if (! c.wave.isEmpty())
        {
            const float fraction = inDeck && lib->deckLength() > 0.0 ? (float) (lib->deckPosition() / lib->deckLength()) : -1.0f;
            drawWave (g, c.wave.toFloat().reduced (0.0f, 10.0f), item->preview.peaks,
                      item->state == PreviewState::stale ? colour::textFaint.withAlpha (0.6f) : colour::textFaint,
                      sounding ? fraction : -1.0f, colour::signal);
        }
    }

    g.setColour (colour::textFaint);
    g.setFont (font (type::small));
    g.drawText (whenText (e.modified), c.modified.withTrimmedLeft (12), juce::Justification::centredLeft, false);

    // estado da prévia: renderizando é uma ação em andamento (laranja); falhou é erro (vermelho funcional)
    auto st = c.state.withTrimmedLeft (12);
    juce::String label;
    auto ink = colour::textDim;
    auto icon = Icon::none;

    switch (item->state)
    {
        case PreviewState::ok:        label = "Pronta"; icon = Icon::check; break;
        case PreviewState::stale:     label = "Desatualizada"; icon = Icon::warning; break;
        case PreviewState::queued:    label = "Na fila"; ink = colour::textDim; break;
        case PreviewState::failed:    label = "Falhou"; ink = colour::meterHigh; icon = Icon::warning; break;
        case PreviewState::rendering: label = juce::String (juce::roundToInt (item->progress * 100.0f)) + "%"; ink = colour::signal; break;
        case PreviewState::none:      label = e.error.isNotEmpty() ? "Não abriu"_t : (item->reference ? "Áudio (toca direto)"_t : "Sem prévia"_t);
                                      ink = colour::textFaint; break;
    }

    if (item->state == PreviewState::rendering)
    {
        auto bar = st.withSizeKeepingCentre (st.getWidth(), 4).withTrimmedRight (44).toFloat();
        g.setColour (colour::raised);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (colour::signal);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (4.0f, bar.getWidth() * item->progress)), 2.0f);
        st = st.removeFromRight (40);
    }
    else if (icon != Icon::none)
    {
        icons::draw (g, icon, st.removeFromLeft (14).toFloat().withSizeKeepingCentre (13.0f, 13.0f), ink);
        st.removeFromLeft (6);
    }

    g.setColour (ink);
    g.setFont (font (type::small, Weight::medium));
    g.drawText (label, st, item->state == PreviewState::rendering ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
}

void ProjectsView::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    const auto c = columnsFor (list.getVisibleRowWidth(), rowHeight);
    const auto* item = itemAt (row);
    const auto* info = rowAt (row);

    if (item == nullptr || info == nullptr)
        return;

    if (e.mods.isPopupMenu())
    {
        list.selectRow (row);
        showRowMenu (row);
    }
    else if (e.x < c.play.getRight())
    {
        playRow (row, e.mods.isShiftDown());
    }
    else if (c.star.contains (e.x, c.star.getCentreY()))
    {
        lib->setFavorite (item->entry.file, ! lib->metaOf (item->entry.file).favorite);
    }
    else if (info->versions > 1 && e.x < c.name.getX() + 18)
    {
        lib->toggleStack (info->stack);
    }
}

void ProjectsView::showRowMenu (int row)
{
    const auto* item = itemAt (row);

    if (item == nullptr)
        return;

    using S = ProjectLibrary::Status;
    const auto file = item->entry.file;
    const auto meta = lib->metaOf (file);
    const bool hasAudio = item->preview.valid;
    juce::Component::SafePointer<ProjectsView> safe (this);

    juce::PopupMenu menu;
    menu.addItem ("Tocar a prévia"_t, hasAudio, false, [safe, row] { if (safe != nullptr) safe->playRow (row); });
    menu.addItem ("Tocar no outro deck (Shift+▶)"_t, hasAudio, false, [safe, row] { if (safe != nullptr) safe->playRow (row, true); });
    menu.addSeparator();
    menu.addItem (meta.favorite ? "Tirar a estrela"_t : "Marcar com estrela"_t, [safe, file, meta]
                  { if (safe != nullptr) safe->lib->setFavorite (file, ! meta.favorite); });

    juce::PopupMenu colours;
    colours.addItem ("Sem cor"_t, true, meta.colour.isEmpty(), [safe, file] { if (safe != nullptr) safe->lib->setColour (file, {}); });
    static const char* names[] = { "Laranja", "Branco", "Cinza", "Laranja claro", "Cinza claro", "Ferrugem", "Pêssego", "Grafite" };

    for (int i = 0; i < 8; ++i)
    {
        const auto hex = channelColourForIndex (i);
        colours.addColouredItem (100 + i, juce::String::fromUTF8 (names[i]), theme::fromHex (hex), true,
                                 meta.colour.equalsIgnoreCase (hex));
    }

    menu.addSubMenu ("Cor", colours);

    juce::PopupMenu statuses;

    for (auto s : { S::none, S::idea, S::production, S::mix, S::done })
        statuses.addItem (s == S::none ? "Sem status"_t : ProjectLibrary::statusLabel (s), true, meta.status == s,
                          [safe, file, s] { if (safe != nullptr) safe->lib->setStatus (file, s); });

    menu.addSubMenu ("Status", statuses);
    menu.addItem ("Tags..."_t, [safe, file] { if (safe != nullptr) safe->editTags (file); });

    juce::PopupMenu addTo;

    for (auto& c : lib->combos())
    {
        const auto name = c.name;
        addTo.addItem (name, [safe, name, file]
        {
            if (safe == nullptr)
                return;

            const auto r = safe->lib->addToCombo (name, file);
            safe->status (r.wasOk() ? file.getFileNameWithoutExtension() + " no combo "_t + name + "." : r.getErrorMessage(), r.failed());
        });
    }

    if (! lib->combos().empty())
        addTo.addSeparator();

    addTo.addItem ("Novo combo..."_t, [safe, file] { if (safe != nullptr) safe->askNewCombo (file); });
    menu.addSubMenu ("Adicionar ao combo"_t, addTo);

    // na vista de um combo: a ordem, a nota, a versão e tirar
    if (const auto* info = rowAt (row); info != nullptr && info->comboIndex >= 0)
    {
        const auto comboName = lib->getFilter().combo;
        const int index = info->comboIndex;
        const auto* combo = lib->findCombo (comboName);
        const int count = combo != nullptr ? (int) combo->items.size() : 0;
        const bool latest = combo != nullptr && combo->items[(size_t) index].latest;

        menu.addSeparator();
        menu.addItem ("Tocar o combo daqui"_t, [safe, comboName, index]
        {
            if (safe == nullptr)
                return;

            const auto r = safe->lib->playCombo (comboName, index);
            safe->status (r.wasOk() ? "Tocando o combo "_t + comboName + "." : r.getErrorMessage(), r.failed());
        });
        menu.addItem ("Nota..."_t, [safe, comboName, index] { if (safe != nullptr) safe->editComboNote (comboName, index); });
        menu.addItem ("Subir"_t, index > 0, false, [safe, comboName, index] { if (safe != nullptr) safe->lib->moveInCombo (comboName, index, index - 1); });
        menu.addItem ("Descer"_t, index < count - 1, false, [safe, comboName, index] { if (safe != nullptr) safe->lib->moveInCombo (comboName, index, index + 1); });
        menu.addItem ("Seguir a versão mais nova"_t, file.hasFileExtension ("wnl"), latest, [safe, comboName, index, latest]
                      { if (safe != nullptr) safe->lib->setComboLatest (comboName, index, ! latest); });
        menu.addItem ("Tirar do combo"_t, [safe, comboName, index] { if (safe != nullptr) safe->lib->removeFromCombo (comboName, index); });
    }

    menu.addSeparator();
    menu.addItem ("Renderizar a prévia"_t, item->entry.error.isEmpty(), false, [safe, file]
                  { if (safe != nullptr) safe->lib->renderPreview (file); });
    menu.addItem ("Abrir o projeto"_t, item->entry.error.isEmpty(), false, [safe, row] { if (safe != nullptr) safe->openRow (row); });
    menu.addItem ("Importar daqui..."_t, item->entry.error.isEmpty() && ! item->reference && onCommand != nullptr, false,
                  [safe, file] { if (safe != nullptr) safe->importFrom (file); });
    menu.addItem ("Mostrar na pasta"_t, [file] { file.revealToUser(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (220), [safe, file] (int result)
    {
        // as cores vêm do addColouredItem, que só devolve o número
        if (safe != nullptr && result >= 100 && result < 108)
            safe->lib->setColour (file, channelColourForIndex (result - 100));
    });
}

void ProjectsView::importFrom (const juce::File& file)
{
    library::ImportSource source;

    if (const auto r = library::readImportSource (file, source); r.failed())
    {
        status (r.getErrorMessage(), true);
        return;
    }

    if (source.patterns.empty())
    {
        status (file.getFileNameWithoutExtension() + ": nenhum padrão com notas ou passos para trazer."_t, true);
        return;
    }

    juce::Component::SafePointer<ProjectsView> safe (this);

    ImportDialog::show (this, std::move (source), [safe, file] (const library::ImportOptions& options) -> juce::String
    {
        if (safe == nullptr || safe->onCommand == nullptr)
            return "Importar não está disponível aqui."_t;

        auto o = new juce::DynamicObject();
        o->setProperty ("path", file.getFullPathName());
        o->setProperty ("patterns", options.patterns.joinIntoString (","));
        o->setProperty ("mixer", options.mixer);
        o->setProperty ("transpose", options.transpose);
        const auto r = safe->onCommand ("project.importFrom", juce::var (o));

        if (! r.ok)
            return r.error;

        const int patterns = r.value["patterns"].size(), channels = r.value["channels"].size();
        const int semitones = (int) r.value["semitones"];
        auto text = "Importado de "_t + file.getFileNameWithoutExtension() + ": " + juce::String (patterns)
                  + (patterns == 1 ? " padrão e "_t : " padrões e "_t) + juce::String (channels) + (channels == 1 ? " canal" : " canais");

        if (semitones != 0)
            text << " (notas " << (semitones > 0 ? "+" : "") << semitones << " semitons)";

        safe->status (text + ". Ctrl+Z desfaz."_t);
        return {};
    });
}

void ProjectsView::askNewCombo (const juce::File& addAfterCreating)
{
    auto* w = new juce::AlertWindow ("Novo combo", "Uma lista com ordem: um show, um álbum, as faixas para um cliente."_t,
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", {});
    w->addButton ("Criar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ProjectsView> safe (this);

    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, addAfterCreating] (int choice)
    {
        if (safe == nullptr || choice != 1)
            return;

        const auto name = w->getTextEditorContents ("name").trim();
        auto r = safe->lib->createCombo (name);

        if (r.wasOk() && addAfterCreating != juce::File())
            r = safe->lib->addToCombo (name, addAfterCreating);

        if (r.wasOk())
        {
            ProjectLibrary::Filter f;
            f.view = ProjectLibrary::Filter::View::combo;
            f.combo = name;
            safe->lib->setFilter (f);
        }

        safe->status (r.wasOk() ? "Combo criado: "_t + name + ". Arraste projetos da lista para ele na barra lateral." : r.getErrorMessage(), r.failed());
    }), true);
}

void ProjectsView::askRenameCombo (const juce::String& combo)
{
    auto* w = new juce::AlertWindow ("Renomear combo", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", combo);
    w->addButton ("Renomear", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ProjectsView> safe (this);

    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, combo] (int choice)
    {
        if (safe != nullptr && choice == 1)
            if (const auto r = safe->lib->renameCombo (combo, w->getTextEditorContents ("name")); r.failed())
                safe->status (r.getErrorMessage(), true);
    }), true);
}

void ProjectsView::editComboNote (const juce::String& combo, int index)
{
    const auto* c = lib->findCombo (combo);

    if (c == nullptr || ! juce::isPositiveAndBelow (index, (int) c->items.size()))
        return;

    auto* w = new juce::AlertWindow ("Nota", "Uma linha sobre este item no combo (ex.: \"abre o show\", \"versão com o vocal novo\")."_t,
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("note", c->items[(size_t) index].note);
    w->addButton ("Salvar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ProjectsView> safe (this);

    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, combo, index] (int choice)
    {
        if (safe != nullptr && choice == 1)
            safe->lib->setComboNote (combo, index, w->getTextEditorContents ("note"));
    }), true);
}

void ProjectsView::editTags (const juce::File& file)
{
    auto* w = new juce::AlertWindow ("Tags de "_t + file.getFileNameWithoutExtension(),
                                     "Separe por vírgula. Exemplo: sombrio, 808, pra Fulano"_t, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("tags", lib->metaOf (file).tags.joinIntoString (", "));
    w->addButton ("Salvar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ProjectsView> safe (this);

    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, file] (int choice)
    {
        if (safe != nullptr && choice == 1)
            safe->lib->setTags (file, juce::StringArray::fromTokens (w->getTextEditorContents ("tags"), ",", "\""));
    }), true);
}

void ProjectsView::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (e.x >= columnsFor (list.getVisibleRowWidth(), rowHeight).name.getX())
        openRow (row);
}

void ProjectsView::returnKeyPressed (int row)
{
    openRow (row);
}

void ProjectsView::selectedRowsChanged (int)
{
    updateButtons();
}

void ProjectsView::playRow (int row, bool otherDeck)
{
    const auto* item = itemAt (row);

    if (item == nullptr)
        return;

    const auto file = item->entry.file;
    const int active = lib->getActiveDeck();

    const bool playable = item->preview.valid || (item->reference && item->entry.error.isEmpty());

    if (! playable)
    {
        if (item->entry.error.isEmpty())
        {
            lib->renderPreview (file);
            status ("Renderizando a prévia de "_t + item->entry.name + ". Ela toca quando ficar pronta no ▶."_t);
        }
    }
    else if (otherDeck)
    {
        // Shift+▶: no deck que não soa, alinhado com o que toca; Tab troca
        const int other = 1 - active;
        const auto r = lib->loadDeck (other, file);
        status (r.wasOk() ? juce::String ("Deck ") + (other == 0 ? "A" : "B") + ": " + item->entry.name + ". Tab troca entre A e B."
                          : r.getErrorMessage(), r.failed());
    }
    else if (lib->deckInfo (active).project == file || (item->reference && lib->deckInfo (active).audio == file))
    {
        lib->toggleDeck();
    }
    else
    {
        const auto r = lib->playPreview (file);

        if (r.failed())
            status (r.getErrorMessage(), true);
        else if (item->state == PreviewState::stale)
            status ("Tocando uma prévia antiga: o projeto mudou depois dela. \"Renderizar\" faz a nova."_t);
    }

    deck->refresh();
    list.repaint();
    startTimerHz (30);
}

void ProjectsView::switchDeck()
{
    if (lib == nullptr)
        return;

    lib->toggleActiveDeck();
    deck->refresh();
    list.repaint();
}

void ProjectsView::openRow (int row)
{
    const auto* item = itemAt (row);

    if (item != nullptr && item->entry.error.isEmpty() && onOpenProject)
        onOpenProject (item->entry.file);
}

void ProjectsView::renderSelected()
{
    if (const auto* item = selectedItem())
    {
        lib->renderPreview (item->entry.file);
        status ("Renderizando a prévia de "_t + item->entry.name + "...");
    }
}

void ProjectsView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    sidebar->rebuild();
    layoutSidebar();
    list.updateContent();
    list.repaint();
    deck->refresh();
    updateButtons();
    repaint();

    if (lib != nullptr && lib->isDeckPlaying())
        startTimerHz (30);
}

void ProjectsView::timerCallback()
{
    // a agulha anda enquanto o deck toca; parado, o timer descansa
    deck->repaint();

    if (lib != nullptr)
        for (int i = 0; i < getNumRows(); ++i)
            if (const auto* item = itemAt (i); item != nullptr
                && (item->entry.file == lib->deckInfo (0).project || item->entry.file == lib->deckInfo (1).project))
                list.repaintRow (i);

    if (lib == nullptr || ! lib->isDeckPlaying())
    {
        deck->refresh();
        stopTimer();
    }
}

void ProjectsView::visibilityChanged()
{
    if (isVisible() && lib != nullptr)
        lib->refresh();   // a pasta pode ter mudado com a tela fechada
}

//==============================================================================
void ProjectsView::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    auto r = getLocalBounds();
    auto top = r.removeFromTop (metric::toolbar).reduced (metric::pad, 0);
    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    const juce::String title ("Projetos");
    g.drawText (title, top, juce::Justification::centredLeft);

    if (lib != nullptr)
    {
        const int n = (int) lib->items().size();
        g.setColour (colour::textFaint);
        g.setFont (font (type::small));
        const int titleWidth = juce::GlyphArrangement::getStringWidthInt (font (type::title, Weight::semibold), title);
        g.drawText (lib->isScanning() && n == 0 ? "procurando..."_t : juce::String (n) + (n == 1 ? " projeto" : " projetos"),
                    top.withTrimmedLeft (titleWidth + 12), juce::Justification::centredLeft);
    }

    // a busca: fundo de controle e a lupa (o TextEditor fica transparente por cima)
    const auto box = searchArea().toFloat();
    g.setColour (colour::raised);
    g.fillRoundedRectangle (box, metric::radius);
    icons::draw (g, Icon::search, box.withWidth (32.0f).withSizeKeepingCentre (16.0f, 16.0f), colour::textDim);

    // fio entre a barra lateral e a lista
    g.setColour (colour::line);
    g.fillRect (juce::Rectangle<int> (sidebarWidth, metric::toolbar + 8, 1, getHeight() - metric::toolbar - deckHeight - footerRow - 16));

    // legendas das colunas, alinhadas com as linhas da lista
    const auto header = r.removeFromTop (headerRow);
    const auto c = columnsFor (list.getVisibleRowWidth(), headerRow);
    const int dx = list.getX();
    g.setColour (colour::textFaint);
    g.setFont (captionFont());

    auto caption = [&] (juce::Rectangle<int> col, const juce::String& text, juce::Justification j)
    {
        if (! col.isEmpty())
            g.drawText (text, col.translated (dx, header.getY()), j, false);
    };

    caption (c.name, "NOME", juce::Justification::centredLeft);
    caption (c.bpm, "BPM", juce::Justification::centredRight);
    caption (c.key.withTrimmedLeft (12), "TOM", juce::Justification::centredLeft);
    caption (c.length, "DURAÇÃO"_t, juce::Justification::centredRight);
    caption (c.lufs, "LUFS", juce::Justification::centredRight);
    caption (c.wave, "ONDA", juce::Justification::centredLeft);
    caption (c.status.withTrimmedLeft (12), "STATUS", juce::Justification::centredLeft);
    caption (c.modified.withTrimmedLeft (12), "SALVO", juce::Justification::centredLeft);
    caption (c.state.withTrimmedLeft (12), "PRÉVIA"_t, juce::Justification::centredLeft);

    // rodapé da lista: o que a fila de prévias faz agora, ou como usar
    auto footer = getLocalBounds().withTrimmedBottom (deckHeight).removeFromBottom (footerRow).reduced (metric::pad, 0);
    footer.setLeft (sidebarWidth + metric::pad);
    auto renderText = lib != nullptr ? lib->renderStatusText() : juce::String();

    if (lib != nullptr && lib->playingCombo().isNotEmpty())
    {
        const auto* combo = lib->findCombo (lib->playingCombo());
        const auto comboText = "Combo "_t + lib->playingCombo() + ": " + juce::String (lib->playingComboIndex() + 1) + " de "
                             + juce::String (combo != nullptr ? (int) combo->items.size() : 0);
        renderText = renderText.isEmpty() ? comboText : comboText + ". " + renderText;
    }
    g.setColour (renderText.isNotEmpty() ? colour::textDim : colour::textFaint);
    g.setFont (font (type::small, renderText.isNotEmpty() ? Weight::medium : Weight::regular));
    g.drawText (renderText.isNotEmpty() ? renderText
                                        : "O ▶ ouve a prévia sem abrir o projeto; Shift+▶ põe no outro deck e Tab troca A e B. Duplo clique ou Enter abre."_t,
                footer, juce::Justification::centredLeft, true);

    if (lib != nullptr && ! lib->items().empty() && lib->rows().empty())
    {
        auto empty = list.getBounds().withSizeKeepingCentre (juce::jmin (520, list.getWidth()), 64);
        g.setColour (colour::textDim);
        g.setFont (font (type::body, Weight::medium));
        g.drawText ("Nenhum projeto com este filtro."_t, empty.removeFromTop (22), juce::Justification::centred);
        g.setColour (colour::textFaint);
        g.setFont (font (type::small));
        g.drawFittedText ("Limpe a busca (Esc) ou escolha Todos na barra lateral."_t, empty, juce::Justification::centredTop, 2, 0.9f);
    }

    if (lib == nullptr || (lib->items().empty() && ! lib->isScanning()))
    {
        auto empty = list.getBounds().withSizeKeepingCentre (juce::jmin (520, list.getWidth()), 64);
        g.setColour (colour::textDim);
        g.setFont (font (type::body, Weight::medium));
        g.drawText (lib == nullptr ? "Projetos indisponíveis aqui."_t : "Nenhum projeto ainda."_t,
                    empty.removeFromTop (22), juce::Justification::centred);
        g.setColour (colour::textFaint);
        g.setFont (font (type::small));
        g.drawFittedText ("Salve um projeto (Ctrl+S) na pasta Música\\WHYNOT LAB: ele aparece aqui, com uma prévia para ouvir sem abrir."_t,
                          empty, juce::Justification::centredTop, 2, 0.9f);
    }
}

void ProjectsView::resized()
{
    using namespace theme;
    auto r = getLocalBounds();
    auto top = r.removeFromTop (metric::toolbar).reduced (metric::pad - 4, (metric::toolbar - metric::control) / 2);

    for (auto* b : { &openButton, &renderButton, &pendingButton, &refreshButton })
    {
        b->setBounds (top.removeFromRight (b->getIdealWidth (metric::control)));
        top.removeFromRight (2);
    }

    search.setBounds (searchArea());
    deck->setBounds (r.removeFromBottom (deckHeight));
    r.removeFromBottom (footerRow);
    sidebarViewport.setBounds (r.removeFromLeft (sidebarWidth).withTrimmedTop (4));
    layoutSidebar();
    r.removeFromTop (headerRow);
    list.setBounds (r.reduced (4, 0));
}

void ProjectsView::layoutSidebar()
{
    const int ideal = sidebar->getIdealHeight();
    const bool scrolls = ideal > sidebarViewport.getHeight();
    sidebar->setSize (sidebarViewport.getWidth() - (scrolls ? sidebarViewport.getScrollBarThickness() : 0),
                      juce::jmax (ideal, sidebarViewport.getHeight()));
}

juce::Rectangle<int> ProjectsView::searchArea() const
{
    // começa onde a lista começa (o título e a contagem cabem acima da barra lateral) e para antes dos botões
    using namespace theme;
    int buttons = 0;

    for (auto* b : { &openButton, &renderButton, &pendingButton, &refreshButton })
        buttons += b->getIdealWidth (metric::control) + 2;

    const int x = sidebarWidth + 8;
    const int width = juce::jlimit (120, 420, getWidth() - x - buttons - metric::pad - metric::gap);
    return { x, (metric::toolbar - metric::control) / 2, width, metric::control };
}

} // namespace whynot
