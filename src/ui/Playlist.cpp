#include "Playlist.h"
#include "Icons.h"
#include "SampleBrowser.h"
#include "Theme.h"
#include "../app/AppController.h"

#include <limits>

namespace whynot
{

namespace
{
    // Bloco estreito com takes (pedido do Design, 22/10): sem lugar para um nome legível ao lado da pílula "N takes",
    // o nome e a setinha do menu somem e fica só a pílula. O menu volta com o zoom, como em todo bloco estreito.
    bool onlyPill (juce::Rectangle<float> block, juce::Rectangle<float> pill)
    {
        return ! pill.isEmpty() && pill.getX() - 4.0f - (block.getX() + 25.0f) < 40.0f;
    }

    /** Um item de menu com ação. O JUCE só dispara item com id (o 0 quer dizer "nada escolhido"). */
    void addAction (juce::PopupMenu& menu, int id, const juce::String& text, std::function<void()> action)
    {
        juce::PopupMenu::Item item (text);
        item.itemID = id;
        item.action = std::move (action);
        menu.addItem (std::move (item));
    }

    juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();
        for (auto& [k, v] : kv) o->setProperty (k, v);
        return juce::var (o);
    }

    bool isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
    }

    struct SnapOption { const char* name; int ticks; };

    // A primeira é a automática (o nome dela mostra a divisão do zoom de agora), e a lista abre nela.
    // O id de cada item é a posição aqui + 1; o título das tercinas e as separações não contam.
    const SnapOption snapOptions[] = {
        { "Auto", timegrid::snapAuto },
        { "Grade: compasso", timegrid::snapBar },
        { "Grade: tempo", timegrid::snapBeat },
        { "Grade: passo", ticksPerStep },
        { "Grade: 1/2 passo", ticksPerStep / 2 },
        { "Grade: 1/4 passo", ticksPerStep / 4 },
        { "Grade: 1/3 tempo", ticksPerBeat / 3 },
        { "Grade: 1/6 tempo", ticksPerBeat / 6 },
        { "Grade: 1/3 passo", ticksPerStep / 3 },
        { "Sem grade", 1 },
    };

    constexpr int barOption = 1, firstTripletOption = 6, noGridOption = 9;

    /** O cursor de esticar (segurar A na borda do áudio): as setas da borda com um relógio em cima,
        para não confundir com o de aparar. Contorno escuro para aparecer em qualquer fundo. */
    juce::MouseCursor makeStretchCursor()
    {
        juce::Image image (juce::Image::ARGB, 32, 32, true);
        juce::Graphics g (image);

        juce::Path line, heads;
        line.startNewSubPath (6.0f, 23.0f);
        line.lineTo (26.0f, 23.0f);
        heads.addTriangle (1.0f, 23.0f, 8.0f, 18.0f, 8.0f, 28.0f);
        heads.addTriangle (31.0f, 23.0f, 24.0f, 18.0f, 24.0f, 28.0f);

        g.setColour (theme::colour::background);
        g.strokePath (line, juce::PathStrokeType (4.0f));
        g.strokePath (heads, juce::PathStrokeType (2.5f, juce::PathStrokeType::mitered));
        g.fillEllipse (8.0f, 0.0f, 16.0f, 16.0f);

        g.setColour (theme::colour::text);
        g.strokePath (line, juce::PathStrokeType (1.6f));
        g.fillPath (heads);
        icons::draw (g, Icon::timer, { 9.5f, 1.5f, 13.0f, 13.0f }, theme::colour::text);

        return juce::MouseCursor (image, 16, 23);
    }
}

juce::String PlaylistGrid::stretchLabel (double stretch)
{
    return "×"_t + juce::String (stretch, 2).replaceCharacter ('.', ',');
}

//==============================================================================
PlaylistGrid::PlaylistGrid (AppController& a) : app (a), stretchCursor (makeStretchCursor())
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

PlaylistGrid::~PlaylistGrid()
{
    endEdit();
    hScroll.removeListener (this);
    vScroll.removeListener (this);

    for (auto& [id, t] : thumbs)
        if (t.thumbnail != nullptr)
            t.thumbnail->removeChangeListener (this);
}

void PlaylistGrid::refresh (const Project& p)
{
    bpm = p.bpm;
    punchOn = p.punchOn;
    punchIn = p.punchIn;
    punchOut = p.punchOut;
    markers = p.markers;
    stepsPerBeat = juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen));
    stepsPerBar = stepsPerBeat * juce::jmax (1, p.timeSigNum);
    notifyGridChange();   // a fórmula de compasso muda o nome da grade automática
    tracks = p.playlistTracks;

    // projeto novo ou aberto: o app zerou a seleção de tempo (ela é o loop e o começo do play no SONG)
    if (drag != Drag::timeSelect && app.getTimeSelection().isEmpty() && ! timeSelection.isEmpty())
        timeSelection = {};

    // O pincel acompanha o padrão escolhido no transporte, como no FL.
    if (currentPattern != p.currentPattern)
    {
        currentPattern = p.currentPattern;
        brushKind = "pattern";
        brushRef = currentPattern;
    }

    const bool brushOk = brushKind == "audio" ? (p.findChannel (brushRef) != nullptr && p.findChannel (brushRef)->isAudio())
                                              : p.findPattern (brushRef) != nullptr;
    if (! brushOk)
    {
        brushKind = "pattern";
        brushRef = p.currentPattern;
    }

    if (brushKind == "audio")
        brushName = p.findChannel (brushRef)->name;
    else if (auto* pat = p.findPattern (brushRef))
        brushName = pat->name;

    previews.clear();

    for (auto& pat : p.patterns)
        previews[pat.id] = buildPreview (p, pat);

    // miniaturas de onda dos canais de áudio (carregam em segundo plano)
    for (auto it = thumbs.begin(); it != thumbs.end();)
    {
        auto* ch = p.findChannel (it->first);

        if (ch == nullptr || ! ch->isAudio() || app.getModel().resolveSample (ch->samplePath) != it->second.file)
        {
            if (it->second.thumbnail != nullptr)
                it->second.thumbnail->removeChangeListener (this);

            it = thumbs.erase (it);
        }
        else
        {
            ++it;
        }
    }

    for (auto& ch : p.channels)
    {
        if (! ch.isAudio() || thumbs.count (ch.id) != 0)
            continue;

        Thumb t;
        t.file = app.getModel().resolveSample (ch.samplePath);
        t.seconds = app.fileDurationSeconds (t.file);
        t.thumbnail = std::make_unique<juce::AudioThumbnail> (512, app.getEngine().getAudioFileFormatManager().readFormatManager, thumbCache);

        if (t.file.existsAsFile())
            t.thumbnail->setSource (new juce::FileInputSource (t.file));

        t.thumbnail->addChangeListener (this);
        thumbs[ch.id] = std::move (t);
    }

    blocks.clear();
    groupTakes.clear();
    groupPill.clear();
    std::vector<Block> takePieces;

    for (auto& item : p.playlistItems)
    {
        Block b;
        b.item = item;
        b.audio = item.kind == "audio";
        b.trackIndex = juce::jmax (0, trackIndexOf (item.track));

        if (b.audio)
        {
            if (auto* ch = p.findChannel (item.ref))
                b.name = ch->name;
        }
        else if (auto* pat = p.findPattern (item.ref))
        {
            b.name = pat->name;
        }

        // takes (gravar voz): no comp fica só o que toca; com o grupo aberto, cada take vai para a sub-raia dele
        if (item.takeGroup.isNotEmpty())
        {
            b.name = "Take "_t + juce::String (item.take);
            groupTakes[item.takeGroup] = juce::jmax (groupTakes[item.takeGroup], item.take);

            if (openGroups.count (item.takeGroup) != 0)
            {
                auto piece = b;
                piece.takeRow = true;
                takePieces.push_back (std::move (piece));
            }

            if (item.muted)
                continue;

            // a pílula "3 takes" vai no primeiro pedaço do comp
            const auto* first = groupPill.count (item.takeGroup) != 0 ? p.findItem (groupPill[item.takeGroup]) : nullptr;

            if (first == nullptr || item.start < first->start)
                groupPill[item.takeGroup] = item.id;
        }

        blocks.push_back (std::move (b));
    }

    for (auto& piece : takePieces)
        blocks.push_back (std::move (piece));

    for (auto it = openGroups.begin(); it != openGroups.end();)
        it = groupTakes.count (*it) == 0 ? openGroups.erase (it) : std::next (it);

    layoutRows();

    if (findBlock (selectedItem) == nullptr)
        selectedItem.clear();

    for (auto it = selection.begin(); it != selection.end();)
        it = findBlock (*it) == nullptr ? selection.erase (it) : std::next (it);

    if (onBrushChanged)
        onBrushChanged();

    clampScroll();
    updateScrollBars();
    repaint();
}

PlaylistGrid::Preview PlaylistGrid::buildPreview (const Project& p, const Pattern& pat) const
{
    Preview pv;
    pv.lengthTicks = pat.lengthTicks();

    // uma faixa por canal com conteúdo no padrão
    std::vector<const Channel*> lanes;

    for (auto& c : p.channels)
    {
        bool has = pat.notesFor (c.id) != nullptr;

        if (auto it = pat.steps.find (c.id); ! has && it != pat.steps.end())
            for (auto& s : it->second)
                has = has || s.on;

        if (has)
            lanes.push_back (&c);
    }

    if (lanes.empty())
        return pv;

    const float laneH = 1.0f / (float) lanes.size();

    for (size_t li = 0; li < lanes.size(); ++li)
    {
        const auto& c = *lanes[li];
        const float top = (float) li * laneH;

        if (auto it = pat.steps.find (c.id); it != pat.steps.end())
            for (size_t i = 0; i < it->second.size(); ++i)
                if (it->second[i].on)
                    pv.marks.push_back ({ (int) i * ticksPerStep, ticksPerStep * 3 / 4, top + laneH * 0.2f, laneH * 0.6f });

        if (auto* notes = pat.notesFor (c.id))
        {
            int lo = 127, hi = 0;

            for (auto& n : *notes)
            {
                lo = juce::jmin (lo, n.key);
                hi = juce::jmax (hi, n.key);
            }

            const int rows = hi - lo + 1;
            const float rowH = rows <= 1 ? laneH * 0.6f : laneH / (float) rows;

            for (auto& n : *notes)
            {
                const float y = rows <= 1 ? top + laneH * 0.2f : top + (float) (hi - n.key) * rowH;
                pv.marks.push_back ({ n.start, n.length, y, rowH });
            }
        }
    }

    return pv;
}

juce::String PlaylistGrid::getBrushName() const
{
    return brushName;
}

void PlaylistGrid::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

void PlaylistGrid::setPlayhead (double ticks)
{
    if (std::abs (ticks - playhead) < 0.5)
        return;

    // blocos tocando ganham a marca laranja: repinta tudo só quando essa lista muda
    juce::StringArray live;

    if (ticks >= 0.0)
        for (auto& b : blocks)
            if (ticks >= b.item.start && ticks < b.item.end())
                live.add (b.item.id);

    const int bottom = gridArea().getBottom();
    auto column = [this, bottom] (double t) { return juce::Rectangle<int> ((int) tickToX (t) - 2, 0, 5, bottom); };

    if (live != liveItems)
    {
        // só os blocos que entraram ou saíram da lista ganham ou perdem a marca (fluidez, 21/10)
        for (auto& b : blocks)
            if (live.contains (b.item.id) != liveItems.contains (b.item.id))
                repaint (blockBounds (b).getSmallestIntegerContainer().expanded (1));

        liveItems = live;
    }

    if (playhead >= 0.0) repaint (column (playhead));
    playhead = ticks;
    if (playhead >= 0.0) repaint (column (playhead));
}

void PlaylistGrid::setNeedle (int ticks)
{
    if (ticks == needle)
        return;

    const int bottom = gridArea().getBottom();
    auto column = [this, bottom] (int t) { return juce::Rectangle<int> ((int) tickToX (t) - 6, 0, 13, bottom); };

    if (needle >= 0) repaint (column (needle));
    needle = ticks;
    if (needle >= 0) repaint (column (needle));
}

//==============================================================================
juce::Rectangle<int> PlaylistGrid::gridArea() const
{
    return { headerWidth, rulerHeight, juce::jmax (0, getWidth() - headerWidth - scrollSize),
             juce::jmax (0, getHeight() - rulerHeight - scrollSize) };
}

juce::Rectangle<int> PlaylistGrid::headerArea() const   { return { 0, rulerHeight, headerWidth, gridArea().getHeight() }; }
juce::Rectangle<int> PlaylistGrid::rulerArea() const    { return { headerWidth, 0, gridArea().getWidth(), rulerHeight }; }

float PlaylistGrid::tickToX (double tick) const   { return (float) (headerWidth + (tick - scrollTicks) * pxPerTick); }
double PlaylistGrid::xToTick (float x) const      { return scrollTicks + (x - (float) headerWidth) / pxPerTick; }
float PlaylistGrid::trackToY (int index) const
{
    // as sub-raias dos grupos de takes abertos empurram as raias de baixo
    float top = (float) (index * trackHeight);

    if (juce::isPositiveAndBelow (index, (int) laneTops.size()))
        top = laneTops[(size_t) index];
    else if (! laneTops.empty())
        top = (float) contentHeight() + (float) ((index - (int) laneTops.size()) * trackHeight);

    return (float) (rulerHeight + top - scrollY);
}

int PlaylistGrid::yToTrack (float y) const
{
    const double content = y - (float) rulerHeight + scrollY;
    int index = (int) std::floor (content / trackHeight);

    if (laneTops.size() == tracks.size() && ! laneTops.empty())
    {
        index = 0;

        for (size_t i = 0; i < laneTops.size(); ++i)
            if (content >= laneTops[i])
                index = (int) i;
    }

    return juce::jlimit (0, juce::jmax (0, (int) tracks.size() - 1), index);
}

void PlaylistGrid::layoutRows()
{
    laneSubRows.assign (tracks.size(), 0);

    for (auto& b : blocks)
        if (b.takeRow && juce::isPositiveAndBelow (b.trackIndex, (int) laneSubRows.size()))
            laneSubRows[(size_t) b.trackIndex] = juce::jmax (laneSubRows[(size_t) b.trackIndex], groupTakes[b.item.takeGroup]);

    laneTops.assign (tracks.size(), 0.0f);
    float y = 0.0f;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        laneTops[i] = y;
        y += laneSpan ((int) i);
    }
}

float PlaylistGrid::laneSpan (int index) const
{
    const int subs = juce::isPositiveAndBelow (index, (int) laneSubRows.size()) ? laneSubRows[(size_t) index] : 0;
    return (float) (trackHeight + subs * subRowHeight());
}

double PlaylistGrid::contentHeight() const
{
    return laneTops.empty() ? (double) tracks.size() * trackHeight
                            : (double) laneTops.back() + laneSpan ((int) laneTops.size() - 1);
}

juce::Rectangle<float> PlaylistGrid::blockBounds (const Block& b) const
{
    if (b.takeRow)
    {
        // a sub-raia do take, embaixo da raia (o grupo está aberto)
        const float y = trackToY (b.trackIndex) + (float) trackHeight + (float) ((juce::jmax (1, b.item.take) - 1) * subRowHeight());
        return { tickToX (b.item.start), y + 2.0f, juce::jmax (6.0f, (float) b.item.length * pxPerTick), (float) subRowHeight() - 4.0f };
    }

    return { tickToX (b.item.start), trackToY (b.trackIndex) + 2.0f,
             juce::jmax (6.0f, (float) b.item.length * pxPerTick), (float) trackHeight - 4.0f };
}

