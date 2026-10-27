#include "AutomationLanes.h"
#include "Theme.h"
#include "fx/FxWidgets.h"
#include "../app/AppController.h"
#include "../automation/AutomationTarget.h"
#include "../dsp/FxCatalog.h"

#include <map>

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

    juce::String comma (float v, int decimals)   { return juce::String (v, decimals).replaceCharacter ('.', ','); }

    constexpr float pointRadius = 3.5f, hitRadius = 7.0f;
    constexpr int curvePad = 7;          // respiro em cima e embaixo: o ponto no 0 e no 1 aparece inteiro
    constexpr int maxMenuItems = 128;    // VST3 com centenas de parâmetros: o menu para aqui
}

AutomationLanes::AutomationLanes (AppController& a) : app (a)
{
    addButton.setTooltip ("Nova curva de automação: o volume, o pan ou um parâmetro de efeito ou de instrumento "
                          "mudando ao longo da música"_t);
    addButton.onClick = [this] { showAddMenu(); };
    addButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (addButton);
}

//==============================================================================
void AutomationLanes::refresh (const Project& p)
{
    songMode = p.songMode;
    const int stepsPerBeat = juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen));
    barTicks = stepsPerBeat * juce::jmax (1, p.timeSigNum) * ticksPerStep;

    // durante um arrasto, a curva que está na mão fica como está na tela
    juce::String heldId;
    std::vector<automation::Point> heldPoints;

    if (dragging && juce::isPositiveAndBelow (dragLane, (int) lanes.size()))
    {
        heldId = lanes[(size_t) dragLane].id;
        heldPoints = lanes[(size_t) dragLane].points;
    }

    const int heightBefore = getIdealHeight();

    // os nomes ("Voz · Filtro · Frequência") vêm do núcleo, que conhece os parâmetros dos VST3
    std::map<juce::String, juce::String> names;

    if (const auto list = app.run ("automation.list"); list.ok)
        if (auto* array = list.value["lanes"].getArray())
            for (auto& l : *array)
                names[l["id"].toString()] = l["name"].toString();

    lanes.clear();

    for (auto& src : p.automation)
    {
        Lane lane;
        lane.id = src.id;
        lane.name = names[src.id];
        lane.range = automation::rangeFor (p, src.target);

        for (auto& pt : src.points)
            lane.points.push_back ({ pt.t, pt.v, pt.tension });

        const auto& t = src.target;

        if (t.kind == "mixer")
        {
            lane.format = t.param == "volume" ? Format::db : Format::pan;
        }
        else if (t.kind == "insert")
        {
            if (auto* m = p.findMixer (t.track))
                if (auto* ins = m->findInsert (t.insert); ins != nullptr && ins->kind == "whynot")
                    if (auto* info = dsp::findEffect (ins->type.toStdString()))
                        if (const int index = info->indexOf (t.param.toRawUTF8()); index >= 0)
                        {
                            lane.format = Format::effect;
                            lane.param = &info->params[(size_t) index];
                        }
        }

        if (lane.name.isEmpty())
            lane.name = "Alvo removido"_t;

        lanes.push_back (std::move (lane));
    }

    if (heldId.isNotEmpty())
    {
        dragLane = -1;

        for (int i = 0; i < (int) lanes.size(); ++i)
            if (lanes[(size_t) i].id == heldId)
            {
                lanes[(size_t) i].points = heldPoints;
                dragLane = i;
            }

        if (dragLane < 0)
        {
            dragging = false;
            dragPoint = -1;
        }
    }

    // o menu de Nova curva: as trilhas do mixer e os instrumentos VST3
    sources.clear();

    for (auto& m : p.mixer)
        sources.push_back ({ m.id, m.name, true });

    for (auto& c : p.channels)
        if (c.isPlugin())
            sources.push_back ({ c.id, c.name, false });

    clampScroll();

    if (revealLane.isNotEmpty())
        reveal (revealLane);

    if (getIdealHeight() != heightBefore && onHeightChanged)
        onHeightChanged();

    repaint();
}

void AutomationLanes::setView (double newScroll, float newPxPerTick)
{
    if (newScroll == scrollTicks && newPxPerTick == pxPerTick)
        return;

    scrollTicks = newScroll;
    pxPerTick = newPxPerTick;
    repaint();
}

