#include <JuceHeader.h>

#include "AppController.h"
#include "CrashReport.h"
#include "SelfTest.h"
#include "WindowsPower.h"
#include "../common/Log.h"
#include "../control/ControlServer.h"
#include "../ui/MainComponent.h"
#include "../ui/MainWindow.h"
#include "../ui/SplashWindow.h"
#include "../ui/Theme.h"

namespace whynot
{

namespace
{
    std::filesystem::path toPath (const juce::File& file)
    {
        return std::filesystem::path (file.getFullPathName().toWideCharPointer());
    }

    /** O que o JUCE e o Tracktion mandam para o juce::Logger (aparelho de áudio aberto, erros do motor)
        vai para o log da sessão, junto com as linhas do whynot::log.

        O Logger pode ser chamado de qualquer thread, inclusive a de áudio e a de MIDI: por isso a ponte só
        enfileira (logFromAnyThread não trava, não aloca e não grava; o toRawUTF8 só devolve o ponteiro do
        texto, que o juce::String já guarda em UTF-8). Quem grava é a thread do log. */
    struct LogBridge final : juce::Logger
    {
        void logMessage (const juce::String& message) override   { whynot::logFromAnyThread (message.toRawUTF8()); }
    };
}

/** Linha de comando:
      whynot-lab                                  abre a janela
      whynot-lab --self-test <relatorio.txt>      testes headless; código de saída = nº de falhas
      whynot-lab --render <projeto.wnl> <saida.wav> [loops]  (padrão 4; na falha grava <saida>.erro.txt e sai com 1)
      whynot-lab --screenshot <saida.png> [projeto.wnl] [playlist|channelRack|pianoRoll|projects|sounds|plugins|mixer:<trilha>|ideias:<canal>[+plugins|+sons][@LxA]]
      whynot-lab --screenshot-fx <saida.png> <projeto.wnl> <trilha> <slot 1-10>   janela do efeito nativo
      whynot-lab --screenshot-synth <saida.png> <projeto.wnl> <canal> [página 1-4] [camada 1-5]
      --tema=<whynot|claro|colorido|frio>         em qualquer modo com janela: o tema de cores desta vez
                                                  (sem ele, a janela usa o tema salvo e as capturas, o padrão)
      whynot-lab --scan-plugin <plugin.vst3> <saida.xml>   processo filho do scan de plugins
      whynot-lab --control <porta> [projeto.wnl]  abre a janela com a ponte da IA em 127.0.0.1 (Fase 4)
      whynot-lab --headless --control <porta|0> [--control-file <json>] [--data <pasta>] [--parent-pid <pid>]
                                                  sem janela, só a ponte: fecha com app.quit ou quando o pai morre;
                                                  porta ocupada sai com 2 e grava <control-file>.erro.txt.
                                                  Sem --data, usa a pasta de dados de verdade (%APPDATA%\WHYNOT LAB):
                                                  o que a IA mudar lá (atalhos, gravacao.json, pastas de plugin) vale
                                                  também para o app com janela. Sem janela não há autosave nem
                                                  recuperação de queda: o projeto só fica salvo com project.save.
*/
class WhynotApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "WHYNOT LAB"; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        auto args = juce::StringArray::fromTokens (commandLine, true);
        juce::String themeArg;

        for (int i = args.size(); --i >= 0;)
        {
            if (args[i].startsWith ("--tema="))
            {
                themeArg = args[i].fromFirstOccurrenceOf ("=", false, false);
                args.remove (i);
            }
        }
        auto arg = [&args] (int i) { return args[i].unquoted(); };
        auto flag = [&args] (const char* name)   // o valor depois de "--nome", ou vazio
        {
            const int i = args.indexOf (name);
            return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : juce::String();
        };

        // O scan de plugins roda neste modo, num processo separado: se o plugin travar ou cair,
        // quem sai é este processo, não o app.
        if (args[0] == "--scan-plugin")
        {
            setApplicationReturnValue (runPluginScanChild (arg (1), juce::File (arg (2))));
            quit();
            return;
        }

        // Registro de queda (CrashReport.h) antes de qualquer outra coisa. O processo do scan, acima, fica de
        // fora: lá um plugin que cai é o esperado, e o pai lê o código de saída (3 = exceção, abort() também
        // sai com 3) para dizer o motivo em Bloqueados.
        startCrashReport (args, flag ("--data"));

