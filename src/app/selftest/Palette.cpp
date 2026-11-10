// Autoteste da paleta de comandos (frente paleta, 10/11):
// - o que entra na lista: as ações do KeyMap e os comandos sem parâmetro obrigatório, sem repetir e sem as consultas;
// - a busca: "expo" acha Exportar WAV com o atalho ao lado, acento e caixa ignorados, nada achado;
// - as teclas (setas, Page Down, Enter, Esc), executar uma ação e um comando inofensivos, os recentes e o erro no pé;
// - a captura, sem janela: a paleta dentro de um pai fora da área de trabalho (createComponentSnapshot), em paleta.png.
// O miolo da busca tem teste próprio, sem JUCE: tests/ui/test_command_search.cpp.
// Dono: paleta.

#include "SelfTestKit.h"
#include "../KeyMap.h"
#include "../../model/WnlFormat.h"
#include "../../ui/CommandPalette.h"
#include "../../ui/Theme.h"

#include <set>

namespace whynot::selftest
{

namespace
{
    struct Stage : juce::Component
    {
        void paint (juce::Graphics& g) override   { g.fillAll (theme::colour::panel); }
    };

    int rowOf (const CommandPalette& palette, const juce::String& id)
    {
        for (int i = 0; i < palette.numResults(); ++i)
            if (palette.resultId (i) == id)
                return i;

        return -1;
    }

    bool needsParams (const CommandInfo& info)
    {
        const auto* required = info.params["required"].getArray();
        return required != nullptr && ! required->isEmpty();
    }
}

void runPaletteSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;

    rep.section ("Paleta de comandos"_t);

    theme::LookAndFeel laf;   // o visual da casa, só neste pai (nada global)
    Stage stage;              // fora da área de trabalho: sem janela, sem roubar o foco
    stage.setLookAndFeel (&laf);
    stage.setSize (1280, 720);

    CommandPalette palette (app);
    stage.addChildComponent (palette);

    juce::StringArray ran, status;
    bool lastWasError = false, handleActions = true;
    palette.onAction = [&] (const juce::String& id) { ran.add (id); return handleActions; };
    palette.onStatus = [&] (const juce::String& text, bool isError) { status.add (text); lastWasError = isError; };

    rep.check (! palette.isOpen() && ! palette.handleKey (juce::KeyPress (juce::KeyPress::returnKey)),
               "fechada, a paleta não aparece nem pega teclas"_t);

    //--------------------------------------------------------------------------
    // o que entra
    palette.open();
    {
        std::set<juce::String> ids;
        bool unique = true, runnable = true;
        int numActions = 0, numCommands = 0;

        for (int i = 0; i < palette.numResults(); ++i)
        {
            const auto id = palette.resultId (i);
            unique = ids.insert (id).second && unique;

            if (KeyMap::findAction (id) != nullptr)
            {
                ++numActions;
            }
            else
            {
                ++numCommands;
                const auto* info = app.getCommands().find (id);
                runnable = runnable && info != nullptr && ! needsParams (*info) && palette.resultTitle (i) == info->title;
            }
        }

        // a ação de abrir a paleta (a frente editores põe no KeyMap) não entra na própria lista
        const int expectedActions = (int) KeyMap::actions().size() - (KeyMap::findAction (CommandPalette::actionId) != nullptr ? 1 : 0);

        rep.check (palette.isOpen() && palette.getQuery().isEmpty() && palette.selectedRow() == 0, "abre com a busca vazia e a primeira linha escolhida"_t);
        rep.check (numActions == expectedActions && rowOf (palette, CommandPalette::actionId) < 0,
                   "todas as ações do KeyMap, menos a que abre a paleta: "_t + juce::String (numActions));
        rep.check (numCommands >= 15 && runnable,
                   "os comandos do registro que rodam sem parâmetro obrigatório, com o título do registro: "_t + juce::String (numCommands));
        rep.check (unique, "nenhum id repetido"_t);
        rep.check (rowOf (palette, "transport.stop") >= 0 && rowOf (palette, "pattern.add") >= 0 && rowOf (palette, "audio.resetXruns") >= 0,
                   "Parar, Novo padrão e Zerar falhas de buffer (comandos sem parâmetro) estão na lista"_t);
        rep.check (rowOf (palette, "project.open") < 0 && rowOf (palette, "view.toggle") < 0 && rowOf (palette, "transport.setBpm") < 0,
                   "comando que exige parâmetro não entra"_t);
        rep.check (rowOf (palette, "file.new") >= 0 && rowOf (palette, "project.new") < 0 && rowOf (palette, "transport.setSongMode") < 0
                       && rowOf (palette, "file.save") >= 0 && rowOf (palette, "project.save") < 0,
                   "sem duplicar: o comando que uma ação do KeyMap já cobre fica de fora"_t);
        rep.check (rowOf (palette, "plugins.list") < 0 && rowOf (palette, "shortcuts.list") < 0 && rowOf (palette, "record.state") < 0,
                   "as consultas da IA (só devolvem dados) ficam de fora"_t);
        rep.check (palette.resultId (0) == KeyMap::actions().front().id, "sem recentes, a lista vem na ordem do KeyMap"_t);
    }

