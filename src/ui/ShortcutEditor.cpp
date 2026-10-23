#include "ShortcutEditor.h"
#include "Theme.h"
#include "../app/AppController.h"
#include "../app/KeyMap.h"

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

    // O toUpperCase do JUCE não sobe os acentos no Windows ("EDIçãO"): as legendas em maiúsculas passam por aqui.
    juce::String caps (const juce::String& text)
    {
        auto upper = text.toUpperCase();
        const juce::String lower = juce::String::fromUTF8 ("áàâãéêíóôõúüç"), high = juce::String::fromUTF8 ("ÁÀÂÃÉÊÍÓÔÕÚÜÇ");

        for (int i = 0; i < lower.length(); ++i)
            upper = upper.replaceCharacter (lower[i], high[i]);

        return upper;
    }

    bool isOnlyModifier (const juce::KeyPress& k)
    {
        // Ctrl, Shift e Alt sozinhos chegam como teclas sem código útil
        return k.getKeyCode() == 0 || k.getKeyCode() == 16 || k.getKeyCode() == 17 || k.getKeyCode() == 18;
    }
}

ShortcutEditor::ShortcutEditor (AppController& a) : app (a)
{
    std::vector<SegmentedControl::Item> items;

    for (auto& name : KeyMap::presetNames())
        items.push_back ({ name == "fl" ? juce::String ("Estilo FL Studio") : juce::String ("WHYNOT"), Icon::none, KeyMap::presetTitle (name) });

    preset.setItems (items);
    preset.setSelected (juce::jmax (0, KeyMap::presetNames().indexOf (app.getKeyMap().getPreset())));
    preset.onChange = [this] (int index)
    {
        app.run ("shortcuts.setPreset", args ({ { "preset", KeyMap::presetNames()[index] } }));
        message = "Preset: "_t + KeyMap::presetTitle (KeyMap::presetNames()[index]) + ". As trocas feitas à mão foram apagadas."_t;
        rebuild();
    };
    addAndMakeVisible (preset);

    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setWantsKeyboardFocus (false);   // as teclas vêm para o editor (a captura)
    addAndMakeVisible (list);

    setWantsKeyboardFocus (true);
    rebuild();
    setSize (560, 640);
}

ShortcutEditor::~ShortcutEditor()
{
    list.setModel (nullptr);
}

void ShortcutEditor::show (AppController& app, juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ShortcutEditor (app));
    o.dialogTitle = "Atalhos de teclado";
    o.dialogBackgroundColour = theme::colour::panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.escapeKeyTriggersCloseButton = true;    // na captura o editor fica com o Esc antes (só cancela a captura)
    o.componentToCentreAround = parent;
    o.launchAsync();
}

void ShortcutEditor::rebuild()
{
    rows.clear();
    juce::String group;

    for (auto& a : KeyMap::actions())
    {
        if (a.group != group)
        {
            group = a.group;
            rows.push_back ({ true, {}, group });
        }

        rows.push_back ({ false, a.id, a.title });
    }

    list.updateContent();
    list.repaint();
    repaint();
}

int ShortcutEditor::getNumRows()
{
    return (int) rows.size();
}

void ShortcutEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    using namespace theme;

    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    const auto& r = rows[(size_t) row];
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);

    if (r.header)
    {
        g.setColour (colour::textFaint);
        g.setFont (captionFont());
        g.drawText (caps (r.text), area.withTrimmedTop (8), juce::Justification::centredLeft);
        return;
    }

    const bool isCapturing = row == capturing;

    if (isCapturing)
    {
        g.setColour (colour::signalTint);
        g.fillRoundedRectangle (area.toFloat().reduced (0.0f, 2.0f), metric::radius);
    }

    area.reduce (8, 0);
    auto& keys = app.getKeyMap();
    const auto keyText = isCapturing ? juce::String (captureAdds ? "Aperte a tecla a mais..." : "Aperte a tecla nova...")
                                     : keys.describeKeys (r.id);
    const bool none = keyText.isEmpty();
    const auto shown = none ? juce::String ("sem atalho") : keyText;

    g.setFont (font (type::body, Weight::medium));
    const int keyWidth = juce::jmin (area.getWidth() / 2, juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), shown)) + 20);
    auto chip = area.removeFromRight (keyWidth).toFloat().reduced (0.0f, 5.0f);

    if (! none && ! isCapturing)
    {
        g.setColour (colour::raised);
        g.fillRoundedRectangle (chip, metric::radiusSmall);
    }

    g.setColour (isCapturing ? colour::signal : (none ? colour::textFaint : colour::text));
    g.drawText (shown, chip.toNearestInt(), juce::Justification::centred, true);

    g.setColour (colour::text);
    g.setFont (font (type::body));
    g.drawText (r.text + (keys.isCustomised (r.id) ? "  *" : ""), area.withTrimmedRight (8), juce::Justification::centredLeft, true);
}

