#include "ProjectImport.h"

#include "../model/WnlFormat.h"

#include <map>
#include <set>

namespace whynot::library
{

namespace
{
    bool stepsOn (const std::vector<Step>& lane)
    {
        for (auto& s : lane)
            if (s.on)
                return true;

        return false;
    }

    /** Os canais que tocam no padrão, na ordem do projeto. */
    std::vector<juce::String> channelsIn (const Project& p, const Pattern& pattern)
    {
        std::vector<juce::String> out;

        for (auto& ch : p.channels)
        {
            const auto steps = pattern.steps.find (ch.id);
            const bool hasSteps = steps != pattern.steps.end() && stepsOn (steps->second);
            const bool hasNotes = pattern.notesFor (ch.id) != nullptr;

            if (hasSteps || hasNotes)
                out.push_back (ch.id);
        }

        return out;
    }

    const Pattern* findPattern (const Project& p, const juce::String& idOrName)
    {
        if (auto* byId = p.findPattern (idOrName))
            return byId;

        for (auto& pattern : p.patterns)
            if (pattern.name.equalsIgnoreCase (idOrName.trim()))
                return &pattern;

        return nullptr;
    }

    /** O nome, ou o nome " (de X)" se ele já existe (e um número, se até esse existir). */
    template <typename Container>
    juce::String freeName (const Container& existing, const juce::String& name, const juce::String& source)
    {
        auto taken = [&existing] (const juce::String& n)
        {
            for (auto& x : existing)
                if (x.name.equalsIgnoreCase (n))
                    return true;

            return false;
        };

        if (! taken (name))
            return name;

        auto candidate = name + " (de "_t + source + ")";

        for (int n = 2; taken (candidate); ++n)
            candidate = name + " (de "_t + source + " " + juce::String (n) + ")";

        return candidate;
    }