juce::Rectangle<float> PlaylistGrid::pillBounds (const Block& b) const
{
    // a pílula "3 takes" no cabeçalho do primeiro pedaço do comp, à direita do nome e antes do ×1,50
    if (b.takeRow || b.item.takeGroup.isEmpty())
        return {};

    const auto it = groupPill.find (b.item.takeGroup);

    if (it == groupPill.end() || it->second != b.item.id)
        return {};

    const auto r = blockBounds (b);
    const float headH = juce::jmin (17.0f, r.getHeight() * 0.4f);
    const float stretchWidth = b.item.stretch != 1.0 ? 52.0f : 0.0f;
    constexpr float width = 60.0f;

    if (r.getWidth() < width + stretchWidth + 12.0f)
        return {};

    return { r.getRight() - 6.0f - stretchWidth - width, r.getY() + 1.0f, width, headH - 2.0f };
}

PlaylistGrid::Handles PlaylistGrid::handlesOf (const Block& b) const
{
    // Só no áudio fora de grupo de takes (lá o crossfade é automático), e em bloco largo o bastante. Os fades
    // ficam na faixa do nome, onde a borda não apara; o ganho, no meio da borda de baixo.
    Handles h;
    const auto r = blockBounds (b);

    if (! b.audio || b.takeRow || b.item.takeGroup.isNotEmpty() || r.getWidth() < 48.0f)
        return h;

    const float headH = juce::jmin (17.0f, r.getHeight() * 0.4f);
    const float in = juce::jlimit (0.0f, r.getWidth(), (float) (secondsToTicks (b.item.fadeIn, bpm) * pxPerTick));
    const float out = juce::jlimit (0.0f, r.getWidth(), (float) (secondsToTicks (b.item.fadeOut, bpm) * pxPerTick));
    h.fadeIn = { juce::jmax (r.getX() + 4.0f, r.getX() + in) - 5.0f, r.getY(), 10.0f, headH };
    h.fadeOut = { juce::jmin (r.getRight() - 4.0f, r.getRight() - out) - 5.0f, r.getY(), 10.0f, headH };
    h.gain = { r.getCentreX() - 9.0f, r.getBottom() - 9.0f, 18.0f, 9.0f };
    return h;
}

const PlaylistItem* PlaylistGrid::hoveredTakePiece() const
{
    for (auto& b : blocks)
        if (b.takeRow && blockBounds (b).contains (hoverPos))
            return &b.item;

    return nullptr;
}

void PlaylistGrid::setGroupOpen (const juce::String& group, bool open)
{
    if (open)
        openGroups.insert (group);
    else
        openGroups.erase (group);

    refresh (app.getModel().get());
}

void PlaylistGrid::setSnap (int setting)
{
    const bool special = setting == timegrid::snapAuto || setting == timegrid::snapBeat || setting == timegrid::snapBar;
    snapSetting = special ? setting : juce::jmax (1, setting);
    repaint();
}

int PlaylistGrid::getSnapTicks() const
{
    return timegrid::resolveSnap (snapSetting, pxPerTick, meter(), spacing);
}

juce::String PlaylistGrid::getAutoGridName() const
{
    return timegrid::name (timegrid::lineTicks (pxPerTick, meter(), spacing), meter());
}

void PlaylistGrid::notifyGridChange()
{
    if (const auto name = getAutoGridName(); name != lastAutoName)
    {
        lastAutoName = name;

        if (onGridChanged)
            onGridChanged();
    }
}

void PlaylistGrid::zoomBy (float notches, float anchorX)
{
    const double anchorTick = xToTick (anchorX);
    pxPerTick = juce::jlimit (minPxPerTick, maxPxPerTick, pxPerTick * std::pow (1.15f, notches));
    scrollTicks = anchorTick - (anchorX - (float) headerWidth) / pxPerTick;

    clampScroll();
    updateScrollBars();
    notifyGridChange();
    repaint();
}

int PlaylistGrid::snapDown (double tick) const
{
    const int snap = getSnapTicks();
    return juce::jmax (0, snap > 1 ? (int) std::floor (tick / snap) * snap : (int) std::floor (tick));
}

int PlaylistGrid::snapNearest (double tick) const
{
    const int snap = getSnapTicks();
    return juce::jmax (0, snap > 1 ? juce::roundToInt (tick / snap) * snap : juce::roundToInt (tick));
}

int PlaylistGrid::contentTicks() const
{
    int end = 32 * ticksPerBar4;   // 32 compassos à vista no mínimo

    for (auto& b : blocks)
        end = juce::jmax (end, b.item.end() + 8 * ticksPerBar4);

    return end;
}

double PlaylistGrid::ticksToSeconds (double ticks) const
{
    return whynot::ticksToSeconds (ticks, bpm);
}

int PlaylistGrid::offsetTicks (const PlaylistItem& item) const
{
    return juce::roundToInt (secondsToTicks (item.offsetSeconds, bpm));
}

int PlaylistGrid::trackIndexOf (const juce::String& id) const
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == id)
            return (int) i;

    return -1;
}

const PlaylistGrid::Block* PlaylistGrid::findBlock (const juce::String& id) const
{
    for (auto& b : blocks)
        if (b.item.id == id)
            return &b;

    return nullptr;
}

const PlaylistGrid::Block* PlaylistGrid::blockAt (juce::Point<float> p, Drag& part) const
{
    part = Drag::move;

    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it)
    {
        const auto r = blockBounds (*it);

        if (! r.contains (p))
            continue;

        if (it->item.takeGroup.isNotEmpty())
            return &*it;   // takes: as bordas não aparam nem esticam (os takes do grupo ficam alinhados)

        // as alças do clipe: fades nos cantos de cima, ganho embaixo
        if (const auto h = handlesOf (*it); ! h.gain.isEmpty())
        {
            if (h.fadeIn.contains (p))  { part = Drag::fadeIn;  return &*it; }
            if (h.fadeOut.contains (p)) { part = Drag::fadeOut; return &*it; }
            if (h.gain.contains (p))    { part = Drag::gain;    return &*it; }
        }

        const float grab = juce::jmin (8.0f, r.getWidth() * 0.25f);

        if (p.x >= r.getRight() - grab)                  part = Drag::resize;
        else if (it->audio && p.x <= r.getX() + grab)    part = Drag::trimStart;

        return &*it;
    }

    return nullptr;
}

bool PlaylistGrid::stretchHeld() const
{
    // Segurar A, como no FL 2026. Com a playlist no foco, o A chega pelo keyPressed; sem o foco (o mouse só
    // passando por cima), pergunta direto ao teclado, e só com o app na frente.
    return stretchKey || (juce::Process::isForegroundProcess() && juce::KeyPress::isKeyCurrentlyDown ('a'));
}

juce::Rectangle<float> PlaylistGrid::menuButtonBounds (const Block& b) const
{
    // a setinha fica logo depois da área de aparar da borda esquerda, e só em bloco largo o bastante
    const auto r = blockBounds (b);
    const float headH = juce::jmin (17.0f, r.getHeight() * 0.4f);

    if (! b.audio || b.takeRow || r.getWidth() < 44.0f || onlyPill (r, pillBounds (b)))
        return {};

    return { r.getX() + 9.0f, r.getY(), 14.0f, headH };
}

void PlaylistGrid::showBlockMenu (const juce::String& id)
{
    const auto* item = modelItem (id);

    if (item == nullptr || item->kind != "audio")
        return;

    // o botão direito na grade apaga (como no FL), então o menu do bloco abre pela setinha do cabeçalho
    juce::PopupMenu menu;
    const bool grouped = item->takeGroup.isNotEmpty();

    if (! grouped)
    {
        menu.addSectionHeader ("Esticar no tempo (segure A e arraste a borda)"_t);
        menu.addItem (1, "Esticar muda o tom (fita)"_t, true, item->tape);
        menu.addItem (2, "Voltar ao tempo do arquivo (×1)"_t, item->stretch != 1.0);
        menu.addItem (3, "Esticar para o BPM do projeto (warp, o tom fica)"_t);
        menu.addItem (4, "Pegar o swing deste loop para os canais do padrão"_t);
    }

    // edição do clipe (gravar voz): ids 20 a 39; o fatiar usa de 40 em diante, no fim do menu
    menu.addSectionHeader ("Áudio (as alças: fades nos cantos de cima, ganho embaixo)"_t);

    if (grouped)
    {
        menu.addItem (22, "Consolidar o comp num WAV (o que toca de cada take)"_t);
    }
    else
    {
        menu.addItem (20, "Normalizar (o pico em -1 dB)"_t);
        menu.addItem (21, "Inverter (de trás para frente)"_t);
        menu.addItem (22, selection.size() > 1 ? "Consolidar os selecionados num WAV"_t : "Consolidar num WAV"_t);
        menu.addItem (23, "Ganho em 0 dB"_t, item->gainDb != 0.0f);
        menu.addItem (24, "Tirar os fades"_t, item->fadeIn > 0.0 || item->fadeOut > 0.0);
        menu.addSeparator();
        menu.addItem (25, "Acertar o BPM do projeto por este áudio"_t);
        menu.addItem (26, "BPM do projeto em dobro (o beat estava no meio tempo)"_t);
        menu.addItem (27, "BPM do projeto pela metade"_t);
    }

    // o fatiar (Fatiar e 808): ids de 40 em diante, no fim do menu; num grupo de takes, não
    if (! grouped)
    {
        menu.addSectionHeader ("Picotar: um bloco por pedaço"_t);
        menu.addItem (40, "Pelas batidas"_t);   // ids de 40 em diante (combinado com a Gravação de voz, que usa 20 a 39)
        menu.addItem (41, "A cada tempo"_t);
        menu.addItem (42, "A cada compasso"_t);
    }

    const auto* b = findBlock (id);
    const auto area = b != nullptr ? localAreaToGlobal (menuButtonBounds (*b).toNearestInt()) : juce::Rectangle<int>();

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (area).withMinimumWidth (260),
                        [safe = juce::Component::SafePointer<PlaylistGrid> (this), id] (int choice)
    {
        if (safe == nullptr || choice == 0)
            return;

        const auto* now = safe->modelItem (id);

        if (now == nullptr)
            return;

        if (choice >= 40 && choice <= 42)
        {
            const auto chop = safe->app.run ("playlist.chopItem", args ({ { "item", id }, { "mode", choice == 40 ? "attack" : choice == 41 ? "beat" : "bar" } }));
            const int pieces = chop.ok ? chop.value["items"].size() : 0;

            if (safe->onStatus)
                safe->onStatus (! chop.ok ? chop.error
                                : pieces > 1 ? "O clipe virou "_t + juce::String (pieces) + " blocos (Ctrl+Z desfaz)."_t
                                             : "Nenhum corte: o clipe não tem batidas separadas (ou é curto demais)."_t,
                                ! chop.ok);
            return;
        }

        CommandResult result;

        if (choice == 3)
        {
            // warp (bloco 3): o loop no BPM do projeto, sem mudar o tom
            result = safe->app.run ("audio.warpToProject", args ({ { "item", id } }));

            if (result.ok && safe->onStatus)
                safe->onStatus ("O loop estava em "_t + juce::String ((double) result.value["bpm"], 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                                    + " BPM e agora toca no BPM do projeto, com o mesmo tom. Ctrl+Z volta."_t, false);
        }
        else if (choice == 4)
        {
            // groove (bloco 3): o swing medido no loop vai para os canais com passos no padrão atual
            const auto& p = safe->app.getModel().get();
            juce::Array<juce::var> channels;

            if (const auto* pat = p.currentPatternPtr())
                for (auto& [chId, steps] : pat->steps)
                    if (std::any_of (steps.begin(), steps.end(), [] (const Step& s) { return s.on; }))
                        channels.add (chId);

            result = safe->app.run ("audio.extractGroove", args ({ { "item", id }, { "channels", channels } }));

            if (result.ok && safe->onStatus)
                safe->onStatus ("Swing do loop: "_t + juce::String ((int) result.value["swing"]) + "%, aplicado em "_t
                                    + juce::String (result.value["channels"].size()) + " canais do padrão (Ctrl+Z volta)."_t, false);
        }
        else if (choice == 22)
        {
            // consolidar: o comp do grupo de takes (o que toca), ou os blocos de áudio selecionados
            juce::Array<juce::var> items;

            for (auto& i : safe->app.getModel().get().playlistItems)
                if (i.kind == "audio" && (now->takeGroup.isNotEmpty() ? (i.takeGroup == now->takeGroup && ! i.muted)
                                                                      : (i.id == id || safe->selection.count (i.id) != 0)))
                    items.add (i.id);

            result = safe->app.run ("audio.consolidate", args ({ { "items", items } }));
        }
        else if (choice >= 25 && choice <= 27)
        {
            // o andamento pelo áudio: o nome do arquivo ou a análise; o dobro e a metade do BPM de agora
            const double bpmNow = safe->app.getModel().get().bpm;
            result = choice == 25 ? safe->app.run ("audio.matchBpm", args ({ { "item", id } }))
                                  : safe->app.run ("audio.matchBpm", args ({ { "item", id }, { "bpm", choice == 26 ? bpmNow * 2.0 : bpmNow * 0.5 } }));

            if (result.ok && safe->onStatus)
                safe->onStatus ("Projeto em "_t + juce::String ((double) result.value["bpm"], 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                                    + " BPM; o bloco cobre o áudio inteiro."_t, false);
        }
        else if (choice >= 20)
        {
            result = choice == 20 ? safe->app.run ("audio.normalize", args ({ { "item", id } }))
                   : choice == 21 ? safe->app.run ("audio.reverse", args ({ { "item", id } }))
                   : choice == 23 ? safe->app.run ("audio.setGain", args ({ { "item", id }, { "db", 0.0 } }))
                                  : safe->app.run ("audio.setFades", args ({ { "item", id }, { "in", 0.0 }, { "out", 0.0 } }));
        }
        else
        {
            result = choice == 1 ? safe->app.run ("playlist.updateItem", args ({ { "item", id }, { "tape", ! now->tape } }))
                                 : safe->app.run ("playlist.stretchItem", args ({ { "item", id }, { "stretch", 1.0 } }));
        }

        if (! result.ok && safe->onStatus)
            safe->onStatus (result.error, true);
    });
}

//==============================================================================
void PlaylistGrid::resized()
{
    const auto g = gridArea();
    vScroll.setBounds (getWidth() - scrollSize, g.getY(), scrollSize, g.getHeight());
    hScroll.setBounds (g.getX(), getHeight() - scrollSize, g.getWidth(), scrollSize);
    clampScroll();
    updateScrollBars();
}

void PlaylistGrid::clampScroll()
{
    const auto g = gridArea();
    const double totalHeight = contentHeight();
    scrollY = juce::jlimit (0.0, juce::jmax (0.0, totalHeight - g.getHeight()), scrollY);
    scrollTicks = juce::jlimit (0.0, juce::jmax (0.0, (double) contentTicks() - g.getWidth() / (double) pxPerTick), scrollTicks);
}

void PlaylistGrid::updateScrollBars()
{
    const juce::ScopedValueSetter<bool> guard (updatingScrollBars, true);
    const auto g = gridArea();
    vScroll.setRangeLimits (0.0, juce::jmax ((double) g.getHeight(), contentHeight()), juce::dontSendNotification);
    vScroll.setCurrentRange (scrollY, (double) g.getHeight(), juce::dontSendNotification);
    hScroll.setRangeLimits (0.0, (double) contentTicks(), juce::dontSendNotification);
    hScroll.setCurrentRange (scrollTicks, g.getWidth() / pxPerTick, juce::dontSendNotification);
}

void PlaylistGrid::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (updatingScrollBars)
        return;

    if (bar == &vScroll) scrollY = start;
    else                 scrollTicks = start;

    repaint();
}

