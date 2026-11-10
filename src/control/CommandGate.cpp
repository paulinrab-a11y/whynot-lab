#include "CommandGate.h"

#include "CommandPolicy.h"
#include "ControlPolicy.h"
#include "../app/AppController.h"
#include "../common/Text.h"
#include "../model/WnlFormat.h"

namespace whynot::control
{

namespace
{
    std::string utf8 (const juce::String& s)   { return s.toStdString(); }

    juce::String fromUtf8 (std::string_view s)  { return juce::String::fromUTF8 (s.data(), (int) s.size()); }

    juce::String sentence (std::string_view why)
    {
        auto s = fromUtf8 (why);
        return s.isEmpty() ? s : s.substring (0, 1).toUpperCase() + s.substring (1);
    }

    GateDecision refuse (const juce::String& why)    { return { GateDecision::Kind::refuse, why, {} }; }

    /** Confirma levando os parâmetros (com o caminho já normalizado): é com eles que o comando roda depois do ok. */
    GateDecision confirm (const juce::String& why, const juce::var& params)   { return { GateDecision::Kind::confirm, why, params }; }

    void writeCanonical (const juce::var& v, juce::MemoryOutputStream& out)
    {
        if (auto* obj = v.getDynamicObject())
        {
            juce::StringArray keys;

            for (auto& prop : obj->getProperties())
                keys.add (prop.name.toString());

            keys.sort (false);
            out << "{";

            for (int i = 0; i < keys.size(); ++i)
            {
                out << (i > 0 ? "," : "") << juce::JSON::toString (keys[i]) << ":";
                writeCanonical (obj->getProperty (keys[i]), out);
            }

            out << "}";
            return;
        }

        if (auto* list = v.getArray())
        {
            out << "[";

            for (int i = 0; i < list->size(); ++i)
            {
                out << (i > 0 ? "," : "");
                writeCanonical (list->getReference (i), out);
            }

            out << "]";
            return;
        }

        out << juce::JSON::toString (v, true);
    }

    /** As pastas extras do navegador de sons (a mesma configuração que o SampleBrowser grava). */
    juce::StringArray soundBrowserFolders()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Navegador";
        options.filenameSuffix = ".settings";
        options.folderName = "WHYNOT LAB";
        options.osxLibrarySubFolder = "Application Support";

        const auto file = options.getDefaultFile();

        if (! file.existsAsFile())
            return {};

        juce::PropertiesFile settings (file, options);
        auto folders = juce::StringArray::fromLines (settings.getValue ("extraFolders"));
        folders.removeEmptyStrings();
        return folders;
    }

    juce::String limitsIn (const juce::var& v, int depth)
    {
        if (depth > limits::jsonDepth)
            return "Parâmetros aninhados demais."_t;

        if (v.isString())
            return v.toString().getNumBytesAsUTF8() > limits::paramStringBytes
                       ? juce::String ("Texto grande demais num parâmetro (mais de 4 KB)."_t) : juce::String();

        if (auto* list = v.getArray())
        {
            if (list->size() > limits::notesPerCall)
                return "Lista com mais de 5.000 itens num parâmetro: divida em vários pedidos."_t;

            for (auto& item : *list)
                if (auto why = limitsIn (item, depth + 1); why.isNotEmpty())
                    return why;
        }

        if (auto* obj = v.getDynamicObject())
            for (auto& prop : obj->getProperties())
                if (auto why = limitsIn (prop.value, depth + 1); why.isNotEmpty())
                    return why;

        return {};
    }

    /** Troca "state" grande (o estado de plugin em base64) pelo tamanho, em toda a árvore. */
    void stripPluginStates (juce::var& v)
    {
        if (auto* list = v.getArray())
        {
            for (auto& item : *list)
                stripPluginStates (item);

            return;
        }

        if (auto* obj = v.getDynamicObject())
        {
            // a lista de propriedades só se lê aqui; a troca vem depois, pelo setProperty
            juce::Array<std::pair<juce::Identifier, juce::String>> replace;

            for (auto& prop : obj->getProperties())
            {
                if (prop.name.toString() == "state" && prop.value.isString() && prop.value.toString().length() > 256)
                {
                    const auto kb = juce::roundToInt (prop.value.toString().length() * 0.75 / 1024.0);
                    replace.add ({ prop.name, "<estado: " + juce::String (juce::jmax (1, kb)) + " KB>" });
                }
                else
                {
                    auto child = prop.value;   // a cópia de um var com objeto ou lista aponta para o mesmo dado
                    stripPluginStates (child);
                }
            }

            for (auto& [name, text] : replace)
                obj->setProperty (name, text);
        }
    }

