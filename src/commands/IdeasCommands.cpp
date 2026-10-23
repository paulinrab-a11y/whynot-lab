// Comandos do WHYNOT Ideias: gerar acordes, melodia, arpejo, baixo, 808 e bateria por gênero,
// ler acordes e tonalidade de um canal e entender pedidos em português. A tela e a IA (MCP)
// usam os mesmos comandos; cada geração é um passo de desfazer.

#include "CommandRegistry.h"
#include "../ideas/Analysis.h"
#include "../ideas/Generator.h"
#include "../ideas/Prompt.h"

namespace whynot
{

namespace
{
    using params::schema;
    namespace ix = ideas;

    static_assert (ix::ticksPerBeat == ticksPerBeat && ix::ticksPerStep == ticksPerStep, "a grade do Ideias é a do projeto");

    juce::String utf8 (std::string_view s)   { return juce::String::fromUTF8 (s.data(), (int) s.size()); }

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    Pattern* targetPattern (Project& p, const juce::var& a)
    {
        const auto id = params::string (a, "pattern");
        return id.isEmpty() ? p.currentPatternPtr() : p.findPattern (id);
    }

    int nextNoteId (const std::vector<Note>& notes)
    {
        int maxId = 0;

        for (auto& n : notes)
            maxId = juce::jmax (maxId, n.id);

        return maxId + 1;
    }

    juce::String scaleNamePt (const juce::String& s)
    {
        if (s == "major")           return "maior";
        if (s == "phrygian")        return "frígio"_t;
        if (s == "dorian")          return "dórico"_t;
        if (s == "harmonicMinor")   return "menor harmônica"_t;
        if (s == "pentatonicMinor") return "pentatônica menor"_t;
        if (s == "chromatic")       return "cromática"_t;
        return "menor";
    }

    juce::String keyNamePt (const ix::Scale& s)
    {
        return utf8 (ix::pitchNamePt (s.root, s.prefersFlats())) + " " + scaleNamePt (s.name);
    }

    /** "C", "C#", "Db", "Dó", "Fá#", "Sib"... ou um número de 0 a 11. -1 = inválido. */
    int parseRootName (const juce::var& v)
    {
        if (v.isInt() || v.isDouble() || v.isInt64())
            return (((int) v % 12) + 12) % 12;

        const auto folded = ix::foldText (v.toString().trim().toStdString());
        static const char* sharps[] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
        static const char* flats[]  = { "c", "db", "d", "eb", "e", "f", "gb", "g", "ab", "a", "bb", "b" };

        for (int i = 0; i < 12; ++i)
            if (folded == sharps[i] || folded == flats[i])
                return i;

        // Nomes em português passam pelo leitor de pedidos ("fa#" sozinho não basta: precisa de "em").
        const auto parsed = ix::parsePrompt ("em " + folded);
        return parsed.root.value_or (-1);
    }

    /** Os ajustes comuns de ideas.generate e ideas.progressions. Vazio = tudo certo. */
    juce::String readRequest (const Project& p, const juce::var& a, ix::Request& r)
    {
        r.genre = params::string (a, "genre", "trap").toStdString();

        if (ix::findGenre (r.genre) == nullptr)
        {
            juce::StringArray ids;

            for (auto& g : ix::allGenres())
                ids.add (g.id);

            return "Gênero desconhecido: "_t + utf8 (r.genre) + ". Use: " + ids.joinIntoString (", ");
        }

        if (params::has (a, "mood"))
        {
            ix::Mood m;

            if (! ix::parseMood (params::string (a, "mood").toStdString(), m))
                return "Clima desconhecido. Use: dark, sad, chill, happy ou epic."_t;

            r.mood = m;
        }

        r.bars = (int) params::number (a, "bars", 4);

        if (r.bars < 1 || r.bars > 16)
            return "Use de 1 a 16 compassos."_t;

        int root = p.keyRoot;

        if (params::has (a, "root"))
        {
            root = parseRootName (a["root"]);

            if (root < 0)
                return "Tônica inválida. Use C, C#, Db... ou Dó, Fá#, Sib..."_t;
        }

        const auto scale = params::string (a, "scale", p.scale);

        if (! Project::scaleNames().contains (scale))
            return "Escala desconhecida: "_t + scale;

        r.scale = ix::Scale::fromName (root, scale.toStdString());
        r.complexity = juce::jlimit (0.0, 1.0, params::number (a, "complexity", 0.5));
        r.density = juce::jlimit (0.0, 1.0, params::number (a, "density", 0.5));
        r.octave = juce::jlimit (-2, 2, (int) params::number (a, "octave", 0));
        r.variation = (uint32_t) juce::jmax (0.0, params::number (a, "variation", 0));
        r.progression = params::string (a, "progression").toStdString();

        // 1 a 2^31-1: a semente volta igual no resultado (int do JSON) e serve para repetir a ideia.
        if (params::has (a, "seed"))
            r.seed = (uint32_t) juce::jlimit (1.0, 2147483647.0, params::number (a, "seed", 1));
        else
            r.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (1 << 30) + 1;

        return {};
    }

