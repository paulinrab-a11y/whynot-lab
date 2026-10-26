#include "UserMix.h"

#include "../common/SafeJson.h"
#include "../common/Text.h"
#include "../model/WnlFormat.h"

namespace whynot::usermix
{

namespace
{
    juce::File dataFolder;

    struct Cache
    {
        juce::File file;
        juce::Time modified;
        std::vector<UserPreset> presets;
        bool valid = false;
    };

    Cache cache;

    /** "Minha voz trap!" → "minha-voz-trap" (letras com acento ficam). */
    juce::String slugOf (const juce::String& name)
    {
        juce::String out;
        const auto lower = name.trim().toLowerCase();

        for (auto ptr = lower.getCharPointer(); ! ptr.isEmpty(); ++ptr)
        {
            const auto c = *ptr;

            if (juce::CharacterFunctions::isLetterOrDigit (c))
                out << juce::String::charToString (c);
            else if (out.isNotEmpty() && ! out.endsWithChar ('-'))
                out << "-";
        }

        return out.trimCharactersAtEnd ("-");
    }

    juce::Result writeAtomically (const juce::File& file, const juce::String& text)
    {
        if (! file.getParentDirectory().createDirectory())
            return juce::Result::fail ("Não consegui criar a pasta "_t + file.getParentDirectory().getFullPathName());

        juce::TemporaryFile temp (file);

        if (! temp.getFile().replaceWithText (text, false, false, "\n") || ! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("Não consegui gravar "_t + file.getFullPathName());

        return juce::Result::ok();
    }

    const std::vector<UserPreset>& allPresets()
    {
        const auto file = presetsFile();
        const auto modified = file.getLastModificationTime();

        if (cache.valid && cache.file == file && cache.modified == modified)
            return cache.presets;

        cache.presets.clear();
        juce::var root;

        if (file.existsAsFile() && parseJsonSafely (file.loadFileAsString(), root).wasOk())
        {
            if (auto* list = root["presets"].getArray())
            {
                for (auto& v : *list)
                {
                    UserPreset p;
                    p.type = v["type"].toString();
                    p.id = v["id"].toString();
                    p.name = v["name"].toString();

                    if (auto* o = v["params"].getDynamicObject())
                        for (auto& kv : o->getProperties())
                            if (kv.value.isDouble() || kv.value.isInt() || kv.value.isInt64())
                                p.params[kv.name.toString()] = (float) (double) kv.value;

                    if (p.type.isNotEmpty() && isUserPresetId (p.id) && p.name.isNotEmpty())
                        cache.presets.push_back (std::move (p));
                }
            }
        }

        cache.file = file;
        cache.modified = modified;
        cache.valid = true;
        return cache.presets;
    }

    juce::Result writePresets (const std::vector<UserPreset>& list)
    {
        juce::Array<juce::var> items;

        for (auto& p : list)
        {
            auto* params = new juce::DynamicObject();

            for (auto& [id, value] : p.params)
                params->setProperty (id, std::round (value * 10000.0f) / 10000.0f);

            auto* o = new juce::DynamicObject();
            o->setProperty ("type", p.type);
            o->setProperty ("id", p.id);
            o->setProperty ("name", p.name);
            o->setProperty ("params", juce::var (params));
            items.add (juce::var (o));
        }

        auto* root = new juce::DynamicObject();
        root->setProperty ("version", 1);
        root->setProperty ("presets", items);
        const auto result = writeAtomically (presetsFile(), juce::JSON::toString (juce::var (root)));
        cache.valid = false;
        return result;
    }

    juce::File chainFile (const juce::String& name)
    {
        return chainsFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".wnl");
    }
}

void setDataFolder (const juce::File& f)
{
    dataFolder = f;
    cache.valid = false;
}

juce::File chainsFolder()   { return dataFolder.getChildFile ("cadeias"); }
juce::File presetsFile()    { return dataFolder.getChildFile ("meus-presets.json"); }

std::vector<UserPreset> presetsFor (const juce::String& type)
{
    std::vector<UserPreset> out;

    for (auto& p : allPresets())
        if (p.type == type)
            out.push_back (p);

    std::sort (out.begin(), out.end(), [] (const UserPreset& a, const UserPreset& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

std::optional<UserPreset> findPreset (const juce::String& type, const juce::String& id)
{
    for (auto& p : allPresets())
        if (p.type == type && p.id == id)
            return p;

    return std::nullopt;
}

juce::Result savePreset (const juce::String& type, const juce::String& name, const std::map<juce::String, float>& params,
                         juce::String& idOut)
{
    const auto slug = slugOf (name);

    if (slug.isEmpty())
        return juce::Result::fail ("Dê um nome ao preset (letras ou números)."_t);

    if (dataFolder == juce::File())
        return juce::Result::fail ("A pasta de dados do app não está pronta."_t);

    auto list = allPresets();
    idOut = userPrefix + slug;
    const auto id = idOut;
    std::erase_if (list, [&type, &id] (const UserPreset& p) { return p.type == type && p.id == id; });
    list.push_back ({ type, idOut, name.trim(), params });
    return writePresets (list);
}

juce::Result deletePreset (const juce::String& type, const juce::String& id)
{
    auto list = allPresets();
    const auto before = list.size();
    std::erase_if (list, [&type, &id] (const UserPreset& p) { return p.type == type && p.id == id; });

    if (list.size() == before)
        return juce::Result::fail ("Preset não encontrado: "_t + id);

    return writePresets (list);
}

juce::StringArray chainNames()
{
    juce::StringArray names;

    for (auto& f : chainsFolder().findChildFiles (juce::File::findFiles, false, "*.wnl"))
        names.add (f.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

juce::Result saveChain (const juce::String& name, const std::vector<Insert>& inserts, const std::vector<Macro>& macros)
{
    if (name.trim().isEmpty() || juce::File::createLegalFileName (name.trim()).isEmpty())
        return juce::Result::fail ("Dê um nome à cadeia."_t);

    if (dataFolder == juce::File())
        return juce::Result::fail ("A pasta de dados do app não está pronta."_t);

    Project p;
    p.name = name.trim();
    wnl::normalise (p);

    auto* master = p.findMixer (masterTrackId);

    if (master == nullptr)
        return juce::Result::fail ("Não consegui montar a cadeia."_t);

    master->inserts = inserts;
    master->macros = macros;

    for (auto& ins : master->inserts)
        ins.sidechain = {};   // quem dispara é uma trilha de outro projeto

    return writeAtomically (chainFile (name), wnl::toJson (p));
}

juce::Result loadChain (const juce::String& name, std::vector<Insert>& out, std::vector<Macro>* macros)
{
    const auto file = chainFile (name);

    if (! file.existsAsFile())
        return juce::Result::fail ("Cadeia não encontrada: "_t + name);

    Project p;
    const auto result = wnl::load (file, p);

    if (result.failed())
        return result;

    const auto* master = p.findMixer (masterTrackId);

    if (master == nullptr)
        return juce::Result::fail ("O arquivo da cadeia não tem efeitos: "_t + file.getFullPathName());

    out = master->inserts;

    if (macros != nullptr)
        *macros = master->macros;

    return juce::Result::ok();
}

juce::Result deleteChain (const juce::String& name)
{
    const auto file = chainFile (name);

    if (! file.existsAsFile())
        return juce::Result::fail ("Cadeia não encontrada: "_t + name);

    // Vai para "cadeias/apagadas" (dá para recuperar pelo Explorer), sem encher a lixeira do sistema.
    const auto removed = chainsFolder().getChildFile ("apagadas");

    if (removed.createDirectory() && file.moveFileTo (removed.getNonexistentChildFile (file.getFileNameWithoutExtension(), ".wnl")))
        return juce::Result::ok();

    return juce::Result::fail ("Não consegui apagar "_t + file.getFullPathName());
}

} // namespace whynot::usermix
