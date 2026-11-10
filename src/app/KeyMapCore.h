#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

/** O miolo dos atalhos de teclado, sem JUCE (testado em tests/editors): a tabela de ações com as teclas dos
    dois presets, as trocas do usuário por cima e a pergunta "que ação essa tecla dispara aqui?".

    Cada ação tem um contexto: "" (vale no app inteiro, quem responde é a janela), "playlist" ou "pianoroll"
    (vale com o teclado naquele editor). A mesma tecla pode estar em dois editores (o P é o lápis nos dois),
    mas não num editor e no app inteiro ao mesmo tempo: o editor ficaria com ela e a ação global nunca
    dispararia ali.

    O KeyMap (KeyMap.h) é a casca com juce::KeyPress, o arquivo atalhos.json e os nomes das teclas. */
namespace whynot::keymap
{

enum Mod : int { ctrl = 1, shift = 2, alt = 4 };

/** Teclas que não são um caractere. O KeyMap traduz de e para os códigos do JUCE; as outras teclas especiais
    (F13, teclado numérico...) passam com o código do JUCE mesmo, que fica abaixo de `first`. */
namespace code
{
    enum : int
    {
        first = 0x40000000,
        space = first, enter, escape, tab, del, backspace, insert, home, end, pageUp, pageDown,
        left, right, up, down, numpadAdd, numpadSubtract,
        f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12
    };
}

struct Chord
{
    int code = 0;   // caractere em maiúscula ('P', '='), um dos `code::` ou o código do JUCE de uma tecla rara
    int mods = 0;   // soma de Mod