void AutomationLanes::setPlayhead (double ticks)
{
    if (app.isAutomationRecording() != recordingShown)   // o aviso do cabeçalho segue o modo
        repaint (0, 0, getWidth(), headerHeight);

    if (std::abs (ticks - playhead) < 0.5)
        return;

    if (lanes.empty())
    {
        playhead = ticks;
        return;
    }

    auto column = [this] (double t) { return juce::Rectangle<int> ((int) tickToX (t) - 2, headerHeight, 5, getHeight() - headerHeight); };

    if (playhead >= 0.0) repaint (column (playhead));
    playhead = ticks;
    if (playhead >= 0.0) repaint (column (playhead));

    repaint (0, headerHeight, nameWidth, getHeight() - headerHeight);   // o valor de agora, ao lado do nome
}

int AutomationLanes::getIdealHeight() const
{
    return headerHeight + juce::jmin ((int) lanes.size(), maxVisible) * laneHeight;
}

void AutomationLanes::clampScroll()
{
    firstLane = juce::jlimit (0, juce::jmax (0, (int) lanes.size() - maxVisible), firstLane);
}

void AutomationLanes::reveal (const juce::String& laneId)
{
    for (int i = 0; i < (int) lanes.size(); ++i)
    {
        if (lanes[(size_t) i].id != laneId)
            continue;

        if (i < firstLane)                    firstLane = i;
        else if (i >= firstLane + maxVisible)  firstLane = i - maxVisible + 1;

        revealLane.clear();
        clampScroll();
        repaint();
        return;
    }
}

//==============================================================================
juce::Rectangle<int> AutomationLanes::laneBounds (int index) const
{
    const int slot = index - firstLane;

    if (! juce::isPositiveAndBelow (slot, maxVisible) || ! juce::isPositiveAndBelow (index, (int) lanes.size()))
        return {};

    return { 0, headerHeight + slot * laneHeight, getWidth(), laneHeight };
}

juce::Rectangle<int> AutomationLanes::curveBounds (int index) const
{
    const auto r = laneBounds (index);
    return r.isEmpty() ? r : r.withLeft (nameWidth).withRight (getWidth() - scrollSize).reduced (0, curvePad);
}

int AutomationLanes::laneAt (juce::Point<float> p) const
{
    for (int i = firstLane; i < juce::jmin ((int) lanes.size(), firstLane + maxVisible); ++i)
        if (laneBounds (i).toFloat().contains (p))
            return i;

    return -1;
}

int AutomationLanes::pointAt (int lane, juce::Point<float> p) const
{
    if (! juce::isPositiveAndBelow (lane, (int) lanes.size()))
        return -1;

    const auto area = curveBounds (lane).toFloat();
    int best = -1;
    float bestDistance = hitRadius;

    for (int j = 0; j < (int) lanes[(size_t) lane].points.size(); ++j)
    {
        const auto& pt = lanes[(size_t) lane].points[(size_t) j];
        const float d = p.getDistanceFrom ({ tickToX (pt.t), area.getBottom() - pt.v * area.getHeight() });

        if (d < bestDistance)
        {
            bestDistance = d;
            best = j;
        }
    }

    return best;
}

juce::Point<float> AutomationLanes::handlePosition (int lane, int segment) const
{
    const auto& points = lanes[(size_t) lane].points;
    const auto area = curveBounds (lane).toFloat();
    const auto& a = points[(size_t) segment];
    const auto& b = points[(size_t) segment + 1];
    const double mid = 0.5 * (a.t + b.t);
    return { tickToX (mid), area.getBottom() - automation::positionAt (points, mid, 0.0f) * area.getHeight() };
}

int AutomationLanes::handleAt (int lane, juce::Point<float> p) const
{
    if (! juce::isPositiveAndBelow (lane, (int) lanes.size()))
        return -1;

    const auto& points = lanes[(size_t) lane].points;

    for (int j = 0; j + 1 < (int) points.size(); ++j)
    {
        // só nos trechos largos e que sobem ou descem (num trecho reto, a tensão não muda nada)
        if ((points[(size_t) j + 1].t - points[(size_t) j].t) * pxPerTick < 24.0f
            || std::abs (points[(size_t) j + 1].v - points[(size_t) j].v) < 0.002f)
            continue;

        if (p.getDistanceFrom (handlePosition (lane, j)) < hitRadius)
            return j;
    }

    return -1;
}

