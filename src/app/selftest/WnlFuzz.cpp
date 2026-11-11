// Autoteste do .wnl que vem de fora (frente wnl-seguro, 10/11): as regras de caminho ao abrir e ao coletar,
// os tetos, o aviso de "salvo por um app mais novo" e um fuzz do wnl::fromJson + normalise.
//
// O fuzz parte de um projeto válido, em JSON, e estraga a árvore: tipos trocados, números absurdos, infinito,
// textos gigantes, listas enormes, ids repetidos, chaves que somem e caminhos hostis. Semente fixa (muda com
// WHYNOT_SELFTEST_SEED), quantidade em WHYNOT_SELFTEST_WNLFUZZ (padrão 2000). Para cada arquivo:
//   1. o fromJson volta (sem cair e sem demorar): ou recusa com mensagem, ou abre;
//   2. o que abriu passa nas invariantes do modelo (checkInvariants, abaixo);
//   3. o que abriu vira JSON e reabre; a partir da segunda gravação o JSON não muda mais.
// O primeiro arquivo que falha fica em wnl-fuzz-falha.json, na pasta do autoteste.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"
#include "../../model/WnlPaths.h"

#include <cmath>
#include <iterator>
#include <set>
#include <vector>

namespace whynot::selftest
{

namespace
{
    // Um projeto pequeno com um pouco de tudo o que o fromJson lê.
    const char* const seedJson = R"JSON({
  "format": "whynot-lab-project",
  "schemaVersion": 4,
  "app": { "name": "WHYNOT LAB", "version": "0.1.0" },
  "project": { "name": "Semente", "bpm": 140.0, "timeSignature": [4, 4], "key": { "root": "C", "scale": "minor" },
               "metronome": false, "loop": true, "songMode": true, "currentPattern": "pt_a", "swing": 12.5,
               "punch": { "on": false, "in": 0, "out": 3840 } },
  "channels": [
    { "id": "ch_kick", "name": "Kick", "kind": "sampler", "color": "#E5484D",
      "sample": { "path": "samples/kick.wav", "originalPath": "C:/Samples/kick.wav" },
      "sampler": { "rootKey": 60, "oneShot": true, "cutItself": true, "mono": false, "glideMs": 0.0, "tune": 1.5, "attackMs": 2.0,
                   "releaseMs": 80.0, "bendRange": 2, "loop": false, "loopStartPpm": 100000, "loopEndPpm": 900000,
                   "layers": [ { "path": "samples/kick_forte.wav", "minVelocity": 100 } ] },
      "mute": false, "choke": 1, "swing": 10.0, "noteSwing": 5.0, "humanize": 3.0, "output": "mx_kick" },
    { "id": "ch_vox", "name": "Voz", "kind": "audio", "color": "#3E63DD",
      "sample": { "path": "samples/voz.wav" }, "sampler": { "rootKey": 60 },
      "noteTune": { "formantFollows": true, "notes": [ { "start10us": 0, "end10us": 50000, "sourceCents": 6000, "shiftCents": 100,
                                                         "centerPct": 50, "flattenPct": 20, "formantCents": 0, "transitionMs": 40.0 } ] },
      "mute": false, "output": "mx_vox" },
    { "id": "ch_synth", "name": "Lead", "kind": "synth", "color": "#A6A6A6", "sample": { "path": "" }, "sampler": { "rootKey": 60 },
      "synth": { "preset": "init", "params": { "cutoff": 0.5, "nada": 3.0 } }, "mute": false, "output": "mx_master" },
    { "id": "ch_plug", "name": "Plugin", "kind": "plugin", "color": "#A6A6A6", "sample": { "path": "" }, "sampler": { "rootKey": 60 },
      "plugin": { "name": "Um Synth", "manufacturer": "Alguem", "format": "VST3", "file": "C:/Program Files/Common Files/VST3/Um Synth.vst3",
                  "uid": "abc", "instrument": true, "state": "AAAA" },
      "mute": false, "output": "mx_master" },
    { "id": "ch_chop", "name": "Chop", "kind": "sampler", "color": "#30A46C", "sample": { "path": "samples/chop.wav" },
      "sampler": { "rootKey": 60, "slicesPpm": [0, 250000, 500000, 750000], "sliceKey": 36, "sliceBpm": 140.0 },
      "mute": false, "output": "mx_master" }
  ],
  "patterns": [
    { "id": "pt_a", "name": "A", "lengthSteps": 16,
      "lanes": {
        "ch_kick": { "steps": [ { "i": 0, "vel": 100 }, { "i": 4, "vel": 90, "roll": 2, "chance": 50, "pitch": 3, "pan": -20 } ] },
        "ch_synth": { "notes": [ { "id": 1, "t": 0, "len": 480, "key": 60, "vel": 100,
                                   "pitch": [ { "t": 0, "v": 0.0 }, { "t": 240, "v": 2.0 } ], "mod": [ { "t": 0, "v": 0.5 } ] },
                                 { "id": 2, "t": 960, "len": 240, "key": 67, "vel": 90, "slide": true, "muted": true } ] }
      } },
    { "id": "pt_b", "name": "B", "lengthSteps": 32, "lanes": { "ch_kick": { "steps": [ { "i": 31, "vel": 127 } ] } } }
  ],
  "playlist": {
    "tracks": [ { "id": "pl_01", "name": "Raia 1", "colour": "#112233", "muted": false, "solo": false, "recordTrack": "mx_vox" },
                { "id": "pl_02", "name": "Raia 2" } ],
    "items": [ { "id": "it_1", "kind": "pattern", "ref": "pt_a", "track": "pl_01", "start": 0, "length": 3840, "patternOffset": 0 },
               { "id": "it_2", "kind": "audio", "ref": "ch_vox", "track": "pl_02", "start": 3840, "length": 7680, "offsetSeconds": 1.25,
                 "stretch": 1.5, "gainDb": -3.0, "fadeIn": 0.1, "fadeOut": 0.2, "fadeInCurve": "s", "pitch": 2.0, "pan": 0.5,
                 "takeGroup": "tg_1", "take": 2, "file": "samples/voz_consolidada.wav" } ],
    "markers": [ { "id": "mk_1", "t": 3840, "name": "Refrão" } ]
  },
  "mixer": {
    "tracks": [
      { "id": "mx_kick", "name": "Kick", "color": "#E5484D", "volumeDb": -3.0, "pan": 0.0, "mute": false, "solo": false,
        "output": "mx_master", "autoFor": "ch_kick",
        "inserts": [ { "id": "fx_1", "kind": "whynot", "type": "eq", "bypass": false, "params": { "x": 0.0 } },
                     { "id": "fx_2", "kind": "whynot", "type": "comp", "bypass": false, "sidechain": "mx_vox", "params": {} },
                     { "id": "fx_3", "kind": "vst3", "type": "ext", "bypass": true,
                       "plugin": { "name": "Um Efeito", "manufacturer": "Alguem", "format": "VST3", "file": "C:/VST3/Um Efeito.vst3",
                                   "uid": "def", "instrument": false, "state": "BBBB" }, "params": {} } ],
        "sends": [ { "to": "mx_rev", "db": -12.0 } ],
        "macros": [ { "name": "Brilho", "value": 0.5, "links": [ { "insert": "fx_1", "param": "x", "min": 0.0, "max": 1.0 } ] } ] },
      { "id": "mx_vox", "name": "Voz", "color": "#3E63DD", "volumeDb": 0.0, "pan": 0.1, "mute": false, "solo": false, "output": "mx_master",
        "armed": true, "monitor": "on", "inserts": [], "sends": [ { "to": "mx_rev", "db": -6.0, "pre": true } ] },
      { "id": "mx_rev", "name": "Reverb", "color": "#FFFFFF", "volumeDb": 0.0, "pan": 0.0, "mute": false, "solo": false, "output": "mx_master",
        "inserts": [], "sends": [] },
      { "id": "mx_master", "name": "Master", "color": "#FFFFFF", "volumeDb": 0.0, "pan": 0.0, "mute": false, "solo": false, "inserts": [], "sends": [] }
    ]
  },
  "automation": [ { "id": "au_1", "target": { "kind": "mixer", "track": "mx_kick", "channel": "", "insert": "", "param": "volume" },
                    "points": [ { "t": 0, "v": 0.5 }, { "t": 1920, "v": 1.0, "tension": 0.3 }, { "t": 3840, "v": 0.0 } ] } ],
  "midiMap": [ { "channel": 1, "cc": 74, "kind": "mixer", "track": "mx_kick", "insert": "", "param": "volume" } ]
})JSON";

    // Caminhos que um arquivo montado de propósito traria. O NBSP na frente (\u00A0) some no trim do normalise.
    const char* const hostilePaths[] = {
        "\\\\host\\share\\kick.wav", "//host/share/kick.wav", "\\\\?\\UNC\\host\\share\\kick.wav", "\\\\?\\C:\\Users\\x\\kick.wav",
        "\\\\.\\pipe\\coisa", "\\/host/share/x.wav", " //host/share/x.wav", "\xC2\xA0//host/share/x.wav", "\t\\\\host\\share\\x.wav",
        "../../Documents/contrato.docx", "..\\..\\..\\Windows\\win.ini", "samples/../../fora.wav", "C:kick.wav", "\\Windows\\win.ini",
        "C:/Users/x/Documents/contrato.docx", "C:/x/arquivo.txt:fluxo.wav", "samples/CON", "samples/kick.wav:oculto", "NUL", "..", ".",
        "C:/Windows/System32/config/SAM", "http://host/kick.wav", "file:///C:/x.wav", "samples/kick?.wav", "C:\\", "//", "\\\\"
    };

    /** Um lugar da árvore: o valor, e onde ele mora (a chave no objeto ou a posição na lista). */
    struct Slot
    {
        juce::var* value = nullptr;
        juce::var* parent = nullptr;
        juce::Identifier name;
        int index = -1;
    };

    void collectSlots (juce::var& node, std::vector<Slot>& out, int depth = 0)
    {
        if (depth > 40 || out.size() > 20000)
            return;

        if (auto* object = node.getDynamicObject())
        {
            auto& props = object->getProperties();

            for (int i = 0; i < props.size(); ++i)
            {
                out.push_back ({ props.getVarPointerAt (i), &node, props.getName (i), -1 });
                collectSlots (*props.getVarPointerAt (i), out, depth + 1);
            }
        }
        else if (auto* list = node.getArray())
        {
            for (int i = 0; i < list->size(); ++i)
            {
                out.push_back ({ &list->getReference (i), &node, {}, i });
                collectSlots (list->getReference (i), out, depth + 1);
            }
        }
    }

    juce::var hostileValue (juce::Random& random)
    {
        switch (random.nextInt (22))
        {
            case 0:   return juce::var();                                  // null
            case 1:   return true;
            case 2:   return -1;
            case 3:   return 0;
            case 4:   return 2147483647;
            case 5:   return (juce::int64) 9223372036854775807LL;
            case 6:   return (juce::int64) -9223372036854775807LL;
            case 7:   return 1.0e308;
            case 8:   return -1.0e308;
            case 9:   return 4.9e-324;
            case 10:  return 1.0e15;
            case 11:  return juce::String ("@@INF@@");                     // vira 1e999 no texto
            case 12:  return juce::String ("@@NEGINF@@");
            case 13:  return juce::String ("@@NAN@@");                     // vira NaN no texto: não é JSON, tem de ser recusado
            case 14:  return juce::String();
            case 15:  return juce::String::repeatedString ("A", 1 + random.nextInt (200000));
            case 16:  return juce::String::fromUTF8 ("mx_master");
            case 17:  return juce::String::fromUTF8 ("ch_kick");
            case 18:  return juce::var (juce::Array<juce::var>());
            case 19:  return juce::var (new juce::DynamicObject());
            default:  break;
        }

        return juce::String::fromUTF8 (hostilePaths[random.nextInt ((int) std::size (hostilePaths))]);
    }

    /** Uma mudança na árvore. */
    void mutate (juce::var& root, juce::Random& random)
    {
        std::vector<Slot> slots;
        collectSlots (root, slots);

        if (slots.empty())
            return;

        auto& slot = slots[(size_t) random.nextInt ((int) slots.size())];

        switch (random.nextInt (10))
        {
            case 0:   // a chave some
                if (auto* object = slot.parent->getDynamicObject(); object != nullptr && slot.index < 0)
                    object->removeProperty (slot.name);
                break;

            case 1:   // um item da lista repetido muitas vezes (ids repetidos e listas enormes)
                if (auto* list = slot.parent->getArray(); list != nullptr && slot.index >= 0)
                {
                    const auto copy = slot.value->clone();
                    const int times = random.nextInt (8) == 0 ? 1100 + random.nextInt (4000) : 1 + random.nextInt (40);

                    for (int i = 0; i < times; ++i)
                        list->add (copy);   // a mesma referência: o leitor só lê
                }
                break;

            case 2:   // um caminho hostil em qualquer texto
                if (slot.value->isString())
                    *slot.value = juce::String::fromUTF8 (hostilePaths[random.nextInt ((int) std::size (hostilePaths))]);
                break;

            case 3:   // um número vizinho de um limite
                if (slot.value->isInt() || slot.value->isDouble() || slot.value->isInt64())
                {
                    const double edges[] = { -1.0, 0.0, 1.0, 127.0, 128.0, 255.0, 256.0, 257.0, 999.0, 1000.0, 38400000.0, 38400001.0,
                                             2147483647.0, 2147483648.0, -2147483649.0, 0.5, -0.5, 1.0e9, 1.0e10 };
                    *slot.value = edges[random.nextInt ((int) std::size (edges))];
                }
                break;

            default:  // o valor troca por outro de qualquer tipo
                *slot.value = hostileValue (random);
                break;
        }
    }

    /** O JSON do arquivo mutado: os marcadores viram os números que o juce::var não guarda. */
    juce::String textOf (const juce::var& root)
    {
        return juce::JSON::toString (root, true)
                   .replace ("\"@@INF@@\"", "1e999")
                   .replace ("\"@@NEGINF@@\"", "-1e999")
                   .replace ("\"@@NAN@@\"", "NaN");
    }

    bool usable (const juce::String& path)
    {
        return wnl::paths::isUsable (wnl::paths::classify (path.toStdString()));
    }

    /** As invariantes de um projeto que o fromJson devolveu. Vazio = tudo certo; senão, a primeira que falhou. */
    juce::String checkInvariants (const Project& p)
    {
        if (p.findMixer (masterTrackId) == nullptr)                         return "sem master";
        if (p.patterns.empty() || p.currentPatternPtr() == nullptr)         return "sem padrão atual"_t;
        if (! std::isfinite (p.bpm) || p.bpm < 20.0 || p.bpm > 999.0)       return "bpm fora da faixa";
        if (! std::isfinite (p.swing) || p.swing < 0.0f || p.swing > 100.0f) return "swing fora da faixa";
        if (p.keyRoot < 0 || p.keyRoot > 11)                                return "tom fora da faixa";
        if (p.timeSigNum < 1 || p.timeSigNum > 32 || p.timeSigDen < 1 || p.timeSigDen > 32) return "compasso fora da faixa";

        if ((int) p.channels.size() > wnl::limits::channels || (int) p.mixer.size() > wnl::limits::mixerTracks + 1
            || (int) p.patterns.size() > wnl::limits::patterns || (int) p.playlistItems.size() > wnl::limits::playlistItems
            || (int) p.playlistTracks.size() > maxPlaylistTracks || (int) p.markers.size() > wnl::limits::markers
            || (int) p.automation.size() > wnl::limits::automationLanes || (int) p.midiMap.size() > maxMidiLinks)
            return "coleção acima do teto"_t;

        for (auto& c : p.channels)
        {
            if (c.id.isEmpty() || c.id.length() > wnl::limits::idLength)   return "id de canal inválido"_t;
            if (c.name.length() > wnl::limits::nameLength)                  return "nome de canal grande demais";
            if (p.findMixer (c.output) == nullptr)                          return "canal com saída que não existe"_t;
            if (! usable (c.samplePath) || ! usable (c.originalPath))       return "caminho de sample que o app não devia aceitar: "_t + c.samplePath.substring (0, 80);
            if (wnl::paths::classify (c.plugin.file.toStdString()) == wnl::paths::Kind::network) return "plugin com caminho de rede";
            if ((int) c.layers.size() > maxSampleLayers || c.slices.size() > 128) return "camadas ou fatias demais";

            for (auto& l : c.layers)
                if (! usable (l.samplePath))                                return "caminho de camada que o app não devia aceitar: "_t + l.samplePath.substring (0, 80);

            for (float v : { c.glideMs, c.rootCents, c.tune, c.attackMs, c.releaseMs, c.decayMs, c.sustain, c.pitchDrop, c.pitchDropMs,
                             c.start, c.loopStart, c.loopEnd, c.loopXfadeMs, c.swing, c.noteSwing, c.humanize, c.sliceBpm })
                if (! std::isfinite (v))                                    return "número inválido no canal "_t + c.id;

            for (auto& param : c.synthParams)
                if (! std::isfinite (param.second))                         return "parâmetro do synth inválido"_t;
        }

        for (auto& m : p.mixer)
        {
            if (! std::isfinite (m.volumeDb) || ! std::isfinite (m.pan))    return "número inválido no mixer"_t;
            if ((int) m.inserts.size() > maxInserts)                        return "inserts demais";
            if (! m.isMaster() && p.findMixer (p.resolveOutput (m.output)) == nullptr) return "trilha com saída que não existe"_t;

            for (auto& s : m.sends)
                if (p.findMixer (s.to) == nullptr || ! std::isfinite (s.levelDb) || p.reaches (s.to, m.id))
                    return "envio inválido ou em laço"_t;

            for (auto& ins : m.inserts)
            {
                if (ins.sidechain.isNotEmpty() && (p.findMixer (ins.sidechain) == nullptr || p.reaches (m.id, ins.sidechain)))
                    return "sidechain inválido ou em laço"_t;

                if (wnl::paths::classify (ins.plugin.file.toStdString()) == wnl::paths::Kind::network)
                    return "plugin de insert com caminho de rede";

                for (auto& param : ins.params)
                    if (! std::isfinite (param.second))                     return "parâmetro de efeito inválido"_t;
            }
        }

        for (auto& pat : p.patterns)
        {
            if (pat.id.isEmpty() || pat.lengthSteps < 1 || pat.lengthSteps > 256) return "padrão inválido"_t;

            for (auto& c : p.channels)
            {
                const auto lane = pat.steps.find (c.id);

                if (lane == pat.steps.end() || (int) lane->second.size() != pat.lengthSteps)
                    return "raia de passos faltando ou do tamanho errado";
            }

            if (pat.steps.size() != p.channels.size())                      return "raia de passos de um canal que não existe"_t;

            for (auto& [channel, notes] : pat.notes)
            {
                if (p.findChannel (channel) == nullptr)                     return "notas de um canal que não existe"_t;

                std::set<int> ids;

                for (auto& n : notes)
                {
                    if (n.start < 0 || n.start > maxTimelineTicks || n.length < 1 || n.key < 0 || n.key > 127
                        || n.velocity < 1 || n.velocity > 127 || n.id <= 0 || ! ids.insert (n.id).second)
                        return "nota inválida"_t;

                    for (auto* curve : { &n.pitch, &n.mod })
                        for (auto& pt : *curve)
                            if (! std::isfinite (pt.v))                     return "curva de nota inválida"_t;
                }
            }
        }

        for (auto& i : p.playlistItems)
        {
            if (i.id.isEmpty() || p.findPlaylistTrack (i.track) == nullptr) return "bloco sem raia";
            if (i.start < 0 || i.start > maxTimelineTicks || i.length < 1 || i.length > maxTimelineTicks) return "bloco fora da linha do tempo";
            if (! std::isfinite (i.offsetSeconds) || ! std::isfinite (i.stretch) || ! std::isfinite (i.gainDb) || ! std::isfinite (i.fadeIn)
                || ! std::isfinite (i.fadeOut) || ! std::isfinite (i.pitch) || ! std::isfinite (i.pan)) return "número inválido no bloco "_t + i.id;
            if (! usable (i.file))                                          return "arquivo de bloco que o app não devia aceitar: "_t + i.file.substring (0, 80);

            if (i.kind == "audio" ? (p.findChannel (i.ref) == nullptr || ! p.findChannel (i.ref)->isAudio()) : p.findPattern (i.ref) == nullptr)
                return "bloco que aponta para o que não existe"_t;
        }

        for (auto& lane : p.automation)
            for (auto& pt : lane.points)
                if (pt.t < 0 || pt.t > maxTimelineTicks || ! std::isfinite (pt.v) || pt.v < 0.0f || pt.v > 1.0f || ! std::isfinite (pt.tension))
                    return "ponto de automação inválido"_t;

        for (auto& m : p.markers)
            if (m.t < 0 || m.t > maxTimelineTicks || m.id.isEmpty())        return "marcador inválido"_t;

        return {};
    }

    juce::String withChannels (int count)
    {
        juce::String list;

        for (int i = 0; i < count; ++i)
            list << (i > 0 ? "," : "") << "{\"id\":\"ch_" << i << "\",\"name\":\"C\",\"kind\":\"sampler\"}";

        return "{\"format\":\"whynot-lab-project\",\"schemaVersion\":4,\"channels\":[" + list + "]}";
    }
}

void runWnlFuzzSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    rep.section ("Arquivo de terceiros e fuzz do .wnl"_t);

    juce::var seed;
    Project base;
    const bool seedOk = juce::JSON::parse (juce::String::fromUTF8 (seedJson), seed).wasOk()
                        && wnl::fromJson (juce::String::fromUTF8 (seedJson), base).wasOk();
    rep.check (seedOk && base.channels.size() == 5 && base.channels[0].layers.size() == 1 && base.channels[4].slices.size() == 4
                   && base.patterns.size() == 2 && base.playlistItems.size() == 2 && base.mixer.size() == 4 && checkInvariants (base).isEmpty(),
               "o projeto de partida abre inteiro e passa nas invariantes ("_t + (seedOk ? checkInvariants (base) : juce::String ("não abriu"_t)) + ")");

    if (! seedOk || base.channels.size() != 5 || base.channels[0].layers.size() != 1 || base.playlistItems.size() != 2)
        return;   // o resto mexe nesses itens pela posição

    auto set = [] (juce::var node, const char* key, const juce::var& value) { node.getDynamicObject()->setProperty (key, value); };

    //==========================================================================
    // A. Caminhos ao abrir: rede, ".." e inválido viram o nome do arquivo; o absoluto com letra fica.
    {
        auto doc = seed.clone();
        auto channels = doc["channels"];
        set (channels[0]["sample"], "path", "\\\\servidor\\kit\\kick.wav");
        set (channels[0]["sample"], "originalPath", "//servidor/kit/kick.wav");
        set (channels[0]["sampler"]["layers"][0], "path", "../../Documents/contrato.docx");
        set (channels[1]["sample"], "path", "C:/Users/Fulano/Música/voz.wav"_t);
        set (channels[1]["sample"], "originalPath", "D:/Gravações/voz.wav"_t);
        set (channels[3]["plugin"], "file", "\\\\servidor\\vst\\Um Synth.vst3");
        set (doc["playlist"]["items"][1], "file", " //servidor/kit/voz.wav");
        set (doc["mixer"]["tracks"][0]["inserts"][2]["plugin"], "file", "//servidor/vst/Um Efeito.vst3");

        Project p;
        wnl::LoadReport report;
        const bool opened = wnl::fromJson (juce::JSON::toString (doc), p, &report).wasOk();
        const auto* kick = p.findChannel ("ch_kick");
        const auto* vox = p.findChannel ("ch_vox");
        const auto* plug = p.findChannel ("ch_plug");
        const auto* item = p.findItem ("it_2");
        const auto* strip = p.findMixer ("mx_kick");

        rep.check (opened && kick != nullptr && kick->samplePath == "kick.wav" && kick->originalPath.isEmpty(),
                   "sample num caminho de rede (\\\\servidor\\...) abre como \"kick.wav\", sem o caminho de origem: nada toca a rede"_t);
        rep.check (opened && kick != nullptr && kick->layers.size() == 1 && kick->layers[0].samplePath == "contrato.docx",
                   "camada com \"../../\" fica só com o nome do arquivo, dentro da pasta do projeto"_t);
        rep.check (opened && vox != nullptr && vox->samplePath == "C:/Users/Fulano/Música/voz.wav"_t && vox->originalPath == "D:/Gravações/voz.wav"_t,
                   "caminho absoluto com a letra da unidade (o sample que o usuário arrastou e o originalPath) passa como veio"_t);
        rep.check (opened && item != nullptr && item->file == "voz.wav",
                   "arquivo de bloco com espaço na frente de \"//servidor\" também vira só o nome"_t);
        rep.check (opened && plug != nullptr && plug->plugin.file.isEmpty() && plug->plugin.name == "Um Synth"
                       && strip != nullptr && strip->inserts.size() == 3 && strip->inserts[2].plugin.file.isEmpty(),
                   "plugin apontado para a rede fica sem caminho (o nome fica, para aparecer como ausente)"_t);
        rep.check (report.refusedPathCount == 3 && report.refusedPaths.size() == 3 && ! report.savedByNewerApp(),
                   "o relatório da leitura conta os 3 caminhos recusados ("_t + juce::String (report.refusedPathCount) + ")");

        // O desfazer lê o texto do próprio app: o sample que o usuário arrastou de uma pasta de rede continua lá.
        Project session = base;
        session.channels[0].samplePath = "//nas/kits/kick.wav";
        Project undone;
        rep.check (wnl::fromSnapshot (wnl::toJson (session), undone).wasOk() && undone.channels[0].samplePath == "//nas/kits/kick.wav",
                   "o desfazer (fromSnapshot) não mexe no caminho de rede que entrou por uma ação do usuário"_t);
    }

    //==========================================================================
    // B. Coletar ao salvar: só áudio entra em samples/.
    {
        const auto outside = ctx.work.getChildFile ("wnl-fora");
        const auto folder = ctx.work.getChildFile ("wnl-terceiros");
        outside.deleteRecursively();
        folder.deleteRecursively();
        outside.createDirectory();
        const auto secret = outside.getChildFile ("contrato.docx");
        const auto hidden = outside.getChildFile ("senha.txt");
        const auto audio = outside.getChildFile ("meu kick.WAV");
        const auto layer = outside.getChildFile ("camada.flac");
        secret.replaceWithText ("segredo");
        hidden.replaceWithText ("1234");
        audio.replaceWithText ("RIFF de mentira");   // o save não lê o conteúdo: vale a extensão
        layer.replaceWithText ("fLaC de mentira");

        auto slash = [] (const juce::File& f) { return f.getFullPathName().replaceCharacter ('\\', '/'); };

        Project p = base;
        p.channels[0].samplePath = slash (secret);            // o "sample" do terceiro é um documento da vítima
        p.channels[0].originalPath = {};
        p.channels[0].layers[0].samplePath = slash (layer);
        p.channels[1].samplePath = slash (audio);
        p.channels[1].originalPath = {};
        p.playlistItems[1].file = slash (hidden);

        wnl::SaveReport report;
        const auto file = folder.getChildFile ("Beat.wnl");
        const bool saved = wnl::save (p, file, true, {}, &report).wasOk();
        const auto samples = folder.getChildFile ("samples");

        rep.check (saved && ! samples.getChildFile ("contrato.docx").exists() && ! samples.getChildFile ("senha.txt").exists()
                       && samples.findChildFiles (juce::File::findFiles, true).size() == 2,
                   "salvar com coleta não copia o .docx nem o .txt para samples/ (só os 2 áudios entram)"_t);
        rep.check (saved && p.channels[1].samplePath == "samples/meu kick.WAV" && p.channels[1].originalPath == slash (audio)
                       && p.channels[0].layers[0].samplePath == "samples/camada.flac" && samples.getChildFile ("meu kick.WAV").existsAsFile(),
                   "o áudio de fora da pasta continua sendo coletado, com o originalPath (extensão em qualquer caixa)"_t);
        rep.check (saved && p.channels[0].samplePath == slash (secret) && p.channels[0].originalPath.isEmpty() && p.playlistItems[1].file == slash (hidden)
                       && report.notCollected.size() == 2 && report.notCollected.contains (secret.getFullPathName()),
                   "o que não é áudio fica referenciado onde está e sai no relatório do save ("_t + report.notCollected.joinIntoString (", ") + ")");

        // dentro da pasta do projeto nada é copiado: a extensão não importa, a referência só vira relativa
        const auto inside = folder.getChildFile ("notas.txt");
        inside.replaceWithText ("x");
        p.playlistItems[1].file = slash (inside);
        rep.check (wnl::save (p, file, true, {}, &report).wasOk() && p.playlistItems[1].file == "notas.txt" && report.notCollected.size() == 1,
                   "arquivo que já está na pasta do projeto só vira caminho relativo"_t);
    }

    //==========================================================================
    // C. Tetos: arquivo acima do limite é recusado com mensagem; no limite, abre.
    {
        Project p;
        const auto tooMany = wnl::fromJson (withChannels (wnl::limits::channels + 1), p);
        rep.check (tooMany.failed() && tooMany.getErrorMessage().contains ("canais") && tooMany.getErrorMessage().contains (juce::String (wnl::limits::channels)),
                   "1001 canais: recusado com mensagem (\""_t + tooMany.getErrorMessage() + "\")");

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const bool atLimit = wnl::fromJson (withChannels (wnl::limits::channels), p).wasOk() && (int) p.channels.size() == wnl::limits::channels;
        rep.check (atLimit, "1000 canais abrem ("_t + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms)");

        // 60 mil notas com o mesmo id numa raia: abria, mas a conferência dos ids era quadrática
        auto doc = seed.clone();
        auto* notes = doc["patterns"][0]["lanes"]["ch_synth"]["notes"].getArray();
        const auto note = (*notes)[0].clone();

        for (int i = 0; i < 60000; ++i)
            notes->add (note);

        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        const bool manyNotes = wnl::fromJson (juce::JSON::toString (doc, true), p).wasOk();
        const double notesMs = juce::Time::getMillisecondCounterHiRes() - t1;
        const auto* lane = p.findPattern ("pt_a");
        rep.check (manyNotes && lane != nullptr && lane->notes.count ("ch_synth") > 0 && lane->notes.at ("ch_synth").size() == 60002
                       && checkInvariants (p).isEmpty() && notesMs < 20000.0,
                   "60 mil notas com o id repetido abrem com ids únicos, sem travar ("_t + juce::String (notesMs, 0) + " ms)");

        // mixer grande e sem laço: 300 trilhas, cada uma com envios para as 40 seguintes
        doc = seed.clone();
        auto* tracks = doc["mixer"]["tracks"].getArray();

        for (int i = 0; i < 300; ++i)
        {
            auto track = juce::var (new juce::DynamicObject());
            set (track, "id", "mx_t" + juce::String (i));
            set (track, "name", "T");
            set (track, "output", "mx_master");
            juce::Array<juce::var> sends;

            for (int k = i + 1; k < juce::jmin (300, i + 41); ++k)
            {
                auto send = juce::var (new juce::DynamicObject());
                set (send, "to", "mx_t" + juce::String (k));
                set (send, "db", -12.0);
                sends.add (send);
            }

            set (track, "sends", sends);
            tracks->add (track);
        }

        const auto t2 = juce::Time::getMillisecondCounterHiRes();
        const bool bigMixer = wnl::fromJson (juce::JSON::toString (doc, true), p).wasOk();
        const double mixerMs = juce::Time::getMillisecondCounterHiRes() - t2;
        const auto* first = p.findMixer ("mx_t0");
        rep.check (bigMixer && p.mixer.size() == 304 && first != nullptr && first->sends.size() == 40 && mixerMs < 20000.0,
                   "mixer de 300 trilhas com 40 envios cada, sem laço, abre com os envios inteiros e sem travar ("_t + juce::String (mixerMs, 0) + " ms)");

        // e o laço continua sendo desfeito (no projeto pequeno: o conserto anda o grafo a cada envio): o Reverb
        // manda de volta para o Kick, que manda para ele
        doc = seed.clone();
        auto back = juce::var (new juce::DynamicObject());
        set (back, "to", "mx_kick");
        set (back, "db", -6.0);
        juce::Array<juce::var> backSends;
        backSends.add (back);
        set (doc["mixer"]["tracks"][2], "sends", backSends);
        const bool looped = wnl::fromJson (juce::JSON::toString (doc, true), p).wasOk();
        const auto* kick = p.findMixer ("mx_kick");
        const auto* reverb = p.findMixer ("mx_rev");
        rep.check (looped && kick != nullptr && reverb != nullptr && kick->sends.size() + reverb->sends.size() == 1 && checkInvariants (p).isEmpty(),
                   "um laço de envios no arquivo continua sendo desfeito ("_t + (looped ? checkInvariants (p) : juce::String()) + ")");
    }

    //==========================================================================
    // D. Salvo por um app mais novo: avisa na leitura e guarda o original antes de salvar por cima.
    {
        auto doc = seed.clone();
        set (doc["app"], "features", juce::Array<juce::var> { "recurso-do-futuro", "outro", 3 });
        const auto text = juce::JSON::toString (doc);

        Project p;
        wnl::LoadReport report;
        rep.check (wnl::fromJson (text, p, &report).wasOk() && report.savedByNewerApp()
                       && report.unknownFeatures == juce::StringArray ("recurso-do-futuro", "outro"),
                   "arquivo que declara recursos desconhecidos em app.features abre e avisa ("_t + report.unknownFeatures.joinIntoString (", ") + ")");

        wnl::LoadReport plain;
        rep.check (wnl::fromJson (juce::String::fromUTF8 (seedJson), p, &plain).wasOk() && ! plain.savedByNewerApp() && plain.refusedPathCount == 0,
                   "arquivo sem app.features abre sem aviso"_t);
        rep.check (wnl::usedFeatures (base).isEmpty() && ! wnl::toJson (base).contains ("\"features\"") && wnl::toJson (base) == wnl::toJsonReference (base),
                   "o projeto de hoje não usa recurso novo: o arquivo sai sem \"features\", igual nos dois escritores"_t);

        const auto folder = ctx.work.getChildFile ("wnl-mais-novo");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto file = folder.getChildFile ("Futuro.wnl");
        file.replaceWithText (text, false, false, "\n");
        const bool saved = wnl::save (p, file, false).wasOk();
        const auto kept = folder.getChildFile ("history").getChildFile ("Futuro (app mais novo).wnl");
        rep.check (saved && kept.existsAsFile() && kept.loadFileAsString() == text && ! file.loadFileAsString().contains ("recurso-do-futuro"),
                   "salvar por cima guarda o original em history/Futuro (app mais novo).wnl"_t);
        rep.check (wnl::save (p, file, false).wasOk() && folder.getChildFile ("history").getNumberOfChildFiles (juce::File::findFiles) == 1,
                   "salvar de novo não faz outra cópia (o arquivo já é deste app)"_t);
    }

    //==========================================================================
    // E. O fuzz.
    {
        const int iterations = juce::jlimit (0, 2000000, juce::SystemStats::getEnvironmentVariable ("WHYNOT_SELFTEST_WNLFUZZ", "2000").getIntValue());
        const auto seedText = juce::SystemStats::getEnvironmentVariable ("WHYNOT_SELFTEST_SEED", {});
        juce::Random random (seedText.isNotEmpty() ? seedText.getLargeIntValue() : 20261110);

        int opened = 0, refused = 0, sanitised = 0, drifted = 0;
        double slowestMs = 0.0;
        juce::String failure, failingText;
        const auto started = juce::Time::getMillisecondCounterHiRes();

        for (int iter = 0; iter < iterations && failure.isEmpty(); ++iter)
        {
            auto doc = seed.clone();
            const int edits = 1 + random.nextInt (iter % 5 == 0 ? 12 : 3);

            for (int e = 0; e < edits; ++e)
                mutate (doc, random);

            auto text = textOf (doc);

            if (random.nextInt (25) == 0 && text.isNotEmpty())   // e, de vez em quando, o texto cortado no meio
                text = text.substring (0, random.nextInt (text.length()));

            Project p;
            wnl::LoadReport report;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto result = wnl::fromJson (text, p, &report);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            slowestMs = juce::jmax (slowestMs, ms);

            if (ms > 15000.0)
                failure = "levou "_t + juce::String (ms, 0) + " ms para ler";
            else if (result.failed())
            {
                ++refused;

                if (result.getErrorMessage().isEmpty())
                    failure = "recusou sem mensagem";
            }
            else
            {
                ++opened;
                sanitised += report.refusedPathCount > 0 ? 1 : 0;
                failure = checkInvariants (p);

                if (failure.isEmpty())
                {
                    // vira JSON e reabre (nenhum número inválido, nenhum caminho recusado sobrando); a segunda gravação
                    // já é estável. A primeira pode diferir num número enorme que o arquivo guarda com menos casas
                    // do que o float tem: isso só é contado.
                    const auto once = wnl::toJson (p);
                    Project again, third;
                    wnl::LoadReport second;

                    if (wnl::fromJson (once, again, &second).failed())      failure = "o projeto aberto não reabre depois de salvo"_t;
                    else if (second.refusedPathCount != 0)                  failure = "o projeto salvo ainda tem caminho recusado";
                    else if (checkInvariants (again).isNotEmpty())          failure = "reaberto: " + checkInvariants (again);
                    else
                    {
                        const auto twice = wnl::toJson (again);
                        drifted += twice != once ? 1 : 0;

                        if (wnl::fromJson (twice, third).failed() || wnl::toJson (third) != twice)
                            failure = "salvar e reabrir continua mudando o projeto";
                    }
                }
            }

            if (failure.isNotEmpty())
            {
                failure = "arquivo "_t + juce::String (iter) + ": " + failure;
                failingText = text;
            }
        }

        if (failure.isNotEmpty())
            ctx.work.getChildFile ("wnl-fuzz-falha.json").replaceWithText (failingText);

        const double totalMs = juce::Time::getMillisecondCounterHiRes() - started;
        rep.check (failure.isEmpty(),
                   juce::String (iterations) + " arquivos mutados: nenhum derruba nem trava, e o que abre passa nas invariantes e reabre igual ("_t
                       + juce::String (opened) + " abriram, " + juce::String (refused) + " recusados, " + juce::String (sanitised)
                       + " com caminho recusado, " + juce::String (drifted) + " mudaram na primeira regravação, o mais lento "_t
                       + juce::String (slowestMs, 0) + " ms, total " + juce::String (totalMs / 1000.0, 1) + " s)"
                       + (failure.isEmpty() ? juce::String() : " FALHA: "_t + failure + " (em wnl-fuzz-falha.json)"));
        rep.check (iterations < 500 || (opened > iterations / 10 && refused > iterations / 50),
                   "o fuzz passa pelos dois lados: arquivos que abrem e arquivos recusados"_t);
    }
}

} // namespace whynot::selftest
