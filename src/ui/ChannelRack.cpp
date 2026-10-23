#include "ChannelRack.h"
#include "NoteNames.h"
#include "FavoriteStar.h"
#include "SampleBrowser.h"
#include "SamplerPanel.h"
#include "Theme.h"
#include "../app/AppController.h"
#include "../audio/PianoPresets.h"
#include "../record/RecordEngine.h"
#include "../synth/SynthPresets.h"

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

    const int rolls[] = { 1, 2, 3, 4, 6, 8 };

    int nextRoll (int current, bool up)
    {
        int index = 0;
        for (int i = 0; i < 6; ++i) if (rolls[i] == current) index = i;
        return rolls[juce::jlimit (0, 5, index + (up ? 1 : -1))];
    }

    const char* const audioWildcard = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
}

ChannelRack::ChannelRack (AppController& a) : app (a)
{
    setOpaque (true);   // pinta o fundo inteiro: a agulha repinta só as colunas dela (fluidez, 21/10)
    setRepaintsOnMouseActivity (false);
}

int ChannelRack::getIdealHeight() const
{
    return headerHeight + (int) rows.size() * rowHeight + footerHeight;
}

void ChannelRack::refresh (const Project& p)
{
    const auto* pattern = p.currentPatternPtr();
    numSteps = pattern != nullptr ? pattern->lengthSteps : 16;
    patternTicks = numSteps * ticksPerStep;
    const auto& problems = app.getProjector().sampleProblems();

    // um clique num passo mexe numa linha só: guarda o desenho de antes para repintar só o que mudou
    // (o rack inteiro, a 1920x1080, levava uns 15 ms por clique; fluidez, 21/10)
    const auto previousRows = std::move (rows);
    const int previousSteps = numSteps, previousSelected = selectedRow, previousWidth = stepsLeft + stepSize;
    rows.clear();

    for (auto& c : p.channels)
    {
        Row r;
        r.id = c.id;
        r.name = c.name;
        r.colour = theme::fromHex (c.colour);
        r.mute = c.mute;
        r.cutItself = c.cutItself;
        r.mono = c.mono;
        r.oneShot = c.oneShot;
        r.chokeGroup = c.chokeGroup;
        r.swing = c.swing;
        r.isAudio = c.isAudio();
        r.output = p.resolveOutput (c.output);

        if (auto* m = p.findMixer (r.output))
        {
            r.outputName = m->name;
            r.frozen = m->frozen && ! m->isMaster();
            r.ownTrack = m->autoFor == c.id;
        }

        r.armed = c.armed && ! c.isAudio();

        r.problemText = problems[c.id];
        r.problem = r.problemText.isNotEmpty();
        r.isPlugin = c.isPlugin();
        r.isSynth = c.isSynth();
        r.isPiano = c.isPiano();
        r.slices = (int) c.slices.size();
        r.pianoPreset = c.instrumentPreset;

        if (pattern != nullptr)
        {
            if (auto it = pattern->steps.find (c.id); it != pattern->steps.end())
                r.steps = it->second;

            if (auto* list = pattern->notesFor (c.id))
                r.notes = *list;
        }

        r.steps.resize ((size_t) numSteps);
        rows.push_back (std::move (r));
    }

    selectedRow = -1;

    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].id == selectedChannel)
            selectedRow = (int) i;

    layoutRows();

    if (rows.size() != previousRows.size() || numSteps != previousSteps || selectedRow != previousSelected
        || stepsLeft + stepSize != previousWidth)
    {
        repaint();
        return;
    }

    for (size_t i = 0; i < rows.size(); ++i)
        if (! (rows[i] == previousRows[i]))
            repaint (rowBounds ((int) i));
}

void ChannelRack::setSelectedChannel (const juce::String& id)
{
    if (id == selectedChannel)
        return;

    selectedChannel = id;
    selectedRow = -1;

    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].id == id)
            selectedRow = (int) i;

    repaint();
}

void ChannelRack::setPlayheadStep (int step)
{
    if (step != playhead)
    {
        // só as duas colunas que mudam (a de antes e a de agora), não o rack inteiro (fluidez, 21/10)
        const int top = headerHeight - 14, bottom = headerHeight + (int) rows.size() * rowHeight;

        auto column = [&] (int s) -> juce::Rectangle<int>
        {
            if (s < 0 || rows.empty())
                return {};

            // o quadrado do passo, com o aro da agulha, e a linha da prévia das notas (proporcional à linha)
            const auto b = stepBounds (0, s);
            const auto inner = notesBounds (0).toFloat().reduced (5.0f, 4.0f);
            const int lineX = (int) (inner.getX() + inner.getWidth() * (float) s / (float) juce::jmax (1, numSteps));
            return juce::Rectangle<int> (b.getX() - 4, top, b.getWidth() + 8, bottom - top)
                       .getUnion ({ lineX - 2, top, 6, bottom - top });
        };

        repaint (column (playhead));
        playhead = step;
        repaint (column (playhead));
    }

    // este tique vem do timer da tela: serve também para o armado ficar vermelho enquanto grava
    auto* rec = app.recorder();
    const bool nowRecording = rec != nullptr && rec->isRecording();

    if (nowRecording != recording)
    {
        recording = nowRecording;
        repaint (armLeft, headerHeight, armWidth, (int) rows.size() * rowHeight);
    }
}

void ChannelRack::resized()
{
    layoutRows();
}

void ChannelRack::layoutRows()
{
    stepsLeft = tapeLeft + tapeWidth + 8 + outputWidth + 16;
    const int groups = (numSteps + 3) / 4;
    const int available = getWidth() - stepsLeft - 16 - (groups - 1) * 10;
    stepSize = juce::jlimit (14, 36, numSteps > 0 ? available / numSteps - 3 : 30);
}

//==============================================================================
juce::Rectangle<int> ChannelRack::rowBounds (int row) const
{
    return { 0, headerHeight + row * rowHeight, getWidth(), rowHeight };
}

juce::Rectangle<int> ChannelRack::muteBounds (int row) const
{
    return rowBounds (row).withX (16).withWidth (muteWidth).reduced (3, 9);
}

