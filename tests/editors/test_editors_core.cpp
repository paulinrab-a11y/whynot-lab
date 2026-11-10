// Testes dos editores (playlist e piano roll), sem JUCE: a tabela de atalhos com contexto (KeyMapCore.h)
// e o "seguir a reprodução" por página (TimelineFollow.h). Só contas: não abre janela nem arquivo.

#include "../../src/app/KeyMapCore.h"
#include "../../src/ui/TimelineFollow.h"

#include <cstdio>
#include <set>
#include <string>

using namespace whynot;
using namespace whynot::keymap;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static bool has (const Keys& keys, Chord c)
{
    for (auto& k : keys)
        if (k == c)
            return true;

    return false;
}

static void testTable()
{
    std::printf ("Tabela de atalhos\n");

    std::set<std::string_view> ids;
    bool unique = true, filled = true, knownContext = true;

    for (auto& a : actions())
    {
        unique = ids.insert (a.id).second && unique;
        filled = filled && ! a.id.empty() && ! a.title.empty() && ! a.group.empty();
        knownContext = knownContext && (a.context == global || a.context == playlist || a.context == pianoRoll);
    }

    CHECK (unique, "nenhum id repetido (%d ações)", (int) actions().size());
    CHECK (filled, "toda ação tem id, título e grupo");
    CHECK (knownContext, "os contextos são só app inteiro, playlist e pianoroll");

    // nos presets, nenhuma tecla está em duas ações que disputam (mesmo contexto, ou uma delas global)
    for (const char* preset : { "fl", "whynot" })
    {
        int clashes = 0;

        for (auto& a : actions())
            for (auto& b : actions())
                if (a.id < b.id && contextsClash (a.context, b.context))
                    for (auto& key : presetKeys (a, preset))
                        if (has (presetKeys (b, preset), key))
                        {
                            ++clashes;
                            std::printf ("    disputa em %s: %s e %s\n", preset, std::string (a.id).c_str(), std::string (b.id).c_str());
                        }

        CHECK (clashes == 0, "preset %s: nenhuma tecla em duas ações que disputam", preset);
    }

    // as ações de editor têm as mesmas teclas nos dois presets (quem troca de preset não perde as ferramentas)
    bool same = true;

    for (auto& a : actions())
        if (a.context != global)
            same = same && a.fl == a.whynot;

    CHECK (same, "ações dos editores: as mesmas teclas nos dois presets");
}

