#include "WnlFormat.h"
#include "../automation/AutomationTarget.h"
#include "../automation/Macros.h"
#include "../common/FolderHints.h"
#include "../common/SafeJson.h"
#include "../dsp/FxCatalog.h"
#include "../audio/PianoPresets.h"
#include "../synth/SynthParams.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <set>

namespace whynot::wnl
{

namespace
{
    const char* const formatName = "whynot-lab-project";

    juce::var obj()  { return juce::var (new juce::DynamicObject()); }

    void set (juce::var& o, const juce::Identifier& key, const juce::var& value)
    {
        o.getDynamicObject()->setProperty (key, value);
    }

    juce::var roundTo (double v, int decimals)
    {
        const double scale = std::pow (10.0, decimals);
        return std::round (v * scale) / scale;
    }

    // Leitura tolerante: valor ausente ou de tipo errado vira o padrão.
    juce::String getString (const juce::var& o, const char* key, const juce::String& fallback = {})
    {
        const auto& v = o[key];
        return v.isString() ? v.toString() : fallback;
    }

    double getDouble (const juce::var& o, const char* key, double fallback)
    {
        const auto& v = o[key];
        return (v.isDouble() || v.isInt() || v.isInt64()) ? (double) v : fallback;
    }

    int getInt (const juce::var& o, const char* key, int fallback)
    {
        // número fora do int (arquivo corrompido) é limitado, em vez de virar comportamento indefinido
        const auto& v = o[key];

        if (v.isInt())
            return (int) v;

        if (v.isInt64())
            return (int) juce::jlimit ((juce::int64) std::numeric_limits<int>::min(), (juce::int64) std::numeric_limits<int>::max(), (juce::int64) v);

        if (v.isDouble())
        {
            const double d = v;
            return std::isfinite (d) ? (int) juce::jlimit ((double) std::numeric_limits<int>::min(), (double) std::numeric_limits<int>::max(), d)
                                     : fallback;
        }

        return fallback;
    }

    bool getBool (const juce::var& o, const char* key, bool fallback)
    {
        const auto& v = o[key];
        return v.isBool() ? (bool) v : fallback;
    }

    int sanitiseRoll (int roll)
    {
        for (int allowed : { 1, 2, 3, 4, 6, 8 })
            if (roll == allowed)
                return roll;

        return 1;
    }

    const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    int rootFromName (const juce::String& name)
    {
        for (int i = 0; i < 12; ++i)
            if (name.equalsIgnoreCase (noteNames[i]))
                return i;

        return 0;
    }

    juce::String relativeTo (const juce::File& f, const juce::File& folder)
    {
        return f.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
    }

    juce::var pluginToJson (const PluginRef& r)
    {
        auto o = obj();
        set (o, "name", r.name);
        set (o, "manufacturer", r.manufacturer);
        set (o, "format", r.format);
        set (o, "file", r.file);
        set (o, "uid", r.uid);
        set (o, "instrument", r.instrument);
        set (o, "state", r.state);
        return o;
    }

    PluginRef pluginFromJson (const juce::var& o)
    {
        PluginRef r;
        r.name = getString (o, "name");
        r.manufacturer = getString (o, "manufacturer");
        r.format = getString (o, "format", r.format);
        r.file = getString (o, "file");
        r.uid = getString (o, "uid");
        r.instrument = getBool (o, "instrument", false);
        r.state = getString (o, "state");
        return r;
    }
}

//==============================================================================
static void normaliseMixing (Project& p)
{
    std::set<juce::String> insertIds;

    for (auto& m : p.mixer)
    {
        if (m.role.isNotEmpty() && dsp::findRole (m.role.toStdString()) == nullptr)
            m.role = {};

        if ((int) m.inserts.size() > maxInserts)
            m.inserts.resize ((size_t) maxInserts);

        for (auto& ins : m.inserts)
        {
            while (ins.id.isEmpty() || insertIds.count (ins.id) > 0)
                ins.id = makeId ("fx");

            insertIds.insert (ins.id);

            if (ins.kind.isEmpty())
                ins.kind = "whynot";

            if (ins.kind == "whynot")
            {
                // Tipo desconhecido (arquivo de uma versão mais nova) fica guardado, mas não toca.
                if (auto* info = dsp::findEffect (ins.type.toStdString()))
                {
                    std::map<juce::String, float> clean;

                    for (auto& param : info->params)
                        if (! param.hidden)
                            if (auto it = ins.params.find (param.id); it != ins.params.end())
                                clean[param.id] = param.clamp (it->second);

                    ins.params = std::move (clean);
                }
            }

            if (ins.sidechain == m.id || ins.sidechain == masterTrackId || p.findMixer (ins.sidechain) == nullptr)
                ins.sidechain = {};
        }

        macros::tidy (m);   // o rack (bloco 4c): só links para efeitos nativos que ficaram

        std::vector<Send> sends;

        for (auto& s : m.sends)
        {
            const bool valid = ! m.isMaster() && s.to != m.id && s.to != masterTrackId && p.findMixer (s.to) != nullptr
                               && std::none_of (sends.begin(), sends.end(), [&s] (const Send& x) { return x.to == s.to; });

            if (valid)
                sends.push_back ({ s.to, juce::jlimit (-100.0f, 6.0f, s.levelDb), s.preFader });
        }

        m.sends = std::move (sends);
    }

    // Nada pode voltar para a própria trilha: envios e sidechains que fecham um laço saem.
    for (bool changed = true; changed;)
    {
        changed = false;

        for (auto& m : p.mixer)
        {
            for (auto it = m.sends.begin(); it != m.sends.end(); ++it)
            {
                if (p.reaches (it->to, m.id))
                {
                    m.sends.erase (it);
                    changed = true;
                    break;
                }
            }

            for (auto& ins : m.inserts)
            {
                if (ins.sidechain.isNotEmpty() && p.reaches (m.id, ins.sidechain))
                {
                    ins.sidechain = {};
                    changed = true;
                }
            }
        }
    }
}

void normalise (Project& p)
{
    if (p.findMixer (masterTrackId) == nullptr)
    {
        MixerTrack master;
        master.id = masterTrackId;
        master.name = "Master";
        master.colour = "#FFFFFF";
        p.mixer.push_back (master);
    }

    for (auto& m : p.mixer)
    {
        if (m.isMaster())
            m.output = {};
        else if (m.output == m.id || (m.output.isNotEmpty() && p.findMixer (m.output) == nullptr))
            m.output = masterTrackId;
    }

    for (auto& c : p.channels)
        if (p.findMixer (c.output) == nullptr)
            c.output = masterTrackId;

    normaliseMixing (p);

    if (p.patterns.empty())
    {
        Pattern pat;
        pat.id = makeId ("pt");
        pat.name = "Padrão 1"_t;
        p.patterns.push_back (pat);
    }

    for (auto& pat : p.patterns)
    {
        pat.lengthSteps = juce::jlimit (1, 256, pat.lengthSteps);

        // remove raias de canais que não existem mais
        for (auto it = pat.steps.begin(); it != pat.steps.end();)
            it = p.findChannel (it->first) == nullptr ? pat.steps.erase (it) : std::next (it);

        for (auto& c : p.channels)
            pat.steps[c.id].resize ((size_t) pat.lengthSteps);

        // notas: só de canais que existem, valores válidos, ordenadas, ids únicos na raia
        for (auto it = pat.notes.begin(); it != pat.notes.end();)
        {
            if (p.findChannel (it->first) == nullptr || it->second.empty())
            {
                it = pat.notes.erase (it);
                continue;
            }

            auto& notes = it->second;
            int maxId = 0;

            for (auto& n : notes)
            {
                n.start = juce::jlimit (0, maxTimelineTicks, n.start);
                n.length = juce::jlimit (1, 256 * ticksPerStep, n.length);
                n.key = juce::jlimit (0, 127, n.key);
                n.velocity = juce::jlimit (1, 127, n.velocity);
                maxId = juce::jmax (maxId, n.id);
            }

            std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                              { return a.start != b.start ? a.start < b.start : a.key < b.key; });

            std::vector<int> seen;

            for (auto& n : notes)
            {
                if (n.id <= 0 || std::find (seen.begin(), seen.end(), n.id) != seen.end())
                    n.id = ++maxId;

                seen.push_back (n.id);
            }

            ++it;
        }
    }

    if (p.currentPatternPtr() == nullptr)
        p.currentPattern = p.patterns.front().id;

    p.keyRoot = ((p.keyRoot % 12) + 12) % 12;

    if (! Project::scaleNames().contains (p.scale))
        p.scale = "minor";

