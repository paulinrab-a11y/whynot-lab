#include "CommandRegistry.h"
#include "../engine/AudioToMidi.h"
#include "../engine/SampleAnalysis.h"

// Áudio para MIDI (bloco 4e, 23/10): audio.toMidi. O beatbox do MC vira um padrão novo no kit (kick, caixa ou palma,
// chimbal), e uma melodia vira notas no piano roll de um instrumento. O padrão novo fica escolhido; o áudio fica como
// estava.

namespace whynot
{

namespace
{
    using params::schema;

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    /** O nome do arquivo do sample sem a pasta e a extensão (o caminho pode ser relativo ao projeto). */
    juce::String sampleName (const Channel& c)
    {
        return c.samplePath.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false);
    }

    /** O canal do kit que toca o papel, pelo nome do canal ou do sample: a caixa, ou a palma se não houver caixa. */
    const Channel* channelFor (const Project& p, audiomidi::DrumRole role)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            for (auto& c : p.channels)
            {
                if (c.kind != "sampler")
                    continue;

                auto r = roleFromName (c.name);

                if (r == SampleRole::unknown)
                    r = roleFromName (sampleName (c));

                const bool fits = role == audiomidi::DrumRole::kick  ? r == SampleRole::kick
                                : role == audiomidi::DrumRole::snare ? r == (pass == 0 ? SampleRole::snare : SampleRole::clap)
                                                                     : r == (pass == 0 ? SampleRole::hat : SampleRole::openHat);

                if (fits)
                    return &c;
            }
        }

        return nullptr;
    }

    /** O instrumento que recebe a melodia: o pedido, ou o primeiro synth ou plugin, ou o primeiro 808 ou baixo. */
    const Channel* melodyChannel (const Project& p, const juce::var& a, juce::String& error)
    {
        if (params::has (a, "channel"))
        {
            const auto* c = p.findChannel (params::string (a, "channel"));

            if (c == nullptr || c->isAudio())
                error = "Canal de instrumento não encontrado (um canal de áudio não toca notas)."_t;

            return c != nullptr && ! c->isAudio() ? c : nullptr;
        }

        for (auto& c : p.channels)
            if (c.kind == "synth" || c.kind == "plugin")
                return &c;

        for (auto& c : p.channels)
            if (c.kind == "sampler" && (roleFromName (c.name) == SampleRole::bass || roleFromName (sampleName (c)) == SampleRole::bass))
                return &c;

        error = "Ponha um instrumento no rack (o WHYNOT Synth, por exemplo) ou diga o canal (channel)."_t;
        return nullptr;
    }

    juce::String rolePt (audiomidi::DrumRole r)
    {
        return r == audiomidi::DrumRole::kick ? juce::String ("kick") : r == audiomidi::DrumRole::hat ? juce::String ("chimbal") : juce::String ("caixa");
    }
}

