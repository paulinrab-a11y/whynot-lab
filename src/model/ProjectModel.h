#pragma once

#include "Project.h"

namespace whynot
{

/** Dono do projeto aberto. Só a thread de mensagem mexe nele.

    Os comandos alteram data() e depois chamam changed(); quem escuta (UI e
    projetor) é avisado de forma síncrona.
*/
class ProjectModel
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void projectChanged (const ProjectModel&) = 0;
    };

    Project& data() noexcept                 { return project; }
    const Project& get() const noexcept      { return project; }

    /** Troca o projeto inteiro (abrir arquivo, undo, redo). */
    void replace (Project newProject)
    {
        project = std::move (newProject);
        changed();
    }

    void changed()
    {
        dirty = true;
        listeners.call ([this] (Listener& l) { l.projectChanged (*this); });
    }

    void addListener (Listener* l)       { listeners.add (l); }
    void removeListener (Listener* l)    { listeners.remove (l); }

    /** Arquivo .wnl atual (vazio se nunca foi salvo). */
    juce::File file;
    bool dirty = false;

    juce::File projectFolder() const     { return file == juce::File() ? juce::File() : file.getParentDirectory(); }

    /** Caminho absoluto de um sample, resolvendo os relativos pela pasta do projeto. */
    juce::File resolveSample (const juce::String& path) const
    {
        if (path.isEmpty())
            return {};

        if (juce::File::isAbsolutePath (path))
            return juce::File (path);

        return projectFolder().getChildFile (path);
    }

private:
    Project project;
    juce::ListenerList<Listener> listeners;
};

} // namespace whynot
