#include "CommandRegistry.h"
#include "../model/NoteTools.h"
#include "../model/WnlFormat.h"
#include "../engine/MidiImport.h"
#include "../engine/SampleAnalysis.h"

namespace whynot
{

namespace
{
    using params::schema;

    Pattern* targetPattern (Project& p, const juce::var& args)
    {
        const auto id = params::string (args, "pattern");
        return id.isEmpty() ? p.currentPatternPtr() : p.findPattern (id);
    }

    /** Converte um valor na unidade pedida ("ticks", "steps" ou "beats") para ticks. */
    int toTicks (const juce::var& args, double value)
    {
        const auto unit = params::string (args, "unit", "ticks");
        const double scale = unit == "steps" ? ticksPerStep : unit == "beats" ? ticksPerBeat : unit == "bars" ? ticksPerBar4 : 1.0;
        const double ticks = value * scale;

        // limitado antes de virar int: número absurdo vindo de um comando (ou da IA) não estoura
        if (! std::isfinite (ticks))
            return 0;

        return juce::roundToInt (juce::jlimit ((double) -maxTimelineTicks, (double) maxTimelineTicks, ticks));
    }

    double fieldNumber (const juce::var& o, const char* key, double fallback)
    {
        if (! o.isObject() || ! o.hasProperty (key))
            return fallback;

        const auto& v = o[key];
        return (v.isDouble() || v.isInt() || v.isInt64()) ? (double) v : v.toString().getDoubleValue();
    }

    /** Aceita "C", "C#", "Db"... ou um número de 0 a 11. */
    int parseRoot (const juce::var& v, bool& ok)
    {
        ok = true;

        if (v.isInt() || v.isDouble() || v.isInt64())
            return (((int) v % 12) + 12) % 12;

        static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* flats[]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        const auto s = v.toString().trim();

        for (int i = 0; i < 12; ++i)
            if (s.equalsIgnoreCase (sharps[i]) || s.equalsIgnoreCase (flats[i]))
                return i;

        ok = false;
        return 0;
    }

    /** O padrão cresce, de compasso em compasso, até caber a última nota. */
    void growPatternToFit (Pattern& pat)
    {
        int end = 0;

        for (auto& [id, notes] : pat.notes)
            for (auto& n : notes)
                end = juce::jmax (end, n.end());

        const int stepsNeeded = (end + ticksPerStep - 1) / ticksPerStep;

        if (stepsNeeded > pat.lengthSteps)
            pat.lengthSteps = juce::jmin (256, ((stepsNeeded + 15) / 16) * 16);
    }

    int nextNoteId (const std::vector<Note>& notes)
    {
        int maxId = 0;

        for (auto& n : notes)
            maxId = juce::jmax (maxId, n.id);

        return maxId + 1;
    }

    juce::String resolvePlaylistTrack (const Project& p, const juce::var& v)
    {
        if (v.isInt() || v.isDouble() || v.isInt64())
        {
            const int index = (int) v - 1;   // 1 = primeira raia
            return juce::isPositiveAndBelow (index, (int) p.playlistTracks.size()) ? p.playlistTracks[(size_t) index].id : juce::String();
        }

        const auto id = v.toString();
        return p.findPlaylistTrack (id) != nullptr ? id : juce::String();
    }

    void insertBeforeMaster (Project& p, MixerTrack t)
    {
        auto it = std::find_if (p.mixer.begin(), p.mixer.end(), [] (const MixerTrack& m) { return m.isMaster(); });
        p.mixer.insert (it, std::move (t));
    }

    /** Cria um canal de áudio (dono de clipes na playlist) com a própria trilha de mixer. */
    Channel& addAudioChannel (Project& p, const juce::File& file, const juce::String& name)
    {
        Channel ch;
        ch.id = makeId ("ch");
        ch.kind = "audio";
        ch.name = name.isNotEmpty() ? name : file.getFileNameWithoutExtension();
        ch.colour = channelColourForIndex ((int) p.channels.size());
        ch.samplePath = file.getFullPathName().replaceCharacter ('\\', '/');

        MixerTrack mx;
        mx.id = makeId ("mx");
        mx.name = ch.name;
        mx.colour = ch.colour;
        mx.autoFor = ch.id;
        mx.volumeDb = -6.0f;
        ch.output = mx.id;

        p.channels.push_back (ch);
        insertBeforeMaster (p, mx);
        return p.channels.back();
    }