void registerAudioMidiCommands (CommandRegistry& r)
{
    r.add ({ "audio.toMidi", "Áudio para MIDI"_t,
             schema ({ { "item", "string", false, "Id do bloco de áudio da playlist (vale o trecho que ele toca)"_t },
                       { "path", "string", false, "Ou um arquivo de áudio inteiro (caminho completo)"_t },
                       { "mode", "string", true, "drums (o beatbox vira o kit: kick, caixa e chimbal) ou melody (a voz ou o instrumento vira notas)"_t },
                       { "channel", "string", false, "melody: o canal que recebe as notas (padrão: o primeiro synth ou plugin, ou o 808)"_t },
                       { "quantize", "boolean", false, "Encaixar na grade de 1/16 (padrão: sim)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto mode = params::string (a, "mode");

                 if (mode != "drums" && mode != "melody")
                     return CommandResult::fail ("mode é drums (bateria) ou melody (melodia)."_t);

                 juce::File file;
                 double from = 0.0, to = 0.0, stretch = 1.0;
                 juce::String source;

                 if (params::has (a, "item"))
                 {
                     const auto* item = p.findItem (params::string (a, "item"));
                     const auto* channel = item != nullptr ? p.findChannel (item->ref) : nullptr;

                     if (item == nullptr || item->kind != "audio" || channel == nullptr)
                         return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                     file = c.model.resolveSample (channel->samplePath);
                     from = item->offsetSeconds;
                     to = from + item->fileSeconds (ticksToSeconds (item->length, p.bpm));
                     stretch = item->stretch;
                     source = channel->name;
                 }
                 else if (params::has (a, "path"))
                 {
                     const auto path = params::string (a, "path");

                     if (! juce::File::isAbsolutePath (path))
                         return CommandResult::fail ("path precisa ser o caminho completo do arquivo."_t);

                     file = juce::File (path);
                     source = file.getFileNameWithoutExtension();
                 }
                 else
                 {
                     return CommandResult::fail ("Informe item (um bloco de áudio da playlist) ou path (um arquivo)."_t);
                 }

                 std::vector<float> mono;
                 double rate = 0.0;

                 if (! audiomidi::readMono (file, from, to, mono, rate))
                     return CommandResult::fail ("Não consegui ler o áudio: "_t + file.getFullPathName());

                 const bool quantize = params::boolean (a, "quantize", true);
                 auto ticksAt = [&] (double seconds) { return secondsToTicks (seconds * stretch, p.bpm); };
                 constexpr int maxSteps = 256;

                 Pattern pat;
                 pat.id = makeId ("pt");
                 juce::DynamicObject::Ptr out (new juce::DynamicObject());

                 if (mode == "drums")
                 {
                     const auto hits = audiomidi::drumHits (mono, rate);

                     if (hits.empty())
                         return CommandResult::fail ("Não achei batidas nesse áudio."_t);

                     const Channel* lanes[3] { channelFor (p, audiomidi::DrumRole::kick), channelFor (p, audiomidi::DrumRole::snare),
                                               channelFor (p, audiomidi::DrumRole::hat) };
                     std::vector<std::tuple<int, int, int>> placed;   // a raia, o passo e a força
                     juce::StringArray missing;
                     int counts[3] {}, last = 0;
                     juce::Array<juce::var> list;

                     for (auto& h : hits)
                     {
                         const int lane = (int) h.role;
                         list.add (object ({ { "seconds", std::round (h.seconds * 1000.0) / 1000.0 }, { "role", audiomidi::roleName (h.role) },
                                             { "velocity", h.velocity } }));

                         if (lanes[lane] == nullptr)
                         {
                             missing.addIfNotAlreadyThere (rolePt (h.role));
                             continue;
                         }

                         const int step = (int) std::lround (ticksAt (h.seconds) / ticksPerStep);

                         if (step < 0 || step >= maxSteps)
                             continue;

                         placed.push_back ({ lane, step, h.velocity });
                         last = juce::jmax (last, step);
                         ++counts[lane];
                     }

                     if (placed.empty())
                         return CommandResult::fail ("O rack não tem canal para as batidas que achei ("_t + missing.joinIntoString (", ") + ").");

                     pat.lengthSteps = juce::jlimit (16, maxSteps, (last / 16 + 1) * 16);

                     for (auto* ch : lanes)
                         if (ch != nullptr)
                             pat.steps[ch->id].assign ((size_t) pat.lengthSteps, Step {});

                     for (auto& [lane, step, velocity] : placed)
                     {
                         auto& s = pat.steps[lanes[lane]->id][(size_t) step];
                         s.velocity = s.on ? juce::jmax (s.velocity, velocity) : velocity;
                         s.on = true;
                     }

                     pat.name = "Beatbox ("_t + source + ")";
                     out->setProperty ("kick", counts[0]);
                     out->setProperty ("snare", counts[1]);
                     out->setProperty ("hat", counts[2]);
                     out->setProperty ("hits", list);

                     if (! missing.isEmpty())
                         out->setProperty ("note", "Sem canal no rack para: "_t + missing.joinIntoString (", ") + ". Ponha um e rode de novo.");
                 }
                 else
                 {
                     juce::String error;
                     const auto* target = melodyChannel (p, a, error);

                     if (target == nullptr)
                         return CommandResult::fail (error);

                     const auto found = audiomidi::melodyNotes (mono, rate);

                     if (found.empty())
                         return CommandResult::fail ("Não achei notas nesse áudio (uma voz ou um instrumento só, sem a bateria por baixo)."_t);

                     std::vector<Note> notes;
                     int end = 0;

                     for (auto& n : found)
                     {
                         int start = (int) std::lround (ticksAt (n.start)), stop = (int) std::lround (ticksAt (n.end));

                         if (quantize)
                         {
                             start = (int) std::lround ((double) start / ticksPerStep) * ticksPerStep;
                             stop = (int) std::lround ((double) stop / ticksPerStep) * ticksPerStep;
                         }

                         stop = juce::jmax (stop, start + (quantize ? ticksPerStep : ticksPerStep / 4));

                         if (start < 0 || stop > maxSteps * ticksPerStep)
                             continue;

                         // a nota encaixada não passa por cima da anterior
                         if (! notes.empty() && notes.back().end() > start)
                             notes.back().length = juce::jmax (ticksPerStep / 4, start - notes.back().start);

                         Note note;
                         note.id = (int) notes.size() + 1;
                         note.start = start;
                         note.length = stop - start;
                         note.key = juce::jlimit (0, 127, n.key);
                         note.velocity = n.velocity;
                         notes.push_back (note);
                         end = juce::jmax (end, stop);
                     }

                     if (notes.empty())
                         return CommandResult::fail ("As notas ficaram fora dos 16 compassos de um padrão."_t);

                     pat.lengthSteps = juce::jlimit (16, maxSteps, ((end + ticksPerStep - 1) / ticksPerStep + 15) / 16 * 16);
                     pat.notes[target->id] = notes;
                     pat.name = "Melodia ("_t + source + ")";

                     juce::Array<juce::var> keys;

                     for (auto& n : notes)
                         keys.add (n.key);

                     out->setProperty ("channel", target->id);
                     out->setProperty ("notes", (int) notes.size());
                     out->setProperty ("keys", keys);
                 }

                 p.patterns.push_back (pat);
                 p.currentPattern = pat.id;
                 out->setProperty ("pattern", pat.id);
                 out->setProperty ("name", pat.name);
                 out->setProperty ("steps", pat.lengthSteps);
                 return CommandResult::success (juce::var (out.get()));
             } });
}

} // namespace whynot
