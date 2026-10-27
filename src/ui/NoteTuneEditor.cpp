#include "NoteTuneEditor.h"
#include "NoteNames.h"
#include "WindowOwner.h"

namespace whynot
{

namespace
{
    constexpr int keyboardWidth = 64;
    constexpr int waveHeight = 64;
    constexpr int controlsHeight = 56;
    constexpr int peakBuckets = 4000;

    juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    juce::var notesVar (const std::vector<TunedNote>& notes)
    {
        juce::Array<juce::var> list;

        for (auto& n : notes)
            list.add (args ({ { "start", n.start }, { "end", n.end }, { "sourceMidi", n.sourceMidi }, { "shift", n.shift },
                              { "center", n.center }, { "flatten", n.flatten }, { "formant", n.formant }, { "transitionMs", n.transitionMs } }));

        return list;
    }

    bool isBlack (int key)
    {
        const int k = ((key % 12) + 12) % 12;
        return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
    }

    juce::String centsText (float cents)
    {
        const int c = juce::roundToInt (cents);
        return (c > 0 ? "+" : "") + juce::String (c) + "c";
    }
}

//==============================================================================
NoteTuneSlider::NoteTuneSlider (const juce::String& l, double lo, double hi, double d, const juce::String& u, int places)
    : label (l), unit (u), min (lo), max (hi), defaultValue (d), value (d), decimals (places)
{
}

void NoteTuneSlider::setValue (double v)
{
    value = juce::jlimit (min, max, v);
    repaint();
}

juce::Rectangle<float> NoteTuneSlider::track() const
{
    // nome e valor numa linha de 14 px; o trilho embaixo, com 6 px de folga para a bolinha nas pontas
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (16.0f);
    return r.withSizeKeepingCentre (r.getWidth() - 12.0f, 4.0f);
}

void NoteTuneSlider::paint (juce::Graphics& g)
{
    using namespace theme;
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    auto top = getLocalBounds().toFloat().removeFromTop (14.0f);
    g.setColour (colour::textDim.withMultipliedAlpha (alpha));
    g.setFont (font (type::small));
    g.drawText (label, top, juce::Justification::centredLeft, false);

    const auto text = mixed ? juce::String ("—") : juce::String (value, decimals).replaceCharacter ('.', ',') + " " + unit;
    g.setColour (colour::text.withMultipliedAlpha (alpha));
    drawTabularText (g, text, top.removeFromRight (60.0f), numberFont (type::small), juce::Justification::centredRight);

    const auto t = track();
    const float pos = (float) ((value - min) / (max - min));
    const float zero = (float) juce::jlimit (0.0, 1.0, (0.0 - min) / (max - min));   // o trecho aceso sai do zero
    g.setColour (colour::raised.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (t, 2.0f);
    g.setColour (colour::textDim.withMultipliedAlpha (alpha));
    const float a = t.getX() + t.getWidth() * juce::jmin (zero, pos), b = t.getX() + t.getWidth() * juce::jmax (zero, pos);
    g.fillRoundedRectangle (juce::Rectangle<float> (a, t.getY(), juce::jmax (0.0f, b - a), t.getHeight()), 2.0f);

    if (! mixed)
    {
        g.setColour (colour::text.withMultipliedAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ t.getX() + t.getWidth() * pos, t.getCentreY() }));
    }
}

void NoteTuneSlider::setFromX (float x)
{
    const auto t = track();
    const double frac = juce::jlimit (0.0, 1.0, (double) ((x - t.getX()) / juce::jmax (1.0f, t.getWidth())));
    const double step = std::pow (10.0, -decimals);
    setValue (std::round ((min + frac * (max - min)) / step) * step);
    mixed = false;

    if (onChange)
        onChange (value);
}

void NoteTuneSlider::mouseDown (const juce::MouseEvent& e)   { setFromX (e.position.x); }
void NoteTuneSlider::mouseDrag (const juce::MouseEvent& e)   { setFromX (e.position.x); }

void NoteTuneSlider::mouseUp (const juce::MouseEvent&)
{
    if (onCommit)
        onCommit (value);
}

void NoteTuneSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    setValue (defaultValue);
    mixed = false;

    if (onChange) onChange (value);
    if (onCommit) onCommit (value);
}