    void insertBeforeMaster (Project& p, MixerTrack t)
    {
        auto it = std::find_if (p.mixer.begin(), p.mixer.end(), [] (const MixerTrack& m) { return m.isMaster(); });
        p.mixer.insert (it, std::move (t));
    }
}

int shortestTranspose (int fromRoot, int toRoot) noexcept
{
    int d = ((toRoot - fromRoot) % 12 + 12) % 12;   // 0..11
    return d > 5 ? d - 12 : d;                      // -6..5
}

juce::Result readImportSource (const juce::File& wnl, ImportSource& out)
{
    out = {};
    out.file = wnl;
    const auto loaded = wnl::load (wnl, out.project);

    if (loaded.failed())
        return loaded;

    for (auto& pattern : out.project.patterns)
    {
        ImportSource::PatternInfo info;
        info.id = pattern.id;
        info.name = pattern.name;

        for (auto& id : channelsIn (out.project, pattern))
        {
            if (const auto* ch = out.project.findChannel (id))
                info.channels.add (ch->name);

            if (auto it = pattern.steps.find (id); it != pattern.steps.end())
                for (auto& s : it->second)
                    info.steps += s.on ? 1 : 0;

            if (const auto* notes = pattern.notesFor (id))
                info.notes += (int) notes->size();
        }

        if (! info.channels.isEmpty())
            out.patterns.push_back (info);
    }

    return juce::Result::ok();
}

juce::Result importInto (Project& dest, const ImportSource& source, const ImportOptions& options, ImportReport& report)
{
    report = {};
    const auto& src = source.project;
    const auto from = source.file.getFileNameWithoutExtension();
    const auto srcFolder = source.file.getParentDirectory();

    // os padrões pedidos (por id ou nome), sem repetir
    std::vector<const Pattern*> chosen;

    for (auto& wanted : options.patterns)
    {
        const auto* pattern = findPattern (src, wanted);

        if (pattern == nullptr)
            return juce::Result::fail ("Padrão não encontrado em "_t + from + ": " + wanted);

        if (std::find (chosen.begin(), chosen.end(), pattern) == chosen.end())
            chosen.push_back (pattern);
    }

    if (chosen.empty())
        return juce::Result::fail ("Escolha pelo menos um padrão para trazer."_t);

    // os canais que eles usam, na ordem do outro projeto
    std::set<juce::String> used;

    for (auto* pattern : chosen)
        for (auto& id : channelsIn (src, *pattern))
            used.insert (id);

    report.semitones = options.transpose ? shortestTranspose (src.keyRoot, dest.keyRoot) : 0;

    std::map<juce::String, juce::String> channelIds, trackIds;   // id no outro projeto → id aqui

    for (auto& srcChannel : src.channels)
    {
        if (used.count (srcChannel.id) == 0 || srcChannel.isAudio())
            continue;

        Channel ch = srcChannel;
        ch.id = makeId ("ch");
        ch.name = freeName (dest.channels, srcChannel.name, from);
        ch.armed = false;

        // o sample com o caminho absoluto do outro projeto: o próximo salvar copia para a pasta deste
        if (ch.samplePath.isNotEmpty() && ! juce::File::isAbsolutePath (ch.samplePath))
            ch.samplePath = srcFolder.getChildFile (ch.samplePath).getFullPathName().replaceCharacter ('\\', '/');

        for (auto& l : ch.layers)   // as camadas por força (bloco 4f), como o sample
            if (l.samplePath.isNotEmpty() && ! juce::File::isAbsolutePath (l.samplePath))
                l.samplePath = srcFolder.getChildFile (l.samplePath).getFullPathName().replaceCharacter ('\\', '/');

        // a trilha de mixer do canal: uma cópia (a mesma para os canais que dividiam a trilha)
        const auto* srcTrack = src.findMixer (srcChannel.output);

        if (srcTrack != nullptr && ! srcTrack->isMaster() && trackIds.count (srcTrack->id) > 0)
        {
            ch.output = trackIds[srcTrack->id];
        }
        else
        {
            MixerTrack mx;

            if (srcTrack != nullptr && ! srcTrack->isMaster())
            {
                mx = *srcTrack;

                if (! options.mixer)
                {
                    mx.inserts.clear();   // só o volume e o pan
                    mx.sends.clear();
                }
            }
            else
            {
                mx.volumeDb = -6.0f;   // o padrão de um canal novo
            }

            mx.id = makeId ("mx");
            mx.name = freeName (dest.mixer, srcTrack != nullptr && ! srcTrack->isMaster() ? srcTrack->name : ch.name, from);
            mx.colour = srcTrack != nullptr && ! srcTrack->isMaster() ? srcTrack->colour : ch.colour;
            mx.autoFor = srcTrack == nullptr || srcTrack->isMaster() || srcTrack->autoFor == srcChannel.id ? ch.id : juce::String();
            mx.output = {};   // por ora o master; as saídas entre trilhas trazidas voltam abaixo
            mx.solo = mx.armed = mx.frozen = false;

            for (auto& ins : mx.inserts)
                ins.id = makeId ("fx");

            if (srcTrack != nullptr && ! srcTrack->isMaster())
                trackIds[srcTrack->id] = mx.id;

            ch.output = mx.id;
            report.tracks.add (mx.id);
            insertBeforeMaster (dest, mx);
        }

        channelIds[srcChannel.id] = ch.id;
        report.channels.add (ch.id);
        dest.channels.push_back (std::move (ch));
    }

    // entre as trilhas trazidas, a saída, os envios e o sidechain apontam para as cópias; o resto sai
    for (auto& [srcId, newId] : trackIds)
    {
        const auto* srcTrack = src.findMixer (srcId);
        auto* mx = dest.findMixer (newId);

        if (srcTrack == nullptr || mx == nullptr)
            continue;

        if (auto it = trackIds.find (srcTrack->output); it != trackIds.end())
            mx->output = it->second;

        std::vector<Send> sends;

        for (auto& s : mx->sends)
            if (auto it = trackIds.find (s.to); it != trackIds.end())
                sends.push_back ({ it->second, s.levelDb });

        mx->sends = sends;

        for (auto& ins : mx->inserts)
        {
            if (auto it = trackIds.find (ins.sidechain); it != trackIds.end())
                ins.sidechain = it->second;
            else
                ins.sidechain = {};
        }
    }

    // os padrões: só as raias dos canais trazidos, com os ids novos (e as notas no tom daqui, se pedido)
    for (auto* srcPattern : chosen)
    {
        Pattern p;
        p.id = makeId ("pt");
        p.name = freeName (dest.patterns, srcPattern->name, from);
        p.lengthSteps = srcPattern->lengthSteps;

        for (auto& [srcId, newId] : channelIds)
        {
            if (auto it = srcPattern->steps.find (srcId); it != srcPattern->steps.end())
                p.steps[newId] = it->second;

            if (const auto* notes = srcPattern->notesFor (srcId))
            {
                auto copy = *notes;

                for (auto& n : copy)
                    n.key = juce::jlimit (0, 127, n.key + report.semitones);

                p.notes[newId] = copy;
            }
        }

        report.patterns.add (p.id);
        dest.patterns.push_back (std::move (p));
    }

    wnl::normalise (dest);   // raias vazias dos canais novos nos padrões daqui (e dos daqui nos trazidos)
    return juce::Result::ok();
}

} // namespace whynot::library