    constexpr bool operator== (const Chord& o) const noexcept   { return code == o.code && mods == o.mods; }
    constexpr bool operator!= (const Chord& o) const noexcept   { return ! (*this == o); }
};

/** Tecla da tabela: a letra vale em maiúscula ou minúscula. */
constexpr Chord chord (int c, int mods = 0) noexcept
{
    return { c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c, mods };
}

using Keys = std::vector<Chord>;

struct Action
{
    std::string_view id;        // "playlist.tool.draw"
    std::string_view title;     // em português (UTF-8), como aparece no editor de atalhos
    std::string_view group;     // o título do bloco no editor de atalhos
    std::string_view context;   // "" = app inteiro, "playlist", "pianoroll"
    Keys fl, whynot;            // as teclas em cada preset
};

inline constexpr std::string_view global = "", playlist = "playlist", pianoRoll = "pianoroll";

/** Duas ações disputam a mesma tecla? Só no mesmo contexto, ou quando uma delas vale no app inteiro. */
constexpr bool contextsClash (std::string_view a, std::string_view b) noexcept
{
    return a == b || a.empty() || b.empty();
}

inline const std::vector<Action>& actions()
{
    using namespace code;
    auto both = [] (std::string_view id, std::string_view title, std::string_view group, std::string_view context, Keys keys)
    {
        return Action { id, title, group, context, keys, keys };
    };
    auto k = [] (int c, int mods = 0) { return chord (c, mods); };

    static const std::vector<Action> list {
        both ("transport.togglePlay",  "Tocar ou parar (volta à agulha)",          "Transporte", global, { k (space) }),
        both ("transport.togglePause", "Tocar ou pausar (continua de onde parou)", "Transporte", global, { k (space, ctrl) }),
        both ("transport.songMode",    "Alternar PAT e SONG",                      "Transporte", global, { k ('L') }),
        both ("transport.metronome",   "Metrônomo",                                "Transporte", global, { k ('M', ctrl) }),
        both ("transport.record",      "Gravar (liga e desliga o REC)",            "Transporte", global, { k ('R') }),
        both ("transport.countIn",     "Contagem antes de gravar (liga e desliga)", "Transporte", global, { k ('P', ctrl) }),
        both ("record.cancel",         "Cancelar o take (apaga o arquivo)",        "Transporte", global, {}),
        both ("transport.tapTempo",    "Tap tempo: bata no ritmo para achar o BPM", "Transporte", global, {}),

        // FL: Ctrl+N grava uma versão nova. WHYNOT: Ctrl+N é projeto novo, como nos outros programas.
        { "file.new",            "Novo projeto",        "Arquivo", global, {},                {  k ('N', ctrl) } },
        both ("file.open",       "Abrir projeto",       "Arquivo", global, { k ('O', ctrl) }),
        both ("file.save",       "Salvar",              "Arquivo", global, { k ('S', ctrl) }),
        both ("file.saveAs",     "Salvar como",         "Arquivo", global, { k ('S', ctrl | shift) }),
        { "file.saveNewVersion", "Salvar nova versão",  "Arquivo", global, { k ('N', ctrl) }, { k ('N', ctrl | alt) } },
        both ("file.importMidi", "Importar MIDI no piano roll do canal", "Arquivo", global, {}),

        both ("export.wav",   "Exportar WAV",  "Exportar", global, { k ('R', ctrl) }),
        both ("export.mp3",   "Exportar MP3",  "Exportar", global, { k ('R', ctrl | shift) }),
        both ("export.midi",  "Exportar MIDI", "Exportar", global, { k ('M', ctrl | shift) }),
        { "export.stems",     "Exportar stems (um WAV por trilha)", "Exportar", global, {}, { k ('R', ctrl | alt) } },

        both ("pattern.new",      "Próximo padrão vazio (cria um se não houver)", "Padrões", global, { k (f4) }),
        both ("pattern.clone",    "Clonar o padrão atual",                        "Padrões", global, { k ('C', ctrl | shift) }),
        both ("pattern.next",     "Próximo padrão",                               "Padrões", global, { k (numpadAdd) }),
        both ("pattern.previous", "Padrão anterior",                              "Padrões", global, { k (numpadSubtract) }),

        // FL: o Ctrl+Z volta passo a passo, como o "Alternate undo mode" do FL e o padrão do Ableton (pedido do
        // usuário, 21/10): o Ctrl+Z que alterna refazia o que a pessoa tinha acabado de apagar. O Ctrl+Alt+Z do FL
        // continua desfazendo; o alternar fica no editor de atalhos, sem tecla.
        both ("edit.undoToggle", "Desfazer ou refazer a última edição", "Edição", global, {}),
        { "edit.undo",           "Desfazer passo a passo",              "Edição", global,
          { k ('Z', ctrl), k ('Z', ctrl | alt) }, { k ('Z', ctrl) } },
        both ("edit.redo",       "Refazer",                             "Edição", global, { k ('Y', ctrl), k ('Z', ctrl | shift) }),
        both ("playlist.addMarker", "Marcador na agulha (intro, verso, refrão)", "Edição", global, { k ('T', alt) }),   // como o "Add marker" do FL (bloco 1, 22/10)

        both ("view.playlist",       "Playlist",                              "Telas", global, { k (f5) }),
        both ("view.channelRack",    "Channel rack",                          "Telas", global, { k (f6) }),
        both ("view.pianoRoll",      "Piano roll",                            "Telas", global, { k (f7) }),
        both ("view.files",          "Navegador de sons",                     "Telas", global, { k (f8, alt) }),
        both ("view.plugins",        "Plugins",                               "Telas", global, { k (f8) }),
        both ("view.mixer",          "Mixer",                                 "Telas", global, { k (f9) }),
        both ("view.settings",       "Áudio e MIDI",                          "Telas", global, { k (f10) }),
        both ("view.projects",       "Projetos (ouvir sem abrir)",            "Telas", global, { k (f11) }),
        both ("projects.switchDeck", "Projetos: trocar entre os decks A e B", "Telas", global, { k (tab) }),
        both ("view.shortcuts",      "Atalhos de teclado",                    "Telas", global, {}),
        both ("view.commandPalette", "Paleta de comandos (achar e rodar qualquer ação)", "Telas", global, { k ('P', ctrl | shift) }),

        both ("keyboard.typing",    "Teclado do computador como piano",         "Teclado", global, { k ('T', ctrl) }),
        both ("record.captureMidi", "Capturar o que foi tocado (Capture MIDI)", "Teclado", global, {}),

        both ("ui.zoomIn",    "Aumentar interface", "Interface", global, { k ('=', ctrl) }),
        both ("ui.zoomOut",   "Diminuir interface", "Interface", global, { k ('-', ctrl) }),
        both ("ui.zoomReset", "Interface em 100%",  "Interface", global, { k ('0', ctrl) }),

        // Playlist: valem com o teclado na grade da playlist. As teclas são as que estavam fixas no código.
        both ("playlist.tool.draw",   "Ferramenta: desenhar",   "Playlist", playlist, { k ('P') }),
        both ("playlist.tool.paint",  "Ferramenta: pintar",     "Playlist", playlist, { k ('B') }),
        both ("playlist.tool.slice",  "Ferramenta: cortar",     "Playlist", playlist, { k ('C') }),
        both ("playlist.tool.erase",  "Ferramenta: apagar",     "Playlist", playlist, { k ('D') }),
        both ("playlist.tool.select", "Ferramenta: selecionar", "Playlist", playlist, { k ('E') }),
        both ("playlist.tool.mute",   "Ferramenta: mudo",       "Playlist", playlist, { k ('T') }),
        both ("playlist.tool.slip",   "Ferramenta: deslizar o conteúdo", "Playlist", playlist, { k ('S') }),
        both ("playlist.tool.play",   "Ferramenta: tocar daqui", "Playlist", playlist, { k ('Y') }),
        both ("playlist.tool.zoom",   "Ferramenta: zoom",       "Playlist", playlist, { k ('Z') }),
        both ("playlist.follow",      "Seguir a reprodução (a tela anda com a agulha)", "Playlist", playlist, { k ('F') }),
        both ("playlist.selectAll",   "Selecionar tudo",        "Playlist", playlist, { k ('A', ctrl) }),
        both ("playlist.copy",        "Copiar",                 "Playlist", playlist, { k ('C', ctrl) }),
        both ("playlist.cut",         "Recortar",               "Playlist", playlist, { k ('X', ctrl) }),
        both ("playlist.paste",       "Colar",                  "Playlist", playlist, { k ('V', ctrl) }),
        both ("playlist.duplicate",   "Duplicar à direita",     "Playlist", playlist, { k ('B', ctrl) }),
        both ("playlist.delete",      "Apagar a seleção",       "Playlist", playlist, { k (del), k (backspace) }),
        both ("playlist.moveLeft",    "Seleção: um passo da grade para a esquerda", "Playlist", playlist, { k (left, shift) }),
        both ("playlist.moveRight",   "Seleção: um passo da grade para a direita",  "Playlist", playlist, { k (right, shift) }),
        both ("playlist.moveUp",      "Seleção: uma raia para cima",  "Playlist", playlist, { k (up, shift) }),
        both ("playlist.moveDown",    "Seleção: uma raia para baixo", "Playlist", playlist, { k (down, shift) }),
        both ("playlist.cutAtNeedle", "Cortar na agulha (ou nas bordas do trecho)", "Playlist", playlist, { k ('E', ctrl) }),
        both ("playlist.removeTime",  "Tirar o trecho de tempo (o resto encosta)",  "Playlist", playlist, { k (del, ctrl) }),
        both ("playlist.insertTime",  "Abrir espaço no trecho de tempo",            "Playlist", playlist, { k (insert, ctrl) }),
        both ("playlist.zoomSelection",  "Zoom na seleção",          "Playlist", playlist, { k ('Z', shift) }),
        both ("playlist.zoomSong",       "Zoom na música inteira",   "Playlist", playlist, { k ('Z', alt) }),
        both ("playlist.nextMarker",     "Pular para o próximo marcador",  "Playlist", playlist, { k (right, alt) }),
        both ("playlist.previousMarker", "Pular para o marcador anterior", "Playlist", playlist, { k (left, alt) }),

        // Piano roll: valem com o teclado na grade do piano roll.
        both ("pianoroll.tool.draw",   "Ferramenta: desenhar",   "Piano roll", pianoRoll, { k ('P') }),
        both ("pianoroll.tool.paint",  "Ferramenta: pintar",     "Piano roll", pianoRoll, { k ('B') }),
        both ("pianoroll.tool.select", "Ferramenta: selecionar", "Piano roll", pianoRoll, { k ('E') }),
        both ("pianoroll.tool.slice",  "Ferramenta: cortar",     "Piano roll", pianoRoll, { k ('C') }),
        both ("pianoroll.slide",       "Slide (liga e desliga; com seleção, nas notas selecionadas)", "Piano roll", pianoRoll, { k ('S') }),
        both ("pianoroll.follow",      "Seguir a reprodução (a tela anda com a agulha)", "Piano roll", pianoRoll, { k ('F') }),
        both ("pianoroll.selectAll",   "Selecionar tudo",        "Piano roll", pianoRoll, { k ('A', ctrl) }),
        both ("pianoroll.copy",        "Copiar",                 "Piano roll", pianoRoll, { k ('C', ctrl) }),
        both ("pianoroll.cut",         "Recortar",               "Piano roll", pianoRoll, { k ('X', ctrl) }),
        both ("pianoroll.paste",       "Colar",                  "Piano roll", pianoRoll, { k ('V', ctrl) }),
        both ("pianoroll.duplicate",   "Duplicar à direita",     "Piano roll", pianoRoll, { k ('B', ctrl) }),
        both ("pianoroll.delete",      "Apagar as notas selecionadas", "Piano roll", pianoRoll, { k (del), k (backspace) }),
        both ("pianoroll.quantize",    "Quantizar na grade",     "Piano roll", pianoRoll, { k ('Q', alt) }),
        both ("pianoroll.legato",      "Legato (cada nota vai até a próxima)", "Piano roll", pianoRoll, { k ('L', ctrl) }),
        both ("pianoroll.glue",        "Juntar notas",           "Piano roll", pianoRoll, { k ('G', ctrl) }),
        both ("pianoroll.mute",        "Calar as notas selecionadas", "Piano roll", pianoRoll, { k ('M', alt) }),
        both ("pianoroll.unmute",      "Tirar o mudo das notas", "Piano roll", pianoRoll, { k ('M', alt | shift) }),
        both ("pianoroll.transposeUp",   "Transpor um semitom para cima",  "Piano roll", pianoRoll, { k (up) }),
        both ("pianoroll.transposeDown", "Transpor um semitom para baixo", "Piano roll", pianoRoll, { k (down) }),
        both ("pianoroll.octaveUp",      "Transpor uma oitava para cima",  "Piano roll", pianoRoll, { k (up, ctrl), k (up, shift) }),
        both ("pianoroll.octaveDown",    "Transpor uma oitava para baixo", "Piano roll", pianoRoll, { k (down, ctrl), k (down, shift) }),
        both ("pianoroll.nudgeLeft",     "Notas: um passo da grade para a esquerda", "Piano roll", pianoRoll, { k (left), k (left, shift) }),
        both ("pianoroll.nudgeRight",    "Notas: um passo da grade para a direita",  "Piano roll", pianoRoll, { k (right), k (right, shift) }),
    };

    return list;
}

inline const Action* findAction (std::string_view id)
{
    for (auto& a : actions())
        if (a.id == id)
            return &a;

    return nullptr;
}

inline bool isPreset (std::string_view name) noexcept   { return name == "fl" || name == "whynot"; }

inline const Keys& presetKeys (const Action& a, std::string_view preset) noexcept
{
    return preset == "whynot" ? a.whynot : a.fl;
}

/** O preset escolhido e as trocas do usuário por cima dele. */
class Map
{
public:
    using Overrides = std::map<std::string, Keys, std::less<>>;

