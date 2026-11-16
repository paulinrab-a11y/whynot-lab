#include "Templates.h"

#include "TemplateCore.h"
#include "../commands/CommandRegistry.h"
#include "../common/Text.h"
#include "../ideas/Genres.h"
#include "../model/WnlFormat.h"

#include <optional>

namespace whynot::templates
{

namespace
{
    juce::File dataFolder;

    // O pedido do template.new, entre o request() e o endRequest(). Só a thread de mensagem mexe.
    struct Pending
    {
        bool active = false;                          // há um request() aberto
        bool served = false;                          // um newProject passou pelo gancho 1 desde o request()
        juce::String name;
        std::optional<Project> project;               // template do usuário, já lido
    };

    Pending pending;
    const core::FactoryTemplate* recipe = nullptr;    // do gancho 1 para o gancho 2, dentro de um newProject

    juce::String fromStd (const std::string& s)       { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

    std::vector<std::string> toStd (const juce::StringArray& names)
    {
        std::vector<std::string> out;

        for (auto& n : names)
            out.push_back (n.toStdString());

        return out;
    }

    juce::File defaultFile()   { return folder().getChildFile ("padrao.txt"); }

    juce::String storedDefault()
    {
        const auto file = defaultFile();
        return file.existsAsFile() ? juce::StringArray::fromLines (file.loadFileAsString())[0].trim() : juce::String();
    }

    /** Caminho de um arquivo do template → absoluto, com barras para a frente (como o channel.addSampler grava). */
    juce::String absolutePath (const juce::String& path, const juce::File& templateFolder)
    {
        if (path.isEmpty() || juce::File::isAbsolutePath (path))
            return path;

        return templateFolder.getChildFile (path).getFullPathName().replaceCharacter ('\\', '/');
    }

    /** Move a pasta de um template para Templates/apagados, sem sobrescrever o que já está lá. */
    juce::Result moveToDeleted (const juce::File& templateFolder)
    {
        const auto removed = deletedFolder();

        if (! removed.createDirectory())
            return juce::Result::fail ("Não consegui criar a pasta "_t + removed.getFullPathName());

        juce::StringArray already;

        for (auto& f : removed.findChildFiles (juce::File::findFilesAndDirectories, false))
            already.add (f.getFileName());

        const auto target = removed.getChildFile (fromStd (core::deletedNameFor (templateFolder.getFileName().toStdString(), toStd (already))));

        if (! templateFolder.moveFileTo (target))
            return juce::Result::fail ("Não consegui mover "_t + templateFolder.getFullPathName()
                                       + " para a pasta apagados (um arquivo dele pode estar aberto em outro programa)."_t);

        return juce::Result::ok();
    }

    juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto* o = new juce::DynamicObject();

        for (auto& [key, value] : kv)
            o->setProperty (key, value);

        return juce::var (o);
    }

