#include "Favorites.h"
#include "../common/SafeJson.h"

namespace whynot
{

namespace
{
    // Nomes dos tipos no favoritos.json e nos comandos favorites.*
    constexpr std::array<const char*, Favorites::numKinds> kindNames { "sample", "folder", "plugin", "effect", "effectPreset", "synthPreset" };
    constexpr int fileVersion = 1;
}

juce::String Favorites::kindName (Kind k)
{
    return kindNames[(size_t) k];
}

std::optional<Favorites::Kind> Favorites::kindFromName (const juce::String& name)
{
    for (int i = 0; i < numKinds; ++i)
        if (name == kindNames[(size_t) i])
            return (Kind) i;

    return std::nullopt;
}

Favorites::Favorites()
    : folder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("WHYNOT LAB"))
{
}

Favorites& Favorites::get()
{
    static Favorites instance;
    return instance;
}

void Favorites::setFolder (const juce::File& dataFolder)
{
    if (dataFolder == juce::File())
        return;

    folder = dataFolder;
    loaded = false;
    changed();
}

juce::File Favorites::getFile() const
{
    return folder.getChildFile ("favoritos.json");
}

//==============================================================================
void Favorites::load() const
{
    loaded = true;
    loadError = {};

    for (auto& list : ids)
        list.clear();

    if (const auto file = getFile(); file.existsAsFile())
    {
        juce::var root;
        const auto result = parseJsonSafely (file.loadFileAsString(), root);

        if (result.failed() || ! root.isObject())
        {
            // Começa vazio, mas guarda o arquivo: o próximo gravar escreve por cima do favoritos.json.
            loadError = result.failed() ? result.getErrorMessage() : juce::String ("o arquivo não tem os favoritos"_t);
            file.copyFileTo (folder.getChildFile ("favoritos-quebrado.json"));
        }
        else
        {
            const auto lists = root["favorites"];

            for (int k = 0; k < numKinds; ++k)
            {
                if (const auto* items = lists[kindNames[(size_t) k]].getArray())
                {
                    for (auto& v : *items)
                    {
                        const auto id = normalised ((Kind) k, v.isString() ? v.toString() : juce::String());

                        if (id.isNotEmpty() && indexOf ((Kind) k, id) < 0)
                            ids[(size_t) k].add (id);
                    }
                }
            }
        }
    }

    migrateSynthFavorites();
}

void Favorites::migrateSynthFavorites() const
{
    // Até 21/10 os presets favoritos do synth ficavam num arquivo só deles, um id por linha.
    const auto old = folder.getChildFile ("synth-favoritos.txt");

    if (! old.existsAsFile())
        return;

    auto& synth = ids[(size_t) Kind::synthPreset];

    for (auto& line : juce::StringArray::fromLines (old.loadFileAsString()))
        if (line.trim().isNotEmpty() && ! synth.contains (line.trim()))
            synth.add (line.trim());

    // Só some depois de gravado no favoritos.json: se o gravar falhar, a próxima vez tenta de novo.
    if (save())
        old.moveFileTo (folder.getChildFile ("synth-favoritos.txt.migrado"));
}

bool Favorites::save() const
{
    auto* lists = new juce::DynamicObject();

    for (int k = 0; k < numKinds; ++k)
    {
        juce::Array<juce::var> items;

        for (auto& id : ids[(size_t) k])
            items.add (id);

        lists->setProperty (kindNames[(size_t) k], items);
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", fileVersion);
    root->setProperty ("favorites", juce::var (lists));

    folder.createDirectory();
    saveFailed = ! getFile().replaceWithText (juce::JSON::toString (juce::var (root)) + "\n");
    return ! saveFailed;
}

void Favorites::changed()
{
    listeners.call ([] (Listener& l) { l.favoritesChanged(); });
}

//==============================================================================
juce::String Favorites::normalised (Kind k, const juce::String& id)
{
    const auto trimmed = id.trim();

    // Caminho: a mesma forma que o navegador usa (barras do sistema, sem barra no fim).
    if (isPathKind (k) && juce::File::isAbsolutePath (trimmed))
        return juce::File (trimmed).getFullPathName();

    return trimmed;
}

int Favorites::indexOf (Kind k, const juce::String& id) const
{
    // No Windows, "C:\Sons\Kick.wav" e "c:\sons\kick.wav" são o mesmo arquivo.
    const bool ignoreCase = isPathKind (k) && ! juce::File::areFileNamesCaseSensitive();
    return ids[(size_t) k].indexOf (id, ignoreCase);
}

bool Favorites::contains (Kind k, const juce::String& id) const
{
    ensureLoaded();
    const auto key = normalised (k, id);
    return key.isNotEmpty() && indexOf (k, key) >= 0;
}

bool Favorites::set (Kind k, const juce::String& id, bool on)
{
    ensureLoaded();
    const auto key = normalised (k, id);

    if (key.isEmpty())
        return false;

    const int index = indexOf (k, key);

    if (on == (index >= 0))
        return false;

    if (on)
        ids[(size_t) k].add (key);
    else
        ids[(size_t) k].remove (index);

    save();
    changed();
    return true;
}

bool Favorites::toggle (Kind k, const juce::String& id)
{
    const bool on = ! contains (k, id);
    set (k, id, on);
    return on;
}

juce::StringArray Favorites::list (Kind k) const
{
    ensureLoaded();
    return ids[(size_t) k];
}

} // namespace whynot