float AutomationLanes::tickToX (double tick) const   { return (float) (nameWidth + (tick - scrollTicks) * pxPerTick); }
double AutomationLanes::xToTick (float x) const      { return scrollTicks + (x - (float) nameWidth) / pxPerTick; }

float AutomationLanes::yToPosition (int lane, float y) const
{
    const auto area = curveBounds (lane).toFloat();
    float n = area.getHeight() > 0.0f ? juce::jlimit (0.0f, 1.0f, (area.getBottom() - y) / area.getHeight()) : 0.0f;

    // escolha ou liga/desliga: o ponto pula para o valor inteiro mais perto
    if (const auto& range = lanes[(size_t) lane].range; range.has_value() && range->stepped)
        n = range->toNormalised (range->fromNormalised (n));

    return n;
}

int AutomationLanes::snapTick (double tick, bool free) const
{
    if (free)
        return juce::jlimit (0, maxTimelineTicks, juce::roundToInt (tick));

    const int snap = juce::jmax (1, getSnapTicks != nullptr ? getSnapTicks() : ticksPerStep);
    return juce::jlimit (0, maxTimelineTicks, juce::roundToInt (tick / snap) * snap);
}

juce::String AutomationLanes::formatValue (const Lane& lane, float position) const
{
    if (! lane.range.has_value())
        return {};

    const float v = lane.range->fromNormalised (position);

    switch (lane.format)
    {
        case Format::db:       return v <= -99.9f ? juce::String ("-inf dB") : (v > 0.05f ? "+" : "") + comma (v, 1) + " dB";
        case Format::pan:      return std::abs (v) < 0.005f ? juce::String ("Centro") : (v < 0.0f ? "E " : "D ") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
        case Format::effect:   return lane.param != nullptr ? fxui::formatValue (*lane.param, v) : comma (v, 2);
        case Format::percent:
        default:               return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    }
}

std::optional<float> AutomationLanes::shownPosition (const Lane& lane, int index) const
{
    if (lane.points.empty())
        return std::nullopt;

    if (dragging && dragLane == index && juce::isPositiveAndBelow (dragPoint, (int) lane.points.size()))
        return lane.points[(size_t) dragPoint].v;

    // tocando: o valor de agora; parado, com o mouse em cima: o valor naquele ponto da música
    if (playhead >= 0.0)
        return automation::positionAt (lane.points, playhead, 0.0f);

    if (laneAt (hoverPos) == index && hoverPos.x >= (float) nameWidth)
        return automation::positionAt (lane.points, xToTick (hoverPos.x), 0.0f);

    return lane.points.front().v;
}

//==============================================================================
void AutomationLanes::resized()
{
    const int h = 24;
    const int w = addButton.getIdealWidth (h);
    addButton.setBounds (getWidth() - scrollSize - 8 - w, (headerHeight - h) / 2, w, h);
}