//==============================================================================
void PlaylistGrid::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::background);

    // toda rolagem e todo zoom repintam a grade: daqui a automação embaixo acompanha
    if (scrollTicks != notifiedScroll || pxPerTick != notifiedZoom)
    {
        notifiedScroll = scrollTicks;
        notifiedZoom = pxPerTick;
        if (onViewChanged) onViewChanged();
    }

    const bool showPunch = punchOn || punchDrag != PunchEdge::none;
    const int shownIn = punchDrag != PunchEdge::none ? dragIn : punchIn;
    const int shownOut = punchDrag != PunchEdge::none ? dragOut : punchOut;

    const auto area = gridArea();
    const auto clip = area.toFloat();
    const int firstRow = yToTrack ((float) area.getY());
    const int lastRow = yToTrack ((float) area.getBottom() - 1.0f);

    // A agulha repinta uma coluna de 5 px a cada quadro: blocos, linhas e textos fora da parte suja ficam
    // de fora (o recorte já os escondia, mas o texto e as prévias custavam do mesmo jeito)
    const auto dirty = g.getClipBounds();
    const auto dirtyArea = dirty.getIntersection (area);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area);

        for (int i = firstRow; i <= lastRow && i < (int) tracks.size(); ++i)
        {
            const float y = trackToY (i);
            g.setColour (i % 2 == 0 ? colour::grid::laneEven : colour::grid::laneOdd);
            g.fillRect (juce::Rectangle<float> ((float) area.getX(), y, (float) area.getWidth(), (float) trackHeight));
            g.setColour (colour::grid::laneEdge);
            g.fillRect (juce::Rectangle<float> ((float) area.getX(), y + (float) trackHeight - 1.0f, (float) area.getWidth(), 1.0f));

            // as sub-raias dos takes (grupo aberto): um degrau abaixo das raias, para ler como "dentro"
            const int subs = juce::isPositiveAndBelow (i, (int) laneSubRows.size()) ? laneSubRows[(size_t) i] : 0;

            for (int k = 0; k < subs; ++k)
            {
                const float sy = y + (float) trackHeight + (float) (k * subRowHeight());
                g.setColour (colour::grid::rowOut);
                g.fillRect (juce::Rectangle<float> ((float) area.getX(), sy, (float) area.getWidth(), (float) subRowHeight()));
                g.setColour (colour::grid::laneEdge);
                g.fillRect (juce::Rectangle<float> ((float) area.getX(), sy + (float) subRowHeight() - 1.0f, (float) area.getWidth(), 1.0f));
            }
        }

        // compassos (de 4 em 4 mais fortes, a frase do trap) e, quando há espaço, tempos, passos e
        // as divisões do passo: a grade segue o zoom
        const auto m = meter();
        const int every = timegrid::visibleTicks (snapSetting, pxPerTick, m, spacing);
        const auto firstLine = juce::jmax ((juce::int64) 0, (juce::int64) std::floor (xToTick ((float) dirtyArea.getX() - 1.0f) / every));
        const auto lastLine = (juce::int64) std::ceil (xToTick ((float) dirtyArea.getRight() + 1.0f) / every);

        for (auto i = firstLine; i <= lastLine && ! dirtyArea.isEmpty(); ++i)
        {
            const auto tick = i * every;

            switch (timegrid::levelOf (tick, m))
            {
                case timegrid::Level::bar:   g.setColour ((tick / m.bar()) % 4 == 0 ? colour::textDim.withAlpha (0.3f) : colour::line); break;
                case timegrid::Level::beat:  g.setColour (colour::grid::beat); break;
                case timegrid::Level::step:  g.setColour (colour::grid::step); break;
                case timegrid::Level::fine:  g.setColour (colour::grid::fine); break;
            }

            g.fillRect (tickToX ((double) tick), (float) area.getY(), 1.0f, (float) area.getHeight());
        }

        const auto dirtyBlocks = dirtyArea.toFloat().expanded (1.0f);

        for (auto& b : blocks)
            if (blockBounds (b).intersects (clip) && blockBounds (b).intersects (dirtyBlocks))   // só o que a parte suja toca (Fluidez)
                b.takeRow ? paintTakePiece (g, b, clip) : paintBlock (g, b, clip);

        // os cortes de um grupo aberto: um fio que atravessa o comp e todas as sub-raias
        for (auto& group : openGroups)
        {
            std::set<int> cuts;
            int first = std::numeric_limits<int>::max(), lane = -1;

            for (auto& b : blocks)
            {
                if (b.takeRow && b.item.takeGroup == group)
                {
                    cuts.insert (b.item.start);
                    cuts.insert (b.item.end());
                    first = juce::jmin (first, b.item.start);
                    lane = b.trackIndex;
                }
            }

            if (lane < 0)
                continue;

            const int last = cuts.empty() ? first : *cuts.rbegin();
            const float top = trackToY (lane) + 2.0f, bottom = trackToY (lane) + laneSpan (lane) - 2.0f;
            g.setColour (colour::text.withAlpha (0.35f));

            for (int c : cuts)
                if (c != first && c != last)
                    g.fillRect (tickToX (c) - 0.5f, top, 1.0f, bottom - top);
        }

        // punch: o trecho onde a gravação vale, em vermelho funcional bem leve, com as duas pontas
        if (showPunch)
        {
            const float x1 = tickToX (shownIn), x2 = tickToX (shownOut);
            g.setColour (colour::meterHigh.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (x1, (float) area.getY(), x2 - x1, (float) area.getHeight()));
            g.setColour (colour::meterHigh.withAlpha (0.45f));
            g.fillRect (x1 - 0.5f, (float) area.getY(), 1.0f, (float) area.getHeight());
            g.fillRect (x2 - 0.5f, (float) area.getY(), 1.0f, (float) area.getHeight());
        }

        // seleção de tempo: a faixa clara por todas as raias, com as duas pontas
        if (! timeSelection.isEmpty())
        {
            const float x1 = tickToX (timeSelection.getStart()), x2 = tickToX (timeSelection.getEnd());
            g.setColour (colour::text.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (x1, (float) area.getY(), x2 - x1, (float) area.getHeight()));
            g.setColour (colour::textDim.withAlpha (0.6f));
            g.fillRect (x1 - 0.5f, (float) area.getY(), 1.0f, (float) area.getHeight());
            g.fillRect (x2 - 0.5f, (float) area.getY(), 1.0f, (float) area.getHeight());
        }

        // marcadores: um fio fino por todas as raias, onde a parte da música começa
        for (auto& mk : markers)
        {
            const float x = tickToX (mk.t);

            if (x >= (float) area.getX() - 1.0f && x <= (float) area.getRight() + 1.0f)
            {
                g.setColour (colour::textDim.withAlpha (0.28f));
                g.fillRect (x - 0.5f, (float) area.getY(), 1.0f, (float) area.getHeight());
            }
        }

        if (drag == Drag::select && ! rubberBand.isEmpty())
        {
            g.setColour (colour::text.withAlpha (0.06f));
            g.fillRect (rubberBand);
            g.setColour (colour::textDim);
            g.drawRect (rubberBand, 1.0f);
        }

        // a tesoura mostra onde vai cortar
        if (tool == Tool::slice && clip.contains (hoverPos))
        {
            const float x = tickToX (drag == Drag::slice ? sliceTick : snapNearest (xToTick (hoverPos.x)));
            g.setColour (colour::signal.withAlpha (0.8f));
            g.fillRect (x - 0.5f, (float) area.getY(), 1.0f, (float) area.getHeight());
        }

        if (dragOver)
        {
            const int track = yToTrack ((float) dropPos.y);
            const float x = tickToX (snapDown (xToTick ((float) dropPos.x)));
            g.setColour (colour::signal);
            g.fillRect (x - 1.0f, trackToY (track), 2.0f, (float) trackHeight);
        }

        if (playhead >= 0.0)
        {
            g.setColour (colour::text);
            g.fillRect (tickToX (playhead) - 0.5f, (float) area.getY(), 1.5f, (float) area.getHeight());
        }
        else if (needle >= 0)
        {
            // a agulha parada: o play começa daqui (sem laranja, não está tocando)
            g.setColour (colour::textDim.withAlpha (0.4f));
            g.fillRect (tickToX (needle) - 0.5f, (float) area.getY(), 1.0f, (float) area.getHeight());
        }

        if (blocks.empty())
        {
            g.setColour (colour::textDim);
            g.setFont (font (15.0f));
            g.drawText ("Clique na grade para colocar \""_t + brushName + "\". Arraste arquivos de áudio do Explorer para criar clipes."_t,
                        area.withHeight (juce::jmin (area.getHeight(), 3 * trackHeight)).reduced (16, 0),
                        juce::Justification::centred);
        }
    }

    // régua
    const auto ruler = rulerArea();
    g.setColour (colour::panel);
    g.fillRect (ruler);
    g.setColour (colour::line);
    g.fillRect (ruler.getX(), ruler.getBottom() - 1, ruler.getWidth(), 1);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (ruler);

        const int barTicks = stepsPerBar * ticksPerStep;
        const float pxPerBar = (float) barTicks * pxPerTick;
        const int labelEvery = pxPerBar >= 36.0f ? 1 : (pxPerBar >= 18.0f ? 2 : (pxPerBar >= 9.0f ? 4 : 8));
        const auto dirtyRuler = dirty.getIntersection (ruler);
        const int firstBar = juce::jmax (0, (int) std::floor (xToTick ((float) dirtyRuler.getX() - 46.0f) / barTicks));
        const int lastBar = (int) std::ceil (xToTick ((float) dirtyRuler.getRight() + 1.0f) / barTicks);

        for (int bar = firstBar; bar <= lastBar && ! dirtyRuler.isEmpty(); ++bar)
        {
            const float x = tickToX ((double) bar * barTicks);
            g.setColour (colour::textDim.withAlpha (0.6f));
            g.fillRect (x, (float) ruler.getBottom() - 10.0f, 1.0f, 9.0f);

            if (bar % labelEvery == 0)
            {
                g.setColour (colour::text);
                g.setFont (font (13.0f, Weight::medium));
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, (float) ruler.getY(), 40.0f, (float) ruler.getHeight() - 4.0f),
                            juce::Justification::centredLeft, false);
            }
        }

        // de perto: o tempo ("3.2"), o passo ("3.2.4") e marquinhas nas divisões à vista
        timegrid::paintFineRuler (g, ruler, xToTick ((float) ruler.getX()), xToTick ((float) ruler.getRight()),
                                  timegrid::visibleTicks (snapSetting, pxPerTick, meter(), spacing), pxPerTick, meter(),
                                  [this] (double t) { return tickToX (t); }, font (11.0f), colour::textFaint, colour::textFaint.withAlpha (0.8f));

        if (showPunch)
        {
            // a faixa do punch no alto da régua, com as pontas de arrastar
            const float x1 = tickToX (shownIn), x2 = tickToX (shownOut);
            g.setColour (colour::meterHigh.withAlpha (0.9f));
            g.fillRoundedRectangle (juce::Rectangle<float> (x1, (float) ruler.getY() + 2.0f, x2 - x1, 4.0f), 2.0f);
            g.fillRect (juce::Rectangle<float> (x1 - 1.0f, (float) ruler.getY() + 2.0f, 2.0f, 12.0f));
            g.fillRect (juce::Rectangle<float> (x2 - 1.0f, (float) ruler.getY() + 2.0f, 2.0f, 12.0f));
        }

        if (! timeSelection.isEmpty())
        {
            const float x1 = tickToX (timeSelection.getStart()), x2 = tickToX (timeSelection.getEnd());
            g.setColour (colour::textDim.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (x1, (float) ruler.getBottom() - 7.0f, x2 - x1, 6.0f));
        }

        // marcadores: a bandeirinha com o nome, por cima do número do compasso
        for (auto& mk : markers)
        {
            const auto r = markerBounds (mk);

            if (! r.intersects (ruler.toFloat()))
                continue;

            g.setColour (colour::raisedHigh);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (colour::text);
            g.fillRect (r.withWidth (2.0f));
            g.setFont (font (11.0f, Weight::semibold));
            g.drawText (mk.name, r.withTrimmedLeft (6.0f).withTrimmedRight (4.0f), juce::Justification::centredLeft, true);
        }

        if (playhead >= 0.0 || needle >= 0)
        {
            // tocando, o triângulo é branco; parado, é a agulha, em textDim
            const float x = tickToX (playhead >= 0.0 ? playhead : (double) needle);
            juce::Path marker;
            marker.addTriangle (x - 5.0f, (float) ruler.getY() + 3.0f, x + 5.0f, (float) ruler.getY() + 3.0f, x, (float) ruler.getY() + 11.0f);
            g.setColour (playhead >= 0.0 ? colour::text : colour::textDim);
            g.fillPath (marker);
        }
    }

    // raias
    const auto header = headerArea();
    g.setColour (colour::panel);
    g.fillRect (header);

    if (dirty.intersects (header))
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (header);

        for (int i = firstRow; i <= lastRow && i < (int) tracks.size(); ++i)
        {
            const auto row = juce::Rectangle<float> (0.0f, trackToY (i), (float) headerWidth, (float) trackHeight);
            g.setColour (colour::textDim);
            g.setFont (font (12.0f));
            g.drawText (juce::String (i + 1), row.withWidth (30.0f), juce::Justification::centred, false);
            g.setColour (colour::text);
            g.setFont (font (14.0f, Weight::medium));

            // a raia que grava (a entrada escolhida no cabeçalho): o nome sobe e a entrada aparece embaixo,
            // com o ponto do armar em vermelho quando a trilha dela está armada
            const auto* recordTrack = app.getModel().get().findMixer (tracks[(size_t) i].recordTrack);
            auto nameRow = row.withTrimmedLeft (30.0f).withTrimmedRight (8.0f);

            if (recordTrack != nullptr)
            {
                const auto* rec = app.recorder();
                const auto input = rec == nullptr ? juce::String()
                                 : rec->settings().stereo ? "Entradas "_t + juce::String (rec->settings().firstInput + 1) + " e "
                                                                + juce::String (rec->settings().firstInput + 2)
                                                          : "Entrada "_t + juce::String (rec->settings().firstInput + 1);
                const auto sub = nameRow.removeFromBottom (nameRow.getHeight() * 0.42f);
                const auto dot = sub.withWidth (sub.getHeight()).reduced (sub.getHeight() * 0.3f);
                g.setColour (recordTrack->armed ? colour::meterHigh : colour::textFaint);
                g.fillEllipse (dot);
                g.setColour (colour::textDim);
                g.setFont (font (type::small));
                g.drawText (recordTrack->armed ? input : "não armada"_t, sub.withTrimmedLeft (sub.getHeight()), juce::Justification::centredLeft, true);
                g.setColour (colour::text);
                g.setFont (font (14.0f, Weight::medium));
            }

            g.drawFittedText (tracks[(size_t) i].name, nameRow.toNearestInt(),
                              recordTrack != nullptr ? juce::Justification::bottomLeft : juce::Justification::centredLeft, 1, 0.85f);
            g.setColour (colour::line);
            g.fillRect (row.withTop (row.getBottom() - 1.0f));

            // as sub-raias dos takes: recuo de 12 px, um fio ligando à raia de cima e o "Take 1", "Take 2"...
            const int subs = juce::isPositiveAndBelow (i, (int) laneSubRows.size()) ? laneSubRows[(size_t) i] : 0;

            for (int k = 0; k < subs; ++k)
            {
                const auto sub = juce::Rectangle<float> (0.0f, row.getBottom() + (float) (k * subRowHeight()), (float) headerWidth, (float) subRowHeight());
                g.setColour (colour::grid::rowOut);
                g.fillRect (sub);
                g.setColour (colour::line);
                g.fillRect (sub.getX() + 18.0f, sub.getY(), 1.0f, k + 1 == subs ? sub.getHeight() * 0.5f : sub.getHeight());
                g.fillRect (sub.getX() + 18.0f, sub.getCentreY(), 8.0f, 1.0f);
                g.setColour (colour::textDim);
                g.setFont (font (type::small));
                g.drawText ("Take "_t + juce::String (k + 1), sub.withTrimmedLeft (30.0f), juce::Justification::centredLeft, false);
                g.setColour (colour::line);
                g.fillRect (sub.withTop (sub.getBottom() - 1.0f));
            }
        }
    }

    g.setColour (colour::line);
    g.fillRect (header.getRight() - 1, header.getY(), 1, header.getHeight());

    // cantos
    g.setColour (colour::panel);
    g.fillRect (0, 0, headerWidth, rulerHeight);
    g.fillRect (0, getHeight() - scrollSize, headerWidth, scrollSize);
    g.fillRect (getWidth() - scrollSize, 0, scrollSize, rulerHeight);
    g.setColour (colour::textDim);
    g.setFont (font (13.0f));
    g.drawText ("Raias", juce::Rectangle<int> (12, 0, headerWidth - 12, rulerHeight), juce::Justification::centredLeft);

    if (dragOver)
    {
        g.setColour (colour::signal);
        g.drawRect (getLocalBounds(), 2);
    }
}