        // Minimizado ou atrás de outra janela, o app continua nos núcleos rápidos (ver WindowsPower.h).
        keepFullCpuSpeed();

        if (args[0] == "--self-test")
        {
            const juce::File report (arg (1));
            const auto work = report.getParentDirectory().getChildFile ("selftest_work");
            const int failures = runSelfTest (report, work);
            setApplicationReturnValue (failures);
            quit();
            return;
        }

        if (args[0] == "--render")
        {
            AppController app (true);
            auto result = app.openProject (juce::File (arg (1)));

            if (result.wasOk())
                result = app.exportWav (juce::File (arg (2)), args.size() > 3 ? juce::jmax (1, arg (3).getIntValue()) : 4,
                                        app.getModel().get().songMode);

            if (result.failed())
            {
                // a pasta pode não existir (o render falhou antes de criá-la): sem ela, o motivo se perdia
                const auto errorFile = juce::File (arg (2)).withFileExtension ("erro.txt");
                errorFile.getParentDirectory().createDirectory();
                errorFile.replaceWithText (result.getErrorMessage());
            }

            setApplicationReturnValue (result.wasOk() ? 0 : 1);
            quit();
            return;
        }

        if (args[0] == "--headless")
        {
            // só a ponte da IA, sem janela (Fase 4): o servidor MCP abre o app assim
            const auto portText = flag ("--control");
            const auto dataText = flag ("--data");
            const auto controlFileText = flag ("--control-file");
            controller = std::make_unique<AppController> (true, dataText.isEmpty() ? juce::File() : juce::File (dataText));
            logAudioDevice();

            control::ControlServer::Options options;
            options.port = portText.getIntValue();
            options.onQuitRequested = [this] { quit(); };

            // a captura para a IA sem janela (fatia 4.6): a tela montada fora da área de trabalho, só para a foto
            options.capture = [this] (const juce::String& view, juce::String& channel, int maxWidth, const juce::File& png, juce::String& why)
            {
                return MainComponent::captureOffscreen (*controller, view, channel, maxWidth, png, why);
            };

            if (controlFileText.isNotEmpty())
                options.controlFile = juce::File (controlFileText);

            juce::String error = "Use --headless --control <porta>."_t;

            if (portText.isNotEmpty() && portText.containsOnly ("0123456789"))
            {
                controlServer = std::make_unique<control::ControlServer> (*controller, options);

                if (controlServer->start (error))
                {
                    if (const auto parent = flag ("--parent-pid"); parent.isNotEmpty())
                        parentWatch = std::make_unique<control::ParentWatch> ((std::uint32_t) parent.getLargeIntValue(), [this] { quit(); });

                    return;
                }
            }

            if (options.controlFile != juce::File())
                options.controlFile.getSiblingFile (options.controlFile.getFileName() + ".erro.txt").replaceWithText (error);

            controlServer = nullptr;
            setApplicationReturnValue (2);
            quit();
            return;
        }

        // a logo enquanto o motor, os plugins e o piano carregam; as capturas não mostram
        const bool capture = args[0].startsWith ("--screenshot");

        if (! capture)
            splash = SplashWindow::showNow();

        controller = std::make_unique<AppController> (false);
        logAudioDevice();

        // duplo clique num .wnl: o Windows passa o caminho como primeiro argumento
        if (arg (0).endsWithIgnoreCase (".wnl"))
            openFromCommandLine (juce::File (arg (0)));
        else if (args[0] == "--control" && arg (2).endsWithIgnoreCase (".wnl"))
            openFromCommandLine (juce::File (arg (2)));

        if (args[0] == "--screenshot" || args[0] == "--screenshot-fx" || args[0] == "--screenshot-synth")
        {
            screenshotFile = juce::File (arg (1));

            if (args.size() > 2)
                controller->openProject (juce::File (arg (2)));
        }

        // tema de cores: o que o usuário escolheu; as capturas usam o padrão, ou o --tema=<nome> pedido
        theme::applyScheme (theme::schemeFromKey (themeArg).value_or (capture ? theme::ColourScheme::whynot : theme::savedScheme()));

        mainWindow = std::make_unique<MainWindow> (*controller);
        mainWindow->setBackgroundColour (theme::colour::background);

        if (splash != nullptr)
            splash->fadeOut ([this] { splash = nullptr; });

