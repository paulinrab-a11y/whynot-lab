#include "PianoRoll.h"
#include "NoteNames.h"
#include "Theme.h"
#include "../app/AppController.h"
#include "../ideas/Theory.h"
#include "../model/NoteCurves.h"
#include "../model/NoteTools.h"

#include <limits>
#include <optional>

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

    juce::var curveVar (const std::vector<CurvePoint>& pts)
    {
        juce::Array<juce::var> list;

        for (auto& p : pts)
        {
            auto o = new juce::DynamicObject();
            o->setProperty ("t", p.t);
            o->setProperty ("v", (double) p.v);
            list.add (juce::var (o));
        }

        return list;
    }

    juce::var noteVar (const Note& n, bool withId)
    {
        auto o = new juce::DynamicObject();
        if (withId) o->setProperty ("id", n.id);
        o->setProperty ("t", n.start);
        o->setProperty ("len", n.length);
        o->setProperty ("key", n.key);
        o->setProperty ("vel", n.velocity);
        o->setProperty ("slide", n.slide);

        if (n.muted)   // copiar, colar e duplicar levam o mudo junto
            o->setProperty ("muted", true);

        // e as curvas de pitch bend e mod wheel (26/10)
        if (! n.pitch.empty()) o->setProperty ("pitch", curveVar (n.pitch));
        if (! n.mod.empty())   o->setProperty ("mod", curveVar (n.mod));

        return juce::var (o);
    }

    bool isBlackKey (int key)
    {
        const int k = ((key % 12) + 12) % 12;
        return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
    }

    struct SnapOption { const char* name; int ticks; };

    // A primeira é a automática (o nome dela mostra a divisão do zoom de agora). O id de cada item na
    // lista é a posição aqui + 1; o título das tercinas e as separações não contam.
    const SnapOption snapOptions[] = {
        { "Auto", timegrid::snapAuto },
        { "Grade: 1/4 passo", ticksPerStep / 4 },
        { "Grade: 1/2 passo", ticksPerStep / 2 },
        { "Grade: passo", ticksPerStep },
        { "Grade: tempo", timegrid::snapBeat },
        { "Grade: compasso", timegrid::snapBar },
        { "Grade: 1/6 passo", ticksPerStep / 6 },
        { "Grade: 1/3 passo", ticksPerStep / 3 },
        { "Grade: 1/6 tempo", ticksPerBeat / 6 },
        { "Grade: 1/3 tempo", ticksPerBeat / 3 },
        { "Sem grade", 1 },
    };

    constexpr int firstTripletOption = 6, noGridOption = 10;

    /** Um item de menu com ação. O JUCE só dispara item com id (o 0 quer dizer "nada escolhido"). */
    void addAction (juce::PopupMenu& menu, int id, const juce::String& text, std::function<void()> action,
                    bool ticked = false, const juce::String& keys = {})
    {
        juce::PopupMenu::Item item (text);
        item.itemID = id;
        item.action = std::move (action);
        item.isTicked = ticked;
        item.shortcutKeyDescription = keys;
        menu.addItem (std::move (item));
    }

    juce::String scaleLabel (const juce::String& id)
    {
        if (id == "minor")           return "Menor";
        if (id == "major")           return "Maior";
        if (id == "harmonicMinor")   return "Menor harmônica"_t;
        if (id == "phrygian")        return "Frígio"_t;
        if (id == "dorian")          return "Dórico"_t;
        if (id == "pentatonicMinor") return "Pentatônica menor"_t;
        return "Cromática"_t;
    }

}

//==============================================================================
PianoRollGrid::PianoRollGrid (AppController& a) : app (a)
{
    // pinta o fundo inteiro: opaco, a agulha repinta só a grade (sem a tela de trás), fluidez 21/10
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (false);

    for (auto* bar : { &hScroll, &vScroll })
    {
        bar->setAutoHide (false);
        bar->setWantsKeyboardFocus (false);
        bar->addListener (this);
        addAndMakeVisible (bar);
    }
}

PianoRollGrid::~PianoRollGrid()
{
    endEdit();
    hScroll.removeListener (this);
    vScroll.removeListener (this);
}

void PianoRollGrid::refresh (const Project& p, const juce::String& newChannel)
{
    const bool channelChanged = newChannel != channel;
    channel = newChannel;

    const auto* ch = p.findChannel (channel);
    const auto* pat = p.currentPatternPtr();

    channelOk = ch != nullptr && ! ch->isAudio() && pat != nullptr;
    channelMono = ch != nullptr && ch->mono;
    channelRootKey = ch != nullptr ? ch->rootKey : 60;
    channelName = ch != nullptr ? ch->name : juce::String();
    patternTicks = pat != nullptr ? pat->lengthTicks() : ticksPerBar4;
    keyRoot = p.keyRoot;
    degrees = Project::scaleDegrees (p.scale);
    stepsPerBeat = juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen));
    stepsPerBar = stepsPerBeat * juce::jmax (1, p.timeSigNum);
    notifyGridChange();   // a fórmula de compasso muda o nome da grade automática
    glideTicks = ch != nullptr ? ch->glideMs / 1000.0 * p.bpm / 60.0 * ticksPerBeat : 0.0;
    bendRange = ch != nullptr ? curves::bendRangeFor (*ch) : 2.0f;
    channelBends = ch != nullptr && ! ch->isAudio();   // synth, VST3, sampler e WHYNOT Piano (estes dois desde 27/10)

    notes.clear();
    stepNotes.clear();
    ghosts.clear();

    if (pat != nullptr && ch != nullptr)
    {
        if (auto* list = pat->notesFor (channel))
            notes = *list;

        // Passos do channel rack aparecem como notas; viram notas de verdade na primeira edição.
        if (auto it = pat->steps.find (channel); it != pat->steps.end())
        {
            for (size_t i = 0; i < it->second.size(); ++i)
            {
                const auto& s = it->second[i];

                if (! s.on)
                    continue;

                const int roll = juce::jmax (1, s.roll);

                for (int k = 0; k < roll; ++k)
                {
                    Note n;
                    n.id = -(int) stepNotes.size() - 1;
                    n.start = (int) i * ticksPerStep + k * (ticksPerStep / roll);
                    n.length = ticksPerStep / roll;
                    n.key = juce::jlimit (0, 127, ch->rootKey + s.pitch);
                    n.velocity = s.velocity;
                    stepNotes.push_back (n);
                }
            }
        }

        for (auto& [id, list] : pat->notes)
            if (id != channel)
                for (auto& n : list)
                    ghosts.push_back ({ n.start, n.length, n.key });
    }

    // a seleção só guarda notas que ainda existem
    for (auto it = selected.begin(); it != selected.end();)
        it = findNote (*it) == nullptr ? selected.erase (it) : std::next (it);

    if (channelChanged)
    {
        selected.clear();
        scrollToContent();
    }

    clampScroll();
    updateScrollBars();
    repaint();
}

void PianoRollGrid::revealNotes()
{
    scrollToContent();
    clampScroll();
    updateScrollBars();
    repaint();
}

void PianoRollGrid::setTool (Tool t)
{
    tool = t;
    repaint();
}

void PianoRollGrid::setPlayhead (double ticks)
{
    if (std::abs (ticks - playhead) < 0.5)
        return;

    const int bottom = gridArea().getBottom();
    auto column = [this, bottom] (double t)
    {
        return juce::Rectangle<int> ((int) tickToX (t) - 2, 0, 5, bottom);
    };

    if (playhead >= 0.0) repaint (column (playhead));
    playhead = ticks;
    if (playhead >= 0.0) repaint (column (playhead));
}

//==============================================================================
juce::Rectangle<int> PianoRollGrid::gridArea() const
{
    return { keyboardWidth, rulerHeight, juce::jmax (0, getWidth() - keyboardWidth - scrollSize),
             juce::jmax (0, getHeight() - rulerHeight - velocityHeight - scrollSize) };
}

void PianoRollGrid::setPatternStart (int ticks)
{
    if (ticks == patternStart)
        return;

    patternStart = ticks;
    repaint();   // a marca na régua e a linha na grade (muda só com um clique ou Alt+setas)
}

juce::Point<float> PianoRollGrid::rulerPoint (int ticks) const
{
    return { tickToX (ticks), (float) rulerArea().getCentreY() };
}

juce::Rectangle<int> PianoRollGrid::keyboardArea() const   { return { 0, rulerHeight, keyboardWidth, gridArea().getHeight() }; }
juce::Rectangle<int> PianoRollGrid::rulerArea() const      { return { keyboardWidth, 0, gridArea().getWidth(), rulerHeight }; }
juce::Rectangle<int> PianoRollGrid::velocityArea() const   { return { keyboardWidth, gridArea().getBottom(), gridArea().getWidth(), velocityHeight }; }
juce::Rectangle<int> PianoRollGrid::laneLabelArea() const  { return { 0, gridArea().getBottom(), keyboardWidth, velocityHeight }; }

void PianoRollGrid::setLaneMode (LaneMode m)
{
    laneMode = m;
    repaint (velocityArea().withX (0).withWidth (getWidth()));
}

float PianoRollGrid::tickToX (double tick) const   { return (float) (keyboardWidth + (tick - scrollTicks) * pxPerTick); }
double PianoRollGrid::xToTick (float x) const      { return scrollTicks + (x - (float) keyboardWidth) / pxPerTick; }
float PianoRollGrid::keyToY (int key) const        { return (float) (rulerHeight + ((127 - key) - scrollRows) * rowHeight); }

int PianoRollGrid::yToKey (float y) const
{
    return juce::jlimit (0, 127, 127 - (int) std::floor ((y - (float) rulerHeight) / rowHeight + scrollRows));
}

juce::Rectangle<float> PianoRollGrid::noteBounds (const Note& n) const
{
    return { tickToX (n.start), keyToY (n.key), juce::jmax (4.0f, (float) n.length * pxPerTick), rowHeight };
}

void PianoRollGrid::setSnap (int setting)
{
    const bool special = setting == timegrid::snapAuto || setting == timegrid::snapBeat || setting == timegrid::snapBar;
    snapSetting = special ? setting : juce::jmax (1, setting);
    repaint();
}

int PianoRollGrid::getSnapTicks() const
{
    return timegrid::resolveSnap (snapSetting, pxPerTick, meter(), spacing);
}

juce::String PianoRollGrid::getAutoGridName() const
{
    return timegrid::name (timegrid::lineTicks (pxPerTick, meter(), spacing), meter());
}

void PianoRollGrid::notifyGridChange()
{
    if (const auto name = getAutoGridName(); name != lastAutoName)
    {
        lastAutoName = name;

        if (onGridChanged)
            onGridChanged();
    }
}

void PianoRollGrid::zoomBy (float notches, float anchorX)
{
    const double anchorTick = xToTick (anchorX);
    pxPerTick = juce::jlimit (minPxPerTick, maxPxPerTick, pxPerTick * std::pow (1.15f, notches));
    scrollTicks = anchorTick - (anchorX - (float) keyboardWidth) / pxPerTick;

    clampScroll();
    updateScrollBars();
    notifyGridChange();
    repaint();
}

int PianoRollGrid::snapDown (double tick) const
{
    const int snap = getSnapTicks();
    const int t = snap > 1 ? (int) std::floor (tick / snap) * snap : (int) std::floor (tick);
    return juce::jmax (0, t);
}

int PianoRollGrid::snapNearest (double tick) const
{
    const int snap = getSnapTicks();
    const int t = snap > 1 ? juce::roundToInt (tick / snap) * snap : juce::roundToInt (tick);
    return juce::jmax (0, t);
}

int PianoRollGrid::contentTicks() const
{
    int end = patternTicks;

    for (auto& n : notes)
        end = juce::jmax (end, n.end());

    return end + 2 * stepsPerBar * ticksPerStep;
}

bool PianoRollGrid::inScale (int key) const
{
    const int degree = ((key - keyRoot) % 12 + 12) % 12;
    return std::find (degrees.begin(), degrees.end(), degree) != degrees.end();
}

int PianoRollGrid::lockKey (int key) const
{
    return scaleLock ? notetools::nearestInScale (key, keyRoot, degrees) : key;
}

int PianoRollGrid::stepKey (int key, int direction) const
{
    int k = key + direction;

    while (scaleLock && k > 0 && k < 127 && ! inScale (k))
        k += direction;

    return juce::jlimit (0, 127, k);
}

const Note* PianoRollGrid::findNote (int id) const
{
    for (auto& n : notes)
        if (n.id == id)
            return &n;

    return nullptr;
}

const Note* PianoRollGrid::noteAt (juce::Point<float> p, bool& onRightEdge) const
{
    onRightEdge = false;

    // de trás para frente: a nota desenhada por cima ganha
    for (auto it = notes.rbegin(); it != notes.rend(); ++it)
    {
        const auto r = noteBounds (*it);

        if (r.contains (p))
        {
            onRightEdge = p.x >= r.getRight() - juce::jmin (8.0f, r.getWidth() * 0.35f);
            return &*it;
        }
    }

    return nullptr;
}

//==============================================================================
void PianoRollGrid::resized()
{
    const auto g = gridArea();
    vScroll.setBounds (getWidth() - scrollSize, g.getY(), scrollSize, g.getHeight());
    hScroll.setBounds (g.getX(), getHeight() - scrollSize, g.getWidth(), scrollSize);

    if (centreKeyOnResize >= 0 && g.getHeight() > 0)
    {
        scrollRows = (127 - centreKeyOnResize) - g.getHeight() / rowHeight / 2.0;
        centreKeyOnResize = -1;
    }

    clampScroll();
    updateScrollBars();
}

void PianoRollGrid::clampScroll()
{
    const auto g = gridArea();
    const double visibleRows = g.getHeight() / rowHeight;
    scrollRows = juce::jlimit (0.0, juce::jmax (0.0, 128.0 - visibleRows), scrollRows);
    const double visibleTicks = g.getWidth() / pxPerTick;
    scrollTicks = juce::jlimit (0.0, juce::jmax (0.0, contentTicks() - visibleTicks), scrollTicks);
}