static void testPresetsIntact()
{
    std::printf ("Presets: as teclas de antes\n");
    Map fl;
    auto g = [&] (int c, int mods = 0) { return std::string (fl.actionFor (chord (c, mods), global)); };

    CHECK (fl.preset() == "fl", "o preset padrão é o fl");
    CHECK (g (code::space) == "transport.togglePlay" && g (code::space, ctrl) == "transport.togglePause", "Espaço toca, Ctrl+Espaço pausa");
    CHECK (g ('l') == "transport.songMode" && g ('R') == "transport.record" && g ('m', ctrl) == "transport.metronome"
               && g ('p', ctrl) == "transport.countIn", "L, R, Ctrl+M e Ctrl+P");
    CHECK (g ('n', ctrl) == "file.saveNewVersion" && g ('o', ctrl) == "file.open" && g ('s', ctrl) == "file.save"
               && g ('s', ctrl | shift) == "file.saveAs", "FL: Ctrl+N nova versão; Ctrl+O, Ctrl+S, Ctrl+Shift+S");
    CHECK (g ('z', ctrl) == "edit.undo" && g ('z', ctrl | alt) == "edit.undo" && g ('y', ctrl) == "edit.redo"
               && g ('z', ctrl | shift) == "edit.redo", "FL: Ctrl+Z e Ctrl+Alt+Z desfazem, Ctrl+Y e Ctrl+Shift+Z refazem");
    CHECK (g ('r', ctrl) == "export.wav" && g ('r', ctrl | shift) == "export.mp3" && g ('m', ctrl | shift) == "export.midi"
               && g ('r', ctrl | alt).empty(), "FL: Ctrl+R WAV, Ctrl+Shift+R MP3, Ctrl+Shift+M MIDI, stems sem tecla");
    CHECK (g (code::f4) == "pattern.new" && g ('c', ctrl | shift) == "pattern.clone" && g (code::numpadAdd) == "pattern.next"
               && g (code::numpadSubtract) == "pattern.previous", "F4, Ctrl+Shift+C, + e - do teclado numérico");
    CHECK (g (code::f5) == "view.playlist" && g (code::f6) == "view.channelRack" && g (code::f7) == "view.pianoRoll"
               && g (code::f8, alt) == "view.files" && g (code::f8) == "view.plugins" && g (code::f9) == "view.mixer"
               && g (code::f10) == "view.settings" && g (code::f11) == "view.projects" && g (code::tab) == "projects.switchDeck",
           "F5 a F11, Alt+F8 e Tab");
    CHECK (g ('t', ctrl) == "keyboard.typing" && g ('t', alt) == "playlist.addMarker" && g ('=', ctrl) == "ui.zoomIn"
               && g ('-', ctrl) == "ui.zoomOut" && g ('0', ctrl) == "ui.zoomReset", "Ctrl+T, Alt+T e o zoom da interface");
    CHECK (g ('p', ctrl | shift) == "view.commandPalette", "FL: Ctrl+Shift+P abre a paleta de comandos");

    Map wn;
    wn.setPreset ("whynot");
    auto w = [&] (int c, int mods = 0) { return std::string (wn.actionFor (chord (c, mods), global)); };
    CHECK (w ('n', ctrl) == "file.new" && w ('n', ctrl | alt) == "file.saveNewVersion", "WHYNOT: Ctrl+N novo, Ctrl+Alt+N nova versão");
    CHECK (w ('z', ctrl) == "edit.undo" && w ('z', ctrl | alt).empty() && w ('y', ctrl) == "edit.redo" && w ('z', ctrl | shift) == "edit.redo",
           "WHYNOT: Ctrl+Z desfaz, Ctrl+Y e Ctrl+Shift+Z refazem");
    CHECK (w ('r', ctrl | alt) == "export.stems" && w ('p', ctrl | shift) == "view.commandPalette", "WHYNOT: Ctrl+Alt+R stems, Ctrl+Shift+P paleta");

    Map other;
    other.setPreset ("nao-existe");
    CHECK (other.preset() == "fl", "preset desconhecido vira fl");
}