    juce::String channelNamed (const Project& p, const char* name)
    {
        for (auto& c : p.channels)
            if (c.name.equalsIgnoreCase (name))
                return c.id;

        return {};
    }
}

//==============================================================================
void setDataFolder (const juce::File& f)
{
    dataFolder = f;
    pending = {};
    recipe = nullptr;
}

juce::File folder()          { return dataFolder.getChildFile ("Templates"); }
juce::File deletedFolder()   { return folder().getChildFile (fromStd (std::string (core::deletedFolderName))); }

juce::String folderNameOf (const juce::String& name)
{
    return fromStd (core::folderNameFor (juce::File::createLegalFileName (name.trim()).toStdString()));
}

juce::File fileFor (const juce::String& name)
{
    const auto clean = folderNameOf (name);
    return clean.isEmpty() ? juce::File() : folder().getChildFile (clean).getChildFile (clean + ".wnl");
}

juce::StringArray userNames()
{
    juce::StringArray names;

    if (dataFolder == juce::File())
        return names;

    // um template é uma pasta com o .wnl de mesmo nome dentro (a pasta "apagados" e as de rascunho não têm)
    for (auto& dir : folder().findChildFiles (juce::File::findDirectories, false))
    {
        const auto name = dir.getFileName();

        if (dir.getChildFile (name + ".wnl").existsAsFile() && core::findFactory (name.toStdString()) == nullptr
            && ! core::isReservedName (name.toStdString()))
            names.add (name);
    }

    auto sorted = toStd (names);
    core::sortNames (sorted);
    names.clear();

    for (auto& n : sorted)
        names.add (fromStd (n));

    return names;
}

std::vector<Info> list()
{
    std::vector<Info> out;

    for (auto& e : core::listAll (toStd (userNames()), storedDefault().toStdString()))
        out.push_back ({ fromStd (e.name), fromStd (e.description), e.factory, e.isDefault });

    return out;
}

bool exists (const juce::String& name)
{
    if (core::findFactory (name.trim().toStdString()) != nullptr)
        return true;

    const auto file = fileFor (name);
    return file != juce::File() && file.existsAsFile();
}

juce::String defaultName()
{
    return fromStd (core::resolveDefault (storedDefault().toStdString(), toStd (userNames())));
}

juce::Result setDefault (const juce::String& requested)
{
    if (dataFolder == juce::File())
        return juce::Result::fail ("A pasta de dados do app não está pronta."_t);

    const auto name = requested.trim();
    const auto file = defaultFile();

    if (name.isEmpty())
    {
        if (file.existsAsFile() && ! file.deleteFile())
            return juce::Result::fail ("Não consegui apagar "_t + file.getFullPathName());

        return juce::Result::ok();
    }

    juce::String canonical;

    if (const auto* factory = core::findFactory (name.toStdString()))
        canonical = fromStd (factory->name);
    else if (const auto mine = fileFor (name); mine != juce::File() && mine.existsAsFile())
        canonical = mine.getFileNameWithoutExtension();
    else
        return juce::Result::fail ("Template não encontrado: "_t + name + " (veja o template.list)."_t);

    if (! file.getParentDirectory().createDirectory() || ! file.replaceWithText (canonical + "\n", false, false, "\n"))
        return juce::Result::fail ("Não consegui gravar "_t + file.getFullPathName());

    return juce::Result::ok();
}

//==============================================================================
void clearNotes (Project& p)
{
    for (auto& pattern : p.patterns)
    {
        pattern.notes.clear();

        for (auto& [channel, steps] : pattern.steps)
            for (auto& step : steps)
                step = Step();
    }

    p.playlistItems.clear();
    p.automation.clear();
}

juce::Result save (const juce::String& requested, const Project& current, const juce::File& projectFolder,
                   const SaveOptions& options, juce::String* savedName)
{
    if (dataFolder == juce::File())
        return juce::Result::fail ("A pasta de dados do app não está pronta."_t);

    const auto name = folderNameOf (requested);

    if (const auto problem = core::checkName (name.toStdString(), toStd (userNames()), options.replace); problem != core::NameProblem::none)
        return juce::Result::fail (juce::String::fromUTF8 (core::problemText (problem)));

    auto copy = current;
    copy.name = name;          // o nome do projeto não entra: o template leva o dele
    copy.punchOn = false;      // estado de gravação: não é ponto de partida

    for (auto& c : copy.channels)
        c.armed = false;

    for (auto& m : copy.mixer)
        m.armed = false;

    if (options.clearNotes)
        clearNotes (copy);

    wnl::normalise (copy);

    const auto root = folder();
    const auto target = root.getChildFile (name);
    const bool replacing = target.exists();

    // Trocando um template que existe, o novo é gravado ao lado e só então o antigo vai para os apagados: se a
    // gravação falhar, o antigo fica onde está. O projeto aberto pode ter nascido deste template e ainda apontar
    // para os samples dele; a pasta nova entra no mesmo lugar, com os mesmos nomes de arquivo.
    const auto writeTo = replacing ? root.getChildFile (name + ".salvando") : target;

    if (replacing)
        writeTo.deleteRecursively();

    if (const auto result = wnl::save (copy, writeTo.getChildFile (name + ".wnl"), true, projectFolder); result.failed())
    {
        writeTo.deleteRecursively();   // nada pela metade na lista
        return result;
    }

    if (replacing)
    {
        if (const auto moved = moveToDeleted (target); moved.failed())
        {
            writeTo.deleteRecursively();
            return moved;
        }

        if (! writeTo.moveFileTo (target))
            return juce::Result::fail ("O template novo ficou em "_t + writeTo.getFullPathName()
                                       + " e não consegui pôr no lugar do antigo (que está na pasta apagados)."_t);
    }

    if (savedName != nullptr)
        *savedName = name;

    return juce::Result::ok();
}

juce::Result remove (const juce::String& requested)
{
    const auto name = requested.trim();

    if (core::findFactory (name.toStdString()) != nullptr)
        return juce::Result::fail ("Os templates de fábrica não se apagam."_t);

    const auto file = fileFor (name);

    if (file == juce::File() || ! file.existsAsFile() || core::isReservedName (file.getFileNameWithoutExtension().toStdString()))
        return juce::Result::fail ("Template não encontrado: "_t + name);

    const bool wasDefault = defaultName().equalsIgnoreCase (file.getFileNameWithoutExtension());

    if (const auto moved = moveToDeleted (file.getParentDirectory()); moved.failed())
        return moved;

    if (wasDefault)
        setDefault ({});   // o "novo projeto" volta ao de fábrica

    return juce::Result::ok();
}

juce::Result loadForNew (const juce::String& name, Project& out)
{
    const auto file = fileFor (name);

    if (file == juce::File() || ! file.existsAsFile())
        return juce::Result::fail ("Template não encontrado: "_t + name.trim() + " (veja o template.list)."_t);

    Project loaded;

    if (const auto result = wnl::load (file, loaded); result.failed())
        return result;

    // O projeto novo não tem pasta: o que era relativo à pasta do template passa a apontar para ela. Salvar o
    // projeto em outro lugar copia esses arquivos para a samples/ dele (wnl::save); o template fica como está.
    const auto templateFolder = file.getParentDirectory();

    for (auto& c : loaded.channels)
    {
        for (auto& layer : c.layers)
            layer.samplePath = absolutePath (layer.samplePath, templateFolder);

        c.samplePath = absolutePath (c.samplePath, templateFolder);

        // o sample sumiu da pasta do template: vale o lugar de onde ele veio, se ainda estiver lá
        if (c.samplePath.isNotEmpty() && ! juce::File (c.samplePath).existsAsFile()
            && juce::File::isAbsolutePath (c.originalPath) && juce::File (c.originalPath).existsAsFile())
            c.samplePath = c.originalPath;
    }

    for (auto& item : loaded.playlistItems)
        item.file = absolutePath (item.file, templateFolder);

    loaded.name = Project().name;   // "Sem título": o salvar troca pelo nome do arquivo
    wnl::normalise (loaded);
    out = std::move (loaded);
    return juce::Result::ok();
}

//==============================================================================
juce::Result request (const juce::String& requested)
{
    pending = {};
    const auto name = requested.trim();

    if (name.isEmpty())
        return juce::Result::fail ("Diga o nome do template (veja o template.list)."_t);

    if (const auto* factory = core::findFactory (name.toStdString()))
    {
        pending.name = fromStd (factory->name);
    }
    else
    {
        Project loaded;

        if (const auto result = loadForNew (name, loaded); result.failed())
            return result;

        pending.name = fileFor (name).getFileNameWithoutExtension();
        pending.project = std::move (loaded);
    }

    pending.active = true;
    return juce::Result::ok();
}

bool endRequest()
{
    const bool served = pending.active && pending.served;
    pending = {};
    return served;
}

bool beginNewProject (Project& fresh)
{
    recipe = nullptr;

    // o template pedido pelo template.new; sem pedido, o padrão do usuário
    juce::String name;
    std::optional<Project> project;

    if (pending.active)
    {
        pending.served = true;
        name = pending.name;
        project = std::move (pending.project);
        pending.project.reset();
    }
    else
    {
        name = defaultName();

        if (name.isEmpty())
            return false;
    }

    if (const auto* factory = core::findFactory (name.toStdString()))
    {
        if (factory->kit)
        {
            recipe = factory;   // o kit entra como sempre; a receita fica para o gancho 2
            return false;
        }

        fresh = Project();      // "Vazio": o newProject normaliza (master, um padrão, as raias)
        return true;
    }

    if (! project.has_value())
    {
        // o padrão do usuário: se o arquivo não abre, o app não fica sem "novo projeto"
        Project loaded;

        if (const auto result = loadForNew (name, loaded); result.failed())
        {
            juce::Logger::writeToLog ("Template padrão \""_t + name + "\" não abriu ("_t + result.getErrorMessage()
                                      + "): novo projeto de fábrica."_t);
            return false;
        }

        project = std::move (loaded);
    }

    fresh = std::move (*project);
    return true;
}

void finishNewProject (CommandRegistry& registry, const ProjectModel& model)
{
    const auto* r = std::exchange (recipe, nullptr);

    if (r == nullptr)
        return;

    // Cada passo é um comando do app (os mesmos da tela e da IA); o que falhar fica no log e o resto segue.
    auto run = [&registry, r] (const char* id, const juce::var& params)
    {
        if (const auto result = registry.run (id, params); ! result.ok)
            juce::Logger::writeToLog ("Template \""_t + juce::String::fromUTF8 (r->name) + "\": "_t + id + " falhou: " + result.error);
    };

    if (const auto* genre = ideas::findGenre (r->genre))
    {
        run ("transport.setBpm", args ({ { "bpm", genre->bpmDefault } }));
        run ("project.setKey", args ({ { "root", model.get().keyRoot }, { "scale", genre->preferMinor ? "minor" : "major" } }));
    }

    if (r->glideMs > 0.0f)
        if (const auto bass = channelNamed (model.get(), "808"); bass.isNotEmpty())
            run ("channel.setGlide", args ({ { "channel", bass }, { "ms", r->glideMs } }));

    if (r->chokeHats)
        for (auto* hat : { "Hat", "Open hat" })
            if (const auto id = channelNamed (model.get(), hat); id.isNotEmpty())
                run ("channel.setChoke", args ({ { "channel", id }, { "group", 1 } }));

    if (juce::String (r->style).isNotEmpty())
        run ("mixer.applyStyle", args ({ { "style", r->style } }));

    for (int i = 0; i < (int) r->lanes.size(); ++i)
        if (const auto lane = juce::String::fromUTF8 (r->lanes[(size_t) i]); lane.isNotEmpty())
            run ("playlist.renameTrack", args ({ { "track", i + 1 }, { "name", lane } }));
}

} // namespace whynot::templates