juce::Rectangle<int> ChannelRack::armBounds (int row) const
{
    return rowBounds (row).withX (armLeft).withWidth (armWidth).reduced (0, 10);
}

juce::Rectangle<int> ChannelRack::tapeBounds (int row) const
{
    return rowBounds (row).withX (tapeLeft).withWidth (tapeWidth).reduced (0, 6);
}

juce::Rectangle<int> ChannelRack::outputBounds (int row) const
{
    return rowBounds (row).withX (tapeLeft + tapeWidth + 8).withWidth (outputWidth).reduced (0, 8);
}

juce::Rectangle<int> ChannelRack::stepBounds (int row, int step) const
{
    const int x = stepsLeft + step * (stepSize + 3) + (step / 4) * 10;
    const auto r = rowBounds (row);
    return { x, r.getCentreY() - stepSize / 2, stepSize, stepSize };
}

juce::Rectangle<int> ChannelRack::notesBounds (int row) const
{
    const auto first = stepBounds (row, 0);
    const auto last = stepBounds (row, juce::jmax (0, numSteps - 1));
    return { first.getX(), rowBounds (row).getY() + 5, last.getRight() - first.getX(), rowHeight - 10 };
}

juce::Rectangle<int> ChannelRack::addButtonBounds() const
{
    return { 16, getHeight() - footerHeight + 4, 172, 32 };
}

juce::Rectangle<int> ChannelRack::lengthBounds() const
{
    return { getWidth() - 16 - 136, 8, 136, 32 };
}

ChannelRack::Hit ChannelRack::hitTest (juce::Point<int> p) const
{
    if (addButtonBounds().contains (p))   return { Hit::Kind::addButton, -1, -1 };
    if (lengthBounds().contains (p))      return { Hit::Kind::lengthBox, -1, -1 };

    for (int row = 0; row < (int) rows.size(); ++row)
    {
        if (! rowBounds (row).contains (p))
            continue;

        const auto& r = rows[(size_t) row];

        if (muteBounds (row).expanded (3, 9).contains (p))  return { Hit::Kind::mute, row, -1 };
        if (armBounds (row).contains (p) && ! r.isAudio)   return { Hit::Kind::arm, row, -1 };   // áudio arma no mixer
        if (tapeBounds (row).contains (p))                  return { Hit::Kind::tape, row, -1 };
        if (outputBounds (row).contains (p))                return { Hit::Kind::output, row, -1 };

        if (r.isAudio)
            return { Hit::Kind::none, row, -1 };

        if (! r.notes.empty())
            return notesBounds (row).contains (p) ? Hit { Hit::Kind::notes, row, -1 } : Hit { Hit::Kind::none, row, -1 };

        for (int s = 0; s < numSteps; ++s)
            if (stepBounds (row, s).expanded (2, 4).contains (p))
                return { Hit::Kind::step, row, s };

        return { Hit::Kind::none, row, -1 };
    }

    return {};
}