void AutomationLanes::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::background);

    // cabeçalho: o nome do painel, a dica do modo e o botão de nova curva
    const auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (colour::panel);
    g.fillRect (header);
    g.setColour (colour::line);
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, header.getBottom() - 1, getWidth(), 1);

    icons::draw (g, Icon::automation, { 12.0f, 7.0f, 14.0f, 14.0f }, colour::textDim);
    g.setColour (colour::textDim);
    g.setFont (captionFont());
    g.drawText ("AUTOMAÇÃO"_t, juce::Rectangle<int> (32, 0, nameWidth - 32, headerHeight), juce::Justification::centredLeft, false);

    recordingShown = app.isAutomationRecording();
    const auto hint = recordingShown ? (songMode ? "Gravando automação: no play, mexa num knob, fader ou controlador."_t
                                                 : "Gravando automação: ela grava no modo SONG."_t)
                    : lanes.empty() ? "O volume, o pan ou um efeito mudando ao longo da música. Toca no modo SONG."_t
                    : songMode      ? "Toca no modo SONG. A bolinha no meio do trecho curva."_t
                                    : "Modo PAT: as curvas ficam paradas. Toque a playlist (SONG) para ouvir."_t;
    auto hintArea = header.withLeft (nameWidth + 8).withRight (addButton.getX() - 8);

    if (recordingShown)
    {
        g.setColour (colour::meterHigh);   // o vermelho funcional do REC
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) hintArea.getX() + 4.0f, (float) header.getCentreY() }));
        hintArea.removeFromLeft (14);
    }

    g.setColour (recordingShown ? colour::text : colour::textFaint);
    g.setFont (font (type::body));
    g.drawText (hint, hintArea, juce::Justification::centredLeft, true);

    // raias
    const int last = juce::jmin ((int) lanes.size(), firstLane + maxVisible);
    const int hoverLane = dragging ? -1 : laneAt (hoverPos);
    const int hoverPoint = hoverLane >= 0 ? pointAt (hoverLane, hoverPos) : -1;
    const int hoverHandle = hoverLane >= 0 && hoverPoint < 0 ? handleAt (hoverLane, hoverPos) : -1;

    for (int i = firstLane; i < last; ++i)
    {
        const auto& lane = lanes[(size_t) i];
        const auto row = laneBounds (i);
        const auto strip = row.withLeft (nameWidth).withRight (getWidth() - scrollSize);
        const auto curve = curveBounds (i).toFloat();
        const bool live = songMode && lane.range.has_value();   // laranja = toca; no PAT, cinza
        const auto ink = live ? colour::signal : colour::textDim.withAlpha (0.55f);

        g.setColour (i % 2 == 0 ? colour::grid::laneEven : colour::grid::laneOdd);
        g.fillRect (strip);

        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (strip);

            // os compassos, como na grade de cima (de 4 em 4 mais fortes)
            const float pxPerBar = (float) barTicks * pxPerTick;
            const int every = pxPerBar >= 8.0f ? 1 : (pxPerBar >= 4.0f ? 2 : 4);
            const int firstBar = juce::jmax (0, (int) std::floor (xToTick ((float) strip.getX()) / barTicks));
            const int lastBar = (int) std::ceil (xToTick ((float) strip.getRight()) / barTicks);

            for (int bar = firstBar; bar <= lastBar; ++bar)
            {
                if (bar % every != 0)
                    continue;

                g.setColour (bar % 4 == 0 ? colour::textDim.withAlpha (0.3f) : colour::line);
                g.fillRect (tickToX ((double) bar * barTicks), (float) strip.getY(), 1.0f, (float) strip.getHeight());
            }

            // a curva: reta ou curvada (a tensão) entre os pontos; antes do primeiro e depois do último, reta no
            // valor da ponta
            if (! lane.points.empty())
            {
                auto yOf = [&curve] (float v) { return curve.getBottom() - v * curve.getHeight(); };

                juce::Path path;
                path.startNewSubPath ((float) strip.getX(), yOf (lane.points.front().v));

                for (size_t j = 0; j < lane.points.size(); ++j)
                {
                    const auto& pt = lane.points[j];

                    if (j > 0 && std::abs (lane.points[j - 1].tension) >= 1.0e-4f)
                    {
                        // um passo a cada ~3 px na tela, entre 4 e 64
                        const auto& a = lane.points[j - 1];
                        const int steps = juce::jlimit (4, 64, (int) ((float) (pt.t - a.t) * pxPerTick / 3.0f));

                        for (int k = 1; k < steps; ++k)
                        {
                            const double t = a.t + (double) (pt.t - a.t) * k / steps;
                            path.lineTo (tickToX (t), yOf (automation::positionAt (lane.points, t, 0.0f)));
                        }
                    }

                    path.lineTo (tickToX (pt.t), yOf (pt.v));
                }

                path.lineTo ((float) strip.getRight(), yOf (lane.points.back().v));

                auto area = path;
                area.lineTo ((float) strip.getRight(), curve.getBottom());
                area.lineTo ((float) strip.getX(), curve.getBottom());
                area.closeSubPath();
                g.setColour (ink.withAlpha (live ? 0.10f : 0.05f));
                g.fillPath (area);

                g.setColour (ink);
                g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                // as bolinhas do meio de cada trecho (a tensão): pequenas e vazadas; maiores com o mouse em cima
                if (lane.range.has_value())
                {
                    for (int j = 0; j + 1 < (int) lane.points.size(); ++j)
                    {
                        const bool held = dragging && dragLane == i && dragSegment == j;
                        const bool over = hoverLane == i && hoverHandle == j;
                        const auto& a = lane.points[(size_t) j];
                        const auto& b = lane.points[(size_t) j + 1];

                        if (! held && ((b.t - a.t) * pxPerTick < 24.0f || std::abs (b.v - a.v) < 0.002f))
                            continue;

                        const auto c = handlePosition (i, j);

                        if (c.x < (float) strip.getX() || c.x > (float) strip.getRight())
                            continue;

                        const float r = held || over ? 3.5f : 2.5f;
                        const auto ring = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
                        g.setColour (held || over ? ink : colour::background);
                        g.fillEllipse (ring);
                        g.setColour (ink.withAlpha (held || over ? 1.0f : 0.7f));
                        g.drawEllipse (ring, 1.0f);
                    }
                }

                for (int j = 0; j < (int) lane.points.size(); ++j)
                {
                    const auto& pt = lane.points[(size_t) j];
                    const float x = tickToX (pt.t);

                    if (x < (float) strip.getX() - 8.0f || x > (float) strip.getRight() + 8.0f)
                        continue;

                    const bool held = dragging && dragLane == i && dragPoint == j;
                    const bool over = hoverLane == i && hoverPoint == j;
                    const float r = held || over ? 4.5f : pointRadius;
                    const auto dot = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ x, yOf (pt.v) });

                    g.setColour (held || over ? (live ? colour::signal : colour::text) : colour::background);
                    g.fillEllipse (dot);
                    g.setColour (ink);
                    g.drawEllipse (dot, 1.5f);
                }
            }

            if (playhead >= 0.0)
            {
                g.setColour (colour::text);
                g.fillRect (tickToX (playhead) - 0.5f, (float) strip.getY(), 1.5f, (float) strip.getHeight());
            }
        }

        g.setColour (colour::grid::laneEdge);
        g.fillRect (strip.getX(), strip.getBottom() - 1, strip.getWidth(), 1);

        // a coluna do nome, alinhada com as raias da playlist: o alvo e o valor de agora
        const auto nameArea = row.withWidth (nameWidth);
        g.setColour (colour::panel);
        g.fillRect (nameArea);
        g.setColour (colour::line);
        g.fillRect (nameArea.getRight() - 1, nameArea.getY(), 1, nameArea.getHeight());
        g.fillRect (nameArea.withTop (nameArea.getBottom() - 1));

        auto text = nameArea.reduced (12, 9);
        g.setColour (lane.range.has_value() ? colour::text : colour::textFaint);
        g.setFont (font (type::body, Weight::medium));
        g.drawFittedText (lane.name, text.removeFromTop (20), juce::Justification::centredLeft, 1, 0.8f);

        if (! lane.range.has_value())
        {
            g.setColour (colour::textFaint);
            g.setFont (font (type::body));
            g.drawText ("Saiu: apague a curva"_t, text.removeFromTop (18), juce::Justification::centredLeft, true);
        }
        else if (const auto shown = shownPosition (lane, i))
        {
            g.setColour (live ? colour::text : colour::textDim);
            drawTabularText (g, formatValue (lane, *shown), text.removeFromTop (18).toFloat(), numberFont (type::small),
                             juce::Justification::centredLeft);
        }
    }

    // coluna da direita: a barrinha de rolagem quando há mais de 3 curvas
    if (! lanes.empty())
    {
        const auto column = juce::Rectangle<int> (getWidth() - scrollSize, headerHeight, scrollSize, getHeight() - headerHeight);
        g.setColour (colour::panel);
        g.fillRect (column);

        if ((int) lanes.size() > maxVisible)
        {
            const auto track = column.toFloat().reduced (3.0f, 4.0f);
            const float total = (float) lanes.size();
            g.setColour (colour::raisedHigh);
            g.fillRoundedRectangle (track.withTrimmedTop (track.getHeight() * (float) firstLane / total)
                                         .withHeight (track.getHeight() * (float) maxVisible / total), 3.0f);
        }
    }
}