void PianoRollGrid::updateScrollBars()
{
    const juce::ScopedValueSetter<bool> guard (updatingScrollBars, true);
    const auto g = gridArea();
    vScroll.setRangeLimits (0.0, 128.0, juce::dontSendNotification);
    vScroll.setCurrentRange (scrollRows, g.getHeight() / rowHeight, juce::dontSendNotification);
    hScroll.setRangeLimits (0.0, (double) contentTicks(), juce::dontSendNotification);
    hScroll.setCurrentRange (scrollTicks, g.getWidth() / pxPerTick, juce::dontSendNotification);
}

void PianoRollGrid::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (updatingScrollBars)
        return;

    if (bar == &vScroll) scrollRows = start;
    else                 scrollTicks = start;

    repaint();
}

void PianoRollGrid::scrollToContent()
{
    int lo = 128, hi = -1;

    for (auto* list : { &notes, &stepNotes })
        for (auto& n : *list)
        {
            lo = juce::jmin (lo, n.key);
            hi = juce::jmax (hi, n.key);
        }

    // sem notas: abre na nota de raiz do canal (o 808 em Dó 2, o sino em Dó 5)
    const int centre = hi >= 0 ? (lo + hi) / 2 : channelRootKey;
    scrollTicks = 0.0;

    if (gridArea().getHeight() > 0)
        scrollRows = (127 - centre) - gridArea().getHeight() / rowHeight / 2.0;
    else
        centreKeyOnResize = centre;
}

//==============================================================================
void PianoRollGrid::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::background);

    if (! channelOk)
    {
        g.setColour (colour::textDim);
        g.setFont (font (15.0f));
        g.drawText ("Escolha um canal de sampler no channel rack para escrever notas. Canais de áudio tocam pela playlist."_t,
                    getLocalBounds().reduced (24), juce::Justification::centred);
        return;
    }

    const auto area = gridArea();

    // A agulha repinta uma coluna de 5 px a cada quadro: cada parte só é desenhada se a parte suja chega
    // nela (o teclado, a régua e a força tinham texto desenhado à toa, mesmo recortado). Fluidez, 21/10.
    const auto dirty = g.getClipBounds();

    if (dirty.intersects (area))
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area);
        paintGrid (g, area);
        paintNotes (g, area);
        paintModes (g);
    }

    if (dirty.intersects (rulerArea()))
        paintRuler (g);

    if (dirty.intersects (keyboardArea()))
        paintKeyboard (g);

    if (dirty.intersects (velocityArea().withX (0).withWidth (getWidth())))
        paintVelocity (g);

    // parado no PAT, a linha fina de onde o play vai começar (como a agulha parada da playlist)
    if (playhead < 0.0 && patternStart > 0)
    {
        const float x = tickToX (patternStart);

        if (x >= (float) area.getX() && x <= (float) area.getRight())
        {
            g.setColour (colour::textDim.withAlpha (0.6f));
            g.fillRect (x - 0.5f, (float) area.getY(), 1.0f, (float) area.getHeight());
        }
    }

    if (playhead >= 0.0)
    {
        const float x = tickToX (playhead);

        if (x >= (float) area.getX() && x <= (float) area.getRight())
        {
            g.setColour (colour::text);
            g.fillRect (x - 0.5f, 0.0f, 1.5f, (float) area.getBottom());
        }
    }

    // cantos
    g.setColour (colour::panel);
    g.fillRect (0, 0, keyboardWidth, rulerHeight);
    g.fillRect (0, getHeight() - scrollSize, keyboardWidth, scrollSize);
    g.fillRect (getWidth() - scrollSize, 0, scrollSize, rulerHeight);
    g.fillRect (getWidth() - scrollSize, area.getBottom(), scrollSize, getHeight() - area.getBottom());

    if (const juce::Rectangle<int> nameArea (8, 0, keyboardWidth - 12, rulerHeight); dirty.intersects (nameArea))
    {
        g.setColour (colour::textDim);
        g.setFont (font (12.0f, Weight::medium));
        g.drawFittedText (channelName, nameArea, juce::Justification::centredLeft, 1, 0.8f);
    }
}

void PianoRollGrid::paintGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    using namespace theme;
    const bool chromatic = degrees.size() == 12;
    const int top = yToKey ((float) area.getY());
    const int bottom = yToKey ((float) area.getBottom() - 1.0f);

    // linhas das teclas como no teclado, em qualquer escala (vídeo do produtor, 22/10): a da tecla preta escura e a da
    // branca clara, para quem não lê nota se achar pelo teclado. A escala do projeto fica num fio na beira da grade:
    // as notas dela marcadas e a tônica em laranja, sem trocar o claro e o escuro das linhas.
    for (int key = bottom; key <= top; ++key)
    {
        const float y = keyToY (key);
        g.setColour (isBlackKey (key) ? colour::grid::rowOut : colour::grid::row);
        g.fillRect (juce::Rectangle<float> ((float) area.getX(), y, (float) area.getWidth(), rowHeight));

        g.setColour (key % 12 == 0 ? colour::line : colour::shade.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> ((float) area.getX(), y + rowHeight - 1.0f, (float) area.getWidth(), 1.0f));

        const bool root = ((key - keyRoot) % 12 + 12) % 12 == 0;

        if (root || (! chromatic && inScale (key)))
        {
            g.setColour (root ? colour::signal : colour::textFaint.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> ((float) area.getX(), y + 1.0f, 3.0f, juce::jmax (1.0f, rowHeight - 3.0f)));
        }
    }

    if (areBeatHintsVisible())
        paintBeatHints (g, area);

    // colunas: compasso, tempo, passo e, de perto, as divisões do passo (a grade segue o zoom)
    const auto m = meter();
    const int every = timegrid::visibleTicks (snapSetting, pxPerTick, m, spacing);
    const auto dirty = g.getClipBounds().getIntersection (area);
    const auto firstLine = juce::jmax ((juce::int64) 0, (juce::int64) std::floor (xToTick ((float) dirty.getX() - 1.0f) / every));
    const auto lastLine = (juce::int64) std::ceil (xToTick ((float) dirty.getRight() + 1.0f) / every);

    for (auto i = firstLine; i <= lastLine && ! dirty.isEmpty(); ++i)
    {
        const auto tick = i * every;

        switch (timegrid::levelOf (tick, m))
        {
            case timegrid::Level::bar:   g.setColour (colour::textDim.withAlpha (0.3f)); break;
            case timegrid::Level::beat:  g.setColour (colour::line); break;
            case timegrid::Level::step:  g.setColour (colour::shade.withAlpha (0.6f)); break;
            case timegrid::Level::fine:  g.setColour (colour::shade.withAlpha (0.3f)); break;
        }

        g.fillRect (tickToX ((double) tick), (float) area.getY(), 1.0f, (float) area.getHeight());
    }

    // depois do fim do padrão: escurecido
    const float endX = tickToX (patternTicks);

    if (endX < (float) area.getRight())
    {
        g.setColour (colour::shade.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (endX, (float) area.getY(), (float) area.getRight() - endX, (float) area.getHeight()));
        g.setColour (colour::textDim.withAlpha (0.7f));
        g.fillRect (endX - 0.5f, (float) area.getY(), 1.5f, (float) area.getHeight());
    }
}

//==============================================================================
// Dicas do beat (21/10, noite): o acorde do beat em cada compasso, discreto, em tons de cinza (cor não
// dá importância). A fundamental, onde o 808 casa, fica mais clara (mais ainda na região do 808, Dó 1 a
// Si 2); a terça e a quinta, mais fracas. Compasso com pouca confiança fica mais apagado.
namespace
{
    juce::String beatChordName (const BeatHint& h, bool flats)
    {
        const auto name = ideas::chordName (h.root, h.minor, flats);
        return juce::String::fromUTF8 (name.data(), (int) name.size());
    }

    juce::String beatRootName (const BeatHint& h, bool flats)
    {
        const auto name = ideas::pitchNamePt (h.root, flats);
        return juce::String::fromUTF8 (name.data(), (int) name.size());
    }

    juce::String beatHintTooltip (const BeatHint& h, bool flats)
    {
        return "Compasso "_t + juce::String (h.songBar + 1) + " do beat: " + beatChordName (h, flats) + " ("
               + juce::String (juce::roundToInt (h.confidence * 100.0)) + "%). O 808 casa em "_t + beatRootName (h, flats)
               + " (a linha mais clara); as mais fracas são as outras notas do acorde."_t;
    }
}

void PianoRollGrid::setBeatHints (std::vector<BeatHint> hints, bool visible, bool flats)
{
    const bool same = visible == showBeatHints && flats == beatHintFlats && hints.size() == beatHints.size()
                      && std::equal (hints.begin(), hints.end(), beatHints.begin(), [] (const BeatHint& a, const BeatHint& b)
                         { return a.start == b.start && a.end == b.end && a.root == b.root && a.minor == b.minor
                                  && std::abs (a.confidence - b.confidence) < 1.0e-6; });

    if (same)
        return;

    beatHintFlats = flats;
    beatHints = std::move (hints);
    showBeatHints = visible;
    repaint();
}

const BeatHint* PianoRollGrid::beatHintAt (double tick) const
{
    for (auto& h : beatHints)
        if (tick >= h.start && tick < h.end)
            return &h;

    return nullptr;
}

void PianoRollGrid::paintBeatHints (juce::Graphics& g, juce::Rectangle<int> area)
{
    using namespace theme;
    const int top = yToKey ((float) area.getY());
    const int bottom = yToKey ((float) area.getBottom() - 1.0f);

    for (auto& h : beatHints)
    {
        const float x0 = juce::jmax ((float) area.getX(), tickToX (h.start));
        const float x1 = juce::jmin ((float) area.getRight(), tickToX (h.end));

        if (x1 <= x0)
            continue;

        const float strength = 0.4f + 0.6f * (float) juce::jlimit (0.0, 1.0, h.confidence);

        for (int key = bottom; key <= top; ++key)
        {
            const int rel = ((key - h.root) % 12 + 12) % 12;
            float alpha = 0.0f;

            if (rel == 0)
                alpha = key >= 24 && key < 48 ? 0.17f : 0.10f;
            else if (rel == 7 || rel == (h.minor ? 3 : 4))
                alpha = 0.045f;
            else
                continue;

            g.setColour (colour::text.withAlpha (alpha * strength));
            g.fillRect (juce::Rectangle<float> (x0, keyToY (key), x1 - x0, rowHeight - 1.0f));
        }
    }
}

void PianoRollGrid::paintBeatHintNames (juce::Graphics& g, juce::Rectangle<int> r)
{
    using namespace theme;
    const int barTicks = stepsPerBar * ticksPerStep;
    const auto barFont = font (13.0f, Weight::medium);
    g.setFont (font (12.0f, Weight::medium));

    for (auto& h : beatHints)
    {
        const float x = tickToX (h.start);

        if (x > (float) r.getRight() || tickToX (h.end) < (float) r.getX())
            continue;

        // depois do número do compasso: "1  Am"
        const auto number = juce::String (h.start / juce::jmax (1, barTicks) + 1);
        const float left = x + 4.0f + juce::GlyphArrangement::getStringWidth (barFont, number) + 7.0f;
        const auto name = beatChordName (h, beatHintFlats) + (h.confidence < 0.5 ? "?" : "");
        g.setColour (h.confidence < 0.5 ? colour::textFaint : colour::textDim);
        g.drawText (name, juce::Rectangle<float> (left, (float) r.getY(), juce::jmax (0.0f, tickToX (h.end) - left - 4.0f), (float) r.getHeight() - 4.0f),
                    juce::Justification::centredLeft, true);
    }
}