    //--------------------------------------------------------------------------
    // a tela: centrada no alto, 560 px, dez linhas à vista
    {
        const auto card = palette.getBounds().reduced (CommandPalette::shadow);
        rep.check (card.getWidth() == CommandPalette::cardWidth && std::abs (card.getCentreX() - stage.getWidth() / 2) <= 1
                       && card.getY() == theme::metric::topBar + theme::metric::gap,
                   "painel de 560 px centrado no alto do pai"_t);
        rep.check (card.getHeight() == theme::metric::toolbar + 1 + 4 + CommandPalette::maxRows * CommandPalette::rowHeight + 8,
                   "dez linhas de 32 px à vista (o resto rola)"_t);
        rep.check (! palette.hitTest (2, 2) && palette.hitTest (palette.getWidth() / 2, palette.getHeight() / 2),
                   "a sombra não pega cliques; o cartão pega"_t);

        stage.setSize (1000, 600);
        rep.check (std::abs (palette.getBounds().getCentreX() - 500) <= 1, "o pai muda de tamanho e a paleta continua no meio"_t);
        stage.setSize (1280, 720);
    }

    //--------------------------------------------------------------------------
    // a busca
    palette.setQuery ("expo");
    rep.check (palette.resultId (0) == "export.wav" && palette.selectedRow() == 0,
               "\"expo\" acha Exportar WAV primeiro (veio "_t + palette.resultId (0) + ")");
    rep.check (palette.resultKeys (0) == app.getKeyMap().describeKeys ("export.wav") && palette.resultKeys (0).isNotEmpty(),
               "o atalho aparece ao lado: "_t + palette.resultKeys (0));

    palette.setQuery ("METRONOMO");
    rep.check (palette.resultId (0) == "transport.metronome", "acento e caixa ignorados: \"METRONOMO\" acha Metrônomo"_t);

    palette.setQuery ("view.mix");
    rep.check (palette.resultId (0) == "view.mixer", "a busca também vale pelo id"_t);

    {
        const auto png = ctx.work.getChildFile ("paleta.png");
        palette.setQuery ("exp");
        const auto image = juce::SoftwareImageType().convert (stage.createComponentSnapshot (stage.getLocalBounds(), true, 1.0f));
        png.deleteFile();
        bool saved = false;

        if (juce::FileOutputStream out (png); out.openedOk())
            saved = juce::PNGImageFormat().writeImageToStream (image, out);

        rep.check (saved && image.getWidth() == stage.getWidth(), "captura da paleta com \"exp\": "_t + png.getFullPathName());

        // só a linha ativa tem o laranja: o fundo dela puxa para o vermelho; o da linha de baixo é o cinza do cartão
        const int x = palette.getX() + CommandPalette::shadow + theme::metric::gap + 4;
        const int y = palette.getY() + CommandPalette::shadow + theme::metric::toolbar + 1 + 4 + CommandPalette::rowHeight / 2;
        const auto active = image.getPixelAt (x, y), next = image.getPixelAt (x, y + CommandPalette::rowHeight);
        rep.check ((int) active.getRed() - (int) active.getBlue() > 20 && std::abs ((int) next.getRed() - (int) next.getBlue()) < 8,
                   "laranja só na linha ativa"_t);
    }