//==============================================================================
void AutomationLanes::mouseMove (const juce::MouseEvent& e)
{
    hoverPos = e.position;
    const int index = laneAt (e.position);
    auto newCursor = juce::MouseCursor::NormalCursor;

    if (index >= 0 && lanes[(size_t) index].range.has_value() && curveBounds (index).expanded (0, curvePad).toFloat().contains (e.position))
        newCursor = pointAt (index, e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor
                  : handleAt (index, e.position) >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                                      : juce::MouseCursor::CrosshairCursor;

    setMouseCursor (newCursor);
    repaint();
}

void AutomationLanes::mouseExit (const juce::MouseEvent&)
{
    hoverPos = { -1.0f, -1.0f };
    repaint();
}

void AutomationLanes::mouseDown (const juce::MouseEvent& e)
{
    const int index = laneAt (e.position);

    if (index < 0)
        return;

    auto& lane = lanes[(size_t) index];

    if (! curveBounds (index).expanded (0, curvePad).toFloat().contains (e.position))
    {
        if (e.mods.isPopupMenu())
            showLaneMenu (index);

        return;
    }

    if (! lane.range.has_value())
    {
        if (onStatus) onStatus ("O alvo desta curva saiu (efeito ou trilha apagados). Botão direito no nome apaga a curva."_t, true);
        return;
    }

    const int hit = pointAt (index, e.position);

    // a bolinha do meio: arrastar curva o trecho; duplo clique (ou o botão direito) deixa reto
    if (const int segment = hit < 0 ? handleAt (index, e.position) : -1; segment >= 0)
    {
        if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)
        {
            lane.points[(size_t) segment].tension = 0.0f;
            commit (index);
            return;
        }

        dragLane = index;
        dragPoint = -1;
        dragSegment = segment;
        dragging = true;
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            lane.points.erase (lane.points.begin() + hit);
            commit (index);
        }
        else
        {
            showLaneMenu (index);
        }

        return;
    }

    int point = hit;

    if (point < 0)
    {
        // clique na raia: ponto novo (um por tick; no mesmo tick, troca o valor)
        const int t = snapTick (xToTick (e.position.x), e.mods.isAltDown());
        const float v = yToPosition (index, e.position.y);
        auto it = std::lower_bound (lane.points.begin(), lane.points.end(), t,
                                    [] (const automation::Point& p, int tick) { return p.t < tick; });

        if (it != lane.points.end() && it->t == t)
            it->v = v;
        else
            it = lane.points.insert (it, { t, v });

        point = (int) std::distance (lane.points.begin(), it);
    }

    dragLane = index;
    dragPoint = point;
    dragging = true;
    repaint();
}