void PianoRollGrid::paintNotes (juce::Graphics& g, juce::Rectangle<int> area)
{
    using namespace theme;
    const auto clip = area.toFloat().getIntersection (g.getClipBounds().toFloat().expanded (2.0f));

    // notas dos outros canais do padrão, de fundo
    g.setColour (colour::textDim.withAlpha (0.16f));

    for (auto& gh : ghosts)
    {
        const auto r = juce::Rectangle<float> (tickToX (gh.start), keyToY (gh.key),
                                               juce::jmax (3.0f, (float) gh.length * pxPerTick), rowHeight).reduced (0.5f, 1.5f);
        if (r.intersects (clip))
            g.fillRoundedRectangle (r, 2.0f);
    }

    auto drawNote = [&] (const Note& n, bool isSelected)
    {
        const auto r = noteBounds (n).reduced (0.0f, 1.0f);

        if (! r.intersects (clip))
            return;

        if (n.muted)
        {
            // muda (Alt+M): só o contorno, apagada; continua editável e não toca
            g.setColour (colour::textDim.withAlpha (0.18f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (colour::textDim.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        }
        else
        {
            g.setColour (colour::signal.withAlpha (0.5f + 0.5f * ((float) n.velocity / 127.0f)));
            g.fillRoundedRectangle (r, 3.0f);
        }

        if (isSelected)
        {
            g.setColour (colour::text);
            g.drawRoundedRectangle (r.reduced (0.75f), 3.0f, 1.5f);
        }

        // slide: rampa preta no começo da nota
        const float h = r.getHeight();

        if (n.slide && r.getWidth() > h)
        {
            juce::Path ramp;
            ramp.addTriangle (r.getX() + 3.0f, r.getBottom() - 2.5f,
                              r.getX() + 3.0f + h, r.getBottom() - 2.5f,
                              r.getX() + 3.0f + h, r.getY() + 2.5f);
            g.setColour (colour::onSignal.withAlpha (0.85f));
            g.fillPath (ramp);
        }

        if (r.getWidth() > 36.0f && rowHeight >= 13.0f)
        {
            g.setColour (n.muted ? colour::textDim : colour::onSignal);
            g.setFont (font (juce::jmin (13.0f, rowHeight - 3.0f), Weight::semibold));
            g.drawText (noteNamePt (n.key), r.withTrimmedLeft (n.slide ? h + 6.0f : 5.0f), juce::Justification::centredLeft, false);
        }
    };

    for (auto& n : stepNotes)
        drawNote (n, false);

    for (auto& n : notes)
        if (selected.count (n.id) == 0)
            drawNote (n, false);

    for (auto& n : notes)
        if (selected.count (n.id) != 0)
            drawNote (n, true);

    // Glide (canal mono): a curva sai da altura da nota que estava soando e chega na nota
    // com slide no tempo do glide. É exatamente o que o sampler faz. Sem nenhuma nota com slide não há curva: pula a
    // cópia e a ordenação de todas as notas, que rodavam a cada quadro da agulha (desempenho, 29/10).
    auto hasSlide = [] (const std::vector<Note>& list) { return std::any_of (list.begin(), list.end(), [] (const Note& n) { return n.slide; }); };

    if (channelMono && ! clip.isEmpty() && (hasSlide (notes) || hasSlide (stepNotes)))
    {
        std::vector<Note> all (notes);
        all.insert (all.end(), stepNotes.begin(), stepNotes.end());
        std::stable_sort (all.begin(), all.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

        for (size_t i = 1; i < all.size(); ++i)
        {
            const auto& cur = all[i];

            if (! cur.slide)
                continue;

            const Note* prev = nullptr;

            for (size_t j = i; j-- > 0;)
                if (all[j].start < cur.start) { prev = &all[j]; break; }

            if (prev == nullptr || prev->key == cur.key)
                continue;

            const float x0 = tickToX (cur.start);
            const float x1 = tickToX (cur.start + juce::jmax (glideTicks, 20.0));

            if (x1 + 4.0f < clip.getX() || x0 - 4.0f > clip.getRight())
                continue;

            const float y0 = keyToY (prev->key) + rowHeight * 0.5f;
            const float y1 = keyToY (cur.key) + rowHeight * 0.5f;

            juce::Path curve;
            curve.startNewSubPath (x0, y0);
            curve.cubicTo ((x0 + x1) * 0.5f, y0, (x0 + x1) * 0.5f, y1, x1, y1);
            g.setColour (colour::background.withAlpha (0.7f));
            g.strokePath (curve, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour::text);
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    if (drag == Drag::select && ! rubberBand.isEmpty())
    {
        g.setColour (colour::text.withAlpha (0.08f));
        g.fillRect (rubberBand);
        g.setColour (colour::text.withAlpha (0.6f));
        g.drawRect (rubberBand, 1.0f);
    }
}

void PianoRollGrid::paintModes (juce::Graphics& g)
{
    // o carimbo e o encaixe mudam o que o clique faz: ficam à vista enquanto ligados (Esc desliga o carimbo)
    using namespace theme;
    juce::StringArray parts;

    for (auto& s : notetools::chordShapes())
        if (chordStamp == s.id)
            parts.add ("Acorde: "_t + juce::String::fromUTF8 (s.label));

    if (scaleLock)
        parts.add ("Na escala"_t);

    if (parts.isEmpty())
        return;

    const auto text = parts.joinIntoString ("  ·  ");
    const auto f = font (type::small, Weight::medium);
    const int w = juce::GlyphArrangement::getStringWidthInt (f, text) + 24;
    const auto area = gridArea();
    const auto chip = juce::Rectangle<int> (area.getRight() - w - 8, area.getY() + 8, w, 22).toFloat();
    g.setColour (colour::panel.withAlpha (0.94f));
    g.fillRoundedRectangle (chip, 11.0f);
    g.setColour (colour::line);
    g.drawRoundedRectangle (chip.reduced (0.5f), 11.0f, 1.0f);
    g.setColour (colour::text);
    g.setFont (f);
    g.drawText (text, chip, juce::Justification::centred, false);
}

void PianoRollGrid::paintRuler (juce::Graphics& g)
{
    using namespace theme;
    const auto r = rulerArea();
    g.setColour (colour::panel);
    g.fillRect (r);
    g.setColour (colour::line);
    g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (r);

    const int barTicks = stepsPerBar * ticksPerStep;
    const float pxPerBar = (float) barTicks * pxPerTick;
    const int labelEvery = pxPerBar >= 40.0f ? 1 : (pxPerBar >= 20.0f ? 2 : 4);
    const int firstBar = juce::jmax (0, (int) std::floor (xToTick ((float) r.getX()) / barTicks));
    const int lastBar = (int) std::ceil (xToTick ((float) r.getRight()) / barTicks);

    for (int bar = firstBar; bar <= lastBar; ++bar)
    {
        const float x = tickToX ((double) bar * barTicks);
        g.setColour (colour::textDim.withAlpha (0.6f));
        g.fillRect (x, (float) r.getBottom() - 11.0f, 1.0f, 10.0f);

        if (pxPerBar >= 60.0f)
            for (int beat = 1; beat * stepsPerBeat < stepsPerBar; ++beat)
                g.fillRect (tickToX ((double) bar * barTicks + beat * stepsPerBeat * ticksPerStep), (float) r.getBottom() - 5.0f, 1.0f, 4.0f);

        if (bar % labelEvery == 0)
        {
            g.setColour ((double) bar * barTicks < patternTicks ? colour::text : colour::textDim);
            g.setFont (font (13.0f, Weight::medium));
            g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, (float) r.getY(), 40.0f, (float) r.getHeight() - 4.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    if (areBeatHintsVisible() && pxPerBar >= 60.0f)
        paintBeatHintNames (g, r);

    // de perto: o tempo ("3.2"), o passo ("3.2.4") e marquinhas nas divisões à vista
    timegrid::paintFineRuler (g, r, xToTick ((float) r.getX()), xToTick ((float) r.getRight()),
                              timegrid::visibleTicks (snapSetting, pxPerTick, meter(), spacing), pxPerTick, meter(),
                              [this] (double t) { return tickToX (t); }, font (11.0f), colour::textFaint, colour::textFaint.withAlpha (0.8f));

    // fim do padrão
    const float endX = tickToX (patternTicks);
    juce::Path marker;
    marker.addTriangle (endX - 6.0f, (float) r.getY() + 4.0f, endX, (float) r.getY() + 4.0f, endX, (float) r.getY() + 12.0f);
    g.setColour (colour::textDim);
    g.fillPath (marker);

    // o começo do play no PAT (29/10): o triângulo da agulha da playlist; branco quando não é o começo do padrão
    if (patternStart >= 0)
    {
        const float x = tickToX (patternStart);
        juce::Path start;
        start.addTriangle (x - 5.0f, (float) r.getY() + 3.0f, x + 5.0f, (float) r.getY() + 3.0f, x, (float) r.getY() + 11.0f);
        g.setColour (patternStart > 0 ? colour::text : colour::textDim);
        g.fillPath (start);
    }

    // a alça de esticar a seleção, no fim dela: um triângulo claro que se arrasta para os lados
    if (const auto handle = stretchHandle(); ! handle.isEmpty())
    {
        juce::Path grip;
        grip.addTriangle (handle.getX(), handle.getY(), handle.getRight(), handle.getY(), handle.getCentreX(), handle.getBottom());
        g.setColour (drag == Drag::stretch ? colour::signal : colour::text);
        g.fillPath (grip);
    }
}

void PianoRollGrid::paintKeyboard (juce::Graphics& g)
{
    using namespace theme;
    const auto kb = keyboardArea();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (kb);

    const auto white = colour::keys::white, black = colour::keys::black, seam = colour::keys::seam;
    g.setColour (white);
    g.fillRect (kb);

    const int top = yToKey ((float) kb.getY());
    const int bottom = yToKey ((float) kb.getBottom() - 1.0f);
    const int hoverKey = kb.toFloat().contains (hoverPos) ? yToKey (hoverPos.y) : -1;

    for (int key = bottom; key <= top; ++key)
    {
        const auto row = juce::Rectangle<float> ((float) kb.getX(), keyToY (key), (float) kb.getWidth(), rowHeight);
        const bool lit = key == previewKey;

        if (isBlackKey (key))
        {
            const auto cap = row.withWidth ((float) kb.getWidth() * 0.6f);
            g.setColour (seam);
            g.fillRect (cap.getRight(), row.getCentreY(), row.getRight() - cap.getRight(), 1.0f);
            g.setColour (lit ? colour::signal : (key == hoverKey ? colour::keys::hoverBlack : black));
            g.fillRect (cap);
        }
        else
        {
            if (lit || key == hoverKey)
            {
                g.setColour (lit ? colour::signal : colour::keys::hoverWhite);
                g.fillRect (row);
            }

            const int k = key % 12;

            if (k == 0 || k == 5)   // B|C e E|F não têm tecla preta no meio
            {
                g.setColour (seam);
                g.fillRect (row.getX(), row.getBottom() - 1.0f, row.getWidth(), 1.0f);
            }
        }

        if (key % 12 == 0 && rowHeight >= 10.0f)
        {
            g.setColour (colour::keys::label);
            g.setFont (font (juce::jmin (12.0f, rowHeight - 2.0f), Weight::semibold));
            g.drawText (noteNamePt (key), row.withTrimmedRight (6.0f), juce::Justification::centredRight, false);
        }
    }

    g.setColour (colour::line);
    g.fillRect (kb.getRight() - 1, kb.getY(), 1, kb.getHeight());
}

void PianoRollGrid::paintVelocity (juce::Graphics& g)
{
    using namespace theme;
    const auto v = velocityArea();
    g.setColour (colour::panel);
    g.fillRect (v.withX (0).withWidth (getWidth()));
    g.setColour (colour::line);
    g.fillRect (0, v.getY(), getWidth(), 1);

    // as abas da raia (26/10): Força, Pitch e Mod, uma embaixo da outra; no Pitch, o alcance embaixo delas
    {
        const juce::String names[] = { "Força"_t, "Pitch", "Mod" };

        for (int i = 0; i < 3; ++i)
        {
            const bool on = (int) laneMode == i;
            const juce::Rectangle<int> tab (8, v.getY() + 6 + i * 20, keyboardWidth - 12, 18);

            if (on)
            {
                g.setColour (colour::signal);
                g.fillRect (tab.getX() - 6, tab.getY() + 3, 2, tab.getHeight() - 6);
            }

            g.setColour (on ? colour::text : colour::textFaint);
            g.setFont (font (13.0f, on ? Weight::semibold : Weight::regular));
            g.drawText (names[i], tab, juce::Justification::centredLeft);
        }

        if (laneMode == LaneMode::pitch)
        {
            g.setColour (colour::textDim);
            g.setFont (font (12.0f));
            g.drawText ("±"_t + juce::String (juce::roundToInt (bendRange)) + " st",
                        juce::Rectangle<int> (8, v.getY() + 70, keyboardWidth - 12, 18), juce::Justification::centredLeft);
        }
    }

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (v);

    if (laneMode != LaneMode::velocity)
    {
        paintCurves (g, v);
        return;
    }

    const float usable = (float) v.getHeight() - 16.0f;

    auto stem = [&] (const Note& n, juce::Colour c)
    {
        const float x = tickToX (n.start);

        if (x < (float) v.getX() - 6.0f || x > (float) v.getRight() + 6.0f)
            return;

        const float h = usable * ((float) n.velocity / 127.0f);
        const float topY = (float) v.getBottom() - 6.0f - h;
        g.setColour (c);
        g.fillRect (x, topY, 2.0f, h);
        g.fillEllipse (x - 2.5f, topY - 3.5f, 7.0f, 7.0f);
    };

    for (auto& n : stepNotes) stem (n, colour::signal.withAlpha (0.7f));
    for (auto& n : notes) if (selected.count (n.id) == 0) stem (n, colour::signal);
    for (auto& n : notes) if (selected.count (n.id) != 0) stem (n, colour::text);
}

//==============================================================================
// A raia Pitch e Mod (26/10): a curva de cada nota, do começo ao fim dela. No Pitch, o meio da raia é o tom da nota
// e as bordas são o alcance do instrumento; no Mod, embaixo é 0 e em cima é a roda toda.
float PianoRollGrid::laneY (float value) const
{
    const auto v = velocityArea().toFloat().reduced (0.0f, 8.0f);

    if (laneMode == LaneMode::pitch)
        return v.getCentreY() - juce::jlimit (-1.0f, 1.0f, value / juce::jmax (1.0f, bendRange)) * v.getHeight() * 0.5f;

    return v.getBottom() - juce::jlimit (0.0f, 1.0f, value) * v.getHeight();
}

float PianoRollGrid::laneValue (float y, bool free) const
{
    const auto v = velocityArea().toFloat().reduced (0.0f, 8.0f);

    if (laneMode == LaneMode::pitch)
    {
        const float range = juce::jmax (1.0f, bendRange);
        const float st = juce::jlimit (-range, range, (v.getCentreY() - y) / (v.getHeight() * 0.5f) * range);
        return free ? st : std::round (st);   // em semitons inteiros; com Alt, livre
    }

    return juce::jlimit (0.0f, 1.0f, (v.getBottom() - y) / v.getHeight());
}

void PianoRollGrid::paintCurves (juce::Graphics& g, juce::Rectangle<int> lane)
{
    using namespace theme;
    const bool pitch = laneMode == LaneMode::pitch;
    const auto kind = pitch ? curves::Kind::pitch : curves::Kind::mod;
    const auto v = lane.toFloat();

    // as linhas de referência: o tom da nota (0) e meio alcance para cada lado; no Mod, metade da roda
    g.setColour (colour::line);

    if (pitch)
    {
        g.fillRect (v.getX(), laneY (bendRange * 0.5f), v.getWidth(), 1.0f);
        g.fillRect (v.getX(), laneY (-bendRange * 0.5f), v.getWidth(), 1.0f);
        g.setColour (colour::textFaint.withAlpha (0.5f));
        g.fillRect (v.getX(), laneY (0.0f), v.getWidth(), 1.0f);
    }
    else
    {
        g.fillRect (v.getX(), laneY (0.5f), v.getWidth(), 1.0f);
    }

    auto drawNote = [&] (const Note& n, juce::Colour c)
    {
        const float x0 = tickToX (n.start), x1 = tickToX (n.end());

        if (x1 < v.getX() || x0 > v.getRight())
            return;

        const auto& pts = curves::of (n, kind);

        // a faixa da nota, fraquinha: mostra onde dá para desenhar
        g.setColour (c.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (x0, v.getY() + 2.0f, juce::jmax (1.0f, x1 - x0 - 1.0f), v.getHeight() - 4.0f));   // 1 px entre notas coladas

        if (pts.empty())
        {
            g.setColour (c.withAlpha (0.35f));
            g.fillRect (x0, laneY (0.0f) - 0.5f, juce::jmax (1.0f, x1 - x0), 1.0f);
            return;
        }

        // a curva, um ponto a cada 2 px, só dentro da nota
        juce::Path path;
        const float from = juce::jmax (x0, v.getX() - 2.0f), to = juce::jmin (x1, v.getRight() + 2.0f);

        // (o Path::isEmpty continua true numa linha reta sem altura: o primeiro ponto vai por esta conta)
        for (float x = from;; x = juce::jmin (to, x + 2.0f))
        {
            const float y = laneY (curves::valueAt (pts, xToTick (x) - n.start));

            if (x == from)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);

            if (x >= to)
                break;
        }

        g.setColour (c);
        g.strokePath (path, juce::PathStrokeType (1.6f));

        for (auto& p : pts)
            if (p.t <= n.length)
                g.fillEllipse (tickToX (n.start + p.t) - 3.0f, laneY (p.v) - 3.0f, 6.0f, 6.0f);
    };

    for (auto& n : notes) if (selected.count (n.id) == 0) drawNote (n, colour::signal);
    for (auto& n : notes) if (selected.count (n.id) != 0) drawNote (n, colour::text);

    // um canal que não toca o pitch bend nem lê o CC1: a curva fica guardada e sai no MIDI
    if (! channelBends && ! notes.empty())
    {
        g.setColour (colour::textFaint);
        g.setFont (font (12.0f));
        g.drawText (pitch ? "Este instrumento ainda não toca o pitch bend; a curva sai no MIDI exportado."_t
                          : "Este instrumento ainda não usa o mod wheel; a curva sai no MIDI exportado."_t,
                    lane.reduced (8, 4), juce::Justification::topRight);
    }
}

void PianoRollGrid::clickLaneLabel (juce::Point<float> pos)
{
    const auto label = laneLabelArea();
    const int row = (int) std::floor ((pos.y - (float) label.getY() - 6.0f) / 20.0f);

    if (row >= 0 && row < 3)
    {
        setLaneMode ((LaneMode) row);
        return;
    }

    // o alcance, no Pitch: 2, 7, 12 e 24 semitons, em volta
    if (laneMode == LaneMode::pitch && row == 3)
    {
        const int steps[] = { 2, 7, 12, 24 };
        int next = steps[0];

        for (int s : steps)
            if (s > juce::roundToInt (bendRange))
            {
                next = s;
                break;
            }

        const auto result = app.run ("channel.setBendRange", args ({ { "channel", channel }, { "semitones", next } }));

        if (onStatus)
            onStatus (result.ok ? "Pitch bend: o instrumento dobra até "_t + juce::String (next) + " semitons."_t : result.error, ! result.ok);
    }
}

void PianoRollGrid::applyCurve (float fromX, float toX, float y, bool erase, bool free)
{
    const auto kind = laneMode == LaneMode::pitch ? curves::Kind::pitch : curves::Kind::mod;
    const double lo = xToTick (juce::jmin (fromX, toX)), hi = xToTick (juce::jmax (fromX, toX));
    const double slack = erase ? 5.0 / pxPerTick : 0.0;   // apagar pega 5 px em volta
    const float value = laneValue (y, free);
    const int at = juce::roundToInt (xToTick (toX) / (free ? 1.0 : curves::stepTicks)) * (free ? 1 : curves::stepTicks);
    std::vector<std::pair<int, std::vector<CurvePoint>>> changed;   // os comandos rodam depois: eles refazem `notes`

    for (auto& n : notes)
    {
        if (! selected.empty() && selected.count (n.id) == 0)
            continue;   // com seleção, só as selecionadas

        if (n.end() <= lo - slack || n.start > hi + slack)
            continue;

        auto pts = curves::of (n, kind);
        const double a = lo - n.start - slack, b = hi - n.start + slack;

        if (erase)
        {
            std::erase_if (pts, [a, b] (const CurvePoint& p) { return p.t >= a && p.t <= b; });

            // sem o ponto do começo, o primeiro valeria desde o começo e mudaria a nota inteira: ela volta a sair do tom
            if (! pts.empty() && pts.front().t > 0)
                pts.insert (pts.begin(), { 0, 0.0f });
        }
        else
        {
            const int t = juce::jlimit (0, n.length, at - n.start);

            // Desenhar numa nota sem curva sai do tom dela (ou da roda em 0) e segura até onde o arrasto começou:
            // clicar no fim da nota e puxar faz o bend só no fim, não uma rampa desde o começo.
            const bool fresh = pts.empty();

            // à mão livre: o que estava entre o ponto de antes e este sai
            std::erase_if (pts, [a, b, t] (const CurvePoint& p) { return (p.t > a && p.t < b) || p.t == t; });

            if (fresh && t > 0)
            {
                pts.push_back ({ 0, 0.0f });
                const int down = juce::jlimit (0, n.length, juce::roundToInt (xToTick (dragStart.x)) - n.start);

                if (down > 0 && down < t)
                    pts.push_back ({ down, 0.0f });
            }

            pts.push_back ({ t, value });
            curves::tidy (pts, kind);

            if (pts.size() > 200)   // um arrasto longo não passa do teto de pontos
                pts = curves::simplify (pts, kind == curves::Kind::pitch ? 0.02f : 0.005f);
        }

        curves::tidy (pts, kind);

        if (pts != curves::of (n, kind))
            changed.push_back ({ n.id, std::move (pts) });
    }

    for (auto& [id, pts] : changed)
    {
        const auto result = app.run ("pianoroll.setNoteCurve", args ({ { "channel", channel }, { "id", id },
                                                                       { "curve", kind == curves::Kind::pitch ? "pitch" : "mod" },
                                                                       { "points", curveVar (pts) } }));

        if (! result.ok && onStatus)
            onStatus (result.error, true);
    }
}

void PianoRollGrid::clearCurveAt (float x)
{
    const auto kind = laneMode == LaneMode::pitch ? curves::Kind::pitch : curves::Kind::mod;
    const double tick = xToTick (x);
    std::vector<int> ids;

    for (auto& n : notes)
        if ((selected.empty() || selected.count (n.id) != 0) && n.start <= tick && n.end() > tick && ! curves::of (n, kind).empty())
            ids.push_back (n.id);

    for (int id : ids)
        app.run ("pianoroll.setNoteCurve", args ({ { "channel", channel }, { "id", id },
                                                   { "curve", kind == curves::Kind::pitch ? "pitch" : "mod" },
                                                   { "points", juce::Array<juce::var>() } }));
}

//==============================================================================
void PianoRollGrid::preview (int key, int velocity, bool highlightKey)
{
    app.previewNote (channel, key, velocity);

    if (highlightKey)
    {
        previewKey = key;
        repaint (keyboardArea());
    }
}

void PianoRollGrid::beginEdit (const juce::String& name)
{
    if (! editing)
    {
        editing = true;
        app.getCommands().beginGesture (name);
    }
}

void PianoRollGrid::endEdit()
{
    if (editing)
    {
        editing = false;
        app.getCommands().endGesture();
    }
}

void PianoRollGrid::convertSteps()
{
    if (! stepNotes.empty())
        app.run ("pianoroll.stepsToNotes", args ({ { "channel", channel } }));
}

int PianoRollGrid::addNote (int start, int key)
{
    Note n;
    n.start = start;
    n.length = juce::jmax (1, lastLength);
    n.key = lockKey (key);
    n.velocity = lastVelocity;
    n.slide = slideMode;

    juce::Array<juce::var> list { noteVar (n, false) };
    const auto result = app.run ("pianoroll.addNotes", args ({ { "channel", channel }, { "notes", juce::var (list) } }));

    if (! result.ok)
    {
        if (onStatus) onStatus (result.error, true);
        return -1;
    }

    if (auto* ids = result.value["ids"].getArray(); ids != nullptr && ! ids->isEmpty())
        return (int) ids->getFirst();

    return -1;
}

std::vector<int> PianoRollGrid::addChord (int start, int key)
{
    std::vector<Note> list;

    for (int k : notetools::chordKeys (chordStamp, lockKey (key), keyRoot, degrees))
    {
        Note n;
        n.start = start;
        n.length = juce::jmax (1, lastLength);
        n.key = k;
        n.velocity = lastVelocity;
        list.push_back (n);
    }

    return addNoteList (list);
}

void PianoRollGrid::runTransform (const juce::String& op, double amount)
{
    if (! channelOk)
        return;

    static const std::map<juce::String, juce::String> titles {
        { "legato", "Legato" }, { "glue", "Juntar notas" }, { "mute", "Mudo nas notas" }, { "unmute", "Tirar o mudo" },
        { "humanize", "Humanizar" }, { "toScale", "Levar para a escala" }, { "strum", "Strum" } };

    const auto title = titles.count (op) != 0 ? titles.at (op) : op;
    auto a = args ({ { "channel", channel }, { "op", op }, { "amount", amount } });

    if (! selected.empty())
    {
        juce::Array<juce::var> ids;

        for (int id : selected)
            ids.add (id);

        a.getDynamicObject()->setProperty ("ids", ids);
    }

    CommandRegistry::ScopedGesture gesture (app.getCommands(), title);

    if (selected.empty())
        convertSteps();   // sem seleção vale para todas, inclusive os passos do channel rack

    const auto result = app.run ("pianoroll.transform", a);

    if (! result.ok)
    {
        if (onStatus) onStatus (result.error, true);
        return;
    }

    const int changed = (int) result.value["changed"];
    const auto count = [changed] (const char* one, const char* many)
    {
        return juce::String (changed) + juce::String::fromUTF8 (changed == 1 ? one : many);
    };

    juce::String message;

    if (changed == 0)
        message = op == "toScale" ? "As notas já estão na escala."_t
                : op == "legato"  ? "Nada a mudar: as notas já vão até a próxima."_t
                : op == "glue"    ? "Nada a juntar: só notas da mesma tecla que se encostam viram uma."_t
                                  : "Nada a mudar."_t;
    else if (op == "legato")   message = count (" nota vai até a próxima (legato)", " notas vão até a próxima (legato)");
    else if (op == "glue")     message = count (" nota juntada na vizinha", " notas juntadas nas vizinhas");
    else if (op == "mute")     message = count (" nota muda. Alt+Shift+M tira o mudo.", " notas mudas. Alt+Shift+M tira o mudo.");
    else if (op == "unmute")   message = count (" nota voltou a tocar", " notas voltaram a tocar");
    else if (op == "humanize") message = count (" nota com tempo e força um pouco diferentes", " notas com tempo e força um pouco diferentes");
    else if (op == "toScale")  message = count (" nota levada para a escala", " notas levadas para a escala");
    else if (op == "strum")    message = count (" nota dedilhada (strum)", " notas dedilhadas (strum)");

    if (onStatus)
        onStatus (message, false);

    repaint();
}

void PianoRollGrid::setChordStamp (const juce::String& id)
{
    chordStamp = id;
    repaint();
}

void PianoRollGrid::setScaleLock (bool on)
{
    scaleLock = on;
    repaint();
}

void PianoRollGrid::importMidi (const juce::File& file, int track, bool replace, int at)
{
    if (! channelOk)
    {
        if (onStatus) onStatus ("Escolha um canal de instrumento para receber as notas do MIDI."_t, true);
        return;
    }

    const auto result = app.run ("pianoroll.importMidi", args ({ { "channel", channel }, { "path", file.getFullPathName() },
                                                                 { "track", track }, { "replace", replace }, { "at", at } }));

    if (! result.ok)
    {
        if (onStatus) onStatus (result.error, true);
        return;
    }

    const int count = (int) result.value["notes"];
    const int dropped = (int) result.value["dropped"];
    const double fileBpm = (double) result.value["fileBpm"];
    const double bpm = app.getModel().get().bpm;
    const auto trackName = result.value["track"].toString();
    juce::String message = juce::String (count) + (count == 1 ? " nota de "_t : " notas de "_t) + file.getFileName()
                           + (trackName.isNotEmpty() ? " (" + trackName + ")" : juce::String()) + " em "_t + channelName + ".";

    if (fileBpm > 0.0 && std::abs (fileBpm - bpm) >= 0.5)
        message << " O arquivo foi feito a "_t << juce::String (fileBpm, fileBpm == std::round (fileBpm) ? 0 : 1)
                << " BPM; aqui toca no BPM do projeto ("_t << juce::String (bpm, bpm == std::round (bpm) ? 0 : 1) << ").";

    if (dropped > 0)
        message << " " << juce::String (dropped) << (dropped == 1 ? " nota depois de 16 compassos ficou de fora."_t
                                                                  : " notas depois de 16 compassos ficaram de fora."_t);

    selected.clear();

    if (onStatus)
        onStatus (message, false);

    refresh (app.getModel().get(), channel);
    revealNotes();
}

void PianoRollGrid::updateNotes (const std::vector<Note>& changed)
{
    if (changed.empty())
        return;

    juce::Array<juce::var> list;

    for (auto& n : changed)
        list.add (noteVar (n, true));

    const auto result = app.run ("pianoroll.updateNotes", args ({ { "channel", channel }, { "notes", juce::var (list) } }));

    if (! result.ok && onStatus)
        onStatus (result.error, true);
}

void PianoRollGrid::removeNotes (const std::vector<int>& ids)
{
    if (ids.empty())
        return;

    juce::Array<juce::var> list;

    for (int id : ids)
    {
        list.add (id);
        selected.erase (id);
    }

    app.run ("pianoroll.removeNotes", args ({ { "channel", channel }, { "ids", juce::var (list) } }));
}

void PianoRollGrid::eraseAt (juce::Point<float> pos)
{
    bool edge = false;

    if (auto* n = noteAt (pos, edge))
    {
        const int id = n->id;
        removeNotes ({ id });
    }
}

void PianoRollGrid::paintAt (juce::Point<float> pos)
{
    // Pincel: uma nota a cada "duração da última nota", na altura em que o mouse está.
    const int length = juce::jmax (1, lastLength);
    const int slot = (int) std::floor ((xToTick (pos.x) - paintOrigin) / length);
    const int start = paintOrigin + slot * length;
    const int key = lockKey (yToKey (pos.y));

    if (start < 0)
        return;

    for (auto& n : notes)
        if (n.key == key && n.start < start + length && n.end() > start)
            return;

    if (addNote (start, key) >= 0)
        preview (key, lastVelocity);
}

void PianoRollGrid::applyVelocity (float fromX, float toX, float y)
{
    const auto v = velocityArea().toFloat();
    const float usable = v.getHeight() - 16.0f;
    const int vel = juce::jlimit (1, 127, juce::roundToInt ((v.getBottom() - 6.0f - y) / usable * 127.0f));
    const float lo = juce::jmin (fromX, toX) - 5.0f, hi = juce::jmax (fromX, toX) + 5.0f;
    std::vector<Note> changed;

    for (auto& n : notes)
    {
        if (! selected.empty() && selected.count (n.id) == 0)
            continue;   // com seleção, só as selecionadas

        const float x = tickToX (n.start);

        if (x >= lo && x <= hi && n.velocity != vel)
        {
            auto c = n;
            c.velocity = vel;
            changed.push_back (c);
        }
    }

    lastVelocity = vel;
    updateNotes (changed);
}

void PianoRollGrid::nudgeSelection (int deltaTicks, int deltaKeys)
{
    std::vector<Note> moved;

    for (int id : selected)
    {
        if (auto* n = findNote (id))
        {
            auto c = *n;
            c.start = juce::jmax (0, c.start + deltaTicks);
            // um passo da seta: um semitom, ou a próxima nota da escala com o encaixe ligado; a oitava fica igual
            c.key = std::abs (deltaKeys) == 1 ? stepKey (c.key, deltaKeys) : juce::jlimit (0, 127, c.key + deltaKeys);
            moved.push_back (c);
        }
    }

    updateNotes (moved);

    if (deltaKeys != 0 && ! moved.empty())
        preview (moved.front().key, moved.front().velocity);
}

void PianoRollGrid::toggleSlideOnSelection()
{
    if (selected.empty())
    {
        slideMode = ! slideMode;
        if (onSlideChanged) onSlideChanged();
        return;
    }

    bool allSlide = true;

    for (int id : selected)
        if (auto* n = findNote (id))
            allSlide = allSlide && n->slide;

    std::vector<Note> changed;

    for (int id : selected)
    {
        if (auto* n = findNote (id))
        {
            auto c = *n;
            c.slide = ! allSlide;
            changed.push_back (c);
        }
    }

    CommandRegistry::ScopedGesture gesture (app.getCommands(), "Slide");

    if (! allSlide && ! channelMono)
    {
        app.run ("channel.setMono", args ({ { "channel", channel }, { "on", true } }));
        if (onStatus) onStatus ("Mono ligado em "_t + channelName + ": o slide precisa de uma nota por vez."_t, false);
    }

    updateNotes (changed);
}

//==============================================================================
std::vector<Note> PianoRollGrid::selectedNotes() const
{
    std::vector<Note> out;

    for (auto& n : notes)
        if (selected.count (n.id) != 0)
            out.push_back (n);

    return out;
}

std::vector<int> PianoRollGrid::addNoteList (const std::vector<Note>& list)
{
    std::vector<int> ids;

    if (list.empty())
        return ids;

    juce::Array<juce::var> vars;

    for (auto& n : list)
        vars.add (noteVar (n, false));

    const auto result = app.run ("pianoroll.addNotes", args ({ { "channel", channel }, { "notes", juce::var (vars) } }));

    if (! result.ok && onStatus)
        onStatus (result.error, true);

    if (auto* added = result.value["ids"].getArray())
        for (auto& id : *added)
            ids.push_back ((int) id);

    return ids;
}

void PianoRollGrid::copySelection()
{
    auto list = selectedNotes();

    if (list.empty())
        return;

    int first = std::numeric_limits<int>::max();

    for (auto& n : list)
        first = juce::jmin (first, n.start);

    for (auto& n : list)
        n.start -= first;

    clipboard = list;

    if (onStatus)
        onStatus (juce::String ((int) list.size()) + (list.size() == 1 ? " nota copiada" : " notas copiadas"), false);
}

void PianoRollGrid::pasteNotes()
{
    if (clipboard.empty())
        return;

    // cola onde o mouse está (as notas ficam no tom delas); sem o mouse na grade, no começo da tela
    const int at = gridArea().toFloat().contains (hoverPos) ? snapDown (xToTick (hoverPos.x)) : snapDown (scrollTicks);
    auto list = clipboard;

    for (auto& n : list)
        n.start += at;

    beginEdit ("Colar notas");
    convertSteps();
    const auto ids = addNoteList (list);
    endEdit();
    selected = std::set<int> (ids.begin(), ids.end());
    repaint();
}

void PianoRollGrid::duplicateSelection()
{
    // Ctrl+B, como no FL: a seleção (sem seleção, tudo) se repete logo à direita, arredondada no tempo
    convertSteps();
    auto list = selected.empty() ? notes : selectedNotes();

    if (list.empty())
        return;

    int first = std::numeric_limits<int>::max(), last = 0;

    for (auto& n : list)
    {
        first = juce::jmin (first, n.start);
        last = juce::jmax (last, n.end());
    }

    const int beat = stepsPerBeat * ticksPerStep;
    const int span = juce::jmax (beat, (last - first + beat - 1) / beat * beat);

    for (auto& n : list)
        n.start += span;

    beginEdit ("Duplicar notas");
    const auto ids = addNoteList (list);
    endEdit();
    selected = std::set<int> (ids.begin(), ids.end());
    repaint();
}

void PianoRollGrid::quantizeSelection()
{
    // Alt+Q: os inícios vão para a grade de agora (sem seleção, todas as notas); a duração fica
    convertSteps();
    const int grid = juce::jmax (ticksPerStep / 16, getSnapTicks() > 1 ? getSnapTicks() : ticksPerStep);
    std::vector<Note> changed;

    for (auto n : (selected.empty() ? notes : selectedNotes()))
    {
        const int q = juce::jmax (0, juce::roundToInt ((double) n.start / grid) * grid);

        if (q != n.start)
        {
            n.start = q;
            changed.push_back (n);
        }
    }

    if (changed.empty())
        return;

    beginEdit ("Quantizar notas");
    updateNotes (changed);
    endEdit();

    if (onStatus)
        onStatus (juce::String ((int) changed.size()) + (changed.size() == 1 ? " nota quantizada na grade de "_t : " notas quantizadas na grade de "_t)
                      + timegrid::name (grid, meter()), false);
}

void PianoRollGrid::sliceNoteAt (juce::Point<float> pos, bool free)
{
    bool edge = false;
    const auto* hit = noteAt (pos, edge);

    if (hit == nullptr)
        return;

    const auto n = *hit;
    const int at = free ? juce::roundToInt (xToTick (pos.x)) : snapNearest (xToTick (pos.x));

    if (at <= n.start || at >= n.end())
        return;

    auto left = n;
    left.length = at - n.start;
    auto right = n;
    right.start = at;
    right.length = n.end() - at;
    right.slide = false;
    right.pitch = curves::from (n.pitch, at - n.start);   // o pedaço da direita segue a curva de onde foi cortado (26/10)
    right.mod = curves::from (n.mod, at - n.start);

    updateNotes ({ left });
    const auto ids = addNoteList ({ right });
    selected = { n.id };

    for (int id : ids)
        selected.insert (id);
}

juce::Rectangle<float> PianoRollGrid::stretchHandle() const
{
    if (selected.size() < 1)
        return {};

    int last = -1;

    for (auto& n : notes)
        if (selected.count (n.id) != 0)
            last = juce::jmax (last, n.end());

    if (last < 0)
        return {};

    const auto r = rulerArea().toFloat();
    return { tickToX (last) - 6.0f, r.getBottom() - 13.0f, 12.0f, 12.0f };
}

void PianoRollGrid::mouseMove (const juce::MouseEvent& e)
{
    const auto oldHover = hoverPos;
    hoverPos = e.position;
    auto newCursor = juce::MouseCursor::NormalCursor;

    if (channelOk && gridArea().toFloat().contains (e.position))
    {
        bool edge = false;

        if (noteAt (e.position, edge) != nullptr)
            newCursor = edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
        else if (tool != Tool::select)
            newCursor = juce::MouseCursor::CrosshairCursor;
    }
    else if (keyboardArea().toFloat().contains (e.position))
    {
        newCursor = juce::MouseCursor::PointingHandCursor;
    }
    else if (velocityArea().toFloat().contains (e.position))
    {
        newCursor = laneMode == LaneMode::velocity ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor;
    }
    else if (laneLabelArea().toFloat().contains (e.position))
    {
        newCursor = juce::MouseCursor::PointingHandCursor;
    }

    setMouseCursor (newCursor);

    const auto kb = keyboardArea().toFloat();

    if (kb.contains (oldHover) || kb.contains (hoverPos))
        repaint (keyboardArea());
}

void PianoRollGrid::mouseExit (const juce::MouseEvent&)
{
    hoverPos = { -1.0f, -1.0f };
    repaint (keyboardArea());
}

void PianoRollGrid::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    const auto pos = e.position;
    dragStart = lastDragPos = pos;
    lastDeltaTicks = lastDeltaKeys = lastLengthDelta = 0;
    drag = Drag::none;

    if (! channelOk)
        return;

    if (keyboardArea().toFloat().contains (pos))
    {
        drag = Drag::keys;
        preview (yToKey (pos.y), 100, true);
        return;
    }

    if (laneLabelArea().toFloat().contains (pos))
    {
        clickLaneLabel (pos);   // as abas Força, Pitch e Mod e o alcance do pitch bend (26/10)
        return;
    }

    if (velocityArea().toFloat().contains (pos) && laneMode != LaneMode::velocity)
    {
        // a raia Pitch ou Mod (26/10): desenhar com o botão esquerdo, apagar pontos com o direito; duplo clique tira a
        // curva da nota
        beginEdit (laneMode == LaneMode::pitch ? "Pitch bend"_t : "Mod wheel"_t);
        convertSteps();

        if (e.getNumberOfClicks() >= 2 && ! e.mods.isPopupMenu())
        {
            clearCurveAt (pos.x);
            return;
        }

        drag = e.mods.isPopupMenu() ? Drag::curveErase : Drag::curve;
        applyCurve (pos.x, pos.x, pos.y, drag == Drag::curveErase, e.mods.isAltDown());
        return;
    }

    if (velocityArea().toFloat().contains (pos))
    {
        beginEdit ("Força das notas"_t);
        convertSteps();
        drag = Drag::velocity;
        applyVelocity (pos.x, pos.x, pos.y);
        return;
    }

    // a alça de esticar a seleção, na régua (como no FL): arrastar muda os tempos e as durações juntos
    if (const auto handle = stretchHandle(); ! handle.isEmpty() && handle.expanded (3.0f).contains (pos))
    {
        beginEdit ("Esticar notas");
        originals = selectedNotes();
        stretchFrom = std::numeric_limits<int>::max();
        stretchEnd = 0;

        for (auto& n : originals)
        {
            stretchFrom = juce::jmin (stretchFrom, n.start);
            stretchEnd = juce::jmax (stretchEnd, n.end());
        }

        drag = Drag::stretch;
        return;
    }

    // o clique na régua (29/10): de onde o padrão começa a tocar no PAT, como a agulha na régua da playlist (Alt: sem a grade)
    if (rulerArea().toFloat().contains (pos) && ! e.mods.isPopupMenu())
    {
        if (app.getModel().get().songMode)
        {
            if (onStatus) onStatus ("No SONG o play começa da agulha da playlist. No PAT, o clique na régua escolhe de onde o padrão toca."_t, false);
            return;
        }

        const double t = juce::jmax (0.0, xToTick (pos.x));
        const int snap = getSnapTicks();
        const int at = e.mods.isAltDown() || snap <= 1 ? juce::roundToInt (t) : juce::roundToInt (t / snap) * snap;
        const auto r = app.run ("transport.setPatternStart", args ({ { "at", at } }));
        if (! r.ok && onStatus) onStatus (r.error, true);
        return;
    }

    if (! gridArea().toFloat().contains (pos))
        return;

    beginEdit ("Editar notas");
    convertSteps();

    if (tool == Tool::slice && ! e.mods.isPopupMenu())
    {
        sliceNoteAt (pos, e.mods.isAltDown());
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        drag = Drag::erase;
        eraseAt (pos);
        return;
    }

    bool onEdge = false;
    std::optional<Note> hit;

    if (auto* n = noteAt (pos, onEdge))
        hit = *n;

    if (hit && e.mods.isCtrlDown())   // Ctrl+clique numa nota: põe ou tira da seleção
    {
        if (selected.count (hit->id) != 0) selected.erase (hit->id);
        else                               selected.insert (hit->id);
        repaint();
        return;
    }

    if (! hit && (tool == Tool::select || e.mods.isCtrlDown()))
    {
        drag = Drag::select;
        selectionBefore = e.mods.isShiftDown() ? selected : std::set<int>();
        selected = selectionBefore;
        rubberBand = {};
        repaint();
        return;
    }

    if (! hit && tool == Tool::paint)
    {
        drag = Drag::paint;
        selected.clear();
        paintOrigin = snapDown (xToTick (pos.x));
        paintAt (pos);
        return;
    }

    if (! hit && chordStamp.isNotEmpty())
    {
        // carimbo: o acorde inteiro, com o baixo na nota clicada; o mesmo arrasto move o acorde
        const auto ids = addChord (snapDown (xToTick (pos.x)), yToKey (pos.y));
        selected = std::set<int> (ids.begin(), ids.end());
        originals.clear();

        for (int id : ids)
            if (auto* n = findNote (id))
            {
                originals.push_back (*n);
                preview (n->key, n->velocity);
            }

        if (! ids.empty())
        {
            anchorId = ids.front();
            drag = Drag::move;
        }

        repaint();
        return;
    }

    if (! hit)
    {
        // desenhar: nota nova na grade, e o mesmo arrasto já move ela
        const int id = addNote (snapDown (xToTick (pos.x)), yToKey (pos.y));

        if (auto* n = findNote (id))
        {
            selected = { id };
            originals = { *n };
            anchorId = id;
            drag = Drag::move;
            preview (n->key, n->velocity);
        }

        repaint();
        return;
    }

    // clicou numa nota: ela vira a referência de duração e força das próximas
    lastLength = hit->length;
    lastVelocity = hit->velocity;
    anchorId = hit->id;

    if (selected.count (hit->id) == 0)
        selected = { hit->id };

    if (e.mods.isShiftDown() && ! onEdge)
    {
        // Shift+arrasto: arrasta uma cópia
        std::vector<int> sources (selected.begin(), selected.end());
        juce::Array<juce::var> list;

        for (int id : sources)
            if (auto* n = findNote (id))
                list.add (noteVar (*n, false));

        const auto result = app.run ("pianoroll.addNotes", args ({ { "channel", channel }, { "notes", juce::var (list) } }));

        if (auto* ids = result.value["ids"].getArray(); result.ok && ids != nullptr && ids->size() == (int) sources.size())
        {
            selected.clear();

            for (int i = 0; i < ids->size(); ++i)
            {
                selected.insert ((int) (*ids)[i]);

                if (sources[(size_t) i] == hit->id)
                    anchorId = (int) (*ids)[i];
            }
        }
    }

    originals.clear();

    for (int id : selected)
        if (auto* n = findNote (id))
            originals.push_back (*n);

    drag = onEdge ? Drag::resize : Drag::move;
    preview (hit->key, hit->velocity);
    repaint();
}

void PianoRollGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::stretch)
    {
        const int span = juce::jmax (1, stretchEnd - stretchFrom);
        const int end = e.mods.isAltDown() ? juce::roundToInt (xToTick (e.position.x)) : snapNearest (xToTick (e.position.x));
        const int newSpan = juce::jmax (ticksPerStep / 16, end - stretchFrom);

        if (newSpan == lastLengthDelta)
            return;

        lastLengthDelta = newSpan;
        const double k = (double) newSpan / span;
        std::vector<Note> stretched;

        for (auto o : originals)
        {
            o.start = stretchFrom + juce::roundToInt ((o.start - stretchFrom) * k);
            o.length = juce::jmax (1, juce::roundToInt (o.length * k));

            for (auto* pts : { &o.pitch, &o.mod })   // a curva estica junto (26/10): o dive do fim continua no fim
                for (auto& p : *pts)
                    p.t = juce::roundToInt (p.t * k);

            stretched.push_back (o);
        }

        updateNotes (stretched);
        repaint();
        return;
    }

    const auto pos = e.position;

    auto anchor = [this]() -> const Note*
    {
        for (auto& o : originals)
            if (o.id == anchorId)
                return &o;

        return nullptr;
    };

    switch (drag)
    {
        case Drag::keys:
        {
            const int key = yToKey (pos.y);
            if (key != previewKey)
                preview (key, 100, true);
            break;
        }

        case Drag::velocity:   applyVelocity (lastDragPos.x, pos.x, pos.y); break;
        case Drag::curve:      applyCurve (lastDragPos.x, pos.x, pos.y, false, e.mods.isAltDown()); break;
        case Drag::curveErase: applyCurve (lastDragPos.x, pos.x, pos.y, true, false); break;
        case Drag::erase:      eraseAt (pos); break;
        case Drag::paint:      paintAt (pos); break;

        case Drag::select:
        {
            rubberBand = juce::Rectangle<float> (dragStart, pos);
            selected = selectionBefore;

            for (auto& n : notes)
                if (noteBounds (n).intersects (rubberBand))
                    selected.insert (n.id);

            repaint();
            break;
        }

        case Drag::move:
        {
            const auto* a = anchor();
            if (a == nullptr) break;

            const double raw = xToTick (pos.x) - xToTick (dragStart.x);
            const int target = e.mods.isAltDown() ? juce::roundToInt (a->start + raw) : snapNearest (a->start + raw);
            int dt = target - a->start;
            int dk = yToKey (pos.y) - yToKey (dragStart.y);

            for (auto& o : originals)   // nenhuma nota sai da grade
            {
                dt = juce::jmax (dt, -o.start);
                dk = juce::jlimit (-o.key, 127 - o.key, dk);
            }

            if (dt == lastDeltaTicks && dk == lastDeltaKeys)
                break;

            const bool keyChanged = dk != lastDeltaKeys;
            lastDeltaTicks = dt;
            lastDeltaKeys = dk;

            std::vector<Note> moved;

            for (auto o : originals)
            {
                o.start += dt;
                o.key = lockKey (o.key + dk);   // com o encaixe ligado, cada nota cai na escala
                moved.push_back (o);
            }

            updateNotes (moved);

            if (keyChanged)
                preview (lockKey (a->key + dk), a->velocity);
            break;
        }

        case Drag::resize:
        {
            const auto* a = anchor();
            if (a == nullptr) break;

            const double raw = xToTick (pos.x) - xToTick (dragStart.x);
            const int end = e.mods.isAltDown() ? juce::roundToInt (a->end() + raw) : snapNearest (a->end() + raw);
            const int snap = getSnapTicks();
            const int minLength = snap > 1 ? juce::jmin (snap, ticksPerStep) : 20;
            const int newLength = juce::jmax (minLength, end - a->start);
            const int dl = newLength - a->length;

            if (dl == lastLengthDelta)
                break;

            lastLengthDelta = dl;
            std::vector<Note> resizedNotes;

            for (auto o : originals)
            {
                o.length = juce::jmax (minLength, o.length + dl);
                resizedNotes.push_back (o);
            }

            updateNotes (resizedNotes);
            lastLength = newLength;
            break;
        }

        case Drag::none:
            break;
    }

    lastDragPos = pos;
}

void PianoRollGrid::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::select)
        rubberBand = {};

    drag = Drag::none;
    originals.clear();
    previewKey = -1;
    endEdit();
    repaint();
}

void PianoRollGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // No Windows cada clique da rodinha vale ~0,14.
    const float delta = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
    const float notches = delta / 0.14f;

    if (e.mods.isCtrlDown() || rulerArea().contains (e.getPosition()))
    {
        // Ctrl+rodinha, ou a rodinha em cima da régua: zoom, e de perto a grade fica mais fina
        zoomBy (notches, e.position.x);
        return;
    }

    if (e.mods.isAltDown())
    {
        const double anchorRow = (e.position.y - (float) rulerHeight) / rowHeight + scrollRows;
        rowHeight = juce::jlimit (8.0f, 32.0f, rowHeight + notches);
        scrollRows = anchorRow - (e.position.y - (float) rulerHeight) / rowHeight;
    }
    else if (e.mods.isShiftDown() || w.deltaX != 0.0f)
    {
        scrollTicks -= notches * 60.0f / pxPerTick;
    }
    else
    {
        scrollRows -= notches * 3.0f;
    }

    clampScroll();
    updateScrollBars();
    repaint();
}

bool PianoRollGrid::keyPressed (const juce::KeyPress& key)
{
    if (! channelOk)
        return false;

    const int code = key.getKeyCode();
    const auto mods = key.getModifiers();

    if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && ! selected.empty())
    {
        removeNotes (std::vector<int> (selected.begin(), selected.end()));
        return true;
    }

    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        convertSteps();
        selected.clear();

        for (auto& n : notes)
            selected.insert (n.id);

        repaint();
        return true;
    }

    if (code == juce::KeyPress::escapeKey && ! selected.empty())
    {
        selected.clear();
        repaint();
        return true;
    }

    if (code == juce::KeyPress::escapeKey && chordStamp.isNotEmpty())   // Esc sem seleção desliga o carimbo
    {
        setChordStamp ({});
        if (onModesChanged) onModesChanged();
        if (onStatus) onStatus ("Carimbo de acorde desligado: o clique volta a pôr uma nota."_t, false);
        return true;
    }

    // as ferramentas do FL (bloco 1, 22/10): Ctrl+L legato, Ctrl+G juntar, Alt+M mudo, Alt+Shift+M tira o mudo
    if (key == juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0))   { runTransform ("legato"); return true; }
    if (key == juce::KeyPress ('g', juce::ModifierKeys::commandModifier, 0))   { runTransform ("glue"); return true; }
    if (key == juce::KeyPress ('m', juce::ModifierKeys::altModifier, 0))       { runTransform ("mute"); return true; }
    if (key == juce::KeyPress ('m', juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        runTransform ("unmute");
        return true;
    }

    // como no FL: Ctrl+C/X/V, Ctrl+B duplica à direita, Alt+Q quantiza na grade
    const auto ctrl = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('c', ctrl, 0))   { copySelection(); return true; }
    if (key == juce::KeyPress ('x', ctrl, 0))   { copySelection(); removeNotes (std::vector<int> (selected.begin(), selected.end())); return true; }
    if (key == juce::KeyPress ('v', ctrl, 0))   { pasteNotes(); return true; }
    if (key == juce::KeyPress ('b', ctrl, 0))   { duplicateSelection(); return true; }
    if (key == juce::KeyPress ('q', juce::ModifierKeys::altModifier, 0))   { quantizeSelection(); return true; }

    // setas: um semitom (ou a próxima nota da escala) e um passo; Ctrl+cima/baixo é a oitava, como no FL
    // (vídeo do produtor, 22/10), e o Shift+cima/baixo de antes continua
    if (! selected.empty() && ! mods.isAltDown())
    {
        const int snap = getSnapTicks();
        const int stepTicks = snap > 1 ? snap : ticksPerStep;
        const bool octave = mods.isShiftDown() || mods.isCommandDown();

        if (code == juce::KeyPress::upKey)     { nudgeSelection (0, octave ? 12 : 1); return true; }
        if (code == juce::KeyPress::downKey)   { nudgeSelection (0, octave ? -12 : -1); return true; }

        if (! mods.isCommandDown())
        {
            if (code == juce::KeyPress::leftKey)   { nudgeSelection (-stepTicks, 0); return true; }
            if (code == juce::KeyPress::rightKey)  { nudgeSelection (stepTicks, 0); return true; }
        }
    }

    if (! singleKeyShortcuts)
        return false;   // teclado-piano ligado: a letra vira nota (no MainComponent)

    if (key == juce::KeyPress ('s'))   { toggleSlideOnSelection(); return true; }
    if (key == juce::KeyPress ('p'))   { setTool (Tool::draw);   if (onToolChanged) onToolChanged(); return true; }
    if (key == juce::KeyPress ('b'))   { setTool (Tool::paint);  if (onToolChanged) onToolChanged(); return true; }
    if (key == juce::KeyPress ('e'))   { setTool (Tool::select); if (onToolChanged) onToolChanged(); return true; }
    if (key == juce::KeyPress ('c'))   { setTool (Tool::slice);  if (onToolChanged) onToolChanged(); return true; }

    return false;
}