void PlaylistGrid::paintBlock (juce::Graphics& g, const Block& b, juce::Rectangle<float> clip)
{
    using namespace theme;
    const auto r = blockBounds (b).reduced (0.5f, 0.0f);
    const bool isSelected = selection.count (b.item.id) != 0;
    const bool live = liveItems.contains (b.item.id);

    g.setColour (b.audio ? colour::panel : colour::raised);
    g.fillRoundedRectangle (r, 3.0f);

    const float headH = juce::jmin (17.0f, r.getHeight() * 0.4f);
    const auto head = r.withHeight (headH);
    g.setColour (b.audio ? colour::grid::clipHead : colour::raisedHigh);
    g.fillRoundedRectangle (head, 3.0f);
    g.fillRect (head.withTrimmedTop (headH * 0.5f));

    // áudio: a setinha do menu do bloco (fita, voltar ao ×1) antes do nome
    auto nameArea = head.reduced (6.0f, 0.0f);

    if (const auto menuButton = menuButtonBounds (b); ! menuButton.isEmpty())
    {
        icons::draw (g, Icon::chevronDown, menuButton.withSizeKeepingCentre (10.0f, 10.0f), colour::textDim);
        nameArea.setLeft (menuButton.getRight() + 2.0f);
    }

    // o esticar no fim do cabeçalho ("×1,50", e "fita" se o tom vai junto), e o ganho do bloco ("+3,0 dB")
    const bool gained = b.audio && b.item.gainDb != 0.0f;
    const bool stretched = b.audio && (b.item.stretch != 1.0 || gained);
    const auto stretchText = (b.audio && b.item.stretch != 1.0 ? stretchLabel (b.item.stretch) + (b.item.tape ? " fita" : "") : juce::String())
                             + (gained ? (b.item.stretch != 1.0 ? "  " : "") + juce::String (b.item.gainDb > 0.0f ? "+" : "")
                                             + juce::String (b.item.gainDb, 1).replaceCharacter ('.', ',') + " dB"
                                       : juce::String());

    // takes: a pílula "3 takes" (fundo raised, seta de abrir; aberta, a pílula de escolhido)
    const auto pill = pillBounds (b);

    if (! pill.isEmpty())
    {
        const bool open = openGroups.count (b.item.takeGroup) != 0;
        const bool over = pill.contains (hoverPos);
        g.setColour (open ? colour::raisedHigh.brighter (0.08f) : over ? colour::raisedHigh : colour::raised);
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        icons::draw (g, open ? Icon::chevronDown : Icon::chevronRight, pill.withWidth (pill.getHeight()).reduced (3.0f),
                     over || open ? colour::text : colour::textDim);
        g.setColour (over || open ? colour::text : colour::textDim);
        g.setFont (font (11.0f, Weight::medium));
        g.drawText (juce::String (groupTakes[b.item.takeGroup]) + " takes", pill.withTrimmedLeft (pill.getHeight() - 1.0f),
                    juce::Justification::centredLeft, false);
        nameArea.setRight (pill.getX() - 4.0f);
    }

    const bool pillAlone = onlyPill (r, pill);

    if (! pillAlone)
    {
        g.setColour (colour::text);
        g.setFont (font (12.5f, Weight::semibold));
        g.drawText (b.name, nameArea.withTrimmedRight (stretched && ! b.item.muted && pill.isEmpty() ? 52.0f : 0.0f).getIntersection (clip),
                    juce::Justification::centredLeft, true);
    }

    if (stretched && ! b.item.muted && ! pillAlone)
    {
        g.setColour (colour::textDim);
        g.setFont (font (11.0f, Weight::medium));
        g.drawText (stretchText, nameArea.getIntersection (clip), juce::Justification::centredRight, false);
    }

    const auto body = r.withTrimmedTop (headH).reduced (3.0f, 3.0f);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (body.getIntersection (clip).toNearestInt());

        if (! b.audio)
        {
            if (auto it = previews.find (b.item.ref); it != previews.end() && it->second.lengthTicks > 0)
            {
                const auto& pv = it->second;
                g.setColour (colour::textDim.withAlpha (0.85f));

                // o bloco cortado ou deslizado começa no meio do padrão (patternOffset)
                const int offset = b.item.patternOffset;

                for (int rep = offset / pv.lengthTicks; rep * pv.lengthTicks < offset + b.item.length; ++rep)
                {
                    const int base = b.item.start - offset + rep * pv.lengthTicks;

                    if (tickToX (base) > clip.getRight())
                        break;

                    if (base > b.item.start)
                    {
                        g.setColour (colour::shade.withAlpha (0.6f));
                        g.fillRect (tickToX (base), body.getY(), 1.0f, body.getHeight());
                        g.setColour (colour::textDim.withAlpha (0.85f));
                    }

                    for (auto& m : pv.marks)
                    {
                        const int t = base + m.start;

                        if (t < b.item.start || t >= b.item.end())
                            continue;

                        const float x = tickToX (t);
                        const float w = juce::jmax (1.5f, (float) m.length * pxPerTick);
                        g.fillRect (x, body.getY() + m.y * body.getHeight(), w, juce::jmax (1.5f, m.h * body.getHeight() - 0.5f));
                    }
                }
            }
        }
        else if (auto it = thumbs.find (b.item.ref); it != thumbs.end() && it->second.thumbnail != nullptr
                                                      && it->second.thumbnail->getTotalLength() > 0.0)
        {
            // só o pedaço à vista: de perto, um clipe longo passa de 100 mil pixels de largura. Esticado, a onda
            // acompanha: cada segundo da playlist mostra 1 / stretch do arquivo.
            const auto shown = body.getIntersection (clip).toNearestInt();
            auto secondsAt = [&] (int x) { return b.item.offsetSeconds + b.item.fileSeconds (ticksToSeconds (xToTick ((float) x) - b.item.start)); };

            g.setColour (colour::textDim);

            if (! shown.isEmpty())
                it->second.thumbnail->drawChannels (g, shown, secondsAt (shown.getX()), secondsAt (shown.getRight()), 0.95f);

            // depois do fim do arquivo o bloco segue em silêncio, como uma janela: uma linha marca onde o áudio acaba
            const double fileEnd = b.item.start + secondsToTicks ((it->second.seconds - b.item.offsetSeconds) * b.item.stretch, bpm);

            if (it->second.seconds > 0.0 && fileEnd < b.item.end())
            {
                const float x = tickToX (fileEnd);
                g.setColour (colour::background.withAlpha (0.35f));
                g.fillRect (juce::Rectangle<float> (x, body.getY(), body.getRight() - x, body.getHeight()).getIntersection (clip));
                g.setColour (colour::textFaint);
                g.fillRect (x, body.getY(), 1.0f, body.getHeight());
            }
        }
        else
        {
            g.setColour (colour::textDim);
            g.setFont (font (12.0f));
            g.drawText (thumbs.count (b.item.ref) != 0 && ! thumbs.at (b.item.ref).file.existsAsFile() ? "arquivo não encontrado"_t : "carregando"_t,
                        body, juce::Justification::centredLeft, true);
        }
    }

    // fades: a sombra por cima da onda e a reta do volume; com o mouse no bloco, as alças
    if (b.audio && b.item.takeGroup.isEmpty() && ! b.takeRow)
    {
        const auto fadeBody = r.withTrimmedTop (headH);
        const float in = juce::jlimit (0.0f, fadeBody.getWidth(), (float) (secondsToTicks (b.item.fadeIn, bpm) * pxPerTick));
        const float out = juce::jlimit (0.0f, fadeBody.getWidth(), (float) (secondsToTicks (b.item.fadeOut, bpm) * pxPerTick));

        for (int side = 0; side < 2; ++side)
        {
            const float w = side == 0 ? in : out;

            if (w <= 0.5f)
                continue;

            const float x0 = side == 0 ? fadeBody.getX() : fadeBody.getRight(), x1 = side == 0 ? fadeBody.getX() + w : fadeBody.getRight() - w;
            juce::Path shade;
            shade.startNewSubPath (x0, fadeBody.getY());
            shade.lineTo (x1, fadeBody.getY());
            shade.lineTo (x0, fadeBody.getBottom());
            shade.closeSubPath();
            g.setColour (colour::shade.withAlpha (0.45f));
            g.fillPath (shade);
            g.setColour (colour::textDim);
            g.drawLine (x0, fadeBody.getBottom(), x1, fadeBody.getY(), 1.0f);
        }

        if (r.contains (hoverPos))
        {
            const auto h = handlesOf (b);
            g.setColour (colour::text);

            if (! h.fadeIn.isEmpty())
            {
                g.fillRect (h.fadeIn.withSizeKeepingCentre (6.0f, 6.0f).withY (r.getY() + 2.0f));
                g.fillRect (h.fadeOut.withSizeKeepingCentre (6.0f, 6.0f).withY (r.getY() + 2.0f));
                g.fillRoundedRectangle (h.gain.withSizeKeepingCentre (14.0f, 4.0f), 2.0f);
            }
        }
    }

    if (b.item.muted)
    {
        // mudo: o bloco apaga e ganha o aviso na cabeça
        g.setColour (colour::background.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (colour::textFaint);
        g.setFont (font (11.0f, Weight::medium));
        g.drawText (stretched ? "mudo  " + stretchText : juce::String ("mudo"), nameArea.getIntersection (clip),
                    juce::Justification::centredRight, false);
    }

    g.setColour (isSelected ? colour::text : colour::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, isSelected ? 1.5f : 1.0f);

    if (live)
    {
        g.setColour (colour::signal);
        g.fillRect (r.withHeight (2.5f).reduced (2.0f, 0.0f));
    }
}

void PlaylistGrid::paintTakePiece (juce::Graphics& g, const Block& b, juce::Rectangle<float> clip)
{
    // O que toca fica claro (raisedHigh, onda branca, a faixa do nome); os outros, apagados (sem fundo, onda
    // em textFaint). Com o mouse num apagado, o fundo acende pela metade: um clique escolhe ele.
    using namespace theme;
    const auto r = blockBounds (b).reduced (0.5f, 0.0f);
    const bool playing = ! b.item.muted;
    const bool over = ! playing && r.contains (hoverPos);

    if (playing || over)
    {
        g.setColour (colour::raisedHigh.withAlpha (playing ? 1.0f : 0.5f));
        g.fillRoundedRectangle (r, 3.0f);
    }

    float headH = 0.0f;

    if (playing)
    {
        headH = juce::jmin (11.0f, r.getHeight() * 0.35f);
        g.setColour (colour::grid::clipHead);
        g.fillRoundedRectangle (r.withHeight (headH), 3.0f);
        g.fillRect (r.withHeight (headH).withTrimmedTop (headH * 0.5f));
    }

    if (auto it = thumbs.find (b.item.ref); it != thumbs.end() && it->second.thumbnail != nullptr && it->second.thumbnail->getTotalLength() > 0.0)
    {
        const auto body = r.withTrimmedTop (headH).reduced (3.0f, 2.0f);
        const auto shown = body.getIntersection (clip).toNearestInt();
        auto secondsAt = [&] (int x) { return b.item.offsetSeconds + b.item.fileSeconds (ticksToSeconds (xToTick ((float) x) - b.item.start)); };

        if (! shown.isEmpty())
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (shown);
            g.setColour (playing ? colour::text : colour::textFaint.withAlpha (0.6f));
            it->second.thumbnail->drawChannels (g, shown, secondsAt (shown.getX()), secondsAt (shown.getRight()), 0.95f);
        }
    }

    g.setColour (colour::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
}

//==============================================================================
void PlaylistGrid::beginEdit (const juce::String& name)
{
    if (! editing)
    {
        editing = true;
        app.getCommands().beginGesture (name);
    }
}

void PlaylistGrid::endEdit()
{
    if (editing)
    {
        editing = false;
        app.getCommands().endGesture();
    }
}

void PlaylistGrid::startDrag (const PlaylistItem& item, Drag part)
{
    original = item;
    drag = part;
    lastStart = item.start;
    lastTrack = trackIndexOf (item.track);
    lastLength = item.length;
}

bool PlaylistGrid::placeBrush (juce::Point<float> pos)
{
    const int track = yToTrack (pos.y);
    const int start = snapDown (xToTick (pos.x));
    const auto params = brushKind == "audio"
        ? args ({ { "kind", "audio" }, { "ref", brushRef }, { "track", track + 1 }, { "start", start } })
        : args ({ { "kind", "pattern" }, { "ref", brushRef.isNotEmpty() ? brushRef : currentPattern }, { "track", track + 1 }, { "start", start } });

    const auto result = app.run ("playlist.addItem", params);

    if (! result.ok)
    {
        if (onStatus) onStatus (result.error, true);
        return false;
    }

    selectedItem = result.value["item"].toString();

    if (auto* item = modelItem (selectedItem))
        startDrag (*item, Drag::move);

    return true;
}

void PlaylistGrid::eraseAt (juce::Point<float> pos)
{
    Drag part;

    if (auto* b = blockAt (pos, part))
    {
        if (b->item.takeGroup.isNotEmpty())
        {
            // um trecho de um grupo de takes: sai o trecho inteiro, de todos os takes (senão o de baixo voltaria mudo)
            const auto group = b->item.takeGroup;
            const int from = b->item.start, to = b->item.end();
            juce::StringArray ids;

            for (auto& i : app.getModel().get().playlistItems)
                if (i.takeGroup == group && i.start < to && from < i.end())
                    ids.add (i.id);

            for (auto& id : ids)
                app.run ("playlist.removeItem", args ({ { "item", id } }));

            return;
        }

        const auto id = b->item.id;
        app.run ("playlist.removeItem", args ({ { "item", id } }));
    }
}