    //--------------------------------------------------------------------------
    // as teclas
    palette.setQuery ({});
    const int total = palette.numResults();
    palette.handleKey (juce::KeyPress (juce::KeyPress::downKey));
    palette.handleKey (juce::KeyPress (juce::KeyPress::downKey));
    const int afterDown = palette.selectedRow();
    palette.handleKey (juce::KeyPress (juce::KeyPress::upKey));
    const int afterUp = palette.selectedRow();
    palette.handleKey (juce::KeyPress (juce::KeyPress::pageDownKey));
    const int afterPage = palette.selectedRow();
    palette.handleKey (juce::KeyPress (juce::KeyPress::pageUpKey));
    palette.handleKey (juce::KeyPress (juce::KeyPress::upKey));
    palette.handleKey (juce::KeyPress (juce::KeyPress::upKey));
    rep.check (afterDown == 2 && afterUp == 1 && afterPage == 1 + CommandPalette::maxRows - 1 && palette.selectedRow() == total - 1,
               "setas e Page Down andam na lista; a seta para cima na primeira linha dá a volta"_t);
    rep.check (! palette.handleKey (juce::KeyPress ('a')) && ! palette.handleKey (juce::KeyPress (juce::KeyPress::homeKey)),
               "letras e o Home ficam com o campo de busca"_t);

    palette.setQuery ("qqqqzz");
    palette.handleKey (juce::KeyPress (juce::KeyPress::returnKey));
    rep.check (palette.numResults() == 0 && palette.isOpen() && ran.isEmpty() && status.isEmpty(), "nada achado: a lista fica vazia e o Enter não faz nada"_t);

    palette.handleKey (juce::KeyPress (juce::KeyPress::escapeKey));
    rep.check (! palette.isOpen(), "Esc fecha"_t);

    //--------------------------------------------------------------------------
    // executar
    palette.open();
    palette.setQuery ("interface em 100");
    const auto zoomRow = palette.resultId (0);
    palette.handleKey (juce::KeyPress (juce::KeyPress::returnKey));
    rep.check (zoomRow == "ui.zoomReset" && ran.size() == 1 && ran[0] == "ui.zoomReset" && ! palette.isOpen() && status.isEmpty(),
               "Enter numa ação do KeyMap: passa pelo caminho da tecla (onAction) e fecha"_t);

    palette.open();
    palette.setQuery ("zerar falhas");
    const auto xrunRow = palette.resultId (0);
    const auto before = wnl::toJson (ctx.model.get());
    palette.handleKey (juce::KeyPress (juce::KeyPress::returnKey));
    rep.check (xrunRow == "audio.resetXruns" && ! palette.isOpen() && status.size() == 1 && ! lastWasError && ran.size() == 1,
               "Enter num comando do registro: roda pelo app.run, fecha e o título vai para o pé"_t);
    rep.check (wnl::toJson (ctx.model.get()) == before, "o comando inofensivo não mexeu no projeto"_t);

    palette.open();
    rep.check (palette.resultId (0) == "audio.resetXruns" && palette.resultId (1) == "ui.zoomReset"
                   && palette.resultId (2) == KeyMap::actions().front().id && palette.numResults() == total,
               "busca vazia: os recentes primeiro, o mais novo na frente, e depois o resto"_t);

    // o clique numa linha faz o mesmo que o Enter (runRow é o que o listBoxItemClicked chama)
    palette.runRow (1);
    rep.check (ran.size() == 2 && ran[1] == "ui.zoomReset" && ! palette.isOpen(), "clicar numa linha executa e fecha"_t);

    handleActions = false;
    palette.open();
    palette.runRow (0);
    rep.check (status.size() == 2 && lastWasError && ! palette.isOpen(), "ação que ninguém atende: o erro vai para a barra de status"_t);

    palette.open();
    palette.open();
    rep.check (! palette.isOpen(), "a tecla da paleta, com ela aberta, fecha"_t);

    stage.removeChildComponent (&palette);
    stage.setLookAndFeel (nullptr);
}

} // namespace whynot::selftest