void AutomationLanes::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || ! juce::isPositiveAndBelow (dragLane, (int) lanes.size()))
        return;

    auto& points = lanes[(size_t) dragLane].points;

    if (juce::isPositiveAndBelow (dragSegment, (int) points.size() - 1))
    {
        // a bolinha vai para onde o mouse está: a tensão que põe o meio do trecho nessa altura
        const auto& a = points[(size_t) dragSegment];
        const auto& b = points[(size_t) dragSegment + 1];
        const float v = yToPosition (dragLane, e.position.y);

        if (std::abs (b.v - a.v) >= 0.002f)
            points[(size_t) dragSegment].tension = automation::tensionForMidpoint ((v - a.v) / (b.v - a.v));

        hoverPos = e.position;
        repaint();
        return;
    }

    if (! juce::isPositiveAndBelow (dragPoint, (int) points.size()))
        return;

    // o ponto não passa dos vizinhos: a ordem fica como está
    const int lo = dragPoint > 0 ? points[(size_t) dragPoint - 1].t + 1 : 0;
    const int hi = dragPoint + 1 < (int) points.size() ? points[(size_t) dragPoint + 1].t - 1 : maxTimelineTicks;
    auto& pt = points[(size_t) dragPoint];
    pt.t = juce::jlimit (lo, juce::jmax (lo, hi), snapTick (xToTick (e.position.x), e.mods.isAltDown()));
    pt.v = yToPosition (dragLane, e.position.y);
    hoverPos = e.position;
    repaint();
}

void AutomationLanes::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    const int index = dragLane;
    dragLane = dragPoint = dragSegment = -1;
    commit (index);   // um passo de desfazer por arrasto
}

void AutomationLanes::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if ((int) lanes.size() <= maxVisible || e.mods.isCtrlDown() || w.deltaY == 0.0f)
    {
        juce::Component::mouseWheelMove (e, w);
        return;
    }

    firstLane += w.deltaY < 0.0f ? 1 : -1;
    clampScroll();
    repaint();
}

juce::String AutomationLanes::getTooltip()
{
    const int index = laneAt (hoverPos);

    if (index < 0)
        return {};

    const auto& lane = lanes[(size_t) index];

    if (! curveBounds (index).expanded (0, curvePad).toFloat().contains (hoverPos))
        return lane.name + ". Botão direito: apagar os pontos ou a curva."_t;

    if (! lane.range.has_value())
        return "O alvo desta curva saiu (efeito ou trilha apagados). Botão direito no nome apaga a curva."_t;

    if (const int p = pointAt (index, hoverPos); p >= 0)
        return formatValue (lane, lane.points[(size_t) p].v) + ". Arraste para mover, botão direito apaga. Alt: sem grade."_t;

    if (handleAt (index, hoverPos) >= 0)
        return "Arraste para cima ou para baixo para curvar o trecho. Duplo clique deixa reto."_t;

    return "Clique cria um ponto, arraste move, botão direito apaga. Alt: sem grade."_t
           + (songMode ? juce::String() : " A curva toca no modo SONG."_t);
}

