#pragma once

#include "Project.h"
#include "ProjectChange.h"

namespace whynot
{

/** Dono do projeto aberto. Só a thread de mensagem mexe nele.

    Os comandos alteram data() e depois chamam changed(); quem escuta (UI e
    projetor) é avisado de forma síncrona.

    O changed() aceita uma máscara do que mudou (change::patterns, change::mixer...). Sem máscara vale
    change::all, o comportamento de sempre. Quem escuta e quer a máscara sobrescreve o
    projectChangedMasked; quem não quer continua só com o projectChanged.
*/
class ProjectModel
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void projectChanged (const ProjectModel&) = 0;

        /** O mesmo aviso, com o que mudou. O padrão ignora a máscara e chama o projectChanged. */
        virtual void projectChangedMasked (const ProjectModel& model, change::Mask)   { projectChanged (model); }
    };

    Project& data() noexcept                 { return project; }
    const Project& get() const noexcept      { return project; }

    /** Troca o projeto inteiro (abrir arquivo, undo, redo). */
    void replace (Project newProject)
    {
        project = std::move (newProject);
        changed();
    }

    /** Avisa quem escuta. `what` diz o que mudou; sem ele (ou com 0), tudo. */
    void changed (change::Mask what = change::all)
    {
        dirty = true;
        const auto mask = change::normalised (what);
        listeners.call ([this, mask] (Listener& l) { l.projectChangedMasked (*this, mask); });
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
