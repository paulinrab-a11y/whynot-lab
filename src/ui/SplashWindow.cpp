#include "SplashWindow.h"
#include "Theme.h"

namespace whynot
{

namespace
{
    constexpr juce::uint32 minimumMs = 900;    // abaixo disso a logo só pisca
    constexpr float fadeStep = 1.0f / 15.0f;   // 15 quadros a 60 Hz: 0,25 s
}

SplashWindow::SplashWindow()
{
    setOpaque (true);
    setSize (width, height);
}

std::unique_ptr<SplashWindow> SplashWindow::showNow()
{
    std::unique_ptr<SplashWindow> s (new SplashWindow());
    s->centreWithSize (width, height);
    s->setAlwaysOnTop (true);   // a janela principal abre por baixo, e a logo sai por cima dela
    s->addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowHasDropShadow);
    s->setVisible (true);
    s->toFront (false);
    s->shownAt = juce::Time::getMillisecondCounter();

    if (auto* peer = s->getPeer())
        peer->performAnyPendingRepaintsNow();

    return s;
}

juce::Image SplashWindow::preview()
{
    SplashWindow s;
    return juce::SoftwareImageType().convert (s.createComponentSnapshot (s.getLocalBounds(), true, 1.0f));
}

void SplashWindow::fadeOut (std::function<void()> onGone)
{
    gone = std::move (onGone);
    const auto elapsed = juce::Time::getMillisecondCounter() - shownAt;
    const int wait = elapsed < minimumMs ? (int) (minimumMs - elapsed) : 0;
    juce::Component::SafePointer<SplashWindow> safe (this);

    juce::Timer::callAfterDelay (wait, [safe]
    {
        if (safe != nullptr)
            safe->startTimerHz (60);
    });
}

void SplashWindow::timerCallback()
{
    const float alpha = getAlpha() - fadeStep;

    if (alpha > 0.0f)
    {
        setAlpha (alpha);
        return;
    }

    stopTimer();
    setVisible (false);

    if (gone)
        juce::MessageManager::callAsync (std::move (gone));
}

void SplashWindow::paint (juce::Graphics& g)
{
    // sempre a marca: preto, a logo branca e o laranja, em qualquer tema
    const auto& brand = theme::palette (theme::ColourScheme::whynot);
    const auto w = (float) getWidth();

    g.fillAll (brand.background);
    g.setColour (brand.line);
    g.drawRect (getLocalBounds(), 1);

    theme::drawLogo (g, juce::Rectangle<float> (0.0f, 52.0f, w, 132.0f).withSizeKeepingCentre (110.0f, 132.0f), juce::Colours::white);

    g.setColour (brand.text);
    g.setFont (theme::font (22.0f, theme::Weight::semibold).withExtraKerningFactor (0.18f));
    g.drawText ("WHYNOT LAB", juce::Rectangle<float> (0.0f, 216.0f, w, 30.0f), juce::Justification::centred, false);

    g.setColour (brand.signal);
    g.fillRoundedRectangle (juce::Rectangle<float> (w * 0.5f - 16.0f, 256.0f, 32.0f, 3.0f), 1.5f);

    g.setColour (brand.textFaint);
    g.setFont (theme::font (theme::type::small));
    g.drawText ("Abrindo o estúdio..."_t, juce::Rectangle<float> (0.0f, 276.0f, w, 18.0f), juce::Justification::centred, false);

    if (auto* app = juce::JUCEApplication::getInstance())
        g.drawText ("v" + app->getApplicationVersion(), getLocalBounds().toFloat().reduced (14.0f, 10.0f),
                    juce::Justification::bottomRight, false);
}

} // namespace whynot
