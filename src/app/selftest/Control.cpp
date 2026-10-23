// Autoteste da ponte de controle da IA (Fase 4, fatias 4.2 e 4.5): o WebSocket de verdade, com um cliente pequeno
// que roda a fila da thread de mensagem enquanto espera as respostas; a confirmação (com o ticket do cliente de IA
// e com a pergunta na janela), o ia.log e o "Desligar a IA". Ver docs/fase-4-plano.md, seções 5.5, 5.7 e 6.

#include "SelfTestKit.h"

#include "../../control/AudioPreview.h"
#include "../../control/CommandGate.h"
#include "../../control/CommandPolicy.h"
#include "../../control/ControlLog.h"
#include "../../control/ControlPlatform.h"
#include "../../control/ControlServer.h"
#include "../../control/WsProtocol.h"
#include "../../model/WnlFormat.h"
#include "../../ui/AiBridgeUi.h"
#include "../../ui/MainComponent.h"
#include "../../ui/Theme.h"

#include <map>
#include <optional>

namespace whynot::selftest
{

namespace
{
    namespace ws = control::ws;

    juce::String upgradeRequest (int port, const juce::String& token, const juce::String& extraHeaders = {}, const juce::String& host = {})
    {
        return "GET / HTTP/1.1\r\n"
               "Host: " + (host.isEmpty() ? "127.0.0.1:" + juce::String (port) : host) + "\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
               "Sec-WebSocket-Version: 13\r\n"
               + (token.isEmpty() ? juce::String() : "Authorization: Bearer " + token + "\r\n")
               + extraHeaders + "\r\n";
    }

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** Cliente de WebSocket do teste. Enquanto espera, roda a thread de mensagem, onde a ponte executa os pedidos. */
    struct TestClient
    {
        juce::StreamingSocket socket;
        ws::Decoder decoder { 32 * 1024 * 1024, false };
        int nextId = 1000;
        int stray = 0;               // mensagens que chegaram sem ninguém esperar
        bool closed = false;
        std::uint16_t closeCode = 0;

        bool connect (int port)   { return socket.connect ("127.0.0.1", port, 2000); }

        // o write do JUCE é um send() só, que pode mandar menos: repete até ir tudo
        void write (const void* data, std::size_t size)
        {
            const auto* p = static_cast<const char*> (data);

            while (size > 0)
            {
                const int sent = socket.write (p, (int) juce::jmin<std::size_t> (size, 64 * 1024));

                if (sent <= 0)
                    return;

                p += sent;
                size -= (std::size_t) sent;
            }
        }

        void write (const std::string& bytes)                   { write (bytes.data(), bytes.size()); }
        void write (const std::vector<std::uint8_t>& bytes)     { write (bytes.data(), bytes.size()); }

        /** Manda o aperto de mão e devolve a primeira linha da resposta ("HTTP/1.1 101 ..."). */
        juce::String handshake (const juce::String& request, const std::string& glued = {})
        {
            write (request.toStdString() + glued);
            std::string head;
            const auto end = juce::Time::getMillisecondCounter() + 4000;

            while (juce::Time::getMillisecondCounter() < end)
            {
                if (socket.waitUntilReady (true, 20) != 1)
                    continue;

                char chunk[2048];
                const int n = socket.read (chunk, (int) sizeof (chunk), false);

                if (n <= 0)
                    break;

                head.append (chunk, (std::size_t) n);
                const auto at = head.find ("\r\n\r\n");

                if (at != std::string::npos)
                {
                    if (head.compare (0, 12, "HTTP/1.1 101") == 0 && head.size() > at + 4)
                        decoder.feed (head.data() + at + 4, head.size() - at - 4);

                    head.resize (at);
                    break;
                }
            }

            return juce::String::fromUTF8 (head.data(), (int) head.size()).upToFirstOccurrenceOf ("\r\n", false, false);
        }

        void sendText (const juce::String& text)   { write (ws::encodeClientFrame (ws::Opcode::text, text.toStdString(), { 9, 8, 7, 6 })); }

        int send (const juce::String& method, const juce::var& params = {})
        {
            const int id = ++nextId;
            auto* o = new juce::DynamicObject();
            o->setProperty ("jsonrpc", "2.0");
            o->setProperty ("id", id);
            o->setProperty ("method", method);

            if (! params.isVoid())
                o->setProperty ("params", params);

            sendText (juce::JSON::toString (juce::var (o), true));
            return id;
        }

        /** A próxima mensagem do servidor, ou nada no tempo. */
        std::optional<ws::Message> receive (int timeoutMs)
        {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);

            for (;;)
            {
                ws::Message m;
                const auto status = decoder.next (m);

                if (status == ws::Decoder::Status::message)
                {
                    if (m.kind == ws::Message::Kind::close)
                    {
                        closed = true;
                        closeCode = m.code;
                    }

                    return m;
                }

                if (status == ws::Decoder::Status::error || closed || juce::Time::getMillisecondCounter() >= end)
                    return std::nullopt;

                pump (2);

                if (socket.waitUntilReady (true, 3) == 1)
                {
                    char chunk[65536];
                    const int n = socket.read (chunk, (int) sizeof (chunk), false);

                    if (n <= 0)
                    {
                        closed = true;
                        return std::nullopt;
                    }

                    decoder.feed (chunk, (std::size_t) n);
                }
            }
        }

        /** A resposta com esse id (com result ou error), ou var() se não veio a tempo. */
        juce::var waitFor (int id, int timeoutMs = 8000)
        {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

            while (juce::Time::getMillisecondCounter() < end)
            {
                const auto m = receive ((int) (end - juce::Time::getMillisecondCounter()));

                if (! m)
                    return {};

                if (m->kind != ws::Message::Kind::text)
                    continue;

                const auto v = juce::JSON::parse (juce::String::fromUTF8 (m->payload.data(), (int) m->payload.size()));

                if (v.hasProperty ("id") && v["id"] == juce::var (id))
                    return v;

                ++stray;
            }

            return {};
        }

        juce::var call (const juce::String& method, const juce::var& params = {}, int timeoutMs = 8000)
        {
            return waitFor (send (method, params), timeoutMs);
        }

        /** command.run: devolve o "result". */
        juce::var run (const juce::String& id, const juce::var& params = {})
        {
            return call ("command.run", args ({ { "id", id }, { "params", params.isVoid() ? juce::var (new juce::DynamicObject()) : params } }))["result"];
        }

        /** command.run com o ticket de uma confirmação dada no cliente de IA (sem janela). */
        juce::var runConfirmed (const juce::String& id, const juce::var& params, const juce::String& ticket)
        {
            return call ("command.run", args ({ { "id", id }, { "params", params }, { "confirm", ticket } }))["result"];
        }

        juce::var batch (const juce::String& title, const juce::Array<juce::var>& steps)
        {
            return call ("command.batch", args ({ { "title", title }, { "steps", steps } }))["result"];
        }