void PlaylistGrid::showLaneMenu (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) tracks.size()))
        return;

    const auto lane = tracks[(size_t) index];
    const auto* rec = app.recorder();
    const auto names = rec != nullptr ? rec->inputNames() : juce::StringArray();
    const auto& p = app.getModel().get();
    const auto* mixerTrack = p.findMixer (lane.recordTrack);
    const bool recordingHere = mixerTrack != nullptr && mixerTrack->armed;
    const int current = rec != nullptr ? rec->settings().firstInput : -1;
    const bool stereo = rec != nullptr && rec->settings().stereo;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Gravar nesta raia (escolher a entrada já arma)"_t);

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (100 + i, "Entrada "_t + juce::String (i + 1) + " (mono): " + names[i], true, recordingHere && ! stereo && current == i);

    for (int i = 0; i + 1 < names.size(); i += 2)
        menu.addItem (200 + i, "Entradas "_t + juce::String (i + 1) + " e " + juce::String (i + 2) + " (estéreo)"_t, true,
                      recordingHere && stereo && current == i);

    if (names.isEmpty())
        menu.addItem (1, "A placa não tem entradas abertas: escolha uma em Áudio e MIDI (F10)"_t, false);

    if (lane.recordTrack.isNotEmpty())
    {
        menu.addSeparator();
        menu.addItem (2, "Parar de gravar nesta raia"_t);
    }

    const auto area = localAreaToGlobal (juce::Rectangle<int> (0, (int) trackToY (index), headerWidth, trackHeight));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (area).withMinimumWidth (240),
                        [safe = juce::Component::SafePointer<PlaylistGrid> (this), id = lane.id] (int choice)
    {
        if (safe == nullptr || choice == 0)
            return;

        const auto result = choice == 2 ? safe->app.run ("playlist.setLaneInput", args ({ { "track", id }, { "first", 0 } }))
                                        : safe->app.run ("playlist.setLaneInput", args ({ { "track", id }, { "first", choice % 100 + 1 },
                                                                                          { "stereo", choice >= 200 } }));

        if (safe->onStatus)
        {
            if (! result.ok)
                safe->onStatus (result.error, true);
            else if (result.value["warning"].toString().isNotEmpty())
                safe->onStatus (result.value["warning"].toString(), true);
            else if (choice != 2)
                safe->onStatus ("Raia armada: com o REC, o play grava a entrada nela."_t, false);
        }
    });
}

void PlaylistGrid::renameTrack (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) tracks.size()))
        return;

    auto* w = new juce::AlertWindow ("Renomear raia", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", tracks[(size_t) index].name);
    w->addButton ("Renomear", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    const auto id = tracks[(size_t) index].id;

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, id] (int choice)
    {
        if (choice == 1)
        {
            const auto result = app.run ("playlist.renameTrack", args ({ { "track", id }, { "name", w->getTextEditorContents ("name") } }));

            if (! result.ok && onStatus)
                onStatus (result.error, true);
        }
    }), true);
}

//==============================================================================
void PlaylistGrid::setTool (Tool t)
{
    tool = t;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void PlaylistGrid::setTimeSelection (juce::Range<int> r)
{
    timeSelection = r.getLength() > 0 ? r : juce::Range<int>();
    app.setTimeSelection (timeSelection);   // a janela Exportar oferece o trecho
    repaint();
}

void PlaylistGrid::selectAll()
{
    selection.clear();

    for (auto& i : app.getModel().get().playlistItems)
        selection.insert (i.id);

    repaint();
}

const PlaylistItem* PlaylistGrid::modelItem (const juce::String& id) const
{
    for (auto& i : app.getModel().get().playlistItems)
        if (i.id == id)
            return &i;

    return nullptr;
}

std::vector<PlaylistItem> PlaylistGrid::selectedItems() const
{
    std::vector<PlaylistItem> out;

    for (auto& i : app.getModel().get().playlistItems)
        if (selection.count (i.id) != 0)
            out.push_back (i);

    return out;
}

juce::String PlaylistGrid::addCopy (const PlaylistItem& i, int start, int trackIndex)
{
    // a cópia leva tudo do bloco: o corte do áudio, o deslocamento no padrão, o mudo e o esticar
    const auto result = app.run ("playlist.addItem", args ({ { "kind", i.kind }, { "ref", i.ref }, { "track", trackIndex + 1 },
                                                             { "start", juce::jmax (0, start) }, { "length", i.length },
                                                             { "offsetSeconds", i.offsetSeconds }, { "patternOffset", i.patternOffset },
                                                             { "muted", i.muted }, { "stretch", i.stretch }, { "tape", i.tape } }));
    if (! result.ok && onStatus)
        onStatus (result.error, true);

    return result.ok ? result.value["item"].toString() : juce::String();
}

void PlaylistGrid::deleteSelection()
{
    const auto items = selectedItems();

    if (items.empty())
        return;

    app.getCommands().beginGesture (items.size() == 1 ? "Apagar bloco" : "Apagar blocos");

    for (auto& i : items)
        app.run ("playlist.removeItem", args ({ { "item", i.id } }));

    app.getCommands().endGesture();
    selection.clear();
    repaint();
}

void PlaylistGrid::copySelection()
{
    const auto items = selectedItems();

    if (items.empty())
        return;

    int minStart = std::numeric_limits<int>::max(), minTrack = std::numeric_limits<int>::max();

    for (auto& i : items)
    {
        minStart = juce::jmin (minStart, i.start);
        minTrack = juce::jmin (minTrack, trackIndexOf (i.track));
    }

    // no clipboard as posições ficam relativas: start a partir de 0, a raia no campo track como número
    clipboard.clear();

    for (auto i : items)
    {
        i.start -= minStart;
        i.track = juce::String (trackIndexOf (i.track) - minTrack);
        clipboard.push_back (i);
    }

    if (onStatus)
        onStatus (juce::String ((int) items.size()) + (items.size() == 1 ? " bloco copiado" : " blocos copiados"), false);
}

void PlaylistGrid::paste()
{
    if (clipboard.empty())
        return;

    // cola onde o mouse está; sem o mouse na grade, no começo da seleção de tempo ou da tela
    int at = snapDown (scrollTicks), track = juce::jmax (0, yToTrack ((float) gridArea().getY()));

    if (gridArea().toFloat().contains (hoverPos))
    {
        at = snapDown (xToTick (hoverPos.x));
        track = yToTrack (hoverPos.y);
    }
    else if (! timeSelection.isEmpty())
    {
        at = timeSelection.getStart();
    }

    app.getCommands().beginGesture ("Colar blocos");
    selection.clear();

    for (auto& c : clipboard)
    {
        const int t = juce::jlimit (0, juce::jmax (0, (int) tracks.size() - 1), track + c.track.getIntValue());
        const auto id = addCopy (c, at + c.start, t);

        if (id.isNotEmpty())
            selection.insert (id);
    }

    app.getCommands().endGesture();
    repaint();
}

void PlaylistGrid::duplicateSelection()
{
    // Ctrl+B, como no FL: a seleção (ou o que está dentro da seleção de tempo, ou tudo) repete
    // logo à direita; com seleção de tempo, o tamanho dela é o passo
    auto items = selectedItems();

    if (items.empty() && ! timeSelection.isEmpty())
        for (auto& i : app.getModel().get().playlistItems)
            if (i.start >= timeSelection.getStart() && i.end() <= timeSelection.getEnd())
                items.push_back (i);

    if (items.empty())
        items = app.getModel().get().playlistItems;

    if (items.empty())
        return;

    int minStart = std::numeric_limits<int>::max(), maxEnd = 0;

    for (auto& i : items)
    {
        minStart = juce::jmin (minStart, i.start);
        maxEnd = juce::jmax (maxEnd, i.end());
    }

    const int span = ! timeSelection.isEmpty() ? timeSelection.getLength() : maxEnd - minStart;
    app.getCommands().beginGesture ("Duplicar blocos");
    selection.clear();

    for (auto& i : items)
    {
        const auto id = addCopy (i, i.start + span, trackIndexOf (i.track));

        if (id.isNotEmpty())
            selection.insert (id);
    }

    app.getCommands().endGesture();

    if (! timeSelection.isEmpty())
        setTimeSelection (timeSelection + span);   // a seleção de tempo anda junto, para repetir de novo

    repaint();
}

void PlaylistGrid::moveSelectionBy (int deltaTicks, int deltaTracks)
{
    const auto items = selectedItems();

    if (items.empty())
        return;

    for (auto& i : items)
    {
        deltaTicks = juce::jmax (deltaTicks, -i.start);
        const int ti = trackIndexOf (i.track);
        deltaTracks = juce::jlimit (-ti, (int) tracks.size() - 1 - ti, deltaTracks);
    }

    if (deltaTicks == 0 && deltaTracks == 0)
        return;

    app.getCommands().beginGesture ("Mover blocos");

    for (auto& i : items)
        app.run ("playlist.updateItem", args ({ { "item", i.id }, { "start", i.start + deltaTicks },
                                                { "track", trackIndexOf (i.track) + deltaTracks + 1 } }));

    app.getCommands().endGesture();
}

void PlaylistGrid::sliceAt (juce::Point<float> pos)
{
    const int track = yToTrack (pos.y);

    if (! juce::isPositiveAndBelow (track, (int) tracks.size()))
        return;

    const auto trackId = tracks[(size_t) track].id;

    if (slicedTracks.count (trackId) != 0)
        return;

    juce::String id, group;

    for (auto& i : app.getModel().get().playlistItems)
    {
        if (i.track == trackId && sliceTick > i.start && sliceTick < i.end() && (i.takeGroup.isEmpty() || ! i.muted))
        {
            id = i.id;
            group = i.takeGroup;
        }
    }

    if (id.isEmpty())
        return;

    slicedTracks.insert (trackId);

    // takes: a tesoura corta todos os takes do grupo no mesmo ponto
    const auto result = group.isNotEmpty() ? app.run ("takes.cut", args ({ { "group", group }, { "at", sliceTick } }))
                                           : app.run ("playlist.splitItem", args ({ { "item", id }, { "at", sliceTick } }));

    if (! result.ok && onStatus)
        onStatus (result.error, true);
}

void PlaylistGrid::mouseMove (const juce::MouseEvent& e)
{
    hoverPos = e.position;
    updateCursor();

    if (tool == Tool::slice)
        repaint();

    // takes: o pedaço apagado e a pílula acendem com o mouse em cima
    juce::String over;

    if (const auto* piece = hoveredTakePiece())
        over = piece->id;

    for (auto& b : blocks)
        if (b.audio && ! b.takeRow && b.item.takeGroup.isEmpty() && blockBounds (b).contains (hoverPos))
            over = "audio:" + b.item.id;

    for (auto& b : blocks)
        if (const auto pill = pillBounds (b); ! pill.isEmpty() && pill.contains (hoverPos))
            over = "pill:" + b.item.id;

    if (over != hoverTake)
    {
        hoverTake = over;
        repaint();
    }
}

void PlaylistGrid::updateCursor()
{
    juce::MouseCursor newCursor = juce::MouseCursor::NormalCursor;

    if (punchEdgeAt (hoverPos) != PunchEdge::none)
        newCursor = juce::MouseCursor::LeftRightResizeCursor;

    if (gridArea().toFloat().contains (hoverPos))
    {
        Drag part;

        const auto* hit = blockAt (hoverPos, part);
        const bool onBlock = hit != nullptr;

        switch (tool)
        {
            case Tool::slice:   newCursor = juce::MouseCursor::IBeamCursor; break;
            case Tool::play:    newCursor = juce::MouseCursor::PointingHandCursor; break;
            case Tool::slip:    newCursor = onBlock ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor; break;
            case Tool::erase:
            case Tool::mute:    newCursor = onBlock ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor; break;
            case Tool::select:  newCursor = onBlock ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor; break;
            case Tool::draw:
            case Tool::paint:
                if (onBlock)
                    newCursor = part == Drag::move ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor;
                else
                    newCursor = juce::MouseCursor::CrosshairCursor;
                break;
        }

        // na borda de um áudio com o A segurado, o cursor de esticar (onde a borda aparar, ela estica)
        if (hit != nullptr && hit->audio && part != Drag::move && stretchHeld()
            && (tool == Tool::draw || tool == Tool::paint || tool == Tool::select))
            newCursor = stretchCursor;

        if (hit != nullptr && menuButtonBounds (*hit).contains (hoverPos)
            && (tool == Tool::draw || tool == Tool::paint || tool == Tool::select))
            newCursor = juce::MouseCursor::PointingHandCursor;

        // as alças do clipe: fades na horizontal, ganho na vertical
        if (hit != nullptr && (part == Drag::fadeIn || part == Drag::fadeOut) && (tool == Tool::draw || tool == Tool::paint || tool == Tool::select))
            newCursor = juce::MouseCursor::LeftRightResizeCursor;

        if (hit != nullptr && part == Drag::gain && (tool == Tool::draw || tool == Tool::paint || tool == Tool::select))
            newCursor = juce::MouseCursor::UpDownResizeCursor;

        // takes: a pílula abre o grupo, e um clique num pedaço apagado escolhe ele
        if (hit != nullptr && ((! pillBounds (*hit).isEmpty() && pillBounds (*hit).contains (hoverPos))
                               || (hit->takeRow && hit->item.muted && tool != Tool::slice && tool != Tool::erase)))
            newCursor = juce::MouseCursor::PointingHandCursor;
    }

    setMouseCursor (newCursor);
}

void PlaylistGrid::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto pos = e.position;
    dragStart = pos;
    drag = Drag::none;

    // régua: botão direito abre o punch; com o punch ligado, as pontas da faixa arrastam
    if (rulerArea().toFloat().contains (pos))
    {
        if (auto* m = markerAt (pos))
        {
            // marcador: o clique pula a agulha para ele; arrastar move; botão direito renomeia ou apaga
            if (e.mods.isPopupMenu())
            {
                showMarkerMenu (*m);
                return;
            }

            dragMarker = m->id;
            drag = Drag::marker;
            setTimeSelection ({});
            app.run ("transport.setPosition", args ({ { "at", m->t } }));

            if (playhead < 0.0)
                setNeedle (m->t);

            return;
        }

        if (e.mods.isPopupMenu())
        {
            showRulerMenu (xToTick (pos.x));
        }
        else if (const auto edge = punchEdgeAt (pos); edge != PunchEdge::none)
        {
            punchDrag = edge;
            dragIn = punchIn;
            dragOut = punchOut;
        }
        else if (e.mods.isCommandDown())
        {
            // Ctrl+arrastar na régua: seleção de tempo, como no FL
            drag = Drag::timeSelect;
            timeAnchor = e.mods.isAltDown() ? juce::jmax (0, juce::roundToInt (xToTick (pos.x))) : snapNearest (xToTick (pos.x));
            setTimeSelection ({});
        }
        else
        {
            // clique simples: a agulha vai para lá (o play e a gravação começam dela), como no FL; a
            // seleção de tempo sai, senão o play continuaria nela. Alt: sem a grade.
            const int at = e.mods.isAltDown() ? juce::jmax (0, juce::roundToInt (xToTick (pos.x))) : snapNearest (xToTick (pos.x));
            setTimeSelection ({});
            app.run ("transport.setPosition", args ({ { "at", at } }));

            if (playhead < 0.0)
                setNeedle (at);
        }

        return;
    }

    // cabeçalho da raia: botão direito escolhe a entrada da placa para gravar nela (já arma), como no FL
    if (headerArea().toFloat().contains (pos) && e.mods.isPopupMenu() && ! tracks.empty())
    {
        showLaneMenu (yToTrack (pos.y));
        return;
    }

    if (! gridArea().toFloat().contains (pos) || tracks.empty())
        return;

    if (e.mods.isPopupMenu())
    {
        beginEdit ("Apagar blocos");
        drag = Drag::erase;
        eraseAt (pos);
        return;
    }

    Drag part = Drag::move;
    const auto* hitPtr = blockAt (pos, part);
    const int tickHere = e.mods.isAltDown() ? juce::jmax (0, juce::roundToInt (xToTick (pos.x))) : snapNearest (xToTick (pos.x));

    // takes: a pílula "3 takes" abre e fecha o grupo; um pedaço apagado de um take passa a tocar com um clique
    if (hitPtr != nullptr && ! e.mods.isPopupMenu())
    {
        if (const auto pill = pillBounds (*hitPtr); ! pill.isEmpty() && pill.contains (pos))
        {
            setGroupOpen (hitPtr->item.takeGroup, ! isGroupOpen (hitPtr->item.takeGroup));
            return;
        }

        if (hitPtr->takeRow && tool != Tool::slice && tool != Tool::erase)
        {
            if (hitPtr->item.muted)
                if (const auto result = app.run ("takes.choose", args ({ { "item", hitPtr->item.id } })); ! result.ok && onStatus)
                    onStatus (result.error, true);

            return;
        }

        if (tool == Tool::mute && hitPtr->item.takeGroup.isNotEmpty())
        {
            if (onStatus)
                onStatus ("Nos takes, abra o grupo (\"" + juce::String (groupTakes[hitPtr->item.takeGroup]) + " takes\") e escolha o take de cada trecho."_t, false);

            return;
        }
    }

    // a setinha no cabeçalho do áudio abre o menu do bloco (fita, voltar ao ×1); a alça de um fade curto,
    // desenhada por cima dela, vence (o cursor já mostra o arrasto)
    if (hitPtr != nullptr && (tool == Tool::draw || tool == Tool::paint || tool == Tool::select)
        && part != Drag::fadeIn && part != Drag::fadeOut && menuButtonBounds (*hitPtr).contains (pos))
    {
        selectedItem = hitPtr->item.id;
        selection = { selectedItem };
        repaint();
        showBlockMenu (selectedItem);
        return;
    }

    switch (tool)
    {
        case Tool::erase:
            beginEdit ("Apagar blocos");
            drag = Drag::erase;
            eraseAt (pos);
            return;

        case Tool::slice:
            beginEdit ("Cortar blocos");
            drag = Drag::slice;
            sliceTick = tickHere;
            slicedTracks.clear();
            sliceAt (pos);
            repaint();
            return;

        case Tool::mute:
            if (hitPtr != nullptr)
            {
                beginEdit ("Mudo");
                drag = Drag::mute;
                muteTarget = ! hitPtr->item.muted;
                app.run ("playlist.updateItem", args ({ { "item", hitPtr->item.id }, { "muted", muteTarget } }));
            }
            return;

        case Tool::play:
            app.run ("transport.playFrom", args ({ { "at", tickHere } }));
            return;

        case Tool::slip:
            if (hitPtr != nullptr)
            {
                beginEdit ("Deslizar o conteúdo"_t);
                startDrag (hitPtr->item, Drag::slip);
            }
            return;

        case Tool::draw:
        case Tool::paint:
        case Tool::select:
            break;
    }

    // caixa de seleção: a ferramenta Selecionar no vazio, ou Ctrl+arrastar com o lápis e o pincel (como no FL)
    if (hitPtr == nullptr && (tool == Tool::select || e.mods.isCommandDown()))
    {
        drag = Drag::select;
        selectionBefore = e.mods.isShiftDown() ? selection : std::set<juce::String>();
        selection = selectionBefore;
        rubberBand = { pos, pos };
        repaint();
        return;
    }

    if (hitPtr != nullptr)
    {
        const auto hit = *hitPtr;

        // clicar num bloco faz dele o pincel: o próximo clique na grade coloca o mesmo
        selectedItem = hit.item.id;
        brushKind = hit.item.kind;
        brushRef = hit.item.ref;
        brushName = hit.name;
        if (onBrushChanged) onBrushChanged();

        // Shift+clique com a ferramenta Selecionar (ou Ctrl+clique) soma ou tira da seleção
        if ((tool == Tool::select && e.mods.isShiftDown()) || e.mods.isCommandDown())
        {
            if (selection.erase (hit.item.id) == 0)
                selection.insert (hit.item.id);

            repaint();
            return;
        }

        if (selection.count (hit.item.id) == 0)
            selection = { hit.item.id };

        // um pedaço do comp leva o grupo de takes inteiro junto: os takes andam alinhados
        if (hit.item.takeGroup.isNotEmpty())
            for (auto& i : app.getModel().get().playlistItems)
                if (i.takeGroup == hit.item.takeGroup)
                    selection.insert (i.id);

        // vários blocos selecionados: o arrasto leva todos juntos (com Shift, leva as cópias)
        if (selection.size() > 1 && part == Drag::move)
        {
            beginEdit (e.mods.isShiftDown() ? "Copiar blocos" : "Mover blocos");
            originals = selectedItems();

            if (e.mods.isShiftDown())
            {
                std::set<juce::String> copies;

                for (auto& o : originals)
                {
                    const auto id = addCopy (o, o.start, trackIndexOf (o.track));

                    if (id.isNotEmpty())
                        copies.insert (id);
                }

                selection = copies;
                originals = selectedItems();
            }

            original = hit.item;
            drag = Drag::moveMany;
            lastStart = 0;
            lastTrack = 0;
            repaint();
            return;
        }

        // segurar A na borda de um áudio: estica no tempo em vez de aparar (como no FL 2026)
        const bool stretching = hit.audio && (part == Drag::resize || part == Drag::trimStart) && stretchHeld();

        if (stretching)
            part = part == Drag::resize ? Drag::stretchEnd : Drag::stretchStart;

        beginEdit (part == Drag::move ? "Mover bloco" : stretching ? "Esticar áudio"_t
                   : part == Drag::gain ? "Ganho do bloco" : part == Drag::fadeIn || part == Drag::fadeOut ? "Fade do bloco" : "Aparar bloco");

        if (e.mods.isShiftDown() && part == Drag::move)
        {
            // Shift+arrasto: arrasta uma cópia (com o esticar do áudio)
            const auto result = app.run ("playlist.addItem", args ({ { "kind", hit.item.kind }, { "ref", hit.item.ref },
                                                                      { "track", hit.trackIndex + 1 }, { "start", hit.item.start },
                                                                      { "length", hit.item.length },
                                                                      { "stretch", hit.item.stretch }, { "tape", hit.item.tape } }));
            if (result.ok)
            {
                selectedItem = result.value["item"].toString();
                selection = { selectedItem };

                if (hit.audio && hit.item.offsetSeconds > 0.0)
                    app.run ("playlist.updateItem", args ({ { "item", selectedItem }, { "offsetSeconds", hit.item.offsetSeconds } }));

                if (hit.item.patternOffset > 0 || hit.item.muted)
                    app.run ("playlist.updateItem", args ({ { "item", selectedItem }, { "patternOffset", hit.item.patternOffset },
                                                            { "muted", hit.item.muted } }));
            }
        }

        if (auto* item = modelItem (selectedItem))
            startDrag (*item, part);

        repaint();
        return;
    }

    // no vazio: o lápis coloca um bloco; o pincel coloca e segue colocando enquanto arrasta
    selection.clear();
    beginEdit (tool == Tool::paint ? "Pintar blocos" : "Colocar na playlist");

    if (placeBrush (pos))
    {
        selection = { selectedItem };

        if (tool == Tool::paint)
            if (auto* item = modelItem (selectedItem))
            {
                drag = Drag::paint;
                paintTrack = yToTrack (pos.y);
                paintNext = item->end();
            }
    }

    repaint();
}