void ShortcutEditor::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
        return;

    if (e.mods.isPopupMenu())
        showRowMenu (row);
    else
        startCapture (row, false);
}

void ShortcutEditor::startCapture (int row, bool adding)
{
    capturing = row;
    captureAdds = adding;
    message = "Aperte a combinação de teclas. Esc cancela."_t;
    grabKeyboardFocus();
    list.repaint();
    repaint();
}

void ShortcutEditor::setKeys (const juce::String& action, const juce::Array<juce::KeyPress>& keys)
{
    juce::Array<juce::var> names;
    for (auto& k : keys) names.add (KeyMap::describe (k));

    const auto result = app.run ("shortcuts.set", args ({ { "action", action }, { "keys", names } }));

    if (! result.ok)
        message = result.error;
}

bool ShortcutEditor::keyPressed (const juce::KeyPress& key)
{
    if (capturing < 0)
    {
        // sem captura, Esc fecha a janela (com captura, Esc só cancela a captura)
        if (key == juce::KeyPress::escapeKey)
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState (0);

        return key == juce::KeyPress::escapeKey;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        capturing = -1;
        message.clear();
        list.repaint();
        repaint();
        return true;
    }

    if (isOnlyModifier (key))
        return true;

    const auto action = rows[(size_t) capturing].id;
    auto& keys = app.getKeyMap();
    const auto previous = keys.actionFor (key);
    auto next = captureAdds ? keys.keysFor (action) : juce::Array<juce::KeyPress>();
    next.addIfNotAlreadyThere (key);
    setKeys (action, next);

    message = KeyMap::describe (key) + " agora: " + KeyMap::findAction (action)->title;

    if (previous.isNotEmpty() && previous != action)
        message << " (saiu de " << KeyMap::findAction (previous)->title << ")";

    capturing = -1;
    list.repaint();
    repaint();
    return true;
}

void ShortcutEditor::showRowMenu (int row)
{
    const auto action = rows[(size_t) row].id;
    juce::PopupMenu menu;
    menu.addItem (1, "Trocar tecla");
    menu.addItem (2, "Adicionar outra tecla");
    menu.addItem (3, "Sem atalho");
    menu.addItem (4, "Voltar ao do preset", app.getKeyMap().isCustomised (action));

    juce::Component::SafePointer<ShortcutEditor> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options(), [safe, row, action] (int choice)
    {
        if (safe == nullptr)
            return;

        switch (choice)
        {
            case 1: safe->startCapture (row, false); return;
            case 2: safe->startCapture (row, true); return;
            case 3:
                safe->setKeys (action, {});
                safe->message = KeyMap::findAction (action)->title + ": sem atalho";
                break;
            case 4:
                safe->app.run ("shortcuts.set", args ({ { "action", action } }));
                safe->message = KeyMap::findAction (action)->title + ": de volta ao preset ("_t
                                + (safe->app.getKeyMap().describeKeys (action).isNotEmpty() ? safe->app.getKeyMap().describeKeys (action)
                                                                                           : juce::String ("sem atalho")) + ")";
                break;
            default: return;
        }

        safe->list.repaint();
        safe->repaint();
    });
}

void ShortcutEditor::resized()
{
    using namespace theme;
    auto r = getLocalBounds().reduced (metric::pad + 4, 0);
    auto top = r.removeFromTop (56);
    preset.setBounds (top.withSizeKeepingCentre (top.getWidth(), metric::control).removeFromRight (preset.getIdealWidth()));
    r.removeFromBottom (44);
    list.setBounds (r.expanded (8, 0));
}

void ShortcutEditor::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    auto r = getLocalBounds().reduced (metric::pad + 4, 0);
    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Preset", r.removeFromTop (56), juce::Justification::centredLeft);

    auto footer = getLocalBounds().removeFromBottom (44).reduced (metric::pad + 4, 0);
    g.setColour (colour::line);
    g.fillRect (footer.removeFromTop (1));
    g.setColour (message.isNotEmpty() ? colour::textDim : colour::textFaint);
    g.setFont (font (type::small));
    g.drawFittedText (message.isNotEmpty() ? message : "Clique numa ação e aperte a tecla. Botão direito: mais opções. * = trocado à mão."_t,
                      footer, juce::Justification::centredLeft, 2);
}

} // namespace whynot