juce::String PianoRollGrid::getTooltip()
{
    if (! channelOk)
        return {};

    if (keyboardArea().toFloat().contains (hoverPos))
        return "Clique para ouvir " + noteNamePt (yToKey (hoverPos.y)) + " (" + Project::keyName (yToKey (hoverPos.y)) + ")";

    if (laneLabelArea().toFloat().contains (hoverPos))
        return laneMode == LaneMode::pitch
                   ? "Força, Pitch (pitch bend) ou Mod (mod wheel): o que a raia desenha. Clique no alcance para dobrar mais (2, 7, 12 ou 24 semitons)"_t
                   : "Força, Pitch (pitch bend) ou Mod (mod wheel): o que a raia desenha"_t;

    if (velocityArea().toFloat().contains (hoverPos) && laneMode == LaneMode::pitch)
        return "Desenhe o pitch bend da nota (em semitons; com Alt, livre). Botão direito apaga pontos, duplo clique tira a curva. "
               "Com seleção, só nas selecionadas"_t;

    if (velocityArea().toFloat().contains (hoverPos) && laneMode == LaneMode::mod)
        return "Desenhe o mod wheel (CC1) da nota. Botão direito apaga pontos, duplo clique tira a curva. Com seleção, só nas selecionadas"_t;

    if (velocityArea().toFloat().contains (hoverPos))
        return "Arraste para mudar a força das notas (com seleção, só das selecionadas)"_t;

    const auto* hint = areBeatHintsVisible() ? beatHintAt (xToTick (hoverPos.x)) : nullptr;

    if (rulerArea().toFloat().contains (hoverPos))
        return (hint != nullptr ? beatHintTooltip (*hint, beatHintFlats) + "\n" : juce::String())
               + "Clique: de onde o padrão começa a tocar no PAT (Alt+← e Alt+→ andam um tempo; Alt no clique, sem a grade). "_t
               + "Rodinha aqui (ou Ctrl+rodinha na grade): zoom. Na grade Auto, de perto ela fica mais fina."_t;

    bool edge = false;

    if (auto* n = noteAt (hoverPos, edge))
        return noteNamePt (n->key) + " (" + Project::keyName (n->key) + ")" + juce::String (", força "_t) + juce::String (n->velocity)
               + (n->slide ? juce::String (", com slide") : juce::String())
               + (n->muted ? juce::String (", muda (Alt+Shift+M tira o mudo)") : juce::String())
               + (edge ? juce::String (". Arraste para mudar a duração."_t) : juce::String());

    if (hint != nullptr && gridArea().toFloat().contains (hoverPos))
        return beatHintTooltip (*hint, beatHintFlats);

    return {};
}