//==============================================================================
void ChannelRack::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    // cabeçalho: título do cartão, números dos tempos sobre os passos e o tamanho do padrão
    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Channel rack", metric::pad, 0, 200, headerHeight, juce::Justification::centredLeft);

    g.setColour (colour::textFaint);
    g.setFont (font (type::small, Weight::semibold));

    for (int beat = 0; beat * 4 < numSteps; ++beat)
    {
        const auto b = stepBounds (0, beat * 4);
        g.drawText (juce::String (beat + 1), b.getX() + 2, headerHeight - 18, stepSize, 14, juce::Justification::centredLeft);
    }

    {
        const auto r = lengthBounds().toFloat();
        g.setColour (hover.kind == Hit::Kind::lengthBox ? colour::raisedHigh : colour::raised);
        g.fillRoundedRectangle (r, metric::radius);
        icons::draw (g, Icon::channelRack, { r.getX() + 9.0f, r.getCentreY() - 8.0f, 16.0f, 16.0f }, colour::textDim);
        icons::draw (g, Icon::chevronDown, { r.getRight() - 22.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f }, colour::textDim);
        g.setColour (colour::text);
        g.setFont (font (type::body, Weight::medium));
        g.drawText (juce::String (numSteps) + " passos", r.withTrimmedLeft (31.0f).withTrimmedRight (24.0f), juce::Justification::centredLeft);
    }

    // linhas. A agulha repinta só duas colunas: o que fica fora da parte suja não é desenhado (texto e
    // caminhos custam mesmo recortados)
    const auto dirty = g.getClipBounds();

    for (int row = 0; row < (int) rows.size(); ++row)
    {
        const auto& r = rows[(size_t) row];

        if (! dirty.intersects (rowBounds (row)))
            continue;

        if (row == selectedRow)
        {
            g.setColour (colour::raised.withAlpha (0.5f));
            g.fillRoundedRectangle (rowBounds (row).reduced (8, 1).toFloat(), metric::radius);
        }

        // LED de mute no hexágono da marca: laranja cheio = tocando, contorno cinza = mudo
        if (dirty.intersects (muteBounds (row).expanded (8)))
        {
            const auto hex = hexagon (muteBounds (row).toFloat().withSizeKeepingCentre (15.0f, 15.0f));
            g.setColour (r.mute ? colour::raised : colour::signal);
            g.fillPath (hex);
            g.setColour (r.mute ? colour::textDim : colour::signal.darker (0.25f));
            g.strokePath (hex, juce::PathStrokeType (1.2f));
        }

        // armar (notas ao vivo): bolinha cheia em laranja quando armado, vermelha enquanto grava.
        // Desarmado, o aro só aparece com o mouse na linha: raro para quem começa, não enche o rack.
        // Canal de áudio não tem: ele grava pela trilha do mixer.
        if (! r.isAudio && (r.armed || hover.row == row))
        {
            const auto dot = armBounds (row).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            const bool over = hover.kind == Hit::Kind::arm && hover.row == row;

            if (r.armed)
            {
                g.setColour (recording ? colour::meterHigh : colour::signal);
                g.fillEllipse (dot);
            }
            else
            {
                g.setColour (over ? colour::textDim : colour::textFaint.withAlpha (0.55f));
                g.drawEllipse (dot.reduced (0.5f), 1.2f);
            }
        }

        if (dirty.intersects (tapeBounds (row)))
            drawNamePlate (g, tapeBounds (row).toFloat(), r.name, r.colour, row == selectedRow, r.problem, 14.0f,
                           r.frozen ? std::optional<Icon> (Icon::snowflake) : std::nullopt);

        // saída sempre visível: para onde vai o som deste canal
        // saída padrão (o Master ou a trilha que o mixer criou para o canal) em cinza; um destino escolhido,
        // como um grupo "Bateria", fica em branco e chama a atenção
        if (dirty.intersects (outputBounds (row)))
            drawRoutePill (g, outputBounds (row).toFloat(), r.outputName, hover.kind == Hit::Kind::output && hover.row == row,
                           r.output == masterTrackId || r.ownTrack, type::body);

        if (r.isAudio)
        {
            g.setColour (colour::textDim);
            g.setFont (font (type::body));
            g.drawText ("Canal de áudio: os clipes dele ficam na playlist (F5)"_t, notesBounds (row), juce::Justification::centredLeft);
            continue;
        }

        if (! r.notes.empty())
        {
            paintNotesPreview (g, row);
            continue;
        }

        // passos
        for (int s = 0; s < numSteps; ++s)
        {
            if (! dirty.intersects (stepBounds (row, s).expanded (4)))
                continue;

            const auto b = stepBounds (row, s).toFloat();
            const auto& step = r.steps[(size_t) s];
            const bool over = hover.kind == Hit::Kind::step && hover.row == row && hover.step == s;

            if (step.on)
            {
                const float strength = 0.35f + 0.65f * (step.velocity / 127.0f);
                g.setColour (colour::signal.withAlpha (r.mute ? strength * 0.4f : strength));
                g.fillRoundedRectangle (b, metric::radiusSmall);

                if (step.roll > 1)
                {
                    g.setColour (colour::onSignal.withAlpha (0.8f));
                    const float w = b.getWidth() - 8.0f;

                    for (int k = 0; k < step.roll; ++k)
                    {
                        const float x = b.getX() + 4.0f + w * (k + 0.5f) / step.roll;
                        g.fillRect (x - 1.0f, b.getBottom() - 9.0f, 2.0f, 5.0f);
                    }
                }
            }
            else
            {
                // grupos de 4 passos alternam o tom: dá para contar os tempos de relance
                const auto off = (s / 4) % 2 == 0 ? colour::raised : colour::raised.brighter (0.07f);
                g.setColour (over ? colour::raisedHigh.brighter (0.1f) : off);
                g.fillRoundedRectangle (b, metric::radiusSmall);
            }

            if (s == playhead)
            {
                g.setColour (colour::text.withAlpha (0.85f));
                g.drawRoundedRectangle (b.expanded (2.0f), metric::radiusSmall + 2.0f, 1.5f);
            }
        }
    }

    if (rows.empty())
    {
        g.setColour (colour::textDim);
        g.setFont (font (type::title));
        g.drawText ("Arraste samples do Explorer para cá, ou use Adicionar canal."_t,
                    getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (footerHeight), juce::Justification::centred);
    }

    // rodapé
    if (dirty.intersects (getLocalBounds().removeFromBottom (footerHeight)))
    {
        const auto b = addButtonBounds().toFloat();
        g.setColour (hover.kind == Hit::Kind::addButton ? colour::raisedHigh : colour::raised);
        g.fillRoundedRectangle (b, metric::radius);
        icons::draw (g, Icon::plus, { b.getX() + 10.0f, b.getCentreY() - 8.0f, 16.0f, 16.0f }, colour::text);
        g.setColour (colour::text);
        g.setFont (font (type::body, Weight::medium));
        g.drawText ("Adicionar canal", b.withTrimmedLeft (33.0f), juce::Justification::centredLeft);
        icons::draw (g, Icon::chevronDown, { b.getRight() - 24.0f, b.getCentreY() - 7.0f, 14.0f, 14.0f }, colour::textDim);   // abre menu

        g.setColour (colour::textFaint);
        g.setFont (font (type::body));   // dica: 13 px, a revisão de iniciante pediu
        g.drawText ("Clique liga o passo, arraste para pintar, botão direito apaga. Ctrl+roda: força. Alt+roda: roll. Clique no nome: canal do piano roll."_t,
                    addButtonBounds().getRight() + 16, getHeight() - footerHeight, getWidth() - addButtonBounds().getRight() - 32, footerHeight - 4,
                    juce::Justification::centredLeft);
    }

    if (dragOver)
    {
        g.setColour (colour::signal.withAlpha (0.12f));
        g.fillRect (getLocalBounds());
        g.setColour (colour::signal);
        g.drawRect (getLocalBounds(), 2);
    }
}

void ChannelRack::paintNotesPreview (juce::Graphics& g, int row)
{
    using namespace theme;
    const auto& r = rows[(size_t) row];
    const auto b = notesBounds (row).toFloat();
    const bool over = hover.kind == Hit::Kind::notes && hover.row == row;

    g.setColour (over ? colour::raisedHigh : colour::raised);
    g.fillRoundedRectangle (b, metric::radiusSmall);

    // tempos marcados de leve, como os grupos de 4 passos
    g.setColour (colour::shade.withAlpha (0.45f));
    for (int beat = 1; beat * 4 < numSteps; ++beat)
        g.fillRect (b.getX() + b.getWidth() * (float) (beat * 4) / (float) numSteps, b.getY() + 3.0f, 1.0f, b.getHeight() - 6.0f);

    int lo = 127, hi = 0;

    for (auto& n : r.notes)
    {
        lo = juce::jmin (lo, n.key);
        hi = juce::jmax (hi, n.key);
    }

    const auto inner = b.reduced (5.0f, 4.0f);
    const int span = hi - lo;
    const float noteH = juce::jlimit (3.0f, 6.0f, inner.getHeight() / (float) (span + 1));

    for (auto& n : r.notes)
    {
        const float x = inner.getX() + inner.getWidth() * (float) n.start / (float) patternTicks;
        const float w = juce::jmax (2.0f, inner.getWidth() * (float) n.length / (float) patternTicks);
        const float y = span == 0 ? inner.getCentreY() - noteH * 0.5f
                                  : inner.getY() + (inner.getHeight() - noteH) * (float) (hi - n.key) / (float) span;
        const float strength = 0.45f + 0.55f * ((float) n.velocity / 127.0f);
        g.setColour (colour::signal.withAlpha (r.mute ? strength * 0.4f : strength));
        g.fillRoundedRectangle (juce::Rectangle<float> (x, y, juce::jmax (1.0f, juce::jmin (w, inner.getRight() - x)), noteH), 1.0f);
    }

    if (playhead >= 0)
    {
        g.setColour (colour::text);
        g.fillRect (inner.getX() + inner.getWidth() * (float) playhead / (float) juce::jmax (1, numSteps), b.getY() + 2.0f, 1.5f, b.getHeight() - 4.0f);
    }
}