static void testEditorKeys()
{
    std::printf ("Editores: as teclas que estavam fixas no código\n");
    Map m;
    auto pl = [&] (int c, int mods = 0) { return std::string (m.actionFor (chord (c, mods), playlist)); };
    auto pr = [&] (int c, int mods = 0) { return std::string (m.actionFor (chord (c, mods), pianoRoll)); };

    CHECK (pl ('p') == "playlist.tool.draw" && pl ('b') == "playlist.tool.paint" && pl ('c') == "playlist.tool.slice"
               && pl ('d') == "playlist.tool.erase" && pl ('e') == "playlist.tool.select" && pl ('t') == "playlist.tool.mute"
               && pl ('s') == "playlist.tool.slip" && pl ('y') == "playlist.tool.play" && pl ('z') == "playlist.tool.zoom",
           "playlist: P B C D E T S Y Z são as ferramentas");
    CHECK (pl ('a', ctrl) == "playlist.selectAll" && pl ('c', ctrl) == "playlist.copy" && pl ('x', ctrl) == "playlist.cut"
               && pl ('v', ctrl) == "playlist.paste" && pl ('b', ctrl) == "playlist.duplicate",
           "playlist: Ctrl+A, C, X, V e B");
    CHECK (pl (code::del) == "playlist.delete" && pl (code::backspace) == "playlist.delete" && pl ('e', ctrl) == "playlist.cutAtNeedle"
               && pl (code::del, ctrl) == "playlist.removeTime" && pl (code::insert, ctrl) == "playlist.insertTime",
           "playlist: Delete, Backspace, Ctrl+E, Ctrl+Delete, Ctrl+Insert");
    CHECK (pl ('z', shift) == "playlist.zoomSelection" && pl ('z', alt) == "playlist.zoomSong"
               && pl (code::right, alt) == "playlist.nextMarker" && pl (code::left, alt) == "playlist.previousMarker",
           "playlist: Shift+Z, Alt+Z e Alt+setas");
    CHECK (pl (code::left, shift) == "playlist.moveLeft" && pl (code::right, shift) == "playlist.moveRight"
               && pl (code::up, shift) == "playlist.moveUp" && pl (code::down, shift) == "playlist.moveDown",
           "playlist: Shift+setas andam com a seleção");
    CHECK (pl ('f') == "playlist.follow" && pr ('f') == "pianoroll.follow", "F liga e desliga o seguir nos dois editores");

    CHECK (pr ('p') == "pianoroll.tool.draw" && pr ('b') == "pianoroll.tool.paint" && pr ('e') == "pianoroll.tool.select"
               && pr ('c') == "pianoroll.tool.slice" && pr ('s') == "pianoroll.slide", "piano roll: P B E C e o S do slide");
    CHECK (pr ('a', ctrl) == "pianoroll.selectAll" && pr ('c', ctrl) == "pianoroll.copy" && pr ('x', ctrl) == "pianoroll.cut"
               && pr ('v', ctrl) == "pianoroll.paste" && pr ('b', ctrl) == "pianoroll.duplicate"
               && pr (code::del) == "pianoroll.delete" && pr (code::backspace) == "pianoroll.delete",
           "piano roll: Ctrl+A, C, X, V, B, Delete e Backspace");
    CHECK (pr ('q', alt) == "pianoroll.quantize" && pr ('l', ctrl) == "pianoroll.legato" && pr ('g', ctrl) == "pianoroll.glue"
               && pr ('m', alt) == "pianoroll.mute" && pr ('m', alt | shift) == "pianoroll.unmute",
           "piano roll: Alt+Q, Ctrl+L, Ctrl+G, Alt+M e Alt+Shift+M");
    CHECK (pr (code::up) == "pianoroll.transposeUp" && pr (code::down) == "pianoroll.transposeDown"
               && pr (code::up, ctrl) == "pianoroll.octaveUp" && pr (code::up, shift) == "pianoroll.octaveUp"
               && pr (code::down, ctrl) == "pianoroll.octaveDown" && pr (code::down, shift) == "pianoroll.octaveDown"
               && pr (code::left) == "pianoroll.nudgeLeft" && pr (code::left, shift) == "pianoroll.nudgeLeft"
               && pr (code::right) == "pianoroll.nudgeRight" && pr (code::right, shift) == "pianoroll.nudgeRight",
           "piano roll: setas transpõem e andam, Ctrl ou Shift+cima/baixo é a oitava");

    // a janela (contexto global) não enxerga as teclas dos editores, e os editores não enxergam as globais
    CHECK (m.actionFor (chord ('p'), global).empty() && m.actionFor (chord ('c', ctrl), global).empty(),
           "o P e o Ctrl+C dos editores não são ação do app inteiro");
    CHECK (pl ('l').empty() && pr (code::space).empty() && pl ('s', ctrl).empty(), "L, Espaço e Ctrl+S não são dos editores");
}

