#pragma once

#include "../model/ProjectChange.h"

#include <string_view>

/** O que cada comando muda no projeto, em bits de change::Mask (frente log-mascara, 16/11). Sem JUCE:
    tests/commands/test_change_scope.cpp.

    O CommandRegistry pergunta aqui, uma vez por comando (no add), e passa a máscara ao ProjectModel::changed()
    depois de cada comando que mudou o modelo. O MainComponent só refaz as telas que a máscara pede
    (src/ui/RefreshPlan.h). Num gesto ou num lote cada comando avisa com a máscara dele, e quem escuta soma.

    A regra é por prefixo do id (`prefixes`), mas o prefixo só vale para os ids de `byPrefix`: os comandos cujo
    corpo foi lido e que mexem só no domínio do prefixo (contando o que o wnl::normalise arruma depois). Onde o
    prefixo mente, ou o id não tem prefixo na tabela e a máscara é certa, a linha está em `exceptions`. Qualquer
    outro id vale change::all, inclusive um comando novo que ninguém conferiu: máscara de menos deixa uma tela
    velha (bug que se vê); máscara de mais só custa desempenho. NA DÚVIDA, change::all.

    Comando novo: o teste lê os ids de src/commands/*.cpp e falha enquanto o id não estiver numa das quatro
    listas daqui. Leia o corpo do comando e o normalise antes de escolher:
      - mexe só no domínio do prefixo            -> `byPrefix`
      - o prefixo mente, ou não há prefixo        -> `exceptions`, com a máscara
      - mexe em tudo, ou você não tem certeza     -> `everything`
      - changesModel = false (o registro não avisa por ele; se ele mexer no modelo por conta própria, o aviso
        dele já sai com change::all)             -> `notModel`

    O que conta para cada bit está em src/model/ProjectChange.h. Efeitos em cascata que já apareceram:
      - tirar efeito ou trilha do mixer apaga as curvas de automação do alvo (normalise)  -> + playlist
      - mudar o BPM refaz o teto dos fades dos blocos de áudio (normalise)               -> + playlist
      - canal novo ganha trilha de mixer e raia de passos em todo padrão; canal removido leva blocos, notas
        e a trilha dele                                                                  -> all
      - Project::midiMap não tem bit                                                     -> all
*/
namespace whynot::changescope
{
    using change::Mask;

    struct Prefix
    {
        std::string_view prefix;
        Mask mask;
    };

    inline constexpr Prefix prefixes[] = {
        { "pianoroll.",  change::patterns },
        { "steps.",      change::patterns },
        { "pattern.",    change::patterns },
        { "playlist.",   change::playlist },
        { "automation.", change::playlist },
        { "marker.",     change::playlist },
        { "mixer.",      change::mixer },
        { "fx.",         change::mixer },
        { "insert.",     change::mixer },
        { "channel.",    change::channels },
        { "sampler.",    change::channels },
        { "synth.",      change::channels },
        { "piano.",      change::channels },
        { "transport.",  change::transport },
    };

    struct Rule
    {
        std::string_view id;
        Mask mask;
    };

    /** Onde o prefixo mente, e os ids sem prefixo na tabela cuja máscara é certa. */
    inline constexpr Rule exceptions[] = {
        // o padrão atual é do transporte (o seletor de padrão, o rack e o piano roll passam a mostrar outro padrão)
        { "pattern.add",    change::patterns | change::transport },
        { "pattern.clone",  change::patterns | change::transport },
        { "pattern.select", change::patterns | change::transport },

        // "loop": true liga o SONG e o loop do projeto
        { "playlist.selectMarkers", change::playlist | change::transport },
        // cria a trilha de mixer da raia e arma (uma armada por vez)
        { "playlist.setLaneInput",  change::playlist | change::mixer },
        // "base" escreve o volume, o pan ou o parâmetro do efeito nativo da trilha (setBase)
        { "automation.recordPass",  change::playlist | change::mixer },

        // o normalise apaga as curvas de automação dos efeitos que saíram
        { "mixer.removeInsert", change::mixer | change::playlist },
        { "mixer.loadChain",    change::mixer | change::playlist },
        { "mixer.applyStyle",   change::mixer | change::playlist },   // buses, envios e cadeias: tudo em Project::mixer

        // a trilha automática acompanha o nome do canal; a saída e o congelado são da trilha
        { "channel.rename",    change::channels | change::mixer },
        { "channel.setOutput", change::channels | change::mixer },
        { "channel.setFrozen", change::channels | change::mixer },
        // fatiar pode pôr as fatias no piano roll do padrão
        { "channel.slice",             change::channels | change::patterns },
        { "channel.slicesToPianoRoll", change::channels | change::patterns },

        // o normalise refaz o teto dos fades dos blocos de áudio (os segundos que cabem no bloco)
        { "transport.setBpm", change::transport | change::playlist },

        // sem prefixo na tabela, com a máscara certa
        { "project.rename",   change::transport },
        { "project.setSwing", change::transport },
        { "record.setPunch",  change::playlist },
        { "audio.setGain",    change::playlist },   // os três só mexem no PlaylistItem (as alças do bloco de áudio)
        { "audio.setFades",   change::playlist },
        { "audio.setPitch",   change::playlist },
    };