//==============================================================================
void ChannelRack::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.getPosition());

    if (h.kind != hover.kind || h.row != hover.row || h.step != hover.step)
    {
        hover = h;
        setMouseCursor (h.kind == Hit::Kind::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ChannelRack::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void ChannelRack::setStep (int row, int step, bool on)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || ! juce::isPositiveAndBelow (step, numSteps))
        return;

    if (rows[(size_t) row].steps[(size_t) step].on == on)
        return;

    app.run ("steps.set", args ({ { "channel", rows[(size_t) row].id }, { "index", step }, { "on", on } }));
}

void ChannelRack::mouseDown (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.getPosition());

    switch (h.kind)
    {
        case Hit::Kind::step:
        {
            paintRow = h.row;
            painting = true;
            paintValue = ! e.mods.isPopupMenu() && ! rows[(size_t) h.row].steps[(size_t) h.step].on;
            lastPaintedStep = h.step;
            app.getCommands().beginGesture ("Editar passos");
            setStep (h.row, h.step, paintValue);
            break;
        }

        case Hit::Kind::mute:
            app.run ("channel.setMute", args ({ { "channel", rows[(size_t) h.row].id } }));
            break;

        case Hit::Kind::arm:
        {
            const auto& r = rows[(size_t) h.row];
            const auto result = app.run ("record.arm", args ({ { "channel", r.id }, { "on", ! r.armed } }));
            if (! result.ok && onStatus) onStatus (result.error, true);
            break;
        }

        case Hit::Kind::tape:
        {
            const auto id = rows[(size_t) h.row].id;
            setSelectedChannel (id);
            if (onChannelSelected) onChannelSelected (id);
            if (e.mods.isPopupMenu())
                showTapeMenu (h.row);
            break;
        }

        case Hit::Kind::notes:
        {
            const auto id = rows[(size_t) h.row].id;
            setSelectedChannel (id);
            if (onOpenPianoRoll) onOpenPianoRoll (id);
            break;
        }

        case Hit::Kind::output:     showOutputMenu (h.row); break;
        case Hit::Kind::addButton:  showAddMenu(); break;
        case Hit::Kind::lengthBox:  showLengthMenu(); break;
        case Hit::Kind::none:       break;
    }
}

void ChannelRack::mouseDrag (const juce::MouseEvent& e)
{
    if (! painting)
        return;

    // pinta ao longo da mesma linha, preenchendo passos pulados num arrasto rápido
    for (int s = 0; s < numSteps; ++s)
    {
        const auto b = stepBounds (paintRow, s);

        if (e.x >= b.getX() - 2 && e.x < b.getRight() + (s % 4 == 3 ? 12 : 3))
        {
            if (s != lastPaintedStep)
            {
                const int from = juce::jmin (s, lastPaintedStep), to = juce::jmax (s, lastPaintedStep);
                for (int k = from; k <= to; ++k)
                    setStep (paintRow, k, paintValue);
                lastPaintedStep = s;
            }
            break;
        }
    }
}

void ChannelRack::mouseUp (const juce::MouseEvent&)
{
    if (painting)
    {
        painting = false;
        paintRow = -1;
        app.getCommands().endGesture();
    }
}

void ChannelRack::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.getPosition());

    if (h.kind != Hit::Kind::tape)
        return;

    // instrumento VST3 ou WHYNOT Synth: duplo clique abre a janela dele (renomear fica no menu)
    if (rows[(size_t) h.row].isPlugin || rows[(size_t) h.row].isSynth)
    {
        const auto result = app.run ("plugin.openEditor", args ({ { "channel", rows[(size_t) h.row].id } }));

        if (! result.ok && onStatus)
            onStatus (rows[(size_t) h.row].problem ? rows[(size_t) h.row].problemText : result.error, true);

        return;
    }

    // WHYNOT Piano: duplo clique abre o piano roll (o timbre fica no botão direito)
    if (rows[(size_t) h.row].isPiano)
    {
        if (onOpenPianoRoll)
            onOpenPianoRoll (rows[(size_t) h.row].id);

        return;
    }

    // sampler: duplo clique abre os ajustes do canal, como o VST3 abre o plugin (renomear fica no menu)
    if (! rows[(size_t) h.row].isAudio)
    {
        showSamplerPanel (h.row);
        return;
    }

    renameChannel (h.row);
}

void ChannelRack::showSamplerPanel (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    SamplerPanel::show (app, rows[(size_t) row].id, localAreaToGlobal (tapeBounds (row)), onStatus);
}

void ChannelRack::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = hitTest (e.getPosition());

    if (h.kind == Hit::Kind::step && (e.mods.isCtrlDown() || e.mods.isAltDown()))
    {
        const auto& step = rows[(size_t) h.row].steps[(size_t) h.step];
        const bool up = w.deltaY > 0;
        auto params = args ({ { "channel", rows[(size_t) h.row].id }, { "index", h.step }, { "on", true } });

        if (e.mods.isCtrlDown())
            params.getDynamicObject()->setProperty ("velocity", juce::jlimit (1, 127, step.velocity + (up ? 8 : -8)));
        else
            params.getDynamicObject()->setProperty ("roll", nextRoll (step.roll, up));

        app.run ("steps.set", params);
        return;
    }

    juce::Component::mouseWheelMove (e, w);
}

