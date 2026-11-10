#pragma once

#include <JuceHeader.h>

#include "KeyMapCore.h"

#include <map>

namespace whynot
{

/** Uma ação que pode ter atalho de teclado. */
struct ShortcutAction
{
    juce::String id;      // "file.saveNewVersion"
    juce::String title;   // em português, como aparece no editor e nos menus
    juce::String group;   // Transporte, Arquivo, Exportar, Edição, Telas, Teclado, Interface, Playlist, Piano roll
    juce::String context; // vazio = vale no app inteiro; "playlist" ou "pianoroll" = com o teclado naquele editor
};

/** Atalhos de teclado configuráveis.

    Dois presets prontos: "fl" (as teclas do FL Studio para as mesmas ações: Ctrl+N salva uma
    versão nova, Ctrl+Z alterna desfazer/refazer, Ctrl+Alt+Z desfaz passo a passo...) e "whynot"
    (o costume dos outros programas: Ctrl+N projeto novo, Ctrl+Z/Ctrl+Y). Por cima do preset,
    o usuário troca a tecla de qualquer ação; fica tudo em atalhos.json na pasta de dados.

    As ferramentas e a edição da playlist e do piano roll também estão aqui, cada uma com o contexto do seu
    editor: a mesma tecla pode valer nos dois (o P é o lápis na playlist e no piano roll). A tabela e a
    resolução tecla → ação moram no KeyMapCore.h, sem JUCE, com teste.
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

    /** Os contextos dos editores (o do app inteiro é a string vazia). */
    static constexpr const char* playlistContext = "playlist";
    static constexpr const char* pianoRollContext = "pianoroll";

    juce::String getPreset() const            { return juce::String (core.preset()); }
    void setPreset (const juce::String&);      // apaga as trocas do usuário

    juce::Array<juce::KeyPress> keysFor (const juce::String& action) const;
    juce::String describeKeys (const juce::String& action) const;   // "Ctrl+Z, Ctrl+Y" ou vazio

    /** Troca as teclas de uma ação (lista vazia = sem atalho). Tira as mesmas teclas das ações que disputam
        com ela: as do mesmo contexto e, se uma das duas vale no app inteiro, a outra também. */
    void setKeys (const juce::String& action, const juce::Array<juce::KeyPress>&);
    void resetAction (const juce::String& action);
    bool isCustomised (const juce::String& action) const   { return core.isCustomised (action.toStdString()); }

    /** A ação do app inteiro que essa tecla dispara, ou vazio. */
    juce::String actionFor (const juce::KeyPress&) const;

    /** A ação de um editor (playlistContext, pianoRollContext) que essa tecla dispara, ou vazio. */
    juce::String actionFor (const juce::KeyPress&, const char* context) const;

    /** A ação que perderia essa tecla se ela fosse dada a `action` (o aviso do editor de atalhos), ou vazio. */
    juce::String clashFor (const juce::String& action, const juce::KeyPress&) const;

    /** Para a IA e o autoteste: { preset, actions: [{ id, title, group, context, keys: [...] }] }. */
    juce::var toVar() const;

private:
    juce::File file;
    keymap::Map core;   // o preset escolhido e as trocas do usuário
};

} // namespace whynot
