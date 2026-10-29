#include "CommandPolicy.h"

#include <algorithm>
#include <array>

namespace whynot::control
{

namespace
{
    struct Entry
    {
        std::string_view id;
        CommandRule rule;
    };

    // o padrão de CommandRule é confirmar; ok() é o livre
    constexpr CommandRule ok()   { CommandRule r; r.risk = Risk::free; return r; }

    constexpr CommandRule reading (std::string_view param)
    {
        CommandRule r;
        r.risk = Risk::free;
        r.path = { param, Access::read, {} };
        return r;
    }

    constexpr CommandRule writing (std::string_view param, std::string_view extension)
    {
        CommandRule r;
        r.risk = Risk::free;
        r.path = { param, Access::write, extension };
        return r;
    }

    constexpr CommandRule confirm (std::string_view why)
    {
        CommandRule r;
        r.risk = Risk::confirm;
        r.why = why;
        return r;
    }

    constexpr CommandRule blocked (std::string_view why)
    {
        CommandRule r;
        r.risk = Risk::blocked;
        r.why = why;
        return r;
    }

    constexpr CommandRule unsaved (CommandRule r)      { r.confirmIfUnsaved = true; return r; }
    constexpr CommandRule overProject (CommandRule r)  { r.savesProject = true; return r; }
    constexpr CommandRule intoFolder (CommandRule r)   { r.writesIntoFolder = true; return r; }

    constexpr std::string_view thirdParty  = "abre um plugin VST3 de terceiros dentro do app; se ele cair, o app cai junto";
    constexpr std::string_view scanAll     = "roda todos os plugins VST3 das pastas, um por um";
    constexpr std::string_view pcSetting   = "muda uma configuração do seu computador, fora do projeto";
    constexpr std::string_view recording   = "a gravação fica com você: ela liga a entrada da placa de som";
    constexpr std::string_view preference  = "grava uma preferência sua (favoritos, a tag do produtor), fora do projeto";
    constexpr std::string_view libraryData = "muda a biblioteca da tela Projetos (pastas, combos, tags), fora do projeto";
    constexpr std::string_view userPresets = "grava ou apaga um preset ou uma cadeia sua no disco, fora do projeto";