    for (auto& c : p.channels)
    {
        if (c.kind != "audio" && c.kind != "plugin" && c.kind != "synth")
            c.kind = "sampler";

        // WHYNOT Synth: só parâmetros conhecidos, dentro da faixa, com 4 casas (o que o arquivo guarda).
        if (c.isSynth())
        {
            std::map<juce::String, float> clean;

            for (auto& [id, value] : c.synthParams)
                if (const int i = synth::indexOf (id.toStdString()); i >= 0 && ! synth::param (i).hidden && std::isfinite (value))
                    clean[id] = (float) (std::round (synth::param (i).clamp (value) * 10000.0) / 10000.0);

            c.synthParams = std::move (clean);
        }
        else
        {
            c.synthParams.clear();
            c.synthPreset = {};
        }

        // WHYNOT Piano: só no sampler, com um preset conhecido
        if (c.kind != "sampler" || c.instrument != "piano")
        {
            c.instrument = {};
            c.instrumentPreset = {};
        }
        else if (audio::findPianoPreset (c.instrumentPreset.toRawUTF8()) == nullptr)
        {
            c.instrumentPreset = "grand";
        }

        if (c.isPlugin() && c.plugin.isEmpty())   // instrumento sem referência não tem o que abrir
            c.kind = "sampler";

        c.glideMs = juce::jlimit (0.0f, 2000.0f, c.glideMs);
        c.chokeGroup = juce::jlimit (0, 8, c.chokeGroup);
        c.swing = std::isfinite (c.swing) ? juce::jlimit (0.0f, 100.0f, c.swing) : 0.0f;

        auto fix = [] (float& v, float fallback, float lo, float hi) { v = std::isfinite (v) ? juce::jlimit (lo, hi, v) : fallback; };
        const Channel defaults;
        fix (c.rootCents,   defaults.rootCents,   -50.0f, 50.0f);
        fix (c.tune,        defaults.tune,        -24.0f, 24.0f);
        fix (c.attackMs,    defaults.attackMs,    0.0f, 1000.0f);
        fix (c.releaseMs,   defaults.releaseMs,   1.0f, 5000.0f);
        fix (c.decayMs,     defaults.decayMs,     1.0f, 20000.0f);
        fix (c.sustain,     defaults.sustain,     0.0f, 1.0f);
        fix (c.pitchDrop,   defaults.pitchDrop,   -48.0f, 48.0f);
        fix (c.pitchDropMs, defaults.pitchDropMs, 1.0f, 5000.0f);
        fix (c.start,       defaults.start,       0.0f, 0.99f);
        fix (c.loopStart,   defaults.loopStart,   0.0f, 1.0f);
        fix (c.loopEnd,     defaults.loopEnd,     0.0f, 1.0f);
        fix (c.loopXfadeMs, defaults.loopXfadeMs, 0.0f, 500.0f);

        if (c.loopEnd < c.loopStart)
            std::swap (c.loopStart, c.loopEnd);

        // Fatias: em ordem, dentro do sample, sem repetidas, a primeira no começo, até 128 teclas.
        if (c.kind != "sampler" || c.instrument.isNotEmpty())
            c.slices.clear();

        std::erase_if (c.slices, [] (float v) { return ! std::isfinite (v) || v < 0.0f || v >= 1.0f; });
        std::sort (c.slices.begin(), c.slices.end());
        c.slices.erase (std::unique (c.slices.begin(), c.slices.end()), c.slices.end());

        if (! c.slices.empty() && c.slices.front() != 0.0f)
            c.slices.insert (c.slices.begin(), 0.0f);

        if (c.slices.size() > 128)
            c.slices.resize (128);

        c.sliceKey = juce::jlimit (0, 128 - juce::jmax (1, (int) c.slices.size()), c.sliceKey);
        c.sliceBpm = std::isfinite (c.sliceBpm) && c.sliceBpm >= 20.0f ? juce::jmin (c.sliceBpm, 999.0f) : 0.0f;

        // Camadas por força (bloco 4f): só no sampler sem fatias (fatiado, cada tecla é uma fatia: fatiar tira as camadas,
        // e o desfazer devolve); com caminho, a força de 2 a 127, em ordem, uma por força
        if (c.kind != "sampler" || c.instrument.isNotEmpty() || ! c.slices.empty())
            c.layers.clear();

        std::erase_if (c.layers, [] (const SampleLayer& l) { return l.samplePath.trim().isEmpty(); });

        for (auto& l : c.layers)
            l.minVelocity = juce::jlimit (2, 127, l.minVelocity);

        std::stable_sort (c.layers.begin(), c.layers.end(), [] (const SampleLayer& a, const SampleLayer& b) { return a.minVelocity < b.minVelocity; });
        c.layers.erase (std::unique (c.layers.begin(), c.layers.end(),
                                     [] (const SampleLayer& a, const SampleLayer& b) { return a.minVelocity == b.minVelocity; }),
                        c.layers.end());

        if ((int) c.layers.size() > maxSampleLayers)
            c.layers.resize ((size_t) maxSampleLayers);

        if (c.isAudio())
            c.armed = false;   // gravação: o canal de áudio não recebe notas; o áudio arma a trilha do mixer
    }

    // Gravação: uma trilha armada por vez (a entrada de áudio é uma só), nunca o master; punch com tamanho.
    bool trackArmed = false;

    for (auto& m : p.mixer)
    {
        m.armed = m.armed && ! m.isMaster() && ! trackArmed;
        trackArmed = trackArmed || m.armed;
        m.frozen = m.frozen && ! m.isMaster();   // congelar (3.5): o master não congela

        if (m.isMaster() || (m.monitor != "off" && m.monitor != "on"))
            m.monitor = {};   // monitoração (gravar voz): só "off" e "on" saem no arquivo; o resto é o padrão
    }

    p.punchIn = juce::jlimit (0, maxTimelineTicks, p.punchIn);
    p.punchOut = juce::jlimit (p.punchIn + ticksPerStep, maxTimelineTicks + ticksPerStep, p.punchOut);

    // playlist: pelo menos 12 raias, com ids fixos e legíveis
    for (int i = (int) p.playlistTracks.size(); i < 12; ++i)
    {
        PlaylistTrack t;
        t.id = "pl_" + juce::String (i + 1).paddedLeft ('0', 2);

        while (p.findPlaylistTrack (t.id) != nullptr)
            t.id += "b";

        t.name = "Raia " + juce::String (i + 1);   // em português, como as raias que a gravação cria
        p.playlistTracks.push_back (t);
    }

    // gravar voz: a raia só grava por uma trilha do mixer que existe (o master não)
    for (auto& t : p.playlistTracks)
        if (t.recordTrack == masterTrackId || p.findMixer (t.recordTrack) == nullptr)
            t.recordTrack = {};

    // a cor da raia (bloco 4): só "#RRGGBB"; o resto vira sem cor
    for (auto& t : p.playlistTracks)
        if (t.colour.isNotEmpty() && ! (t.colour.length() == 7 && t.colour.startsWithChar ('#')
                                        && t.colour.substring (1).containsOnly ("0123456789abcdefABCDEF")))
            t.colour = {};

    std::erase_if (p.playlistItems, [&p] (const PlaylistItem& i)
    {
        const bool refOk = i.kind == "audio" ? (p.findChannel (i.ref) != nullptr && p.findChannel (i.ref)->isAudio())
                                             : p.findPattern (i.ref) != nullptr;
        return ! refOk || p.findPlaylistTrack (i.track) == nullptr;
    });

    for (auto& i : p.playlistItems)
    {
        if (i.kind != "audio")
            i.kind = "pattern";

        i.start = juce::jlimit (0, maxTimelineTicks, i.start);
        i.length = juce::jlimit (ticksPerStep, maxTimelineTicks, i.length);
        i.offsetSeconds = std::isfinite (i.offsetSeconds) ? roundToMicroseconds (juce::jmax (0.0, i.offsetSeconds)) : 0.0;
        i.patternOffset = i.kind == "pattern" ? juce::jlimit (0, maxTimelineTicks, i.patternOffset) : 0;
        i.stretch = i.kind == "audio" ? clampStretch (i.stretch) : 1.0;
        i.tape = i.kind == "audio" && i.tape;

        // ganho e fades do bloco (gravar voz): só no áudio, e os fades cabem no bloco
        const double blockSeconds = ticksToSeconds (i.length, p.bpm);
        i.gainDb = i.kind == "audio" ? clampClipGain (i.gainDb) : 0.0f;
        i.fadeIn = i.kind == "audio" && std::isfinite (i.fadeIn) ? roundToMicroseconds (juce::jlimit (0.0, blockSeconds, i.fadeIn)) : 0.0;
        i.fadeOut = i.kind == "audio" && std::isfinite (i.fadeOut) ? roundToMicroseconds (juce::jlimit (0.0, juce::jmax (0.0, blockSeconds - i.fadeIn), i.fadeOut)) : 0.0;

        if (i.kind != "audio" || i.takeGroup.isEmpty())
        {
            i.takeGroup = {};   // takes (gravar voz): só áudio, e o número só com o grupo
            i.take = 0;
        }
        else
        {
            i.take = juce::jlimit (1, 999, i.take);
        }
    }

    // Marcadores (bloco 1, 22/10): dentro da linha do tempo, com nome (até maxMarkerName letras), ids únicos,
    // ordenados por t. Um marcador com t inválido sai.
    std::erase_if (p.markers, [] (const Marker& m) { return m.t < 0 || m.t > maxTimelineTicks; });
    std::stable_sort (p.markers.begin(), p.markers.end(), [] (const Marker& a, const Marker& b) { return a.t < b.t; });
    std::set<juce::String> markerIds;

    for (auto& m : p.markers)
    {
        m.name = m.name.trim().substring (0, maxMarkerName).trim();

        if (m.name.isEmpty())
            m.name = "Marcador";

        while (m.id.isEmpty() || markerIds.count (m.id) > 0)
            m.id = makeId ("mk");

        markerIds.insert (m.id);
    }

    // Automação (3.5): uma curva por parâmetro, só de alvos que existem, com os pontos limpos (t entre 0
    // e o fim da linha do tempo, v entre 0 e 1, ordenados, um por t: o último vence). A curva de uma
    // trilha ou efeito que saiu vai junto, com aviso no log (o desfazer devolve os dois).
    std::vector<AutomationLane> lanes;
    std::set<juce::String> laneIds;

    for (auto& lane : p.automation)
    {
        const auto target = automation::tidy (lane.target);

        if (! automation::rangeFor (p, target).has_value())
        {
            juce::Logger::writeToLog ("Automação: curva "_t + lane.id + " sai, o alvo não existe mais ("_t + target.key() + ")");
            continue;
        }

        if (std::any_of (lanes.begin(), lanes.end(), [&] (const AutomationLane& l) { return l.target.key() == target.key(); }))
            continue;

        std::vector<automation::Point> points;

        for (auto& pt : lane.points)
            points.push_back ({ pt.t, pt.v });

        AutomationLane clean;
        clean.id = lane.id;
        clean.target = target;

        // v com 4 casas, as mesmas do arquivo: o projeto reaberto soa igual ao que foi salvo
        for (auto& pt : automation::cleanPoints (points, maxTimelineTicks))
            clean.points.push_back ({ pt.t, std::round (pt.v * 10000.0f) / 10000.0f });

        while (clean.id.isEmpty() || laneIds.count (clean.id) > 0)
            clean.id = makeId ("au");

        laneIds.insert (clean.id);
        lanes.push_back (std::move (clean));
    }