//==============================================================================
void AutomationLanes::commit (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) lanes.size()))
        return;

    juce::Array<juce::var> list;

    for (auto& pt : lanes[(size_t) index].points)
        list.add (pt.tension != 0.0f ? args ({ { "t", pt.t }, { "v", pt.v }, { "tension", pt.tension } }) : args ({ { "t", pt.t }, { "v", pt.v } }));

    const auto result = app.run ("automation.setPoints", args ({ { "lane", lanes[(size_t) index].id }, { "points", list } }));

    if (! result.ok && onStatus)
        onStatus (result.error, true);
}

void AutomationLanes::showLaneMenu (int index)
{
    const auto& lane = lanes[(size_t) index];
    const auto id = lane.id;

    juce::PopupMenu menu;
    menu.addSectionHeader (lane.name);
    menu.addItem (1, "Apagar os pontos"_t, ! lane.points.empty());
    menu.addItem (2, "Apagar a curva"_t);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (laneBounds (index).withWidth (nameWidth))),
                        [this, id] (int choice)
                        {
                            if (choice == 1) app.run ("automation.setPoints", args ({ { "lane", id }, { "points", juce::Array<juce::var>() } }));
                            if (choice == 2) app.run ("automation.removeLane", args ({ { "lane", id } }));
                        });
}

void AutomationLanes::showAddMenu()
{
    // cada trilha do mixer (e cada instrumento VST3) vira um submenu com o que dá para mover;
    // o que já tem curva aparece marcado e só rola até ela
    juce::PopupMenu menu;
    std::vector<juce::var> choices;
    bool instrumentsHeader = false;

    for (auto& s : sources)
    {
        const auto result = app.run ("automation.targets", args ({ { s.isTrack ? "track" : "channel", s.id } }));
        auto* list = result.ok ? result.value["targets"].getArray() : nullptr;

        if (list == nullptr || list->isEmpty())
            continue;

        juce::PopupMenu sub;
        const auto prefix = s.name + " · "_t;

        for (auto& t : *list)
        {
            if (sub.getNumItems() >= maxMenuItems)
                break;

            choices.push_back (t);
            const auto name = t["name"].toString();
            sub.addItem ((int) choices.size(), name.startsWith (prefix) ? name.substring (prefix.length()) : name,
                         true, t["lane"].toString().isNotEmpty());
        }

        if (! s.isTrack && ! instrumentsHeader)
        {
            menu.addSectionHeader ("Instrumentos VST3");
            instrumentsHeader = true;
        }

        menu.addSubMenu (s.name, sub);
    }

    if (choices.empty())
    {
        if (onStatus) onStatus ("Nada para automatizar ainda."_t, false);
        return;
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                        [this, choices] (int choice)
                        {
                            if (! juce::isPositiveAndBelow (choice - 1, (int) choices.size()))
                                return;

                            const auto& t = choices[(size_t) choice - 1];

                            if (const auto existing = t["lane"].toString(); existing.isNotEmpty())
                            {
                                reveal (existing);
                                return;
                            }

                            // a curva nova já nasce com um ponto no valor de agora: o som não muda até mexer
                            app.getCommands().beginGesture ("Nova automação"_t);
                            const auto added = app.run ("automation.addLane", args ({ { "target", t["target"] } }));

                            if (added.ok)
                            {
                                revealLane = added.value["lane"].toString();
                                juce::Array<juce::var> first;
                                first.add (args ({ { "t", 0 }, { "v", t["v"] } }));
                                app.run ("automation.setPoints", args ({ { "lane", revealLane }, { "points", first } }));
                            }

                            app.getCommands().endGesture();

                            if (! added.ok)
                            {
                                if (onStatus) onStatus (added.error, true);
                            }
                            else
                            {
                                reveal (revealLane);
                                if (onStatus) onStatus ("Curva nova: "_t + t["name"].toString() + ". Clique na raia para pôr pontos."_t, false);
                            }
                        });
}

} // namespace whynot