//==============================================================================
NoteTuneEditor::NoteTuneEditor (AppController& a, const juce::String& item) : app (a), itemId (item)
{
    using namespace theme;
    setWantsKeyboardFocus (true);
    setOpaque (true);

    title.setFont (font (type::title, Weight::semibold));
    title.setColour (juce::Label::textColourId, colour::text);
    title.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title);

    tool.setItems ({ { "Mover"_t, Icon::select, "Arrastar a nota para cima ou para baixo (Alt: fino, em cents)"_t },
                     { "Cortar"_t, Icon::scissors, "Clique numa nota para cortar ali (ou duplo clique com o Mover)"_t } });
    addAndMakeVisible (tool);

    playButton.setTooltip ("Tocar o bloco do começo, com a afinação (Espaço)"_t);
    playButton.onClick = [this] { togglePlay(); };
    scaleButton.setTooltip ("Cada nota para a nota da escala do projeto mais perto, no centro do semitom"_t);
    scaleButton.onClick = [this] { tuneToScale(); };
    mergeButton.setTooltip ("Juntar as notas escolhidas que estão encostadas (J)"_t);
    mergeButton.onClick = [this] { mergeSelected(); };
    resetButton.setTooltip ("Tirar todas as edições deste áudio (o arquivo original nunca muda)"_t);
    resetButton.onClick = [this]
    {
        const auto r = app.run ("audio.clearNoteTune", args ({ { "item", itemId } }));
        status = r.ok ? "A voz voltou ao original (Ctrl+Z desfaz)."_t : r.error;
        readModel();
        repaint();
    };

    for (auto* b : { &playButton, &scaleButton, &mergeButton, &resetButton })
        addAndMakeVisible (b);

    followButton.setTooltip ("Ligado: o timbre sobe e desce junto com a nota, como fita. Desligado: o timbre fica, só a nota muda"_t);
    followButton.setColour (juce::ToggleButton::textColourId, colour::textDim);
    followButton.onClick = [this] { setFormantFollows (followButton.getToggleState()); };
    addAndMakeVisible (followButton);

    center.setTooltip ("Puxa a nota para o centro do semitom e tira o desvio lento (a nota torta). Duplo clique: 0"_t);
    flatten.setTooltip ("Achata o vibrato (o desvio rápido) sem mexer na nota. Duplo clique: 0"_t);
    transition.setTooltip ("Quanto tempo a correção leva para entrar no começo da nota: 0 = salto, mais = desliza. Duplo clique: 40 ms"_t);
    formant.setTooltip ("O tamanho da voz, à parte da nota: negativo = mais grossa; positivo = mais fina. Duplo clique: 0"_t);

    auto wire = [this] (NoteTuneSlider& s, std::function<void (TunedNote&, double)> set, const juce::String& what)
    {
        s.onChange = [this, set] (double v) { applyToSelected ([&] (TunedNote& n) { set (n, v); }, false, {}); };
        s.onCommit = [this, set, what] (double v) { applyToSelected ([&] (TunedNote& n) { set (n, v); }, true, what); };
        addAndMakeVisible (s);
    };
    wire (center, [] (TunedNote& n, double v) { n.center = (float) (v / 100.0); }, "Centralizar"_t);
    wire (flatten, [] (TunedNote& n, double v) { n.flatten = (float) (v / 100.0); }, "Achatar o vibrato"_t);
    wire (transition, [] (TunedNote& n, double v) { n.transitionMs = (float) v; }, "Transição"_t);
    wire (formant, [] (TunedNote& n, double v) { n.formant = (float) v; }, "Formante"_t);

    app.getModel().addListener (this);
    readModel();
    startAnalysis();
    refreshSliders();
    startTimerHz (30);
    setSize (1040, 620);
}

NoteTuneEditor::~NoteTuneEditor()
{
    app.getModel().removeListener (this);
    cancelled = true;   // a análise confere a cada 256 quadros e para
    worker.removeAllJobs (true, 10000);
}

//==============================================================================
bool NoteTuneEditor::readModel()
{
    const auto& p = app.getModel().get();
    const auto* item = p.findItem (itemId);
    const auto* channel = item != nullptr ? p.findChannel (item->ref) : nullptr;

    if (item == nullptr || item->kind != "audio" || channel == nullptr)
        return false;

    channelId = channel->id;
    file = app.getModel().resolveSample (channel->samplePath);
    itemStart = item->start;
    itemStretch = item->stretch;
    clipFrom = item->offsetSeconds;
    clipTo = clipFrom + item->fileSeconds (ticksToSeconds (item->length, p.bpm));
    title.setText ("Afinar nota a nota: "_t + channel->name, juce::dontSendNotification);
    follows = channel->tuneFormantFollows;
    followButton.setToggleState (follows, juce::dontSendNotification);

    if (drag == Drag::none)
    {
        notes = channel->tunedNotes;

        if (notes.empty() && analysis != nullptr)
        {
            notes = dsp::notesFromAnalysis (*analysis);
            notetune::normalise (notes);
        }

        std::erase_if (selected, [this] (int i) { return i >= (int) notes.size(); });
    }

    refreshCurve();
    refreshSliders();
    return true;
}

void NoteTuneEditor::projectChanged (const ProjectModel&)
{
    if (! readModel())
    {
        if (onItemGone)
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<NoteTuneEditor> (this)]
                                             { if (safe != nullptr && safe->onItemGone) safe->onItemGone(); });
        return;
    }

    repaint();
}

void NoteTuneEditor::startAnalysis()
{
    if (file == juce::File())
        return;

    // tudo na thread do editor (a análise sai do cache em ms; ler o áudio para a forma de onda, não)
    analysing = true;
    juce::Component::SafePointer<NoteTuneEditor> safe (this);
    const auto f = file;
    const auto* stop = &cancelled;

    worker.addJob ([safe, f, stop]
    {
        juce::String error;
        auto result = notetune::analyse (f, error, stop);
        std::vector<float> wave;

        if (const auto audio = result != nullptr ? notetune::loadAudio (f, error) : nullptr)
        {
            const auto& x = audio->channels[0];
            wave.assign ((size_t) peakBuckets, 0.0f);

            for (size_t i = 0; i < x.size(); ++i)
            {
                auto& v = wave[i * (size_t) peakBuckets / x.size()];
                v = juce::jmax (v, std::abs (x[i]));
            }
        }

        if (stop->load())
            return;

        juce::MessageManager::callAsync ([safe, result, wave = std::move (wave), error]() mutable
        {
            if (safe != nullptr)
                safe->analysisReady (result, std::move (wave), error);
        });
    });
}