//==============================================================================
PianoRoll::PianoRoll (AppController& a) : app (a), grid (a), ideas (a)
{
    theme::styleComboBox (channelBox, Icon::pianoRoll);
    channelBox.setTooltip ("Canal que o piano roll está editando"_t);
    channelBox.onChange = [this]
    {
        if (refreshing) return;
        const int index = channelBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (index, channelIds.size()) && onChannelPicked)
            onChannelPicked (channelIds[index]);
    };

    toolSwitch.setItems ({
        { "Desenhar", Icon::pencil, "Desenhar: clique cria a nota e o arrasto move; a borda direita muda a duração. Botão direito apaga. (P)"_t },
        { "Pintar",   Icon::brush,  "Pintar: arraste para encher de notas com a duração da última (B)"_t },
        { "Selecionar", Icon::select, "Selecionar: arraste uma caixa. Delete apaga, setas transpõem, Shift+setas uma oitava. "
                                      "Ctrl+C/V copia e cola, Ctrl+B duplica, Alt+Q quantiza, a alça na régua estica. (E)"_t },
        { "Cortar", Icon::scissors, "Cortar: clique numa nota para dividir ali; Alt corta fora da grade. (C)"_t },
    });

    toolSwitch.onChange = [this] (int index)
    {
        grid.setTool (index == 1 ? PianoRollGrid::Tool::paint : index == 2 ? PianoRollGrid::Tool::select
                    : index == 3 ? PianoRollGrid::Tool::slice : PianoRollGrid::Tool::draw);
    };

    for (int i = 0; i < (int) std::size (snapOptions); ++i)
    {
        if (i == 1 || i == noGridOption)
            snapBox.addSeparator();

        if (i == firstTripletOption)
            snapBox.addSectionHeading ("Tercinas");

        snapBox.addItem (juce::String::fromUTF8 (snapOptions[i].name), i + 1);
    }

    snapBox.setSelectedId (1, juce::dontSendNotification);
    theme::styleComboBox (snapBox, Icon::magnet);
    snapBox.setTooltip ("Grade: onde as notas encaixam. Na Auto, ela segue o zoom (Ctrl+rodinha, ou a rodinha em cima da régua): "
                        "de perto fica mais fina, até 1/16 de passo. Segure Alt enquanto arrasta para ignorar."_t);
    snapBox.onChange = [this]
    {
        const int index = juce::jlimit (0, (int) std::size (snapOptions) - 1, snapBox.getSelectedId() - 1);
        grid.setSnap (snapOptions[(size_t) index].ticks);
        updateSnapLabel();
    };

    slideButton.setClickingTogglesState (true);
    slideButton.setTooltip ("Slide (S): notas novas deslizam da nota anterior, como no 808. Com notas selecionadas, S liga ou desliga o slide delas."_t);
    slideButton.onClick = [this] { setSlideMode (slideButton.getToggleState()); };

    monoButton.setTooltip ("Mono: uma nota por vez. Precisa estar ligado para o slide deslizar."_t);
    monoButton.onClick = [this] { app.run ("channel.setMono", args ({ { "channel", channel } })); };

    glideButton.setTooltip ("Tempo do glide: quanto a nota leva para deslizar"_t);
    glideButton.setShowsMenuArrow (true);
    glideButton.onClick = [this] { showGlideMenu(); };

    // groove (29/10): swing e humanizar das notas só na hora de tocar; as notas continuam na grade
    grooveButton.setTooltip ("Groove: swing (as notas do contratempo atrasam) e humanizar (tempo e força um pouco ao acaso), só "
                             "na hora de tocar. As notas continuam na grade e a quantização não muda."_t);
    grooveButton.setShowsMenuArrow (true);
    grooveButton.onClick = [this]
    {
        makeGrooveMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&grooveButton),
                                        [safe = juce::Component::SafePointer<PianoRoll> (this)] (int choice)
                                        {
                                            if (safe != nullptr && choice > 0)
                                                safe->chooseGroove (choice);
                                        });
    };

    // as ferramentas do FL (bloco 1, 22/10): num menu, com os atalhos ao lado, e o carimbo de acorde
    editButton.setTooltip ("Editar: legato (Ctrl+L), juntar notas (Ctrl+G), mudo (Alt+M), humanizar, levar para a escala, "
                           "carimbo de acorde e importar MIDI. Vale nas notas selecionadas; sem seleção, em todas."_t);
    editButton.setShowsMenuArrow (true);
    editButton.setNeutralWhenOn (true);   // aceso com o carimbo ou o encaixe na escala ligado (não é som: sem laranja)
    editButton.onClick = [this]
    {
        makeEditMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editButton));
    };

    for (int i = 0; i < 12; ++i)
        keyBox.addItem (noteNamePt (i, false), i + 1);   // "Dó", "Lá": quem só lê português reconhece

    theme::styleComboBox (keyBox, Icon::note);
    keyBox.setTooltip ("Tom da música: a tônica. As linhas fora da escala ficam escuras."_t);
    keyBox.onChange = [this]
    {
        if (refreshing) return;
        app.run ("project.setKey", args ({ { "root", keyBox.getSelectedItemIndex() } }));
    };

    scaleIds = Project::scaleNames();

    for (auto& id : scaleIds)
        scaleBox.addItem (scaleLabel (id), scaleBox.getNumItems() + 1);

    theme::styleComboBox (scaleBox);
    scaleBox.setTooltip ("Escala"_t);
    scaleBox.onChange = [this]
    {
        if (refreshing) return;
        const int index = scaleBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (index, scaleIds.size()))
            app.run ("project.setKey", args ({ { "root", app.getModel().get().keyRoot }, { "scale", scaleIds[index] } }));
    };

    beatHintsButton.setClickingTogglesState (true);
    beatHintsButton.setNeutralWhenOn (true);   // mostrar dicas não é som ligado: sem laranja
    beatHintsButton.setTooltip ("Dicas do beat: em cada compasso, a linha clara é a fundamental do acorde do beat, onde o 808 casa; "
                                "as mais fracas são as outras notas do acorde. É uma estimativa: confira de ouvido."_t);
    beatHintsButton.onClick = [this] { setBeatHintsOn (beatHintsButton.getToggleState()); };
    addChildComponent (beatHintsButton);
    beatHintsButton.setWantsKeyboardFocus (false);

    ideasButton.setClickingTogglesState (true);
    ideasButton.setNeutralWhenOn (true);   // abrir o painel não é som ligado: "escolhido" neutro, sem laranja
    ideasButton.setTooltip ("Ideias: gera acordes, melodia, arpejo, baixo, 808 ou bateria por gênero e clima"_t);
    ideasButton.onClick = [this] { setIdeasVisible (ideasButton.getToggleState()); };
    ideas.onStatus = [this] (const juce::String& m, bool e) { if (onStatus) onStatus (m, e); };
    ideas.onChannelCreated = [this] (const juce::String& id) { if (onChannelPicked) onChannelPicked (id); };
    ideas.onGenerated = [this]
    {
        grid.refresh (app.getModel().get(), channel);
        grid.revealNotes();
    };
    addChildComponent (ideas);

    grid.onStatus = [this] (const juce::String& m, bool e) { if (onStatus) onStatus (m, e); };
    grid.onToolChanged = [this] { syncToolButtons(); };
    grid.onSlideChanged = [this] { setSlideMode (grid.getSlideMode()); };
    grid.onGridChanged = [this] { updateSnapLabel(); };
    grid.onModesChanged = [this] { syncModeButtons(); };

    for (juce::Component* c : { (juce::Component*) &channelBox, (juce::Component*) &toolSwitch, (juce::Component*) &snapBox,
                                (juce::Component*) &slideButton,
                                (juce::Component*) &monoButton, (juce::Component*) &glideButton,
                                (juce::Component*) &grooveButton,
                                (juce::Component*) &editButton, (juce::Component*) &keyBox,
                                (juce::Component*) &scaleBox, (juce::Component*) &ideasButton })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }

    addAndMakeVisible (grid);
    syncToolButtons();
    updateSnapLabel();
}