    p.automation = std::move (lanes);

    // MIDI learn (bloco 4d): só alvos que existem, o CC de 0 a 127 e o canal de 0 a 16, um alvo por CC e um CC por
    // alvo (o último vence). Tirar a trilha ou o efeito tira o link (o desfazer devolve os dois).
    std::vector<MidiLink> links;

    for (auto it = p.midiMap.rbegin(); it != p.midiMap.rend(); ++it)
    {
        auto link = *it;
        const auto* m = p.findMixer (link.track);
        bool valid = m != nullptr && link.cc >= 0 && link.cc <= 127 && link.channel >= 0 && link.channel <= 16;

        if (valid && link.kind == "mixer")
        {
            link.insert = {};
            valid = link.param == "volume" || link.param == "pan";
        }
        else if (valid && link.kind == "insert")
        {
            const auto* ins = m->findInsert (link.insert);
            const auto* info = ins != nullptr && ins->kind == "whynot" ? dsp::findEffect (ins->type.toStdString()) : nullptr;
            const int index = info != nullptr ? info->indexOf (link.param.toRawUTF8()) : -1;
            valid = index >= 0 && ! info->params[(size_t) index].hidden;
        }
        else if (valid && link.kind == "macro")
        {
            link.insert = {};
            valid = link.param.containsOnly ("0123456789") && link.param.getIntValue() >= 1 && link.param.getIntValue() <= maxMacros
                    && link.param == juce::String (link.param.getIntValue());
        }
        else
        {
            valid = false;
        }

        if (valid && (int) links.size() < maxMidiLinks
            && std::none_of (links.begin(), links.end(), [&link] (const MidiLink& l) { return l.sameControl (link) || l.sameTarget (link); }))
            links.push_back (link);
    }

    std::reverse (links.begin(), links.end());
    p.midiMap = std::move (links);
}