void PlaylistGrid::mouseDrag (const juce::MouseEvent& e)
{
    const auto pos = e.position;

    if (punchDrag != PunchEdge::none)
    {
        const int tick = e.mods.isAltDown() ? juce::jmax (0, juce::roundToInt (xToTick (pos.x))) : snapNearest (xToTick (pos.x));

        if (punchDrag == PunchEdge::in)
            dragIn = juce::jlimit (0, juce::jmax (0, dragOut - ticksPerStep), tick);
        else
            dragOut = juce::jmax (dragIn + ticksPerStep, tick);

        repaint();
        return;
    }

    const double raw = xToTick (pos.x) - xToTick (dragStart.x);
    auto snapped = [&] (double tick) { return e.mods.isAltDown() ? juce::jmax (0, juce::roundToInt (tick)) : snapNearest (tick); };

    switch (drag)
    {
        case Drag::erase:
            eraseAt (pos);
            break;

        case Drag::timeSelect:
            setTimeSelection (juce::Range<int>::between (timeAnchor, snapped (xToTick (pos.x))));
            break;

        case Drag::marker:
        {
            if (pos.getDistanceFrom (dragStart) < 4.0f)
                break;

            beginEdit ("Mover marcador"_t);
            const int at = juce::jmax (0, snapped (xToTick (pos.x)));

            if (auto* m = app.getModel().get().findMarker (dragMarker); m != nullptr && m->t != at)
                app.run ("playlist.updateMarker", args ({ { "marker", dragMarker }, { "at", at } }));

            break;
        }

        case Drag::select:
        {
            rubberBand = juce::Rectangle<float> (dragStart, pos);
            selection = selectionBefore;

            for (auto& b : blocks)
                if (blockBounds (b).intersects (rubberBand))
                    selection.insert (b.item.id);

            repaint();
            break;
        }

        case Drag::slice:
            sliceAt (pos);
            break;

        case Drag::mute:
        {
            Drag part;

            if (auto* b = blockAt (pos, part); b != nullptr && b->item.muted != muteTarget)
                app.run ("playlist.updateItem", args ({ { "item", b->item.id }, { "muted", muteTarget } }));
            break;
        }

        case Drag::slip:
        {
            // o conteúdo anda com o mouse e as bordas ficam: arrastar para a direita mostra o que vinha antes
            const int moved = snapped (original.start + raw) - original.start;

            if (moved == lastStart - original.start)
                break;

            lastStart = original.start + moved;

            if (original.kind == "audio")   // esticado, o arquivo anda 1 / stretch do que o mouse andou
                app.run ("playlist.updateItem", args ({ { "item", original.id },
                                                        { "offsetSeconds", juce::jmax (0.0, original.offsetSeconds - original.fileSeconds (ticksToSeconds (moved))) } }));
            else
                app.run ("playlist.updateItem", args ({ { "item", original.id },
                                                        { "patternOffset", juce::jmax (0, original.patternOffset - moved) } }));
            break;
        }

        case Drag::paint:
        {
            // o pincel enche a raia: um bloco atrás do outro enquanto o mouse anda para a direita
            const int at = snapDown (xToTick (pos.x));

            while (paintNext <= at)
            {
                const auto result = app.run ("playlist.addItem", args ({ { "kind", brushKind }, { "ref", brushRef.isNotEmpty() ? brushRef : currentPattern },
                                                                          { "track", paintTrack + 1 }, { "start", paintNext } }));
                const auto* item = result.ok ? modelItem (result.value["item"].toString()) : nullptr;

                if (item == nullptr)
                    break;

                selection.insert (item->id);
                paintNext = item->end();
            }
            break;
        }

        case Drag::moveMany:
        {
            int dt = snapped (original.start + raw) - original.start;
            int dTrack = yToTrack (pos.y) - yToTrack (dragStart.y);

            for (auto& o : originals)   // nenhum bloco sai da grade
            {
                dt = juce::jmax (dt, -o.start);
                const int ti = trackIndexOf (o.track);
                dTrack = juce::jlimit (-ti, (int) tracks.size() - 1 - ti, dTrack);
            }

            if (dt == lastStart && dTrack == lastTrack)
                break;

            lastStart = dt;
            lastTrack = dTrack;

            for (auto& o : originals)
                app.run ("playlist.updateItem", args ({ { "item", o.id }, { "start", o.start + dt },
                                                        { "track", trackIndexOf (o.track) + dTrack + 1 } }));
            break;
        }

        case Drag::move:
        {
            const int start = snapped (original.start + raw);
            const int startTrack = trackIndexOf (original.track);
            const int track = juce::jlimit (0, juce::jmax (0, (int) tracks.size() - 1),
                                            startTrack + yToTrack (pos.y) - yToTrack (dragStart.y));

            if (start == lastStart && track == lastTrack)
                break;

            lastStart = start;
            lastTrack = track;
            app.run ("playlist.updateItem", args ({ { "item", original.id }, { "start", start }, { "track", track + 1 } }));
            break;
        }

        case Drag::fadeIn:
        case Drag::fadeOut:
        {
            // o fade segue o mouse, em segundos da playlist (o arquivo não muda)
            const double ticks = drag == Drag::fadeIn ? xToTick (pos.x) - original.start : original.end() - xToTick (pos.x);
            app.run ("audio.setFades", args ({ { "item", original.id }, { drag == Drag::fadeIn ? "in" : "out", juce::jmax (0.0, ticksToSeconds (ticks)) } }));
            break;
        }

        case Drag::gain:
        {
            // para cima sobe: 0,2 dB por pixel; com Shift, 0,05 (o ajuste fino)
            const float perPixel = e.mods.isShiftDown() ? 0.05f : 0.2f;
            app.run ("audio.setGain", args ({ { "item", original.id }, { "db", clampClipGain (original.gainDb + (dragStart.y - pos.y) * perPixel) } }));
            break;
        }

        case Drag::resize:
        {
            const int snap = getSnapTicks();
            const int minLength = snap > 1 ? juce::jlimit (ticksPerStep, ticksPerBeat, snap) : ticksPerStep;
            // o áudio pode passar do fim do arquivo: o resto do bloco fica em silêncio, como uma janela
            const int length = juce::jmax (minLength, snapped (original.end() + raw) - original.start);

            if (length == lastLength)
                break;

            lastLength = length;
            app.run ("playlist.updateItem", args ({ { "item", original.id }, { "length", length } }));
            break;
        }

        case Drag::trimStart:
        {
            // aparar o começo do áudio: o bloco encolhe pela esquerda e o arquivo anda junto (esticado, o começo
            // do arquivo fica stretch vezes mais longe na grade)
            int start = snapped (original.start + raw);
            const int fileStart = original.start - juce::roundToInt (offsetTicks (original) * original.stretch);
            start = juce::jlimit (juce::jmax (0, fileStart), original.end() - ticksPerStep, start);

            if (start == lastStart)
                break;

            // o corte anda em segundos do arquivo: o que o bloco andou na grade, no BPM de agora
            lastStart = start;
            const double cut = juce::jmax (0.0, original.offsetSeconds + original.fileSeconds (ticksToSeconds (start - original.start)));
            app.run ("playlist.updateItem", args ({ { "item", original.id }, { "start", start },
                                                    { "length", original.end() - start },
                                                    { "offsetSeconds", cut } }));
            break;
        }

        case Drag::stretchEnd:
        case Drag::stretchStart:
        {
            // Segurar A e arrastar a borda: a duração e o esticar mudam juntos, e o bloco mostra o mesmo pedaço do
            // arquivo. Pela direita o começo fica parado; pela esquerda, o fim. A grade vale como no aparar (Alt solta).
            const int snap = getSnapTicks();
            const int minLength = snap > 1 ? juce::jlimit (ticksPerStep, ticksPerBeat, snap) : ticksPerStep;
            const int shortest = juce::jmax (minLength, (int) std::ceil (original.length * minStretch / original.stretch));
            const int longest = juce::jmax (shortest, (int) std::floor (original.length * maxStretch / original.stretch));
            const bool fromEnd = drag == Drag::stretchEnd;

            int length = fromEnd ? snapped (original.end() + raw) - original.start
                                 : original.end() - snapped (original.start + raw);
            length = juce::jlimit (shortest, juce::jmax (shortest, fromEnd ? longest : juce::jmin (longest, original.end())), length);

            if (length == lastLength)
                break;

            lastLength = length;
            const double stretch = clampStretch (original.stretch * length / (double) original.length);
            app.run ("playlist.updateItem", args ({ { "item", original.id },
                                                    { "start", fromEnd ? original.start : juce::jmax (0, original.end() - length) },
                                                    { "length", length }, { "stretch", stretch } }));

            if (onStatus)
                onStatus ("Esticar no tempo: "_t + stretchLabel (stretch) + (original.tape ? " (fita: o tom vai junto)" : " (o tom fica)"), false);
            break;
        }

        case Drag::none:
            break;
    }
}

