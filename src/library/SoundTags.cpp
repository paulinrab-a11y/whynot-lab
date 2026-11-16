#include "SoundTags.h"

#include "../common/SafeJson.h"
#include "../common/Text.h"

namespace whynot::soundtags
{

Store& Store::get()
{
    static Store store;
    return store;
}

void Store::setFolder (const juce::File& dataFolder)
{
    storeFile = dataFolder.getChildFile ("tags-sons.json");
    load();
}

juce::String Store::keyOf (const juce::File& f)
{
    return f.getFullPathName().toLowerCase();
}

UserTags Store::of (const juce::File& f) const
{
    const auto it = entries.find (keyOf (f));
    return it != entries.end() ? it->second : UserTags();
}

Tags Store::tagsFor (const juce::File& f) const
{
    const auto path = f.getFullPathName();
    auto tags = tagsForPath (std::string_view (path.toRawUTF8(), path.getNumBytesAsUTF8()));
    const auto user = of (f);
    return withOverride (tags, user.hasTypes, user.types);
}

bool Store::change (const juce::Array<juce::File>& files, const juce::StringArray& add, const juce::StringArray& remove)
{
    for (auto& f : files)
    {
        auto& e = entries[keyOf (f)];

        for (auto label : add)
            if ((label = label.trim().substring (0, 40)).isNotEmpty() && ! e.labels.contains (label, true))
                e.labels.add (label);

        for (auto& label : remove)
            e.labels.removeString (label.trim(), true);

        if (e.empty())
            entries.erase (keyOf (f));
    }

    return save();
}

bool Store::setTypes (const juce::Array<juce::File>& files, Mask types, bool clear)
{
    for (auto& f : files)
    {
        auto& e = entries[keyOf (f)];
        e.hasTypes = ! clear;
        e.types = clear ? 0 : types;

        if (e.empty())
            entries.erase (keyOf (f));
    }

    return save();
}

juce::StringArray Store::allLabels() const
{
    juce::StringArray out;

    for (auto& [key, e] : entries)
        for (auto& label : e.labels)
            out.addIfNotAlreadyThere (label, true);

    out.sortNatural();
    return out;
}

void Store::load()
{
    entries.clear();
    problem.clear();
    ++rev;

    if (! storeFile.existsAsFile())
        return;

    juce::var parsed;
    const auto read = parseJsonSafely (storeFile.loadFileAsString(), parsed);

    if (read.failed() || ! parsed.isObject())
    {
        // estragado: guarda ao lado e começa vazio (nada se perde, nada derruba)
        const auto kept = storeFile.getSiblingFile ("tags-sons.estragado.json");
        kept.deleteFile();
        storeFile.moveFileTo (kept);
        problem = "O arquivo de tags dos sons estava estragado e foi guardado em "_t + kept.getFileName() + "; começando sem tags.";
        return;
    }

    if (auto* object = parsed.getDynamicObject())
    {
        for (auto& p : object->getProperties())
        {
            UserTags e;

            if (auto* labels = p.value["tags"].getArray())
                for (auto& l : *labels)
                    if (const auto s = l.toString().trim().substring (0, 40); s.isNotEmpty())
                        e.labels.addIfNotAlreadyThere (s, true);

            if (auto* types = p.value["types"].getArray())
            {
                e.hasTypes = true;

                for (auto& t : *types)
                    if (const int i = typeFromId (t.toString().toStdString()); i >= 0)
                        e.types |= bit ((Type) i);
            }

            if (! e.empty())
                entries[p.name.toString().toLowerCase()] = std::move (e);
        }
    }
}

bool Store::save()
{
    ++rev;

    if (storeFile == juce::File())
        return false;

    auto* root = new juce::DynamicObject();

    for (auto& [key, e] : entries)
    {
        auto* o = new juce::DynamicObject();

        if (! e.labels.isEmpty())
        {
            juce::Array<juce::var> labels;

            for (auto& l : e.labels)
                labels.add (l);

            o->setProperty ("tags", labels);
        }

        if (e.hasTypes)
        {
            juce::Array<juce::var> types;

            for (int i = 0; i < numTypes; ++i)
                if ((e.types & bit ((Type) i)) != 0)
                    types.add (juce::String (typeId ((Type) i)));

            o->setProperty ("types", types);
        }

        root->setProperty (key, juce::var (o));
    }

    // atômico: escreve ao lado e troca
    storeFile.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (storeFile);

    if (! temp.getFile().replaceWithText (juce::JSON::toString (juce::var (root))))
        return false;

    return temp.overwriteTargetFileWithTemporary();
}

} // namespace whynot::soundtags
