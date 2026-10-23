#include "SamplerPanel.h"
#include "Theme.h"
#include "../app/AppController.h"

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

    // Os controles mostram as unidades de quem usa: sustain, início e as pontas do laço em %,
    // o resto como no modelo. A ordem é a dos campos em channel.setSampler.
    const dsp::ParamInfo samplerParams[] = {
        { "tune",        "Afinação",       -24.0f, 24.0f,   0.0f,   "st", dsp::ParamKind::continuous, false, {},
          "Transpõe o canal em semitons, com fração (0,50 st = 50 cents)" },
        { "attackMs",    "Ataque",          0.1f,  1000.0f, 1.0f,   "ms", dsp::ParamKind::continuous, true,  {},
          "Quanto tempo a nota leva para chegar ao volume cheio" },
        { "decayMs",     "Decay",           10.0f, 20000.0f, 300.0f, "ms", dsp::ParamKind::continuous, true, {},
          "Tempo para cair do pico até o sustain (-60 dB do caminho)" },
        { "sustain",     "Sustain",         0.0f,  100.0f,  100.0f, "%",  dsp::ParamKind::continuous, false, {},
          "Nível enquanto a nota é segurada. 100% = sem decaimento; 0% = a nota some sozinha (decay do 808)" },
        { "releaseMs",   "Release",         1.0f,  5000.0f, 30.0f,  "ms", dsp::ParamKind::continuous, true,  {},
          "Quanto o som demora para sumir quando a nota acaba (no canal que respeita a nota, como o 808)" },
        { "pitchDrop",   "Queda",          -24.0f, 48.0f,   0.0f,   "st", dsp::ParamKind::continuous, false, {},
          "A nota começa tantos semitons acima e cai até ela: o \"drop\" do 808. Um slide não reinicia a queda" },
        { "pitchDropMs", "Tempo da queda",  5.0f,  2000.0f, 60.0f,  "ms", dsp::ParamKind::continuous, true,  {},
          "Quanto tempo a queda de tom leva (aos 100% falta 1% do caminho)" },
        { "start",       "Início",          0.0f,  99.0f,   0.0f,   "%",  dsp::ParamKind::continuous, false, {},
          "Onde a leitura do sample começa: pula silêncio ou o ataque" },
        { "loop",        "Laço",            0.0f,  1.0f,    0.0f,   "",   dsp::ParamKind::toggle,     false, {},
          "Enquanto a nota soa, a leitura volta do fim do laço para o começo dele: a nota mais longa que o sample continua soando (o 808 esticado)" },
        { "loopStart",   "Começo do laço",  0.0f,  100.0f,  0.0f,   "%",  dsp::ParamKind::continuous, false, {},
          "Onde o laço começa, em % do sample. Também dá para arrastar a ponta na onda" },
        { "loopEnd",     "Fim do laço",     0.0f,  100.0f,  100.0f, "%",  dsp::ParamKind::continuous, false, {},
          "Onde o laço acaba e volta para o começo, em % do sample" },
        { "loopXfadeMs", "Crossfade",       0.0f,  500.0f,  20.0f,  "ms", dsp::ParamKind::continuous, false, {},
          "Mistura antes do fim do laço, para a volta não estalar. Com 0 ms a volta é seca" },
    };

    constexpr int numParams = (int) std::size (samplerParams);

    float modelValue (const Channel& c, int index)
    {
        switch (index)
        {
            case 0: return c.tune;
            case 1: return c.attackMs;
            case 2: return c.decayMs;
            case 3: return c.sustain * 100.0f;
            case 4: return c.releaseMs;
            case 5: return c.pitchDrop;
            case 6: return c.pitchDropMs;
            case 7: return c.start * 100.0f;
            case 8: return c.loop ? 1.0f : 0.0f;
            case 9: return c.loopStart * 100.0f;
            case 10: return c.loopEnd * 100.0f;
            case 11: return c.loopXfadeMs;
            default: return 0.0f;
        }
    }

    float toModel (int index, float shown)   { return index == 3 || index == 7 || index == 9 || index == 10 ? shown / 100.0f : shown; }

    juce::String noteName (int key)   { return Project::keyName (key); }   // "C2", como no piano roll

    juce::String centsText (float cents)
    {
        const int c = juce::roundToInt (cents);
        return c == 0 ? juce::String() : (c > 0 ? " +" : " ") + juce::String (c) + " cents";
    }

    constexpr int headerHeight = 64, rootRowHeight = 44, waveHeight = 64, groupTitle = 20;
}