void PlaylistGrid::mouseUp (const juce::MouseEvent&)
{
    if (punchDrag != PunchEdge::none)
    {
        punchDrag = PunchEdge::none;

        if (dragIn != punchIn || dragOut != punchOut)
        {
            const auto result = app.run ("record.setPunch", args ({ { "start", dragIn }, { "end", dragOut }, { "on", true } }));
            if (! result.ok && onStatus) onStatus (result.error, true);
        }

        repaint();
        return;
    }

    drag = Drag::none;
    rubberBand = {};
    originals.clear();
    endEdit();
    repaint();
}

//==============================================================================
// Marcadores na régua (bloco 1, 22/10)
juce::Rectangle<float> PlaylistGrid::markerBounds (const Marker& m) const
{
    const float w = juce::jlimit (24.0f, 140.0f, juce::GlyphArrangement::getStringWidth (theme::font (11.0f, theme::Weight::semibold), m.name) + 12.0f);
    return { tickToX (m.t), (float) rulerArea().getY() + 1.0f, w, 14.0f };
}

const Marker* PlaylistGrid::markerAt (juce::Point<float> p) const
{
    if (! rulerArea().toFloat().contains (p))
        return nullptr;

    for (auto it = markers.rbegin(); it != markers.rend(); ++it)   // o de cima (o mais à direita) ganha
        if (markerBounds (*it).expanded (1.0f, 2.0f).contains (p))
            return &*it;

    return nullptr;
}

void PlaylistGrid::addMarkerMenu (juce::PopupMenu& menu, int at)
{
    // as partes de um beat; "Outro nome..." abre a caixa de texto
    juce::PopupMenu names;
    juce::Component::SafePointer<PlaylistGrid> safe (this);

    auto add = [safe, at] (const juce::String& name)
    {
        return [safe, at, name]
        {
            if (safe == nullptr)
                return;

            const auto result = safe->app.run ("playlist.addMarker", args ({ { "at", at }, { "name", name } }));

            if (! result.ok && safe->onStatus)
                safe->onStatus (result.error, true);
        };
    };

    // ids a partir de 100: o menu da régua usa de 1 a 4 para o punch
    int id = 100;

    for (auto* name : { "Intro", "Verso", "Pré-refrão", "Refrão", "Ponte", "Drop", "Final", "Outro" })
        addAction (names, id++, juce::String::fromUTF8 (name), add (juce::String::fromUTF8 (name)));

    names.addSeparator();
    addAction (names, id, "Outro nome..."_t, [safe, at]
    {
        if (safe == nullptr)
            return;

        const auto result = safe->app.run ("playlist.addMarker", args ({ { "at", at } }));

        if (result.ok)
            safe->renameMarker (result.value["id"].toString(), {});
    });

    menu.addSectionHeader ("Marcador: as partes da música"_t);
    menu.addSubMenu ("Marcador aqui ("_t + positionName (at) + ")", names);
}

void PlaylistGrid::showMarkerMenu (const Marker& m)
{
    juce::PopupMenu menu;
    const auto id = m.id, name = m.name;
    juce::Component::SafePointer<PlaylistGrid> safe (this);

    menu.addSectionHeader (name + " (" + positionName (m.t) + ")");
    addAction (menu, 1, "Selecionar o trecho até o próximo (loop e exportação)"_t, [safe, id] { if (safe != nullptr) safe->selectMarkerRange (id); });
    addAction (menu, 2, "Renomear..."_t, [safe, id, name] { if (safe != nullptr) safe->renameMarker (id, name); });
    addAction (menu, 3, "Apagar o marcador"_t, [safe, id]
    {
        if (safe != nullptr)
            safe->app.run ("playlist.removeMarker", args ({ { "marker", id } }));
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (markerBounds (m).toNearestInt())));
}

void PlaylistGrid::renameMarker (const juce::String& id, const juce::String& current)
{
    auto* w = new juce::AlertWindow ("Nome do marcador"_t, "Intro, verso, refrão, ponte..."_t, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", current);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    auto& controller = app;   // a playlist pode ser refeita com o diálogo aberto: guarda só o app e o id

    w->enterModalState (true, juce::ModalCallbackFunction::create ([&controller, id, w] (int choice)
    {
        const auto name = w->getTextEditorContents ("name").trim();

        if (choice == 1 && name.isNotEmpty())
            controller.run ("playlist.updateMarker", args ({ { "marker", id }, { "name", name } }));
    }), true);
}

void PlaylistGrid::selectMarkerRange (const juce::String& id)
{
    const auto& p = app.getModel().get();

    if (auto* m = p.findMarker (id))
    {
        const int end = p.markerEnd (*m);
        setTimeSelection ({ m->t, end });
        app.run ("transport.setPosition", args ({ { "at", m->t } }));

        if (onStatus)
            onStatus ("Trecho "_t + m->name + ": de " + positionName (m->t) + " a "_t + positionName (end)
                          + ". O play toca em loop, e Exportar oferece só ele."_t, false);
    }
}

PlaylistGrid::PunchEdge PlaylistGrid::punchEdgeAt (juce::Point<float> p) const
{
    if (! punchOn || ! rulerArea().toFloat().contains (p))
        return PunchEdge::none;

    if (std::abs (p.x - tickToX (punchIn)) <= 5.0f)    return PunchEdge::in;
    if (std::abs (p.x - tickToX (punchOut)) <= 5.0f)   return PunchEdge::out;
    return PunchEdge::none;
}

juce::String PlaylistGrid::positionName (int tick) const
{
    // "5" no começo do compasso, "5.3" num tempo, "5.3.2" num passo
    const int barTicks = stepsPerBar * ticksPerStep, beatTicks = stepsPerBeat * ticksPerStep;
    const int bar = tick / barTicks + 1, beat = (tick % barTicks) / beatTicks + 1, step = (tick % beatTicks) / ticksPerStep + 1;

    if (tick % barTicks == 0)    return juce::String (bar);
    if (tick % beatTicks == 0)   return juce::String (bar) + "." + juce::String (beat);
    return juce::String (bar) + "." + juce::String (beat) + "." + juce::String (step);
}

void PlaylistGrid::showRulerMenu (double tick)
{
    // punch in/out: com o REC, só fica o que for gravado dentro da faixa vermelha
    const int barTicks = stepsPerBar * ticksPerStep;
    const int at = juce::jmax (0, snapNearest (tick));
    const int barStart = juce::jmax (0, (int) std::floor (tick / barTicks)) * barTicks;

    juce::PopupMenu menu;
    addMarkerMenu (menu, at);
    menu.addSeparator();

    // reamostrar (bloco 3): a música, ou o trecho selecionado, vira um áudio no fim (ids 200 em diante)
    const auto range = timeSelection;
    juce::Component::SafePointer<PlaylistGrid> safeGrid (this);
    addAction (menu, 200, range.isEmpty() ? "Reamostrar a música: vira um áudio no fim, para picotar"_t
                                              : "Reamostrar o trecho selecionado: vira um áudio no fim"_t,
               [safeGrid, range]
               {
                   if (safeGrid == nullptr)
                       return;

                   juce::MouseCursor::showWaitCursor();
                   const auto result = range.isEmpty() ? safeGrid->app.run ("project.resample")
                                                           : safeGrid->app.run ("project.resample", args ({ { "from", range.getStart() },
                                                                                                            { "to", range.getEnd() } }));
                   juce::MouseCursor::hideWaitCursor();

                   if (safeGrid->onStatus)
                       safeGrid->onStatus (result.ok ? "Reamostrado: o áudio está no fim da música, numa raia vazia (Ctrl+Z desfaz)."_t : result.error,
                                           ! result.ok);
               });
    menu.addSeparator();
    menu.addSectionHeader ("Punch: gravar só um trecho"_t);
    menu.addItem (1, "Só o compasso "_t + positionName (barStart));
    menu.addItem (2, "Punch in aqui ("_t + positionName (at) + ")");
    menu.addItem (3, "Punch out aqui ("_t + positionName (juce::jmax (ticksPerStep, at)) + ")");
    menu.addSeparator();
    menu.addItem (4, punchOn ? "Desligar o punch"_t
                             : "Ligar o punch ("_t + positionName (punchIn) + " a "_t + positionName (punchOut) + ")",
                  punchOn || punchOut > punchIn);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> ((int) tickToX (tick), 0, 1, rulerHeight))),
                        [this, at, barStart, barTicks] (int choice)
                        {
                            juce::var a;

                            if (choice == 1)
                                a = args ({ { "start", barStart }, { "end", barStart + barTicks }, { "on", true } });
                            else if (choice == 2)
                                a = args ({ { "start", at }, { "end", punchOut > at + ticksPerStep ? punchOut : at + barTicks }, { "on", true } });
                            else if (choice == 3)
                            {
                                const int end = juce::jmax (ticksPerStep, at);
                                a = args ({ { "start", punchIn <= end - ticksPerStep ? punchIn : juce::jmax (0, end - barTicks) }, { "end", end }, { "on", true } });
                            }
                            else if (choice == 4)
                                a = args ({ { "on", ! punchOn } });
                            else
                                return;

                            const auto result = app.run ("record.setPunch", a);
                            if (! result.ok && onStatus) onStatus (result.error, true);
                        });
}

void PlaylistGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    // duplo clique no marcador: o trecho dele (até o próximo) vira a seleção de tempo, como no FL
    if (auto* m = markerAt (e.position))
    {
        selectMarkerRange (m->id);
        return;
    }

    if (rulerArea().toFloat().contains (e.position) && ! timeSelection.isEmpty())
    {
        setTimeSelection ({});
        return;
    }

    if (headerArea().toFloat().contains (e.position))
    {
        renameTrack (yToTrack (e.position.y));
        return;
    }

    Drag part;

    if (auto* b = blockAt (e.position, part); b != nullptr && ! b->audio && onOpenPattern)
        onOpenPattern (b->item.ref);
}

void PlaylistGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
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
        trackHeight = juce::jlimit (30, 120, trackHeight + juce::roundToInt (notches * 4.0f));
        layoutRows();
    }
    else if (e.mods.isShiftDown() || w.deltaX != 0.0f)
    {
        scrollTicks -= notches * 80.0f / pxPerTick;
    }
    else
    {
        scrollY -= notches * 40.0f;
    }

    clampScroll();
    updateScrollBars();
    repaint();
}

bool PlaylistGrid::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const auto mods = key.getModifiers();
    const auto ctrl = juce::ModifierKeys::commandModifier;

    if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && ! selection.empty())
    {
        deleteSelection();
        return true;
    }

    if (key == juce::KeyPress ('a', ctrl, 0))   { selectAll(); return true; }
    if (key == juce::KeyPress ('c', ctrl, 0))   { copySelection(); return true; }
    if (key == juce::KeyPress ('x', ctrl, 0))   { copySelection(); deleteSelection(); return true; }
    if (key == juce::KeyPress ('v', ctrl, 0))   { paste(); return true; }
    if (key == juce::KeyPress ('b', ctrl, 0))   { duplicateSelection(); return true; }

    // Segurar A: a borda do áudio estica no tempo (como no FL 2026). Vale com o teclado-piano ligado,
    // porque o A não é nota. Solta no keyStateChanged.
    if (key == juce::KeyPress ('a'))
    {
        if (! stretchKey)
        {
            stretchKey = true;
            updateCursor();
        }

        return true;
    }

    if (code == juce::KeyPress::escapeKey && (! selection.empty() || ! timeSelection.isEmpty()))
    {
        selection.clear();
        setTimeSelection ({});
        return true;
    }

    // Shift+setas: a seleção anda pela grade (para os lados) ou de raia (para cima e para baixo)
    if (mods.isShiftDown() && ! mods.isCommandDown() && ! selection.empty())
    {
        const int step = juce::jmax (ticksPerStep, getSnapTicks());

        if (code == juce::KeyPress::leftKey)    { moveSelectionBy (-step, 0); return true; }
        if (code == juce::KeyPress::rightKey)   { moveSelectionBy (step, 0); return true; }
        if (code == juce::KeyPress::upKey)      { moveSelectionBy (0, -1); return true; }
        if (code == juce::KeyPress::downKey)    { moveSelectionBy (0, 1); return true; }
    }

    if (! singleKeyShortcuts)
        return false;   // teclado-piano ligado: a letra vira nota (no MainComponent)

    const std::pair<juce::juce_wchar, Tool> keys[] = { { 'p', Tool::draw }, { 'b', Tool::paint }, { 'c', Tool::slice }, { 'd', Tool::erase },
                                                       { 'e', Tool::select }, { 't', Tool::mute }, { 's', Tool::slip }, { 'y', Tool::play } };

    for (auto& [letter, t] : keys)
    {
        if (key == juce::KeyPress ((int) letter))
        {
            setTool (t);
            if (onToolChanged) onToolChanged();
            return true;
        }
    }

    return false;
}

bool PlaylistGrid::keyStateChanged (bool)
{
    if (stretchKey && ! juce::KeyPress::isKeyCurrentlyDown ('a'))
    {
        stretchKey = false;
        updateCursor();
    }

    return false;   // o teclado-piano, no MainComponent, também acompanha as teclas
}

void PlaylistGrid::focusLost (FocusChangeType)
{
    stretchKey = false;   // o A solto fora da playlist não chega aqui
}