    std::vector<ix::GenNote> notesOf (const Pattern& pat, const juce::String& channel)
    {
        std::vector<ix::GenNote> out;

        if (auto* list = pat.notesFor (channel))
            for (auto& n : *list)
                out.push_back ({ n.start, n.length, n.key, n.velocity, n.slide });

        return out;
    }

    juce::var chordList (const std::vector<ix::ChordEvent>& chords, const ix::Scale& scale, int offsetTicks = 0)
    {
        juce::Array<juce::var> list;
        const bool flats = scale.prefersFlats();

        for (auto& e : chords)
        {
            const int t = e.start + offsetTicks;
            list.add (object ({ { "name", utf8 (e.chord.name (flats)) },
                                { "start", t },
                                { "length", e.length },
                                { "bar", t / ticksPerBar4 + 1 },
                                { "beat", (t % ticksPerBar4) / ticksPerBeat + 1 } }));
        }

        return list;
    }

    //==========================================================================
    // Bateria: cada papel vai para o canal com o nome certo ("Kick", "Caixa", "Chimbal"...).
    bool nameMatches (const juce::String& name, std::initializer_list<const char*> words)
    {
        const auto folded = juce::String (ix::foldText (name.toStdString()));
        juce::StringArray tokens;
        tokens.addTokens (folded, " _-.()[]0123456789", "");
        tokens.removeEmptyStrings();

        for (auto* w : words)
        {
            const juce::String word (w);

            for (auto& t : tokens)
                if (t == word || (word.length() >= 4 && t.startsWith (word)))
                    return true;
        }

        return false;
    }

    /** O canal do papel pelo nome, sem repetir um canal já escolhido para outro papel
        ("Perc Clap" casa com caixa e percussão: fica com o primeiro). */
    juce::String channelForRole (const Project& p, const juce::String& role, const juce::StringArray& taken)
    {
        for (auto& ch : p.channels)
        {
            if (ch.isAudio() || taken.contains (ch.id))
                continue;

            const auto& n = ch.name;
            const bool open = nameMatches (n, { "open", "oh", "aberto", "openhat", "ohat", "ohh" });

            if (role == "kick"    && nameMatches (n, { "kick", "bumbo", "bd", "kik", "bombo" }))                            return ch.id;
            if (role == "snare"   && nameMatches (n, { "snare", "caixa", "clap", "palma", "palmas", "sd" }))                return ch.id;
            if (role == "openhat" && open)                                                                                return ch.id;
            if (role == "hat"     && ! open && nameMatches (n, { "hat", "hh", "hihat", "hi-hat", "chimbal", "chimbau", "closed", "ch" })) return ch.id;
            if (role == "perc"    && nameMatches (n, { "perc", "rim", "cowbell", "shaker", "conga", "bongo", "tom", "tamb", "tambourine", "pandeiro" })) return ch.id;
        }

        return {};
    }