    /** Conferidos: mexem só no domínio do prefixo. */
    inline constexpr std::string_view byPrefix[] = {
        "pianoroll.addNotes", "pianoroll.updateNotes", "pianoroll.removeNotes", "pianoroll.clear",
        "pianoroll.stepsToNotes", "pianoroll.transform", "pianoroll.importMidi", "pianoroll.setNoteCurve",
        "steps.set", "steps.setGrid", "steps.clear", "pattern.rename", "pattern.setLength", "playlist.updateItem",
        "playlist.splitByChannel", "playlist.stretchItem", "playlist.splitItem", "playlist.removeItem",
        "playlist.clear", "playlist.renameTrack", "playlist.setTrack", "playlist.moveTrack", "playlist.addTrack",
        "playlist.removeTrack", "playlist.addMarker", "playlist.updateMarker", "playlist.removeMarker",
        "playlist.cutAt", "playlist.removeSpace", "playlist.insertSpace", "playlist.chopItem", "automation.addLane",
        "automation.setPoints", "automation.removeLane", "automation.setTension", "mixer.setVolume", "mixer.setPan",
        "mixer.setMute", "mixer.setSolo", "mixer.setOutput", "mixer.rename", "mixer.addInsert", "mixer.moveInsert",
        "mixer.setInsertParam", "mixer.setInsertBypass", "mixer.loadInsertPreset", "mixer.setInsertSidechain",
        "mixer.setSend", "mixer.removeSend", "mixer.setRole", "mixer.addTrack", "mixer.setMacro", "mixer.mapMacro",
        "mixer.unmapMacro", "mixer.setFrozen", "mixer.addPluginInsert", "channel.setMono", "channel.setOneShot",
        "channel.setChoke", "channel.setSwing", "channel.setGlide", "channel.setSampler", "channel.detectRoot",
        "channel.setSample", "channel.setMute", "channel.setCutItself", "channel.setGroove", "channel.addLayer",
        "channel.setLayer", "channel.removeLayer", "channel.setBendRange", "synth.loadPreset", "synth.setParam",
        "synth.randomize", "piano.setPreset", "transport.setSongMode", "transport.setTimeSignature",
        "transport.setMetronome", "transport.setLoop",
    };

    /** Mudam o modelo e valem change::all. */
    inline constexpr std::string_view everything[] = {
        // com certeza: canal novo ou removido, trilha removida, bloco que pode criar canal, tom que reescreve os
        // efeitos de afinação, importar, reamostrar, consolidar, o take gravado, armar canal e trilha, e o
        // midiMap (que não tem bit)
        "channel.addSampler", "channel.addAudio", "channel.addPiano", "channel.addSynth", "channel.addPlugin",
        "channel.addSlicer", "channel.addKit", "channel.remove", "channel.renderInPlace", "mixer.removeTrack",
        "playlist.addItem", "project.setKey", "project.resample", "project.importFrom", "audio.consolidate",
        "record.commitTake", "record.arm", "midi.map", "midi.unmap",

        // por dúvida: o corpo não foi conferido linha a linha, ou passa por código de fora do comando (takes::,
        // notetune::, blockfile::, o Ideias). Quem conferir pode trocar pela máscara certa em `exceptions`.
        "audio.toMidi", "audio.setNoteTune", "audio.editNotes", "audio.splitNote", "audio.mergeNotes",
        "audio.tuneToScale", "audio.clearNoteTune", "audio.removeSilence", "audio.setProperties", "audio.normalize",
        "audio.reverse", "audio.matchBpm", "audio.warpToProject", "audio.extractGroove", "ideas.generate",
        "record.setMonitor", "record.voice", "record.captureMidi", "takes.choose", "takes.cut",
    };