    const std::string& preset() const noexcept   { return presetName; }

    /** Escolhe o preset e apaga as trocas do usuário. Nome desconhecido vira "fl". */
    void setPreset (std::string_view name)
    {
        presetName = isPreset (name) ? std::string (name) : std::string ("fl");
        overrides.clear();
    }

    /** Só o nome, sem mexer nas trocas (ao ler o arquivo). */
    void setPresetKeepingOverrides (std::string_view name)
    {
        if (isPreset (name))
            presetName = std::string (name);
    }

    const Overrides& getOverrides() const noexcept   { return overrides; }
    bool isCustomised (std::string_view id) const    { return overrides.find (id) != overrides.end(); }

    Keys keysFor (std::string_view id) const
    {
        if (auto it = overrides.find (id); it != overrides.end())
            return it->second;

        const auto* a = findAction (id);
        return a != nullptr ? presetKeys (*a, presetName) : Keys();
    }

    /** Troca as teclas de uma ação (lista vazia = sem atalho) e tira essas teclas das ações que disputam com ela
        (mesmo contexto, ou uma das duas no app inteiro). Em outro editor a tecla fica. */
    void setKeys (std::string_view id, const Keys& keys)
    {
        const auto* target = findAction (id);

        if (target == nullptr)
            return;

        for (auto& a : actions())
        {
            if (a.id == id || ! contextsClash (a.context, target->context))
                continue;

            auto current = currentKeys (a);
            const auto before = current.size();

            for (auto& key : keys)
                for (auto it = current.begin(); it != current.end();)
                    it = *it == key ? current.erase (it) : it + 1;

            if (current.size() != before)
                overrides[std::string (a.id)] = current;
        }

        overrides[std::string (id)] = keys;
    }