void NoteTuneEditor::analysisReady (std::shared_ptr<const dsp::NoteAnalysis> result, std::vector<float> wave, juce::String error)
{
    analysing = false;
    analysis = std::move (result);
    peaks = std::move (wave);
    analysisError = error;

    if (analysis == nullptr)
        status = error;
    else if (analysis->notes.empty())
        status = "Não achei notas neste áudio (sem voz cantada, ou baixo demais)."_t;
    else
        status = "Arraste as notas para cima e para baixo. Clique para escolher; Shift soma; duplo clique corta."_t;

    readModel();
    fitView();
    repaint();
}

bool NoteTuneEditor::waitForAnalysis (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (analysing && juce::Time::getMillisecondCounter() < until)
    {
        // a resposta chega pela fila de mensagens
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    return analysis != nullptr;
}

void NoteTuneEditor::fitView()
{
    viewFrom = clipFrom;
    viewTo = juce::jmax (clipFrom + 0.1, clipTo);
    float lo = 1000.0f, hi = -1000.0f;

    for (auto& n : notes)
        if (n.end > clipFrom && n.start < clipTo)
        {
            lo = juce::jmin (lo, juce::jmin (n.sourceMidi, n.targetMidi()));
            hi = juce::jmax (hi, juce::jmax (n.sourceMidi, n.targetMidi()));
        }

    if (lo > hi)
    {
        lo = 55.0f;
        hi = 72.0f;
    }

    // pelo menos uma oitava e meia, com folga de 3 semitons
    const float mid = 0.5f * (lo + hi);
    const float half = juce::jmax (9.0f, 0.5f * (hi - lo) + 3.0f);
    midiLow = std::floor (mid - half);
    midiHigh = std::ceil (mid + half);
}

void NoteTuneEditor::refreshCurve()
{
    if (analysis == nullptr)
    {
        corrected.clear();
        return;
    }

    corrected = dsp::correctedCurve (analysis->curve, dsp::correctionFor (analysis->curve, notes, follows));
}

void NoteTuneEditor::refreshSliders()
{
    const bool any = ! selected.empty();

    for (auto* s : { &center, &flatten, &transition, &formant })
        s->setEnabled (any);

    mergeButton.setEnabled (selected.size() > 1);

    if (! any)
        return;

    auto pick = [this] (NoteTuneSlider& s, std::function<double (const TunedNote&)> get)
    {
        const double first = get (notes[(size_t) *selected.begin()]);
        bool same = true;

        for (int i : selected)
            same = same && std::abs (get (notes[(size_t) i]) - first) < 1.0e-6;

        s.setValue (first);
        s.setMixed (! same);
    };

    pick (center, [] (const TunedNote& n) { return n.center * 100.0; });
    pick (flatten, [] (const TunedNote& n) { return n.flatten * 100.0; });
    pick (transition, [] (const TunedNote& n) { return (double) n.transitionMs; });
    pick (formant, [] (const TunedNote& n) { return (double) n.formant; });
}

//==============================================================================
bool NoteTuneEditor::commit (std::vector<TunedNote> next, bool nextFollows, const juce::String& done)
{
    const auto r = app.run ("audio.setNoteTune", args ({ { "item", itemId }, { "notes", notesVar (next) }, { "formantFollows", nextFollows } }));
    status = r.ok ? done + " (Ctrl+Z desfaz)."_t : r.error;
    readModel();
    repaint();
    return r.ok;
}

void NoteTuneEditor::applyToSelected (const std::function<void (TunedNote&)>& change, bool commitNow, const juce::String& done)
{
    if (selected.empty())
        return;

    auto next = notes;

    for (int i : selected)
        change (next[(size_t) i]);

    if (commitNow)
    {
        commit (std::move (next), follows, done);
        return;
    }

    notes = std::move (next);   // a prévia enquanto arrasta (o comando vai ao soltar)
    refreshCurve();
    repaint();
}

bool NoteTuneEditor::moveSelected (float semitones, bool fine)
{
    if (selected.empty() || semitones == 0.0f)
        return false;

    auto next = notes;

    for (int i : selected)
    {
        auto& n = next[(size_t) i];
        const float target = n.sourceMidi + n.shift;
        notetune::moveTo (n, fine ? target + semitones : std::round (target + semitones));
    }

    const auto& first = next[(size_t) *selected.begin()];
    return commit (std::move (next), follows, "Nota em "_t + noteNamePt ((int) std::lround (first.targetMidi())));
}

bool NoteTuneEditor::splitAt (int note, double seconds)
{
    auto next = notes;

    if (! notetune::split (next, note, seconds))
    {
        status = "Corte perto demais da ponta da nota (cada pedaço precisa de 30 ms)."_t;
        repaint();
        return false;
    }

    selected = { note + 1 };
    return commit (std::move (next), follows, "Nota cortada em duas"_t);
}

bool NoteTuneEditor::mergeSelected()
{
    if (selected.size() < 2)
        return false;

    auto next = notes;
    int merged = 0;
    const std::vector<int> order (selected.begin(), selected.end());

    // de trás para frente: os índices de antes continuam valendo; só junta as encostadas (até 30 ms de folga)
    for (size_t k = order.size() - 1; k > 0; --k)
    {
        const int i = order[k - 1];

        if (order[k] == i + 1 && next[(size_t) i + 1].start - next[(size_t) i].end < 0.03 && notetune::merge (next, i))
            ++merged;
    }

    if (merged == 0)
    {
        status = "Escolha notas vizinhas e encostadas para juntar."_t;
        repaint();
        return false;
    }

    selected = { order.front() };
    return commit (std::move (next), follows, merged == 1 ? "Duas notas viraram uma"_t : juce::String (merged + 1) + " notas viraram uma"_t);
}

bool NoteTuneEditor::tuneToScale()
{
    const auto r = app.run ("audio.tuneToScale", args ({ { "item", itemId } }));
    status = r.ok ? "Todas as notas na escala do projeto ("_t + r.value["scale"].toString() + "). Ctrl+Z desfaz." : r.error;
    readModel();
    repaint();
    return r.ok;
}

bool NoteTuneEditor::resetSelected()
{
    if (selected.empty())
        return false;

    auto next = notes;

    for (int i : selected)
    {
        auto& n = next[(size_t) i];
        n.shift = n.center = n.flatten = n.formant = 0.0f;
    }

    return commit (std::move (next), follows, "Notas de volta ao original"_t);
}

void NoteTuneEditor::selectNotes (std::set<int> which)
{
    std::erase_if (which, [this] (int i) { return i < 0 || i >= (int) notes.size(); });
    selected = std::move (which);
    refreshSliders();
    repaint();
}

void NoteTuneEditor::setFormantFollows (bool on)
{
    followButton.setToggleState (on, juce::dontSendNotification);

    if (on != follows)
        commit (notes, on, on ? "O timbre anda junto com o tom"_t : "O timbre fica; só a nota muda"_t);
}

void NoteTuneEditor::togglePlay()
{
    if (app.isPlaying())
    {
        app.run ("transport.stop");
        return;
    }

    if (! app.getModel().get().songMode)
        app.run ("transport.setSongMode", args ({ { "on", true } }));

    // do começo do trecho que está na tela, para ouvir a edição de agora
    const double fromFile = juce::jlimit (clipFrom, clipTo, viewFrom);
    const int at = itemStart + (int) std::lround (secondsToTicks ((fromFile - clipFrom) * itemStretch, app.getModel().get().bpm));
    app.run ("transport.playFrom", args ({ { "at", at } }));
}

void NoteTuneEditor::timerCallback()
{
    double now = -1.0;

    if (app.isPlaying() && app.getModel().get().songMode)
    {
        const double ticks = app.positionInSteps() * ticksPerStep;
        const double seconds = ticksToSeconds (ticks - itemStart, app.getModel().get().bpm) / juce::jmax (1.0e-6, itemStretch);
        now = clipFrom + seconds;
    }

    playButton.setIcon (app.isPlaying() ? Icon::stop : Icon::play);
    playButton.setLabel (app.isPlaying() ? "Parar"_t : "Tocar"_t);

    if (std::abs (now - playheadSeconds) > 1.0e-4)
    {
        playheadSeconds = now;
        repaint (gridArea().getUnion (waveArea()));
    }
}

//==============================================================================
juce::Rectangle<int> NoteTuneEditor::toolbarArea() const   { return getLocalBounds().removeFromTop (theme::metric::toolbar); }
juce::Rectangle<int> NoteTuneEditor::controlsArea() const  { return getLocalBounds().removeFromBottom (controlsHeight); }

juce::Rectangle<int> NoteTuneEditor::keyboardArea() const
{
    auto r = getLocalBounds();
    r.removeFromTop (theme::metric::toolbar);
    r.removeFromBottom (controlsHeight + waveHeight + 20);   // 20: a faixa de status
    return r.removeFromLeft (keyboardWidth);
}

juce::Rectangle<int> NoteTuneEditor::gridArea() const
{
    auto r = keyboardArea();
    return r.withX (r.getRight()).withRight (getWidth());
}

juce::Rectangle<int> NoteTuneEditor::waveArea() const
{
    const auto g = gridArea();
    return { g.getX(), g.getBottom(), g.getWidth(), waveHeight };
}

float NoteTuneEditor::xFor (double seconds) const
{
    const auto g = gridArea().toFloat();
    return g.getX() + (float) ((seconds - viewFrom) / juce::jmax (1.0e-6, viewTo - viewFrom)) * g.getWidth();
}

double NoteTuneEditor::secondsAt (float x) const
{
    const auto g = gridArea().toFloat();
    return viewFrom + (double) ((x - g.getX()) / juce::jmax (1.0f, g.getWidth())) * (viewTo - viewFrom);
}

float NoteTuneEditor::rowHeight() const   { return (float) gridArea().getHeight() / juce::jmax (1.0f, midiHigh - midiLow + 1.0f); }

float NoteTuneEditor::yFor (float midi) const
{
    // a linha da nota inteira k vai de yFor(k + 0,5) a yFor(k - 0,5)
    const auto g = gridArea().toFloat();
    return g.getY() + (midiHigh + 0.5f - midi) * rowHeight();
}

float NoteTuneEditor::midiAt (float y) const
{
    return midiHigh + 0.5f - (y - (float) gridArea().getY()) / juce::jmax (1.0f, rowHeight());
}

juce::Rectangle<float> NoteTuneEditor::noteBounds (int i) const
{
    if (i < 0 || i >= (int) notes.size())
        return {};

    const auto& n = notes[(size_t) i];
    const float h = juce::jmax (6.0f, rowHeight() * 0.78f);
    const float x0 = xFor (n.start), x1 = xFor (n.end);
    return { x0, yFor (n.targetMidi()) - h * 0.5f, juce::jmax (3.0f, x1 - x0), h };
}

int NoteTuneEditor::noteAt (juce::Point<float> p) const
{
    for (int i = (int) notes.size() - 1; i >= 0; --i)
        if (noteBounds (i).expanded (0.0f, 2.0f).contains (p))
            return i;

    return -1;
}

void NoteTuneEditor::resized()
{
    using namespace theme;
    auto bar = toolbarArea().reduced (metric::pad, (metric::toolbar - metric::control) / 2);
    playButton.setBounds (bar.removeFromRight (playButton.getIdealWidth (metric::control)));
    bar.removeFromRight (metric::gap);
    resetButton.setBounds (bar.removeFromRight (resetButton.getIdealWidth (metric::control)));
    bar.removeFromRight (2);
    scaleButton.setBounds (bar.removeFromRight (scaleButton.getIdealWidth (metric::control)));
    bar.removeFromRight (2);
    mergeButton.setBounds (bar.removeFromRight (mergeButton.getIdealWidth (metric::control)));
    bar.removeFromRight (metric::pad);
    tool.setBounds (bar.removeFromRight (tool.getIdealWidth()));
    bar.removeFromRight (metric::pad);
    title.setBounds (bar);

    auto c = controlsArea().reduced (metric::pad, 10);
    followButton.setBounds (c.removeFromRight (200));
    c.removeFromRight (metric::pad);
    const int w = (c.getWidth() - 3 * metric::pad) / 4;

    for (auto* s : { &center, &flatten, &transition, &formant })
    {
        s->setBounds (c.removeFromLeft (w));
        c.removeFromLeft (metric::pad);
    }
}

//==============================================================================
void NoteTuneEditor::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    g.setColour (colour::line);
    g.fillRect (toolbarArea().removeFromBottom (1));
    g.fillRect (controlsArea().removeFromTop (1));

    const auto grid = gridArea();
    const auto keys = keyboardArea();
    const auto& p = app.getModel().get();
    const float rh = rowHeight();

    // as linhas das notas: a escala do projeto clara, a tônica mais clara, fora da escala escura
    for (int k = (int) midiLow; k <= (int) midiHigh; ++k)
    {
        const float y0 = yFor ((float) k + 0.5f);
        const auto row = juce::Rectangle<float> ((float) grid.getX(), y0, (float) grid.getWidth(), rh);
        const bool root = ((k - p.keyRoot) % 12 + 12) % 12 == 0;
        g.setColour (root ? colour::grid::rowRoot : p.inScale (k) ? colour::grid::row : colour::grid::rowOut);
        g.fillRect (row);
        g.setColour (colour::grid::laneEdge);
        g.fillRect (row.withHeight (1.0f));

        // o teclado
        const auto key = juce::Rectangle<float> ((float) keys.getX(), y0, (float) keys.getWidth(), rh);
        const bool black = isBlack (k);
        g.setColour (black ? colour::keys::black : colour::keys::white);
        g.fillRect (black ? key.withTrimmedRight (key.getWidth() * 0.35f) : key);
        g.setColour (colour::keys::seam);
        g.fillRect (key.withHeight (1.0f));

        if (! black && rh >= 11.0f)
        {
            g.setColour (colour::keys::label);
            g.setFont (font (juce::jmin (type::small, rh - 2.0f)));
            g.drawText (noteNamePt (k), key.reduced (6.0f, 0.0f), juce::Justification::centredRight, false);
        }
    }

    // segundos na régua de cima da grade
    {
        const double span = viewTo - viewFrom;
        const double step = span > 30.0 ? 5.0 : span > 10.0 ? 1.0 : span > 4.0 ? 0.5 : 0.25;
        g.setFont (font (type::small));

        for (double t = std::ceil (viewFrom / step) * step; t < viewTo; t += step)
        {
            const float x = xFor (t);
            g.setColour (colour::grid::beat);
            g.fillRect (juce::Rectangle<float> (x, (float) grid.getY(), 1.0f, (float) grid.getHeight()));
            g.setColour (colour::textFaint);
            g.drawText (juce::String (t - clipFrom, step < 1.0 ? 2 : 0).replaceCharacter ('.', ',') + " s",
                        juce::Rectangle<float> (x + 3.0f, (float) grid.getY() + 2.0f, 60.0f, 14.0f), juce::Justification::centredLeft, false);
        }
    }

    g.saveState();
    g.reduceClipRegion (grid);

    // as notas
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(size_t) i];

        if (n.end < viewFrom || n.start > viewTo)
            continue;

        const auto r = noteBounds (i);
        const bool isSelected = selected.count (i) != 0;

        // onde ela estava, se foi movida: só o contorno
        if (std::abs (n.targetMidi() - n.sourceMidi) > 0.02f)
        {
            const auto was = r.withY (yFor (n.sourceMidi) - r.getHeight() * 0.5f);
            g.setColour (colour::textFaint.withAlpha (0.6f));
            g.drawRoundedRectangle (was.reduced (0.5f), metric::radiusSmall, 1.0f);
        }

        g.setColour (isSelected ? colour::signal : n.edited() ? colour::signalTint.withAlpha (0.55f) : colour::raisedHigh);
        g.fillRoundedRectangle (r, metric::radiusSmall);
        g.setColour (isSelected ? colour::signal : n.edited() ? colour::signal : (i == hoverNote ? colour::textDim : colour::floatingEdge));
        g.drawRoundedRectangle (r.reduced (0.5f), metric::radiusSmall, 1.0f);

    }

    // a curva fina: cantada (cinza, onde foi mudada) e a que sai (branca)
    if (analysis != nullptr)
    {
        const auto& curve = analysis->curve;
        const auto drawCurve = [&] (const std::vector<float>& values, juce::Colour colourToUse, float thickness, bool onlyChanged)
        {
            juce::Path path;
            bool open = false;
            const size_t from = (size_t) juce::jmax (0.0, std::floor (viewFrom / curve.hopSeconds));
            const size_t to = juce::jmin (values.size(), (size_t) std::ceil (viewTo / curve.hopSeconds) + 1);

            for (size_t k = from; k < to; ++k)
            {
                const bool show = values[k] > 0.0f && (! onlyChanged || (k < corrected.size() && std::abs (corrected[k] - values[k]) > 0.01f));

                if (! show)
                {
                    open = false;
                    continue;
                }

                const juce::Point<float> pt (xFor ((double) k * curve.hopSeconds), yFor (values[k]));

                if (open)
                    path.lineTo (pt);
                else
                    path.startNewSubPath (pt);

                open = true;
            }

            g.setColour (colourToUse);
            g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        drawCurve (curve.midi, colour::textFaint.withAlpha (0.7f), 1.0f, true);
        drawCurve (corrected.empty() ? curve.midi : corrected, colour::text, 1.5f, false);
    }

    // o nome de cada nota por cima da curva (num fundo da cor da nota, para a curva não riscar o texto)
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(size_t) i];
        const auto r = noteBounds (i);

        if (n.end < viewFrom || n.start > viewTo || r.getWidth() <= 34.0f || r.getHeight() < 12.0f)
            continue;

        const bool isSelected = selected.count (i) != 0;
        const auto labelFont = font (juce::jmin (type::small, r.getHeight() - 2.0f), Weight::medium);
        const int key = (int) std::lround (n.targetMidi());
        auto label = noteNamePt (key);

        if (r.getWidth() > 90.0f)
            label << " " << centsText ((n.targetMidi() - (float) key) * 100.0f);

        const float w = juce::jmin (r.getWidth() - 4.0f, juce::GlyphArrangement::getStringWidth (labelFont, label) + 8.0f);
        const auto plate = juce::Rectangle<float> (r.getX() + 2.0f, r.getY() + 2.0f, w, r.getHeight() - 4.0f);
        g.setColour (isSelected ? colour::signal : n.edited() ? colour::panel.interpolatedWith (colour::signal, 0.35f) : colour::raisedHigh);
        g.fillRoundedRectangle (plate, metric::radiusSmall - 1.0f);
        g.setColour (isSelected ? colour::onSignal : colour::text);
        g.setFont (labelFont);
        g.drawText (label, plate.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    // fora do bloco: escurece
    g.setColour (colour::shade.withAlpha (0.45f));

    if (xFor (clipFrom) > (float) grid.getX())
        g.fillRect (juce::Rectangle<float> ((float) grid.getX(), (float) grid.getY(), xFor (clipFrom) - (float) grid.getX(), (float) grid.getHeight()));

    if (xFor (clipTo) < (float) grid.getRight())
        g.fillRect (juce::Rectangle<float> (xFor (clipTo), (float) grid.getY(), (float) grid.getRight() - xFor (clipTo), (float) grid.getHeight()));

    g.restoreState();

    // a forma de onda embaixo
    const auto wave = waveArea();
    g.setColour (colour::background);
    g.fillRect (wave);

    if (! peaks.empty() && analysis != nullptr && analysis->sampleRate > 0.0)
    {
        const double total = (double) analysis->numFrames / analysis->sampleRate;
        const float mid = (float) wave.getCentreY(), half = (float) wave.getHeight() * 0.45f;

        for (int x = wave.getX(); x < wave.getRight(); ++x)
        {
            const double a = secondsAt ((float) x), b = secondsAt ((float) x + 1.0f);
            const auto ia = (size_t) juce::jlimit (0.0, (double) peaks.size() - 1.0, a / total * (double) peaks.size());
            const auto ib = (size_t) juce::jlimit ((double) ia + 1.0, (double) peaks.size(), b / total * (double) peaks.size());
            float v = 0.0f;

            for (size_t i = ia; i < ib; ++i)
                v = juce::jmax (v, peaks[i]);

            const bool inSelected = std::any_of (selected.begin(), selected.end(), [&] (int i)
                                                 { return a < notes[(size_t) i].end && b > notes[(size_t) i].start; });
            g.setColour (inSelected ? colour::signal.withAlpha (0.8f) : colour::textFaint);
            g.fillRect ((float) x, mid - v * half, 1.0f, juce::jmax (1.0f, 2.0f * v * half));
        }
    }

    // a agulha
    if (playheadSeconds >= viewFrom && playheadSeconds <= viewTo)
    {
        g.setColour (colour::signal);
        g.fillRect (juce::Rectangle<float> (xFor (playheadSeconds) - 0.5f, (float) grid.getY(), 1.5f, (float) (wave.getBottom() - grid.getY())));
    }

    // o status e os avisos
    const auto line = juce::Rectangle<int> (keys.getX() + metric::pad, wave.getBottom(), getWidth() - 2 * metric::pad, 20);
    g.setColour (colour::textFaint);
    g.setFont (font (type::small));
    g.drawText (status, line, juce::Justification::centredLeft, true);

    if (analysis == nullptr)
    {
        g.setColour (colour::textDim);
        g.setFont (font (type::body, Weight::medium));
        g.drawText (analysing ? "Analisando a voz: achando as notas..."_t : analysisError, grid, juce::Justification::centred, true);
    }
}