//==============================================================================
// O toJson antigo, que monta a árvore de juce::var e deixa o juce::JSON escrever. Ficou como referência:
// o autoteste da fluidez compara a saída dele com a do toJson novo, byte a byte. Campo novo no .wnl entra
// nos dois (o autoteste acusa se faltar num deles).
juce::String toJsonReference (const Project& p)
{
    auto root = obj();
    set (root, "format", formatName);
    set (root, "schemaVersion", currentSchemaVersion);

    auto app = obj();
    set (app, "name", "WHYNOT LAB");
    set (app, "version", ProjectInfo::versionString);
    set (root, "app", app);

    auto proj = obj();
    set (proj, "name", p.name);
    set (proj, "bpm", roundTo (p.bpm, 3));
    juce::Array<juce::var> ts { p.timeSigNum, p.timeSigDen };
    set (proj, "timeSignature", ts);
    auto key = obj();
    set (key, "root", noteNames[p.keyRoot]);
    set (key, "scale", p.scale);
    set (proj, "key", key);
    set (proj, "metronome", p.metronome);
    set (proj, "loop", p.loop);
    set (proj, "songMode", p.songMode);
    set (proj, "currentPattern", p.currentPattern);

    if (p.punchOn || p.punchIn != 0 || p.punchOut != ticksPerBar4)   // opcional (3.5): projeto sem punch sai igual
    {
        auto punch = obj();
        set (punch, "on", p.punchOn);
        set (punch, "in", p.punchIn);
        set (punch, "out", p.punchOut);
        set (proj, "punch", punch);
    }

    set (root, "project", proj);

    juce::Array<juce::var> channels;

    for (auto& c : p.channels)
    {
        auto ch = obj();
        set (ch, "id", c.id);
        set (ch, "name", c.name);
        set (ch, "kind", c.kind);
        set (ch, "color", c.colour);

        auto sample = obj();
        set (sample, "path", c.samplePath);
        set (sample, "originalPath", c.originalPath);
        set (ch, "sample", sample);

        auto sampler = obj();
        set (sampler, "rootKey", c.rootKey);
        set (sampler, "oneShot", c.oneShot);
        set (sampler, "cutItself", c.cutItself);
        set (sampler, "mono", c.mono);
        set (sampler, "glideMs", roundTo (c.glideMs, 1));
        set (sampler, "rootCents", roundTo (c.rootCents, 1));
        set (sampler, "tune", roundTo (c.tune, 2));
        set (sampler, "attackMs", roundTo (c.attackMs, 1));

        if (c.releaseMs != Channel {}.releaseMs)   // opcional: um canal com o release de sempre sai igual ao de antes
            set (sampler, "releaseMs", roundTo (c.releaseMs, 1));
        set (sampler, "decayMs", roundTo (c.decayMs, 1));
        set (sampler, "sustain", roundTo (c.sustain, 3));
        set (sampler, "pitchDrop", roundTo (c.pitchDrop, 2));
        set (sampler, "pitchDropMs", roundTo (c.pitchDropMs, 1));
        set (sampler, "start", roundTo (c.start, 4));

        // Laço: só grava se foi mexido, para um canal sem laço sair igual ao de antes. As pontas vão em
        // milionésimos inteiros: o JSON do .wnl guarda só 4 casas, e 0,0001 de um 808 de 2 s já são 10
        // amostras (o laço sairia de fase ao reabrir).
        if (const Channel plain; c.loop || c.loopStart != plain.loopStart || c.loopEnd != plain.loopEnd || c.loopXfadeMs != plain.loopXfadeMs)
        {
            set (sampler, "loop", c.loop);
            set (sampler, "loopStartPpm", juce::roundToInt ((double) c.loopStart * 1.0e6));
            set (sampler, "loopEndPpm", juce::roundToInt ((double) c.loopEnd * 1.0e6));
            set (sampler, "loopXfadeMs", roundTo (c.loopXfadeMs, 1));
        }

        if (! c.slices.empty())   // fatiador: só grava num canal fatiado; os começos em milionésimos, como o laço
        {
            juce::Array<juce::var> starts;

            for (float v : c.slices)
                starts.add (juce::roundToInt ((double) v * 1.0e6));

            set (sampler, "slicesPpm", starts);
            set (sampler, "sliceKey", c.sliceKey);
            set (sampler, "sliceBpm", roundTo (c.sliceBpm, 3));
        }

        if (! c.layers.empty())   // camadas por força (bloco 4f): só quando há
        {
            juce::Array<juce::var> layers;

            for (auto& l : c.layers)
            {
                auto lo = obj();
                set (lo, "path", l.samplePath);
                set (lo, "minVelocity", l.minVelocity);
                layers.add (lo);
            }

            set (sampler, "layers", layers);
        }

        set (ch, "sampler", sampler);

        if (c.isPlugin())
            set (ch, "plugin", pluginToJson (c.plugin));

        if (c.chokeGroup != 0)
            set (ch, "choke", c.chokeGroup);

        if (c.swing != 0.0f)
            set (ch, "swing", roundTo (c.swing, 1));
        if (c.isSynth())
        {
            // Campo opcional: arquivos sem ele abrem iguais. O núcleo sobe o esquema para 4 no merge do synth.
            auto synthObj = obj();

            if (c.synthPreset.isNotEmpty())
                set (synthObj, "preset", c.synthPreset);

            auto params = obj();

            for (auto& prm : synth::allParams())   // na ordem do synth: diff estável
                if (auto it = c.synthParams.find (prm.id); it != c.synthParams.end())
                    set (params, juce::Identifier (prm.id), roundTo (it->second, 4));

            set (synthObj, "params", params);
            set (ch, "synth", synthObj);
        }

        if (c.isPiano())
        {
            // Campo opcional, como o patternOffset: arquivos sem ele abrem iguais.
            auto inst = obj();
            set (inst, "type", c.instrument);
            set (inst, "preset", c.instrumentPreset);
            set (ch, "instrument", inst);
        }

        set (ch, "mute", c.mute);
        set (ch, "output", c.output);

        if (c.armed)
            set (ch, "armed", true);

        channels.add (ch);
    }

    set (root, "channels", channels);

    juce::Array<juce::var> patterns;

    for (auto& pat : p.patterns)
    {
        auto po = obj();
        set (po, "id", pat.id);
        set (po, "name", pat.name);
        set (po, "lengthSteps", pat.lengthSteps);

        auto lanes = obj();

        for (auto& c : p.channels)   // ordem dos canais, não do mapa: diff estável
        {
            juce::Array<juce::var> on, notes;

            if (auto it = pat.steps.find (c.id); it != pat.steps.end())
            {
                for (size_t i = 0; i < it->second.size(); ++i)
                {
                    const auto& s = it->second[i];

                    if (! s.on)
                        continue;

                    auto so = obj();
                    set (so, "i", (int) i);
                    set (so, "vel", s.velocity);

                    if (s.roll != 1)
                        set (so, "roll", s.roll);

                    on.add (so);
                }
            }

            if (auto* list = pat.notesFor (c.id))
            {
                for (auto& n : *list)
                {
                    auto no = obj();
                    set (no, "id", n.id);
                    set (no, "t", n.start);
                    set (no, "len", n.length);
                    set (no, "key", n.key);
                    set (no, "vel", n.velocity);

                    if (n.slide)
                        set (no, "slide", true);

                    if (n.muted)   // nota muda (bloco 1, 22/10): opcional
                        set (no, "muted", true);

                    notes.add (no);
                }
            }

            if (on.isEmpty() && notes.isEmpty())
                continue;

            auto lane = obj();

            if (! on.isEmpty())    set (lane, "steps", on);
            if (! notes.isEmpty()) set (lane, "notes", notes);

            set (lanes, juce::Identifier (c.id), lane);
        }

        set (po, "lanes", lanes);
        patterns.add (po);
    }

    set (root, "patterns", patterns);

    juce::Array<juce::var> plTracks, plItems;

    for (auto& t : p.playlistTracks)
    {
        auto to = obj();
        set (to, "id", t.id);
        set (to, "name", t.name);

        if (t.recordTrack.isNotEmpty())
            set (to, "recordTrack", t.recordTrack);

        if (t.colour.isNotEmpty())
            set (to, "colour", t.colour);

        if (t.muted)
            set (to, "muted", true);

        if (t.solo)
            set (to, "solo", true);

        plTracks.add (to);
    }

    for (auto& i : p.playlistItems)
    {
        auto io = obj();
        set (io, "id", i.id);
        set (io, "kind", i.kind);
        set (io, "ref", i.ref);
        set (io, "track", i.track);
        set (io, "start", i.start);
        set (io, "length", i.length);

        if (i.offsetSeconds > 0.0)
            set (io, "offsetSeconds", roundTo (i.offsetSeconds, 6));

        // opcionais (21/10): só saem no arquivo quando diferentes do padrão
        if (i.patternOffset != 0)
            set (io, "patternOffset", i.patternOffset);

        if (i.muted)
            set (io, "muted", true);

        if (i.stretch != 1.0)
            set (io, "stretch", roundTo (i.stretch, 6));

        if (i.tape)
            set (io, "tape", true);

        if (i.takeGroup.isNotEmpty())
        {
            set (io, "takeGroup", i.takeGroup);
            set (io, "take", i.take);
        }

        if (i.gainDb != 0.0f)
            set (io, "gainDb", roundTo (i.gainDb, 1));

        if (i.fadeIn > 0.0)
            set (io, "fadeIn", roundTo (i.fadeIn, 6));

        if (i.fadeOut > 0.0)
            set (io, "fadeOut", roundTo (i.fadeOut, 6));

        plItems.add (io);
    }

    auto playlist = obj();
    set (playlist, "tracks", plTracks);
    set (playlist, "items", plItems);

    // marcadores na régua (bloco 1, 22/10), opcionais: projeto sem marcador sai igual
    if (! p.markers.empty())
    {
        juce::Array<juce::var> marks;

        for (auto& m : p.markers)
        {
            auto mo = obj();
            set (mo, "id", m.id);
            set (mo, "t", m.t);
            set (mo, "name", m.name);
            marks.add (mo);
        }

        set (playlist, "markers", marks);
    }

    set (root, "playlist", playlist);

    // Automação (3.5), opcional: projeto sem curvas sai igual. v = posição do knob, 0 a 1.
    if (! p.automation.empty())
    {
        juce::Array<juce::var> lanes;

        for (auto& lane : p.automation)
        {
            auto lo = obj();
            set (lo, "id", lane.id);

            auto target = obj();
            set (target, "kind", lane.target.kind);

            if (lane.target.track.isNotEmpty())    set (target, "track", lane.target.track);
            if (lane.target.channel.isNotEmpty())  set (target, "channel", lane.target.channel);
            if (lane.target.insert.isNotEmpty())   set (target, "insert", lane.target.insert);

            set (target, "param", lane.target.param);
            set (lo, "target", target);

            juce::Array<juce::var> points;

            for (auto& pt : lane.points)
            {
                auto po = obj();
                set (po, "t", pt.t);
                set (po, "v", roundTo (pt.v, 4));
                points.add (po);
            }

            set (lo, "points", points);
            lanes.add (lo);
        }

        set (root, "automation", lanes);
    }

    // MIDI learn (bloco 4d), opcional: projeto sem controlador ligado sai igual.
    if (! p.midiMap.empty())
    {
        juce::Array<juce::var> links;

        for (auto& link : p.midiMap)
        {
            auto lo = obj();
            set (lo, "cc", link.cc);

            if (link.channel > 0)
                set (lo, "channel", link.channel);

            set (lo, "kind", link.kind);
            set (lo, "track", link.track);

            if (link.insert.isNotEmpty())
                set (lo, "insert", link.insert);

            set (lo, "param", link.param);
            links.add (lo);
        }

        set (root, "midiMap", links);
    }

    juce::Array<juce::var> tracks;

    for (auto& m : p.mixer)
    {
        auto mo = obj();
        set (mo, "id", m.id);
        set (mo, "name", m.name);
        set (mo, "color", m.colour);
        set (mo, "volumeDb", roundTo (m.volumeDb, 2));
        set (mo, "pan", roundTo (m.pan, 3));
        set (mo, "mute", m.mute);
        set (mo, "solo", m.solo);
        set (mo, "output", m.isMaster() ? juce::String ("device") : m.output);

        if (m.autoFor.isNotEmpty())
            set (mo, "autoFor", m.autoFor);

        if (m.role.isNotEmpty())
            set (mo, "role", m.role);

        if (m.armed)
            set (mo, "armed", true);

        if (m.frozen)
            set (mo, "frozen", true);

        if (m.monitor.isNotEmpty())
            set (mo, "monitor", m.monitor);

        juce::Array<juce::var> inserts, sends;

        for (auto& ins : m.inserts)
        {
            auto io = obj();
            set (io, "id", ins.id);
            set (io, "kind", ins.kind);
            set (io, "type", ins.type);
            set (io, "bypass", ins.bypass);

            if (ins.preset.isNotEmpty())
                set (io, "preset", ins.preset);

            if (ins.sidechain.isNotEmpty())
                set (io, "sidechain", ins.sidechain);

            if (ins.kind == "vst3")
                set (io, "plugin", pluginToJson (ins.plugin));

            // Parâmetros na ordem do efeito (diff estável); desconhecidos, em ordem alfabética.
            auto params = obj();
            std::set<juce::String> written;

            if (auto* info = dsp::findEffect (ins.type.toStdString()))
                for (auto& param : info->params)
                    if (auto it = ins.params.find (param.id); it != ins.params.end())
                    {
                        set (params, juce::Identifier (param.id), roundTo (it->second, 4));
                        written.insert (it->first);
                    }

            for (auto& [id, value] : ins.params)
                if (written.count (id) == 0)
                    set (params, juce::Identifier (id), roundTo (value, 4));

            set (io, "params", params);
            inserts.add (io);
        }

        for (auto& snd : m.sends)
        {
            auto so = obj();
            set (so, "to", snd.to);
            set (so, "db", roundTo (snd.levelDb, 2));

            if (snd.preFader)
                set (so, "pre", true);

            sends.add (so);
        }

        set (mo, "inserts", inserts);
        set (mo, "sends", sends);

        if (! m.macros.empty())   // o rack (bloco 4c): opcional, igual ao toJson
        {
            juce::Array<juce::var> macroList;

            for (auto& mac : m.macros)
            {
                auto ao = obj();

                if (mac.name.isNotEmpty())
                    set (ao, "name", mac.name);

                set (ao, "value", roundTo (mac.value, 4));
                juce::Array<juce::var> links;

                for (auto& link : mac.links)
                {
                    auto lo = obj();
                    set (lo, "insert", link.insert);
                    set (lo, "param", link.param);
                    set (lo, "min", roundTo (link.min, 4));
                    set (lo, "max", roundTo (link.max, 4));
                    links.add (lo);
                }

                set (ao, "links", links);
                macroList.add (ao);
            }

            set (mo, "macros", macroList);
        }

        tracks.add (mo);
    }

    auto mixer = obj();
    set (mixer, "tracks", tracks);
    set (root, "mixer", mixer);

    // O JUCE usa \r\n no Windows; o arquivo é sempre \n (igual em qualquer sistema e no git).
    // Seis casas (gravar voz, 21/10): o corte do áudio e o esticar são arredondados no microssegundo
    // (roundToMicroseconds, roundTo 6); com quatro, o take reaberto andava até 50 µs (2 amostras).
    return juce::JSON::toString (root, juce::JSON::FormatOptions{}
                                         .withSpacing (juce::JSON::Spacing::multiLine)
                                         .withMaxDecimalPlaces (6))
             .replace ("\r\n", "\n");
}

//==============================================================================
namespace
{
    /** Escreve o JSON direto no texto, no formato do juce::JSON::toString com Spacing::multiLine e 6 casas:
        2 espaços por nível, "chave": valor, objeto vazio como "{\n<recuo>}", lista vazia como "[]".
        O caminho antigo levava 1,6 s num projeto de 154 KB (medido no autoteste da fluidez): o .replace
        das quebras de linha do juce::String refaz o texto inteiro a cada linha (quadrático), e a árvore de
        juce::var monta um objeto por passo e por nota. Cada comando guarda o projeto duas vezes para o
        desfazer, e o clique esperava por isso (pedido do usuário de 21/10 à noite). */
    class JsonWriter
    {
    public:
        JsonWriter()   { out.preallocate (256 * 1024); }

        void beginObject()   { out.writeByte ('{'); open.push_back ({ false, true }); }
        void endObject()     { close ('}', true); }
        void beginArray()    { out.writeByte ('['); open.push_back ({ true, true }); }
        void endArray()      { close (']', false); }

        /** Próximo membro do objeto aberto: depois dele vem um valor, um objeto ou uma lista. */
        JsonWriter& key (const char* k)            { newItem(); writeQuoted (k, std::strlen (k)); out.write (": ", 2); return *this; }
        JsonWriter& key (const juce::String& k)    { newItem(); writeQuoted (k); out.write (": ", 2); return *this; }

        /** Próximo elemento da lista aberta. */
        JsonWriter& item()   { newItem(); return *this; }

        void value (int v)
        {
            char buffer[16];
            const auto r = std::to_chars (buffer, buffer + sizeof (buffer), v);
            out.write (buffer, (size_t) (r.ptr - buffer));
        }

        void value (bool v)                  { if (v) out.write ("true", 4); else out.write ("false", 5); }
        void value (const char* v)           { writeQuoted (v, std::strlen (v)); }
        void value (const juce::String& v)   { writeQuoted (v); }