    /** changesModel = false: o registro não chama o changed() por eles. */
    inline constexpr std::string_view notModel[] = {
        "audio.analyzeNotes", "audio.detectBpm", "audio.detectKey", "audio.devices", "audio.findSilence",
        "audio.performance", "audio.resetXruns", "audio.setDevice", "automation.list", "automation.setRecording",
        "automation.targets", "browser.find", "browser.info", "channel.layers", "edit.redo", "edit.undo",
        "edit.undoToggle", "export.cancel", "export.producerTag", "export.setProducerTag", "export.status",
        "favorites.list", "favorites.set", "favorites.toggle", "fx.list", "ideas.detectChords", "ideas.options",
        "ideas.parse", "ideas.progressions", "library.addFolder", "library.comboAdd", "library.comboCreate",
        "library.comboDelete", "library.comboLatest", "library.comboMove", "library.comboNote", "library.comboPlay",
        "library.comboRemove", "library.comboRename", "library.combos", "library.deck", "library.list",
        "library.loadDeck", "library.loop", "library.matchLoudness", "library.pause", "library.play",
        "library.refresh", "library.removeFolder", "library.renderPending", "library.renderPreview",
        "library.search", "library.seek", "library.setActive", "library.setMeta", "library.setSync", "library.stop",
        "library.switchDeck", "library.toggleVersions", "live.note", "midi.cancelLearn", "midi.inputs", "midi.learn",
        "midi.mappings", "midi.setInputEnabled", "midi.setLiveChannel", "mixer.compare", "mixer.deleteChain",
        "mixer.deleteInsertPreset", "mixer.listChains", "mixer.macros", "mixer.saveChain", "mixer.saveInsertPreset",
        "mixer.showInsert", "piano.listPresets", "piano.status", "pianoroll.inspectMidi", "playlist.gotoMarker",
        "plugin.openEditor", "plugin.parameters", "plugin.setParameter", "plugins.blocked", "plugins.list",
        "plugins.scan", "plugins.setFolders", "plugins.unblock", "project.autosave", "project.exportMidi",
        "project.exportMp3", "project.exportPack", "project.exportStems", "project.exportWav", "project.new",
        "project.open", "project.save", "project.saveNewVersion", "record.cancel", "record.guide",
        "record.inputDevices", "record.note", "record.setCountIn", "record.setHeadphones", "record.setInput",
        "record.setInputDevice", "record.setLatency", "record.setLowLatency", "record.setMic", "record.state",
        "shortcuts.list", "shortcuts.set", "shortcuts.setPreset", "synth.listPresets", "synth.params",
        "synth.setFavorite", "takes.list",
        // templates (16/11): fora do desfazer; o template.new passa pelo novo projeto, que avisa as telas sozinho
        "template.choose", "template.delete", "template.list", "template.new", "template.save", "template.saveDialog",
        "template.setDefault",
        "transport.play", "transport.playFrom", "transport.record",
        "transport.setPatternStart", "transport.setPosition", "transport.stop", "transport.togglePause",
        "transport.togglePlay", "view.toggle",
    };

    enum class Decision { none, exception, prefix, everything, notModel };

    /** A máscara do prefixo do id; 0 = o id não tem prefixo na tabela. */
    constexpr Mask prefixMask (std::string_view id) noexcept
    {
        for (const auto& p : prefixes)
            if (id.substr (0, p.prefix.size()) == p.prefix)
                return p.mask;

        return 0;
    }

    /** Em qual lista o id está (none = ninguém decidiu: o teste falha e o registro usa change::all). */
    constexpr Decision decisionFor (std::string_view id) noexcept
    {
        for (const auto& rule : exceptions)
            if (rule.id == id)
                return Decision::exception;

        for (const auto listed : byPrefix)
            if (listed == id)
                return Decision::prefix;

        for (const auto listed : everything)
            if (listed == id)
                return Decision::everything;

        for (const auto listed : notModel)
            if (listed == id)
                return Decision::notModel;

        return Decision::none;
    }

    /** O que o comando muda. Nunca devolve 0. */
    constexpr Mask maskFor (std::string_view id) noexcept
    {
        for (const auto& rule : exceptions)
            if (rule.id == id)
                return change::normalised (rule.mask);

        for (const auto listed : byPrefix)
            if (listed == id)
                return change::normalised (prefixMask (id));   // sem prefixo (engano na lista): 0 vira all

        return change::all;
    }

    static_assert (maskFor ("mixer.setVolume") == change::mixer);
    static_assert (maskFor ("pianoroll.updateNotes") == change::patterns);
    static_assert (maskFor ("pattern.select") == (change::patterns | change::transport));
    static_assert (maskFor ("channel.addSampler") == change::all);
    static_assert (maskFor ("mixer.comandoQueNinguemConferiu") == change::all);
    static_assert (maskFor ("edit.undo") == change::all);
}