    CommandResult writeDrums (Project& p, Pattern& pat, const ix::Request& r, int atBar, const juce::var& a)
    {
        const auto res = ix::generateDrums (r);

        if (! res.ok)
            return CommandResult::fail (utf8 (res.error));

        const int firstStep = atBar * 16;
        pat.lengthSteps = juce::jmax (pat.lengthSteps, firstStep + res.lengthSteps);

        auto* lanes = new juce::DynamicObject();
        juce::Array<juce::var> missing;
        const auto overrides = a["channels"];

        juce::StringArray written;   // canais que já receberam uma raia nesta geração

        for (auto& [role, steps] : res.lanes)
        {
            const juce::String roleId (role);
            auto channel = overrides.isObject() ? overrides[roleId.toRawUTF8()].toString() : juce::String();

            if (channel.isNotEmpty() && p.findChannel (channel) == nullptr)
                return CommandResult::fail ("Canal não encontrado para "_t + roleId + ": " + channel);

            if (channel.isEmpty())
                channel = channelForRole (p, roleId, written);

            const bool any = std::any_of (steps.begin(), steps.end(), [] (const ix::DrumStep& s) { return s.on; });

            if (channel.isEmpty())
            {
                if (any)
                    missing.add (roleId);

                continue;
            }

            auto& lane = pat.steps[channel];
            lane.resize ((size_t) pat.lengthSteps);

            // Dois papéis pedidos no mesmo canal somam (passo ligado em qualquer um deles fica ligado);
            // o primeiro papel do canal substitui o que havia nesses compassos.
            const bool merge = written.contains (channel);

            for (size_t i = 0; i < steps.size(); ++i)
            {
                auto& s = lane[(size_t) firstStep + i];

                if (! merge)
                {
                    s.on = steps[i].on;
                    s.velocity = steps[i].velocity;
                    s.roll = steps[i].roll;
                }
                else if (steps[i].on)
                {
                    s.velocity = s.on ? juce::jmax (s.velocity, steps[i].velocity) : steps[i].velocity;
                    s.roll = s.on ? juce::jmax (s.roll, steps[i].roll) : steps[i].roll;
                    s.on = true;
                }
            }

            written.addIfNotAlreadyThere (channel);
            lanes->setProperty (roleId, channel);
        }

        const auto& g = *ix::findGenre (r.genre);
        return CommandResult::success (object ({
            { "part", "drums" },
            { "seed", (int) r.seed },
            { "variation", (int) r.variation },
            { "beat", res.beat + 1 },
            { "beats", (int) g.beats.size() },
            { "lanes", juce::var (lanes) },
            { "missing", missing },
            { "summary", utf8 (g.name) + " · bateria "_t + juce::String (res.beat + 1) + "/" + juce::String ((int) g.beats.size())
                         + " · "_t + juce::String (r.bars) + (r.bars == 1 ? " compasso" : " compassos") } }));
    }
}

void registerIdeasCommands (CommandRegistry& r)
{
    r.add ({ "ideas.options", "Opções do Ideias"_t, schema ({}), false,
             [] (CommandContext&, const juce::var&)
             {
                 juce::Array<juce::var> genres, moods, parts;

                 for (auto& g : ix::allGenres())
                     genres.add (object ({ { "id", g.id }, { "name", utf8 (g.name) }, { "description", utf8 (g.description) },
                                           { "bpm", juce::Array<juce::var> { g.bpmLow, g.bpmHigh, g.bpmDefault } },
                                           { "mood", utf8 (ix::moodId (g.defaultMood)) }, { "beats", (int) g.beats.size() } }));

                 for (auto m : ix::allMoods())
                     moods.add (object ({ { "id", utf8 (ix::moodId (m)) }, { "name", utf8 (ix::moodName (m)) } }));

                 for (auto pt : ix::allParts())
                     parts.add (object ({ { "id", utf8 (ix::partId (pt)) }, { "name", utf8 (ix::partName (pt)) } }));

                 return CommandResult::success (object ({ { "genres", genres }, { "moods", moods }, { "parts", parts } }));
             } });

    r.add ({ "ideas.progressions", "Progressões do gênero"_t,
             schema ({ { "genre", "string", false, "trap, drill, funk, rap, pop, rnb, phonk ou lofi"_t },
                       { "mood", "string", false, "dark, sad, chill, happy ou epic" },
                       { "root", "string", false, "Tônica (padrão: a do projeto)"_t },
                       { "scale", "string", false, "Escala (padrão: a do projeto)" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 ix::Request req;

                 if (auto err = readRequest (c.model.get(), a, req); err.isNotEmpty())
                     return CommandResult::fail (err);

                 const auto& g = *ix::findGenre (req.genre);
                 const auto mood = req.mood.value_or (g.defaultMood);
                 juce::Array<juce::var> list;

                 for (auto* e : ix::progressionsFor (req.genre, ! req.scale.isMajor(), mood))
                 {
                     ix::Request one = req;
                     one.progression = e->text;
                     one.complexity = 0.0;
                     one.bars = 4;
                     std::string text, error;
                     const auto chords = ix::chordTimeline (one, text, error);
                     list.add (object ({ { "text", e->text }, { "names", utf8 (ix::chordNames (chords, req.scale)) },
                                         { "moods", e->moods } }));
                 }

                 return CommandResult::success (object ({ { "key", keyNamePt (req.scale) }, { "progressions", list } }));
             } });

    r.add ({ "ideas.generate", "Gerar ideia"_t,
             schema ({ { "part", "string", true, "chords, melody, arp, bass, 808 ou drums"_t },
                       { "channel", "string", false, "Canal que recebe as notas (obrigatório, menos na bateria)"_t },
                       { "genre", "string", false, "trap (padrão), drill, funk, rap, pop, rnb, phonk ou lofi"_t },
                       { "mood", "string", false, "dark, sad, chill, happy ou epic; padrão = o do gênero"_t },
                       { "bars", "integer", false, "Compassos: 1 a 16 (padrão 4)"_t },
                       { "at", "integer", false, "Compasso onde começa, a partir de 0 (padrão 0)"_t },
                       { "root", "string", false, "Tônica: C, F#, Bb, Dó, Fá#... (padrão: a do projeto)"_t },
                       { "scale", "string", false, "minor, major, harmonicMinor, phrygian, dorian, pentatonicMinor; padrão: a do projeto"_t },
                       { "complexity", "number", false, "0 a 1: extensões nos acordes, variações (padrão 0,5)"_t },
                       { "density", "number", false, "0 a 1: quantas notas (padrão 0,5)"_t },
                       { "seed", "integer", false, "A ideia: mesma semente, mesma progressão e batida em todas as partes; ausente = nova"_t },
                       { "variation", "integer", false, "Variação desta parte com a mesma ideia (botão Variar)"_t },
                       { "progression", "string", false, "Graus romanos, ex.: \"i VI III VII\" ou \"ii7 V7 Imaj7*2\"; vazio = escolhe pelo gênero"_t },
                       { "follow", "string", false, "Id de um canal: usa os acordes que ele já toca em vez de uma progressão"_t },
                       { "octave", "integer", false, "-2 a 2" },
                       { "replace", "boolean", false, "Apaga o que o canal tinha nesses compassos (padrão true)"_t },
                       { "channels", "object", false, "Bateria: { kick, snare, hat, openhat, perc } → id do canal; padrão = pelo nome"_t },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = targetPattern (p, a);

                 if (pat == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 ix::Part part;

                 if (! ix::parsePart (params::string (a, "part").toStdString(), part))
                     return CommandResult::fail ("Parte desconhecida. Use chords, melody, arp, bass, 808 ou drums."_t);

                 ix::Request req;
                 req.part = part;

                 if (auto err = readRequest (p, a, req); err.isNotEmpty())
                     return CommandResult::fail (err);

                 const int atBar = (int) params::number (a, "at", 0);

                 if (atBar < 0 || (atBar + req.bars) * 16 > 256)
                     return CommandResult::fail ("O padrão vai até 16 compassos: diminua \"bars\" ou \"at\"."_t);

                 if (part == ix::Part::drums)
                     return writeDrums (p, *pat, req, atBar, a);

                 const auto channel = params::string (a, "channel");
                 auto* ch = p.findChannel (channel);

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado: "_t + channel);

                 if (ch->isAudio())
                     return CommandResult::fail ("Canal de áudio não tem notas; escolha um instrumento."_t);

                 const int offset = atBar * ticksPerBar4;

                 if (params::has (a, "follow"))
                 {
                     const auto follow = params::string (a, "follow");

                     if (p.findChannel (follow) == nullptr)
                         return CommandResult::fail ("Canal para seguir não encontrado: "_t + follow);

                     // Os acordes do outro canal nos mesmos compassos, trazidos para o começo.
                     std::vector<ix::GenNote> source;

                     for (auto n : notesOf (*pat, follow))
                     {
                         n.start -= offset;

                         if (n.end() > 0 && n.start < req.bars * ticksPerBar4)
                         {
                             const int s = juce::jmax (0, n.start);
                             n.length = n.end() - s;
                             n.start = s;
                             source.push_back (n);
                         }
                     }

                     for (auto& d : ix::detectChords (source, req.bars * ticksPerBar4))
                         req.chords.push_back (d.event);

                     if (req.chords.empty())
                         return CommandResult::fail ("O canal "_t + p.findChannel (follow)->name + " não tem notas nesses compassos para seguir."_t);
                 }

                 const auto res = ix::generate (req);

                 if (! res.ok)
                     return CommandResult::fail (utf8 (res.error));

                 auto& notes = pat->notes[channel];

                 if (params::boolean (a, "replace", true))
                 {
                     const int from = offset, to = offset + req.bars * ticksPerBar4;
                     notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n) { return n.start >= from && n.start < to; }),
                                  notes.end());
                 }

                 int id = nextNoteId (notes);
                 bool slides = false;

                 for (auto& g : res.notes)
                 {
                     Note n;
                     n.id = id++;
                     n.start = g.start + offset;
                     n.length = g.length;
                     n.key = g.key;
                     n.velocity = g.velocity;
                     n.slide = g.slide;
                     slides = slides || g.slide;
                     notes.push_back (n);
                 }

                 std::stable_sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y) { return x.start < y.start; });
                 pat->lengthSteps = juce::jmax (pat->lengthSteps, (atBar + req.bars) * 16);

                 // O 808 com slide precisa do canal em mono para deslizar.
                 if (slides && ! ch->mono)
                     ch->mono = true;

                 const auto& g = *ix::findGenre (req.genre);
                 const auto mood = req.mood.value_or (g.defaultMood);
                 const auto names = utf8 (ix::chordNames (res.chords, req.scale));
                 juce::Array<juce::var> sound;

                 for (auto cat : ix::soundCategories (part, req.genre))
                     sound.add (utf8 (cat));
                 const bool usedRoman = req.chords.empty();

                 return CommandResult::success (object ({
                     { "part", utf8 (ix::partId (part)) },
                     { "channel", channel },
                     { "seed", (int) req.seed },
                     { "variation", (int) req.variation },
                     { "beat", res.beat + 1 },
                     { "notes", (int) res.notes.size() },
                     { "progression", usedRoman ? utf8 (res.progression) : juce::String() },
                     { "chordNames", names },
                     { "chords", chordList (res.chords, req.scale, offset) },
                     { "key", keyNamePt (req.scale) },
                     { "mono", slides },
                     { "sound", sound },
                     { "summary", utf8 (g.name) + " · "_t + utf8 (ix::moodName (mood)) + " · "_t + keyNamePt (req.scale) + " · "_t
                                  + (usedRoman ? utf8 (res.progression) + " (" + names + ")" : "seguindo " + names)
                                  + " · "_t + juce::String (req.bars) + (req.bars == 1 ? " compasso" : " compassos") } }));
             } });