    // Em ordem alfabética (o static_assert abaixo confere). Os motivos vão para a IA e para a tela.
    constexpr std::array table {
        Entry { "audio.analyzeNotes",         ok() },   // afinação nota a nota (27/10)
        Entry { "audio.clearNoteTune",        ok() },   // afinação nota a nota (27/10)
        Entry { "audio.consolidate",          ok() },
        Entry { "audio.detectBpm",            ok() },
        Entry { "audio.detectKey",            ok() },
        Entry { "audio.devices",              ok() },
        Entry { "audio.editNotes",            ok() },   // afinação nota a nota (27/10)
        Entry { "audio.extractGroove",        ok() },
        Entry { "audio.findSilence",          ok() },
        Entry { "audio.matchBpm",             ok() },
        Entry { "audio.mergeNotes",           ok() },   // afinação nota a nota (27/10)
        Entry { "audio.normalize",            ok() },
        Entry { "audio.performance",          ok() },
        Entry { "audio.removeSilence",        ok() },
        Entry { "audio.resetXruns",           ok() },
        Entry { "audio.reverse",              ok() },
        Entry { "audio.setDevice",            blocked (pcSetting) },
        Entry { "audio.setFades",             ok() },
        Entry { "audio.setGain",              ok() },
        Entry { "audio.setNoteTune",          ok() },   // afinação nota a nota (27/10)
        Entry { "audio.setPitch",             ok() },
        Entry { "audio.setProperties",        ok() },   // playlist fatia 6 (27/10, noite): o painel do bloco
        Entry { "audio.splitNote",            ok() },   // afinação nota a nota (27/10)
        Entry { "audio.toMidi",               reading ("path") },
        Entry { "audio.tuneToScale",          ok() },   // afinação nota a nota (27/10)
        Entry { "audio.warpToProject",        ok() },
        Entry { "automation.addLane",         ok() },
        Entry { "automation.list",            ok() },
        Entry { "automation.recordPass",      ok() },   // (26/10) uma passada gravada, com desfazer
        Entry { "automation.removeLane",      ok() },
        Entry { "automation.setPoints",       ok() },
        Entry { "automation.setRecording",    ok() },   // (26/10) só o modo; quem mexe nos controles é a pessoa
        Entry { "automation.setTension",      ok() },   // (26/10)
        Entry { "automation.targets",         ok() },
        Entry { "browser.find",               reading ("folder") },
        Entry { "browser.info",               reading ("path") },
        Entry { "browser.preview",            reading ("path") },
        Entry { "browser.stopPreview",        ok() },
        Entry { "channel.addAudio",           reading ("path") },
        Entry { "channel.addKit",             reading ("folder") },
        Entry { "channel.addLayer",           reading ("path") },
        Entry { "channel.addPiano",           ok() },
        Entry { "channel.addPlugin",          confirm (thirdParty) },
        Entry { "channel.addSampler",         reading ("path") },
        Entry { "channel.addSlicer",          reading ("path") },
        Entry { "channel.addSynth",           ok() },
        Entry { "channel.detectRoot",         ok() },
        Entry { "channel.layers",             ok() },
        Entry { "channel.remove",             ok() },
        Entry { "channel.removeLayer",        ok() },
        Entry { "channel.rename",             ok() },
        Entry { "channel.renderInPlace",      ok() },
        Entry { "channel.setBendRange",       ok() },
        Entry { "channel.setChoke",           ok() },
        Entry { "channel.setCutItself",       ok() },
        Entry { "channel.setFrozen",          ok() },
        Entry { "channel.setGlide",           ok() },
        Entry { "channel.setGroove",          ok() },   // swing e humanizar das notas (29/10): só o projeto
        Entry { "channel.setLayer",           reading ("path") },
        Entry { "channel.setMono",            ok() },
        Entry { "channel.setMute",            ok() },
        Entry { "channel.setOneShot",         ok() },
        Entry { "channel.setOutput",          ok() },
        Entry { "channel.setSample",          reading ("path") },
        Entry { "channel.setSampler",         ok() },
        Entry { "channel.setSwing",           ok() },
        Entry { "channel.slice",              ok() },
        Entry { "channel.slicesToPianoRoll",  ok() },
        Entry { "edit.redo",                  ok() },   // a ponte só deixa refazer passo da IA
        Entry { "edit.undo",                  ok() },   // e só desfazer passo da IA
        Entry { "edit.undoToggle",            blocked ("é o Ctrl+Z da tela, que alterna sozinho; a IA usa edit.undo e edit.redo") },
        Entry { "export.cancel",              ok() },   // exportação em segundo plano (extra-onda1): só para
        Entry { "export.producerTag",         ok() },
        Entry { "export.setProducerTag",      confirm (preference) },
        Entry { "export.status",              ok() },   // e só lê
        Entry { "favorites.list",             ok() },
        Entry { "favorites.set",              confirm (preference) },
        Entry { "favorites.toggle",           confirm (preference) },
        Entry { "fx.list",                    ok() },
        Entry { "ideas.detectChords",         ok() },
        Entry { "ideas.generate",             ok() },
        Entry { "ideas.options",              ok() },
        Entry { "ideas.parse",                ok() },
        Entry { "ideas.progressions",         ok() },
        Entry { "library.addFolder",          confirm (libraryData) },
        Entry { "library.comboAdd",           confirm (libraryData) },
        Entry { "library.comboCreate",        confirm (libraryData) },
        Entry { "library.comboDelete",        confirm (libraryData) },
        Entry { "library.comboLatest",        ok() },
        Entry { "library.comboMove",          confirm (libraryData) },
        Entry { "library.comboNote",          confirm (libraryData) },
        Entry { "library.comboPlay",          ok() },
        Entry { "library.comboRemove",        confirm (libraryData) },
        Entry { "library.comboRename",        confirm (libraryData) },
        Entry { "library.combos",             ok() },
        Entry { "library.deck",               ok() },
        Entry { "library.list",               ok() },
        Entry { "library.loadDeck",           ok() },
        Entry { "library.loop",               ok() },
        Entry { "library.matchLoudness",      ok() },
        Entry { "library.pause",              ok() },
        Entry { "library.play",               ok() },
        Entry { "library.refresh",            ok() },
        Entry { "library.removeFolder",       confirm (libraryData) },
        Entry { "library.renderPending",      ok() },
        Entry { "library.renderPreview",      ok() },
        Entry { "library.search",             ok() },
        Entry { "library.seek",               ok() },
        Entry { "library.setActive",          ok() },
        Entry { "library.setMeta",            confirm (libraryData) },
        Entry { "library.setSync",            ok() },
        Entry { "library.stop",               ok() },
        Entry { "library.switchDeck",         ok() },
        Entry { "library.toggleVersions",     ok() },
        Entry { "live.note",                  blocked ("nota ao vivo fica pendurada se o \"solta\" não chegar; para ouvir, use o render_preview") },
        Entry { "midi.cancelLearn",           ok() },
        Entry { "midi.inputs",                ok() },
        Entry { "midi.learn",                 ok() },
        Entry { "midi.map",                   ok() },
        Entry { "midi.mappings",              ok() },
        Entry { "midi.setInputEnabled",       blocked (pcSetting) },
        Entry { "midi.setLiveChannel",        ok() },
        Entry { "midi.unmap",                 ok() },
        Entry { "mixer.addInsert",            ok() },
        Entry { "mixer.addPluginInsert",      confirm (thirdParty) },
        Entry { "mixer.addTrack",             ok() },
        Entry { "mixer.applyStyle",           ok() },
        Entry { "mixer.compare",              ok() },
        Entry { "mixer.deleteChain",          confirm (userPresets) },
        Entry { "mixer.deleteInsertPreset",   confirm (userPresets) },
        Entry { "mixer.listChains",           ok() },
        Entry { "mixer.loadChain",            ok() },
        Entry { "mixer.loadInsertPreset",     ok() },
        Entry { "mixer.macros",               ok() },
        Entry { "mixer.mapMacro",             ok() },
        Entry { "mixer.moveInsert",           ok() },
        Entry { "mixer.removeInsert",         ok() },
        Entry { "mixer.removeSend",           ok() },
        Entry { "mixer.removeTrack",          ok() },
        Entry { "mixer.rename",               ok() },
        Entry { "mixer.saveChain",            confirm (userPresets) },
        Entry { "mixer.saveInsertPreset",     confirm (userPresets) },
        Entry { "mixer.setFrozen",            ok() },
        Entry { "mixer.setInsertBypass",      ok() },
        Entry { "mixer.setInsertParam",       ok() },
        Entry { "mixer.setInsertSidechain",   ok() },
        Entry { "mixer.setMacro",             ok() },
        Entry { "mixer.setMute",              ok() },
        Entry { "mixer.setOutput",            ok() },
        Entry { "mixer.setPan",               ok() },
        Entry { "mixer.setRole",              ok() },
        Entry { "mixer.setSend",              ok() },
        Entry { "mixer.setSolo",              ok() },
        Entry { "mixer.setVolume",            ok() },
        Entry { "mixer.showInsert",           ok() },
        Entry { "mixer.unmapMacro",           ok() },
        Entry { "pattern.add",                ok() },
        Entry { "pattern.clone",              ok() },
        Entry { "pattern.rename",             ok() },
        Entry { "pattern.select",             ok() },
        Entry { "pattern.setLength",          ok() },
        Entry { "piano.listPresets",          ok() },
        Entry { "piano.setPreset",            ok() },
        Entry { "piano.status",               ok() },
        Entry { "pianoroll.addNotes",         ok() },
        Entry { "pianoroll.clear",            ok() },
        Entry { "pianoroll.importMidi",       reading ("path") },
        Entry { "pianoroll.inspectMidi",      reading ("path") },
        Entry { "pianoroll.removeNotes",      ok() },
        Entry { "pianoroll.setNoteCurve",     ok() },
        Entry { "pianoroll.stepsToNotes",     ok() },
        Entry { "pianoroll.transform",        ok() },
        Entry { "pianoroll.updateNotes",      ok() },
        Entry { "playlist.addItem",           reading ("path") },
        Entry { "playlist.addMarker",         ok() },
        Entry { "playlist.addTrack",          ok() },
        Entry { "playlist.chopItem",          ok() },
        Entry { "playlist.clear",             ok() },
        Entry { "playlist.cutAt",             ok() },
        Entry { "playlist.gotoMarker",        ok() },   // playlist fatia 6: só a agulha
        Entry { "playlist.insertSpace",       ok() },
        Entry { "playlist.moveTrack",         ok() },
        Entry { "playlist.removeItem",        ok() },
        Entry { "playlist.removeMarker",      ok() },
        Entry { "playlist.removeSpace",       ok() },
        Entry { "playlist.removeTrack",       ok() },
        Entry { "playlist.renameTrack",       ok() },
        Entry { "playlist.selectMarkers",     ok() },   // playlist fatia 6: o trecho e o laço
        Entry { "playlist.setLaneInput",      blocked (recording) },
        Entry { "playlist.setTrack",          ok() },
        Entry { "playlist.splitItem",         ok() },
        Entry { "playlist.stretchItem",       ok() },
        Entry { "playlist.updateItem",        ok() },
        Entry { "playlist.updateMarker",      ok() },
        Entry { "plugin.openEditor",          ok() },
        Entry { "plugin.parameters",          ok() },
        Entry { "plugin.setParameter",        ok() },
        Entry { "plugins.blocked",            ok() },
        Entry { "plugins.list",               ok() },
        Entry { "plugins.scan",               confirm (scanAll) },
        Entry { "plugins.setFolders",         blocked (pcSetting) },
        Entry { "plugins.unblock",            blocked ("libera um plugin que já travou ou derrubou o app") },
        Entry { "project.autosave",           ok() },
        Entry { "project.exportMidi",         writing ("path", ".mid") },
        Entry { "project.exportMp3",          writing ("path", ".mp3") },
        Entry { "project.exportPack",         intoFolder (writing ("folder", {})) },
        Entry { "project.exportStems",        intoFolder (writing ("folder", {})) },
        Entry { "project.exportWav",          writing ("path", ".wav") },
        Entry { "project.importFrom",         reading ("path") },
        Entry { "project.new",                unsaved (ok()) },
        Entry { "project.open",               unsaved (reading ("path")) },
        Entry { "project.rename",             ok() },
        Entry { "project.resample",           ok() },
        Entry { "project.save",               overProject (writing ("path", ".wnl")) },
        Entry { "project.saveNewVersion",     ok() },   // nunca grava por cima: pula os nomes que existem
        Entry { "project.setKey",             ok() },
        Entry { "project.setSwing",           ok() },   // o swing global do rack (29/10)
        Entry { "record.arm",                 blocked (recording) },
        Entry { "record.cancel",              blocked (recording) },
        Entry { "record.captureMidi",         ok() },
        Entry { "record.commitTake",          blocked (recording) },
        Entry { "record.guide",               ok() },
        Entry { "record.inputDevices",        ok() },
        Entry { "record.note",                blocked (recording) },
        Entry { "record.setCountIn",          blocked (pcSetting) },
        Entry { "record.setHeadphones",       blocked (pcSetting) },
        Entry { "record.setInput",            blocked (pcSetting) },
        Entry { "record.setInputDevice",      blocked (pcSetting) },
        Entry { "record.setLatency",          blocked (pcSetting) },
        Entry { "record.setLowLatency",       blocked (pcSetting) },
        Entry { "record.setMic",              blocked (pcSetting) },
        Entry { "record.setMonitor",          blocked (recording) },
        Entry { "record.setPunch",            ok() },
        Entry { "record.state",               ok() },
        Entry { "record.voice",               blocked (recording) },
        Entry { "shortcuts.list",             ok() },
        Entry { "shortcuts.set",              blocked (pcSetting) },
        Entry { "shortcuts.setPreset",        blocked (pcSetting) },
        Entry { "steps.clear",                ok() },
        Entry { "steps.set",                  ok() },
        Entry { "steps.setGrid",              ok() },
        Entry { "synth.listPresets",          ok() },
        Entry { "synth.loadPreset",           ok() },
        Entry { "synth.params",               ok() },
        Entry { "synth.randomize",            ok() },
        Entry { "synth.setFavorite",          confirm ("grava nos favoritos do synth, fora do projeto") },
        Entry { "synth.setParam",             ok() },
        Entry { "takes.choose",               ok() },
        Entry { "takes.cut",                  ok() },
        Entry { "takes.list",                 ok() },
        Entry { "transport.play",             ok() },
        Entry { "transport.playFrom",         ok() },
        Entry { "transport.record",           blocked (recording) },
        Entry { "transport.setBpm",           ok() },
        Entry { "transport.setLoop",          ok() },
        Entry { "transport.setMetronome",     ok() },
        Entry { "transport.setPatternStart",  ok() },   // o começo do play no PAT (29/10), como a agulha
        Entry { "transport.setPosition",      ok() },
        Entry { "transport.setSongMode",      ok() },
        Entry { "transport.setTimeSignature", ok() },
        Entry { "transport.stop",             ok() },
        Entry { "transport.togglePause",      ok() },
        Entry { "transport.togglePlay",       ok() },
        Entry { "view.toggle",                ok() },
    };

