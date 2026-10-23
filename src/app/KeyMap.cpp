#include "KeyMap.h"
#include "../common/SafeJson.h"
#include "../common/Text.h"

namespace whynot
{

namespace
{
    using KP = juce::KeyPress;
    constexpr int ctrl = juce::ModifierKeys::commandModifier;
    constexpr int shift = juce::ModifierKeys::shiftModifier;
    constexpr int alt = juce::ModifierKeys::altModifier;

    KP key (int code, int mods = 0)   { return KP (code, juce::ModifierKeys (mods), 0); }

    // As teclas iguais nos dois presets.
    KeyMap::Bindings common()
    {
        return {
            { "transport.togglePlay",  { key (KP::spaceKey) } },
            { "transport.togglePause", { key (KP::spaceKey, ctrl) } },
            { "transport.songMode",    { key ('L') } },
            { "transport.metronome",   { key ('M', ctrl) } },
            { "transport.record",      { key ('R') } },
            { "transport.countIn",     { key ('P', ctrl) } },
            { "file.open",             { key ('O', ctrl) } },
            { "file.save",             { key ('S', ctrl) } },
            { "file.saveAs",           { key ('S', ctrl | shift) } },
            { "export.wav",            { key ('R', ctrl) } },
            { "export.mp3",            { key ('R', ctrl | shift) } },
            { "export.midi",           { key ('M', ctrl | shift) } },
            { "pattern.new",           { key (KP::F4Key) } },
            { "pattern.clone",         { key ('C', ctrl | shift) } },
            { "pattern.next",          { key (KP::numberPadAdd) } },
            { "pattern.previous",      { key (KP::numberPadSubtract) } },
            { "view.playlist",         { key (KP::F5Key) } },
            { "view.channelRack",      { key (KP::F6Key) } },
            { "view.pianoRoll",        { key (KP::F7Key) } },
            { "view.files",            { key (KP::F8Key, alt) } },
            { "view.plugins",          { key (KP::F8Key) } },
            { "view.mixer",            { key (KP::F9Key) } },
            { "view.settings",         { key (KP::F10Key) } },
            { "view.projects",         { key (KP::F11Key) } },
            { "projects.switchDeck",   { key (KP::tabKey) } },
            { "keyboard.typing",       { key ('T', ctrl) } },
            { "playlist.addMarker",    { key ('T', alt) } },   // como o "Add marker" do FL (bloco 1, 22/10)
            { "ui.zoomIn",             { key ('=', ctrl) } },
            { "ui.zoomOut",            { key ('-', ctrl) } },
            { "ui.zoomReset",          { key ('0', ctrl) } },
        };
    }

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
    static const std::vector<ShortcutAction> list {
        { "transport.togglePlay",  "Tocar ou parar (volta à agulha)"_t,         "Transporte" },
        { "transport.togglePause", "Tocar ou pausar (continua de onde parou)"_t, "Transporte" },
        { "transport.songMode",    "Alternar PAT e SONG",                         "Transporte" },
        { "transport.metronome",   "Metrônomo"_t,                                 "Transporte" },
        { "transport.record",      "Gravar (liga e desliga o REC)",               "Transporte" },
        { "transport.countIn",     "Contagem antes de gravar (liga e desliga)",   "Transporte" },
        { "record.cancel",         "Cancelar o take (apaga o arquivo)",           "Transporte" },
        { "transport.tapTempo",    "Tap tempo: bata no ritmo para achar o BPM",   "Transporte" },

        { "file.new",              "Novo projeto",                                "Arquivo" },
        { "file.open",             "Abrir projeto",                               "Arquivo" },
        { "file.save",             "Salvar",                                      "Arquivo" },
        { "file.saveAs",           "Salvar como",                                 "Arquivo" },
        { "file.saveNewVersion",   "Salvar nova versão"_t,                        "Arquivo" },
        { "file.importMidi",       "Importar MIDI no piano roll do canal",        "Arquivo" },

        { "export.wav",            "Exportar WAV",                                "Exportar" },
        { "export.mp3",            "Exportar MP3",                                "Exportar" },
        { "export.midi",           "Exportar MIDI",                               "Exportar" },
        { "export.stems",          "Exportar stems (um WAV por trilha)",          "Exportar" },

        { "pattern.new",           "Próximo padrão vazio (cria um se não houver)"_t, "Padrões"_t },
        { "pattern.clone",         "Clonar o padrão atual"_t,                    "Padrões"_t },
        { "pattern.next",          "Próximo padrão"_t,                           "Padrões"_t },
        { "pattern.previous",      "Padrão anterior"_t,                          "Padrões"_t },

        { "edit.undoToggle",       "Desfazer ou refazer a última edição"_t,       "Edição"_t },
        { "edit.undo",             "Desfazer passo a passo",                      "Edição"_t },
        { "edit.redo",             "Refazer",                                     "Edição"_t },
        { "playlist.addMarker",    "Marcador na agulha (intro, verso, refrão)"_t, "Edição"_t },

        { "view.playlist",         "Playlist",                                    "Telas" },
        { "view.channelRack",      "Channel rack",                                "Telas" },
        { "view.pianoRoll",        "Piano roll",                                  "Telas" },
        { "view.files",            "Navegador de sons",                           "Telas" },
        { "view.plugins",          "Plugins",                                     "Telas" },
        { "view.mixer",            "Mixer",                                       "Telas" },
        { "view.settings",         "Áudio e MIDI"_t,                              "Telas" },
        { "view.projects",         "Projetos (ouvir sem abrir)",                  "Telas" },
        { "projects.switchDeck",   "Projetos: trocar entre os decks A e B",       "Telas" },
        { "view.shortcuts",        "Atalhos de teclado",                          "Telas" },

        { "keyboard.typing",       "Teclado do computador como piano",           "Teclado" },
        { "record.captureMidi",    "Capturar o que foi tocado (Capture MIDI)",   "Teclado" },

        { "ui.zoomIn",             "Aumentar interface",                          "Interface" },
        { "ui.zoomOut",            "Diminuir interface",                          "Interface" },
        { "ui.zoomReset",          "Interface em 100%",                           "Interface" },
    };

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
    auto b = common();