        /** Número com casas: o mesmo texto do juce::JSON (ele decide entre "140.0", "0.3333" e notação científica). */
        void value (double v)                { juce::JSON::writeToStream (out, juce::var (v), numberFormat); }

        void member (const char* k, int v)                   { key (k).value (v); }
        void member (const char* k, bool v)                  { key (k).value (v); }
        void member (const char* k, double v)                { key (k).value (v); }
        void member (const char* k, const char* v)           { key (k).value (v); }
        void member (const char* k, const juce::String& v)   { key (k).value (v); }

        juce::String toString()   { return out.toUTF8(); }

    private:
        struct Level { bool array; bool empty; };

        void newItem()
        {
            jassert (! open.empty());
            auto& level = open.back();
            if (level.empty)
                out.writeByte ('\n');
            else
                out.write (",\n", 2);

            level.empty = false;
            out.writeRepeatedByte (' ', open.size() * 2);
        }

        void close (char bracket, bool isObject)
        {
            jassert (! open.empty() && open.back().array != isObject);
            const bool wasEmpty = open.back().empty;
            open.pop_back();

            // o JUCE quebra a linha até no objeto vazio ("{\n}"), mas não na lista vazia ("[]")
            if (isObject || ! wasEmpty)
            {
                out.writeByte ('\n');
                out.writeRepeatedByte (' ', open.size() * 2);
            }

            out.writeByte (bracket);
        }

        void writeQuoted (const juce::String& s)
        {
            const auto* utf8 = s.toRawUTF8();
            writeQuoted (utf8, std::strlen (utf8));
        }

        void writeQuoted (const char* utf8, size_t length)
        {
            out.writeByte ('"');
            bool plain = true;

            for (size_t i = 0; i < length && plain; ++i)
            {
                const auto c = (unsigned char) utf8[i];
                plain = c >= 0x20 && c < 0x80 && c != '"' && c != '\\';
            }

            if (plain)
                out.write (utf8, length);
            else
                writeEscaped (utf8);

            out.writeByte ('"');
        }

        /** O caminho lento, igual ao do juce::JSON: caractere por caractere (acentos, aspas, controles). */
        void writeEscaped (const char* utf8)
        {
            juce::CharPointer_UTF8 t (utf8);

            for (;;)
            {
                const auto c = t.getAndAdvance();

                switch (c)
                {
                    case 0:    return;
                    case '\"': out.write ("\\\"", 2); break;
                    case '\\': out.write ("\\\\", 2); break;
                    case '\b': out.write ("\\b", 2);  break;
                    case '\f': out.write ("\\f", 2);  break;
                    case '\t': out.write ("\\t", 2);  break;
                    case '\r': out.write ("\\r", 2);  break;
                    case '\n': out.write ("\\n", 2);  break;

                    default:
                        if (juce::CharacterFunctions::isAsciiControlCharacter (c))
                            out << "\\u" << juce::String::toHexString ((int) (unsigned short) c).paddedLeft ('0', 4);
                        else
                            out << juce::String::charToString (c);

                        break;
                }
            }
        }

        juce::MemoryOutputStream out;
        std::vector<Level> open;
        // seis casas, como o toJsonReference (gravar voz, 21/10: o corte do take reaberto não anda)
        const juce::JSON::FormatOptions numberFormat = juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine)
                                                                                    .withMaxDecimalPlaces (6);
    };

    /** O mesmo arredondamento do roundTo, sem passar por juce::var. */
    double rounded (double v, int decimals)
    {
        const double scale = std::pow (10.0, decimals);
        return std::round (v * scale) / scale;
    }

    void writePlugin (JsonWriter& w, const PluginRef& r)
    {
        w.beginObject();
        w.member ("name", r.name);
        w.member ("manufacturer", r.manufacturer);
        w.member ("format", r.format);
        w.member ("file", r.file);
        w.member ("uid", r.uid);
        w.member ("instrument", r.instrument);
        w.member ("state", r.state);
        w.endObject();
    }
}