static void testOverrides()
{
    std::printf ("Trocas do usuário\n");
    Map m;

    // mesma tecla em dois contextos: trocar num editor não mexe no outro
    m.setKeys ("playlist.tool.draw", { chord ('1') });
    CHECK (m.actionFor (chord ('1'), playlist) == "playlist.tool.draw" && m.actionFor (chord ('p'), playlist).empty(),
           "playlist: o lápis foi para o 1 e o P ficou livre");
    CHECK (m.actionFor (chord ('p'), pianoRoll) == "pianoroll.tool.draw" && ! m.isCustomised ("pianoroll.tool.draw"),
           "piano roll: o P continua sendo o lápis, sem troca gravada");
    CHECK (m.getOverrides().size() == 1, "só a troca do usuário fica gravada");

    // dar a um editor uma tecla que o outro editor usa: as duas ficam
    m.setKeys ("pianoroll.quantize", { chord ('d') });
    CHECK (m.actionFor (chord ('d'), pianoRoll) == "pianoroll.quantize" && m.actionFor (chord ('d'), playlist) == "playlist.tool.erase",
           "o D vale nos dois editores, cada um com a sua ação");

    // conflito no mesmo contexto: a tecla sai da outra ação
    CHECK (m.clashFor ("playlist.tool.mute", chord ('b')) == "playlist.tool.paint", "avisa de quem a tecla vai sair (mesmo contexto)");
    m.setKeys ("playlist.tool.mute", { chord ('b') });
    CHECK (m.actionFor (chord ('b'), playlist) == "playlist.tool.mute" && m.keysFor ("playlist.tool.paint").empty()
               && m.actionFor (chord ('b'), pianoRoll) == "pianoroll.tool.paint",
           "conflito na playlist: o B saiu do pintar da playlist e ficou no do piano roll");

    // ação com duas teclas perde só a que foi tomada
    m.setKeys ("playlist.copy", { chord (code::backspace) });
    CHECK (m.keysFor ("playlist.delete") == Keys { chord (code::del) }, "o apagar perdeu o Backspace e ficou com o Delete");

    // ação global toma a tecla de qualquer editor (o editor ficaria com ela antes da janela)
    CHECK (m.clashFor ("transport.record", chord ('e')) == "playlist.tool.select", "global contra editor também é disputa");
    m.setKeys ("transport.record", { chord ('e') });
    CHECK (m.actionFor (chord ('e'), global) == "transport.record" && m.actionFor (chord ('e'), playlist).empty()
               && m.actionFor (chord ('e'), pianoRoll).empty() && m.actionFor (chord ('r'), global).empty(),
           "o E virou gravar no app inteiro e saiu das ferramentas dos dois editores");

    // e o editor toma a tecla de uma ação global
    m.setKeys ("pianoroll.glue", { chord ('l') });
    CHECK (m.actionFor (chord ('l'), pianoRoll) == "pianoroll.glue" && m.actionFor (chord ('l'), global).empty(),
           "o L foi para o piano roll e saiu do PAT/SONG");

    // voltar ao preset
    m.reset ("playlist.tool.draw");
    CHECK (m.actionFor (chord ('p'), playlist) == "playlist.tool.draw" && ! m.isCustomised ("playlist.tool.draw"), "voltar ao preset devolve o P");

    // sem atalho
    m.setKeys ("playlist.tool.zoom", {});
    CHECK (m.actionFor (chord ('z'), playlist).empty() && m.isCustomised ("playlist.tool.zoom"), "lista vazia = sem atalho");

    // ação desconhecida não entra
    m.setKeys ("nao.existe", { chord ('k') });
    m.setRaw ("tambem.nao", { chord ('k') });
    CHECK (m.actionFor (chord ('k'), global).empty() && ! m.isCustomised ("nao.existe") && ! m.isCustomised ("tambem.nao"),
           "ação desconhecida é ignorada");

    // o preset fica intacto: trocar de preset apaga as trocas e as teclas voltam
    m.setPreset ("fl");
    CHECK (m.getOverrides().empty() && m.actionFor (chord ('b'), playlist) == "playlist.tool.paint"
               && m.actionFor (chord ('r'), global) == "transport.record" && m.actionFor (chord ('e'), playlist) == "playlist.tool.select"
               && m.actionFor (chord ('l'), global) == "transport.songMode",
           "escolher o preset de novo apaga as trocas: a tabela do preset não foi mexida");

    // atalhos.json antigo: só ações globais trocadas, lidas como estão (sem tirar tecla de ninguém)
    Map old;
    old.setPresetKeepingOverrides ("whynot");
    old.setRaw ("export.stems", { chord ('e', ctrl | shift) });
    old.setRaw ("transport.record", { chord ('p') });   // antes desta rodada a playlist já ficava com o P
    CHECK (old.preset() == "whynot" && old.actionFor (chord ('e', ctrl | shift), global) == "export.stems"
               && old.actionFor (chord ('p'), global) == "transport.record" && old.actionFor (chord ('p'), playlist) == "playlist.tool.draw"
               && old.actionFor (chord ('n', ctrl), global) == "file.new",
           "arquivo antigo: as trocas valem e as ferramentas dos editores continuam com as teclas de sempre");
}

