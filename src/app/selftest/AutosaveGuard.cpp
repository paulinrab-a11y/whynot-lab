// Autoteste da frente app-autosave (10/11): o autosave periódico espera o gesto (com teto de adiamentos) e lê só
// os plugins que podem ter mudado; a sessão anota a janela de plugin aberta e os últimos plugins adicionados, e a
// abertura depois de uma queda cita o suspeito e oferece o bloqueio, sem bloquear sozinha.
// Nenhuma janela aparece: a "janela" do plugin é uma fábrica que não cria nada. Usa os plugins de build/test_plugins.

#include "SelfTestKit.h"
#include "../../model/WnlFormat.h"

namespace whynot::selftest
{

void runAutosaveGuardSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    const int retryTicks = AppController::autosaveRetrySeconds * 10;   // o Housekeeping chama o serviceAutosave a cada 100 ms

    //--------------------------------------------------------------------------
    rep.section ("Autosave: espera o gesto acabar"_t);
    {
        app.run ("project.new");
        const auto channel = model.get().channels.empty() ? juce::String() : model.get().channels.front().id;

        app.makeAutosaveDue();
        rep.check (app.serviceAutosave() == juce::File(), "venceu sem nada mudado: não grava"_t);

        app.getCommands().beginGesture ("Autoteste");
        app.run ("steps.set", args ({ { "channel", channel }, { "index", 2 }, { "on", true } }));
        app.makeAutosaveDue();
        const auto during = app.serviceAutosave();
        rep.check (during == juce::File() && app.autosaveDeferrals() == 1, "venceu no meio de um gesto: adia, não grava"_t);
        app.getCommands().endGesture();

        juce::File after;
        int ticks = 0;

        while (after == juce::File() && ticks < retryTicks * 4)
        {
            after = app.serviceAutosave();
            ++ticks;
        }

        rep.check (after.existsAsFile() && ticks == retryTicks,
                   "gesto fechado: grava na tentativa seguinte, "_t + juce::String (ticks / 10.0, 1) + " s depois");
        rep.check (app.autosaveDeferrals() == 0 && model.dirty, "os adiamentos zeram e o projeto continua não salvo"_t);

        Project fromAutosave;
        rep.check (after.existsAsFile() && wnl::load (after, fromAutosave).wasOk() && fromAutosave.channels.size() == model.get().channels.size(),
                   "o autosave adiado abre e tem o projeto inteiro"_t);

        // teto: um gesto que não acaba nunca ganha autosave no meio; fechou, grava no passo seguinte
        app.getCommands().beginGesture ("Autoteste");
        app.run ("steps.set", args ({ { "channel", channel }, { "index", 3 }, { "on", true } }));
        app.makeAutosaveDue();
        int savedInGesture = 0;

        for (int i = 0; i < AppController::autosaveMaxDeferrals * retryTicks + 50; ++i)
            if (app.serviceAutosave() != juce::File())
                ++savedInGesture;

        rep.check (savedInGesture == 0 && app.autosaveDeferrals() == AppController::autosaveMaxDeferrals,
                   "gesto longo: nenhum autosave no meio e os adiamentos param no teto ("_t + juce::String (app.autosaveDeferrals()) + ")");
        app.getCommands().endGesture();

        const auto atLimit = app.serviceAutosave();
        rep.check (atLimit.existsAsFile(), "passado o teto, grava no primeiro passo depois que o gesto fecha"_t);

        after.deleteFile();
        atLimit.deleteFile();
    }

