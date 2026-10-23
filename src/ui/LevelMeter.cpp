#include "LevelMeter.h"
#include "Theme.h"

namespace whynot
{

namespace
{
    // as mesmas velocidades de antes (a 30 Hz: 1,6 dB e 1 dB por quadro, pico parado por 45 quadros),
    // agora por segundo: o medidor anda igual com a tela a 60 ou a 144 Hz
    constexpr double fallDbPerSecond = 48.0;
    constexpr double peakFallDbPerSecond = 30.0;
    constexpr double peakHoldMs = 1500.0;
}

LevelMeter::LevelMeter (bool isVertical) : vertical (isVertical)
{
    setInterceptsMouseClicks (true, false);
    setTooltip ("Clique para limpar o aviso de clipe"_t);
}

LevelMeter::~LevelMeter()
{
    setSource (nullptr);
}

void LevelMeter::setSource (te::LevelMeterPlugin* plugin)
{
    if (source.get() == plugin)
        return;

    if (auto* old = dynamic_cast<te::LevelMeterPlugin*> (source.get()))
        old->measurer.removeClient (client);

    source = plugin;
    client.reset();

    if (plugin != nullptr)
        plugin->measurer.addClient (client);
}

float LevelMeter::toProportion (float db) noexcept
{
    // -60 dB embaixo, +6 dB em cima; o trecho útil (-24..0) ocupa a maior parte.
    const float clamped = juce::jlimit (-60.0f, 6.0f, db);
    return std::pow ((clamped + 60.0f) / 66.0f, 1.6f);
}

void LevelMeter::tick()
{
    if (source == nullptr)
        return;

    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastTickMs < 0.0 ? 1.0 / 30.0 : juce::jlimit (0.0, 0.1, (now - lastTickMs) * 0.001);
    lastTickMs = now;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = client.getAndClearAudioLevel (ch).dB;
        const float decayed = level[ch] - (float) (fallDbPerSecond * dt);
        level[ch] = juce::jmax (db, decayed, -100.0f);

        if (db >= peak[ch])
        {
            peak[ch] = db;
            peakHoldUntil[ch] = now + peakHoldMs;
        }
        else if (now >= peakHoldUntil[ch])
        {
            peak[ch] = juce::jmax (-100.0f, peak[ch] - (float) (peakFallDbPerSecond * dt));
        }

        if (db > 0.0f)
            clipped = true;
    }

    // repinta só quando algum pixel muda: parado no silêncio, ou num nível estável, não custa nada
    if (const auto px = pixelsNow(); ! (px == shown))
    {
        shown = px;
        repaint();
    }
}

LevelMeter::Pixels LevelMeter::pixelsNow() const
{
    Pixels px;
    const float length = (float) (vertical ? getHeight() : getWidth());

    for (int ch = 0; ch < 2; ++ch)
    {
        px.level[ch] = juce::roundToInt (length * toProportion (level[ch]) * 2.0f);   // meio pixel: a borda suave conta
        px.peak[ch] = peak[ch] > -60.0f ? juce::roundToInt (length * toProportion (peak[ch]) * 2.0f) : -1;
    }

    px.clipped = clipped;
    return px;
}

const juce::ColourGradient& LevelMeter::gradientFor (juce::Rectangle<float> bar, int channel)
{
    using namespace theme;

    if (gradientSize != getLocalBounds())
    {
        gradientSize = getLocalBounds();

        for (auto& grad : gradients)
            grad = {};
    }

    auto& grad = gradients[channel];

    if (grad.getNumColours() == 0)
    {
        // gradiente fixo no comprimento da barra: verde até -18, âmbar até -6, vermelho perto de 0
        grad = vertical
            ? juce::ColourGradient (colour::meterLow, bar.getX(), bar.getBottom(), colour::meterHigh, bar.getX(), bar.getY(), false)
            : juce::ColourGradient (colour::meterLow, bar.getX(), bar.getY(), colour::meterHigh, bar.getRight(), bar.getY(), false);
        grad.addColour (toProportion (-18.0f), colour::meterLow);
        grad.addColour (toProportion (-6.0f), colour::meterMid);
        grad.addColour (toProportion (-1.0f), colour::meterHigh);
    }

    return grad;
}

void LevelMeter::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat();

    // no mixer o medidor é um poço preto; na barra de cima (fundo preto) ele precisa de um trilho visível
    g.setColour (vertical ? colour::background : colour::raised);
    g.fillRoundedRectangle (r, 2.0f);

    const float gap = 2.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = vertical ? juce::Rectangle<float> (r.getX() + ch * (r.getWidth() + gap) / 2.0f, r.getY(),
                                                      (r.getWidth() - gap) / 2.0f, r.getHeight())
                            : juce::Rectangle<float> (r.getX(), r.getY() + ch * (r.getHeight() + gap) / 2.0f,
                                                      r.getWidth(), (r.getHeight() - gap) / 2.0f);

        const float p = toProportion (level[ch]);

        g.setGradientFill (gradientFor (bar, ch));
        g.fillRect (vertical ? bar.withTop (bar.getBottom() - bar.getHeight() * p) : bar.withWidth (bar.getWidth() * p));

        if (peak[ch] > -60.0f)
        {
            const float pp = toProportion (peak[ch]);
            g.setColour (colour::text.withAlpha (0.8f));

            if (vertical)
                g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * pp - 1.0f, bar.getWidth(), 2.0f);
            else
                g.fillRect (bar.getX() + bar.getWidth() * pp - 1.0f, bar.getY(), 2.0f, bar.getHeight());
        }
    }

    // marca de 0 dB
    const float zero = toProportion (0.0f);
    g.setColour (colour::textDim.withAlpha (0.5f));

    if (vertical)
        g.fillRect (r.getX(), r.getBottom() - r.getHeight() * zero, r.getWidth(), 1.0f);
    else
        g.fillRect (r.getX() + r.getWidth() * zero, r.getY(), 1.0f, r.getHeight());

    if (clipped)
    {
        g.setColour (colour::meterHigh);
        g.fillRect (vertical ? r.withHeight (3.0f) : r.withLeft (r.getRight() - 3.0f));
    }
}

} // namespace whynot