        if (const auto portText = flag ("--control"); portText.isNotEmpty())
        {
            // a ponte da IA com a janela aberta (Fase 4): só liga com --control
            control::ControlServer::Options options;
            options.port = portText.getIntValue();

            // a confirmação aparece nesta janela (seção 5.5); a ponte fecha a pergunta no prazo ou se a IA sair
            options.askUser = [this] (const control::Confirmation& c, std::function<void (bool)> answer)
            {
                if (mainWindow == nullptr)
                    return answer (false);

                mainWindow->getContent().askAi (c.title, c.command, c.reason, control::ActivityLog::shortParams (c.params, 110),
                                                c.seconds, std::move (answer));
            };
            options.onAskCancelled = [this]
            {
                if (mainWindow != nullptr)
                    mainWindow->getContent().cancelAiQuestion();
            };

            // a captura para a IA (fatia 4.6): a tela que você vê (com `view` e `channel`, você vê a troca de tela e de canal)
            options.capture = [this] (const juce::String& view, juce::String& channel, int maxWidth, const juce::File& png, juce::String& why)
            {
                if (mainWindow == nullptr)
                {
                    why = "A janela já fechou."_t;
                    return juce::Rectangle<int>();
                }

                return mainWindow->getContent().capturePng (view, channel, maxWidth, png, why);
            };

            controlServer = std::make_unique<control::ControlServer> (*controller, options);
            juce::String error;

            if (portText.containsOnly ("0123456789") && controlServer->start (error))
            {
                // o aviso "IA conectada" e o "Desligar a IA" no pé (seção 5.7)
                mainWindow->getContent().setAiHooks ({ [this] { return controlServer != nullptr && controlServer->hasClient(); },
                                                       [this] { return controlServer == nullptr || controlServer->isEnabled(); },
                                                       [this] { return controlServer != nullptr && controlServer->isAwaitingConfirmation(); },
                                                       [this] (bool on) { if (controlServer != nullptr) controlServer->setEnabled (on); } });
            }
            else
            {
                controlServer = nullptr;
                juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                .withTitle ("Ponte da IA"_t)
                                                .withMessage (error.isNotEmpty() ? error : "Porta inválida em --control."_t)
                                                .withButton ("OK"),
                                              std::function<void (int)>());
            }
        }

        if (args[0] == "--screenshot" && args.size() > 3)
            mainWindow->showView (arg (3));

        if (args[0] == "--screenshot-fx")
        {
            // --screenshot-fx <saida.png> <projeto.wnl> <trilha> <slot 1-10>
            const auto track = arg (3);
            const int slot = arg (4).getIntValue() - 1;

            juce::Timer::callAfterDelay (1500, [this, track, slot]
            {
                const bool ok = mainWindow->saveFxScreenshot (screenshotFile, track, slot);
                setApplicationReturnValue (ok ? 0 : 1);
                quit();
            });
        }
        else if (args[0] == "--screenshot-synth")
        {
            // --screenshot-synth <saida.png> <projeto.wnl> <canal> [página 1-4] [camada 1-5]
            const auto channelName = arg (3);
            const int page = juce::jmax (1, args.size() > 4 ? arg (4).getIntValue() : 1) - 1;
            const int layer = juce::jmax (1, args.size() > 5 ? arg (5).getIntValue() : 1) - 1;

            juce::Timer::callAfterDelay (1500, [this, channelName, page, layer]
            {
                const bool ok = mainWindow->saveSynthScreenshot (screenshotFile, channelName, page, layer);
                setApplicationReturnValue (ok ? 0 : 1);
                quit();
            });
        }
        else if (screenshotFile != juce::File())
            juce::Timer::callAfterDelay (1500, [this]
            {
                // o tom do beat roda em segundo plano: a captura espera ele (até 10 s), com o aviso e as dicas na tela
                controller->finishBeatAnalysis (10000);
                const bool ok = mainWindow->saveScreenshot (screenshotFile);
                setApplicationReturnValue (ok ? 0 : 1);
                quit();
            });
    }

    void shutdown() override
    {
        splash = nullptr;
        parentWatch = nullptr;
        controlServer = nullptr;   // a ponte sai antes da janela e do motor
        mainWindow = nullptr;
        controller = nullptr;

        // o log fecha por último: o que o motor disser ao desligar ainda entra
        whynot::log ("Sessão encerrada pelo Sair"_t);
        juce::Logger::setCurrentLogger (nullptr);
        logBridge = nullptr;
        sessionlog::stop();
    }

    void systemRequestedQuit() override
    {
        // Fechar no meio da exportação cancela o render (caça de bugs do produtor, 21/10): pergunta antes.
        if (mainWindow != nullptr && controller != nullptr && controller->isExportingInBackground()
            && ! juce::NativeMessageBox::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Exportação em andamento"_t,
                                                          "A exportação ainda não terminou. Se sair agora, ela é cancelada"_t
                                                              + " e o arquivo pela metade é apagado."_t,
                                                          mainWindow.get(), nullptr))
            return;

        if (mainWindow == nullptr || mainWindow->confirmDiscardChanges())
            quit();
    }