    void keepOnly (const juce::var& list, const juce::String& id)
    {
        if (auto* items = list.getArray())   // a lista do var é compartilhada: muda no próprio JSON
            for (int i = items->size(); --i >= 0;)
                if ((*items)[i]["id"].toString() != id)
                    items->remove (i);
    }
}

//==============================================================================
AllowedFolders allowedFolders (AppController& app, const juce::File& scratchFolder)
{
    std::vector<std::string> readWrite, readOnly;
    const auto projectFolder = app.getModel().projectFolder();

    if (projectFolder != juce::File())
        readWrite.push_back (utf8 (projectFolder.getFullPathName()));

    readWrite.push_back (utf8 (app.getDefaultProjectsFolder().getFullPathName()));   // inclui a Biblioteca
    readWrite.push_back (utf8 (scratchFolder.getFullPathName()));

    readOnly = readWrite;
    readOnly.push_back (utf8 (app.getKitFolder().getFullPathName()));

    for (auto& folder : soundBrowserFolders())
        readOnly.push_back (utf8 (folder));

    return { prepareRoots (readOnly), prepareRoots (readWrite) };
}

juce::String checkParamLimits (const juce::var& params)
{
    return limitsIn (params, 0);
}

juce::String lockedItemsVeto (const Project& before, const Project& after)
{
    for (const auto& was : before.playlistItems)
    {
        if (! was.locked)
            continue;

        const auto* now = after.findItem (was.id);
        juce::String what;

        if (now == nullptr)
            what = "apagaria"_t;
        else if (! now->locked)
            what = "destravaria"_t;
        else if (now->start != was.start || now->track != was.track)
            what = "moveria"_t;
        else if (now->length != was.length || now->offsetSeconds != was.offsetSeconds || now->patternOffset != was.patternOffset
                 || now->stretch != was.stretch)
            what = "apararia ou cortaria"_t;

        if (what.isNotEmpty())
            return "O bloco "_t + was.id + " está travado, e o comando o " + what
                   + ". A pessoa destrava pela tela (Propriedades do bloco ou o menu dele); ganho, fades, tom e mudo podem mudar travado."_t;
    }

    return {};
}