static void testFollow()
{
    using namespace whynot::follow;
    std::printf ("Seguir a reprodução\n");

    CHECK (pageScroll (100.0, 0.0, 1000.0) == 0.0 && pageScroll (999.0, 0.0, 1000.0) == 0.0, "agulha à vista: a tela fica");
    CHECK (pageScroll (1000.0, 0.0, 1000.0) == 900.0, "saiu pela direita: pula uma página e deixa 10%% de margem atrás da agulha");
    CHECK (pageScroll (50.0, 4000.0, 1000.0) == 0.0 && pageScroll (2500.0, 4000.0, 1000.0) == 2400.0, "voltou (laço): a tela vai até ela, sem passar do zero");
    CHECK (pageScroll (-1.0, 300.0, 1000.0) == 300.0 && pageScroll (5000.0, 300.0, 0.0) == 300.0, "parado ou sem largura: nada muda");

    Follower f;
    CHECK (f.isOn() && f.isWanted(), "ligado por padrão");

    // toca: a tela acompanha por página
    auto r = f.step (0.0, true, false, 0.0, 0.1, 1000.0);
    CHECK (r.scroll == 0.0 && ! r.stateChanged, "começou a tocar com a agulha à vista");
    r = f.step (1000.0, false, false, 0.0, 0.1, 1000.0);
    CHECK (r.scroll == 900.0, "a agulha passou da borda: pede a página seguinte");
    f.settled (900.0, 0.1);
    r = f.step (1200.0, false, false, 900.0, 0.1, 1000.0);
    CHECK (r.scroll == 900.0 && ! r.stateChanged && f.isOn(), "depois do próprio pulo o seguir continua ligado");

    // gesto de edição: não rola
    r = f.step (2500.0, false, true, 900.0, 0.1, 1000.0);
    CHECK (r.scroll == 900.0 && f.isOn(), "com o mouse apertado na grade a tela não anda");
    r = f.step (2510.0, false, false, 900.0, 0.1, 1000.0);
    CHECK (r.scroll == 2410.0, "soltou: volta a acompanhar");
    f.settled (2410.0, 0.1);

    // o usuário rolou: desliga sozinho, e não religa enquanto toca
    r = f.step (2600.0, false, false, 500.0, 0.1, 1000.0);
    CHECK (r.scroll == 500.0 && r.stateChanged && ! f.isOn() && f.isWanted(), "rolou à mão: o seguir se desliga (a preferência fica)");
    r = f.step (5000.0, false, false, 500.0, 0.1, 1000.0);
    CHECK (r.scroll == 500.0 && ! r.stateChanged, "desligado, a tela fica onde o usuário deixou");

    // tocar de novo religa
    r = f.step (0.0, true, false, 500.0, 0.1, 1000.0);
    CHECK (r.stateChanged && f.isOn() && r.scroll == 0.0, "tocar de novo religa e a tela vai até a agulha");
    f.settled (0.0, 0.1);

    // zoom também desliga; o botão religa
    r = f.step (10.0, false, false, 0.0, 0.2, 500.0);
    CHECK (r.stateChanged && ! f.isOn(), "zoom à mão: desliga");
    f.setOn (true);
    r = f.step (700.0, false, false, 0.0, 0.2, 500.0);
    CHECK (f.isOn() && r.scroll == 650.0, "o botão religa e a tela já acompanha no zoom novo");
    f.settled (650.0, 0.2);

    // desligado pelo usuário: nem tocar de novo religa
    f.setOn (false);
    r = f.step (9000.0, true, false, 650.0, 0.2, 500.0);
    CHECK (! f.isOn() && ! f.isWanted() && r.scroll == 650.0 && ! r.stateChanged, "desligado no botão, fica desligado");

    // parado: nada
    f.setOn (true);
    r = f.step (-1.0, false, false, 123.0, 0.2, 500.0);
    CHECK (r.scroll == 123.0 && ! r.stateChanged, "parado: a tela não anda");
}

int main()
{
    testTable();
    testPresetsIntact();
    testEditorKeys();
    testOverrides();
    testFollow();

    std::printf (failures == 0 ? "\nTudo certo.\n" : "\n%d falha(s).\n", failures);
    return failures == 0 ? 0 : 1;
}