void PianoRoll::setChannel (const juce::String& id)
{
    if (id != channel)
    {
        channel = id;
        importDialog.reset();   // o cartão do MIDI era para o outro canal
        refresh (app.getModel().get());
    }
}

void PianoRoll::refresh (const Project& p)
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);

    juce::StringArray ids, names;

    for (auto& c : p.channels)
    {
        if (! c.isAudio())
        {
            ids.add (c.id);
            names.add (c.name);
        }
    }

    if (ids != channelIds || names != channelNames)
    {
        channelIds = ids;
        channelNames = names;
        channelBox.clear (juce::dontSendNotification);

        for (int i = 0; i < names.size(); ++i)
            channelBox.addItem (names[i], i + 1);
    }

    channelBox.setSelectedItemIndex (channelIds.indexOf (channel), juce::dontSendNotification);

    const auto* ch = p.findChannel (channel);
    const bool editable = ch != nullptr && ! ch->isAudio();
    monoButton.setToggleState (editable && ch->mono, juce::dontSendNotification);
    glideButton.setLabel (juce::String (editable ? juce::roundToInt (ch->glideMs) : 0) + " ms");

    // o groove aceso quando o canal tem swing ou humanizar; com swing, o valor no botão
    const float noteSwing = editable ? ch->noteSwing : 0.0f, humanize = editable ? ch->humanize : 0.0f;
    grooveButton.setToggleState (noteSwing > 0.0f || humanize > 0.0f, juce::dontSendNotification);

    if (const auto label = noteSwing > 0.0f ? "Swing "_t + juce::String (juce::roundToInt (noteSwing)) + "%" : juce::String ("Groove");
        grooveLabel != label)
    {
        grooveLabel = label;
        resized();   // a largura do botão muda com o texto
    }
    keyBox.setSelectedItemIndex (p.keyRoot, juce::dontSendNotification);
    scaleBox.setSelectedItemIndex (scaleIds.indexOf (p.scale), juce::dontSendNotification);

    for (juce::Component* c : { (juce::Component*) &toolSwitch, (juce::Component*) &slideButton, (juce::Component*) &monoButton,
                                (juce::Component*) &glideButton, (juce::Component*) &grooveButton, (juce::Component*) &editButton })
        c->setEnabled (editable);

    grid.refresh (p, channel);
    ideas.refresh (p, channel);
    updateBeatHints (p);
}