juce::String toJson (const Project& p)
{
    // Mesma ordem de chaves e mesmos campos opcionais do toJsonReference (acima): o texto sai igual.
    JsonWriter w;
    w.beginObject();
    w.member ("format", formatName);
    w.member ("schemaVersion", currentSchemaVersion);

    w.key ("app").beginObject();
    w.member ("name", "WHYNOT LAB");
    w.member ("version", ProjectInfo::versionString);
    w.endObject();

    w.key ("project").beginObject();
    w.member ("name", p.name);
    w.member ("bpm", rounded (p.bpm, 3));
    w.key ("timeSignature").beginArray();
    w.item().value (p.timeSigNum);
    w.item().value (p.timeSigDen);
    w.endArray();
    w.key ("key").beginObject();
    w.member ("root", noteNames[p.keyRoot]);
    w.member ("scale", p.scale);
    w.endObject();
    w.member ("metronome", p.metronome);
    w.member ("loop", p.loop);
    w.member ("songMode", p.songMode);
    w.member ("currentPattern", p.currentPattern);

    if (p.punchOn || p.punchIn != 0 || p.punchOut != ticksPerBar4)   // opcional (3.5): projeto sem punch sai igual
    {
        w.key ("punch").beginObject();
        w.member ("on", p.punchOn);
        w.member ("in", p.punchIn);
        w.member ("out", p.punchOut);
        w.endObject();
    }

    w.endObject();

    w.key ("channels").beginArray();

    for (auto& c : p.channels)
    {
        w.item().beginObject();
        w.member ("id", c.id);
        w.member ("name", c.name);
        w.member ("kind", c.kind);
        w.member ("color", c.colour);

        w.key ("sample").beginObject();
        w.member ("path", c.samplePath);
        w.member ("originalPath", c.originalPath);
        w.endObject();

        w.key ("sampler").beginObject();
        w.member ("rootKey", c.rootKey);
        w.member ("oneShot", c.oneShot);
        w.member ("cutItself", c.cutItself);
        w.member ("mono", c.mono);
        w.member ("glideMs", rounded (c.glideMs, 1));
        w.member ("rootCents", rounded (c.rootCents, 1));
        w.member ("tune", rounded (c.tune, 2));
        w.member ("attackMs", rounded (c.attackMs, 1));

        if (c.releaseMs != Channel {}.releaseMs)   // opcional (Fatiar e 808): o release de sempre sai igual ao de antes
            w.member ("releaseMs", rounded (c.releaseMs, 1));

        w.member ("decayMs", rounded (c.decayMs, 1));
        w.member ("sustain", rounded (c.sustain, 3));
        w.member ("pitchDrop", rounded (c.pitchDrop, 2));
        w.member ("pitchDropMs", rounded (c.pitchDropMs, 1));
        w.member ("start", rounded (c.start, 4));

        // laço (Fatiar e 808): só quando foi mexido; as pontas em milionésimos inteiros
        if (const Channel plain; c.loop || c.loopStart != plain.loopStart || c.loopEnd != plain.loopEnd || c.loopXfadeMs != plain.loopXfadeMs)
        {
            w.member ("loop", c.loop);
            w.member ("loopStartPpm", juce::roundToInt ((double) c.loopStart * 1.0e6));
            w.member ("loopEndPpm", juce::roundToInt ((double) c.loopEnd * 1.0e6));
            w.member ("loopXfadeMs", rounded (c.loopXfadeMs, 1));
        }

        if (! c.slices.empty())   // fatiador (Fatiar e 808): só num canal fatiado; os começos em milionésimos
        {
            w.key ("slicesPpm").beginArray();

            for (float v : c.slices)
                w.item().value (juce::roundToInt ((double) v * 1.0e6));

            w.endArray();
            w.member ("sliceKey", c.sliceKey);
            w.member ("sliceBpm", rounded (c.sliceBpm, 3));
        }

        if (! c.layers.empty())   // camadas por força (bloco 4f), igual ao toJsonReference
        {
            w.key ("layers").beginArray();

            for (auto& l : c.layers)
            {
                w.item().beginObject();
                w.member ("path", l.samplePath);
                w.member ("minVelocity", l.minVelocity);
                w.endObject();
            }

            w.endArray();
        }

        w.endObject();

        if (c.isPlugin())
        {
            w.key ("plugin");
            writePlugin (w, c.plugin);
        }

        if (c.chokeGroup != 0)
            w.member ("choke", c.chokeGroup);

        if (c.swing != 0.0f)
            w.member ("swing", rounded (c.swing, 1));

        if (c.isSynth())
        {
            w.key ("synth").beginObject();

            if (c.synthPreset.isNotEmpty())
                w.member ("preset", c.synthPreset);

            w.key ("params").beginObject();

            for (auto& prm : synth::allParams())   // na ordem do synth: diff estável
                if (auto it = c.synthParams.find (prm.id); it != c.synthParams.end())
                    w.member (prm.id, rounded (it->second, 4));

            w.endObject();
            w.endObject();
        }

        if (c.isPiano())
        {
            w.key ("instrument").beginObject();
            w.member ("type", c.instrument);
            w.member ("preset", c.instrumentPreset);
            w.endObject();
        }

        w.member ("mute", c.mute);
        w.member ("output", c.output);

        if (c.armed)
            w.member ("armed", true);

        w.endObject();
    }

    w.endArray();

    w.key ("patterns").beginArray();
    std::vector<const juce::String*> writtenLanes;

    for (auto& pat : p.patterns)
    {
        w.item().beginObject();
        w.member ("id", pat.id);
        w.member ("name", pat.name);
        w.member ("lengthSteps", pat.lengthSteps);

        w.key ("lanes").beginObject();
        writtenLanes.clear();

        for (auto& c : p.channels)   // ordem dos canais, não do mapa: diff estável
        {
            const auto stepsIt = pat.steps.find (c.id);
            const auto* stepList = stepsIt != pat.steps.end() ? &stepsIt->second : nullptr;
            const auto* noteList = pat.notesFor (c.id);
            const bool anyStep = stepList != nullptr && std::any_of (stepList->begin(), stepList->end(), [] (const Step& s) { return s.on; });

            if (! anyStep && noteList == nullptr)
                continue;

            // dois canais com o mesmo id davam a mesma raia, e o objeto do JUCE guardava uma só, no primeiro lugar
            if (std::any_of (writtenLanes.begin(), writtenLanes.end(), [&] (const juce::String* id) { return *id == c.id; }))
                continue;

            writtenLanes.push_back (&c.id);
            w.key (c.id).beginObject();

            if (anyStep)
            {
                w.key ("steps").beginArray();

                for (size_t i = 0; i < stepList->size(); ++i)
                {
                    const auto& s = (*stepList)[i];

                    if (! s.on)
                        continue;

                    w.item().beginObject();
                    w.member ("i", (int) i);
                    w.member ("vel", s.velocity);

                    if (s.roll != 1)
                        w.member ("roll", s.roll);

                    w.endObject();
                }

                w.endArray();
            }

            if (noteList != nullptr)
            {
                w.key ("notes").beginArray();

                for (auto& n : *noteList)
                {
                    w.item().beginObject();
                    w.member ("id", n.id);
                    w.member ("t", n.start);
                    w.member ("len", n.length);
                    w.member ("key", n.key);
                    w.member ("vel", n.velocity);

                    if (n.slide)
                        w.member ("slide", true);

                    if (n.muted)   // nota muda (bloco 1, 22/10): opcional, igual ao toJsonReference
                        w.member ("muted", true);

                    w.endObject();
                }

                w.endArray();
            }

            w.endObject();
        }

        w.endObject();
        w.endObject();
    }

    w.endArray();

    w.key ("playlist").beginObject();
    w.key ("tracks").beginArray();

    for (auto& t : p.playlistTracks)
    {
        w.item().beginObject();
        w.member ("id", t.id);
        w.member ("name", t.name);

        if (t.recordTrack.isNotEmpty())
            w.member ("recordTrack", t.recordTrack);

        if (t.colour.isNotEmpty())
            w.member ("colour", t.colour);

        if (t.muted)
            w.member ("muted", true);

        if (t.solo)
            w.member ("solo", true);

        w.endObject();
    }

    w.endArray();
    w.key ("items").beginArray();

    for (auto& i : p.playlistItems)
    {
        w.item().beginObject();
        w.member ("id", i.id);
        w.member ("kind", i.kind);
        w.member ("ref", i.ref);
        w.member ("track", i.track);
        w.member ("start", i.start);
        w.member ("length", i.length);

        if (i.offsetSeconds > 0.0)
            w.member ("offsetSeconds", rounded (i.offsetSeconds, 6));

        // opcionais (21/10): só saem no arquivo quando diferentes do padrão
        if (i.patternOffset != 0)
            w.member ("patternOffset", i.patternOffset);

        if (i.muted)
            w.member ("muted", true);

        if (i.stretch != 1.0)
            w.member ("stretch", rounded (i.stretch, 6));

        if (i.tape)
            w.member ("tape", true);

        if (i.takeGroup.isNotEmpty())
        {
            w.member ("takeGroup", i.takeGroup);
            w.member ("take", i.take);
        }

        if (i.gainDb != 0.0f)
            w.member ("gainDb", rounded (i.gainDb, 1));

        if (i.fadeIn > 0.0)
            w.member ("fadeIn", rounded (i.fadeIn, 6));

        if (i.fadeOut > 0.0)
            w.member ("fadeOut", rounded (i.fadeOut, 6));

        w.endObject();
    }

    w.endArray();

    // marcadores na régua (bloco 1, 22/10), opcionais: igual ao toJsonReference
    if (! p.markers.empty())
    {
        w.key ("markers").beginArray();

        for (auto& m : p.markers)
        {
            w.item().beginObject();
            w.member ("id", m.id);
            w.member ("t", m.t);
            w.member ("name", m.name);
            w.endObject();
        }

        w.endArray();
    }

    w.endObject();

    // Automação (3.5), opcional: projeto sem curvas sai igual. v = posição do knob, 0 a 1.
    if (! p.automation.empty())
    {
        w.key ("automation").beginArray();

        for (auto& lane : p.automation)
        {
            w.item().beginObject();
            w.member ("id", lane.id);

            w.key ("target").beginObject();
            w.member ("kind", lane.target.kind);

            if (lane.target.track.isNotEmpty())    w.member ("track", lane.target.track);
            if (lane.target.channel.isNotEmpty())  w.member ("channel", lane.target.channel);
            if (lane.target.insert.isNotEmpty())   w.member ("insert", lane.target.insert);

            w.member ("param", lane.target.param);
            w.endObject();

            w.key ("points").beginArray();

            for (auto& pt : lane.points)
            {
                w.item().beginObject();
                w.member ("t", pt.t);
                w.member ("v", rounded (pt.v, 4));
                w.endObject();
            }

            w.endArray();
            w.endObject();
        }

        w.endArray();
    }

    // MIDI learn (bloco 4d), opcional, igual ao toJsonReference
    if (! p.midiMap.empty())
    {
        w.key ("midiMap").beginArray();

        for (auto& link : p.midiMap)
        {
            w.item().beginObject();
            w.member ("cc", link.cc);

            if (link.channel > 0)
                w.member ("channel", link.channel);

            w.member ("kind", link.kind);
            w.member ("track", link.track);

            if (link.insert.isNotEmpty())
                w.member ("insert", link.insert);

            w.member ("param", link.param);
            w.endObject();
        }

        w.endArray();
    }

    w.key ("mixer").beginObject();
    w.key ("tracks").beginArray();

    for (auto& m : p.mixer)
    {
        w.item().beginObject();
        w.member ("id", m.id);
        w.member ("name", m.name);
        w.member ("color", m.colour);
        w.member ("volumeDb", rounded (m.volumeDb, 2));
        w.member ("pan", rounded (m.pan, 3));
        w.member ("mute", m.mute);
        w.member ("solo", m.solo);
        w.member ("output", m.isMaster() ? juce::String ("device") : m.output);

        if (m.autoFor.isNotEmpty())
            w.member ("autoFor", m.autoFor);

        if (m.role.isNotEmpty())
            w.member ("role", m.role);

        if (m.armed)
            w.member ("armed", true);

        if (m.frozen)
            w.member ("frozen", true);

        if (m.monitor.isNotEmpty())
            w.member ("monitor", m.monitor);

        w.key ("inserts").beginArray();

        for (auto& ins : m.inserts)
        {
            w.item().beginObject();
            w.member ("id", ins.id);
            w.member ("kind", ins.kind);
            w.member ("type", ins.type);
            w.member ("bypass", ins.bypass);

            if (ins.preset.isNotEmpty())
                w.member ("preset", ins.preset);

            if (ins.sidechain.isNotEmpty())
                w.member ("sidechain", ins.sidechain);

            if (ins.kind == "vst3")
            {
                w.key ("plugin");
                writePlugin (w, ins.plugin);
            }

            // Parâmetros na ordem do efeito (diff estável); desconhecidos, em ordem alfabética.
            w.key ("params").beginObject();
            std::vector<const juce::String*> written;

            if (auto* info = dsp::findEffect (ins.type.toStdString()))
                for (auto& param : info->params)
                    if (auto it = ins.params.find (param.id); it != ins.params.end())
                        if (std::find (written.begin(), written.end(), &it->first) == written.end())
                        {
                            w.member (param.id, rounded (it->second, 4));
                            written.push_back (&it->first);
                        }

            for (auto& [id, value] : ins.params)
                if (std::find (written.begin(), written.end(), &id) == written.end())
                    w.key (id).value (rounded (value, 4));

            w.endObject();
            w.endObject();
        }

        w.endArray();
        w.key ("sends").beginArray();

        for (auto& snd : m.sends)
        {
            w.item().beginObject();
            w.member ("to", snd.to);
            w.member ("db", rounded (snd.levelDb, 2));

            if (snd.preFader)   // envio pré-fader (revisão do mixer, 22/10): opcional, igual ao toJsonReference
                w.member ("pre", true);

            w.endObject();
        }

        w.endArray();

        if (! m.macros.empty())   // o rack (bloco 4c): opcional, igual ao toJsonReference
        {
            w.key ("macros").beginArray();

            for (auto& mac : m.macros)
            {
                w.item().beginObject();

                if (mac.name.isNotEmpty())
                    w.member ("name", mac.name);

                w.member ("value", rounded (mac.value, 4));
                w.key ("links").beginArray();

                for (auto& link : mac.links)
                {
                    w.item().beginObject();
                    w.member ("insert", link.insert);
                    w.member ("param", link.param);
                    w.member ("min", rounded (link.min, 4));
                    w.member ("max", rounded (link.max, 4));
                    w.endObject();
                }

                w.endArray();
                w.endObject();
            }

            w.endArray();
        }

        w.endObject();
    }

    w.endArray();
    w.endObject();
    w.endObject();
    return w.toString();
}