//==============================================================================
void NoteTuneEditor::mouseMove (const juce::MouseEvent& e)
{
    const int over = noteAt (e.position);

    if (over != hoverNote)
    {
        hoverNote = over;
        setMouseCursor (over >= 0 ? (tool.getSelected() == 1 ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::UpDownResizeCursor)
                                  : juce::MouseCursor::NormalCursor);
        repaint (gridArea());
    }
}

void NoteTuneEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    if (! gridArea().contains (e.getPosition()))
        return;

    const int over = noteAt (e.position);

    if (over < 0)
    {
        if (! e.mods.isShiftDown())
            selectNotes ({});

        return;
    }

    if (tool.getSelected() == 1)
    {
        splitAt (over, secondsAt (e.position.x));
        return;
    }

    if (e.mods.isShiftDown() || e.mods.isCommandDown())
    {
        auto next = selected;
        const bool removing = next.count (over) != 0;

        if (removing) next.erase (over);
        else next.insert (over);

        selectNotes (next);

        if (removing)
            return;   // tirar da escolha não começa um arrasto das outras
    }
    else if (selected.count (over) == 0)
    {
        selectNotes ({ over });
    }

    drag = Drag::move;
    dragStart = notes;
    dragFrom = e.position;
    dragChanged = false;
}

void NoteTuneEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (drag != Drag::move || selected.empty())
        return;

    const bool fine = e.mods.isAltDown();
    const float delta = fine ? (dragFrom.y - e.position.y) / juce::jmax (1.0f, rowHeight()) * 0.25f
                             : (float) std::lround ((dragFrom.y - e.position.y) / juce::jmax (1.0f, rowHeight()));
    notes = dragStart;

    if (std::abs (delta) > 1.0e-3f)
    {
        for (int i : selected)
        {
            auto& n = notes[(size_t) i];
            const float target = n.sourceMidi + n.shift;
            notetune::moveTo (n, fine ? std::round ((target + delta) * 100.0f) / 100.0f : std::round (target + delta));
        }

        dragChanged = true;
    }

    refreshCurve();
    repaint();
}

void NoteTuneEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag != Drag::move)
        return;

    drag = Drag::none;

    if (! dragChanged)
        return;

    auto next = notes;
    notes = dragStart;   // o comando escreve; a tela relê do modelo
    const auto& first = next[(size_t) *selected.begin()];
    commit (std::move (next), follows, "Nota em "_t + noteNamePt ((int) std::lround (first.targetMidi()))
                                           + " " + centsText ((first.targetMidi() - std::round (first.targetMidi())) * 100.0f));
}

void NoteTuneEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (tool.getSelected() == 0)
        if (const int over = noteAt (e.position); over >= 0)
            splitAt (over, secondsAt (e.position.x));
}

void NoteTuneEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown())
    {
        // zoom no tempo em volta do mouse
        const double at = secondsAt (e.position.x);
        const double factor = w.deltaY > 0 ? 0.8 : 1.25;
        const double span = juce::jlimit (0.2, juce::jmax (0.2, clipTo - clipFrom), (viewTo - viewFrom) * factor);
        viewFrom = juce::jlimit (clipFrom, juce::jmax (clipFrom, clipTo - span), at - (at - viewFrom) * span / (viewTo - viewFrom));
        viewTo = viewFrom + span;
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const double span = viewTo - viewFrom;
        const double move = -(w.deltaX != 0.0f ? w.deltaX : w.deltaY) * span * 0.3;
        viewFrom = juce::jlimit (clipFrom, juce::jmax (clipFrom, clipTo - span), viewFrom + move);
        viewTo = viewFrom + span;
    }
    else
    {
        const float move = w.deltaY > 0 ? 2.0f : -2.0f;
        midiLow = juce::jlimit (12.0f, 100.0f, midiLow + move);
        midiHigh = juce::jlimit (midiLow + 12.0f, 120.0f, midiHigh + move);
    }

    repaint();
}