        /** Espera o close do servidor; devolve o código (0 = não veio). */
        std::uint16_t waitClose (int timeoutMs)
        {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

            while (! closed && juce::Time::getMillisecondCounter() < end)
                if (! receive ((int) (end - juce::Time::getMillisecondCounter())))
                    break;

            return closeCode;
        }
    };

    juce::var step (const juce::String& id, const juce::var& params)   { return args ({ { "id", id }, { "params", params } }); }

    bool connectAi (TestClient& c, int port, const juce::String& token)
    {
        return c.connect (port) && c.handshake (upgradeRequest (port, token)).startsWith ("HTTP/1.1 101");
    }

    bool isFailure (const juce::var& result)   { return result.isObject() && ! (bool) result["ok"]; }

    juce::var parse (const ws::Message& m)     { return juce::JSON::parse (juce::String::fromUTF8 (m.payload.data(), (int) m.payload.size())); }

    bool savePng (juce::Component& c, const juce::File& file)
    {
        const auto image = juce::SoftwareImageType().convert (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f));
        file.deleteFile();

        if (juce::FileOutputStream out (file); out.openedOk())
        {
            juce::PNGImageFormat png;
            return png.writeImageToStream (image, out) && image.getWidth() > 0;
        }

        return false;
    }

    void runConfirmationInWindow (Report& rep, AppController& app, const juce::File& folder, const juce::File& existing);
    void runAiScreens (Report& rep, const juce::File& work);
    void runAudioMeasures (Report& rep, const juce::File& folder);
}

void runControlSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;

    rep.section ("Ponte de controle da IA (Fase 4)");
    app.run ("project.new");

    const auto folder = ctx.work.getChildFile ("ponte");
    folder.createDirectory();

    control::ControlServer::Options options;
    options.port = 0;
    options.controlFile = folder.getChildFile ("control.json");
    options.scratchFolder = folder.getChildFile ("ia");
    options.requestsPerSecond = 1000.0;   // o limite de pedidos tem teste próprio (whynot_control_tests)
    options.requestBurst = 1000.0;
    options.gestureWaitMs = 1500;
    options.logFile = folder.getChildFile ("ia.log");
    options.confirmSeconds = 3;   // o prazo do ticket tem teste: 3 s em vez de 2 min

    auto server = std::make_unique<control::ControlServer> (app, options);
    juce::String error;
    const bool started = server->start (error);
    const int port = server->getPort();
    rep.check (started && port > 0, "a ponte abre em 127.0.0.1, numa porta escolhida pelo sistema ("_t + juce::String (port) + ")"
                                      + (started ? juce::String() : ": " + error));

    if (! started)
        return;

    const auto token = server->getToken();
    const auto controlJson = juce::JSON::parse (options.controlFile.loadFileAsString());
    rep.check ((int) controlJson["port"] == port && controlJson["token"].toString() == token && token.length() == 43
                 && (bool) controlJson["headless"] && (juce::int64) controlJson["pid"] == (juce::int64) control::currentProcessId(),
               "o arquivo de controle tem a porta, o token de 43 caracteres, o pid e o modo sem janela");

    //==========================================================================
    // Quem pode ligar
    auto statusOf = [port] (const juce::String& request)
    {
        TestClient c;
        return c.connect (port) ? c.handshake (request) : juce::String ("sem conexão"_t);
    };

    rep.check (statusOf (upgradeRequest (port, {})).startsWith ("HTTP/1.1 401"), "sem token: 401");
    rep.check (statusOf (upgradeRequest (port, "tokenErrado")).startsWith ("HTTP/1.1 401"), "token errado: 401 (depois de 1 s)");
    rep.check (statusOf (upgradeRequest (port, token, "Origin: https://site.example\r\n")).startsWith ("HTTP/1.1 403"),
               "pedido com Origin, como o de uma página no navegador: 403"_t);
    rep.check (statusOf (upgradeRequest (port, token, {}, "evil.example:" + juce::String (port))).startsWith ("HTTP/1.1 403"),
               "Host de outro nome (DNS rebinding): 403");

    {
        juce::String lan;

        for (auto& a : juce::IPAddress::getAllAddresses (false))
        {
            const int first = a.toString().upToFirstOccurrenceOf (".", false, false).getIntValue();

            // um endereço de verdade da máquina: nem 127, nem 0, nem link-local, nem multicast (224 a 239)
            if (a != juce::IPAddress::local() && ! a.isNull() && first != 127 && first != 0 && first < 224
                  && ! a.toString().startsWith ("169.254."))
            {
                lan = a.toString();
                break;
            }
        }

        if (lan.isEmpty())
        {
            rep.lines.add ("  (sem endereço de rede local aqui: o teste pelo IP da rede foi pulado)"_t);
        }
        else
        {
            juce::StreamingSocket s;
            rep.check (! s.connect (lan, port, 1000), "pelo IP da rede local ("_t + lan + "), a ponte não aceita conexão"_t);
        }
    }

    TestClient ai;

    if (! connectAi (ai, port, token))
    {
        rep.check (false, "com o token certo: 101, o WebSocket abre"_t);
        return;
    }

    rep.check (true, "com o token certo: 101, o WebSocket abre"_t);

    //==========================================================================
    // Métodos
    const auto hi = ai.call ("hello", args ({ { "protocol", 1 } }));
    rep.check ((int) hi["result"]["protocol"] == 1 && (bool) hi["result"]["headless"] && (int) hi["result"]["schemaVersion"] == wnl::currentSchemaVersion
                 && hi["result"]["scratch"].toString() == options.scratchFolder.getFullPathName()
                 && hi["result"]["confirmations"].toString() == "client",
               "hello: protocolo 1, sem janela, esquema "_t + juce::String (wnl::currentSchemaVersion)
                   + ", a pasta da IA e a confirmação pelo cliente de IA (sem janela)"_t);
    rep.check ((int) ai.call ("hello", args ({ { "protocol", 2 } }))["error"]["code"] == -32602, "hello pedindo outro protocolo: recusado");

    {
        const auto list = ai.call ("command.list");
        const auto registered = app.getCommands().describe();
        const auto* commands = list["result"]["commands"].getArray();
        const auto* all = registered.getArray();
        juce::StringArray unclassified;
        bool fields = commands != nullptr;

        if (all != nullptr)
            for (auto& c : *all)
                if (control::findRule (c["id"].toString().toStdString()) == nullptr)
                    unclassified.add (c["id"].toString());

        if (commands != nullptr)
            for (auto& c : *commands)
                fields = fields && c["risk"].toString().isNotEmpty() && c.hasProperty ("params") && c["domain"].toString().isNotEmpty();

        rep.check (commands != nullptr && all != nullptr && commands->size() == all->size(),
                   "command.list traz os "_t + juce::String (all != nullptr ? all->size() : 0) + " comandos do registro");
        rep.check (fields, "cada comando vem com o schema, o domínio e a classe de risco"_t);
        rep.check (unclassified.isEmpty(), "todo comando registrado tem classe de risco para a IA"_t
                                            + (unclassified.isEmpty() ? juce::String() : " (faltam: " + unclassified.joinIntoString (", ") + ")"));

        const auto transport = ai.call ("command.list", args ({ { "domain", "transport" } }));
        bool onlyTransport = transport["result"]["commands"].getArray() != nullptr && transport["result"]["commands"].getArray()->size() > 0;

        if (auto* t = transport["result"]["commands"].getArray())
            for (auto& c : *t)
                onlyTransport = onlyTransport && c["id"].toString().startsWith ("transport.");

        rep.check (onlyTransport, "command.list { domain: transport } traz só o transporte"_t);
    }

    //==========================================================================
    // Comandos e desfazer
    const auto bpmTitle = app.getCommands().find ("transport.setBpm")->title;
    const auto r1 = ai.run ("transport.setBpm", args ({ { "bpm", 150 } }));
    rep.check ((bool) r1["ok"] && std::abs (model.get().bpm - 150.0) < 1.0e-9 && r1["undo"].toString() == "IA: " + bpmTitle,
               "command.run muda o BPM, num passo de desfazer chamado \"IA: "_t + bpmTitle + "\"");

    const auto u1 = ai.run ("edit.undo");
    rep.check ((bool) u1["ok"] && std::abs (model.get().bpm - 140.0) < 1.0e-9, "edit.undo pela ponte desfaz o passo da IA"_t);

    app.run ("transport.setBpm", args ({ { "bpm", 120 } }));   // você, pela tela
    const auto u2 = ai.run ("edit.undo");
    rep.check (isFailure (u2) && std::abs (model.get().bpm - 120.0) < 1.0e-9 && u2["error"].toString().contains ("feito por você"_t),
               "com um passo seu no topo, a IA não desfaz"_t);

    rep.check (isFailure (ai.run ("comando.inventado")), "comando desconhecido: ok false, com a explicação"_t);

    const auto blocked = ai.run ("audio.setDevice", args ({ { "type", "ASIO" } }));
    rep.check (isFailure (blocked) && (bool) blocked["refused"] && blocked["error"].toString().contains ("pela tela"),
               "comando bloqueado (placa de som): recusado, dizendo que é só pela tela"_t);

    const auto channelsBefore = model.get().channels.size();
    const auto plugin = ai.run ("channel.addPlugin", args ({ { "plugin", "WHYNOT Teste Synth" } }));
    rep.check (isFailure (plugin) && plugin.hasProperty ("needsConfirmation") && model.get().channels.size() == channelsBefore
                 && plugin["needsConfirmation"]["ticket"].toString().length() == 43 && (int) plugin["needsConfirmation"]["expiresInSeconds"] == 3,
               "abrir um VST3 pede confirmação, não roda sem ela, e volta com um ticket para o cliente de IA perguntar"_t);

    // pastas: o kit está liberado para leitura; um arquivo de fora pede confirmação
    const auto kitSamples = app.getKitFolder().findChildFiles (juce::File::findFiles, false, "*.wav");

    if (kitSamples.isEmpty())
    {
        rep.check (false, "achei um sample no kit para o teste das pastas"_t);
    }
    else
    {
        const auto inKit = ai.run ("channel.addSampler", args ({ { "path", kitSamples[0].getFullPathName() } }));
        rep.check ((bool) inKit["ok"] && model.get().channels.size() == channelsBefore + 1, "sample do kit (pasta permitida): o canal entra"_t);

        const auto outside = folder.getChildFile ("fora.wav");
        kitSamples[0].copyFileTo (outside);
        const auto outsideRun = ai.run ("channel.addSampler", args ({ { "path", outside.getFullPathName() } }));
        rep.check (isFailure (outsideRun) && outsideRun.hasProperty ("needsConfirmation") && outsideRun["error"].toString().contains ("fora das pastas"),
                   "sample fora das pastas permitidas: pede confirmação"_t);
    }

    rep.check ((bool) ai.run ("channel.addSampler", args ({ { "path", "relativo.wav" } }))["refused"], "caminho relativo: recusado"_t);

    const auto preview = options.scratchFolder.getChildFile ("previa-teste.wav");
    const auto exported = ai.run ("project.exportWav", args ({ { "path", preview.getFullPathName() }, { "loops", 1 } }));
    rep.check ((bool) exported["ok"] && preview.existsAsFile(), "export na pasta da IA: grava"_t);

    const auto exportArgs = args ({ { "path", preview.getFullPathName() }, { "loops", 1 } });
    const auto again = ai.run ("project.exportWav", exportArgs);
    rep.check (isFailure (again) && again["error"].toString().contains ("por cima"), "export por cima de um arquivo que existe: pede confirmação"_t);

    //==========================================================================
    // A confirmação sem janela (seção 5.5): o ticket é do app, de uso único, preso ao comando e aos parâmetros
    {
        const auto ticket = again["needsConfirmation"]["ticket"].toString();
        preview.setLastModificationTime (juce::Time (2020, 0, 1, 12, 0));
        const auto written = [&preview] { return preview.getLastModificationTime().getYear() > 2020; };

        const auto other = ai.runConfirmed ("project.exportWav", args ({ { "path", preview.getFullPathName() }, { "loops", 2 } }), ticket);
        rep.check (isFailure (other) && (bool) other["refused"] && other["error"].toString().contains ("não vale"_t) && ! written(),
                   "o ticket com outros parâmetros (2 passadas em vez de 1): recusado, e o arquivo fica como estava"_t);

        const auto burned = ai.runConfirmed ("project.exportWav", exportArgs, ticket);
        rep.check (isFailure (burned) && ! written(), "o mesmo ticket depois disso, já com os parâmetros certos: recusado (uso único)"_t);

        const auto fresh = ai.run ("project.exportWav", exportArgs)["needsConfirmation"]["ticket"].toString();
        const auto wrongCommand = ai.runConfirmed ("channel.addPlugin", args ({ { "plugin", "WHYNOT Teste Synth" } }), fresh);
        rep.check (isFailure (wrongCommand) && ! written(), "o ticket de um comando usado em outro: recusado"_t);

        const auto good = ai.run ("project.exportWav", exportArgs)["needsConfirmation"]["ticket"].toString();
        const auto confirmed = ai.runConfirmed ("project.exportWav", args ({ { "loops", 1 }, { "path", preview.getFullPathName() } }), good);
        rep.check ((bool) confirmed["ok"] && written(),
                   "o ticket certo (os mesmos parâmetros, mesmo em outra ordem): o export grava por cima"_t);

        const auto late = ai.run ("project.exportWav", exportArgs)["needsConfirmation"]["ticket"].toString();
        pump (3300);
        preview.setLastModificationTime (juce::Time (2020, 0, 1, 12, 0));
        const auto expired = ai.runConfirmed ("project.exportWav", exportArgs, late);
        rep.check (isFailure (expired) && ! written(), "o ticket depois do prazo (3 s neste teste, 2 min de verdade): recusado"_t);

        rep.check (isFailure (ai.runConfirmed ("project.exportWav", exportArgs, "inventado"))
                     && isFailure (ai.runConfirmed ("project.exportWav", exportArgs, juce::String::repeatedString ("A", 43))),
                   "ticket inventado: recusado"_t);
    }

    rep.check (model.dirty && ai.run ("project.new").hasProperty ("needsConfirmation"), "projeto novo com alterações não salvas: pede confirmação"_t);

    //==========================================================================
    // Lote
    {
        const auto before = wnl::toJson (model.get());
        const auto undoBefore = app.getCommands().undoDescription();
        const auto redoBefore = app.getCommands().redoDescription();

        // Aqui o JUCE guarda numa reserva o "IA: Mudar BPM" desfeito lá em cima (o seu passo veio por cima dele).
        // Um lote que falha não pode devolver essa reserva ao refazer: o Ctrl+Y aplicaria uma foto velha do projeto.
        const auto bad = ai.batch ("Lote que falha", { step ("transport.setBpm", args ({ { "bpm", 133 } })),
                                                        step ("pattern.rename", args ({ { "pattern", "pt_nao_existe" }, { "name", "Nada" } })),
                                                        step ("project.rename", args ({ { "name", "Lote da IA" } })) });
        rep.check (isFailure (bad) && bad["error"].toString().startsWith ("Passo 2 (pattern.rename)") && wnl::toJson (model.get()) == before
                     && app.getCommands().undoDescription() == undoBefore && app.getCommands().redoDescription() == redoBefore,
                   "lote com o 2º passo errado: nada fica no modelo, no desfazer nem no refazer"_t);

        // um passo que depende do anterior: o padrão novo vira o atual, e as notas entram nele
        const auto patternsBefore = model.get().patterns.size();
        const auto firstChannel = model.get().channels.front().id;
        const auto chained = ai.batch ("Padrão com notas", { step ("pattern.add", args ({ { "name", "Do lote" } })),
                                                             step ("pianoroll.addNotes", args ({ { "channel", firstChannel },
                                                                                                 { "notes", juce::Array<juce::var> { noteVar (0, 1, 60, false) } },
                                                                                                 { "unit", "beats" } })) });
        const auto* current = model.get().findPattern (model.get().currentPattern);
        rep.check ((bool) chained["ok"] && model.get().patterns.size() == patternsBefore + 1 && current != nullptr && current->name == "Do lote"
                     && current->notes.count (firstChannel) == 1 && current->notes.at (firstChannel).size() == 1,
                   "lote que cria um padrão e põe uma nota nele: o 2º passo acha o que o 1º criou"_t);
        app.getCommands().undo();
        rep.check (wnl::toJson (model.get()) == before, "um Ctrl+Z desfaz esse lote inteiro"_t);

        rep.check (isFailure (ai.batch ("Abrir", { step ("project.new", args ({})) })), "project.new num lote: recusado (limpa o desfazer)"_t);

        const auto good = ai.batch ("Dois passos", { step ("transport.setBpm", args ({ { "bpm", 133 } })),
                                                     step ("project.rename", args ({ { "name", "Lote da IA" } })) });
        rep.check ((bool) good["ok"] && std::abs (model.get().bpm - 133.0) < 1.0e-9 && model.get().name == "Lote da IA"
                     && app.getCommands().undoDescription() == "IA: Dois passos", "lote certo: um passo de desfazer só, \"IA: Dois passos\""_t);

        const auto undone = ai.run ("edit.undo");
        rep.check ((bool) undone["ok"] && wnl::toJson (model.get()) == before, "um edit.undo desfaz o lote inteiro"_t);

        const auto mixed = ai.batch ("Com play", { step ("transport.setBpm", args ({ { "bpm", 99 } })), step ("transport.play", args ({})) });
        rep.check (isFailure (mixed) && std::abs (model.get().bpm - 99.0) > 1.0e-9, "lote com comando que não muda o projeto (play): recusado inteiro"_t);

        const auto withBlocked = ai.batch ("Com placa", { step ("transport.setBpm", args ({ { "bpm", 99 } })), step ("audio.setDevice", args ({})) });
        rep.check (isFailure (withBlocked) && (int) withBlocked["failedStep"] == 2 && std::abs (model.get().bpm - 99.0) > 1.0e-9,
                   "lote com comando bloqueado: recusado, dizendo qual passo"_t);
    }

    //==========================================================================
    // O seu gesto vem primeiro
    {
        auto& registry = app.getCommands();
        registry.beginGesture ("Gesto do teste");
        const int waiting = ai.send ("command.run", args ({ { "id", "transport.setBpm" }, { "params", args ({ { "bpm", 111 } }) } }));
        const auto early = ai.waitFor (waiting, 400);
        const bool heldBack = early.isVoid() && std::abs (model.get().bpm - 111.0) > 1.0e-9;
        registry.endGesture();
        const auto late = ai.waitFor (waiting, 3000);
        rep.check (heldBack && (bool) late["result"]["ok"] && std::abs (model.get().bpm - 111.0) < 1.0e-9
                     && registry.undoDescription() == "IA: " + bpmTitle,
                   "com um gesto seu aberto, o comando da IA espera e roda depois, num passo próprio"_t);

        registry.beginGesture ("Gesto longo");
        const auto gaveUp = ai.call ("command.run", args ({ { "id", "transport.setBpm" }, { "params", args ({ { "bpm", 112 } }) } }), 4000);
        registry.endGesture();
        rep.check (isFailure (gaveUp["result"]) && gaveUp["result"]["error"].toString().contains ("editando") && std::abs (model.get().bpm - 112.0) > 1.0e-9,
                   "gesto que não acaba: o comando desiste depois do tempo e não roda"_t);
    }

    //==========================================================================
    // Ver o projeto
    {
        const auto inspect = ai.call ("project.inspect")["result"];
        rep.check (std::abs ((double) inspect["project"]["bpm"] - model.get().bpm) < 1.0e-9 && inspect["channels"].getArray() != nullptr
                     && inspect["channels"].getArray()->size() == (int) model.get().channels.size(),
                   "project.inspect devolve o .wnl em JSON (BPM e canais iguais aos do modelo)"_t);

        const auto onlyProject = ai.call ("project.inspect", args ({ { "sections", juce::Array<juce::var> { "project" } } }))["result"];
        rep.check (onlyProject.hasProperty ("project") && ! onlyProject.hasProperty ("channels"), "project.inspect { sections: [project] } traz só o cabeçalho"_t);

        const auto firstChannel = model.get().channels.front().id;
        const auto oneChannel = ai.call ("project.inspect", args ({ { "channel", firstChannel } }))["result"];
        rep.check (oneChannel["channels"].getArray() != nullptr && oneChannel["channels"].getArray()->size() == 1, "project.inspect { channel } traz um canal só"_t);

        auto copy = model.get();
        copy.channels.front().kind = "plugin";
        copy.channels.front().plugin.name = "Teste";
        copy.channels.front().plugin.state = juce::String::repeatedString ("QUJD", 2000);
        const auto stripped = control::inspectProject (copy, {}, {}, {});
        rep.check (stripped["channels"][0]["plugin"]["state"].toString().startsWith ("<estado:"), "o estado do plugin em base64 sai trocado pelo tamanho"_t);

        const auto status = ai.call ("app.status")["result"];
        rep.check ((bool) status["dirty"] && (bool) status["aiUndo"] && ! (bool) status["gestureOpen"], "app.status: alterado, o último passo é da IA, sem gesto aberto"_t);
    }

    //==========================================================================
    // Ouvir (fatia 4.6): a prévia na pasta da IA, com as medidas
    {
        // o kick do projeto novo e o canal do kit que a IA pôs lá em cima: o padrão tem som de certeza
        for (const auto& channel : { model.get().channels.front().id, model.get().channels.back().id })
            app.run ("steps.setGrid", args ({ { "channel", channel }, { "grid", "x...x...x...x..." } }));
        const auto preview = ai.call ("render.preview", args ({ { "what", "pattern" }, { "loops", 2 } }), 60000)["result"];
        const juce::File wav (preview["path"].toString());
        const auto* bars = preview["barsRmsDb"].getArray();
        const double barSeconds = 60.0 / model.get().bpm * model.get().timeSigNum;

        rep.check ((bool) preview["ok"] && wav.existsAsFile() && wav.isAChildOf (options.scratchFolder) && preview["what"].toString() == "pattern"
                     && (double) preview["seconds"] >= 2.0 * barSeconds - 0.05 && bars != nullptr && bars->size() >= 2
                     && std::abs ((double) preview["barSeconds"] - barSeconds) < 0.01 && (int) preview["channels"] == 2,
                   "render.preview: o padrão em 2 voltas, renderizado na pasta da IA, com um RMS por compasso ("_t
                       + preview["seconds"].toString() + " s, " + juce::String (bars != nullptr ? bars->size() : 0) + " compassos)");
        rep.check ((double) preview["peakDb"] > -40.0 && (double) preview["rmsDb"] < (double) preview["peakDb"]
                     && (double) preview["lufs"] > -60.0 && (int) preview["clipped"] == 0,
                   "as medidas do beat: pico "_t + preview["peakDb"].toString() + " dBFS, RMS " + preview["rmsDb"].toString()
                       + " dBFS, " + preview["lufs"].toString() + " LUFS, nada cortou");
        rep.check (isFailure (ai.call ("render.preview", args ({ { "what", "nada" } }))["result"]), "render.preview com what errado: recusado"_t);
    }

    //==========================================================================
    // Pedidos estragados
    {
        ai.sendText ("{nada");
        const auto parseError = ai.receive (3000);
        const auto pe = parseError ? juce::JSON::parse (juce::String::fromUTF8 (parseError->payload.data(), (int) parseError->payload.size())) : juce::var();
        rep.check ((int) pe["error"]["code"] == -32700 && pe["id"].isVoid(), "JSON inválido: -32700, e a conexão continua"_t);

        ai.sendText ("{\"jsonrpc\":\"2.0\",\"id\":7}");
        rep.check ((int) ai.waitFor (7)["error"]["code"] == -32600, "pedido sem method: -32600"_t);

        rep.check ((int) ai.call ("nada.disso")["error"]["code"] == -32601, "método que não existe: -32601"_t);
        rep.check ((int) ai.call ("command.run", args ({ { "id", "transport.setBpm" }, { "params", juce::Array<juce::var> { 150 } } }))["error"]["code"] == -32602
                     && (int) ai.call ("app.status", juce::Array<juce::var> { 1 })["error"]["code"] == -32602,
                   "params em forma de lista (no JUCE, uma lista também é objeto): -32602, sem derrubar a ponte"_t);
        rep.check (ai.call ("ui.screenshot")["result"]["error"].toString().contains ("não tem captura"_t),
                   "ui.screenshot sem o gancho da captura: recusado, com o motivo"_t);

        juce::String deep = "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"hello\",\"params\":{\"x\":";

        for (int i = 0; i < 70; ++i)
            deep << "[";

        for (int i = 0; i < 70; ++i)
            deep << "]";

        ai.sendText (deep + "}}");
        const auto deepReply = ai.receive (3000);
        const auto dr = deepReply ? juce::JSON::parse (juce::String::fromUTF8 (deepReply->payload.data(), (int) deepReply->payload.size())) : juce::var();
        rep.check ((int) dr["error"]["code"] == -32600, "JSON com 70 níveis: recusado antes de ler"_t);

        ai.stray = 0;
        ai.sendText ("{\"jsonrpc\":\"2.0\",\"method\":\"hello\"}");   // notificação: sem resposta
        const auto afterNotification = ai.call ("app.status");
        rep.check (afterNotification.hasProperty ("result") && ai.stray == 0, "notificação (sem id) não tem resposta"_t);

        // 40 pedidos de uma vez, com a thread de mensagem parada: a fila guarda 32, o resto ouve "ocupado"
        juce::Array<int> ids;

        for (int i = 0; i < 40; ++i)
            ids.add (ai.send ("app.status"));

        juce::Thread::sleep (400);
        std::map<int, juce::var> replies;   // os "ocupado" chegam antes das outras respostas: junta por id
        const auto end = juce::Time::getMillisecondCounter() + 8000;

        while ((int) replies.size() < ids.size() && juce::Time::getMillisecondCounter() < end)
        {
            const auto m = ai.receive ((int) (end - juce::Time::getMillisecondCounter()));

            if (! m)
                break;

            const auto v = juce::JSON::parse (juce::String::fromUTF8 (m->payload.data(), (int) m->payload.size()));

            if (ids.contains ((int) v["id"]))
                replies[(int) v["id"]] = v;
        }

        int busy = 0, answered = 0;

        for (auto& [id, reply] : replies)
        {
            if ((int) reply["error"]["code"] == -32000)
                ++busy;
            else if (reply.hasProperty ("result"))
                ++answered;
        }

        rep.check (busy > 0 && busy + answered == 40, "40 pedidos de uma vez: a fila guarda 32 e responde \"ocupado\" ao resto ("_t
                                                       + juce::String (answered) + " respondidos, " + juce::String (busy) + " ocupados)");
    }

    //==========================================================================
    // Conexões
    {
        bool gone = false, stillHere = true;
        control::ParentWatch alive (control::currentProcessId(), [&stillHere] { stillHere = false; });
        control::ParentWatch dead (0xFFFFFFF0u, [&gone] { gone = true; });
        pump (2500);
        rep.check (gone && stillHere, "ParentWatch: fecha quando o processo pai não existe, e não fecha com ele vivo"_t);

        TestClient second;
        const bool secondIn = connectAi (second, port, token);
        rep.check (secondIn && ai.waitClose (3000) == ws::closeCode::policy && second.call ("hello").hasProperty ("result"),
                   "uma conexão nova com o token toma o lugar da antiga (close 1008)"_t);

        second.write (ws::encodeClientFrame (ws::Opcode::text, std::string (2 * 1024 * 1024, 'x'), { 1, 2, 3, 4 }));
        rep.check (second.waitClose (4000) == ws::closeCode::tooBig, "mensagem de 2 MB: a ponte fecha com 1009"_t);

        TestClient glued;
        const auto gluedFrame = ws::encodeClientFrame (ws::Opcode::text, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"hello\"}", { 1, 2, 3, 4 });
        const bool gluedIn = glued.connect (port) && glued.handshake (upgradeRequest (port, token), std::string (gluedFrame.begin(), gluedFrame.end())).startsWith ("HTTP/1.1 101");
        rep.check (gluedIn && glued.waitFor (5, 600).isVoid() && glued.call ("hello").hasProperty ("result"),
                   "quadro colado no pedido HTTP, antes do 101: ignorado"_t);

        glued.write (ws::encodeClientFrame (ws::Opcode::ping, "oi", { 1, 2, 3, 4 }));
        const auto pong = glued.receive (2000);
        rep.check (pong && pong->kind == ws::Message::Kind::pong && pong->payload == "oi", "ping: a ponte responde pong com os mesmos dados"_t);

        // 4 conexões paradas, sem pedido: a 5ª é fechada na hora; as paradas fecham sozinhas em 2 s
        std::vector<std::unique_ptr<juce::StreamingSocket>> idle;

        for (int i = 0; i < 4; ++i)
        {
            idle.push_back (std::make_unique<juce::StreamingSocket>());
            idle.back()->connect ("127.0.0.1", port, 1000);
        }

        pump (200);
        juce::StreamingSocket fifth;
        char byte = 0;
        const bool fifthClosed = fifth.connect ("127.0.0.1", port, 1000) && fifth.waitUntilReady (true, 1500) == 1 && fifth.read (&byte, 1, false) <= 0;
        juce::Thread::sleep (2500);
        bool idleClosed = true;

        for (auto& s : idle)
            idleClosed = idleClosed && s->waitUntilReady (true, 500) == 1 && s->read (&byte, 1, false) <= 0;

        rep.check (fifthClosed && idleClosed, "conexões sem token: no máximo 4 ao mesmo tempo, e cada uma fecha em 2 s sem pedido"_t);

        // cliente que cai no meio de um pedido demorado não derruba nada
        TestClient quitter;

        if (connectAi (quitter, port, token))
        {
            quitter.send ("command.run", args ({ { "id", "project.exportWav" },
                                                 { "params", args ({ { "path", options.scratchFolder.getChildFile ("cai.wav").getFullPathName() } }) } }));
            quitter.socket.close();
        }

        pump (3000);
        TestClient last;
        rep.check (connectAi (last, port, token) && last.call ("hello").hasProperty ("result"), "cliente que cai no meio de um pedido: a ponte continua aceitando"_t);

        // "Desligar a IA" (seção 5.7): fecha a conexão e recusa as próximas até ligar de novo
        server->setEnabled (false);
        const auto offClose = last.waitClose (3000);
        pump (100);
        const auto offStatus = statusOf (upgradeRequest (port, token));
        rep.check (offClose == ws::closeCode::policy && offStatus.startsWith ("HTTP/1.1 503") && ! server->hasClient() && ! server->isEnabled(),
                   "Desligar a IA: a conexão fecha (1008), e a próxima, mesmo com o token, ouve 503 ("_t + offStatus + ")");

        server->setEnabled (true);
        TestClient back;
        rep.check (connectAi (back, port, token) && back.call ("hello").hasProperty ("result") && server->hasClient(),
                   "Ligar a IA de novo: a ponte volta a aceitar"_t);

        server->stop();
        rep.check (back.waitClose (3000) == ws::closeCode::goingAway && ! options.controlFile.existsAsFile(),
                   "ao fechar a ponte: close 1001 para o cliente e o arquivo de controle apagado"_t);
    }

    server = nullptr;

    //==========================================================================
    // O registro (seção 5.7)
    {
        const auto text = options.logFile.loadFileAsString();
        juce::StringArray lines;
        lines.addLines (text);
        lines.removeEmptyStrings();

        rep.check (text.contains ("command.run  transport.setBpm  ok  ") && text.contains ("{\"bpm\":150}"),
                   "ia.log: uma linha por pedido, com o método, o comando, o resultado, a duração e os parâmetros"_t);
        rep.check (text.contains ("pede confirmação ao cliente de IA"_t) && text.contains ("confirmado no cliente de IA; ok"_t)
                     && text.contains ("recusado: confirmação que não vale"_t),
                   "ia.log: o pedido de confirmação, a confirmada e as recusadas"_t);
        rep.check (text.contains ("-- a IA conectou") && text.contains ("-- a pessoa desligou a IA") && text.contains ("-- a pessoa ligou a IA de novo")
                     && text.contains ("-- ponte fechada"),
                   "ia.log: conectou, desligada, ligada de novo e a ponte fechada"_t);
        rep.check (! text.contains (token) && lines.size() > 20, "o token nunca vai para o ia.log ("_t + juce::String (lines.size()) + " linhas)");

        const auto longParams = args ({ { "name", juce::String::repeatedString ("x", 400) } });
        const auto line = control::ActivityLog::formatLine (juce::Time (2026, 9, 23, 10, 15, 2), "command.run", "project.rename", "ok", 12.4, longParams);
        rep.check (line.startsWith ("2026-10-23 10:15:02  command.run  project.rename  ok  12 ms  {\"name\":\"xxx")
                     && line.endsWith (juce::String::charToString ((juce::juce_wchar) 0x2026)) && line.length() < 270,
                   "a linha do registro corta os parâmetros em 200 caracteres, com \"...\""_t);

        // com 1 MB, vira ia.anterior.log e começa de novo
        const auto rotating = folder.getChildFile ("roda.log");
        rotating.replaceWithText (juce::String::repeatedString ("0123456789", (int) (control::ActivityLog::rotateBytes / 10) + 10));
        control::ActivityLog (rotating).event ("depois de 1 MB");
        rep.check (folder.getChildFile ("roda.anterior.log").getSize() > control::ActivityLog::rotateBytes && rotating.getSize() < 200,
                   "ia.log com mais de 1 MB: vira ia.anterior.log, e o novo começa vazio"_t);
    }

    runConfirmationInWindow (rep, app, folder, preview);
    runAiScreens (rep, ctx.work);
    runAudioMeasures (rep, folder);
}