    juce::var idList (const juce::Array<juce::var>& ids, const char* key)
    {
        auto o = new juce::DynamicObject();
        o->setProperty (key, ids);
        return juce::var (o);
    }
}

void registerCompositionCommands (CommandRegistry& r)
{
    //==========================================================================
    // Piano roll
    r.add ({ "pianoroll.addNotes", "Adicionar notas",
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "notes", "array", true, "Lista de { t, len, key, vel?, slide?, muted? }: início, duração, nota MIDI (60 = Dó central), força 1-127, slide do 808, nota muda"_t },
                       { "unit", "string", false, "Unidade de t e len: ticks (960 por tempo, padrão), steps, beats ou bars"_t },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = targetPattern (p, a);
                 const auto channel = params::string (a, "channel");
                 auto* list = a["notes"].getArray();

                 if (pat == nullptr)                    return CommandResult::fail ("Padrão não encontrado."_t);
                 if (p.findChannel (channel) == nullptr) return CommandResult::fail ("Canal não encontrado: "_t + channel);
                 if (p.findChannel (channel)->isAudio()) return CommandResult::fail ("Canal de áudio não tem notas; use a playlist."_t);
                 if (list == nullptr || list->isEmpty()) return CommandResult::fail ("Informe pelo menos uma nota em \"notes\"."_t);

                 auto& notes = pat->notes[channel];
                 juce::Array<juce::var> ids;

                 for (auto& no : *list)
                 {
                     Note n;
                     n.id = nextNoteId (notes);
                     n.start = juce::jmax (0, toTicks (a, fieldNumber (no, "t", 0)));
                     n.length = juce::jmax (1, toTicks (a, fieldNumber (no, "len", ticksPerStep)));
                     n.key = juce::jlimit (0, 127, (int) fieldNumber (no, "key", 60));
                     n.velocity = juce::jlimit (1, 127, (int) fieldNumber (no, "vel", 100));
                     n.slide = no.isObject() && no.hasProperty ("slide") && (bool) no["slide"];
                     n.muted = no.isObject() && no.hasProperty ("muted") && (bool) no["muted"];
                     notes.push_back (n);
                     ids.add (n.id);
                 }

                 growPatternToFit (*pat);
                 return CommandResult::success (idList (ids, "ids"));
             } });

    r.add ({ "pianoroll.updateNotes", "Editar notas",
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "notes", "array", true, "Lista de { id, t?, len?, key?, vel?, slide?, muted? } com só o que muda"_t },
                       { "unit", "string", false, "Unidade de t e len: ticks (padrão), steps, beats ou bars"_t },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = targetPattern (p, a);
                 const auto channel = params::string (a, "channel");
                 auto* list = a["notes"].getArray();

                 if (pat == nullptr)  return CommandResult::fail ("Padrão não encontrado."_t);
                 if (list == nullptr) return CommandResult::fail ("Informe as notas em \"notes\"."_t);

                 auto& notes = pat->notes[channel];

                 for (auto& no : *list)
                 {
                     const int id = (int) fieldNumber (no, "id", -1);
                     auto it = std::find_if (notes.begin(), notes.end(), [id] (const Note& n) { return n.id == id; });

                     if (it == notes.end())
                         return CommandResult::fail ("Nota não encontrada: "_t + juce::String (id));

                     if (no.hasProperty ("t"))     it->start = juce::jmax (0, toTicks (a, fieldNumber (no, "t", 0)));
                     if (no.hasProperty ("len"))   it->length = juce::jmax (1, toTicks (a, fieldNumber (no, "len", 1)));
                     if (no.hasProperty ("key"))   it->key = juce::jlimit (0, 127, (int) fieldNumber (no, "key", it->key));
                     if (no.hasProperty ("vel"))   it->velocity = juce::jlimit (1, 127, (int) fieldNumber (no, "vel", it->velocity));
                     if (no.hasProperty ("slide")) it->slide = (bool) no["slide"];
                     if (no.hasProperty ("muted")) it->muted = (bool) no["muted"];
                 }

                 growPatternToFit (*pat);
                 return CommandResult::success();
             } });

    r.add ({ "pianoroll.removeNotes", "Apagar notas",
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "ids", "array", true, "Ids das notas" },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* pat = targetPattern (c.model.data(), a);
                 auto* ids = a["ids"].getArray();

                 if (pat == nullptr || ids == nullptr)
                     return CommandResult::fail ("Informe o padrão e os ids das notas."_t);

                 auto& notes = pat->notes[params::string (a, "channel")];
                 std::erase_if (notes, [ids] (const Note& n) { return ids->contains (juce::var (n.id)); });
                 return CommandResult::success();
             } });

    r.add ({ "pianoroll.clear", "Limpar piano roll",
             schema ({ { "channel", "string", true, "Id do canal" }, { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* pat = targetPattern (c.model.data(), a);

                 if (pat == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 pat->notes.erase (params::string (a, "channel"));
                 return CommandResult::success();
             } });

    r.add ({ "pianoroll.stepsToNotes", "Passar passos para o piano roll",
             schema ({ { "channel", "string", true, "Id do canal" }, { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = targetPattern (p, a);
                 const auto channel = params::string (a, "channel");
                 auto* ch = p.findChannel (channel);

                 if (pat == nullptr || ch == nullptr)
                     return CommandResult::fail ("Canal ou padrão não encontrado."_t);

                 auto& steps = pat->steps[channel];
                 auto& notes = pat->notes[channel];
                 const int swing = ch->swingTicks();   // as notas ficam onde os passos soavam

                 for (size_t i = 0; i < steps.size(); ++i)
                 {
                     const auto& s = steps[i];

                     if (! s.on)
                         continue;

                     const int sub = ticksPerStep / s.roll;

                     for (int k = 0; k < s.roll; ++k)
                     {
                         Note n;
                         n.id = nextNoteId (notes);
                         n.start = (int) i * ticksPerStep + k * sub + (i % 2 == 1 ? swing : 0);
                         n.length = sub;
                         n.key = ch->rootKey;
                         n.velocity = s.velocity;
                         notes.push_back (n);
                     }
                 }

                 std::fill (steps.begin(), steps.end(), Step{});
                 return CommandResult::success();
             } });

    // As ferramentas do piano roll que o FL tem (bloco 1, 22/10): src/model/NoteTools.h
    r.add ({ "pianoroll.transform", "Ferramenta do piano roll"_t,
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "op", "string", true, "legato (cada nota até a próxima), glue (junta notas da mesma tecla que se encostam), "
                                                "mute, unmute, humanize (tempo e força ao acaso), toScale (leva para a escala do projeto) "
                                                "ou strum (dedilha os acordes, da grave para a aguda)"_t },
                       { "ids", "array", false, "Ids das notas; ausente = todas as notas do canal no padrão"_t },
                       { "amount", "number", false, "humanize: de 0 a 1 (padrão 0,5 = até 20 ticks e 10 de força); strum: de 0 a 1 (0,5 = 40 ticks entre as notas)"_t },
                       { "seed", "integer", false, "humanize: semente do acaso (o mesmo número dá o mesmo resultado)"_t },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = targetPattern (p, a);
                 const auto channel = params::string (a, "channel");
                 const auto op = params::string (a, "op");

                 if (pat == nullptr)                     return CommandResult::fail ("Padrão não encontrado."_t);
                 if (p.findChannel (channel) == nullptr) return CommandResult::fail ("Canal não encontrado: "_t + channel);

                 std::set<int> ids;

                 if (auto* list = a["ids"].getArray())
                     for (auto& id : *list)
                         ids.insert ((int) id);

                 auto& notes = pat->notes[channel];
                 int changed = 0;
                 juce::Array<juce::var> removed;

                 if (op == "legato")       changed = notetools::legato (notes, ids);
                 else if (op == "glue")    for (int id : notetools::glue (notes, ids)) removed.add (id);
                 else if (op == "mute")    changed = notetools::setMuted (notes, ids, true);
                 else if (op == "unmute")  changed = notetools::setMuted (notes, ids, false);
                 else if (op == "toScale") changed = notetools::toScale (notes, ids, p.keyRoot, Project::scaleDegrees (p.scale));
                 else if (op == "strum")   changed = notetools::strum (notes, ids, juce::roundToInt (juce::jlimit (0.0, 1.0, params::number (a, "amount", 0.5)) * 80.0));
                 else if (op == "humanize")
                 {
                     const double amount = juce::jlimit (0.0, 1.0, params::number (a, "amount", 0.5));
                     juce::Random rng;

                     if (params::has (a, "seed"))
                         rng.setSeed ((juce::int64) params::number (a, "seed", 0));

                     changed = notetools::humanize (notes, ids, juce::roundToInt (amount * 40.0), juce::roundToInt (amount * 20.0), rng);
                 }
                 else
                 {
                     return CommandResult::fail ("Ferramenta desconhecida: "_t + op + ". Use legato, glue, mute, unmute, humanize, toScale ou strum."_t);
                 }

                 std::stable_sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y) { return x.start < y.start; });
                 growPatternToFit (*pat);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("changed", changed + removed.size());
                 o->setProperty ("removed", removed);
                 return CommandResult::success (juce::var (o));
             } });

    // Importar MIDI (bloco 1, 22/10; o que faltou do pedido do produtor, no mesmo dia): as notas de um .mid (pacote
    // de acordes, melodia) no canal, no padrão atual. A leitura fica no engine/MidiImport; aqui, trocar ou somar.
    r.add ({ "pianoroll.importMidi", "Importar MIDI"_t,
             schema ({ { "channel", "string", true, "Id do canal que vai tocar as notas" },
                       { "path", "string", true, "Caminho completo do arquivo .mid"_t },
                       { "replace", "boolean", false, "true (padrão): troca o que o canal toca no padrão; false: soma às notas que já estão lá"_t },
                       { "track", "integer", false, "0 (padrão): todas as trilhas do arquivo; N: só a N-ª da lista do pianoroll.inspectMidi"_t },
                       { "at", "number", false, "Onde entra o primeiro compasso com notas do arquivo; padrão 0 (o começo do padrão)"_t },
                       { "unit", "string", false, "Unidade do at: ticks (960 por tempo, padrão), steps, beats ou bars"_t },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = targetPattern (p, a);
                 const auto channel = params::string (a, "channel");
                 const auto path = params::string (a, "path");
                 const auto* ch = p.findChannel (channel);

                 if (pat == nullptr)                       return CommandResult::fail ("Padrão não encontrado."_t);
                 if (ch == nullptr)                        return CommandResult::fail ("Canal não encontrado: "_t + channel);
                 if (ch->isAudio())                        return CommandResult::fail ("Canal de áudio não tem notas; escolha um instrumento."_t);
                 if (! juce::File::isAbsolutePath (path))  return CommandResult::fail ("Informe o caminho completo do arquivo MIDI."_t);

                 MidiImportOptions options;
                 options.track = (int) juce::jlimit (-1.0, 10000.0, params::number (a, "track", 0));
                 options.at = toTicks (a, params::number (a, "at", 0));
                 options.projectBpm = p.bpm;

                 if (options.at < 0 || options.at >= options.limitTicks)
                     return CommandResult::fail ("O começo (at) fica fora do padrão, que vai até o compasso 16."_t);

                 const juce::File file (path);
                 MidiImportResult imported;

                 if (const auto read = readMidiNotes (file, options, imported); read.failed())
                     return CommandResult::fail (read.getErrorMessage());

                 if (imported.notes.empty())
                     return CommandResult::fail ("Nenhuma nota de "_t + file.getFileName() + " coube no padrão, que vai até o compasso 16."_t);

                 auto& notes = pat->notes[channel];

                 if (params::boolean (a, "replace", true))
                 {
                     notes.clear();

                     if (auto it = pat->steps.find (channel); it != pat->steps.end())
                         std::fill (it->second.begin(), it->second.end(), Step{});
                 }

                 int id = nextNoteId (notes);

                 for (auto n : imported.notes)
                 {
                     n.id = id++;
                     notes.push_back (n);
                 }

                 std::stable_sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y) { return x.start < y.start; });
                 growPatternToFit (*pat);

                 const auto& info = imported.info;
                 auto o = new juce::DynamicObject();
                 o->setProperty ("notes", (int) imported.notes.size());
                 o->setProperty ("dropped", imported.dropped);
                 o->setProperty ("fileBpm", info.bpm);
                 o->setProperty ("lengthSteps", pat->lengthSteps);
                 o->setProperty ("tracks", (int) info.tracks.size());
                 o->setProperty ("track", options.track > 0 ? info.tracks[(size_t) options.track - 1].name : juce::String());
                 return CommandResult::success (juce::var (o));
             } });

    // O que tem num .mid antes de importar: as trilhas com notas (o track do pianoroll.importMidi), o andamento e o compasso.
    r.add ({ "pianoroll.inspectMidi", "Ver um arquivo MIDI"_t,
             schema ({ { "path", "string", true, "Caminho completo do arquivo .mid"_t } }), false,
             [] (CommandContext&, const juce::var& a)
             {
                 const auto path = params::string (a, "path");

                 if (! juce::File::isAbsolutePath (path))
                     return CommandResult::fail ("Informe o caminho completo do arquivo MIDI."_t);

                 MidiFileInfo info;

                 if (const auto read = readMidiInfo (juce::File (path), info); read.failed())
                     return CommandResult::fail (read.getErrorMessage());

                 juce::Array<juce::var> tracks;

                 for (size_t i = 0; i < info.tracks.size(); ++i)
                 {
                     const auto& t = info.tracks[i];
                     auto o = new juce::DynamicObject();
                     o->setProperty ("track", (int) i + 1);
                     o->setProperty ("name", t.name);
                     o->setProperty ("notes", t.notes);
                     o->setProperty ("midiChannel", t.midiChannel);   // 0 = a trilha inteira
                     o->setProperty ("lowKey", t.lowKey);
                     o->setProperty ("highKey", t.highKey);
                     tracks.add (juce::var (o));
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("format", info.format);
                 o->setProperty ("ticksPerBeat", info.ticksPerBeat);   // 0 = SMPTE
                 o->setProperty ("bpm", info.bpm);
                 o->setProperty ("timeSigNum", info.timeSigNum);
                 o->setProperty ("timeSigDen", info.timeSigDen);
                 o->setProperty ("notes", info.totalNotes());
                 o->setProperty ("tracks", tracks);
                 return CommandResult::success (juce::var (o));
             } });

    //==========================================================================
    // Tonalidade e modo
    r.add ({ "project.setKey", "Tonalidade",
             schema ({ { "root", "string", true, "Tônica: C, C#, D... ou 0 a 11"_t },
                       { "scale", "string", false, "minor, major, harmonicMinor, phrygian, dorian, pentatonicMinor ou chromatic" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 bool ok = false;
                 const int root = parseRoot (a["root"], ok);

                 if (! ok)
                     return CommandResult::fail ("Tônica inválida. Use C, C#, D... ou um número de 0 a 11."_t);

                 const auto scale = params::string (a, "scale", c.model.get().scale);

                 if (! Project::scaleNames().contains (scale))
                     return CommandResult::fail ("Escala desconhecida: "_t + scale);

                 c.model.data().keyRoot = root;
                 c.model.data().scale = scale;

                 // A afinação e a harmonia (efeitos "tune" e "harmony") com "seguir o tom do projeto" ligado (o padrão)
                 // mudam junto, no mesmo passo de desfazer. Quem escolheu o tom na mão fica como está.
                 for (auto& m : c.model.data().mixer)
                     for (auto& ins : m.inserts)
                         if (ins.type == "tune" || ins.type == "harmony")
                             if (auto follow = ins.params.find ("follow"); follow == ins.params.end() || follow->second >= 0.5f)
                             {
                                 ins.params["key"] = (float) root;
                                 ins.params["scale"] = (float) Project::scaleNames().indexOf (scale);
                             }

                 return CommandResult::success();
             } });

    r.add ({ "transport.setSongMode", "Modo song",
             schema ({ { "on", "boolean", false, "true = toca a playlist (SONG); false = toca o padrão (PAT); ausente = alterna"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 c.app.stop();
                 c.model.data().songMode = params::boolean (a, "on", ! c.model.get().songMode);
                 return CommandResult::success();
             } });

    //==========================================================================
    // Canais
    auto channelFlag = [&r] (const char* id, const juce::String& title, const juce::String& help, bool Channel::* member)
    {
        r.add ({ id, title,
                 schema ({ { "channel", "string", true, "Id do canal" }, { "on", "boolean", false, help } }), true,
                 [member] (CommandContext& c, const juce::var& a)
                 {
                     auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                     if (ch == nullptr)
                         return CommandResult::fail ("Canal não encontrado."_t);

                     ch->*member = params::boolean (a, "on", ! (ch->*member));
                     return CommandResult::success();
                 } });
    };

    channelFlag ("channel.setMono", "Mono (glide)", "true = uma nota por vez, notas com slide deslizam; ausente = alterna", &Channel::mono);
    channelFlag ("channel.setOneShot", "Tocar o sample inteiro", "true = ignora a duração da nota; false = respeita (808)"_t, &Channel::oneShot);

    r.add ({ "channel.setChoke", "Grupo de corte (choke)",
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "group", "integer", true, "0 = nenhum; 1 a 8: uma nota de um canal do grupo corta os outros do grupo"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 const double group = params::number (a, "group", 0);

                 if (! std::isfinite (group) || group < 0 || group > 8)
                     return CommandResult::fail ("O grupo vai de 0 (nenhum) a 8."_t);

                 ch->chokeGroup = (int) group;
                 return CommandResult::success();
             } });

    r.add ({ "channel.setSwing", "Swing do canal",
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "amount", "number", true, "0 a 100 %: atrasa os passos ímpares (100 % = sensação de tercina)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 const double amount = params::number (a, "amount", 0);

                 if (! std::isfinite (amount))
                     return CommandResult::fail ("Swing inválido."_t);

                 ch->swing = (float) juce::jlimit (0.0, 100.0, amount);
                 return CommandResult::success();
             } });

    r.add ({ "channel.setGlide", "Tempo do glide",
             schema ({ { "channel", "string", true, "Id do canal" }, { "ms", "number", true, "Duração do slide em milissegundos (0 a 2000)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 ch->glideMs = (float) juce::jlimit (0.0, 2000.0, params::number (a, "ms", ch->glideMs));
                 return CommandResult::success();
             } });

    r.add ({ "channel.setSampler", "Forma da nota do sampler",
             schema ({ { "channel", "string", true, "Id do canal (sampler)" },
                       { "rootKey", "integer", false, "Nota em que o sample soa no tom original (0 a 127)"_t },
                       { "rootCents", "number", false, "Quanto o sample está fora de rootKey, em cents (-50 a 50)"_t },
                       { "tune", "number", false, "Afinação do canal em semitons, com fração (-24 a 24; 0,5 = 50 cents)"_t },
                       { "attackMs", "number", false, "Ataque em ms (0 a 1000)"_t },
                       { "releaseMs", "number", false, "Saída quando a nota acaba, em ms (1 a 5000; vale no canal que respeita a nota)"_t },
                       { "decayMs", "number", false, "Tempo do decaimento até o sustain, em ms (1 a 20000)"_t },
                       { "sustain", "number", false, "Nível depois do decaimento (0 a 1; 1 = sem decaimento, 0 = a nota some)"_t },
                       { "pitchDrop", "number", false, "A nota começa tantos semitons acima e cai até ela (-48 a 48)"_t },
                       { "pitchDropMs", "number", false, "Tempo da queda de tom em ms (1 a 5000)"_t },
                       { "start", "number", false, "Onde a leitura do sample começa (0 a 0,99)"_t },
                       { "loop", "boolean", false, "Laço: enquanto a nota soa, volta do fim do laço para o começo dele (a nota mais longa que o sample continua)"_t },
                       { "loopStart", "number", false, "Começo do laço, em fração do sample (0 a 1)"_t },
                       { "loopEnd", "number", false, "Fim do laço, em fração do sample (0 a 1, depois do começo)"_t },
                       { "loopXfadeMs", "number", false, "Crossfade antes do fim do laço, em ms (0 a 500)"_t },
                       { "loopAuto", "boolean", false, "Acha o laço sozinho no corpo do som (pontos em fase) e liga o laço"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 if (ch->kind != "sampler")
                     return CommandResult::fail ("Só canais do sampler têm essa forma de nota."_t);

                 auto value = [&a] (const char* key, float current, float lo, float hi)
                 {
                     const double v = params::number (a, key, current);
                     return std::isfinite (v) ? (float) juce::jlimit ((double) lo, (double) hi, v) : current;
                 };

                 if (params::has (a, "rootKey"))
                 {
                     const double key = params::number (a, "rootKey", ch->rootKey);   // limita antes de converter: NaN ou 1e300 em int é indefinido
                     if (std::isfinite (key))
                         ch->rootKey = juce::roundToInt (juce::jlimit (0.0, 127.0, key));
                 }
                 ch->rootCents   = value ("rootCents",   ch->rootCents,   -50.0f, 50.0f);
                 ch->tune        = value ("tune",        ch->tune,        -24.0f, 24.0f);
                 ch->attackMs    = value ("attackMs",    ch->attackMs,    0.0f, 1000.0f);
                 ch->releaseMs   = (float) (std::round (value ("releaseMs", ch->releaseMs, 1.0f, 5000.0f) * 10.0) / 10.0);
                 ch->decayMs     = value ("decayMs",     ch->decayMs,     1.0f, 20000.0f);
                 ch->sustain     = value ("sustain",     ch->sustain,     0.0f, 1.0f);
                 ch->pitchDrop   = value ("pitchDrop",   ch->pitchDrop,   -48.0f, 48.0f);
                 ch->pitchDropMs = value ("pitchDropMs", ch->pitchDropMs, 1.0f, 5000.0f);
                 ch->start       = value ("start",       ch->start,       0.0f, 0.99f);

                 // Pontos do laço com 6 casas, as mesmas do .wnl: salvar e reabrir dá o mesmo laço.
                 auto point = [] (float v) { return (float) (std::round ((double) v * 1.0e6) / 1.0e6); };
                 ch->loop        = params::boolean (a, "loop", ch->loop);
                 ch->loopStart   = point (value ("loopStart", ch->loopStart, 0.0f, 1.0f));
                 ch->loopEnd     = point (value ("loopEnd",   ch->loopEnd,   0.0f, 1.0f));
                 ch->loopXfadeMs = (float) (std::round (value ("loopXfadeMs", ch->loopXfadeMs, 0.0f, 500.0f) * 10.0) / 10.0);

                 if (ch->loopEnd < ch->loopStart)
                     std::swap (ch->loopStart, ch->loopEnd);

                 if (params::boolean (a, "loopAuto", false))
                 {
                     const auto found = suggestLoop (c.model.resolveSample (ch->samplePath));

                     if (! found.found)
                         return CommandResult::fail ("Não deu para achar um laço neste sample (curto demais ou sem corpo depois do ataque)."_t);

                     ch->loop = true;
                     ch->loopStart = point (found.start);
                     ch->loopEnd = point (found.end);
                     ch->loopXfadeMs = (float) (std::round (found.xfadeMs * 10.0) / 10.0);
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("loop", ch->loop);
                 o->setProperty ("loopStart", ch->loopStart);
                 o->setProperty ("loopEnd", ch->loopEnd);
                 o->setProperty ("loopXfadeMs", ch->loopXfadeMs);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "channel.detectRoot", "Achar a nota do sample",
             schema ({ { "channel", "string", true, "Id do canal (sampler)" },
                       { "apply", "boolean", false, "Grava a nota achada como raiz do canal (padrão true)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 if (ch->kind != "sampler" || ch->samplePath.isEmpty())
                     return CommandResult::fail ("O canal não tem sample para analisar."_t);

                 const auto e = detectRootOf (c.model.resolveSample (ch->samplePath));
                 const bool reliable = rootIsReliable (e);

                 if (reliable && params::boolean (a, "apply", true))
                 {
                     ch->rootKey = e.key;
                     ch->rootCents = (float) e.cents;
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("found", reliable);
                 o->setProperty ("rootKey", reliable ? e.key : ch->rootKey);
                 o->setProperty ("note", reliable ? Project::keyName (e.key) : juce::String());
                 o->setProperty ("cents", reliable ? std::round (e.cents * 10.0) / 10.0 : 0.0);
                 o->setProperty ("hz", reliable ? std::round (e.hz * 100.0) / 100.0 : 0.0);
                 o->setProperty ("confidence", std::round (e.confidence * 100.0) / 100.0);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "channel.addAudio", "Adicionar canal de áudio"_t,
             schema ({ { "path", "string", true, "Arquivo de áudio (WAV, AIFF, FLAC, OGG, MP3)"_t }, { "name", "string", false, "Nome do canal" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const juce::File file (params::string (a, "path"));

                 if (! file.existsAsFile())
                     return CommandResult::fail ("Arquivo não encontrado: "_t + file.getFullPathName());

                 auto& ch = addAudioChannel (c.model.data(), file, params::string (a, "name"));
                 auto o = new juce::DynamicObject();
                 o->setProperty ("channel", ch.id);
                 o->setProperty ("mixerTrack", ch.output);
                 return CommandResult::success (juce::var (o));
             } });

    //==========================================================================
    // Renderizar no lugar (bloco 2, 23/10; o "bounce" do Ableton e o "render to audio" do FL): o que o canal toca na
    // música vira um WAV, num canal de áudio novo na primeira raia vazia, e o canal original fica mudo. O som da
    // música continua o mesmo, e a CPU folga (o instrumento e os efeitos da trilha param de rodar ao vivo).
    r.add ({ "channel.renderInPlace", "Renderizar no lugar"_t,
             schema ({ { "channel", "string", true, "Id do canal (precisa tocar na playlist, no SONG)"_t },
                       { "effects", "boolean", false, "true (padrão): com os efeitos, o volume e o pan da trilha do mixer (a trilha nova "
                                                      "segue para o mesmo lugar, com os mesmos envios); false: seco, só o instrumento"_t },
                       { "mute", "boolean", false, "true (padrão): o canal original fica mudo, e a música soa igual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto id = params::string (a, "channel");
                 const auto* source = p.findChannel (id);

                 if (source == nullptr)
                     return CommandResult::fail ("Canal não encontrado: "_t + id);

                 if (source->mute)
                     return CommandResult::fail ("O canal está mudo: tire o mudo para renderizar o que ele toca."_t);

                 // onde o canal toca na música: os blocos de padrão com notas ou passos dele, e os clipes de áudio dele
                 auto patternPlays = [&id] (const Pattern& pat)
                 {
                     if (auto* list = pat.notesFor (id))
                         if (std::any_of (list->begin(), list->end(), [] (const Note& n) { return ! n.muted; }))
                             return true;

                     auto it = pat.steps.find (id);
                     return it != pat.steps.end() && std::any_of (it->second.begin(), it->second.end(), [] (const Step& s) { return s.on; });
                 };

                 int from = maxTimelineTicks, to = 0;

                 for (auto& i : p.playlistItems)
                 {
                     const auto* pat = i.kind == "audio" ? nullptr : p.findPattern (i.ref);
                     const bool plays = ! i.muted && (i.kind == "audio" ? i.ref == id : pat != nullptr && patternPlays (*pat));

                     if (plays)
                     {
                         from = juce::jmin (from, i.start);
                         to = juce::jmax (to, i.end());
                     }
                 }

                 if (to <= from)
                     return CommandResult::fail ("Este canal não toca na playlist. Coloque o padrão dele na música (SONG) para renderizar no lugar."_t);

                 const bool effects = params::boolean (a, "effects", true);
                 const auto track = p.resolveOutput (source->output);
                 const bool withTrack = effects && track != masterTrackId;
                 const int bar = juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen)) * juce::jmax (1, p.timeSigNum) * ticksPerStep;
                 const int end = juce::jmin (maxTimelineTicks, to + (withTrack ? bar : 0));   // com efeitos, um compasso para o reverb e o delay acabarem

                 // O projeto do render: com efeitos, como a trilha do canal soa sozinha (a conta dos stems), sem o
                 // que vem depois dela; seco, o canal direto no master. O master neutro, sem efeitos nem volume.
                 Project solo = withTrack ? c.app.soloTrackProject (p, track, false) : p;
                 solo.songMode = true;

                 for (auto& other : solo.channels)
                 {
                     if (other.id == id)
                     {
                         if (! withTrack)
                             other.output = masterTrackId;
                         continue;
                     }

                     // só o canal escolhido toca na trilha; quem dispara um sidechain dela de outra trilha continua
                     const auto out = solo.resolveOutput (other.output);

                     if (! withTrack || out == track || solo.reaches (out, track, false))
                         other.mute = true;
                 }

                 for (auto& m : solo.mixer)
                 {
                     m.frozen = false;

                     if (m.isMaster())
                     {
                         m.volumeDb = 0.0f;
                         m.pan = 0.0f;

                         for (auto& ins : m.inserts)
                             ins.bypass = true;
                     }
                     else if (withTrack && m.id == track)
                     {
                         m.output = masterTrackId;   // o que vem depois (um bus) continua ao vivo, na trilha nova
                     }
                 }

                 // a automação do master sai (o master está neutro); a da trilha e a do instrumento ficam no render
                 std::erase_if (solo.automation, [&] (const AutomationLane& l)
                 {
                     return l.target.track == masterTrackId || (! withTrack && l.target.kind != "channel");
                 });

                 const auto base = juce::File::createLegalFileName (source->name.replace (" ", "_") + (effects ? "_render" : "_seco"));
                 const auto out = c.app.recordingsFolder().getNonexistentChildFile (base, ".wav", false);
                 out.getParentDirectory().createDirectory();

                 // o render começa na amostra de antes (a grade de 48 kHz do motor), e a fração vai para o corte do bloco
                 const double fromSeconds = ticksToSeconds (from, p.bpm);
                 const double alignedFrom = std::floor (fromSeconds * 48000.0) / 48000.0;

                 if (const auto rendered = c.app.renderRange (solo, alignedFrom, ticksToSeconds (end, p.bpm), out); rendered.failed())
                     return CommandResult::fail (rendered.getErrorMessage());

                 // o som renderizado: um canal de áudio novo. Com efeitos, numa trilha nova e limpa, que sai para o mesmo
                 // lugar da trilha do canal e manda os mesmos envios (o reverb do bus continua ao vivo)
                 const auto sourceName = source->name, sourceColour = source->colour, sourceOutput = source->output;
                 Channel ch;
                 ch.id = makeId ("ch");
                 ch.kind = "audio";
                 ch.name = sourceName + " (áudio)"_t;
                 ch.colour = sourceColour;
                 ch.samplePath = out.getFullPathName().replaceCharacter ('\\', '/');
                 ch.output = sourceOutput;

                 juce::String newTrack;

                 if (withTrack)
                 {
                     const auto* original = p.findMixer (track);
                     MixerTrack mx;
                     mx.id = makeId ("mx");
                     mx.name = ch.name;
                     mx.colour = original != nullptr ? original->colour : sourceColour;
                     mx.output = original != nullptr ? original->output : juce::String();
                     mx.sends = original != nullptr ? original->sends : std::vector<Send>();
                     mx.role = original != nullptr ? original->role : juce::String();
                     mx.autoFor = ch.id;
                     ch.output = newTrack = mx.id;
                     insertBeforeMaster (p, mx);
                 }

                 if (params::boolean (a, "mute", true))
                     if (auto* original = p.findChannel (id))
                         original->mute = true;

                 p.channels.push_back (ch);

                 // a primeira raia vazia (a de nome padrão ganha o nome do canal); sem nenhuma, uma raia nova
                 juce::String lane;

                 for (auto& t : p.playlistTracks)
                 {
                     if (std::none_of (p.playlistItems.begin(), p.playlistItems.end(), [&t] (const PlaylistItem& i) { return i.track == t.id; }))
                     {
                         lane = t.id;

                         if (t.name.startsWith ("Raia "))
                             t.name = ch.name;
                         break;
                     }
                 }

                 if (lane.isEmpty())
                 {
                     PlaylistTrack t;
                     t.id = makeId ("pl");
                     t.name = ch.name;
                     p.playlistTracks.push_back (t);
                     lane = t.id;
                 }

                 PlaylistItem item;
                 item.id = makeId ("it");
                 item.kind = "audio";
                 item.ref = ch.id;
                 item.track = lane;
                 item.start = from;
                 item.length = end - from;
                 item.offsetSeconds = roundToMicroseconds (fromSeconds - alignedFrom);
                 p.playlistItems.push_back (item);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("channel", ch.id);
                 o->setProperty ("item", item.id);
                 o->setProperty ("track", newTrack);
                 o->setProperty ("lane", lane);
                 o->setProperty ("path", ch.samplePath);
                 o->setProperty ("from", from);
                 o->setProperty ("to", end);
                 return CommandResult::success (juce::var (o));
             } });

    // Reamostrar (bloco 3, 23/10; o resample do Ableton): a música, ou um trecho dela, com o mixer e o master,
    // vira um áudio no fim da música, numa raia vazia (para não tocar junto com o que ele copia). Bom para picotar,
    // inverter e esticar o beat inteiro.
    r.add ({ "project.resample", "Reamostrar a música"_t,
             schema ({ { "from", "number", false, "Começo do trecho, na linha do tempo; padrão 0"_t },
                       { "to", "number", false, "Fim do trecho; padrão = o fim da música"_t },
                       { "unit", "string", false, "Unidade de from/to: ticks (padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();

                 if (p.playlistItems.empty())
                     return CommandResult::fail ("A playlist está vazia: reamostrar copia o que a música toca."_t);

                 const int songEnd = p.songEndTicks();
                 const int from = juce::jlimit (0, maxTimelineTicks, params::has (a, "from") ? toTicks (a, params::number (a, "from", 0)) : 0);
                 const int to = juce::jlimit (0, maxTimelineTicks, params::has (a, "to") ? toTicks (a, params::number (a, "to", 0)) : songEnd);

                 if (to <= from)
                     return CommandResult::fail ("O fim do trecho precisa vir depois do começo."_t);

                 Project song = p;
                 song.songMode = true;
                 const auto out = c.app.recordingsFolder().getNonexistentChildFile ("Reamostrado", ".wav", false);
                 out.getParentDirectory().createDirectory();

                 if (const auto rendered = c.app.renderRange (song, ticksToSeconds (from, p.bpm), ticksToSeconds (to, p.bpm), out); rendered.failed())
                     return CommandResult::fail (rendered.getErrorMessage());

                 Channel ch;
                 ch.id = makeId ("ch");
                 ch.kind = "audio";
                 ch.name = "Reamostrado"_t;
                 ch.colour = channelColourForIndex ((int) p.channels.size());
                 ch.samplePath = out.getFullPathName().replaceCharacter ('\\', '/');
                 ch.output = masterTrackId;   // já vem com o mixer e o master: sai direto, no mesmo volume
                 p.channels.push_back (ch);

                 juce::String lane;

                 for (auto& t : p.playlistTracks)
                     if (std::none_of (p.playlistItems.begin(), p.playlistItems.end(), [&t] (const PlaylistItem& i) { return i.track == t.id; }))
                     {
                         lane = t.id;

                         if (t.name.startsWith ("Raia "))
                             t.name = ch.name;
                         break;
                     }

                 if (lane.isEmpty())
                 {
                     PlaylistTrack t;
                     t.id = makeId ("pl");
                     t.name = ch.name;
                     p.playlistTracks.push_back (t);
                     lane = t.id;
                 }

                 const int bar = juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen)) * juce::jmax (1, p.timeSigNum) * ticksPerStep;

                 PlaylistItem item;
                 item.id = makeId ("it");
                 item.kind = "audio";
                 item.ref = ch.id;
                 item.track = lane;
                 item.start = juce::jmin (maxTimelineTicks - (to - from), (songEnd + bar - 1) / bar * bar);   // no compasso depois do fim
                 item.length = to - from;
                 p.playlistItems.push_back (item);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("channel", ch.id);
                 o->setProperty ("item", item.id);
                 o->setProperty ("lane", lane);
                 o->setProperty ("path", ch.samplePath);
                 o->setProperty ("start", item.start);
                 return CommandResult::success (juce::var (o));
             } });

    //==========================================================================
    // Playlist
    r.add ({ "playlist.addItem", "Colocar na playlist",
             schema ({ { "kind", "string", false, "pattern (padrão) ou audio"_t },
                       { "ref", "string", false, "Id do padrão (padrão = atual) ou do canal de áudio"_t },
                       { "path", "string", false, "Arquivo de áudio: cria o canal de áudio se ainda não existir"_t },
                       { "track", "string", true, "Raia: id ou número (1 = primeira)"_t },
                       { "start", "number", true, "Início"_t },
                       { "length", "number", false, "Duração; padrão = tamanho do padrão ou do arquivo"_t },
                       { "offsetSeconds", "number", false, "Áudio: quanto aparar do começo do arquivo, em segundos do arquivo"_t },
                       { "patternOffset", "number", false, "Padrão: de onde o bloco começa dentro do padrão, em ticks"_t },
                       { "muted", "boolean", false, "Bloco mudo"_t },
                       { "stretch", "number", false, "Áudio esticado no tempo: segundos na playlist por segundo do arquivo (1 = normal, de 0,25 a 4)"_t },
                       { "tape", "boolean", false, "Áudio: o esticar muda o tom junto, como uma fita"_t },
                       { "unit", "string", false, "Unidade de start/length: ticks (padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto track = resolvePlaylistTrack (p, a["track"]);

                 if (track.isEmpty())
                     return CommandResult::fail ("Raia da playlist não encontrada."_t);

                 PlaylistItem item;
                 item.id = makeId ("it");
                 item.track = track;
                 item.start = juce::jlimit (0, maxTimelineTicks, toTicks (a, params::number (a, "start", 0)));
                 item.kind = params::string (a, "kind", params::has (a, "path") ? "audio" : "pattern");

                 if (item.kind == "audio")
                 {
                     const juce::File file (params::string (a, "path"));
                     auto ref = params::string (a, "ref");

                     if (ref.isEmpty())
                     {
                         if (! file.existsAsFile())
                             return CommandResult::fail ("Arquivo de áudio não encontrado: "_t + file.getFullPathName());

                         // reaproveita o canal de áudio do mesmo arquivo, como no FL
                         for (auto& ch : p.channels)
                             if (ch.isAudio() && c.model.resolveSample (ch.samplePath) == file)
                                 ref = ch.id;

                         if (ref.isEmpty())
                             ref = addAudioChannel (p, file, {}).id;
                     }

                     auto* ch = p.findChannel (ref);

                     if (ch == nullptr || ! ch->isAudio())
                         return CommandResult::fail ("Canal de áudio não encontrado: "_t + ref);

                     item.ref = ref;
                     item.stretch = clampStretch (params::number (a, "stretch", 1.0));
                     item.tape = params::boolean (a, "tape", false);
                     const double seconds = c.app.fileDurationSeconds (c.model.resolveSample (ch->samplePath));
                     const int fileTicks = juce::roundToInt (seconds * item.stretch * p.bpm / 60.0 * ticksPerBeat);
                     item.length = params::has (a, "length") ? toTicks (a, params::number (a, "length", 0))
                                                             : juce::jmax (ticksPerStep, fileTicks);
                     item.offsetSeconds = roundToMicroseconds (juce::jmax (0.0, params::number (a, "offsetSeconds", 0.0)));
                 }
                 else
                 {
                     item.kind = "pattern";
                     item.ref = params::string (a, "ref", p.currentPattern);
                     auto* pat = p.findPattern (item.ref);

                     if (pat == nullptr)
                         return CommandResult::fail ("Padrão não encontrado: "_t + item.ref);

                     item.length = params::has (a, "length") ? toTicks (a, params::number (a, "length", 0)) : pat->lengthTicks();
                     item.patternOffset = juce::jlimit (0, maxTimelineTicks, (int) params::number (a, "patternOffset", 0));
                 }

                 item.muted = params::boolean (a, "muted", false);

                 item.length = juce::jlimit (ticksPerStep, maxTimelineTicks, item.length);
                 p.playlistItems.push_back (item);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("item", item.id);
                 o->setProperty ("ref", item.ref);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "playlist.updateItem", "Mover ou aparar bloco",
             schema ({ { "item", "string", true, "Id do bloco" },
                       { "track", "string", false, "Raia nova: id ou número"_t },
                       { "start", "number", false, "Início novo"_t },
                       { "length", "number", false, "Duração nova"_t },
                       { "offsetSeconds", "number", false, "Áudio: quanto aparar do começo do arquivo, em segundos do arquivo (não muda com o BPM)"_t },
                       { "offset", "number", false, "Áudio, forma antiga: o corte na unidade de \"unit\", convertido no BPM atual"_t },
                       { "patternOffset", "number", false, "Padrão: de onde o bloco começa dentro do padrão, em ticks (slip)"_t },
                       { "muted", "boolean", false, "Bloco mudo (ferramenta Mudo)"_t },
                       { "stretch", "number", false, "Áudio: o esticar, sem mudar a duração (playlist.stretchItem muda os dois)"_t },
                       { "tape", "boolean", false, "Áudio: o esticar muda o tom junto, como uma fita"_t },
                       { "unit", "string", false, "Unidade: ticks (padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* item = p.findItem (params::string (a, "item"));

                 if (item == nullptr)
                     return CommandResult::fail ("Bloco da playlist não encontrado."_t);

                 if (params::has (a, "track"))
                 {
                     const auto track = resolvePlaylistTrack (p, a["track"]);

                     if (track.isEmpty())
                         return CommandResult::fail ("Raia da playlist não encontrada."_t);

                     item->track = track;
                 }

                 if (params::has (a, "start"))  item->start = juce::jlimit (0, maxTimelineTicks, toTicks (a, params::number (a, "start", 0)));
                 if (params::has (a, "length")) item->length = juce::jlimit (ticksPerStep, maxTimelineTicks, toTicks (a, params::number (a, "length", 0)));
                 if (params::has (a, "offsetSeconds"))
                     item->offsetSeconds = roundToMicroseconds (juce::jmax (0.0, params::number (a, "offsetSeconds", 0.0)));
                 else if (params::has (a, "offset"))
                     item->offsetSeconds = roundToMicroseconds (ticksToSeconds (juce::jmax (0, toTicks (a, params::number (a, "offset", 0))), p.bpm));

                 if (params::has (a, "patternOffset") && item->kind == "pattern")
                     item->patternOffset = juce::jlimit (0, maxTimelineTicks, (int) params::number (a, "patternOffset", 0));

                 if (params::has (a, "muted"))
                     item->muted = params::boolean (a, "muted", false);

                 if (params::has (a, "stretch") && item->kind == "audio")
                     item->stretch = clampStretch (params::number (a, "stretch", 1.0));

                 if (params::has (a, "tape") && item->kind == "audio")
                     item->tape = params::boolean (a, "tape", false);

                 return CommandResult::success();
             } });

    // Esticar o áudio no tempo (21/10): segurar A e arrastar a borda do bloco. A duração e o esticar mudam
    // na mesma proporção, então o bloco mostra o mesmo pedaço do arquivo (offsetSeconds não muda).
    // Pela borda direita o começo fica parado; pela esquerda, o fim.
    r.add ({ "playlist.stretchItem", "Esticar áudio"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio" },
                       { "stretch", "number", false, "O esticar novo: segundos na playlist por segundo do arquivo (1 = normal, de 0,25 a 4)"_t },
                       { "length", "number", false, "Ou a duração nova do bloco, e o esticar segue a proporção"_t },
                       { "edge", "string", false, "right (padrão: o começo fica parado) ou left (o fim fica parado)"_t },
                       { "unit", "string", false, "Unidade de length: ticks (padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* item = c.model.data().findItem (params::string (a, "item"));

                 if (item == nullptr)
                     return CommandResult::fail ("Bloco da playlist não encontrado."_t);

                 if (item->kind != "audio")
                     return CommandResult::fail ("Só bloco de áudio estica no tempo."_t);

                 if (params::has (a, "stretch") == params::has (a, "length"))
                     return CommandResult::fail ("Diga o stretch ou a duração nova (length), um dos dois."_t);

                 const auto edge = params::string (a, "edge", "right");

                 if (edge != "right" && edge != "left")
                     return CommandResult::fail ("A borda é right ou left."_t);

                 // a duração pedida (ou a que o esticar pedido dá), dentro dos limites do esticar e da linha do tempo
                 const double wanted = params::has (a, "stretch")
                                         ? params::number (a, "stretch", 1.0)
                                         : item->stretch * toTicks (a, params::number (a, "length", 0)) / (double) item->length;

                 if (! std::isfinite (wanted) || wanted <= 0.0)
                     return CommandResult::fail ("Duração inválida."_t);

                 double stretch = clampStretch (wanted);
                 int length = params::has (a, "length") && std::abs (stretch - wanted) < 1.0e-5
                                ? toTicks (a, params::number (a, "length", 0))
                                : juce::roundToInt (item->length * stretch / item->stretch);
                 const int asked = length;
                 const int end = item->end();
                 length = juce::jlimit (ticksPerStep, maxTimelineTicks, length);

                 if (edge == "left")
                     length = juce::jmin (length, end);   // o começo não passa do zero da música

                 if (length != asked)
                     stretch = clampStretch (item->stretch * length / (double) item->length);

                 if (edge == "left")
                     item->start = end - length;

                 item->stretch = stretch;
                 item->length = length;

                 auto o = new juce::DynamicObject();
                 o->setProperty ("item", item->id);
                 o->setProperty ("stretch", item->stretch);
                 o->setProperty ("start", item->start);
                 o->setProperty ("length", item->length);
                 return CommandResult::success (juce::var (o));
             } });

    // A ferramenta Tocar (Y) da playlist: o play começa onde a pessoa clicou.
    r.add ({ "transport.playFrom", "Tocar daqui"_t,
             schema ({ { "at", "number", true, "Onde começar, na linha do tempo"_t },
                       { "unit", "string", false, "Unidade: ticks (padrão), steps, beats ou bars"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 c.app.playFrom (juce::jmax (0, toTicks (a, params::number (a, "at", 0))));
                 return CommandResult::success();
             } });

    // A ferramenta Cortar (C) da playlist: o bloco vira dois. O da direita continua de onde o primeiro
    // parou: o áudio pelo offsetSeconds, o padrão pelo patternOffset (pedido do usuário, 21/10).
    r.add ({ "playlist.splitItem", "Cortar bloco",
             schema ({ { "item", "string", true, "Id do bloco" },
                       { "at", "number", true, "Onde cortar, na linha do tempo (precisa cair dentro do bloco)"_t },
                       { "unit", "string", false, "Unidade de at: ticks (padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* item = p.findItem (params::string (a, "item"));

                 if (item == nullptr)
                     return CommandResult::fail ("Bloco da playlist não encontrado."_t);

                 const int at = toTicks (a, params::number (a, "at", 0));

                 if (at <= item->start || at >= item->end())
                     return CommandResult::fail ("O corte precisa cair dentro do bloco."_t);

                 PlaylistItem right = *item;
                 right.id = makeId ("it");
                 right.start = at;
                 right.length = item->end() - at;

                 if (item->kind == "audio")   // esticado, o arquivo anda 1 / stretch por segundo da playlist
                     right.offsetSeconds = roundToMicroseconds (item->offsetSeconds + item->fileSeconds (ticksToSeconds (at - item->start, p.bpm)));
                 else
                     right.patternOffset = item->patternOffset + (at - item->start);

                 item->length = at - item->start;
                 const auto leftId = item->id;
                 p.playlistItems.push_back (right);   // item pode mudar de endereço daqui em diante

                 auto o = new juce::DynamicObject();
                 o->setProperty ("left", leftId);
                 o->setProperty ("right", right.id);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "playlist.removeItem", "Apagar bloco",
             schema ({ { "item", "string", true, "Id do bloco" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto id = params::string (a, "item");
                 auto& items = c.model.data().playlistItems;
                 const auto before = items.size();
                 std::erase_if (items, [&id] (const PlaylistItem& i) { return i.id == id; });
                 return items.size() < before ? CommandResult::success() : CommandResult::fail ("Bloco da playlist não encontrado."_t);
             } });

    r.add ({ "playlist.clear", "Limpar playlist", schema ({}), true,
             [] (CommandContext& c, const juce::var&)
             {
                 c.model.data().playlistItems.clear();
                 return CommandResult::success();
             } });

    r.add ({ "playlist.renameTrack", "Renomear raia",
             schema ({ { "track", "string", true, "Raia: id ou número"_t }, { "name", "string", true, "Nome novo" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto id = resolvePlaylistTrack (p, a["track"]);
                 const auto name = params::string (a, "name").trim();

                 for (auto& t : p.playlistTracks)
                 {
                     if (t.id == id && name.isNotEmpty())
                     {
                         t.name = name;
                         return CommandResult::success();
                     }
                 }

                 return CommandResult::fail ("Raia não encontrada ou nome vazio."_t);
             } });

    //==========================================================================
    // Marcadores na régua (bloco 1, 22/10): intro, verso, refrão. Clicar pula a agulha; o trecho de um
    // marcador vai até o seguinte (vira loop e trecho de exportação na playlist).
    auto sortMarkers = [] (Project& p)
    {
        std::stable_sort (p.markers.begin(), p.markers.end(), [] (const Marker& x, const Marker& y) { return x.t < y.t; });
    };

    r.add ({ "playlist.addMarker", "Adicionar marcador"_t,
             schema ({ { "at", "number", true, "Onde, na linha do tempo"_t },
                       { "name", "string", false, "Nome (Intro, Verso, Refrão...); ausente = \"Marcador N\""_t },
                       { "unit", "string", false, "Unidade de at: ticks (padrão), steps, beats ou bars"_t } }), true,
             [sortMarkers] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 Marker m;
                 m.id = makeId ("mk");
                 m.t = juce::jlimit (0, maxTimelineTicks, toTicks (a, params::number (a, "at", 0)));
                 m.name = params::string (a, "name").trim().substring (0, maxMarkerName).trim();

                 if (m.name.isEmpty())
                     m.name = "Marcador " + juce::String ((int) p.markers.size() + 1);

                 p.markers.push_back (m);
                 sortMarkers (p);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("id", m.id);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "playlist.updateMarker", "Mudar marcador"_t,
             schema ({ { "marker", "string", true, "Id do marcador" },
                       { "at", "number", false, "Novo lugar na linha do tempo"_t },
                       { "name", "string", false, "Nome novo" },
                       { "unit", "string", false, "Unidade de at: ticks (padrão), steps, beats ou bars"_t } }), true,
             [sortMarkers] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* m = p.findMarker (params::string (a, "marker"));

                 if (m == nullptr)
                     return CommandResult::fail ("Marcador não encontrado."_t);

                 if (params::has (a, "at"))
                     m->t = juce::jlimit (0, maxTimelineTicks, toTicks (a, params::number (a, "at", 0)));

                 if (params::has (a, "name"))
                 {
                     const auto name = params::string (a, "name").trim().substring (0, maxMarkerName).trim();

                     if (name.isEmpty())
                         return CommandResult::fail ("O nome do marcador não pode ficar vazio."_t);

                     m->name = name;
                 }

                 sortMarkers (p);
                 return CommandResult::success();
             } });

    r.add ({ "playlist.removeMarker", "Apagar marcador"_t,
             schema ({ { "marker", "string", true, "Id do marcador" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& markers = c.model.data().markers;
                 const auto id = params::string (a, "marker");
                 const auto before = markers.size();
                 std::erase_if (markers, [&id] (const Marker& m) { return m.id == id; });
                 return markers.size() < before ? CommandResult::success() : CommandResult::fail ("Marcador não encontrado."_t);
             } });
}

} // namespace whynot
