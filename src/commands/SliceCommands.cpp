// Fatiador (pedidos de 21/10 à tarde, frente C): fatiar um beat ou um sample por ataque ou pela
// grade, uma tecla por fatia (da grave para a aguda, como o fatiador do FL), as fatias para o
// piano roll e o "picotar" do clipe de áudio da playlist por ataque. Dono: Sampler.
//
// As fatias são campos opcionais de um canal do sampler (Channel::slices): sem kind novo, o .wnl
// fica no esquema 4. O som sai do WhynotSampler, que toca cada fatia no tom original até a seguinte.

#include "CommandRegistry.h"
#include "../model/PlaylistEdit.h"
#include "../record/BlockFile.h"
#include "../record/ReversedAudio.h"
#include "../engine/SampleAnalysis.h"

namespace whynot
{

namespace
{
    using params::schema;

    audio::Sensitivity sensitivityOf (const juce::var& a)
    {
        const auto s = params::string (a, "sensitivity", "medium");
        return s == "dull" ? audio::Sensitivity::dull : s == "sharp" ? audio::Sensitivity::sharp : audio::Sensitivity::medium;
    }

    /** "1/4" (um tempo), "1/8", "1/16", "1/32", "beat" ou "bar", em tempos. */
    double divisionBeats (const juce::var& a)
    {
        const auto d = params::string (a, "division", "1/8");
        return d == "bar" ? 4.0 : d == "beat" || d == "1/4" ? 1.0 : d == "1/16" ? 0.25 : d == "1/32" ? 0.125 : 0.5;
    }

    juce::Result checkMode (const juce::var& a, SliceMode& mode)
    {
        const auto m = params::string (a, "mode", "attack");
        mode = m == "grid" ? SliceMode::grid : m == "none" ? SliceMode::none : SliceMode::attack;
        return m == "attack" || m == "grid" || m == "none" ? juce::Result::ok()
                                                           : juce::Result::fail ("Modo de fatiar desconhecido: use attack, grid ou none."_t);
    }

    /** Fatia o sample do canal e guarda as fatias nele. */
    juce::Result sliceChannel (CommandContext& c, Channel& ch, const juce::var& a, SliceResult& result)
    {
        SliceMode mode = SliceMode::attack;

        if (auto mr = checkMode (a, mode); mr.failed())
            return mr;

        if (ch.kind != "sampler" || ch.samplePath.isEmpty() || ch.instrument.isNotEmpty())
            return juce::Result::fail ("Só um canal do sampler com sample pode ser fatiado."_t);

        const double bpm = params::number (a, "bpm", 0.0);
        result = sliceFile (c.model.resolveSample (ch.samplePath), mode, sensitivityOf (a), divisionBeats (a),
                            std::isfinite (bpm) ? bpm : 0.0, c.model.get().bpm);

        if (! result.ok)
            return juce::Result::fail (result.error);

        // A fatia toca inteira e só uma vez por nota: one-shot, sem mono, sem laço.
        ch.slices = result.starts;
        ch.sliceBpm = mode == SliceMode::none ? 0.0f : (float) (std::round (result.bpm * 1000.0) / 1000.0);

        for (auto& v : ch.slices)   // as mesmas 6 casas do .wnl: salvar e reabrir dá as mesmas fatias
            v = (float) (std::round ((double) v * 1.0e6) / 1.0e6);

        const double key = params::number (a, "key", ch.sliceKey);
        ch.sliceKey = juce::jlimit (0, 128 - juce::jmax (1, (int) ch.slices.size()), std::isfinite (key) ? juce::roundToInt (key) : 60);

        if (! ch.slices.empty())
        {
            ch.oneShot = true;
            ch.mono = false;
            ch.loop = false;

            // A fatia começa num cruzamento de zero: sem a rampa de 1 ms de fábrica, o ataque da
            // batida sai inteiro (um chimbal de 15 ms perdia o estalo). Um ataque mexido fica.
            if (ch.attackMs == Channel {}.attackMs)
                ch.attackMs = 0.0f;
        }

        return juce::Result::ok();
    }