bool NoteTuneEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();

    if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::downKey))
    {
        const float step = mods.isAltDown() ? 0.1f : mods.isCtrlDown() ? 12.0f : 1.0f;
        return moveSelected (k.isKeyCode (juce::KeyPress::upKey) ? step : -step, mods.isAltDown()) || true;
    }

    if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey))
        return resetSelected() || true;

    if (k == juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0))
    {
        std::set<int> all;

        for (int i = 0; i < (int) notes.size(); ++i)
            all.insert (i);

        selectNotes (all);
        return true;
    }

    if (k == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0))
    {
        app.run ("edit.undo");
        return true;
    }

    if (k == juce::KeyPress ('y', juce::ModifierKeys::ctrlModifier, 0)
        || k == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        app.run ("edit.redo");
        return true;
    }

    if (k == juce::KeyPress ('j') || k == juce::KeyPress ('J'))
        return mergeSelected() || true;

    if (k == juce::KeyPress ('c') || k == juce::KeyPress ('C'))
    {
        tool.setSelected (1);
        return true;
    }

    if (k == juce::KeyPress ('v') || k == juce::KeyPress ('V'))
    {
        tool.setSelected (0);
        return true;
    }

    if (k == juce::KeyPress (juce::KeyPress::spaceKey))
    {
        togglePlay();
        return true;
    }

    return false;
}