    /** Guarda uma troca lida do arquivo como está, sem mexer nas outras ações. */
    void setRaw (std::string_view id, const Keys& keys)
    {
        if (findAction (id) != nullptr)
            overrides[std::string (id)] = keys;
    }

    void reset (std::string_view id)
    {
        if (auto it = overrides.find (id); it != overrides.end())
            overrides.erase (it);
    }

    /** A ação desse contexto que a tecla dispara, ou vazio. Não aloca (é chamado a cada tecla). */
    std::string_view actionFor (Chord key, std::string_view context) const
    {
        for (auto& a : actions())
            if (a.context == context)
                for (auto& candidate : currentKeys (a))
                    if (candidate == key)
                        return a.id;

        return {};
    }

    /** A ação que perderia a tecla se ela fosse dada a `id` (para o aviso do editor de atalhos), ou vazio. */
    std::string_view clashFor (std::string_view id, Chord key) const
    {
        const auto* target = findAction (id);

        if (target == nullptr)
            return {};

        for (auto& a : actions())
            if (a.id != id && contextsClash (a.context, target->context))
                for (auto& candidate : currentKeys (a))
                    if (candidate == key)
                        return a.id;

        return {};
    }

private:
    const Keys& currentKeys (const Action& a) const noexcept
    {
        if (auto it = overrides.find (a.id); it != overrides.end())
            return it->second;

        return presetKeys (a, presetName);
    }

    std::string presetName { "fl" };
    Overrides overrides;
};

} // namespace whynot::keymap