    constexpr bool sortedAndUnique()
    {
        for (std::size_t i = 1; i < table.size(); ++i)
            if (! (table[i - 1].id < table[i].id))
                return false;

        return true;
    }

    static_assert (sortedAndUnique(), "a tabela de CommandPolicy.cpp precisa ficar em ordem alfabética, sem repetir id");

    constexpr CommandRule unclassified = confirm ("comando novo, ainda sem classe de risco para a IA");
}

const CommandRule* findRule (std::string_view id) noexcept
{
    const auto it = std::lower_bound (table.begin(), table.end(), id, [] (const Entry& e, std::string_view v) { return e.id < v; });
    return (it != table.end() && it->id == id) ? &it->rule : nullptr;
}

const CommandRule& ruleFor (std::string_view id) noexcept
{
    const auto* rule = findRule (id);
    return rule != nullptr ? *rule : unclassified;
}

std::string_view riskName (Risk r) noexcept
{
    switch (r)
    {
        case Risk::free:     return "free";
        case Risk::blocked:  return "blocked";
        default:             return "confirm";
    }
}

std::size_t ruleCount() noexcept                       { return table.size(); }
std::string_view ruleIdAt (std::size_t index) noexcept { return index < table.size() ? table[index].id : std::string_view(); }

} // namespace whynot::control