//==============================================================================
SamplerPanel::SamplerPanel (AppController& a, const juce::String& id) : app (a), channelId (id)
{
    for (int key = 0; key < 128; ++key)
        rootBox.addItem (noteName (key) + "  (" + juce::String (key) + ")", key + 1);

    theme::styleComboBox (rootBox);
    rootBox.setTooltip ("Nota em que o sample soa no tom original. Escolher à mão zera os cents."_t);
    rootBox.onChange = [this]
    {
        if (rootBox.getSelectedId() > 0)
            app.run ("channel.setSampler", args ({ { "channel", channelId }, { "rootKey", rootBox.getSelectedId() - 1 }, { "rootCents", 0.0 } }));
    };
    rootBox.setWantsKeyboardFocus (false);
    addAndMakeVisible (rootBox);

    detectButton.setTooltip ("Ouve o sample e acha a nota e os cents (808, baixo, melodia)"_t);
    detectButton.onClick = [this] { detect(); };
    detectButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (detectButton);

    findLoopButton.setTooltip ("Acha um laço no corpo do som, com as pontas em fase, e liga o laço"_t);
    findLoopButton.onClick = [this] { findLoop(); };
    findLoopButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (findLoopButton);

    wave.setTooltip ("A onda do sample. A linha branca é o início da leitura; o trecho laranja é o laço: "
                     "arraste as pontas, ou o meio para mover os dois juntos."_t);
    addAndMakeVisible (wave);

    for (int i = 0; i < numParams; ++i)
    {
        auto* c = controls.add (new ParamControl (samplerParams[i]));
        const auto key = samplerParams[i].id;
        c->onGestureStart = [this, i] { app.getCommands().beginGesture (fxui::utf8 (samplerParams[i].name)); };
        c->onGestureEnd = [this] { app.getCommands().endGesture(); };
        c->onChange = [this, key, i] (float v) { send (key, toModel (i, v)); };
        addAndMakeVisible (c);
    }

    groups = { { "AFINAÇÃO"_t,      { controls[0] } },
               { "ENVELOPE",         { controls[1], controls[2], controls[3], controls[4] } },
               { "QUEDA DE TOM"_t,   { controls[5], controls[6] } },
               { "SAMPLE",           { controls[7] } },
               { "LAÇO"_t,           { controls[8], controls[9], controls[10], controls[11] } } };
    controls[6]->setCaption ("TEMPO");
    controls[8]->setCaption ("LIGADO");
    controls[9]->setCaption ("COMEÇO"_t);
    controls[10]->setCaption ("FIM");

    int cells = 0;
    for (auto& g : groups) cells += (int) g.controls.size();

    const int width = theme::metric::pad * 2 + cells * ParamControl::cellWidth + ((int) groups.size() - 1) * theme::metric::pad;
    setSize (width, headerHeight + rootRowHeight + waveHeight + theme::metric::gap + groupTitle + ParamControl::cellHeight + theme::metric::pad);

    app.getModel().addListener (this);
    refresh();
}

SamplerPanel::~SamplerPanel()
{
    app.getModel().removeListener (this);
}

juce::CallOutBox& SamplerPanel::show (AppController& app, const juce::String& channelId, juce::Rectangle<int> area,
                                      std::function<void (const juce::String&, bool)> status, juce::Component* parent)
{
    auto panel = std::make_unique<SamplerPanel> (app, channelId);
    panel->onStatus = std::move (status);
    return juce::CallOutBox::launchAsynchronously (std::move (panel), area, parent);
}

void SamplerPanel::projectChanged (const ProjectModel&)
{
    refresh();
}

