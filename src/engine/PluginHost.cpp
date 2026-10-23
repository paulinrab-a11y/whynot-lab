#include "PluginHost.h"
#include "../plugins/PluginCatalog.h"

namespace whynot::plugin_host
{

PluginRef refFrom (const juce::PluginDescription& d)
{
    PluginRef r;
    r.name = d.name;
    r.manufacturer = d.manufacturerName;
    r.format = d.pluginFormatName;
    r.file = d.fileOrIdentifier.replaceCharacter ('\\', '/');
    r.uid = juce::String::toHexString (d.uniqueId);
    r.instrument = d.isInstrument;
    return r;
}

juce::PluginDescription descriptionOf (const PluginRef& r)
{
    juce::PluginDescription d;
    d.name = r.name;
    d.manufacturerName = r.manufacturer;
    d.pluginFormatName = r.format;
    d.fileOrIdentifier = juce::File::isAbsolutePath (r.file) ? juce::File (r.file).getFullPathName() : r.file;
    d.uniqueId = (int) r.uid.getHexValue64();
    d.isInstrument = r.instrument;
    return d;
}

te::Plugin::Ptr create (te::Edit& edit, const PluginRef& ref)
{
    auto state = te::ExternalPlugin::create (edit.engine, descriptionOf (ref));

    // O Tracktion restaura o estado a partir desta propriedade quando a instância é criada.
    if (ref.state.isNotEmpty())
        state.setProperty (te::IDs::state, ref.state, nullptr);

    return edit.getPluginCache().createNewPlugin (state);
}

juce::AudioPluginInstance* instanceOf (te::Plugin* plugin)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
        return ext->getAudioPluginInstance();

    return nullptr;
}

juce::String readState (te::Plugin& plugin)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&plugin); ext != nullptr && ext->getAudioPluginInstance() != nullptr)
    {
        ext->flushPluginStateToValueTree();
        return ext->state.getProperty (te::IDs::state).toString();
    }

    return {};
}

juce::String problemFor (te::Plugin* plugin, const PluginRef& ref, const PluginCatalog* catalog)
{
    if (instanceOf (plugin) != nullptr)
        return {};

    auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin);
    const auto file = ext != nullptr ? ext->desc.fileOrIdentifier : descriptionOf (ref).fileOrIdentifier;

    if (catalog != nullptr)
    {
        if (const auto reason = catalog->blockReason (file); reason.isNotEmpty())
            return "Plugin bloqueado: "_t + ref.name + " (" + reason + ")";

        bool known = false;

        for (auto& d : catalog->getTypes())
            known = known || (d.name == ref.name && juce::String::toHexString (d.uniqueId) == ref.uid) || d.fileOrIdentifier == file;

        if (! known)
            return "Plugin não encontrado: "_t + ref.name + ". Escaneie os plugins (F8) ou instale o plugin.";
    }

    const auto error = ext != nullptr ? ext->getLoadError() : juce::String();
    return ref.name + " não abriu"_t + (error.isNotEmpty() ? ": " + error : juce::String ("."));
}

} // namespace whynot::plugin_host
