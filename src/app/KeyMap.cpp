#include "KeyMap.h"
#include "../common/SafeJson.h"
#include "../common/Text.h"

namespace whynot
{

namespace
{
    using KP = juce::KeyPress;

    // As teclas que não são caractere, no código do JUCE e no da tabela (KeyMapCore.h).
    struct SpecialKey { int juceCode, coreCode; };

    const std::vector<SpecialKey>& specialKeys()
    {
        namespace c = keymap::code;
        static const std::vector<SpecialKey> list {
            { KP::spaceKey, c::space }, { KP::returnKey, c::enter }, { KP::escapeKey, c::escape }, { KP::tabKey, c::tab },
            { KP::deleteKey, c::del }, { KP::backspaceKey, c::backspace }, { KP::insertKey, c::insert },
            { KP::homeKey, c::home }, { KP::endKey, c::end }, { KP::pageUpKey, c::pageUp }, { KP::pageDownKey, c::pageDown },
            { KP::leftKey, c::left }, { KP::rightKey, c::right }, { KP::upKey, c::up }, { KP::downKey, c::down },
            { KP::numberPadAdd, c::numpadAdd }, { KP::numberPadSubtract, c::numpadSubtract },
            { KP::F1Key, c::f1 }, { KP::F2Key, c::f2 }, { KP::F3Key, c::f3 }, { KP::F4Key, c::f4 },
            { KP::F5Key, c::f5 }, { KP::F6Key, c::f6 }, { KP::F7Key, c::f7 }, { KP::F8Key, c::f8 },
            { KP::F9Key, c::f9 }, { KP::F10Key, c::f10 }, { KP::F11Key, c::f11 }, { KP::F12Key, c::f12 },
        };

        return list;
    }

    // juce::KeyPress para a tecla da tabela. A letra vale em maiúscula ou minúscula, como no == do KeyPress.
    keymap::Chord toChord (const KP& k)
    {
        const int code = k.getKeyCode();
        const auto m = k.getModifiers();
        const int mods = (m.isCommandDown() ? (int) keymap::ctrl : 0) | (m.isShiftDown() ? (int) keymap::shift : 0)
                       | (m.isAltDown() ? (int) keymap::alt : 0);

        for (auto& s : specialKeys())
            if (s.juceCode == code)
                return { s.coreCode, mods };

        if (code > 0 && code < 256)
            return { (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code), mods };

        return { code, mods };   // tecla rara (F13, teclado numérico): o código do JUCE mesmo
    }

    KP toKeyPress (keymap::Chord c)
    {
        int code = c.code;

        for (auto& s : specialKeys())
            if (s.coreCode == c.code)
                code = s.juceCode;

        const int mods = ((c.mods & keymap::ctrl) != 0 ? (int) juce::ModifierKeys::commandModifier : 0)
                       | ((c.mods & keymap::shift) != 0 ? (int) juce::ModifierKeys::shiftModifier : 0)
                       | ((c.mods & keymap::alt) != 0 ? (int) juce::ModifierKeys::altModifier : 0);

        return KP (code, juce::ModifierKeys (mods), 0);
    }

    juce::Array<KP> toKeyPresses (const keymap::Keys& keys)
    {
        juce::Array<KP> list;
        for (auto& c : keys) list.add (toKeyPress (c));
        return list;
    }

    keymap::Keys toChords (const juce::Array<KP>& keys)
    {
        keymap::Keys list;
        for (auto& k : keys) list.push_back (toChord (k));
        return list;
    }

    juce::String toText (std::string_view s)   { return juce::String::fromUTF8 (s.data(), (int) s.size()); }