//==============================================================================
namespace
{
    /** As janelas abertas. Fecham quando o componente que as abriu some (a janela principal fecha antes do
        AppController) e, por garantia, no fim do JUCE. */
    struct NoteTuneWindows : public juce::DeletedAtShutdown,
                             private juce::ComponentListener
    {
        static NoteTuneWindows& get()
        {
            if (instance == nullptr)
                instance = new NoteTuneWindows();

            return *instance;
        }

        ~NoteTuneWindows() override
        {
            for (auto& o : owners)
                if (o != nullptr)
                    o->removeComponentListener (this);

            list.clear();
            instance = nullptr;
        }

        void watch (juce::Component* owner)
        {
            if (owner == nullptr)
                return;

            for (auto& o : owners)
                if (o == owner)
                    return;

            owners.add (owner);
            owner->addComponentListener (this);
        }

        void componentBeingDeleted (juce::Component& c) override
        {
            c.removeComponentListener (this);
            list.clear();
        }

        juce::OwnedArray<NoteTuneWindow> list;
        juce::Array<juce::Component::SafePointer<juce::Component>> owners;
        static inline NoteTuneWindows* instance = nullptr;
    };
}

NoteTuneWindow::NoteTuneWindow (AppController& app, const juce::String& item)
    : juce::DocumentWindow ("Afinar nota a nota"_t, theme::colour::background, juce::DocumentWindow::closeButton, true),
      itemId (item)
{
    setUsingNativeTitleBar (true);
    auto* editor = new NoteTuneEditor (app, item);
    editor->onItemGone = [this] { closeButtonPressed(); };
    setContentOwned (editor, true);
    setResizable (true, false);
    setResizeLimits (720, 440, 2400, 1600);
}

