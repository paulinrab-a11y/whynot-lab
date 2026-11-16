// Autoteste da frente avisos-tela (16/11): o que a rodada anterior deixou pronto por baixo e agora aparece na tela.
// - o .wnl: projeto que declara recursos desconhecidos (app.features) avisa ao abrir e pergunta antes do primeiro
//   salvar por cima (o original fica em history/); caminho de rede ou que sai da pasta avisa quantos e por quê;
//   o salvar lista o que não foi para samples/ por não ser áudio.
// Nenhuma janela nem caixa aparece: o aviso é lido do estado (AppController::takeProjectNotices e os textos das
// perguntas), e a resposta da caixa é simulada chamando o que o botão chama (como o AutosaveGuard.cpp faz com o
// blockSuspect). Dono: avisos-tela.

#include "SelfTestKit.h"

namespace whynot::selftest
{

namespace
{
    void set (juce::var node, const char* key, const juce::var& value)
    {
        if (auto* o = node.getDynamicObject())
            o->setProperty (key, value);
    }

    juce::String slash (const juce::File& f)   { return f.getFullPathName().replaceCharacter ('\\', '/'); }

    /** Os textos dos avisos numa linha, para o relatório (a coordenação revisa a redação por aqui). */
    juce::String texts (const std::vector<AppController::ProjectNotice>& notices)
    {
        juce::StringArray list;

        for (auto& n : notices)
            list.add ((n.warning ? "[aviso] " : "[nota] ") + n.text);

        return list.isEmpty() ? juce::String ("(nenhum)") : list.joinIntoString (" | ");
    }

    bool anyContains (const std::vector<AppController::ProjectNotice>& notices, const juce::String& a, const juce::String& b, bool warning)
    {
        for (auto& n : notices)
            if (n.warning == warning && n.text.contains (a) && n.text.contains (b))
                return true;

        return false;
    }
}

void runScreenNoticesSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;

    const auto root = ctx.work.getChildFile ("avisos-tela");
    root.deleteRecursively();
    root.createDirectory();