namespace
{
    /** A confirmação com janela: um askUser de mentira faz o papel do cartão (o cartão de verdade tem teste próprio). */
    void runConfirmationInWindow (Report& rep, AppController& app, const juce::File& folder, const juce::File& existing)
    {
        control::Confirmation question;
        std::function<void (bool)> answer;
        int asked = 0, cancelledAsks = 0;

        control::ControlServer::Options options;
        options.port = 0;
        options.controlFile = folder.getChildFile ("control-janela.json");
        options.scratchFolder = folder.getChildFile ("ia");
        options.logFile = folder.getChildFile ("ia-janela.log");
        options.requestsPerSecond = 1000.0;
        options.requestBurst = 1000.0;
        options.confirmSeconds = 2;
        options.askUser = [&] (const control::Confirmation& c, std::function<void (bool)> a) { question = c; answer = std::move (a); ++asked; };
        options.capture = [&app] (const juce::String& view, int maxWidth, const juce::File& png, juce::String& why)
        {
            return MainComponent::captureOffscreen (app, view, maxWidth, png, why);   // o mesmo do --headless
        };
        options.onAskCancelled = [&] { ++cancelledAsks; };

        // a ponte limpa a pasta da IA ao abrir (arquivos de mais de 24 h): o arquivo do teste fica com a data de agora
        if (! existing.existsAsFile())
            existing.replaceWithText ("wav");

        existing.setLastModificationTime (juce::Time::getCurrentTime());

        // a resposta da "pessoa": só existe depois da pergunta (sem pergunta, o teste falha em vez de parar)
        auto reply = [&answer] (bool allowed) { if (auto a = answer) a (allowed); };

        auto server = std::make_unique<control::ControlServer> (app, options);
        juce::String error;

        if (! server->start (error))
        {
            rep.check (false, "a ponte com janela abre: "_t + error);
            return;
        }

        const int port = server->getPort();
        const auto token = server->getToken();
        TestClient ai;

        if (! connectAi (ai, port, token))
        {
            rep.check (false, "a IA liga na ponte com janela"_t);
            return;
        }

        rep.check (ai.call ("hello")["result"]["confirmations"].toString() == "window", "hello com janela: a confirmação é na janela da WHYNOT"_t);

        // a captura (fatia 4.6): aqui, a tela montada fora da área de trabalho, a mesma do --headless
        {
            const auto shot = ai.call ("ui.screenshot", args ({ { "view", "pianoRoll" }, { "width", 900 } }), 30000)["result"];
            const juce::File png (shot["path"].toString());
            const auto image = juce::ImageFileFormat::loadFrom (png);
            bool varied = false;

            if (image.isValid())
            {
                const auto first = image.getPixelAt (image.getWidth() / 2, image.getHeight() / 2);

                for (int y = 0; y < image.getHeight() && ! varied; y += 7)
                    for (int x = 0; x < image.getWidth() && ! varied; x += 7)
                        varied = image.getPixelAt (x, y) != first;
            }

            rep.check ((bool) shot["ok"] && png.isAChildOf (options.scratchFolder) && image.getWidth() == 900 && (int) shot["width"] == 900
                         && image.getHeight() > 400 && varied && (juce::int64) shot["bytes"] < 1024 * 1024,
                       "ui.screenshot { view: pianoRoll, width: 900 }: a tela fora da área de trabalho, em PNG, "_t
                           + shot["width"].toString() + "x" + shot["height"].toString() + ", " + juce::String ((int) shot["bytes"] / 1024) + " KB");
            png.copyFileTo (folder.getParentDirectory().getChildFile ("ia-tela-pianoroll.png"));

            rep.check (isFailure (ai.call ("ui.screenshot", args ({ { "view", "inventada" } }), 30000)["result"]),
                       "ui.screenshot com uma tela que não existe: recusado"_t);
        }

        const auto exportArgs = args ({ { "path", existing.getFullPathName() }, { "loops", 1 } });
        auto sendExport = [&] { return ai.send ("command.run", args ({ { "id", "project.exportWav" }, { "params", exportArgs } })); };
        auto waitAsked = [&] (int count)
        {
            const auto end = juce::Time::getMillisecondCounter() + 3000;

            while (asked < count && juce::Time::getMillisecondCounter() < end)
                pump (10);

            return asked == count;
        };

        // 1. a pergunta aparece, a IA fica sabendo que espera, e os outros pedidos esperam a vez
        const int first = sendExport();
        const auto waiting = ai.receive (3000);
        const auto note = waiting ? parse (*waiting) : juce::var();
        rep.check (waitAsked (1) && server->isAwaitingConfirmation() && question.command == "project.exportWav"
                     && question.reason.contains ("por cima") && question.seconds == 2
                     && question.params["path"].toString() == existing.getFullPathName(),
                   "com janela, gravar por cima pergunta na WHYNOT: o comando, o porquê e o caminho normalizado"_t);
        rep.check (note["method"].toString() == "request.waiting" && note["params"]["id"] == juce::var (first) && (int) note["params"]["seconds"] == 2,
                   "a IA recebe o aviso request.waiting (o servidor MCP estica a espera)"_t);

        const int status = ai.send ("app.status");
        rep.check (ai.waitFor (status, 300).isVoid() && server->isAwaitingConfirmation(), "enquanto a pergunta está aberta, o próximo pedido espera a vez"_t);

        existing.setLastModificationTime (juce::Time (2020, 0, 1, 12, 0));
        reply (true);
        const auto allowed = ai.waitFor (first, 8000)["result"];
        const auto afterStatus = ai.waitFor (status, 3000);
        rep.check ((bool) allowed["ok"] && existing.getLastModificationTime().getYear() > 2020 && afterStatus.hasProperty ("result")
                     && ! server->isAwaitingConfirmation() && cancelledAsks == 0,
                   "Permitir: o export grava por cima, e o pedido que esperava roda depois"_t);

        // 2. Recusar
        const int second = sendExport();
        ai.receive (2000);   // o request.waiting
        waitAsked (2);
        existing.setLastModificationTime (juce::Time (2020, 0, 1, 12, 0));
        reply (false);
        const auto declined = ai.waitFor (second, 3000)["result"];
        rep.check (isFailure (declined) && (bool) declined["declined"] && declined["error"].toString().contains ("recusou")
                     && existing.getLastModificationTime().getYear() == 2020,
                   "Recusar: a IA ouve que você recusou, e o arquivo fica como estava"_t);

        // 3. sem resposta no prazo
        const int third = sendExport();
        ai.receive (2000);
        waitAsked (3);
        const auto stale = answer;
        const auto timedOut = ai.waitFor (third, 5000)["result"];
        const int strayBefore = ai.stray;
        if (stale)
            stale (true);   // a resposta que chega depois do prazo não vale
        pump (300);
        rep.check (isFailure (timedOut) && timedOut["error"].toString().contains ("Sem resposta") && cancelledAsks == 1
                     && existing.getLastModificationTime().getYear() == 2020 && ai.waitFor (third, 300).isVoid() && ai.stray == strayBefore,
                   "sem resposta em 2 s (2 min de verdade): recusado, o cartão fecha, e um clique atrasado não faz nada"_t);

        // 4. a IA desiste ($/cancel) e 5. a IA cai
        const int fourth = sendExport();
        ai.receive (2000);
        waitAsked (4);
        ai.sendText ("{\"jsonrpc\":\"2.0\",\"method\":\"$/cancel\",\"params\":{\"id\":" + juce::String (fourth) + "}}");
        const auto gaveUp = ai.waitFor (fourth, 3000)["result"];
        rep.check (isFailure (gaveUp) && gaveUp["error"].toString().contains ("desistiu") && cancelledAsks == 2,
                   "a IA desiste do pedido ($/cancel): a pergunta fecha"_t);

        sendExport();
        ai.receive (2000);
        waitAsked (5);
        ai.socket.close();
        const auto end = juce::Time::getMillisecondCounter() + 3000;

        while (server->isAwaitingConfirmation() && juce::Time::getMillisecondCounter() < end)
            pump (20);

        rep.check (! server->isAwaitingConfirmation() && cancelledAsks == 3 && existing.getLastModificationTime().getYear() == 2020,
                   "a IA cai com a pergunta aberta: a pergunta fecha e nada roda"_t);

        // 6. Desligar a IA com a pergunta aberta
        TestClient again;

        if (connectAi (again, port, token))
        {
            again.send ("command.run", args ({ { "id", "project.exportWav" }, { "params", exportArgs } }));
            again.receive (2000);
            const auto end6 = juce::Time::getMillisecondCounter() + 3000;

            while (asked < 6 && juce::Time::getMillisecondCounter() < end6)
                pump (10);

            server->setEnabled (false);
            rep.check (asked == 6 && cancelledAsks == 4 && ! server->isAwaitingConfirmation() && again.waitClose (3000) == ws::closeCode::policy,
                       "Desligar a IA com a pergunta aberta: a pergunta fecha e a conexão também"_t);
        }

        server->stop();
        const auto log = options.logFile.loadFileAsString();
        rep.check (log.contains ("pergunta na janela") && log.contains ("permitido na janela; ok") && log.contains ("recusado na janela")
                     && log.contains ("sem resposta: Sem resposta"),
                   "ia.log com janela: a pergunta, a permitida, a recusada e a sem resposta"_t);
    }