    if (name == "whynot")
    {
        b["file.new"]            = { key ('N', ctrl) };
        b["file.saveNewVersion"] = { key ('N', ctrl | alt) };
        b["edit.undo"]           = { key ('Z', ctrl) };
        b["edit.redo"]           = { key ('Y', ctrl), key ('Z', ctrl | shift) };
        b["export.stems"]        = { key ('R', ctrl | alt) };
    }
    else
    {
        // FL: Ctrl+N grava uma versão nova. O Ctrl+Z volta passo a passo, como o "Alternate undo mode"
        // do FL e o padrão do Ableton (pedido do usuário, 21/10): o Ctrl+Z que alterna refazia o que
        // a pessoa tinha acabado de apagar. O Ctrl+Alt+Z do FL continua desfazendo; o alternar fica
        // no editor de atalhos, sem tecla.
        b["file.saveNewVersion"] = { key ('N', ctrl) };
        b["edit.undo"]           = { key ('Z', ctrl), key ('Z', ctrl | alt) };
        b["edit.redo"]           = { key ('Y', ctrl), key ('Z', ctrl | shift) };
    }

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
    presetName = "fl";
    overrides.clear();

    if (! file.existsAsFile())
        return;

    // Lido a cada abertura do app: arquivo corrompido cai no preset "fl" em vez de derrubar o app.
    juce::var json;

    if (parseJsonSafely (file.loadFileAsString(), json).failed())
        return;
    const auto preset = json["preset"].toString();

    if (presetNames().contains (preset))
        presetName = preset;

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

            overrides[p.name.toString()] = keys;
        }
    }
}

bool KeyMap::save() const
{
    if (file == juce::File())
        return false;

    auto root = new juce::DynamicObject();
    root->setProperty ("preset", presetName);
    auto obj = new juce::DynamicObject();

    for (auto& [id, keys] : overrides)
    {
        juce::Array<juce::var> list;
        for (auto& k : keys) list.add (k.getTextDescription());
        obj->setProperty (id, list);
    }

    root->setProperty ("overrides", juce::var (obj));
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root)));
}

void KeyMap::setPreset (const juce::String& name)
{
    presetName = presetNames().contains (name) ? name : "fl";
    overrides.clear();
}

juce::Array<juce::KeyPress> KeyMap::keysFor (const juce::String& action) const
{
    if (auto it = overrides.find (action); it != overrides.end())
        return it->second;

    const auto b = preset (presetName);
    auto it = b.find (action);
    return it != b.end() ? it->second : juce::Array<juce::KeyPress>();
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
    // uma tecla só dispara uma ação: tira das outras
    for (auto& a : actions())
    {
        if (a.id == action)
            continue;

        auto current = keysFor (a.id);
        bool changed = false;

        for (auto& k : keys)
            changed = current.removeAllInstancesOf (k) > 0 || changed;

        if (changed)
            overrides[a.id] = current;
    }

    overrides[action] = keys;
}

void KeyMap::resetAction (const juce::String& action)
{
    overrides.erase (action);
}

juce::String KeyMap::actionFor (const juce::KeyPress& k) const
{
    for (auto& a : actions())
        for (auto& candidate : keysFor (a.id))
            if (candidate == k)
                return a.id;

    return {};
}

juce::var KeyMap::toVar() const
{
    auto root = new juce::DynamicObject();
    root->setProperty ("preset", presetName);
    juce::Array<juce::var> list;

    for (auto& a : actions())
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("id", a.id);
        o->setProperty ("title", a.title);
        o->setProperty ("group", a.group);
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