    //--------------------------------------------------------------------------
    rep.section ("Avisos na tela: o .wnl (versão mais nova, caminhos recusados, o que não é áudio)"_t);
    {
        app.run ("project.new");

        const auto folder = root.getChildFile ("Futuro");
        const auto file = folder.getChildFile ("Futuro.wnl");
        folder.createDirectory();

        const bool saved = app.run ("project.save", args ({ { "path", file.getFullPathName() } })).ok;
        const auto afterPlainSave = app.takeProjectNotices();
        const bool reopened = app.openProject (file).wasOk();
        const auto afterPlainOpen = app.takeProjectNotices();
        rep.check (saved && reopened && afterPlainSave.empty() && afterPlainOpen.empty() && app.newerAppSaveQuestion().isEmpty(),
                   "projeto desta versão: salva e abre sem aviso, e salvar não pergunta nada ("_t + texts (afterPlainOpen) + ")");

        // salvo por uma versão mais nova: o arquivo declara um recurso que este app não conhece
        auto doc = juce::JSON::parse (file.loadFileAsString());
        set (doc["app"], "features", juce::Array<juce::var> { "recurso-do-futuro", "outro" });
        const bool written = file.replaceWithText (juce::JSON::toString (doc));

        const bool opened = app.openProject (file).wasOk();
        const auto onOpen = app.takeProjectNotices();
        rep.check (written && opened && onOpen.size() == 1 && anyContains (onOpen, "versão mais nova"_t, "recurso-do-futuro", true),
                   "features desconhecida: avisa ao abrir: "_t + texts (onOpen));

        const auto question = app.newerAppSaveQuestion();
        rep.check (question.contains ("Salvar por cima apaga") && question.contains ("recurso-do-futuro") && question.contains ("history"),
                   "e pergunta antes do primeiro salvar por cima, dizendo que o original fica em history: "_t
                       + question.replace ("\n", " "));
        rep.check (app.newerAppSaveQuestion (folder.getChildFile ("Outro nome.wnl")).isEmpty() && app.newerAppSaveQuestion (file).isNotEmpty(),
                   "\"Salvar como\" em outro arquivo não pergunta (o original não é tocado); em cima do mesmo, pergunta"_t);

        // a resposta "Salvar por cima" da caixa é o próprio project.save
        const bool over = app.run ("project.save").ok;
        const auto kept = folder.getChildFile ("history").findChildFiles (juce::File::findFiles, false, "*(app mais novo)*.wnl");
        const auto onSave = app.takeProjectNotices();
        rep.check (over && kept.size() == 1 && kept[0].loadFileAsString().contains ("recurso-do-futuro")
                       && onSave.size() == 1 && anyContains (onSave, "original", "history", false),
                   "salvando por cima, o original fica em history/ e o aviso diz onde: "_t + texts (onSave));

        const bool again = app.run ("project.save").ok;
        const auto onSecondSave = app.takeProjectNotices();
        rep.check (again && app.newerAppSaveQuestion().isEmpty() && onSecondSave.empty()
                       && folder.getChildFile ("history").findChildFiles (juce::File::findFiles, false, "*.wnl").size() == 1,
                   "depois do primeiro salvar o arquivo é desta versão: não pergunta nem avisa de novo, e não há segunda cópia"_t);

        // caminho de rede: recusado ao abrir (ficou só o nome do arquivo), e a tela diz quantos e por quê
        doc = juce::JSON::parse (file.loadFileAsString());
        set (doc["channels"][0]["sample"], "path", "\\\\servidor\\kit\\kick.wav");
        set (doc["channels"][0]["sample"], "originalPath", "");
        const auto network = folder.getChildFile ("Rede.wnl");
        const bool networkOpened = network.replaceWithText (juce::JSON::toString (doc)) && app.openProject (network).wasOk();
        const auto onNetwork = app.takeProjectNotices();
        rep.check (networkOpened && onNetwork.size() == 1 && anyContains (onNetwork, "1 caminho", "de rede", true)
                       && app.newerAppSaveQuestion().isEmpty(),
                   "caminho de rede: avisa ao abrir: "_t + texts (onNetwork));

        // caminho que sai da pasta do projeto: o outro motivo
        set (doc["channels"][0]["sample"], "path", "../../fora/kick.wav");
        const auto outside = folder.getChildFile ("Fora.wnl");
        const bool outsideOpened = outside.replaceWithText (juce::JSON::toString (doc)) && app.openProject (outside).wasOk();
        const auto onOutside = app.takeProjectNotices();
        rep.check (outsideOpened && onOutside.size() == 1 && anyContains (onOutside, "1 caminho", "sai da pasta", true),
                   "caminho com \"..\" para fora da pasta: avisa ao abrir: "_t + texts (onOutside));

        // o que não é áudio não entra em samples/ ao salvar, e a tela lista
        const auto notes = root.getChildFile ("fora").getChildFile ("notas.txt");
        notes.getParentDirectory().createDirectory();
        notes.replaceWithText ("um documento, não um sample");
        set (doc["channels"][0]["sample"], "path", slash (notes));
        const auto withDocument = folder.getChildFile ("Documento.wnl");
        const bool documentOpened = withDocument.replaceWithText (juce::JSON::toString (doc)) && app.openProject (withDocument).wasOk();
        const auto onDocumentOpen = app.takeProjectNotices();
        const bool documentSaved = app.run ("project.save").ok;
        const auto onDocumentSave = app.takeProjectNotices();
        rep.check (documentOpened && onDocumentOpen.empty() && documentSaved && onDocumentSave.size() == 1
                       && anyContains (onDocumentSave, "notas.txt", "não é áudio"_t, false)
                       && ! folder.getChildFile ("samples").getChildFile ("notas.txt").exists(),
                   "salvar com um arquivo que não é áudio: não vai para samples/ e o aviso lista: "_t + texts (onDocumentSave));

        app.run ("project.new");
        rep.check (app.takeProjectNotices().empty() && app.newerAppSaveQuestion().isEmpty(), "projeto novo começa sem aviso pendente"_t);
    }
}

} // namespace whynot::selftest