void SamplerPanel::refresh()
{
    const auto* c = app.getModel().get().findChannel (channelId);
    exists = c != nullptr && c->kind == "sampler";

    if (! exists)
    {
        // canal apagado (ou desfeito) com o painel aberto: fecha
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();

        return;
    }

    channelName = c->name;
    // Só o nome do arquivo: o caminho no modelo usa "/", que o juce::File do Windows não separa.
    sampleName = c->samplePath.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
    rootText = noteName (c->rootKey) + centsText (c->rootCents);
    rootBox.setSelectedId (c->rootKey + 1, juce::dontSendNotification);

    for (int i = 0; i < numParams; ++i)
        controls[i]->setValue (modelValue (*c, i));

    // A onda vem do mesmo cache que o som: o arquivo é decodificado uma vez só.
    if (c->samplePath != samplePath)
    {
        samplePath = c->samplePath;
        juce::String error;
        wave.setSample (samplePath.isEmpty() ? nullptr : app.getSampleCache().get (app.getModel().resolveSample (samplePath), error));
    }

    wave.setMarks (c->start, c->loop, c->loopStart, c->loopEnd);
    wave.setSlices (c->slices);

    if (! c->slices.empty())
        sampleName << "  " << juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) << "  " << juce::String ((int) c->slices.size())
                   << " fatias de " << noteName (c->sliceKey) << " a " << noteName (c->sliceKey + (int) c->slices.size() - 1);

    repaint();
}

void SamplerPanel::send (const char* key, float value)
{
    auto a = args ({ { "channel", channelId } });
    auto* o = a.getDynamicObject();

    if (juce::String (key) == "loop")
    {
        const bool on = value >= 0.5f;
        o->setProperty ("loop", on);

        // Ligar com as pontas de fábrica (o sample inteiro, ataque junto): acha um laço no corpo do som.
        if (const auto* c = app.getModel().get().findChannel (channelId); on && c != nullptr && c->loopStart <= 0.0f && c->loopEnd >= 1.0f)
        {
            o->setProperty ("loopAuto", true);

            if (const auto found = app.run ("channel.setSampler", a); found.ok)
                return;

            o->removeProperty ("loopAuto");   // sem corpo para laçar (curto demais): liga com o sample inteiro
        }
    }
    else
    {
        o->setProperty (key, value);
    }

    const auto result = app.run ("channel.setSampler", a);

    if (! result.ok && onStatus)
        onStatus (result.error, true);
}

void SamplerPanel::sendLoop (float loopStart, float loopEnd)
{
    const auto result = app.run ("channel.setSampler", args ({ { "channel", channelId }, { "loopStart", loopStart }, { "loopEnd", loopEnd } }));

    if (! result.ok && onStatus)
        onStatus (result.error, true);
}

void SamplerPanel::detect()
{
    const auto result = app.run ("channel.detectRoot", args ({ { "channel", channelId } }));
    juce::String message;

    if (! result.ok)
        message = result.error;
    else if (! (bool) result.value["found"])
        message = "Sem nota definida neste sample (bateria ou ruído): a raiz ficou como estava."_t;
    else
        message = "Nota achada: "_t + noteName ((int) result.value["rootKey"]) + centsText ((float) (double) result.value["cents"]);

    detectButton.setTooltip (message);
    repaint();

    if (onStatus)
        onStatus (message, ! result.ok);
}

void SamplerPanel::findLoop()
{
    const auto result = app.run ("channel.setSampler", args ({ { "channel", channelId }, { "loopAuto", true } }));

    if (onStatus)
        onStatus (result.ok ? "Laço achado: de "_t + juce::String ((double) result.value["loopStart"] * 100.0, 1) + "% a "
                                  + juce::String ((double) result.value["loopEnd"] * 100.0, 1) + "% do sample, com "
                                  + juce::String ((double) result.value["loopXfadeMs"], 0) + " ms de crossfade"
                            : result.error,
                  ! result.ok);
}

//==============================================================================
SamplerPanel::Wave::Wave (SamplerPanel& p) : panel (p)
{
    setRepaintsOnMouseActivity (false);
}

void SamplerPanel::Wave::setSample (std::shared_ptr<const audio::SampleData> s)
{
    sample = std::move (s);
    buildPeaks();
    repaint();
}

void SamplerPanel::Wave::setMarks (float newStart, bool newLoop, float newLoopStart, float newLoopEnd)
{
    if (newStart == start && newLoop == loop && newLoopStart == loopStart && newLoopEnd == loopEnd)
        return;

    start = newStart;
    loop = newLoop;
    loopStart = newLoopStart;
    loopEnd = newLoopEnd;
    repaint();
}

void SamplerPanel::Wave::setSlices (const std::vector<float>& newSlices)
{
    if (newSlices != slices)
    {
        slices = newSlices;
        repaint();
    }
}

void SamplerPanel::Wave::resized()
{
    buildPeaks();
}