juce::String ChannelRack::getTooltip()
{
    if (hover.kind == Hit::Kind::tape && juce::isPositiveAndBelow (hover.row, (int) rows.size()))
    {
        const auto& r = rows[(size_t) hover.row];
        if (r.problem)
            return r.problemText;

        const juce::String frozenNote = r.frozen ? " Congelado: o som sai de um WAV até descongelar (botão direito)."_t : juce::String();

        if (r.isSynth)
            return r.name + ": duplo clique abre o WHYNOT Synth, botão direito abre opções."_t + frozenNote;

        if (r.isPiano)
            return r.name + ": duplo clique abre o piano roll, botão direito troca o timbre."_t + frozenNote;

        return r.name + (r.isPlugin  ? ": duplo clique abre o plugin, botão direito abre opções."_t
                         : r.isAudio ? ": duplo clique renomeia, botão direito abre opções."_t
                                     : ": duplo clique abre os ajustes do sampler, botão direito abre opções."_t)
               + frozenNote;
    }

    if (hover.kind == Hit::Kind::arm && juce::isPositiveAndBelow (hover.row, (int) rows.size()))
        return rows[(size_t) hover.row].armed
                 ? "Armado: com o REC aceso, o que você tocar no teclado MIDI vira notas deste canal. Clique para desarmar."_t
                 : "Armar para gravar notas: com o REC aceso, o que você tocar no teclado MIDI vira notas deste canal."_t;

    if (hover.kind == Hit::Kind::output)   return "Para onde vai o som deste canal: a trilha do mixer com os efeitos dele (em cinza, o padrão). Clique para mandar para outra, como um grupo."_t;
    if (hover.kind == Hit::Kind::notes)    return "Notas do piano roll. Clique para editar (F7)."_t;
    if (hover.kind == Hit::Kind::mute)     return "Silenciar o canal";
    if (hover.kind == Hit::Kind::step && juce::isPositiveAndBelow (hover.row, (int) rows.size()))
    {
        const auto& s = rows[(size_t) hover.row].steps[(size_t) hover.step];
        if (s.on)
            return "Passo " + juce::String (hover.step + 1) + ": força "_t + juce::String (s.velocity)
                   + (s.roll > 1 ? ", roll de " + juce::String (s.roll) : juce::String());
    }

    return {};
}