    /** As medidas da prévia com sons conhecidos: um seno de 1 kHz a -6 dBFS e um com silêncio e corte. */
    void runAudioMeasures (Report& rep, const juce::File& folder)
    {
        auto write = [&folder] (const juce::String& name, double seconds, std::function<float (int, int)> sample)
        {
            const auto file = folder.getChildFile (name);
            file.deleteFile();
            constexpr double rate = 48000.0;
            const int length = (int) (seconds * rate);
            juce::AudioBuffer<float> buffer (2, length);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < length; ++i)
                    buffer.setSample (ch, i, sample (ch, i));

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
            auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));

            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (buffer, 0, length);

            return file;
        };

        const auto twoPi = juce::MathConstants<double>::twoPi;
        const auto sine = write ("seno-1khz.wav", 4.0, [twoPi] (int, int i) { return (float) (0.5 * std::sin (twoPi * 1000.0 * i / 48000.0)); });
        const auto m = control::measureAudio (sine, 120.0, 4);   // compasso de 2 s
        rep.check (m.ok && std::abs (m.peakDb + 6.02f) < 0.1f && std::abs (m.rmsDb + 9.03f) < 0.1f && m.clipped == 0
                     && m.silenceStart < 0.01 && m.silenceEnd < 0.01 && m.barsRmsDb.size() == 2 && std::abs (m.barsRmsDb[1] + 9.03f) < 0.1f,
                   "medidas de um seno de 1 kHz a 0,5: pico "_t + juce::String (m.peakDb, 2) + " dBFS (-6,02), RMS " + juce::String (m.rmsDb, 2)
                       + " dBFS (-9,03), 2 compassos de 2 s");
        rep.check (std::abs (m.lufs + 6.02f) < 0.5f && std::abs (m.truePeakDb + 6.02f) < 0.3f,
                   "LUFS do mesmo seno: "_t + juce::String (m.lufs, 2) + " (BS.1770: -6,0 nos dois canais), true peak "
                       + juce::String (m.truePeakDb, 2) + " dBTP");

        // meio segundo de silêncio, um seno a todo volume (corta) e um segundo de silêncio no fim
        const auto clipped = write ("corta.wav", 3.0, [twoPi] (int, int i)
        {
            const double t = i / 48000.0;
            return t < 0.5 || t >= 2.0 ? 0.0f : (float) juce::jlimit (-1.0, 1.0, 1.4 * std::sin (twoPi * 220.0 * t));
        });
        const auto c = control::measureAudio (clipped, 120.0, 4);
        rep.check (c.ok && c.clipped > 1000 && std::abs (c.silenceStart - 0.5) < 0.01 && std::abs (c.silenceEnd - 1.0) < 0.01 && c.peakDb > -0.1f,
                   "um som que corta, com silêncio: "_t + juce::String (c.clipped) + " amostras em 0 dBFS, "
                       + juce::String (c.silenceStart, 2) + " s mudo no começo e " + juce::String (c.silenceEnd, 2) + " s no fim");

        rep.check (! control::measureAudio (folder.getChildFile ("nao-existe.wav"), 120.0, 4).ok, "arquivo que não é áudio: as medidas dizem que não deu"_t);
    }

    /** O cartão da pergunta e o aviso do pé, fora da área de trabalho (sem roubar o foco), com a captura. */
    void runAiScreens (Report& rep, const juce::File& work)
    {
        juce::Component stage;
        stage.setSize (900, 420);
        stage.setOpaque (true);

        struct Background : juce::Component
        {
            void paint (juce::Graphics& g) override   { g.fillAll (theme::colour::panel); }
        } background;

        stage.addAndMakeVisible (background);
        background.setBounds (stage.getLocalBounds());

        int answers = 0;
        bool last = false;
        auto card = std::make_unique<AiConfirmCard> ("Adicionar plugin", "channel.addPlugin",
                                                     "Abre um plugin VST3 dentro do app (um plugin que cai derruba a WHYNOT inteira)."_t,
                                                     "{\"plugin\":\"Surge XT\"}", 120);
        card->onAnswer = [&] (bool allowed) { ++answers; last = allowed; };
        stage.addAndMakeVisible (*card);
        card->setBounds (AiConfirmCard::boundsIn (stage.getLocalBounds().withTrimmedTop (56)));

        card->answer (true);
        rep.check (answers == 0 && ! card->isArmed(), "o Permitir fica apagado no primeiro instante: um clique que já vinha não autoriza"_t);

        pump (AiConfirmCard::armMs + 300);
        rep.check (card->isArmed() && ! card->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)) && answers == 0,
                   "depois de 0,7 s o Permitir acende; Enter não autoriza"_t);

        rep.check (! card->hitTest (2, 2) && card->hitTest (card->getWidth() / 2, card->getHeight() / 2),
                   "a sombra do cartão não pega cliques; o cartão pega"_t);

        AiStatusNotice notice;
        stage.addAndMakeVisible (notice);
        notice.setState (AiStatusNotice::State::connected);
        notice.setBounds (stage.getWidth() - notice.getIdealWidth() - 16, stage.getHeight() - 28, notice.getIdealWidth(), 28);

        const auto png = work.getChildFile ("ia-confirmacao.png");
        rep.check (savePng (stage, png) && notice.isVisible(), "captura do cartão da IA e do aviso \"IA conectada\": "_t + png.getFullPathName());

        notice.setState (AiStatusNotice::State::off);
        const bool offVisible = notice.isVisible();
        notice.setState (AiStatusNotice::State::hidden);
        rep.check (offVisible && ! notice.isVisible(), "o aviso do pé: \"IA desligada\" aparece, e sem conexão ele some"_t);

        card->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        card->answer (true);
        rep.check (answers == 1 && ! last, "Esc recusa, e o cartão responde uma vez só"_t);
    }
}

} // namespace whynot::selftest