//==============================================================================
juce::Result fromJson (const juce::String& json, Project& out)
{
    juce::var root;
    const auto parsed = parseJsonSafely (json, root);   // recusa aninhamento demais antes do parser recursivo

    if (parsed.failed())
        return juce::Result::fail (parsed.getErrorMessage().contains ("aninhamento"_t) ? parsed.getErrorMessage()
                                                                                      : "O arquivo não é um JSON válido: "_t + parsed.getErrorMessage());

    if (getString (root, "format") != formatName)
        return juce::Result::fail ("Este arquivo não é um projeto da WHYNOT LAB."_t);

    const int version = getInt (root, "schemaVersion", 0);

    if (version < 1)
        return juce::Result::fail ("Versão do projeto ausente ou inválida."_t);

    if (version > currentSchemaVersion)
        return juce::Result::fail ("Este projeto foi salvo por uma versão mais nova da WHYNOT LAB (esquema "_t
                                   + juce::String (version) + "). Atualize o app para abrir.");

    // Esquema 1 → 2 só acrescentou campos opcionais (inserts, envios, papel): nada a converter.
    // Esquema 2 → 3: o corte do clipe de áudio era em ticks e virou segundos do arquivo (ver os itens da playlist).

    Project p;
    const auto& proj = root["project"];
    p.name = getString (proj, "name", p.name);
    p.bpm = juce::jlimit (20.0, 999.0, getDouble (proj, "bpm", p.bpm));

    if (auto* ts = proj["timeSignature"].getArray(); ts != nullptr && ts->size() == 2)
    {
        p.timeSigNum = juce::jlimit (1, 32, (int) (*ts)[0]);
        p.timeSigDen = juce::jlimit (1, 32, (int) (*ts)[1]);
    }

    p.keyRoot = rootFromName (getString (proj["key"], "root", "C"));
    p.scale = getString (proj["key"], "scale", p.scale);
    p.metronome = getBool (proj, "metronome", p.metronome);
    p.loop = getBool (proj, "loop", p.loop);
    p.songMode = getBool (proj, "songMode", p.songMode);
    p.currentPattern = getString (proj, "currentPattern");
    p.punchOn = getBool (proj["punch"], "on", p.punchOn);
    p.punchIn = getInt (proj["punch"], "in", p.punchIn);
    p.punchOut = getInt (proj["punch"], "out", p.punchOut);

    if (auto* channels = root["channels"].getArray())
    {
        for (auto& co : *channels)
        {
            Channel c;
            c.id = getString (co, "id");

            if (c.id.isEmpty() || p.findChannel (c.id) != nullptr)
                continue;

            c.name = getString (co, "name", "Canal");
            c.colour = getString (co, "color", c.colour);
            c.samplePath = getString (co["sample"], "path");
            c.originalPath = getString (co["sample"], "originalPath");
            c.rootKey = juce::jlimit (0, 127, getInt (co["sampler"], "rootKey", c.rootKey));
            c.oneShot = getBool (co["sampler"], "oneShot", c.oneShot);
            c.cutItself = getBool (co["sampler"], "cutItself", c.cutItself);
            c.kind = getString (co, "kind", c.kind);
            c.mono = getBool (co["sampler"], "mono", c.mono);
            c.glideMs = (float) getDouble (co["sampler"], "glideMs", c.glideMs);
            c.rootCents = (float) getDouble (co["sampler"], "rootCents", c.rootCents);   // forma da nota: campos opcionais
            c.tune = (float) getDouble (co["sampler"], "tune", c.tune);
            c.attackMs = (float) getDouble (co["sampler"], "attackMs", c.attackMs);
            c.releaseMs = (float) getDouble (co["sampler"], "releaseMs", c.releaseMs);
            c.decayMs = (float) getDouble (co["sampler"], "decayMs", c.decayMs);
            c.sustain = (float) getDouble (co["sampler"], "sustain", c.sustain);
            c.pitchDrop = (float) getDouble (co["sampler"], "pitchDrop", c.pitchDrop);
            c.pitchDropMs = (float) getDouble (co["sampler"], "pitchDropMs", c.pitchDropMs);
            c.start = (float) getDouble (co["sampler"], "start", c.start);
            c.loop = getBool (co["sampler"], "loop", c.loop);                          // laço: campos opcionais
            c.loopStart = (float) (getDouble (co["sampler"], "loopStartPpm", (double) c.loopStart * 1.0e6) / 1.0e6);
            c.loopEnd = (float) (getDouble (co["sampler"], "loopEndPpm", (double) c.loopEnd * 1.0e6) / 1.0e6);
            c.loopXfadeMs = (float) getDouble (co["sampler"], "loopXfadeMs", c.loopXfadeMs);

            if (auto* starts = co["sampler"]["slicesPpm"].getArray())   // fatiador: campos opcionais
                for (auto& v : *starts)
                    if (v.isDouble() || v.isInt() || v.isInt64())
                        c.slices.push_back ((float) ((double) v / 1.0e6));

            c.sliceKey = getInt (co["sampler"], "sliceKey", c.sliceKey);
            c.sliceBpm = (float) getDouble (co["sampler"], "sliceBpm", c.sliceBpm);

            if (auto* layers = co["sampler"]["layers"].getArray())   // camadas por força (bloco 4f); o normalise confere
                for (auto& lo : *layers)
                    c.layers.push_back ({ getString (lo, "path"), getInt (lo, "minVelocity", 100) });
            c.plugin = pluginFromJson (co["plugin"]);
            c.synthPreset = getString (co["synth"], "preset");

            if (auto* sp = co["synth"]["params"].getDynamicObject())
                for (auto& prop : sp->getProperties())
                    if (prop.value.isDouble() || prop.value.isInt() || prop.value.isInt64() || prop.value.isBool())
                        c.synthParams[prop.name.toString()] = (float) (double) prop.value;

            c.mute = getBool (co, "mute", c.mute);
            c.chokeGroup = getInt (co, "choke", 0);        // opcionais: o esquema continua 3
            c.swing = (float) getDouble (co, "swing", 0.0);
            c.output = getString (co, "output");
            c.armed = getBool (co, "armed", false);
            c.instrument = getString (co["instrument"], "type");        // opcional: o WHYNOT Piano
            c.instrumentPreset = getString (co["instrument"], "preset");
            p.channels.push_back (c);
        }
    }

    if (auto* tracks = root["mixer"]["tracks"].getArray())
    {
        for (auto& mo : *tracks)
        {
            MixerTrack m;
            m.id = getString (mo, "id");

            if (m.id.isEmpty() || p.findMixer (m.id) != nullptr)
                continue;

            m.name = getString (mo, "name", "Mixer");
            m.colour = getString (mo, "color", m.colour);
            m.volumeDb = (float) juce::jlimit (-100.0, 12.0, getDouble (mo, "volumeDb", 0.0));
            m.pan = (float) juce::jlimit (-1.0, 1.0, getDouble (mo, "pan", 0.0));
            m.mute = getBool (mo, "mute", false);
            m.solo = getBool (mo, "solo", false);
            m.output = m.isMaster() ? juce::String() : getString (mo, "output");
            m.autoFor = getString (mo, "autoFor");
            m.role = getString (mo, "role");
            m.armed = getBool (mo, "armed", false);
            m.frozen = getBool (mo, "frozen", false);
            m.monitor = getString (mo, "monitor");

            if (auto* inserts = mo["inserts"].getArray())
            {
                for (auto& io : *inserts)
                {
                    Insert ins;
                    ins.id = getString (io, "id");
                    ins.kind = getString (io, "kind", "whynot");
                    ins.type = getString (io, "type");
                    ins.bypass = getBool (io, "bypass", false);
                    ins.preset = getString (io, "preset");
                    ins.sidechain = getString (io, "sidechain");
                    ins.plugin = pluginFromJson (io["plugin"]);

                    if (auto* params = io["params"].getDynamicObject())
                        for (auto& prop : params->getProperties())
                            if (prop.value.isDouble() || prop.value.isInt() || prop.value.isInt64() || prop.value.isBool())
                                ins.params[prop.name.toString()] = (float) (double) prop.value;

                    if (ins.type.isNotEmpty())
                        m.inserts.push_back (ins);
                }
            }

            if (auto* sends = mo["sends"].getArray())
                for (auto& so : *sends)
                    m.sends.push_back ({ getString (so, "to"), (float) getDouble (so, "db", -12.0), getBool (so, "pre", false) });

            if (auto* macroList = mo["macros"].getArray())   // o rack (bloco 4c); o normalise confere os links
            {
                for (auto& ao : *macroList)
                {
                    if ((int) m.macros.size() >= maxMacros)
                        break;

                    Macro mac;
                    mac.name = getString (ao, "name");
                    mac.value = (float) getDouble (ao, "value", 0.0);

                    if (auto* links = ao["links"].getArray())
                        for (auto& lo : *links)
                            mac.links.push_back ({ getString (lo, "insert"), getString (lo, "param"),
                                                   (float) getDouble (lo, "min", 0.0), (float) getDouble (lo, "max", 1.0) });

                    m.macros.push_back (mac);
                }
            }

            p.mixer.push_back (m);
        }
    }

    if (auto* patterns = root["patterns"].getArray())
    {
        for (auto& po : *patterns)
        {
            Pattern pat;
            pat.id = getString (po, "id");

            if (pat.id.isEmpty() || p.findPattern (pat.id) != nullptr)
                continue;

            pat.name = getString (po, "name", "Padrão"_t);
            pat.lengthSteps = juce::jlimit (1, 256, getInt (po, "lengthSteps", 16));

            if (auto* lanes = po["lanes"].getDynamicObject())
            {
                for (auto& lane : lanes->getProperties())
                {
                    const auto channelId = lane.name.toString();

                    if (p.findChannel (channelId) == nullptr)
                        continue;

                    auto& steps = pat.steps[channelId];
                    steps.resize ((size_t) pat.lengthSteps);

                    if (auto* on = lane.value["steps"].getArray())
                    {
                        for (auto& so : *on)
                        {
                            const int i = getInt (so, "i", -1);

                            if (i < 0 || i >= pat.lengthSteps)
                                continue;

                            auto& s = steps[(size_t) i];
                            s.on = true;
                            s.velocity = juce::jlimit (1, 127, getInt (so, "vel", 100));
                            s.roll = sanitiseRoll (getInt (so, "roll", 1));
                        }
                    }

                    if (auto* list = lane.value["notes"].getArray())
                    {
                        auto& notes = pat.notes[channelId];

                        for (auto& no : *list)
                        {
                            Note n;
                            n.id = getInt (no, "id", 0);
                            n.start = getInt (no, "t", 0);
                            n.length = getInt (no, "len", ticksPerStep);
                            n.key = getInt (no, "key", 60);
                            n.velocity = getInt (no, "vel", 100);
                            n.slide = getBool (no, "slide", false);
                            n.muted = getBool (no, "muted", false);
                            notes.push_back (n);
                        }
                    }
                }
            }

            p.patterns.push_back (pat);
        }
    }

    if (auto* tracks = root["playlist"]["tracks"].getArray())
    {
        for (auto& to : *tracks)
        {
            PlaylistTrack t;
            t.id = getString (to, "id");
            t.name = getString (to, "name", "Raia");
            t.recordTrack = getString (to, "recordTrack");   // gravar voz (21/10); o normalise confere
            t.colour = getString (to, "colour");             // bloco 4 (23/10): cor, mudo e solo da raia; opcionais
            t.muted = getBool (to, "muted", false);
            t.solo = getBool (to, "solo", false);

            if (t.id.isNotEmpty() && p.findPlaylistTrack (t.id) == nullptr)
                p.playlistTracks.push_back (t);
        }
    }

    if (auto* items = root["playlist"]["items"].getArray())
    {
        for (auto& io : *items)
        {
            PlaylistItem i;
            i.id = getString (io, "id");
            i.kind = getString (io, "kind", "pattern");
            i.ref = getString (io, "ref");
            i.track = getString (io, "track");
            i.start = getInt (io, "start", 0);
            i.length = getInt (io, "length", ticksPerBar4);
            // Até o esquema 2 o corte era em ticks, que o app convertia no BPM do projeto: a mesma
            // conversão aqui faz o projeto antigo abrir tocando o mesmo trecho.
            i.offsetSeconds = version >= 3 ? getDouble (io, "offsetSeconds", 0.0)
                                           : ticksToSeconds (getInt (io, "offset", 0), p.bpm);
            i.patternOffset = getInt (io, "patternOffset", 0);
            i.muted = getBool (io, "muted", false);
            i.stretch = getDouble (io, "stretch", 1.0);   // opcionais do esticar (21/10); o normalise limita
            i.tape = getBool (io, "tape", false);
            i.takeGroup = getString (io, "takeGroup");   // takes e comping (gravar voz, 21/10); o normalise confere
            i.take = getInt (io, "take", 0);
            i.gainDb = (float) getDouble (io, "gainDb", 0.0);   // ganho e fades do bloco (gravar voz); o normalise limita
            i.fadeIn = getDouble (io, "fadeIn", 0.0);
            i.fadeOut = getDouble (io, "fadeOut", 0.0);

            if (i.id.isNotEmpty() && p.findItem (i.id) == nullptr)
                p.playlistItems.push_back (i);
        }
    }

    if (auto* marks = root["playlist"]["markers"].getArray())   // marcadores (bloco 1, 22/10); o normalise confere
    {
        for (auto& mo : *marks)
        {
            const double tick = getDouble (mo, "t", -1.0);
            p.markers.push_back ({ getString (mo, "id"), std::isfinite (tick) && tick >= 0.0 && tick <= 1.0e9 ? (int) tick : -1,
                                   getString (mo, "name") });
        }
    }

    if (auto* lanes = root["automation"].getArray())
    {
        for (auto& lo : *lanes)
        {
            AutomationLane lane;
            lane.id = getString (lo, "id");
            const auto& to = lo["target"];
            lane.target.kind = getString (to, "kind");
            lane.target.track = getString (to, "track");
            lane.target.channel = getString (to, "channel");
            lane.target.insert = getString (to, "insert");
            lane.target.param = getString (to, "param");

            if (auto* points = lo["points"].getArray())
            {
                for (auto& pt : *points)
                {
                    const double tick = getDouble (pt, "t", -1.0), value = getDouble (pt, "v", 0.0);

                    if (std::isfinite (tick) && std::isfinite (value))   // tick absurdo vira -1 e o normalise tira
                        lane.points.push_back ({ tick >= 0.0 && tick <= 1.0e9 ? (int) tick : -1, (float) value });
                }
            }

            p.automation.push_back (lane);
        }
    }

    if (auto* links = root["midiMap"].getArray())   // MIDI learn (bloco 4d); o normalise confere os alvos
        for (auto& lo : *links)
            p.midiMap.push_back ({ getInt (lo, "channel", 0), getInt (lo, "cc", -1), getString (lo, "kind"), getString (lo, "track"),
                                   getString (lo, "insert"), getString (lo, "param") });

    normalise (p);
    out = std::move (p);
    return juce::Result::ok();
}