//==============================================================================
void ChannelRack::showTapeMenu (int row)
{
    juce::PopupMenu menu;
    const auto& r = rows[(size_t) row];

    if (r.isPlugin)
    {
        menu.addItem (8, "Abrir plugin", ! r.problem);
        menu.addSeparator();
    }

    if (r.isSynth)
    {
        menu.addItem (8, "Abrir WHYNOT Synth");
        menu.addSeparator();
    }

    if (r.isPiano)
    {
        for (int i = 0; i < (int) std::size (audio::pianoPresets); ++i)
            menu.addItem (20 + i, "Timbre: "_t + juce::String::fromUTF8 (audio::pianoPresets[i].name), true,
                          r.pianoPreset == audio::pianoPresets[i].id);

        menu.addSeparator();
    }

    const bool instrument = r.isPlugin || r.isSynth || r.isPiano;

    if (! instrument && ! r.isAudio)
    {
        menu.addItem (9, "Ajustes do sampler: afinação, envelope, queda de tom"_t);
        menu.addSeparator();
    }

    menu.addItem (1, "Renomear");

    if (! instrument)
        menu.addItem (2, r.isAudio ? "Trocar arquivo" : "Trocar sample");

    if (! r.isAudio)
    {
        menu.addItem (5, "Abrir no piano roll (F7)");
        menu.addSeparator();
        if (! instrument)
            menu.addItem (4, "Cortar a si mesmo (nota nova corta a anterior)", true, r.cutItself);

        if (! r.isPiano)
            menu.addItem (6, "Mono: uma nota por vez, com slide (808)", true, r.mono);

        if (! instrument)
            menu.addItem (7, "Respeitar a duração da nota (808)"_t, true, ! r.oneShot);

        if (! instrument)
        {
            // Fatiador: uma fatia por tecla, da grave para a aguda, e as notas no piano roll.
            juce::PopupMenu slicing;
            slicing.addSectionHeader ("Pelas batidas"_t);
            slicing.addItem (30, "Suave (só as fortes)"_t);
            slicing.addItem (31, "Médio"_t);
            slicing.addItem (32, "Forte (até as fracas)"_t);
            slicing.addSectionHeader ("Pela grade"_t);
            slicing.addItem (33, "1/4 (um tempo)"_t);
            slicing.addItem (34, "1/8");
            slicing.addItem (35, "1/16");
            slicing.addSeparator();
            slicing.addItem (36, "Mandar as fatias para o piano roll"_t, r.slices > 0);
            slicing.addItem (37, "Tirar as fatias (volta o sample inteiro)"_t, r.slices > 0);
            menu.addSubMenu (r.slices > 0 ? "Fatiar ("_t + juce::String (r.slices) + " fatias)" : "Fatiar"_t, slicing);
        }

        // Grupo de corte: uma nota de um canal do grupo corta os outros (o chimbal fechado corta o aberto).
        juce::PopupMenu choke;
        choke.addItem (50, "Nenhum", true, r.chokeGroup == 0);

        for (int g = 1; g <= 8; ++g)
            choke.addItem (50 + g, juce::String (g), true, r.chokeGroup == g);

        menu.addSubMenu (r.chokeGroup > 0 ? "Grupo de corte: "_t + juce::String (r.chokeGroup) : "Grupo de corte"_t, choke);

        // Swing: atrasa os passos dos contratempos (100% = sensação de tercina); passos fixos, mais fáceis.
        juce::PopupMenu swing;
        const float swings[] = { 0.0f, 25.0f, 50.0f, 66.0f, 75.0f };

        for (int i = 0; i < (int) std::size (swings); ++i)
            swing.addItem (60 + i, i == 0 ? juce::String ("Sem swing") : juce::String (juce::roundToInt (swings[i])) + "%", true,
                           std::abs (r.swing - swings[i]) < 0.5f);

        menu.addSubMenu (r.swing > 0.0f ? "Swing: "_t + juce::String (juce::roundToInt (r.swing)) + "%" : "Swing"_t, swing);
    }

    // congelar: a trilha do mixer por onde o canal sai (e os canais que saem por ela) vira um WAV
    menu.addSeparator();

    if (r.output == masterTrackId)
        menu.addItem (10, "Congelar canal (mande para uma trilha do mixer antes)"_t, false, false);
    else
        menu.addItem (10, "Congelar canal: o som vira um WAV e a CPU folga"_t, true, r.frozen);

    // renderizar no lugar (bloco 2, 23/10): o que o canal toca na música vira áudio numa raia, e ele fica mudo
    menu.addItem (11, "Renderizar no lugar: vira áudio na playlist, com os efeitos da trilha"_t);
    menu.addItem (12, "Renderizar seco: vira áudio, sem os efeitos (eles continuam ao vivo)"_t);

    menu.addSeparator();
    menu.addItem (3, "Remover canal");

    const auto id = rows[(size_t) row].id;
    const bool frozen = r.frozen;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (tapeBounds (row))),
                        [this, row, id, frozen] (int choice)
                        {
                            if (choice == 10)
                            {
                                const auto result = app.run ("channel.setFrozen", args ({ { "channel", id }, { "on", ! frozen } }));

                                if (onStatus && ! result.ok)
                                    onStatus (result.error, true);
                                else if (onStatus && result.value["message"].toString().isNotEmpty())
                                    onStatus (result.value["message"].toString(), false);
                            }

                            if (choice == 11 || choice == 12)
                            {
                                // o render roda aqui, na hora (como o consolidar): o cursor de espera mostra que não travou
                                juce::MouseCursor::showWaitCursor();
                                const auto result = app.run ("channel.renderInPlace", args ({ { "channel", id }, { "effects", choice == 11 } }));
                                juce::MouseCursor::hideWaitCursor();

                                if (onStatus)
                                {
                                    const auto& p = app.getModel().get();
                                    const auto* lane = result.ok ? p.findPlaylistTrack (result.value["lane"].toString()) : nullptr;
                                    onStatus (result.ok ? "Renderizado: virou áudio na raia \""_t + (lane != nullptr ? lane->name : juce::String())
                                                              + "\", e o canal ficou mudo. Ctrl+Z desfaz."_t
                                                        : result.error,
                                              ! result.ok);
                                }
                            }

                            if (choice == 1) renameChannel (row);
                            if (choice == 2) chooseSamples (row);
                            if (choice == 3) app.run ("channel.remove", args ({ { "channel", id } }));
                            if (choice == 4) app.run ("channel.setCutItself", args ({ { "channel", id } }));
                            if (choice == 5 && onOpenPianoRoll) onOpenPianoRoll (id);
                            if (choice == 6) app.run ("channel.setMono", args ({ { "channel", id } }));
                            if (choice == 7) app.run ("channel.setOneShot", args ({ { "channel", id } }));
                            if (choice == 8) app.run ("plugin.openEditor", args ({ { "channel", id } }));
                            if (choice == 9) showSamplerPanel (row);

                            if (choice >= 50 && choice <= 58)
                                app.run ("channel.setChoke", args ({ { "channel", id }, { "group", choice - 50 } }));

                            if (choice >= 60 && choice <= 64)
                            {
                                const float swings[] = { 0.0f, 25.0f, 50.0f, 66.0f, 75.0f };
                                app.run ("channel.setSwing", args ({ { "channel", id }, { "amount", swings[choice - 60] } }));
                            }

                            if (choice >= 30 && choice <= 32)
                                slice (id, args ({ { "mode", "attack" }, { "sensitivity", choice == 30 ? "dull" : choice == 31 ? "medium" : "sharp" } }));

                            if (choice >= 33 && choice <= 35)
                                slice (id, args ({ { "mode", "grid" }, { "division", choice == 33 ? "1/4" : choice == 34 ? "1/8" : "1/16" } }));

                            if (choice == 36)
                            {
                                const auto result = app.run ("channel.slicesToPianoRoll", args ({ { "channel", id } }));
                                if (onStatus) onStatus (result.ok ? "Uma nota por fatia no piano roll do padrão atual."_t : result.error, ! result.ok);
                            }

                            if (choice == 37)
                                app.run ("channel.slice", args ({ { "channel", id }, { "mode", "none" } }));

                            if (choice >= 20 && choice < 20 + (int) std::size (audio::pianoPresets))
                                app.run ("piano.setPreset", args ({ { "channel", id }, { "preset", juce::String (audio::pianoPresets[choice - 20].id) } }));
                        });
}

void ChannelRack::showOutputMenu (int row)
{
    juce::PopupMenu menu;
    const auto& p = app.getModel().get();
    const auto current = rows[(size_t) row].output;
    juce::StringArray ids;

    for (auto& m : p.mixer)
    {
        ids.add (m.id);
        menu.addItem (ids.size(), m.name, true, m.id == current);
    }

    const auto channel = rows[(size_t) row].id;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (outputBounds (row))),
                        [this, ids, channel] (int choice)
                        {
                            if (choice > 0)
                                app.run ("channel.setOutput", args ({ { "channel", channel }, { "track", ids[choice - 1] } }));
                        });
}

void ChannelRack::showLengthMenu()
{
    juce::PopupMenu menu;

    for (int steps : { 8, 16, 32, 64 })
        menu.addItem (steps, juce::String (steps) + " passos", true, steps == numSteps);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (lengthBounds())),
                        [this] (int choice)
                        {
                            if (choice > 0)
                                app.run ("pattern.setLength", args ({ { "steps", choice } }));
                        });
}