void NoteTuneWindow::closeButtonPressed()
{
    // fecha depois deste evento (quem chamou ainda pode estar na pilha)
    juce::MessageManager::callAsync ([w = juce::Component::SafePointer<NoteTuneWindow> (this)]
    {
        if (w != nullptr)
            NoteTuneWindows::get().list.removeObject (w.getComponent());
    });
}

void NoteTuneWindow::open (AppController& app, const juce::String& itemId, juce::Component* owner)
{
    auto& all = NoteTuneWindows::get();

    for (auto* w : all.list)
        if (w->itemId == itemId)
        {
            w->toFront (true);
            return;
        }

    auto* window = all.list.add (new NoteTuneWindow (app, itemId));
    all.watch (owner);
    window->centreWithSize (window->getWidth(), window->getHeight());
    window->setVisible (true);

    // como as janelas de plugin: pertence à principal (fica por cima dela, não dos outros programas)
    if (owner != nullptr)
        if (auto* top = owner->getTopLevelComponent(); top != nullptr && top->getPeer() != nullptr && window->getPeer() != nullptr)
            setNativeWindowOwner (window->getWindowHandle(), top->getWindowHandle());
}

void NoteTuneWindow::closeAll()
{
    if (NoteTuneWindows::instance != nullptr)
        NoteTuneWindows::instance->list.clear();
}

} // namespace whynot