//==============================================================================
juce::File keepOlderSchema (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};

    // O .wnl que este app grava começa sempre com "format" e "schemaVersion" (o toJson escreve nessa ordem):
    // se o começo já diz o esquema atual, não há o que guardar, e salvar não precisa ler e montar o arquivo
    // inteiro de novo (fluidez, 21/10). Qualquer outro começo cai na leitura completa, como antes.
    {
        juce::FileInputStream in (file);
        char head[256] = {};
        const int got = in.openedOk() ? in.read (head, (int) sizeof (head) - 1) : 0;
        const auto start = juce::String::fromUTF8 (head, juce::jmax (0, got));

        if (start.contains ("\"format\": \"" + juce::String (formatName) + "\",\n  \"schemaVersion\": " + juce::String (currentSchemaVersion) + ",\n"))
            return {};
    }

    // O arquivo que vai ser sobrescrito pode ser qualquer coisa: lido com o teto de aninhamento.
    juce::var old;
    const int version = parseJsonSafely (file.loadFileAsString(), old).wasOk()
                            ? getInt (old, "schemaVersion", currentSchemaVersion) : currentSchemaVersion;

    if (version < 1 || version >= currentSchemaVersion)
        return {};

    const auto backup = file.getParentDirectory().getChildFile ("history")
                            .getChildFile (file.getFileNameWithoutExtension() + " (esquema " + juce::String (version) + ").wnl");

    if (! backup.existsAsFile() && backup.getParentDirectory().createDirectory())
        file.copyFileTo (backup);

    return backup;
}

juce::Result save (Project& p, const juce::File& file, bool collectSamples, const juce::File& previousFolder)
{
    const auto folder = file.getParentDirectory();

    if (! folder.createDirectory())
        return juce::Result::fail ("Não consegui criar a pasta "_t + folder.getFullPathName() + "." + noPermissionHint (folder, "salve"));

    keepOlderSchema (file);   // o app antigo não abre o arquivo convertido: o original fica em history/

    if (collectSamples)
    {
        const auto samplesDir = folder.getChildFile ("samples");

        // as camadas por força (bloco 4f) vão junto, como o sample do canal (sem o originalPath)
        for (auto& c : p.channels)
        {
            for (auto& l : c.layers)
            {
                const juce::File src = juce::File::isAbsolutePath (l.samplePath)
                                         ? juce::File (l.samplePath)
                                         : (previousFolder != juce::File() ? previousFolder : folder).getChildFile (l.samplePath);

                if (! src.existsAsFile())
                    continue;

                if (src.isAChildOf (folder))
                {
                    l.samplePath = relativeTo (src, folder);
                    continue;
                }

                if (! samplesDir.createDirectory())
                    return juce::Result::fail ("Não consegui criar a pasta de samples em "_t + folder.getFullPathName() + "."
                                               + noPermissionHint (folder, "salve"));

                auto dest = samplesDir.getChildFile (src.getFileName());

                if (dest.existsAsFile() && ! dest.hasIdenticalContentTo (src))
                    dest = samplesDir.getNonexistentChildFile (src.getFileNameWithoutExtension(), src.getFileExtension(), false);

                if (! dest.existsAsFile() && ! src.copyFileTo (dest))
                    return juce::Result::fail ("Não consegui copiar o sample "_t + src.getFullPathName());

                l.samplePath = relativeTo (dest, folder);
            }
        }

        for (auto& c : p.channels)
        {
            if (c.samplePath.isEmpty())
                continue;

            juce::File src = juce::File::isAbsolutePath (c.samplePath)
                               ? juce::File (c.samplePath)
                               : (previousFolder != juce::File() ? previousFolder : folder).getChildFile (c.samplePath);

            if (! src.existsAsFile())
                continue;   // sample faltando: mantém a referência como está

            if (src.isAChildOf (folder))
            {
                c.samplePath = relativeTo (src, folder);
                continue;
            }

            if (! samplesDir.createDirectory())
                return juce::Result::fail ("Não consegui criar a pasta de samples em "_t + folder.getFullPathName() + "."
                                           + noPermissionHint (folder, "salve"));

            auto dest = samplesDir.getChildFile (src.getFileName());

            // mesmo nome com conteúdo diferente: gera um nome novo em vez de sobrescrever
            if (dest.existsAsFile() && ! dest.hasIdenticalContentTo (src))
                dest = samplesDir.getNonexistentChildFile (src.getFileNameWithoutExtension(), src.getFileExtension(), false);

            if (! dest.existsAsFile() && ! src.copyFileTo (dest))
                return juce::Result::fail ("Não consegui copiar o sample "_t + src.getFullPathName());

            if (c.originalPath.isEmpty())
                c.originalPath = src.getFullPathName().replaceCharacter ('\\', '/');

            c.samplePath = relativeTo (dest, folder);
        }
    }

    juce::TemporaryFile temp (file);

    if (! temp.getFile().replaceWithText (toJson (p), false, false, "\n"))
        return juce::Result::fail ("Não consegui gravar o projeto em "_t + folder.getFullPathName()
                                   + " (o disco pode estar cheio ou a pasta sem permissão)." + noPermissionHint (folder, "salve"));

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Não consegui substituir "_t + file.getFullPathName()
                                   + ". Se ele estiver aberto em outro programa, feche e salve de novo." + noPermissionHint (folder, "salve"));

    return juce::Result::ok();
}

juce::Result load (const juce::File& file, Project& out)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("Arquivo não encontrado: "_t + file.getFullPathName());

    return fromJson (file.loadFileAsString(), out);
}

} // namespace whynot::wnl