    //--------------------------------------------------------------------------
    rep.section ("Autosave e queda: plugins sujos, janela aberta e suspeitos"_t);
    {
        juce::String synthId;
        const auto listed = app.run ("plugins.list");   // guardar: o ponteiro abaixo aponta para dentro dele

        if (auto* list = listed.value["plugins"].getArray())
            for (auto& d : *list)
                if (d["name"].toString() == "WHYNOT Teste Synth")
                    synthId = d["id"];

        if (synthId.isEmpty())
        {
            rep.check (false, "plugins de teste no catálogo (compile o alvo whynot_test_plugins)"_t);
            return;
        }

        app.run ("project.new");
        app.setCrashSuspectTracking (true);   // sem janela a anotação nasce desligada
        app.setPluginWindowFactory ([] (te::Plugin&) { return std::unique_ptr<juce::Component>(); });   // nada aparece na tela

        const auto suspectsFile = app.crashSuspectsFile();
        const auto added = app.run ("channel.addPlugin", args ({ { "plugin", synthId } }));
        const auto channel = added.value["channel"].toString();
        auto* plugin = app.getProjector().instrumentFor (channel);
        auto stateNow = [&] { auto* c = model.get().findChannel (channel); return c != nullptr ? c->plugin.state : juce::String(); };

        rep.check (added.ok && plugin != nullptr, "canal com o plugin de teste"_t);

        if (plugin == nullptr)
        {
            app.setCrashSuspectTracking (false);
            return;
        }

        const auto afterInsert = suspectsFile.loadFileAsString();
        rep.check (afterInsert.contains ("inserido\t") && afterInsert.contains ("WHYNOT Teste Synth") && ! afterInsert.contains ("janela\t"),
                   "plugin adicionado entra nos últimos adicionados da sessão"_t);

        // plugins sujos: parâmetro mudado por comando é relido pelo autosave periódico...
        app.capturePluginStates();   // leitura completa: tudo limpo
        const auto state0 = stateNow();
        app.run ("plugin.setParameter", args ({ { "channel", channel }, { "parameter", "Volume" }, { "value", 0.25 } }));
        app.makeAutosaveDue();
        const auto dirtySave = app.serviceAutosave();
        const auto state1 = stateNow();
        rep.check (dirtySave.existsAsFile() && state1 != state0, "parâmetro mudado por comando: o autosave periódico relê o plugin"_t);

        // ...e um plugin em que ninguém mexeu pelos caminhos conhecidos não é relido (o salvar de verdade relê)
        if (auto* instance = app.channelPlugin (channel))
            for (auto* p : instance->getParameters())
                p->setValueNotifyingHost (0.9f);

        app.makeAutosaveDue();
        app.serviceAutosave();
        rep.check (stateNow() == state1, "plugin sem janela aberta nem comando: o autosave periódico não relê"_t);
        app.capturePluginStates();
        rep.check (stateNow() != state1, "a leitura completa (salvar, exportar, fechar a janela) continua lendo todos"_t);
        dirtySave.deleteFile();

        // janela aberta: anotada antes de o editor nascer
        app.getEngine().getUIBehaviour().createPluginWindow (*plugin->windowState);
        const auto withWindow = suspectsFile.loadFileAsString();
        rep.check (app.openPluginWindowNames().contains ("WHYNOT Teste Synth") && withWindow.contains ("janela\t"),
                   "abrir a janela do plugin grava a anotação de janela aberta"_t);

        // queda simulada: a mesma anotação, de um processo que não existe mais (PID 999999, como no teste do Session)
        const auto fallen = suspectsFile.getSiblingFile ("suspeitos-999999.txt");
        suspectsFile.copyFileTo (fallen);

        juce::String warning, status;
        AppController::CrashNotice notice;
        bool blockedAlone = true, blockedOnRequest = false;

        {
            AppController restarted (true, ctx.data);
            warning = restarted.takeStartupWarning();
            notice = restarted.takeCrashNotice();
            blockedAlone = restarted.getPluginCatalog().isBlocked (notice.file);
            status = restarted.blockSuspect (notice);
            blockedOnRequest = notice.file.isNotEmpty() && restarted.getPluginCatalog().isBlocked (notice.file);
        }

        rep.check (warning.contains ("WHYNOT Teste Synth") && warning.contains ("janela"), "abertura depois da queda cita o plugin da janela: "_t + warning);
        rep.check (notice.name == "WHYNOT Teste Synth" && notice.question.contains ("Bloquear") && ! blockedAlone,
                   "oferece bloquear o suspeito e não bloqueia sozinha"_t);
        rep.check (blockedOnRequest && status.contains ("bloqueado"), "aceitando, o suspeito vai para Bloqueados: "_t + status);
        rep.check (! fallen.exists() && suspectsFile.existsAsFile(), "a anotação da queda é apagada; a desta sessão (processo vivo) fica"_t);

        // devolve o plugin de teste ao catálogo, como o teste da queda no carregamento
        app.getPluginCatalog().load();
        app.unblockPlugin (notice.file);
        app.run ("plugins.scan", args ({ { "wait", true }, { "timeoutSeconds", 6 } }));

        // janela fechada (aqui nenhuma abriu de verdade): a anotação sai na conferência de 1 s
        app.refreshPluginWindowMarker();
        rep.check (app.openPluginWindowNames().isEmpty() && ! suspectsFile.loadFileAsString().contains ("janela\t"),
                   "fechar a janela apaga a anotação de janela aberta"_t);

        app.setCrashSuspectTracking (false);
        rep.check (! suspectsFile.exists(), "sem suspeitos, o arquivo da sessão some"_t);

        app.setPluginWindowFactory (nullptr);
        app.run ("project.new");
    }
}

} // namespace whynot::selftest