void SamplerPanel::Wave::buildPeaks()
{
    peaks.clear();
    const int columns = getWidth();

    if (sample == nullptr || columns <= 0 || sample->numFrames() < 2)
        return;

    const auto frames = sample->numFrames();
    peaks.resize ((size_t) columns);

    for (int x = 0; x < columns; ++x)
    {
        const auto from = frames * x / columns;
        const auto to = std::max (from + 1, frames * (x + 1) / columns);
        float lo = 0.0f, hi = 0.0f;

        for (auto& ch : sample->channels)
        {
            for (auto i = from; i < to; ++i)
            {
                lo = std::min (lo, ch[(size_t) i]);
                hi = std::max (hi, ch[(size_t) i]);
            }
        }

        peaks[(size_t) x] = { lo, hi };
    }
}

float SamplerPanel::Wave::xOf (float fraction) const        { return fraction * (float) getWidth(); }
float SamplerPanel::Wave::fractionAt (float x) const        { return juce::jlimit (0.0f, 1.0f, x / (float) juce::jmax (1, getWidth())); }

SamplerPanel::Wave::Grab SamplerPanel::Wave::grabAt (float x) const
{
    constexpr float reach = 6.0f;
    const float a = xOf (loopStart), b = xOf (loopEnd);

    if (std::abs (x - a) <= reach && std::abs (x - a) <= std::abs (x - b))   return Grab::start;
    if (std::abs (x - b) <= reach)                                           return Grab::end;
    if (x > a && x < b)                                                      return Grab::both;
    return Grab::none;
}

void SamplerPanel::Wave::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto r = getLocalBounds().toFloat();
    g.setColour (colour::background);
    g.fillRoundedRectangle (r, 6.0f);

    // o laço: laranja quando ligado, cinza quando só marcado
    const auto region = juce::Rectangle<float>::leftTopRightBottom (xOf (loopStart), r.getY(), xOf (loopEnd), r.getBottom());
    g.setColour (loop ? colour::signalTint : colour::raised);
    g.fillRect (region);

    // a onda, de cima a baixo do trecho
    const float mid = r.getCentreY(), half = r.getHeight() * 0.5f - 4.0f;
    g.setColour (colour::textDim);

    for (size_t x = 0; x < peaks.size(); ++x)
    {
        const auto [lo, hi] = peaks[x];
        const float top = mid - juce::jlimit (-1.0f, 1.0f, hi) * half, bottom = mid - juce::jlimit (-1.0f, 1.0f, lo) * half;
        g.fillRect ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top));
    }

    if (peaks.empty())
    {
        g.setColour (colour::textFaint);
        g.setFont (font (type::small));
        g.drawText ("Sem sample"_t, getLocalBounds(), juce::Justification::centred);
    }

    // fatias: um fio em cada começo (a primeira, no zero, não precisa)
    g.setColour (colour::text.withAlpha (0.55f));

    for (size_t i = 1; i < slices.size(); ++i)
        g.fillRect (xOf (slices[i]) - 0.5f, r.getY(), 1.0f, r.getHeight());

    if (! slices.empty())   // fatiado: o laço não vale, só as fatias
    {
        g.setColour (colour::text);
        g.fillRect (r.getX(), r.getY(), 1.0f, r.getHeight());
        return;
    }

    // pontas do laço, com uma alça no alto
    for (auto [fraction, which] : { std::pair { loopStart, Grab::start }, std::pair { loopEnd, Grab::end } })
    {
        const float x = xOf (fraction);
        const bool active = hover == which || grab == which;
        g.setColour (loop ? colour::signal : (active ? colour::textDim : colour::textFaint));
        g.fillRect (x - (active ? 1.0f : 0.5f), r.getY(), active ? 2.0f : 1.0f, r.getHeight());
        g.fillRect (which == Grab::start ? x : x - 6.0f, r.getY(), 6.0f, 6.0f);
    }

    // início da leitura
    g.setColour (colour::text);
    g.fillRect (xOf (start) - 0.5f, r.getY(), 1.0f, r.getHeight());
}