    /** Uma nota por fatia no padrão, no tempo em que ela soa no beat (no andamento dele). */
    juce::Result dumpToPianoRoll (Project& p, const Channel& ch, Pattern& pat, double seconds)
    {
        if (ch.slices.empty())
            return juce::Result::fail ("O canal não tem fatias."_t);

        const double bpm = ch.sliceBpm > 0.0f ? (double) ch.sliceBpm : p.bpm;
        const double beatTicks = seconds * bpm / 60.0 * ticksPerBeat;   // o sample inteiro, em ticks do beat
        auto& notes = pat.notes[ch.id];
        notes.clear();

        for (size_t i = 0; i < ch.slices.size(); ++i)
        {
            const double from = ch.slices[i];
            const double to = i + 1 < ch.slices.size() ? ch.slices[i + 1] : 1.0;
            Note n;
            n.id = (int) i + 1;
            n.start = juce::roundToInt (from * beatTicks);
            n.length = juce::jmax (1, juce::roundToInt (to * beatTicks) - n.start);
            n.key = ch.sliceKey + (int) i;
            n.velocity = 100;
            notes.push_back (n);
        }

        // O padrão cresce, de compasso em compasso, até caber o beat (até 256 passos).
        const int stepsNeeded = juce::roundToInt (std::ceil (beatTicks / ticksPerStep - 1.0e-6));

        if (stepsNeeded > pat.lengthSteps)
            pat.lengthSteps = juce::jmin (256, ((stepsNeeded + 15) / 16) * 16);

        return juce::Result::ok();
    }

    juce::var sliceInfo (const Channel& ch, const SliceResult& r)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("channel", ch.id);
        o->setProperty ("slices", (int) ch.slices.size());
        o->setProperty ("firstKey", ch.sliceKey);
        o->setProperty ("lastKey", ch.sliceKey + juce::jmax (0, (int) ch.slices.size() - 1));
        o->setProperty ("bpm", std::round (r.bpm * 100.0) / 100.0);
        o->setProperty ("seconds", std::round (r.seconds * 1000.0) / 1000.0);
        o->setProperty ("truncated", r.truncated);
        return juce::var (o);
    }

    void insertBeforeMaster (Project& p, MixerTrack t)
    {
        auto it = std::find_if (p.mixer.begin(), p.mixer.end(), [] (const MixerTrack& m) { return m.isMaster(); });
        p.mixer.insert (it, std::move (t));
    }

    Pattern* targetPattern (Project& p, const juce::var& a)
    {
        const auto id = params::string (a, "pattern");
        return id.isEmpty() ? p.currentPatternPtr() : p.findPattern (id);
    }

    /** A ordem dos papéis num kit: a bateria primeiro, como o produtor monta o rack. */
    int roleOrder (SampleRole r)
    {
        switch (r)
        {
            case SampleRole::kick:    return 0;
            case SampleRole::snare:   return 1;
            case SampleRole::clap:    return 2;
            case SampleRole::hat:     return 3;
            case SampleRole::openHat: return 4;
            case SampleRole::perc:    return 5;
            case SampleRole::cymbal:  return 6;
            case SampleRole::bass:    return 7;
            case SampleRole::fx:      return 8;
            case SampleRole::vocal:   return 9;
            case SampleRole::loop:    return 10;
            case SampleRole::unknown: break;
        }

        return 11;
    }

    /** O papel pelo nome do arquivo e das pastas dele até o kit (a pasta do kit não conta:
        "Trap 808 Kit" não faz de todo som um 808). */
    SampleRole roleInKit (const juce::File& file, const juce::File& kit)
    {
        if (const auto role = roleFromName (file.getFileNameWithoutExtension()); role != SampleRole::unknown)
            return role;

        for (auto folder = file.getParentDirectory(); folder.isAChildOf (kit); folder = folder.getParentDirectory())
            if (const auto role = roleFromName (folder.getFileName()); role != SampleRole::unknown)
                return role;

        return SampleRole::unknown;
    }