    juce::String modifierText (const juce::ModifierKeys& m)
    {
        juce::String s;
        if (m.isCommandDown()) s << "Ctrl+";
        if (m.isAltDown())     s << "Alt+";
        if (m.isShiftDown())   s << "Shift+";
        return s;
    }
}

const std::vector<ShortcutAction>& KeyMap::actions()
{
    // a tabela (ids, títulos, grupos, contextos e as teclas dos presets) é a do KeyMapCore.h
    static const std::vector<ShortcutAction> list = []
    {
        std::vector<ShortcutAction> v;

        for (auto& a : keymap::actions())
            v.push_back ({ toText (a.id), toText (a.title), toText (a.group), toText (a.context) });

        return v;
    }();

    return list;
}

const ShortcutAction* KeyMap::findAction (const juce::String& id)
{
    for (auto& a : actions())
        if (a.id == id)
            return &a;

    return nullptr;
}

juce::String KeyMap::presetTitle (const juce::String& name)
{
    return name == "whynot" ? "WHYNOT (Ctrl+N novo projeto)" : "Estilo FL Studio";
}

KeyMap::Bindings KeyMap::preset (const juce::String& name)
{
    Bindings b;
    const auto presetName = name.toStdString();

    for (auto& a : keymap::actions())
        if (const auto& keys = keymap::presetKeys (a, presetName); ! keys.empty())
            b[toText (a.id)] = toKeyPresses (keys);

    return b;
}

juce::String KeyMap::describe (const juce::KeyPress& k)
{
    if (! k.isValid())
        return {};

    const int code = k.getKeyCode();
    juce::String name;

    if (code == KP::spaceKey)                       name = "Espaço"_t;
    else if (code == KP::returnKey)                 name = "Enter";
    else if (code == KP::escapeKey)                 name = "Esc";
    else if (code == KP::tabKey)                    name = "Tab";
    else if (code == KP::deleteKey)                 name = "Delete";
    else if (code == KP::backspaceKey)              name = "Backspace";
    else if (code == KP::insertKey)                 name = "Insert";
    else if (code == KP::homeKey)                   name = "Home";
    else if (code == KP::endKey)                    name = "End";
    else if (code == KP::pageUpKey)                 name = "Page Up";
    else if (code == KP::pageDownKey)               name = "Page Down";
    else if (code == KP::leftKey)                   name = "Esquerda";
    else if (code == KP::rightKey)                  name = "Direita";
    else if (code == KP::upKey)                     name = "Cima";
    else if (code == KP::downKey)                   name = "Baixo";
    else if (code >= KP::F1Key && code <= KP::F12Key)   name = "F" + juce::String (code - KP::F1Key + 1);
    else if (code > 32 && code < 127)               name = juce::String::charToString ((juce::juce_wchar) code).toUpperCase();
    else                                            name = k.getTextDescription();

    return modifierText (k.getModifiers()) + name;
}

juce::KeyPress KeyMap::parse (const juce::String& text)
{
    // Aceita o formato gravado (o do JUCE: "ctrl + shift + R") e o mostrado na tela ("Ctrl+Shift+R", "Espaço").
    auto t = text.trim();

    const std::pair<juce::String, const char*> names[] { { "Espaço"_t, "spacebar" }, { "Esquerda", "cursor left" },
                                                        { "Direita", "cursor right" }, { "Cima", "cursor up" },
                                                        { "Baixo", "cursor down" }, { "Enter", "return" } };

    for (auto& [pt, juceName] : names)
        if (t.endsWithIgnoreCase (pt))
            t = t.dropLastCharacters (pt.length()) + juceName;

    return KP::createFromDescription (t);
}

//==============================================================================
KeyMap::KeyMap (const juce::File& f) : file (f) {}

void KeyMap::load()
{
    core.setPreset ("fl");   // também apaga as trocas

    if (! file.existsAsFile())
        return;

    // Lido a cada abertura do app: arquivo corrompido cai no preset "fl" em vez de derrubar o app.
    juce::var json;

    if (parseJsonSafely (file.loadFileAsString(), json).failed())
        return;
    const auto preset = json["preset"].toString();

    core.setPresetKeepingOverrides (preset.toStdString());   // nome desconhecido: fica o "fl"

    if (auto* obj = json["overrides"].getDynamicObject())
    {
        for (auto& p : obj->getProperties())
        {
            if (findAction (p.name.toString()) == nullptr)
                continue;   // ação de uma versão mais nova ou que saiu

            juce::Array<juce::KeyPress> keys;

            if (auto* list = p.value.getArray())
                for (auto& k : *list)
                    if (const auto kp = parse (k.toString()); kp.isValid())
                        keys.add (kp);

            core.setRaw (p.name.toString().toStdString(), toChords (keys));   // como está no arquivo
        }
    }
}

bool KeyMap::save() const
{
    if (file == juce::File())
        return false;

    auto root = new juce::DynamicObject();
    root->setProperty ("preset", getPreset());
    auto obj = new juce::DynamicObject();

    // só as trocas do usuário vão para o arquivo
    for (auto& [id, keys] : core.getOverrides())
    {
        juce::Array<juce::var> list;
        for (auto& k : keys) list.add (toKeyPress (k).getTextDescription());
        obj->setProperty (juce::Identifier (juce::String (id)), list);
    }

    root->setProperty ("overrides", juce::var (obj));
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root)));
}

void KeyMap::setPreset (const juce::String& name)
{
    core.setPreset (name.toStdString());
}

juce::Array<juce::KeyPress> KeyMap::keysFor (const juce::String& action) const
{
    return toKeyPresses (core.keysFor (action.toStdString()));
}

juce::String KeyMap::describeKeys (const juce::String& action) const
{
    juce::StringArray parts;

    for (auto& k : keysFor (action))
        parts.add (describe (k));

    return parts.joinIntoString (", ");
}

void KeyMap::setKeys (const juce::String& action, const juce::Array<juce::KeyPress>& keys)
{
    core.setKeys (action.toStdString(), toChords (keys));
}

void KeyMap::resetAction (const juce::String& action)
{
    core.reset (action.toStdString());
}

juce::String KeyMap::actionFor (const juce::KeyPress& k) const
{
    return toText (core.actionFor (toChord (k), keymap::global));
}

juce::String KeyMap::actionFor (const juce::KeyPress& k, const char* context) const
{
    return toText (core.actionFor (toChord (k), context));
}

juce::String KeyMap::clashFor (const juce::String& action, const juce::KeyPress& k) const
{
    return toText (core.clashFor (action.toStdString(), toChord (k)));
}

juce::var KeyMap::toVar() const
{
    auto root = new juce::DynamicObject();
    root->setProperty ("preset", getPreset());
    juce::Array<juce::var> list;

    for (auto& a : actions())
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("id", a.id);
        o->setProperty ("title", a.title);
        o->setProperty ("group", a.group);
        o->setProperty ("context", a.context);
        juce::Array<juce::var> keys;
        for (auto& k : keysFor (a.id)) keys.add (describe (k));
        o->setProperty ("keys", keys);
        o->setProperty ("custom", isCustomised (a.id));
        list.add (juce::var (o));
    }

    root->setProperty ("actions", list);
    return juce::var (root);
}

} // namespace whynot