void PianoRoll::updateBeatHints (const Project& p)
{
    std::vector<BeatHint> hints;
    bool flats = false;

    if (auto* analyzer = app.beatKey())
    {
        hints = beatHintsForPattern (*analyzer, app.getModel(), p.currentPattern);

        // os nomes dos acordes seguem a armadura do tom do beat: Bb em Fá maior, A# em Si maior
        if (const auto beats = analyzer->beatsOf (app.getModel()); ! beats.empty())
            if (const auto r = analyzer->find (beats.front().file); r != nullptr && r->ok && r->analysis.found())
                flats = ideas::keyPrefersFlats (r->analysis.best().root, r->analysis.best().major);
    }

    // à vista só com um beat analisado no projeto (docs/design.md, princípio 6)
    if (hints.empty() == beatHintsButton.isVisible())
    {
        beatHintsButton.setVisible (! hints.empty());
        resized();
    }

    beatHintsButton.setToggleState (beatHintsOn, juce::dontSendNotification);
    grid.setBeatHints (std::move (hints), beatHintsOn, flats);
}

void PianoRoll::setBeatHintsOn (bool on)
{
    beatHintsOn = on;
    beatHintsButton.setToggleState (on, juce::dontSendNotification);
    grid.setBeatHints (grid.getBeatHints(), on, grid.getBeatHintFlats());

    if (onStatus)
        onStatus (on ? "Dicas do beat ligadas: a linha clara de cada compasso é onde o 808 casa."_t
                     : "Dicas do beat desligadas."_t, false);
}

void PianoRoll::setIdeasVisible (bool on)
{
    ideasButton.setToggleState (on, juce::dontSendNotification);
    ideas.setVisible (on);
    resized();
}

juce::PopupMenu PianoRoll::makeEditMenu()
{
    // vale nas notas selecionadas; sem seleção, em todas (como no FL)
    juce::PopupMenu menu, humanize, strum;
    juce::Component::SafePointer<PianoRoll> safe (this);
    auto tool = [safe] (const char* op, double amount = 0.5)
    {
        return [safe, op, amount] { if (safe != nullptr) safe->grid.runTransform (op, amount); };
    };

    addAction (menu, 1, "Legato: cada nota vai até a próxima"_t, tool ("legato"), false, "Ctrl+L");
    addAction (menu, 2, "Juntar notas da mesma tecla que se encostam"_t, tool ("glue"), false, "Ctrl+G");
    menu.addSeparator();
    addAction (menu, 3, "Mudo: as notas ficam, mas não tocam"_t, tool ("mute"), false, "Alt+M");
    addAction (menu, 4, "Tirar o mudo"_t, tool ("unmute"), false, "Alt+Shift+M");
    menu.addSeparator();
    addAction (humanize, 5, "Leve"_t, tool ("humanize", 0.25));
    addAction (humanize, 6, "Médio"_t, tool ("humanize", 0.5));
    addAction (humanize, 7, "Forte"_t, tool ("humanize", 1.0));
    menu.addSubMenu ("Humanizar: tempo e força um pouco ao acaso"_t, humanize);
    addAction (strum, 11, "Leve"_t, tool ("strum", 0.25));
    addAction (strum, 12, "Médio"_t, tool ("strum", 0.5));
    addAction (strum, 13, "Forte"_t, tool ("strum", 1.0));
    menu.addSubMenu ("Strum: dedilhar os acordes, da grave para a aguda"_t, strum);
    addAction (menu, 8, "Levar as notas para a escala do tom"_t, tool ("toScale"));
    addAction (menu, 9, "Encaixar na escala ao desenhar e mover"_t, [safe]
    {
        if (safe == nullptr)
            return;

        safe->grid.setScaleLock (! safe->grid.getScaleLock());
        safe->syncModeButtons();

        if (safe->onStatus)
            safe->onStatus (safe->grid.getScaleLock() ? "Encaixe na escala ligado: desenhar, mover e as setas só caem em notas do tom."_t
                                                      : "Encaixe na escala desligado."_t, false);
    }, grid.getScaleLock());
    menu.addSubMenu ("Carimbo de acorde: o clique põe o acorde inteiro"_t, makeChordMenu(), true, nullptr, grid.getChordStamp().isNotEmpty());
    menu.addSeparator();
    addAction (menu, 10, "Importar MIDI...  (ou arraste o .mid para cá)"_t, [safe] { if (safe != nullptr) safe->chooseMidiFile(); });
    addAction (menu, 14, "Capturar o que eu toquei agora (Capture MIDI)"_t, [safe]
    {
        // a última frase tocada no teclado-piano ou no controlador, gravando ou não (bloco 3)
        if (safe == nullptr)
            return;

        const auto result = safe->app.run ("record.captureMidi", args ({ { "channel", safe->channel } }));

        if (safe->onStatus)
            safe->onStatus (result.ok ? juce::String ((int) result.value["notes"]) + " notas capturadas"_t
                                            + ((bool) result.value["inTime"] ? " no lugar em que tocaram no padrão."_t : " a partir do começo do padrão."_t)
                                      : result.error,
                            ! result.ok);
    });
    return menu;
}