void ChannelRack::chooseSamples (int row)
{
    chooser = std::make_unique<juce::FileChooser> (row < 0 ? "Escolha os samples"_t : "Escolha o novo sample"_t,
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   audioWildcard);

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                       | (row < 0 ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
    const auto channel = row >= 0 ? rows[(size_t) row].id : juce::String();

    chooser->launchAsync (chooserFlags, [this, channel] (const juce::FileChooser& fc)
    {
        for (auto& f : fc.getResults())
        {
            const auto result = channel.isEmpty()
                ? app.run ("channel.addSampler", args ({ { "path", f.getFullPathName() } }))
                : app.run ("channel.setSample", args ({ { "channel", channel }, { "path", f.getFullPathName() } }));

            if (! result.ok && onStatus)
                onStatus (result.error, true);
        }
    });
}

juce::PopupMenu ChannelRack::createAddMenu (std::vector<std::function<void()>>& actions)
{
    using Kind = Favorites::Kind;
    auto& favourites = Favorites::get();
    auto add = [&actions] (std::function<void()> f) { actions.push_back (std::move (f)); return (int) actions.size(); };
    juce::PopupMenu menu;

    std::vector<juce::PluginDescription> instruments;

    for (auto& d : app.knownPlugins())
        if (d.isInstrument)
            instruments.push_back (d);

    favourites.favoritesFirst (instruments, Kind::plugin, [] (const juce::PluginDescription& d) { return d.createIdentifierString(); });

    // Favoritos primeiro (pedido de 21/10). A estrela de cada item desmarca sem fechar o menu.
    int numFavourites = 0;
    auto favouritesHeader = [&menu, &numFavourites]
    {
        if (numFavourites++ == 0)
            menu.addSectionHeader ("Favoritos");
    };

    for (auto& d : instruments)
    {
        if (const auto id = d.createIdentifierString(); favourites.contains (Kind::plugin, id))
        {
            favouritesHeader();
            menu.addItem (favui::menuItem (add ([this, id] { addPluginChannel (id); }), d.name + " (VST3)", Kind::plugin, id));
        }
    }

    for (auto& id : favourites.list (Kind::synthPreset))
    {
        if (const auto* preset = synth::findPreset (id.toStdString()))
        {
            favouritesHeader();
            menu.addItem (favui::menuItem (add ([this, id] { addSynth (id); }), juce::String::fromUTF8 (preset->name.c_str()) + " (WHYNOT Synth)",
                                           Kind::synthPreset, id));
        }
    }

    if (numFavourites > 0)
        menu.addSeparator();

    menu.addItem (add ([this] { chooseSamples (-1); }), "Sample ou loop (arquivo de áudio)..."_t);
    menu.addItem (add ([this] { addSynth(); }), "WHYNOT Synth (melodias, baixos e 808)");
    menu.addItem (add ([this] { addPiano(); }), "WHYNOT Piano (piano de cauda gravado)");
    menu.addItem (add ([this] { chooseBeatToSlice(); }), "Fatiar um beat ou loop (arquivo de áudio)..."_t);

    juce::PopupMenu vst;

    for (auto& d : instruments)
    {
        const auto id = d.createIdentifierString();
        vst.addItem (favui::menuItem (add ([this, id] { addPluginChannel (id); }),
                                      d.name + (d.manufacturerName.isNotEmpty() ? " (" + d.manufacturerName + ")" : juce::String()),
                                      Kind::plugin, id));
    }

    if (vst.getNumItems() > 0)
        menu.addSubMenu ("Instrumentos VST3", vst);

    return menu;
}

void ChannelRack::showAddMenu()
{
    auto actions = std::make_shared<std::vector<std::function<void()>>>();
    auto menu = createAddMenu (*actions);
    juce::Component::SafePointer<ChannelRack> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (addButtonBounds())),
                        [safe, actions] (int choice)
                        {
                            if (safe != nullptr && choice > 0 && choice <= (int) actions->size())
                                (*actions)[(size_t) choice - 1]();
                        });
}

void ChannelRack::chooseBeatToSlice()
{
    chooser = std::make_unique<juce::FileChooser> ("Escolha o beat ou o loop para fatiar"_t,
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   audioWildcard);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (! file.existsAsFile())
                                  return;

                              const auto result = app.run ("channel.addSlicer", args ({ { "path", file.getFullPathName() } }));

                              if (! result.ok)
                              {
                                  if (onStatus) onStatus (result.error, true);
                                  return;
                              }

                              const auto channel = result.value["channel"].toString();
                              setSelectedChannel (channel);

                              if (onChannelSelected) onChannelSelected (channel);

                              if (onStatus)
                                  onStatus (juce::String ((int) result.value["slices"]) + " fatias nas teclas "_t
                                                + Project::keyName ((int) result.value["firstKey"]) + " a " + Project::keyName ((int) result.value["lastKey"])
                                                + ", a " + juce::String ((double) result.value["bpm"], 0) + " BPM. As notas estão no piano roll (F7)."_t,
                                            false);
                          });
}

void ChannelRack::slice (const juce::String& channel, const juce::var& how)
{
    auto a = how;
    a.getDynamicObject()->setProperty ("channel", channel);
    a.getDynamicObject()->setProperty ("toPianoRoll", true);
    const auto result = app.run ("channel.slice", a);

    if (onStatus)
        onStatus (result.ok ? juce::String ((int) result.value["slices"]) + " fatias nas teclas "_t
                                  + Project::keyName ((int) result.value["firstKey"]) + " a " + Project::keyName ((int) result.value["lastKey"])
                                  + ". As notas do piano roll deste canal foram trocadas por uma por fatia (Ctrl+Z desfaz)."_t
                            : result.error,
                  ! result.ok);
}

void ChannelRack::addPluginChannel (const juce::String& pluginId)
{
    const auto result = app.run ("channel.addPlugin", args ({ { "plugin", pluginId } }));

    if (! result.ok)
    {
        if (onStatus) onStatus (result.error, true);
        return;
    }

    const auto channel = result.value["channel"].toString();
    setSelectedChannel (channel);

    if (onChannelSelected) onChannelSelected (channel);
    if (onStatus) onStatus ("Instrumento no channel rack. Duplo clique no nome abre o plugin."_t, false);
}

