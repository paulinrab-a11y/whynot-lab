#pragma once

#include <JuceHeader.h>

#include <map>

namespace whynot
{

/** Uma ação que pode ter atalho de teclado. */
struct ShortcutAction
{
    juce::String id;      // "file.saveNewVersion"
    juce::String title;   // em português, como aparece no editor e nos menus
    juce::String group;   // Transporte, Arquivo, Exportar, Edição, Telas, Teclado, Interface
};

/** Atalhos de teclado configuráveis.

    Dois presets prontos: "fl" (as teclas do FL Studio para as mesmas ações: Ctrl+N salva uma
    versão nova, Ctrl+Z alterna desfazer/refazer, Ctrl+Alt+Z desfaz passo a passo...) e "whynot"
    (o costume dos outros programas: Ctrl+N projeto novo, Ctrl+Z/Ctrl+Y). Por cima do preset,
    o usuário troca a tecla de qualquer ação; fica tudo em atalhos.json na pasta de dados.
*/
class KeyMap
{
public:
    using Bindings = std::map<juce::String, juce::Array<juce::KeyPress>>;

    static const std::vector<ShortcutAction>& actions();
    static const ShortcutAction* findAction (const juce::String& id);

    static juce::StringArray presetNames()   { return { "fl", "whynot" }; }
    static juce::String presetTitle (const juce::String& name);
    static Bindings preset (const juce::String& name);

    /** "Ctrl+Shift+R", "Espaço", "Alt+F8". */
    static juce::String describe (const juce::KeyPress&);
    static juce::KeyPress parse (const juce::String&);

    explicit KeyMap (const juce::File& file = {});

    void load();
    bool save() const;

    juce::String getPreset() const noexcept   { return presetName; }
    void setPreset (const juce::String&);      // apaga as trocas do usuário

    juce::Array<juce::KeyPress> keysFor (const juce::String& action) const;
    juce::String describeKeys (const juce::String& action) const;   // "Ctrl+Z, Ctrl+Y" ou vazio

    /** Troca as teclas de uma ação (lista vazia = sem atalho). Tira as mesmas teclas de outras ações. */
    void setKeys (const juce::String& action, const juce::Array<juce::KeyPress>&);
    void resetAction (const juce::String& action);
    bool isCustomised (const juce::String& action) const   { return overrides.count (action) > 0; }

    /** A ação que essa tecla dispara, ou vazio. */
    juce::String actionFor (const juce::KeyPress&) const;

    /** Para a IA e o autoteste: { preset, actions: [{ id, title, group, keys: [...] }] }. */
    juce::var toVar() const;

private:
    juce::File file;
    juce::String presetName { "fl" };
    Bindings overrides;
};

} // namespace whynot
