#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <optional>

namespace whynot
{

/** Favoritos do usuário (as estrelas): sons, pastas de kit, plugins VST3, efeitos nativos, presets de efeito
    e presets do WHYNOT Synth. São preferência de quem usa, não do projeto: ficam em
    <pasta de dados>/favoritos.json (a mesma do atalhos.json) e não entram no desfazer.

    Um só no app (Favorites::get()), na thread de mensagem. O AppController informa a pasta; o autoteste
    usa uma pasta própria. Ao trocar de pasta, o synth-favoritos.txt antigo (um id por linha) entra no
    favoritos.json e vira synth-favoritos.txt.migrado. */
class Favorites
{
public:
    enum class Kind { sample, folder, plugin, effect, effectPreset, synthPreset };
    static constexpr int numKinds = 6;

    /** Ids por tipo:
        - sample, folder: caminho completo do arquivo ou da pasta;
        - plugin: PluginDescription::createIdentifierString();
        - effect: o tipo do efeito nativo ("eq");
        - effectPreset: "<tipo>/<preset>" (effectPresetId);
        - synthPreset: o id do preset do synth. */
    static juce::String kindName (Kind);
    static std::optional<Kind> kindFromName (const juce::String&);
    static juce::String effectPresetId (const juce::String& type, const juce::String& preset)   { return type + "/" + preset; }

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void favoritesChanged() = 0;
    };

    static Favorites& get();

    void setFolder (const juce::File& dataFolder);
    juce::File getFile() const;

    bool contains (Kind, const juce::String& id) const;

    /** true se mudou alguma coisa (grava o arquivo e avisa os ouvintes). */
    bool set (Kind, const juce::String& id, bool on);

    /** Inverte e devolve o estado novo. */
    bool toggle (Kind, const juce::String& id);

    /** Na ordem em que foram marcados. */
    juce::StringArray list (Kind) const;

    /** Para a ordem dos menus e das listas: os favoritos primeiro, cada grupo na ordem que já tinha. */
    template <typename T, typename IdOf>
    void favoritesFirst (std::vector<T>& items, Kind kind, IdOf&& idOf) const
    {
        std::stable_partition (items.begin(), items.end(), [&] (const T& item) { return contains (kind, idOf (item)); });
    }

    void addListener (Listener* l)      { listeners.add (l); }
    void removeListener (Listener* l)   { listeners.remove (l); }

    /** O que deu errado ao ler o arquivo (vazio = nada); o arquivo quebrado é guardado em favoritos-quebrado.json. */
    juce::String getLoadError() const     { ensureLoaded(); return loadError; }

    /** O último gravar falhou (disco cheio, pasta sem permissão): a marca vale até fechar o app. */
    bool lastSaveFailed() const noexcept  { return saveFailed; }

private:
    Favorites();

    // Lê na primeira consulta, não ao criar: o autoteste troca a pasta antes, e assim nunca abre a do usuário.
    void ensureLoaded() const             { if (! loaded) load(); }
    void load() const;
    void migrateSynthFavorites() const;
    bool save() const;
    void changed();
    static bool isPathKind (Kind k) noexcept   { return k == Kind::sample || k == Kind::folder; }
    static juce::String normalised (Kind, const juce::String& id);
    int indexOf (Kind, const juce::String& id) const;

    juce::File folder;
    mutable std::array<juce::StringArray, numKinds> ids;
    mutable bool loaded = false, saveFailed = false;
    mutable juce::String loadError;
    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE (Favorites)
};

} // namespace whynot