void ChannelRack::addSynth (const juce::String& preset)
{
    const auto result = preset.isEmpty() ? app.run ("channel.addSynth") : app.run ("channel.addSynth", args ({ { "preset", preset } }));

    if (! result.ok)
    {
        if (onStatus) onStatus (result.error, true);
        return;
    }

    const auto channel = result.value["channel"].toString();
    setSelectedChannel (channel);

    if (onChannelSelected) onChannelSelected (channel);

    if (onStatus)
        onStatus (preset.isEmpty() ? "WHYNOT Synth no channel rack. Escolha um preset na janela; duplo clique no nome reabre."_t
                                   : "WHYNOT Synth com o favorito no channel rack. Duplo clique no nome reabre a janela."_t,
                  false);

    app.run ("plugin.openEditor", args ({ { "channel", channel } }));
}

void ChannelRack::addPiano()
{
    const auto result = app.run ("channel.addPiano");

    if (! result.ok)
    {
        if (onStatus) onStatus (result.error, true);
        return;
    }

    const auto channel = result.value["channel"].toString();
    setSelectedChannel (channel);

    if (onChannelSelected) onChannelSelected (channel);
    if (onStatus) onStatus ("WHYNOT Piano no channel rack. Duplo clique abre o piano roll; botão direito troca o timbre."_t, false);
}

void ChannelRack::renameChannel (int row)
{
    auto* w = new juce::AlertWindow ("Renomear canal", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", rows[(size_t) row].name);
    w->addButton ("Renomear", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    const auto id = rows[(size_t) row].id;

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, id] (int choice)
    {
        if (choice == 1)
        {
            const auto result = app.run ("channel.rename", args ({ { "channel", id }, { "name", w->getTextEditorContents ("name") } }));

            if (! result.ok && onStatus)
                onStatus (result.error, true);
        }
    }), true);
}

//==============================================================================
bool ChannelRack::isInterestedInDragSource (const SourceDetails& details)
{
    const auto text = details.description.toString();

    if (text.startsWith (SampleBrowser::dragPrefix))
        return isInterestedInFileDrag ({ text.fromFirstOccurrenceOf (SampleBrowser::dragPrefix, false, false) });

    if (! text.startsWith ("plugin:"))
        return false;

    const auto id = text.fromFirstOccurrenceOf ("plugin:", false, false);

    for (auto& d : app.knownPlugins())
        if (d.createIdentifierString() == id)
            return d.isInstrument;   // efeito vai no mixer, não no rack

    return false;
}

void ChannelRack::itemDropped (const SourceDetails& details)
{
    dragOver = false;
    repaint();

    // som do navegador: o mesmo caminho de um arquivo solto do Explorer (no nome troca o sample, fora cria canal)
    if (const auto text = details.description.toString(); text.startsWith (SampleBrowser::dragPrefix))
    {
        filesDropped ({ text.fromFirstOccurrenceOf (SampleBrowser::dragPrefix, false, false) },
                      details.localPosition.x, details.localPosition.y);
        return;
    }

    addPluginChannel (details.description.toString().fromFirstOccurrenceOf ("plugin:", false, false));
}

bool ChannelRack::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3") || juce::File (f).isDirectory())   // pasta = kit
            return true;

    return false;
}

void ChannelRack::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragOver = false;
    const auto h = hitTest ({ x, y });

    // soltar sobre o nome de um canal troca o sample dele; em qualquer outro lugar, cria canais
    if (h.kind == Hit::Kind::tape && files.size() == 1)
    {
        app.run ("channel.setSample", args ({ { "channel", rows[(size_t) h.row].id }, { "path", files[0] } }));
        repaint();
        return;
    }

    int added = 0;
    juce::String lastRoot;   // nota achada no último sample com tom (808, melodia)
    juce::String last808;    // o último 808, que nasce mono, com glide e respeitando a nota

    // Pasta de kit: um canal por som, na ordem da bateria, com o papel de cada um.
    for (auto& f : files)
    {
        if (! juce::File (f).isDirectory())
            continue;

        const auto kit = app.run ("channel.addKit", args ({ { "folder", f } }));

        if (! kit.ok)
        {
            if (onStatus) onStatus (kit.error, true);
            continue;
        }

        const int count = kit.value["channels"].size(), skipped = (int) kit.value["skipped"];
        int bass = 0, hats = 0;

        for (auto& role : *kit.value["roles"].getArray())
        {
            bass += role.toString() == "808" ? 1 : 0;
            hats += role.toString() == "hat" || role.toString() == "openHat" ? 1 : 0;
        }

        if (onStatus)
            onStatus ("Kit "_t + juce::File (f).getFileName() + ": " + juce::String (count) + " canais"
                          + (bass > 0 ? ", " + juce::String (bass) + " 808 mono com glide"_t : juce::String())
                          + (hats > 0 ? ", chimbais no grupo de corte 1"_t : juce::String())
                          + (skipped > 0 ? " (" + juce::String (skipped) + " sons ficaram de fora: o limite é 64)"_t : juce::String()),
                      false);
    }

    for (auto& f : files)
    {
        if (! juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            continue;

        const auto result = app.run ("channel.addSampler", args ({ { "path", f } }));

        if (result.ok)
        {
            ++added;

            if (result.value["role"].toString() == "808")
                last808 = juce::File (f).getFileNameWithoutExtension();

            if ((bool) result.value["rootDetected"])
            {
                const int cents = juce::roundToInt ((double) result.value["rootCents"]);
                lastRoot = noteNamePt ((int) result.value["rootKey"])
                           + (cents != 0 ? (cents > 0 ? " +" : " ") + juce::String (cents) + " cents" : juce::String());
            }
        }
        else if (onStatus)
            onStatus (result.error, true);
    }

    if (added > 0 && onStatus)   // só arquivos: a pasta de kit já deu o aviso dela
        onStatus ((added == 1 ? juce::String ("Canal adicionado") : juce::String (added) + " canais adicionados")
                      + (lastRoot.isNotEmpty() ? juce::String (". Nota do sample: ") + lastRoot : juce::String())
                      + (last808.isNotEmpty() ? ". "_t + last808 + " entrou como 808: mono, com glide e respeitando a nota"_t : juce::String())
                      + ". Duplo clique no nome abre os ajustes."_t,
                  false);

    repaint();
}

} // namespace whynot
