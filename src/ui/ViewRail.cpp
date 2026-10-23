#include "ViewRail.h"
#include "Theme.h"

namespace whynot
{

namespace
{
    constexpr int itemHeight = 58;
    constexpr size_t numScreens = 4;   // playlist, rack, piano e projetos: as telas de cima
}

ViewRail::ViewRail()
{
    items = {
        { "playlist",    "Playlist", "Playlist: o arranjo da música (F5)"_t,                      Icon::playlist,    {} },
        { "channelRack", "Rack",     "Channel rack: os sons e os passos do padrão (F6)"_t,        Icon::channelRack, {} },
        { "pianoRoll",   "Piano",    "Piano roll: notas e melodias do canal escolhido (F7)",      Icon::pianoRoll,   {} },
        { "projects",    "Projetos", "Projetos: ouça a prévia dos seus projetos sem abrir e abra o que quiser (F11)"_t, Icon::projects, {} },
        { "mixer",       "Mixer",    "Mixer: volume, pan e para onde vai o som (F9)",             Icon::mixer,       {} },
        { "sounds",      "Sons",     "Sons: o kit, a sua biblioteca e as suas pastas. Clique ouve, arraste para o rack ou a playlist. (Alt+F8)", Icon::folder, {} },
        { "plugins",     "Plugins",  "Plugins: instrumentos e efeitos VST3. Arraste um instrumento para o channel rack. (F8)", Icon::plugin, {} },
        { "settings",    "Áudio"_t,  "Áudio e MIDI: placa de som, ASIO, controladores e teclado-piano (F10)"_t, Icon::settings, {} },
    };

    setRepaintsOnMouseActivity (false);
}

void ViewRail::setState (View current, bool upperVisible, bool mixerVisible, bool browserVisible, bool soundsVisible)
{
    if (current != view || upperVisible != upper || mixerVisible != mixer || browserVisible != browser || soundsVisible != sounds)
    {
        view = current;
        upper = upperVisible;
        mixer = mixerVisible;
        browser = browserVisible;
        sounds = soundsVisible;
        repaint();
    }
}

bool ViewRail::isLit (const Item& item) const
{
    if (item.panel == "mixer")
        return mixer;

    if (item.panel == "plugins")
        return browser;

    if (item.panel == "sounds")
        return sounds;

    if (item.panel == "settings")
        return false;

    const auto active = view == View::playlist ? "playlist"
                      : view == View::pianoRoll ? "pianoRoll"
                      : view == View::projects ? "projects" : "channelRack";
    return upper && item.panel == active;
}

void ViewRail::resized()
{
    using namespace theme;
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (metric::topBar);   // a marca fica alinhada com a barra do transporte

    for (size_t i = 0; i < numScreens; ++i)
        items[i].bounds = r.removeFromTop (itemHeight).reduced (0, 2);

    r.removeFromTop (16);               // mixer, sons e plugins são camadas à parte: separados das telas de cima

    for (size_t i = numScreens; i < numScreens + 3; ++i)
        items[i].bounds = r.removeFromTop (itemHeight).reduced (0, 2);

    items.back().bounds = r.removeFromBottom (itemHeight).reduced (0, 2).translated (0, -metric::gap);
}

int ViewRail::itemAt (juce::Point<int> p) const
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].bounds.contains (p))
            return (int) i;

    return -1;
}

void ViewRail::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::background);

    drawLogo (g, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) metric::topBar).reduced (20.0f, 13.0f), colour::text);

    // fio entre as telas de cima e o mixer
    g.setColour (colour::line);
    g.fillRect (juce::Rectangle<int> (22, items[numScreens].bounds.getY() - 10, getWidth() - 44, 1));

    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        const bool lit = isLit (item);
        const auto r = item.bounds.toFloat();

        if (lit || (int) i == hover)
        {
            g.setColour (lit ? colour::raised : colour::raised.withAlpha (0.6f));
            g.fillRoundedRectangle (r, metric::radius + 2.0f);
        }

        const auto ink = lit ? colour::text : ((int) i == hover ? colour::text : colour::textDim);
        icons::draw (g, item.icon, juce::Rectangle<float> (r.getCentreX() - 11.0f, r.getY() + 9.0f, 22.0f, 22.0f), ink);

        g.setColour (lit ? colour::text : ((int) i == hover ? colour::textDim : colour::textFaint.brighter (0.25f)));
        g.setFont (font (11.0f, lit ? Weight::semibold : Weight::medium));
        g.drawText (item.label, r.withTop (r.getY() + 35.0f).withHeight (14.0f), juce::Justification::centred, false);
    }
}

void ViewRail::mouseMove (const juce::MouseEvent& e)
{
    const int h = itemAt (e.getPosition());

    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ViewRail::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void ViewRail::mouseDown (const juce::MouseEvent& e)
{
    const int index = itemAt (e.getPosition());

    if (index < 0)
        return;

    if (items[(size_t) index].panel == "settings")
    {
        if (onSettings) onSettings();
    }
    else if (onToggle)
    {
        onToggle (items[(size_t) index].panel);
    }
}

juce::String ViewRail::getTooltip()
{
    return juce::isPositiveAndBelow (hover, (int) items.size()) ? items[(size_t) hover].tooltip : juce::String();
}

} // namespace whynot