    r.add ({ "ideas.detectChords", "Detectar acordes"_t,
             schema ({ { "channel", "string", false, "Id do canal; ausente = todos os canais com notas"_t },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = targetPattern (p, a);

                 if (pat == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 std::vector<ix::GenNote> notes;

                 if (params::has (a, "channel"))
                 {
                     const auto channel = params::string (a, "channel");

                     if (p.findChannel (channel) == nullptr)
                         return CommandResult::fail ("Canal não encontrado: "_t + channel);

                     notes = notesOf (*pat, channel);
                 }
                 else
                 {
                     for (auto& [id, list] : pat->notes)
                     {
                         const auto more = notesOf (*pat, id);
                         notes.insert (notes.end(), more.begin(), more.end());
                     }
                 }

                 if (notes.empty())
                     return CommandResult::fail ("Não há notas para analisar."_t);

                 const auto key = ix::detectKey (notes);
                 const auto scale = key.scale();
                 const auto chords = ix::detectChords (notes, pat->lengthTicks());
                 std::vector<ix::ChordEvent> events;

                 for (auto& d : chords)
                     events.push_back (d.event);

                 return CommandResult::success (object ({
                     { "key", keyNamePt (scale) },
                     { "root", key.root },
                     { "scale", key.major ? "major" : "minor" },
                     { "confidence", juce::roundToInt (key.confidence * 100) / 100.0 },
                     { "chords", chordList (events, scale) },
                     { "chordNames", utf8 (ix::chordNames (events, scale)) } }));
             } });

    r.add ({ "ideas.parse", "Entender pedido"_t,
             schema ({ { "text", "string", true, "Ex.: \"trap triste em Fá menor, 4 compassos, melodia\""_t } }), false,
             [] (CommandContext&, const juce::var& a)
             {
                 const auto s = ix::parsePrompt (params::string (a, "text").toStdString());
                 auto o = new juce::DynamicObject();
                 juce::Array<juce::var> understood;

                 for (auto& u : s.understood)
                     understood.add (utf8 (u));

                 if (s.genre)      o->setProperty ("genre", utf8 (*s.genre));
                 if (s.mood)       o->setProperty ("mood", utf8 (ix::moodId (*s.mood)));
                 if (s.part)       o->setProperty ("part", utf8 (ix::partId (*s.part)));
                 if (s.root)       o->setProperty ("root", *s.root);
                 if (s.scale)      o->setProperty ("scale", utf8 (*s.scale));
                 if (s.bars)       o->setProperty ("bars", *s.bars);
                 if (s.bpm)        o->setProperty ("bpm", *s.bpm);
                 if (s.complexity) o->setProperty ("complexity", *s.complexity);
                 if (s.density)    o->setProperty ("density", *s.density);

                 o->setProperty ("understood", understood);
                 return CommandResult::success (juce::var (o));
             } });
}

} // namespace whynot