void SamplerPanel::Wave::mouseMove (const juce::MouseEvent& e)
{
    const auto now = grabAt (e.position.x);

    if (now != hover)
    {
        hover = now;
        setMouseCursor (now == Grab::start || now == Grab::end ? juce::MouseCursor::LeftRightResizeCursor
                        : now == Grab::both                     ? juce::MouseCursor::DraggingHandCursor
                                                                : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SamplerPanel::Wave::mouseDown (const juce::MouseEvent& e)
{
    grab = grabAt (e.position.x);

    if (grab == Grab::none)
        return;

    grabOffset = fractionAt (e.position.x) - loopStart;
    grabLength = loopEnd - loopStart;
    panel.app.getCommands().beginGesture ("Laço"_t);   // o arrasto inteiro é um passo de desfazer
}

void SamplerPanel::Wave::mouseDrag (const juce::MouseEvent& e)
{
    if (grab == Grab::none)
        return;

    // o laço nunca fica com menos de 0,1% do sample (e a voz pede pelo menos 16 amostras)
    constexpr float minimum = 0.001f;
    const float at = fractionAt (e.position.x);
    float a = loopStart, b = loopEnd;

    if (grab == Grab::start)      a = juce::jlimit (0.0f, b - minimum, at);
    else if (grab == Grab::end)   b = juce::jlimit (a + minimum, 1.0f, at);
    else
    {
        a = juce::jlimit (0.0f, 1.0f - grabLength, at - grabOffset);
        b = a + grabLength;
    }

    panel.sendLoop (a, b);
}

void SamplerPanel::Wave::mouseUp (const juce::MouseEvent&)
{
    if (grab != Grab::none)
        panel.app.getCommands().endGesture();

    grab = Grab::none;
    repaint();
}

//==============================================================================
void SamplerPanel::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    auto r = getLocalBounds().reduced (metric::pad, 0);
    auto header = r.removeFromTop (headerHeight);

    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText (channelName, header.removeFromTop (38).withTrimmedTop (12), juce::Justification::bottomLeft, true);

    g.setColour (colour::textDim);
    g.setFont (font (type::small));
    g.drawText ("Sampler  "_t + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + sampleName,
                header.removeFromTop (18), juce::Justification::centredLeft, true);

    // linha da raiz: legenda, lista, botão e a leitura com os cents
    auto rootRow = r.removeFromTop (rootRowHeight);
    g.setColour (colour::textFaint);
    g.setFont (captionFont());
    g.drawText ("RAIZ", rootRow.removeFromLeft (48), juce::Justification::centredLeft);

    g.setColour (colour::textDim);
    g.setFont (font (type::body));
    g.drawText (rootText, rootRow.withLeft (detectButton.getRight() + metric::pad).withRight (findLoopButton.getX() - metric::gap),
                juce::Justification::centredLeft, true);

    g.setColour (colour::line);
    g.fillRect (getLocalBounds().reduced (metric::pad, 0).withY (headerHeight + rootRowHeight - 1).withHeight (1));

    // títulos dos grupos
    g.setColour (colour::textFaint);
    g.setFont (captionFont());

    for (auto& grp : groups)
    {
        if (grp.controls.empty())
            continue;

        auto first = grp.controls.front()->getBounds();
        auto last = grp.controls.back()->getBounds();
        g.drawText (grp.title, juce::Rectangle<int> (first.getX(), first.getY() - groupTitle, last.getRight() - first.getX(), groupTitle),
                    juce::Justification::centred);
    }
}

void SamplerPanel::resized()
{
    using namespace theme;
    auto r = getLocalBounds().reduced (metric::pad, 0);
    r.removeFromTop (headerHeight);

    auto rootRow = r.removeFromTop (rootRowHeight).withSizeKeepingCentre (r.getWidth(), metric::control);
    findLoopButton.setBounds (rootRow.removeFromRight (findLoopButton.getIdealWidth (metric::control)));
    rootRow.removeFromLeft (48);
    rootBox.setBounds (rootRow.removeFromLeft (150));
    rootRow.removeFromLeft (metric::gap);
    detectButton.setBounds (rootRow.removeFromLeft (detectButton.getIdealWidth (metric::control)));

    wave.setBounds (r.removeFromTop (waveHeight).withTrimmedTop (metric::gap));
    r.removeFromTop (metric::gap);

    r.removeFromTop (groupTitle + 4);
    int x = r.getX();

    for (auto& grp : groups)
    {
        for (auto* c : grp.controls)
        {
            c->setBounds (x, r.getY(), ParamControl::cellWidth, ParamControl::cellHeight);
            x += ParamControl::cellWidth;
        }

        x += metric::pad;
    }
}

} // namespace whynot