private:
    /** Instala o registro de queda e, com janela ou com a ponte da IA, abre o log da sessão.
        O autoteste, o --render e as capturas não escrevem log (não deixam marcas na pasta de dados);
        o registro de queda vale para todos, e o do autoteste fica ao lado do relatório dele. */
    void startCrashReport (const juce::StringArray& args, const juce::String& dataArg)
    {
        const auto mode = args[0];
        const auto data = mode == "--headless" && dataArg.isNotEmpty()
                            ? juce::File (dataArg)
                            : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("WHYNOT LAB");
        auto crashes = data.getChildFile ("quedas");

        if (mode == "--self-test" && juce::File::isAbsolutePath (args[1].unquoted()))
            crashes = juce::File (args[1].unquoted()).getParentDirectory().getChildFile ("quedas");

        CrashReport::install ({ toPath (crashes), getApplicationVersion().toStdString(), 5 });

        if (mode == "--self-test" || mode == "--render" || mode.startsWith ("--screenshot"))
            return;

        if (! sessionlog::start (toPath (data.getChildFile ("logs"))))
            return;

        CrashReport::setLogFile (sessionlog::currentFile());   // numa queda, a última linha do log diz onde foi
        logBridge = std::make_unique<LogBridge>();
        juce::Logger::setCurrentLogger (logBridge.get());

        whynot::log ("WHYNOT LAB "_t + getApplicationVersion() + " - sessão iniciada, pid "_t + juce::String (Session::currentPid()));
        whynot::log ("Linha de comando: "_t + (args.isEmpty() ? juce::String ("(vazia)") : args.joinIntoString (" ")));
        whynot::log ("Sistema: "_t + juce::SystemStats::getOperatingSystemName() + ", " + juce::SystemStats::getCpuModel()
                       + ", " + juce::String (juce::SystemStats::getNumCpus()) + " núcleos, "_t
                       + juce::String (juce::SystemStats::getMemorySizeInMegabytes()) + " MB");
    }

    /** O aparelho de áudio que o motor abriu ao nascer (as trocas depois disso são da janela de áudio, F10). */
    void logAudioDevice()
    {
        if (! sessionlog::isOpen() || controller == nullptr)
            return;

        if (auto* device = controller->getEngine().getDeviceManager().deviceManager.getCurrentAudioDevice())
            whynot::log ("Áudio: "_t + device->getTypeName() + " / " + device->getName() + ", "
                           + juce::String (device->getCurrentSampleRate(), 0) + " Hz, buffer de "_t
                           + juce::String (device->getCurrentBufferSizeSamples()) + " amostras");
        else
            whynot::log ("Áudio: nenhum aparelho aberto"_t);
    }

    void openFromCommandLine (const juce::File& file)
    {
        const auto result = controller->openProject (file);
        whynot::log (result.wasOk() ? "Projeto aberto pela linha de comando: "_t + file.getFullPathName()
                                    : "O projeto da linha de comando não abriu: "_t + file.getFullPathName()
                                        + " (" + result.getErrorMessage() + ")");
    }

    std::unique_ptr<AppController> controller;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<SplashWindow> splash;
    std::unique_ptr<control::ControlServer> controlServer;   // Fase 4: só com --control
    std::unique_ptr<control::ParentWatch> parentWatch;       // --headless --parent-pid
    std::unique_ptr<juce::Logger> logBridge;                 // juce::Logger -> log da sessão
    juce::File screenshotFile;
};

} // namespace whynot

START_JUCE_APPLICATION (whynot::WhynotApplication)