juce::PopupMenu PianoRoll::makeChordMenu()
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<PianoRoll> safe (this);
    const auto current = grid.getChordStamp();

    auto pick = [safe] (const juce::String& id)
    {
        return [safe, id]
        {
            if (safe == nullptr)
                return;

            safe->grid.setChordStamp (id);
            safe->syncModeButtons();

            if (safe->grid.getTool() != PianoRollGrid::Tool::draw && id.isNotEmpty())
            {
                safe->grid.setTool (PianoRollGrid::Tool::draw);   // o carimbo é um jeito de desenhar
                safe->syncToolButtons();
            }

            if (safe->onStatus)
                safe->onStatus (id.isEmpty() ? "Carimbo de acorde desligado."_t
                                             : "Carimbo de acorde ligado: clique na grade para pôr o acorde. Esc desliga."_t, false);
        };
    };

    // ids a partir de 100: este menu vira submenu do Editar, e os ids não podem repetir
    addAction (menu, 100, "Desligado (uma nota por clique)"_t, pick ({}), current.isEmpty());
    menu.addSeparator();
    menu.addSectionHeader ("No tom do projeto"_t);
    int id = 101;

    for (auto& s : notetools::chordShapes())
    {
        const juce::String shape (s.id);

        if (shape == "maj")
            menu.addSectionHeader ("Formas fixas"_t);

        addAction (menu, id++, juce::String::fromUTF8 (s.label), pick (shape), current == shape);
    }

    return menu;
}

void PianoRoll::chooseMidiFile()
{
    static juce::File lastFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    midiChooser = std::make_unique<juce::FileChooser> ("Importar MIDI"_t, lastFolder, "*.mid;*.midi");
    juce::Component::SafePointer<PianoRoll> safe (this);

    midiChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (safe == nullptr || file == juce::File())
            return;

        lastFolder = file.getParentDirectory();
        safe->askMidiImport (file);
    });
}

bool PianoRoll::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
            return true;

    return false;
}

void PianoRoll::fileDragEnter (const juce::StringArray&, int, int)
{
    midiDragOver = true;
    repaint();
}

void PianoRoll::fileDragExit (const juce::StringArray&)
{
    midiDragOver = false;
    repaint();
}

void PianoRoll::filesDropped (const juce::StringArray& files, int, int)
{
    midiDragOver = false;
    repaint();

    for (auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
        {
            askMidiImport (juce::File (f));
            return;
        }
}

void PianoRoll::askMidiImport (const juce::File& file)
{
    const auto& p = app.getModel().get();
    const auto* ch = p.findChannel (channel);

    if (ch == nullptr || ch->isAudio())
    {
        if (onStatus) onStatus ("Escolha um canal de instrumento para receber as notas do MIDI."_t, true);
        return;
    }

    MidiFileInfo info;

    if (const auto read = readMidiInfo (file, info); read.failed() || info.tracks.empty())
    {
        if (onStatus) onStatus (read.failed() ? read.getErrorMessage() : file.getFileName() + " não tem notas."_t, true);
        return;
    }

    // a agulha do padrão (no PAT, tocando ou pausado com Ctrl+Espaço), no começo do compasso dela
    const int bar = juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen)) * juce::jmax (1, p.timeSigNum) * ticksPerStep;
    int needle = 0;

    if (const auto* pat = p.currentPatternPtr(); pat != nullptr && ! p.songMode)
        needle = (int) std::fmod (juce::jmax (0.0, app.positionInSteps() * ticksPerStep), (double) juce::jmax (1, pat->lengthTicks())) / bar * bar;

    importDialog = std::make_unique<MidiImportDialog> (file, info, ch->name, p.bpm, needle, bar);
    juce::Component::SafePointer<PianoRoll> safe (this);

    // o cartão some por último: a função que chama estas está dentro dele
    importDialog->onImport = [safe] (const juce::File& f, MidiImportDialog::Choice choice)
    {
        if (auto* roll = safe.getComponent())
        {
            roll->grid.importMidi (f, choice.track, choice.replace, choice.at);
            roll->closeMidiImport();
        }
    };

    importDialog->onClose = [safe]
    {
        if (auto* roll = safe.getComponent())
            roll->closeMidiImport();
    };

    addAndMakeVisible (*importDialog);
    importDialog->setBounds (getLocalBounds());

    if (isShowing())
        importDialog->grabKeyboardFocus();
}

bool PianoRoll::handleClipboardKey (const juce::KeyPress& key)
{
    if (importDialog != nullptr)
        return false;

    for (auto c : { 'c', 'x', 'v', 'b' })
        if (key == juce::KeyPress (c, juce::ModifierKeys::commandModifier, 0))
            return grid.keyPressed (key);

    return false;
}

void PianoRoll::focusGrid()
{
    if (importDialog == nullptr && grid.isShowing())
        grid.grabKeyboardFocus();
}

void PianoRoll::closeMidiImport()
{
    importDialog.reset();

    if (isShowing())
        grid.grabKeyboardFocus();
}

void PianoRoll::paintOverChildren (juce::Graphics& g)
{
    if (! midiDragOver)
        return;

    using namespace theme;
    const auto area = grid.getBounds().toFloat().reduced (12.0f);
    g.setColour (colour::background.withAlpha (0.82f));
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (colour::signal);
    g.drawRoundedRectangle (area, 8.0f, 2.0f);
    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Solte para importar as notas do MIDI neste canal"_t, area.toNearestInt(), juce::Justification::centred);
}

void PianoRoll::syncModeButtons()
{
    editButton.setToggleState (grid.getChordStamp().isNotEmpty() || grid.getScaleLock(), juce::dontSendNotification);
}

void PianoRoll::syncToolButtons()
{
    const auto tool = grid.getTool();
    toolSwitch.setSelected (tool == PianoRollGrid::Tool::paint ? 1 : tool == PianoRollGrid::Tool::select ? 2
                            : tool == PianoRollGrid::Tool::slice ? 3 : 0);
}

void PianoRoll::setSlideMode (bool on)
{
    grid.setSlideMode (on);
    slideButton.setToggleState (on, juce::dontSendNotification);

    // slide sem mono não desliza: liga o mono e avisa
    const auto* ch = app.getModel().get().findChannel (channel);

    if (on && ch != nullptr && ! ch->isAudio() && ! ch->mono)
    {
        const auto name = ch->name;
        app.run ("channel.setMono", args ({ { "channel", channel }, { "on", true } }));
        if (onStatus) onStatus ("Mono ligado em "_t + name + ": o slide precisa de uma nota por vez."_t, false);
    }
}

void PianoRoll::updateSnapLabel()
{
    // A Auto mostra a divisão do zoom de agora: "Auto: 1/4 passo". A seleção é lida antes de trocar o
    // nome, porque o getSelectedId() do JUCE dá 0 quando o texto à vista não bate com o do item.
    const bool showing = snapBox.getSelectedId() == 1;
    snapBox.changeItemText (1, "Auto: " + grid.getAutoGridName());

    if (showing)
        snapBox.setSelectedId (1, juce::dontSendNotification);   // o texto à vista só muda assim
}

void PianoRoll::showGlideMenu()
{
    const auto* ch = app.getModel().get().findChannel (channel);

    if (ch == nullptr)
        return;

    juce::PopupMenu menu;
    const int current = juce::roundToInt (ch->glideMs);

    for (int ms : { 0, 30, 60, 80, 120, 180, 250, 400, 600 })
        menu.addItem (ms + 1, juce::String (ms) + " ms", true, ms == current);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&glideButton), [this, id = channel] (int choice)
    {
        if (choice > 0)
            app.run ("channel.setGlide", args ({ { "channel", id }, { "ms", choice - 1 } }));
    });
}

juce::PopupMenu PianoRoll::makeGrooveMenu()
{
    const auto* ch = app.getModel().get().findChannel (channel);
    const float swing = ch != nullptr ? ch->noteSwing : 0.0f, humanize = ch != nullptr ? ch->humanize : 0.0f;
    juce::PopupMenu menu;

    menu.addSectionHeader ("Swing das notas (só ao tocar)"_t);

    for (int i = 0; i < (int) std::size (grooveSwings); ++i)
        menu.addItem (100 + i, i == 0 ? "Sem swing"_t : juce::String (juce::roundToInt (grooveSwings[i])) + "%" + (grooveSwings[i] >= 66.0f ? " (tercina)"_t : juce::String()),
                      true, std::abs (swing - grooveSwings[i]) < 0.5f);

    menu.addSeparator();
    menu.addSectionHeader ("Humanizar ao tocar (as notas não mudam)"_t);
    const char* const names[] = { "Desligado", "Leve", "Médio", "Forte" };

    for (int i = 0; i < (int) std::size (grooveHumanize); ++i)
        menu.addItem (200 + i, juce::String::fromUTF8 (names[i]) + (i == 0 ? juce::String() : " (" + juce::String (juce::roundToInt (grooveHumanize[i])) + "%)"),
                      true, std::abs (humanize - grooveHumanize[i]) < 0.5f);

    return menu;
}

void PianoRoll::chooseGroove (int menuId)
{
    CommandResult r;

    if (menuId >= 100 && menuId < 100 + (int) std::size (grooveSwings))
        r = app.run ("channel.setGroove", args ({ { "channel", channel }, { "swing", grooveSwings[menuId - 100] } }));
    else if (menuId >= 200 && menuId < 200 + (int) std::size (grooveHumanize))
        r = app.run ("channel.setGroove", args ({ { "channel", channel }, { "humanize", grooveHumanize[menuId - 200] } }));
    else
        return;

    if (! r.ok && onStatus)
        onStatus (r.error, true);
}

void PianoRoll::paint (juce::Graphics& g)
{
    using namespace theme;
    auto bar = getLocalBounds().removeFromTop (toolbarHeight);
    g.setColour (colour::panel);
    g.fillRect (bar);
    g.setColour (colour::line);
    g.fillRect (bar.removeFromBottom (1));

    // fios finos separam os grupos da barra: canal | ferramentas e grade | 808 | tom
    g.setColour (colour::line);

    for (auto x : dividers)
        g.fillRect (x, bar.getY() + 14, 1, bar.getHeight() - 28);
}

void PianoRoll::resized()
{
    if (importDialog != nullptr)
        importDialog->setBounds (getLocalBounds());

    auto r = getLocalBounds();
    auto bar = r.removeFromTop (toolbarHeight).withSizeKeepingCentre (r.getWidth() - 2 * theme::metric::gap, theme::metric::control);

    if (ideas.isVisible())
        ideas.setBounds (r.removeFromRight (IdeasPanel::idealWidth));

    grid.setBounds (r);

    // barra estreita (tela de 1366 com um navegador aberto): campos mais curtos e o botão Ideias só
    // com o ícone, para o tom aparecer inteiro
    const bool narrow = getWidth() < 1240;

    // as ferramentas: todas com o nome; um pouco mais estreito, só a escolhida; estreita, só os ícones
    toolSwitch.setLabels (getWidth() >= 1500 ? SegmentedControl::Labels::all   // 1500: o botão Editar (bloco 1) também cabe
                          : narrow           ? SegmentedControl::Labels::none
                                             : SegmentedControl::Labels::selected);
    dividers.clear();
    const int c = theme::metric::control, gap = theme::metric::gap * 2;

    channelBox.setBounds (bar.removeFromLeft (narrow ? 150 : 180));
    bar.removeFromLeft (gap / 2);
    dividers.add (bar.getX());
    bar.removeFromLeft (gap / 2);
    toolSwitch.setBounds (bar.removeFromLeft (toolSwitch.getIdealWidth()));
    bar.removeFromLeft (theme::metric::gap);
    snapBox.setBounds (bar.removeFromLeft (narrow ? 140 : 156));
    bar.removeFromLeft (gap / 2);
    dividers.add (bar.getX());
    bar.removeFromLeft (gap / 2);
    slideButton.setBounds (bar.removeFromLeft (slideButton.getIdealWidth (c)));
    bar.removeFromLeft (2);
    monoButton.setBounds (bar.removeFromLeft (monoButton.getIdealWidth (c)));
    bar.removeFromLeft (2);
    glideButton.setBounds (bar.removeFromLeft (glideButton.getIdealWidth (c)));
    bar.removeFromLeft (2);
    ideasButton.setLabel (narrow ? juce::String() : "Ideias");

    // o tom e a escala à direita têm prioridade: sem espaço, o Groove fica só com o ícone, e depois o Editar
    const int rightMin = ideasButton.getIdealWidth (c) + gap + 84 + 4 + 100 + (beatHintsButton.isVisible() ? 48 : 0);
    editButton.setLabel ({});
    const int editIconWidth = editButton.getIdealWidth (c);
    grooveButton.setLabel (grooveLabel);

    if (narrow || bar.getWidth() - grooveButton.getIdealWidth (c) - gap - editIconWidth < rightMin)
        grooveButton.setLabel ({});

    grooveButton.setBounds (bar.removeFromLeft (grooveButton.getIdealWidth (c)));
    bar.removeFromLeft (gap / 2);
    dividers.add (bar.getX());
    bar.removeFromLeft (gap / 2);

    // Editar com o nome se couber; senão só o ícone
    editButton.setLabel ("Editar");

    if (narrow || bar.getWidth() - editButton.getIdealWidth (c) < rightMin)
        editButton.setLabel ({});

    editButton.setBounds (bar.removeFromLeft (editButton.getIdealWidth (c)));

    ideasButton.setBounds (bar.removeFromRight (ideasButton.getIdealWidth (c)));
    bar.removeFromRight (gap / 2);
    dividers.add (bar.getRight());
    bar.removeFromRight (gap / 2);

    // se faltar espaço, a escala encolhe antes do tom: a tônica tem que aparecer inteira
    const int keyWidth = 84;

    // as dicas do beat ficam junto do tom; com pouco espaço, "Dicas"; sem espaço, só o ícone
    int hintsWidth = 0;

    if (beatHintsButton.isVisible())
    {
        for (const char* label : { "Dicas do beat", "Dicas", "" })
        {
            beatHintsButton.setLabel (label);
            hintsWidth = beatHintsButton.getIdealWidth (c);

            if (bar.getWidth() - hintsWidth - 8 - keyWidth - 4 >= 100)
                break;
        }

        hintsWidth += 8;
    }

    // a 1240 px, com o Groove na barra, a escala chega a 68 para o tom ficar com os 84 inteiros
    scaleBox.setBounds (bar.removeFromRight (juce::jlimit (68, 150, bar.getWidth() - 4 - keyWidth - hintsWidth)));
    bar.removeFromRight (4);
    keyBox.setBounds (bar.removeFromRight (keyWidth));

    if (beatHintsButton.isVisible())
    {
        bar.removeFromRight (8);
        beatHintsButton.setBounds (bar.removeFromRight (hintsWidth - 8));
    }
}

} // namespace whynot