juce::String PlaylistGrid::getTooltip()
{
    if (headerArea().toFloat().contains (hoverPos))
        return "Duplo clique renomeia a raia. Botão direito: gravar nesta raia (escolher a entrada já arma)."_t;

    if (rulerArea().toFloat().contains (hoverPos))
    {
        if (auto* m = markerAt (hoverPos))
            return m->name + " (" + positionName (m->t) + "): clique leva a agulha para cá; duplo clique seleciona o trecho até o "
                   "próximo marcador (loop e exportação); arraste para mover; botão direito renomeia ou apaga."_t;

        if (punchOn)
            return "Punch: com o REC, grava só de "_t + positionName (punchIn) + " a "_t + positionName (punchOut)
                   + ". Arraste as pontas da faixa vermelha; botão direito muda ou desliga."_t;

        if (! timeSelection.isEmpty())
            return "Trecho de "_t + positionName (timeSelection.getStart()) + " a " + positionName (timeSelection.getEnd())
                   + ": o loop e o começo do play. Gravando com o loop, cada volta vira um take. Ctrl+B repete, e Exportar oferece só ele."
                     " Duplo clique limpa."_t;

        return "Clique: o play e a gravação começam daqui. Ctrl+arrastar: seleciona um trecho. Botão direito: marcador (intro, refrão...) "
               "e punch. Alt+T: marcador na agulha. Rodinha: zoom."_t;
    }

    if (! gridArea().toFloat().contains (hoverPos))
        return {};

    Drag part;

    if (auto* b = blockAt (hoverPos, part))
    {
        if (const auto pill = pillBounds (*b); ! pill.isEmpty() && pill.contains (hoverPos))
            return juce::String (groupTakes[b->item.takeGroup]) + " takes: clique para abrir e escolher o take de cada trecho"_t;

        if (part == Drag::fadeIn || part == Drag::fadeOut)
            return "Arraste para o fade "_t + (part == Drag::fadeIn ? "de entrada"_t : "de saída"_t) + " (agora "
                   + juce::String (part == Drag::fadeIn ? b->item.fadeIn : b->item.fadeOut, 2).replaceCharacter ('.', ',') + " s)";

        if (part == Drag::gain)
            return "Arraste para cima ou para baixo: o ganho do bloco (agora "_t + juce::String (b->item.gainDb, 1).replaceCharacter ('.', ',')
                   + " dB). Com Shift, mais fino."_t;

        if (b->takeRow)
            return b->item.muted ? "Usar o Take "_t + juce::String (b->item.take) + " neste trecho (clique)"_t
                                 : "O Take "_t + juce::String (b->item.take) + " toca neste trecho"_t;

        if (b->item.takeGroup.isNotEmpty())
            return "Take "_t + juce::String (b->item.take) + " de " + juce::String (groupTakes[b->item.takeGroup])
                   + ": arraste para mover os takes juntos. Cortar (C) corta todos no mesmo ponto; \"takes\" abre o grupo."_t;

        if (b->audio)
            return b->name + (b->item.stretch != 1.0 ? " (" + stretchLabel (b->item.stretch) + ")" : juce::String())
                   + ": arraste as bordas para aparar; segure A para esticar no tempo. Shift+arraste copia, "
                     "botão direito apaga, a setinha abre o menu do bloco."_t;

        return b->name + ": duplo clique abre o padrão. Borda direita muda a duração, Shift+arraste copia."_t;
    }

    return "Clique coloca \""_t + brushName + "\". Ctrl+arrastar seleciona, botão direito apaga, Ctrl+roda faz zoom. "
           "Ferramentas: P B C D E T S Y."_t;
}

//==============================================================================
bool PlaylistGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (f))
            return true;

    return false;
}

void PlaylistGrid::fileDragEnter (const juce::StringArray&, int x, int y)
{
    dragOver = true;
    dropPos = { x, y };
    repaint();
}

void PlaylistGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    dropPos = { x, y };
    repaint();
}

void PlaylistGrid::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

namespace
{
    juce::String draggedSample (const juce::DragAndDropTarget::SourceDetails& details)
    {
        const auto text = details.description.toString();
        return text.startsWith (SampleBrowser::dragPrefix) ? text.fromFirstOccurrenceOf (SampleBrowser::dragPrefix, false, false)
                                                           : juce::String();
    }
}

bool PlaylistGrid::isInterestedInDragSource (const SourceDetails& details)
{
    const auto path = draggedSample (details);
    return path.isNotEmpty() && isAudioFile (path);
}

void PlaylistGrid::itemDragEnter (const SourceDetails& details)
{
    fileDragEnter ({ draggedSample (details) }, details.localPosition.x, details.localPosition.y);
}

void PlaylistGrid::itemDragMove (const SourceDetails& details)
{
    fileDragMove ({ draggedSample (details) }, details.localPosition.x, details.localPosition.y);
}

void PlaylistGrid::itemDragExit (const SourceDetails&)
{
    fileDragExit ({});
}

void PlaylistGrid::itemDropped (const SourceDetails& details)
{
    filesDropped ({ draggedSample (details) }, details.localPosition.x, details.localPosition.y);
}

void PlaylistGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragOver = false;
    int track = yToTrack ((float) y);
    const int start = snapDown (xToTick ((float) x));
    int placed = 0;

    CommandRegistry::ScopedGesture gesture (app.getCommands(), "Colocar áudio na playlist"_t);

    for (auto& f : files)
    {
        if (! isAudioFile (f))
            continue;

        const auto result = app.run ("playlist.addItem", args ({ { "path", f }, { "track", track + 1 }, { "start", start } }));

        if (! result.ok)
        {
            if (onStatus) onStatus (result.error, true);
            continue;
        }

        ++placed;
        selectedItem = result.value["item"].toString();
        brushKind = "audio";
        brushRef = result.value["ref"].toString();
        track = juce::jmin (track + 1, juce::jmax (0, (int) tracks.size() - 1));
    }

    // O áudio da playlist só toca no SONG (revisão de iniciante, 22/10): no PAT, o Espaço tocaria o padrão vazio.
    // Passa para o SONG no mesmo passo de desfazer.
    const bool toSong = placed > 0 && ! app.getModel().get().songMode;

    if (toSong)
        app.run ("transport.setSongMode", args ({ { "on", true } }));

    const auto songNote = toSong ? juce::String (" Passei para SONG: o Espaço toca a playlist."_t) : juce::String();

    if (placed > 0 && onStatus)
        onStatus ((placed == 1 ? "Clipe de áudio colocado na playlist."_t : juce::String (placed) + " clipes de áudio colocados na playlist."_t)
                      + songNote, false);

    // o beat em um gesto (gravar voz): no projeto vazio, o BPM vai para o do beat (pelo nome do arquivo ou pela
    // análise), no mesmo passo de desfazer; com mais coisa na playlist, só diz o andamento dele
    if (placed == 1)
    {
        const double before = app.getModel().get().bpm;
        const auto matched = app.run ("audio.matchBpm", args ({ { "item", selectedItem }, { "auto", true } }));
        const double bpmFound = matched.ok ? (double) matched.value["bpm"] : 0.0;
        const auto how = matched.value["source"].toString() == "nome" ? "pelo nome do arquivo"_t : "pela análise"_t;

        if (matched.ok && (bool) matched.value["applied"] && onStatus)
            onStatus ("Beat em "_t + juce::String (bpmFound, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BPM (" + how
                          + "): o projeto foi para esse andamento. Errou o dobro ou a metade? A setinha do bloco acerta."_t + songNote, false);
        else if (matched.ok)
        {
            // um loop (até 32 s) fora do tempo do projeto entra no BPM dele sem mudar o tom, como o warp do Ableton
            // (bloco 3); um beat inteiro só avisa, como antes
            const auto warped = app.run ("audio.warpToProject", args ({ { "item", selectedItem }, { "auto", true } }));

            if (warped.ok && (bool) warped.value["applied"] && onStatus)
                onStatus ("O loop estava em "_t + juce::String ((double) warped.value["bpm"], 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                              + " BPM (" + how + ") e já toca nos "_t + juce::String (before, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                              + " do projeto, com o mesmo tom. A setinha do bloco volta ao tempo do arquivo."_t + songNote, false);
            else if (std::abs (bpmFound - before) > 0.01 && onStatus)
                onStatus ("O áudio parece estar em "_t + juce::String (bpmFound, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BPM ("
                              + how + "); o projeto está em "_t + juce::String (before, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                              + ". A setinha do bloco acerta."_t + songNote, false);
        }
    }

    refresh (app.getModel().get());
}

//==============================================================================
Playlist::Playlist (AppController& a) : app (a), grid (a), lanes (a)
{
    for (int i = 0; i < (int) std::size (snapOptions); ++i)
    {
        if (i == barOption || i == noGridOption)
            snapBox.addSeparator();

        if (i == firstTripletOption)
            snapBox.addSectionHeading ("Tercinas");

        snapBox.addItem (snapOptions[i].name, i + 1);
    }

    // abre na Auto (pedido do usuário, 21/10): no compasso, o bloco pulava 96 px e o vocal não
    // saía do lugar do compasso; a Auto encaixa no tempo e fica mais fina com o zoom, como a Line do FL
    snapBox.setSelectedId (1, juce::dontSendNotification);
    theme::styleComboBox (snapBox, Icon::magnet);
    snapBox.setTooltip ("Grade: onde os blocos encaixam. Na Auto, ela segue o zoom (Ctrl+rodinha, ou a rodinha em cima da régua): "
                        "de perto fica mais fina, até 1/16 de passo. Segure Alt enquanto arrasta para ignorar."_t);
    snapBox.onChange = [this]
    {
        const int index = juce::jlimit (0, (int) std::size (snapOptions) - 1, snapBox.getSelectedId() - 1);
        grid.setSnap (snapOptions[(size_t) index].ticks);
        updateSnapLabel();
    };
    snapBox.setWantsKeyboardFocus (false);

    toolSwitch.setItems ({
        { "Desenhar", Icon::pencil,  "Desenhar (P): clique coloca o bloco escolhido, arrastar move. Ctrl+arrastar seleciona."_t },
        { "Pintar",   Icon::brush,   "Pintar (B): arraste para a direita e os blocos entram um atrás do outro."_t },
        { "Cortar",   Icon::scissors, "Cortar (C): clique num bloco, ou arraste na vertical por várias raias. Alt corta fora da grade."_t },
        { "Apagar",   Icon::eraser,  "Apagar (D): clique ou arraste por cima dos blocos."_t },
        { "Selecionar", Icon::select, "Selecionar (E): arraste uma caixa, Shift soma. Ctrl+C, Ctrl+V, Ctrl+B duplica, Delete apaga."_t },
        { "Mudo",     Icon::speaker, "Mudo (T): clique ou arraste por cima para calar só esses blocos."_t },
        { "Deslizar", Icon::slip,    "Deslizar (S): arraste para andar com o conteúdo dentro do bloco, sem mexer nas bordas."_t },
        { "Tocar",    Icon::play,    "Tocar (Y): clique para tocar a partir dali."_t } });
    toolSwitch.setLabels (SegmentedControl::Labels::selected);   // oito ícones: só a ferramenta escolhida mostra o nome
    toolSwitch.setSelected (0);
    toolSwitch.onChange = [this] (int index) { grid.setTool ((PlaylistGrid::Tool) index); };
    toolSwitch.setWantsKeyboardFocus (false);

    songButton.setTooltip ("Muda para o modo SONG: o play passa a tocar a playlist inteira"_t);
    songButton.onClick = [this] { app.run ("transport.setSongMode", args ({ { "on", true } })); };
    songButton.setWantsKeyboardFocus (false);

    // Gravar voz (fatia 8, revisão de iniciante de 22/10): um clique arma a trilha "Voz" (cria, se não tem), conta e
    // grava; gravando, o mesmo botão para e guarda o take
    voiceButton.setTooltip ("Gravar voz: arma a trilha Voz (cria, se não tem), liga a contagem (o último tamanho; 1 compasso no começo) e grava. "
                            "Clique de novo para parar."_t);
    voiceButton.onClick = [this]
    {
        if (auto* rec = app.recorder(); rec != nullptr && rec->isRecording())
        {
            app.run ("transport.stop");
            return;
        }

        const auto prepared = app.run ("record.voice");

        if (! prepared.ok)
        {
            if (onStatus) onStatus (prepared.error, true);
            return;
        }

        app.run ("transport.play");
    };
    voiceButton.setWantsKeyboardFocus (false);

    grid.onStatus = [this] (const juce::String& m, bool e) { if (onStatus) onStatus (m, e); };
    grid.onOpenPattern = [this] (const juce::String& id) { if (onOpenPattern) onOpenPattern (id); };
    grid.onBrushChanged = [this] { repaint (0, 0, getWidth(), toolbarHeight); };
    grid.onToolChanged = [this] { toolSwitch.setSelected ((int) grid.getTool()); };
    grid.onGridChanged = [this] { updateSnapLabel(); };
    grid.onViewChanged = [this] { lanes.setView (grid.getScrollTicks(), grid.getPxPerTick()); };

    lanes.getSnapTicks = [this] { return grid.getSnapTicks(); };
    lanes.onHeightChanged = [this] { resized(); };
    lanes.onStatus = [this] (const juce::String& m, bool e) { if (onStatus) onStatus (m, e); };

    addAndMakeVisible (snapBox);
    addAndMakeVisible (toolSwitch);
    addChildComponent (songButton);
    addAndMakeVisible (voiceButton);
    addAndMakeVisible (grid);
    addAndMakeVisible (lanes);
    updateSnapLabel();
}

void Playlist::updateSnapLabel()
{
    // A Auto mostra a divisão do zoom de agora: "Auto: tempo". A seleção é lida antes de trocar o
    // nome, porque o getSelectedId() do JUCE dá 0 quando o texto à vista não bate com o do item.
    const bool showing = snapBox.getSelectedId() == 1;
    snapBox.changeItemText (1, "Auto: " + grid.getAutoGridName());

    if (showing)
        snapBox.setSelectedId (1, juce::dontSendNotification);   // o texto à vista só muda assim
}

void Playlist::refresh (const Project& p)
{
    songMode = p.songMode;
    songButton.setVisible (! songMode);
    grid.refresh (p);
    lanes.refresh (p);
    repaint (0, 0, getWidth(), toolbarHeight);
}

void Playlist::paint (juce::Graphics& g)
{
    using namespace theme;
    auto bar = getLocalBounds().removeFromTop (toolbarHeight);
    g.setColour (colour::panel);
    g.fillRect (bar);
    g.setColour (colour::line);
    g.fillRect (bar.removeFromBottom (1));

    bar.reduce (metric::pad, 0);
    bar.removeFromLeft (toolSwitch.getRight() + metric::gap);

    // o que um clique na grade coloca: pílula com o ícone do padrão e o nome
    g.setColour (colour::textFaint);
    g.setFont (font (type::small, Weight::medium));
    g.drawText ("Clique coloca", bar.removeFromLeft (86), juce::Justification::centredLeft);

    const auto name = grid.getBrushName();
    const float nameWidth = juce::GlyphArrangement::getStringWidth (font (type::body, Weight::semibold), name);
    const auto brush = bar.removeFromLeft (juce::jmin (260, juce::roundToInt (nameWidth) + 42))
                          .withSizeKeepingCentre (juce::jmin (260, juce::roundToInt (nameWidth) + 42), metric::control).toFloat();
    g.setColour (colour::raised);
    g.fillRoundedRectangle (brush, metric::radius);
    icons::draw (g, Icon::pattern, { brush.getX() + 10.0f, brush.getCentreY() - 8.0f, 16.0f, 16.0f }, colour::textDim);
    g.setColour (colour::text);
    g.setFont (font (type::body, Weight::semibold));
    g.drawText (name, brush.withTrimmedLeft (33.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, true);

    auto right = bar;
    right.removeFromRight (voiceButton.getWidth() + metric::gap);

    if (! songMode)
        right.removeFromRight (songButton.getWidth() + metric::gap);

    g.setColour (songMode ? colour::textDim : colour::textFaint);
    g.setFont (font (type::body, Weight::medium));
    g.drawText (songMode ? "Modo SONG: o play toca a playlist"_t : "Modo PAT: o play toca só o padrão"_t,
                right, juce::Justification::centredRight, true);
}

void Playlist::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (toolbarHeight).withSizeKeepingCentre (r.getWidth() - 2 * theme::metric::gap, theme::metric::control);
    lanes.setBounds (r.removeFromBottom (juce::jmin (lanes.getIdealHeight(), r.getHeight() / 2)));   // a grade fica com a maior parte
    grid.setBounds (r);
    snapBox.setBounds (bar.removeFromLeft (156));
    bar.removeFromLeft (theme::metric::gap);
    toolSwitch.setBounds (bar.removeFromLeft (toolSwitch.getIdealWidth()));
    voiceButton.setBounds (bar.removeFromRight (voiceButton.getIdealWidth (theme::metric::control)));
    bar.removeFromRight (theme::metric::gap);
    songButton.setBounds (bar.removeFromRight (songButton.getIdealWidth (theme::metric::control)));
}

} // namespace whynot