    // Os campos de fatiar, iguais no channel.slice e no channel.addSlicer.
    #define WHYNOT_SLICE_FIELDS \
        { "mode", "string", false, "attack (padrão: pelas batidas), grid (fatias iguais) ou none (tira as fatias)"_t }, \
        { "sensitivity", "string", false, "Por ataque: dull (só as batidas fortes), medium (padrão) ou sharp (até as fracas)"_t }, \
        { "division", "string", false, "Pela grade: 1/4, 1/8 (padrão), 1/16, 1/32, beat ou bar"_t }, \
        { "bpm", "number", false, "Andamento do beat; ausente = o do nome do arquivo, o do tamanho ou o do projeto"_t }, \
        { "key", "integer", false, "Tecla da primeira fatia (padrão 60, C4)"_t }
}

void registerSliceCommands (CommandRegistry& r)
{
    r.add ({ "channel.slice", "Fatiar o sample",
             schema ({ { "channel", "string", true, "Id do canal (sampler)" },
                       WHYNOT_SLICE_FIELDS,
                       { "toPianoRoll", "boolean", false, "Também põe uma nota por fatia no piano roll (padrão false)"_t },
                       { "pattern", "string", false, "Padrão das notas; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* ch = p.findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 SliceResult result;

                 if (auto sr = sliceChannel (c, *ch, a, result); sr.failed())
                     return CommandResult::fail (sr.getErrorMessage());

                 if (params::boolean (a, "toPianoRoll", false) && ! ch->slices.empty())
                 {
                     auto* pat = targetPattern (p, a);

                     if (pat == nullptr)
                         return CommandResult::fail ("Padrão não encontrado."_t);

                     if (auto dr = dumpToPianoRoll (p, *ch, *pat, result.seconds); dr.failed())
                         return CommandResult::fail (dr.getErrorMessage());
                 }

                 return CommandResult::success (sliceInfo (*ch, result));
             } });

    r.add ({ "channel.slicesToPianoRoll", "Fatias para o piano roll",
             schema ({ { "channel", "string", true, "Id do canal fatiado" },
                       { "pattern", "string", false, "Padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* ch = p.findChannel (params::string (a, "channel"));
                 auto* pat = targetPattern (p, a);

                 if (ch == nullptr || pat == nullptr)
                     return CommandResult::fail ("Canal ou padrão não encontrado."_t);

                 // O tamanho do sample vem do arquivo (o .wnl não guarda): uma leitura rápida do cabeçalho.
                 const auto file = c.model.resolveSample (ch->samplePath);
                 juce::AudioFormatManager formats;
                 formats.registerBasicFormats();
                 std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

                 if (reader == nullptr || reader->sampleRate <= 0.0)
                     return CommandResult::fail ("Não deu para ler o sample do canal."_t);

                 const double seconds = (double) (reader->lengthInSamples - juce::jmax (0, mp3PrimingFrames (file))) / reader->sampleRate;   // o que toca

                 if (auto dr = dumpToPianoRoll (p, *ch, *pat, seconds); dr.failed())
                     return CommandResult::fail (dr.getErrorMessage());

                 auto o = new juce::DynamicObject();
                 o->setProperty ("notes", (int) ch->slices.size());
                 o->setProperty ("steps", pat->lengthSteps);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "channel.addSlicer", "Fatiar um beat",
             schema ({ { "path", "string", true, "Arquivo de áudio do beat ou do sample (WAV, AIFF, FLAC, OGG, MP3)"_t },
                       { "name", "string", false, "Nome do canal; padrão = nome do arquivo"_t },
                       WHYNOT_SLICE_FIELDS,
                       { "toPianoRoll", "boolean", false, "Põe uma nota por fatia no piano roll do padrão atual (padrão true)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const juce::File file (params::string (a, "path"));

                 if (! file.existsAsFile())
                     return CommandResult::fail ("Arquivo não encontrado: "_t + file.getFullPathName());

                 auto& p = c.model.data();
                 Channel ch;
                 ch.id = makeId ("ch");
                 ch.name = params::string (a, "name", file.getFileNameWithoutExtension()).trim();
                 ch.colour = channelColourForIndex ((int) p.channels.size());
                 ch.samplePath = file.getFullPathName().replaceCharacter ('\\', '/');

                 SliceResult result;

                 if (auto sr = sliceChannel (c, ch, a, result); sr.failed())
                     return CommandResult::fail (sr.getErrorMessage());

                 MixerTrack mx;
                 mx.id = makeId ("mx");
                 mx.name = ch.name;
                 mx.colour = ch.colour;
                 mx.autoFor = ch.id;
                 mx.volumeDb = -6.0f;   // o mesmo headroom de um canal novo
                 ch.output = mx.id;

                 p.channels.push_back (ch);
                 insertBeforeMaster (p, mx);

                 if (params::boolean (a, "toPianoRoll", true) && ! ch.slices.empty())
                     if (auto* pat = p.currentPatternPtr())
                         dumpToPianoRoll (p, p.channels.back(), *pat, result.seconds);

                 auto info = sliceInfo (p.channels.back(), result);
                 info.getDynamicObject()->setProperty ("mixerTrack", mx.id);
                 return CommandResult::success (info);
             } });

    // Uma pasta de kit inteira no channel rack: um canal por som, na ordem da bateria, cada um com
    // o papel adivinhado (o 808 mono e com glide, os chimbais no mesmo grupo de corte).
    r.add ({ "channel.addKit", "Adicionar kit",
             schema ({ { "folder", "string", true, "Pasta do kit: um canal por som (WAV, AIFF, FLAC, OGG, MP3), nas subpastas também"_t },
                       { "max", "integer", false, "Quantos sons no máximo (padrão 64, até 128)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const juce::File kit (params::string (a, "folder"));

                 if (! kit.isDirectory())
                     return CommandResult::fail ("Pasta não encontrada: "_t + kit.getFullPathName());

                 const double maxValue = params::number (a, "max", 64.0);
                 const int max = std::isfinite (maxValue) ? juce::jlimit (1, 128, (int) maxValue) : 64;

                 struct Sound
                 {
                     juce::File file;
                     SampleRole role = SampleRole::unknown;
                     SampleProfile profile;
                     bool analysed = false;
                 };

                 std::vector<Sound> sounds;

                 for (auto& f : kit.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", juce::File::FollowSymlinks::no))
                 {
                     if (f.isHidden() || f.getFileName().startsWithChar ('.'))
                         continue;

                     // O nome decide rápido; só o som sem nome que diga passa pela análise (o 808 sem "808").
                     Sound s { f, roleInKit (f, kit), {}, false };

                     if (s.role == SampleRole::unknown)
                     {
                         s.profile = analyseSample (f);
                         s.analysed = true;
                         s.role = soundsLikeBass (s.profile) ? SampleRole::bass : SampleRole::unknown;
                     }

                     sounds.push_back (std::move (s));
                 }

                 if (sounds.empty())
                     return CommandResult::fail ("A pasta não tem sons (WAV, AIFF, FLAC, OGG ou MP3)."_t);

                 std::stable_sort (sounds.begin(), sounds.end(), [] (const Sound& x, const Sound& y)
                 {
                     if (roleOrder (x.role) != roleOrder (y.role))
                         return roleOrder (x.role) < roleOrder (y.role);

                     return x.file.getFullPathName().compareNatural (y.file.getFullPathName()) < 0;
                 });

                 const int skipped = juce::jmax (0, (int) sounds.size() - max);

                 if ((int) sounds.size() > max)
                     sounds.resize ((size_t) max);

                 auto has = [&sounds] (SampleRole r) { return std::any_of (sounds.begin(), sounds.end(), [r] (const Sound& s) { return s.role == r; }); };
                 const bool hats = has (SampleRole::hat) && has (SampleRole::openHat);

                 auto& p = c.model.data();
                 juce::Array<juce::var> channels, roles;

                 for (auto& s : sounds)
                 {
                     Channel ch;
                     ch.id = makeId ("ch");
                     ch.name = s.file.getFileNameWithoutExtension();
                     ch.colour = channelColourForIndex ((int) p.channels.size());
                     ch.samplePath = s.file.getFullPathName().replaceCharacter ('\\', '/');

                     // A nota só onde ela importa (808, melodia, voz): a bateria fica em C4, como no FL.
                     const bool pitched = s.role == SampleRole::bass || s.role == SampleRole::unknown
                                          || s.role == SampleRole::vocal || s.role == SampleRole::fx || s.role == SampleRole::loop;

                     if (pitched)
                     {
                         if (! s.analysed)
                             s.profile = analyseSample (s.file);

                         if (rootIsReliable (s.profile.pitch))
                         {
                             ch.rootKey = s.profile.pitch.key;
                             ch.rootCents = (float) s.profile.pitch.cents;
                         }
                     }

                     ch.mono = s.role == SampleRole::bass;
                     ch.oneShot = s.role != SampleRole::bass;
                     ch.cutItself = s.role == SampleRole::openHat;
                     ch.attackMs = isDrumRole (s.role) ? 0.0f : ch.attackMs;   // o clique do kick é o ataque dele
                     ch.chokeGroup = hats && (s.role == SampleRole::hat || s.role == SampleRole::openHat) ? 1 : 0;

                     MixerTrack mx;
                     mx.id = makeId ("mx");
                     mx.name = ch.name;
                     mx.colour = ch.colour;
                     mx.autoFor = ch.id;
                     mx.volumeDb = -6.0f;
                     ch.output = mx.id;

                     p.channels.push_back (ch);
                     insertBeforeMaster (p, mx);
                     channels.add (ch.id);
                     roles.add (juce::String (roleId (s.role)));
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("channels", channels);
                 o->setProperty ("roles", roles);
                 o->setProperty ("skipped", skipped);
                 return CommandResult::success (juce::var (o));
             } });

    // O "picotar" do clipe de áudio: o bloco vira um bloco por batida, cada um começando no ataque,
    // como o corte da ferramenta Cortar (o da direita continua de onde o anterior parou).
    r.add ({ "playlist.chopItem", "Picotar o clipe",
             schema ({ { "item", "string", true, "Id do bloco de áudio" },
                       { "mode", "string", false, "attack (padrão: nas batidas), beat (a cada tempo) ou bar (a cada compasso)"_t },
                       { "sensitivity", "string", false, "Por ataque: dull, medium (padrão) ou sharp"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto itemId = params::string (a, "item");
                 const auto* item = p.findItem (itemId);

                 if (item == nullptr)
                     return CommandResult::fail ("Bloco da playlist não encontrado."_t);

                 if (item->kind != "audio")
                     return CommandResult::fail ("Só um clipe de áudio pode ser picotado."_t);

                 const auto mode = params::string (a, "mode", "attack");
                 std::vector<int> cuts;   // ticks da playlist

                 if (mode == "attack")
                 {
                     const auto* ch = p.findChannel (item->ref);

                     if (ch == nullptr)
                         return CommandResult::fail ("Canal do clipe não encontrado."_t);

                     const double clipSeconds = ticksToSeconds (item->length, p.bpm);
                     const double from = item->offsetSeconds, to = from + item->fileSeconds (clipSeconds);

                     for (double at : onsetsIn (blockfile::playedOf (c.model, *ch, *item), from, to, sensitivityOf (a)))
                         cuts.push_back (item->start + juce::roundToInt (secondsToTicks ((at - from) * item->stretch, p.bpm)));
                 }
                 else if (mode == "beat" || mode == "bar")
                 {
                     const int grid = mode == "bar" ? ticksPerBar4 : ticksPerBeat;

                     for (int at = (item->start / grid + 1) * grid; at < item->end(); at += grid)
                         cuts.push_back (at);
                 }
                 else
                 {
                     return CommandResult::fail ("Modo desconhecido: use attack, beat ou bar."_t);
                 }

                 // Cortes dentro do bloco e com pelo menos meio passo entre eles.
                 juce::Array<juce::var> ids { juce::var (itemId) };
                 auto currentId = itemId;
                 int previous = item->start;

                 for (int at : cuts)
                 {
                     auto* cur = p.findItem (currentId);

                     if (cur == nullptr || at - previous < ticksPerStep / 2 || at >= cur->end() - ticksPerStep / 2)
                         continue;

                     auto right = playlistedit::splitAt (p, *cur, at);   // os fades ficam nas pontas de fora
                     right.id = makeId ("it");
                     p.playlistItems.push_back (right);   // cur pode mudar de endereço daqui em diante
                     currentId = right.id;
                     ids.add (right.id);
                     previous = at;
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("items", ids);
                 return CommandResult::success (juce::var (o));
             } });
}

#undef WHYNOT_SLICE_FIELDS

} // namespace whynot