GateDecision evaluate (const juce::String& commandId, const juce::var& params, AppController& app, const AllowedFolders& folders)
{
    const auto& rule = ruleFor (utf8 (commandId));

    if (rule.risk == Risk::blocked)
        return refuse ("Este comando é só para você, pela tela: "_t + fromUtf8 (rule.why) + ".");

    if (auto why = checkParamLimits (params); why.isNotEmpty())
        return refuse (why);

    GateDecision run { GateDecision::Kind::run, {}, params.getDynamicObject() != nullptr ? params.clone() : juce::var (new juce::DynamicObject()) };
    const auto& model = app.getModel();

    // Os caminhos primeiro, em todos os parâmetros que a regra marca (inclusive as listas) e também nos comandos
    // que sempre confirmam: o que é inválido (UNC, fluxo alternativo, nome reservado) nem chega a ser perguntado.
    std::vector<PathArgument> given;

    for (std::size_t i = 0; i < maxPathParams; ++i)
    {
        const auto* param = rule.pathParam (i);

        if (param == nullptr || ! params.isObject())
            continue;

        const auto& value = params[juce::Identifier (fromUtf8 (param->name))];

        if (auto* list = value.getArray())
        {
            for (int n = 0; n < list->size(); ++n)
                given.push_back ({ param->name, n, utf8 (list->getReference (n).toString()) });
        }
        else
        {
            given.push_back ({ param->name, -1, utf8 (value.toString()) });
        }
    }

    const auto projectFolder = model.projectFolder();
    const auto checked = checkPathArguments (rule, given, folders.read, folders.write,
                                             projectFolder != juce::File() ? utf8 (projectFolder.getFullPathName()) : std::string());

    if (! checked.ok)
        return refuse (fromUtf8 (checked.reason));

    // daqui em diante o comando usa os caminhos normalizados, com ou sem confirmação
    juce::StringArray outside;

    for (auto& c : checked.paths)
    {
        const auto name = juce::Identifier (fromUtf8 (c.param));
        const auto path = fromUtf8 (c.path);

        if (c.index < 0)
            run.params.getDynamicObject()->setProperty (name, path);
        else if (auto* list = run.params[name].getArray())   // o clone copiou a lista: muda só a do pedido que vai rodar
            list->set (c.index, path);

        if (c.outside)
            outside.addIfNotAlreadyThere (path);
    }

    if (rule.risk == Risk::confirm)
        return confirm (sentence (rule.why) + ".", run.params);

    if (rule.confirmIfUnsaved && model.dirty)
        return confirm ("O projeto aberto tem alterações não salvas, que se perderiam."_t, run.params);

    // project.save sem caminho grava por cima do projeto aberto; os outros reclamam do parâmetro sozinhos
    if (rule.savesProject && model.file.existsAsFile()
        && (! params.isObject() || params[juce::Identifier (fromUtf8 (rule.path.name))].toString().isEmpty()))
        return confirm ("Grava por cima do projeto aberto ("_t + model.file.getFullPathName() + ").", run.params);

    if (outside.size() == 1)
        return confirm ("Usa um caminho fora das pastas permitidas: "_t + outside[0], run.params);

    if (outside.size() > 1)
        return confirm ("Usa caminhos fora das pastas permitidas: "_t + outside.joinIntoString ("; "), run.params);

    for (auto& c : checked.paths)
    {
        if (c.access != Access::write)
            continue;

        auto target = juce::File (fromUtf8 (c.path));
        const auto* param = rule.findPathParam (c.param);

        if (param != nullptr && ! param->extension.empty())
            target = target.withFileExtension (fromUtf8 (param->extension));

        if (rule.writesIntoFolder)
        {
            if (target.isDirectory() && target.getNumberOfChildFiles (juce::File::findFiles) > 0)
                return confirm ("A pasta já tem arquivos, e os stems podem gravar por cima: "_t + target.getFullPathName(), run.params);
        }
        else if (target.exists())
        {
            return confirm ("Grava por cima de "_t + target.getFullPathName(), run.params);
        }
    }

    return run;
}

juce::String canonicalJson (const juce::var& v)
{
    juce::MemoryOutputStream out;
    writeCanonical (v, out);
    return out.toUTF8();
}

juce::var inspectProject (const Project& project, const juce::String& pattern, const juce::String& channel, const juce::StringArray& sections)
{
    auto json = juce::JSON::parse (wnl::toJson (project));
    stripPluginStates (json);

    if (auto* root = json.getDynamicObject(); root != nullptr && ! sections.isEmpty())
    {
        juce::Array<juce::Identifier> drop;

        for (auto& prop : root->getProperties())
        {
            const auto key = prop.name.toString();

            if (key != "format" && key != "schemaVersion" && ! sections.contains (key))
                drop.add (prop.name);
        }

        for (auto& key : drop)
            root->removeProperty (key);
    }

    if (pattern.isNotEmpty())
        keepOnly (json["patterns"], pattern);

    if (channel.isNotEmpty())
    {
        keepOnly (json["channels"], channel);

        if (auto* patterns = json["patterns"].getArray())
        {
            for (auto& p : *patterns)
            {
                if (auto* lanes = p["lanes"].getDynamicObject())
                {
                    juce::Array<juce::Identifier> drop;

                    for (auto& lane : lanes->getProperties())
                        if (lane.name.toString() != channel)
                            drop.add (lane.name);

                    for (auto& key : drop)
                        lanes->removeProperty (key);
                }
            }
        }
    }

    return json;
}

} // namespace whynot::control
