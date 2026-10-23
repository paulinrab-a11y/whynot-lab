#include "ControlServer.h"

#include "AudioPreview.h"
#include "CommandGate.h"
#include "CommandPolicy.h"
#include "ControlPlatform.h"
#include "WsProtocol.h"
#include "../app/AppController.h"
#include "../common/SafeJson.h"
#include "../common/Text.h"
#include "../model/WnlFormat.h"

#include <algorithm>

namespace whynot::control
{

namespace
{
    const juce::String aiPrefix = "IA: ";

    double nowSeconds()   { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> fields)
    {
        auto* o = new juce::DynamicObject();

        for (auto& [key, value] : fields)
            o->setProperty (key, value);

        return juce::var (o);
    }

    juce::String rpcResult (const juce::var& id, const juce::var& result)
    {
        return juce::JSON::toString (object ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } }), true);
    }

    juce::String rpcError (const juce::var& id, int code, const juce::String& message)
    {
        return juce::JSON::toString (object ({ { "jsonrpc", "2.0" }, { "id", id },
                                               { "error", object ({ { "code", code }, { "message", message } }) } }), true);
    }

    juce::var failure (const juce::String& why)   { return object ({ { "ok", false }, { "error", why } }); }

    juce::String fromUtf8 (std::string_view s)    { return juce::String::fromUTF8 (s.data(), (int) s.size()); }

    juce::var describeResult (const CommandResult& result, AppController& app)
    {
        auto out = object ({ { "ok", result.ok } });
        auto* o = out.getDynamicObject();

        if (result.ok)
            o->setProperty ("value", result.value);
        else
            o->setProperty ("error", result.error);

        o->setProperty ("undo", app.getCommands().undoDescription());
        o->setProperty ("dirty", app.getModel().dirty);
        return out;
    }
}

//==============================================================================
/** Uma conexão: a thread dela faz o aperto de mão e depois lê os quadros; a `writer` manda as respostas.
    A thread de mensagem nunca toca no socket. */
struct ControlServer::Connection : juce::Thread
{
    Connection (ControlServer& s, std::unique_ptr<juce::StreamingSocket> sock, int connectionId)
        : juce::Thread ("WHYNOT ponte " + juce::String (connectionId)),
          server (s), socket (std::move (sock)), id (connectionId)
    {
    }

    ~Connection() override
    {
        writer.waitForThreadToExit (500);   // deixa o close sair
        signalThreadShouldExit();
        shutdownSocket();
        writer.stopThread (2000);
        stopThread (3000);
    }

    void run() override
    {
        if (handshake())
        {
            authenticated = true;
            server.promote (*this);
            writer.startThread();
            readLoop();

            // fechar com dados ainda não lidos faz o Windows mandar um RST, e o outro lado perde o close:
            // lê e descarta até ele fechar (ou 2 s)
            bool weClosed;

            {
                const juce::ScopedLock sl (outLock);
                weClosed = closeQueued;
            }

            if (weClosed)
                drain (2000);
        }

        {
            const juce::ScopedLock sl (outLock);
            closing = true;
        }

        outReady.signal();
        writer.waitForThreadToExit (1000);
        shutdownSocket();
        finished = true;

        if (authenticated)
            server.triggerAsyncUpdate();   // o aviso "IA conectada" e o registro ficam sabendo
    }

    //--------------------------------------------------------------------------
    bool handshake()
    {
        std::string buffer;
        const auto deadline = juce::Time::getMillisecondCounter() + 2000;

        while (! threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounter();

            if (now >= deadline)
                return false;   // conexão parada, sem pedido: fecha sem resposta

            const int ready = socket->waitUntilReady (true, (int) juce::jmin<juce::uint32> (100, deadline - now));

            if (ready < 0)
                return false;

            if (ready == 0)
                continue;

            char chunk[4096];
            const int n = socket->read (chunk, (int) sizeof (chunk), false);

            if (n <= 0)
                return false;

            buffer.append (chunk, (std::size_t) n);
            const auto parsed = ws::parseHttpRequest (buffer);

            if (parsed.status == ws::ParseStatus::incomplete)
                continue;

            if (parsed.status == ws::ParseStatus::invalid)
                return refuse (400, parsed.error);

            if (parsed.status == ws::ParseStatus::tooLarge)
                return refuse (431, parsed.error);

            if (server.authPaused())
                return refuse (429, "Erros de token demais: a WHYNOT espera 30 s antes de aceitar de novo.");

            const auto decision = server.judge (parsed.request);

            if (decision.status != 101)
            {
                if (decision.authFailure)
                    juce::Thread::sleep ((int) (limits::authFailureDelaySeconds * 1000.0));

                return refuse (decision.status, decision.reason);
            }

            // "Desligar a IA" (seção 5.7): o token certo, mas a pessoa desligou; só ela liga de novo
            if (! server.isEnabled())
                return refuse (503, "A IA está desligada na WHYNOT: a pessoa liga de novo pelo aviso no pé da janela.");

            // o que veio colado depois do pedido HTTP é ignorado: nenhum quadro conta antes do 101
            return writeNow (ws::switchingProtocolsResponse (decision.clientKey));
        }

        return false;
    }

    void readLoop()
    {
        ws::Decoder decoder (limits::messageInBytes);
        std::vector<char> chunk (64 * 1024);

        while (! threadShouldExit() && ! closing)
        {
            const int ready = socket->waitUntilReady (true, 100);

            if (ready < 0)
                return;

            if (ready == 0)
                continue;

            const int n = socket->read (chunk.data(), (int) chunk.size(), false);

            if (n <= 0)
                return;

            decoder.feed (chunk.data(), (std::size_t) n);
            ws::Message message;

            for (;;)
            {
                const auto status = decoder.next (message);

                if (status == ws::Decoder::Status::needMore)
                    break;

                if (status == ws::Decoder::Status::error)
                {
                    closeWith (decoder.failureCode(), fromUtf8 (decoder.failureReason()));
                    return;
                }

                switch (message.kind)
                {
                    case ws::Message::Kind::text:
                        server.received (*this, message.payload);
                        break;

                    case ws::Message::Kind::ping:
                        if (! server.allowPing())
                        {
                            closeWith (ws::closeCode::policy, "Pings demais.");
                            return;
                        }

                        queueFrame (ws::encodeFrame (ws::Opcode::pong, message.payload));
                        break;

                    case ws::Message::Kind::pong:
                        break;

                    case ws::Message::Kind::close:
                        closeWith (message.code == ws::closeCode::noStatus ? ws::closeCode::normal : message.code, {});
                        return;
                }
            }
        }
    }

    void drain (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        std::vector<char> sink (16 * 1024);

        while (! threadShouldExit() && juce::Time::getMillisecondCounter() < end)
        {
            const int ready = socket->waitUntilReady (true, 50);

            if (ready < 0)
                return;

            if (ready > 0 && socket->read (sink.data(), (int) sink.size(), false) <= 0)
                return;   // o outro lado fechou
        }
    }

    //--------------------------------------------------------------------------
    void queueText (const juce::String& json)
    {
        queueFrame (ws::encodeFrame (ws::Opcode::text, json.toStdString()));
    }

    void queueFrame (std::vector<std::uint8_t> frame)
    {
        {
            const juce::ScopedLock sl (outLock);

            if (closeQueued)
                return;

            if (outboxBytes + frame.size() > 64u * 1024u * 1024u)
            {
                outbox.clear();
                outboxBytes = 0;
                pushClose (ws::closeCode::policy, "O cliente não está lendo as respostas."_t);
            }
            else
            {
                outboxBytes += frame.size();
                outbox.push_back (std::move (frame));
            }
        }

        outReady.signal();
    }

    void closeWith (std::uint16_t code, const juce::String& why)
    {
        {
            const juce::ScopedLock sl (outLock);

            if (closeQueued)
                return;

            pushClose (code, why);
        }

        outReady.signal();
    }

    void pushClose (std::uint16_t code, const juce::String& why)   // com outLock
    {
        auto frame = ws::encodeClose (code, why.toStdString());
        outboxBytes += frame.size();
        outbox.push_back (std::move (frame));
        closeQueued = true;
        closing = true;
    }

    /** Responde a recusa do aperto de mão; devolve sempre false (a conexão fecha). */
    bool refuse (int status, const std::string& why)
    {
        writeNow (ws::errorResponse (status, why));
        return false;
    }

    bool writeNow (const std::string& bytes)
    {
        return writeAll (reinterpret_cast<const std::uint8_t*> (bytes.data()), bytes.size());
    }

    /** Sem waitUntilReady: no JUCE ele só tenta pegar a trava de leitura do socket e devolve -1 enquanto a
        thread de leitura espera dados. O write é um send() só, que pode mandar menos: repete até ir tudo.
        Se o outro lado parar de ler, o send espera; fechar o socket (shutdownSocket) solta. */
    bool writeAll (const std::uint8_t* data, std::size_t size)
    {
        while (size > 0)
        {
            const int sent = socket->write (data, (int) juce::jmin<std::size_t> (size, 64 * 1024));

            if (sent <= 0)
                return false;

            data += sent;
            size -= (std::size_t) sent;
        }

        return true;
    }

    void shutdownSocket()
    {
        const juce::ScopedLock sl (socketLock);

        if (! socketClosed)
        {
            socketClosed = true;
            socket->close();
        }
    }

    /** Manda as respostas; depois de mandar o close, fecha o socket. */
    struct Writer : juce::Thread
    {
        explicit Writer (Connection& c) : juce::Thread ("WHYNOT ponte escrita"), owner (c) {}

        void run() override
        {
            while (! threadShouldExit())
            {
                owner.outReady.wait (100);

                for (;;)
                {
                    std::vector<std::uint8_t> frame;

                    {
                        const juce::ScopedLock sl (owner.outLock);

                        if (owner.outbox.empty())
                            break;

                        frame = std::move (owner.outbox.front());
                        owner.outbox.pop_front();
                        owner.outboxBytes -= frame.size();
                    }

                    if (! owner.writeAll (frame.data(), frame.size()))
                    {
                        owner.closing = true;
                        owner.shutdownSocket();
                        return;
                    }
                }

                const juce::ScopedLock sl (owner.outLock);

                if (owner.closing && owner.outbox.empty())
                    return;
            }
        }

        Connection& owner;
    };

    ControlServer& server;
    std::unique_ptr<juce::StreamingSocket> socket;
    const int id;
    std::atomic<bool> authenticated { false }, finished { false }, closing { false };

    juce::CriticalSection outLock, socketLock;
    std::deque<std::vector<std::uint8_t>> outbox;
    std::size_t outboxBytes = 0;
    bool closeQueued = false, socketClosed = false;
    juce::WaitableEvent outReady;
    Writer writer { *this };
};

//==============================================================================
ControlServer::ControlServer (AppController& a, Options o)
    : juce::Thread ("WHYNOT ponte de controle"),
      app (a),
      options (std::move (o)),
      limiter (options.requestsPerSecond, options.requestBurst)
{
}

ControlServer::~ControlServer()
{
    stop();
}

bool ControlServer::start (juce::String& error)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (started)
        return true;

    std::array<std::uint8_t, 32> bytes {};

    if (! secureRandomBytes (bytes.data(), bytes.size()))
    {
        error = "O Windows não deu um número aleatório para o token."_t;
        return false;
    }

    token = fromUtf8 (encodeToken (bytes));

    if (! listener.createListener (options.port, "127.0.0.1"))
    {
        error = "Não consegui abrir a porta "_t + juce::String (options.port) + " em 127.0.0.1 (outro programa já está nela?)."_t;
        return false;
    }

    port = listener.getBoundPort();

    scratch = options.scratchFolder != juce::File()
                ? options.scratchFolder
                : juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("WHYNOT LAB").getChildFile ("IA");
    scratch.createDirectory();
    cleanScratch();

    controlFile = options.controlFile != juce::File()
                    ? options.controlFile
                    : app.getDataFolder().getChildFile ("control").getChildFile (juce::String (port.load()) + ".json");

    if (! controlFile.getParentDirectory().createDirectory() || ! writeControlFileNow())
    {
        listener.close();
        error = "Não consegui gravar o arquivo de controle em "_t + controlFile.getFullPathName();
        return false;
    }

    activity = ActivityLog (options.logFile != juce::File() ? options.logFile
                                                             : app.getDataFolder().getChildFile ("control").getChildFile ("ia.log"));
    activity.event ("ponte aberta na porta "_t + juce::String (port.load()) + (app.isHeadless() ? " (sem janela)" : " (com janela)"));

    started = true;
    startThread();
    return true;
}

void ControlServer::stop()
{
    if (! started)
        return;

    settle (false, "A WHYNOT fechou a ponte antes da sua resposta."_t, false);
    started = false;
    stopTimer();
    cancelPendingUpdate();

    signalThreadShouldExit();
    listener.close();
    stopThread (3000);

    std::vector<std::unique_ptr<Connection>> closing;

    {
        const juce::ScopedLock sl (connectionsLock);

        for (auto& c : connections)
            if (c->authenticated)
                c->closeWith (ws::closeCode::goingAway, "A WHYNOT está fechando a ponte."_t);

        closing.swap (connections);
    }

    closing.clear();   // cada conexão manda o close e espera as próprias threads, fora da trava

    {
        const juce::ScopedLock sl (queueLock);
        queue.clear();
    }

    // apaga o arquivo só se ele ainda for deste app (outro pode ter aberto a mesma porta depois)
    if (controlFile.existsAsFile() && juce::JSON::parse (controlFile.loadFileAsString())["token"].toString() == token)
        controlFile.deleteFile();

    tickets.clear();
    activity.event ("ponte fechada");
}

bool ControlServer::hasClient() const
{
    const juce::ScopedLock sl (connectionsLock);

    for (auto& c : connections)
        if (c->authenticated && ! c->finished && ! c->closing)
            return true;

    return false;
}

bool ControlServer::connectionAlive (int connection) const
{
    const juce::ScopedLock sl (connectionsLock);

    for (auto& c : connections)
        if (c->id == connection)
            return c->authenticated && ! c->finished && ! c->closing;

    return false;
}

void ControlServer::setEnabled (bool on)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (enabled.exchange (on) == on)
        return;

    if (on)
    {
        activity.event ("a pessoa ligou a IA de novo");
        return;
    }

    const auto why = "A pessoa desligou a IA na WHYNOT."_t;
    settle (false, why, false);
    tickets.clear();

    {
        const juce::ScopedLock sl (connectionsLock);

        for (auto& c : connections)
            if (c->authenticated && ! c->closing)
                c->closeWith (ws::closeCode::policy, why);
    }

    {
        const juce::ScopedLock sl (queueLock);
        queue.clear();
    }

    activity.event ("a pessoa desligou a IA");
}

bool ControlServer::writeControlFileNow()
{
    const auto json = object ({ { "port", port.load() },
                                { "token", token },
                                { "pid", (juce::int64) currentProcessId() },
                                { "headless", app.isHeadless() },
                                { "started", juce::Time::getCurrentTime().toISO8601 (true) },
                                { "protocol", protocolVersion } });

    juce::TemporaryFile temp (controlFile);
    return temp.getFile().replaceWithText (juce::JSON::toString (json) + "\n") && temp.overwriteTargetFileWithTemporary();
}

void ControlServer::cleanScratch() const
{
    // a pasta da IA guarda só o que é temporário: até 24 h e até 200 MB
    auto files = scratch.findChildFiles (juce::File::findFiles, false);
    const auto dayAgo = juce::Time::getCurrentTime() - juce::RelativeTime::hours (24);
    juce::int64 total = 0;

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });

    for (auto& f : files)
    {
        total += f.getSize();

        if (f.getLastModificationTime() < dayAgo || total > 200LL * 1024 * 1024)
            f.deleteFile();
    }
}

//==============================================================================
// Thread que aceita conexões
void ControlServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> socket (listener.waitForNextConnection());

        if (threadShouldExit())
            break;

        if (socket == nullptr)
        {
            juce::Thread::sleep (20);
            continue;
        }

        reap();

        if (socket->getHostName() != "127.0.0.1")
        {
            socket->close();
            continue;
        }

        const juce::ScopedLock sl (connectionsLock);
        int pending = 0;

        for (auto& c : connections)
            if (! c->authenticated && ! c->finished)
                ++pending;

        if (pending >= 4)
        {
            socket->close();   // muitas conexões sem token ao mesmo tempo: não deixa prenderem a ponte
            continue;
        }

        auto connection = std::make_unique<Connection> (*this, std::move (socket), ++nextConnectionId);
        connection->startThread();
        connections.push_back (std::move (connection));
    }
}

void ControlServer::reap()
{
    std::vector<std::unique_ptr<Connection>> done;

    {
        const juce::ScopedLock sl (connectionsLock);

        for (auto it = connections.begin(); it != connections.end();)
        {
            if ((*it)->finished && ! (*it)->isThreadRunning())
            {
                done.push_back (std::move (*it));
                it = connections.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }
}

HandshakeDecision ControlServer::judge (const ws::HttpRequest& request)
{
    const juce::ScopedLock sl (authLock);
    auto decision = decideHandshake (request, port.load(), token.toStdString());

    if (decision.authFailure)
        throttle.recordFailure (nowSeconds());
    else if (decision.status == 101)
        throttle.recordSuccess();

    return decision;
}

bool ControlServer::authPaused()
{
    const juce::ScopedLock sl (authLock);
    return throttle.isPaused (nowSeconds());
}

void ControlServer::promote (Connection& fresh)
{
    {
        const juce::ScopedLock sl (connectionsLock);

        for (auto& c : connections)
            if (c.get() != &fresh && c->authenticated && ! c->closing)
                c->closeWith (ws::closeCode::policy, "Outra conexão da IA tomou o lugar desta."_t);
    }

    // os pedidos da conexão antiga que ainda não rodaram não rodam mais
    {
        const juce::ScopedLock sl (queueLock);
        queue.erase (std::remove_if (queue.begin(), queue.end(), [&fresh] (const Request& r) { return r.connection != fresh.id; }), queue.end());
    }

    triggerAsyncUpdate();   // o aviso "IA conectada" e o registro ficam sabendo
}

bool ControlServer::allowPing()
{
    const juce::ScopedLock sl (queueLock);
    return limiter.tryTake (nowSeconds());
}

void ControlServer::received (Connection& connection, const std::string& text)
{
    if (connection.closing)
        return;   // a conexão foi substituída ou está fechando: o pedido não roda

    const auto json = fromUtf8 (text);

    if (jsonDepth (json) > limits::jsonDepth)
        return connection.queueText (rpcError ({}, -32600, "JSON aninhado demais (mais de 64 níveis)."_t));

    juce::var parsed;

    if (juce::JSON::parse (json, parsed).failed())
        return connection.queueText (rpcError ({}, -32700, "JSON inválido."_t));

    if (! parsed.isObject())
        return connection.queueText (rpcError ({}, -32600, "O pedido precisa ser um objeto JSON-RPC 2.0."));

    const bool hasId = parsed.hasProperty ("id");
    const auto id = parsed["id"];

    if (hasId && ! (id.isString() || id.isInt() || id.isInt64() || id.isDouble()))
        return connection.queueText (rpcError ({}, -32600, "O id precisa ser número ou texto."_t));

    if (parsed["jsonrpc"].toString() != "2.0" || ! parsed["method"].isString())
        return connection.queueText (rpcError (id, -32600, "Pedido JSON-RPC 2.0 malformado (falta jsonrpc ou method)."_t));

    const auto method = parsed["method"].toString();
    const auto params = parsed["params"];

    // no JUCE uma lista também é isObject(): o objeto de verdade é o que tem DynamicObject
    if (! params.isVoid() && params.getDynamicObject() == nullptr)
        return connection.queueText (rpcError (id, -32602, "params precisa ser um objeto."));

    if (method == "$/cancel")   // tira da fila um pedido que ainda não rodou; o que já está rodando vai até o fim
    {
        const auto target = params["id"];

        {
            const juce::ScopedLock sl (queueLock);
            queue.erase (std::remove_if (queue.begin(), queue.end(),
                                         [&] (const Request& r) { return r.connection == connection.id && r.id == target; }),
                         queue.end());

            // pode ser o que espera o seu ok na janela: a thread de mensagem confere e fecha a pergunta
            if (cancelled.size() < 64)
                cancelled.emplace_back (connection.id, target);
        }

        triggerAsyncUpdate();
        return;
    }

    if (! hasId)
        return;   // notificação: nada a responder

    if (! isEnabled())
        return connection.queueText (rpcError (id, -32000, "A IA está desligada na WHYNOT."_t));

    static const juce::StringArray known { "hello", "app.status", "command.list", "command.run", "command.batch", "project.inspect",
                                           "render.preview", "ui.screenshot", "app.quit" };

    if (! known.contains (method))
        return connection.queueText (rpcError (id, -32601, "Método desconhecido: "_t + method));

    {
        const juce::ScopedLock sl (queueLock);

        if ((int) queue.size() >= limits::queuedRequests || ! limiter.tryTake (nowSeconds()))
            return connection.queueText (rpcError (id, -32000, "Ocupado: pedidos demais de uma vez; espere as respostas."_t));

        queue.push_back ({ connection.id, ++nextSerial, id, method, params });
    }

    triggerAsyncUpdate();
}

void ControlServer::send (int connection, const juce::String& json)
{
    const juce::ScopedLock sl (connectionsLock);

    for (auto& c : connections)
    {
        if (c->id == connection)
        {
            if (c->authenticated && ! c->closing)
                c->queueText (json);

            return;
        }
    }
}

void ControlServer::notifyWaiting (const Request& request, int seconds)
{
    // o servidor MCP espera 30 s por resposta: este aviso estica a espera enquanto você decide
    send (request.connection, juce::JSON::toString (object ({ { "jsonrpc", "2.0" }, { "method", "request.waiting" },
                                                              { "params", object ({ { "id", request.id }, { "seconds", seconds },
                                                                                    { "message", "Esperando a confirmação da pessoa na janela da WHYNOT."_t } }) } }),
                                                    true));
}

//==============================================================================
// Thread de mensagem
bool ControlServer::uiBusy() const
{
    if (app.getCommands().isInGesture())
        return true;

    auto* modal = juce::ModalComponentManager::getInstanceWithoutCreating();
    return modal != nullptr && modal->getNumModalComponents() > 0;
}

void ControlServer::timerCallback()
{
    if (awaiting != nullptr)   // a pergunta da janela: confere o tempo e a conexão a cada 250 ms
    {
        checkAwaiting();
        return;
    }

    stopTimer();
    handleAsyncUpdate();
}

void ControlServer::handleAsyncUpdate()
{
    if (! started)
        return;

    if (const bool now = hasClient(); now != hadClient)
    {
        hadClient = now;
        activity.event (now ? "a IA conectou" : "a IA desconectou");
    }

    // um pedido espera o seu ok na janela: os outros esperam a vez (um por vez, na ordem de chegada)
    if (awaiting != nullptr)
    {
        checkAwaiting();
        return;
    }

    {
        const juce::ScopedLock sl (queueLock);
        cancelled.clear();   // os $/cancel de pedidos que não estão mais esperando
    }

    Request request;

    {
        const juce::ScopedLock sl (queueLock);

        if (queue.empty())
            return;

        request = queue.front();
    }

    // um comando da IA no meio do seu gesto entraria no seu passo de desfazer: espera ele acabar
    if ((request.method == "command.run" || request.method == "command.batch") && uiBusy())
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();

        if (waitingSince <= 0.0)
            waitingSince = now;

        if (now - waitingSince < options.gestureWaitMs)
        {
            startTimer (50);
            return;
        }

        waitingSince = 0.0;

        if (popIfFront (request.serial))
        {
            send (request.connection, rpcResult (request.id, failure ("Você está editando agora (arrastando ou com um diálogo aberto). Tente de novo daqui a pouco."_t)));
            activity.write (request.method, request.params["id"].toString(), "desistiu: a pessoa estava editando"_t, options.gestureWaitMs, request.params["params"]);
        }

        triggerAsyncUpdate();
        return;
    }

    waitingSince = 0.0;

    if (! popIfFront (request.serial))   // foi cancelado enquanto esperava
    {
        triggerAsyncUpdate();
        return;
    }

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    outcome = "ok";
    auto response = dispatch (request);

    if (response.isEmpty())
        return;   // esperando o seu ok na janela: a resposta (e o próximo pedido) sai no settle

    if ((std::size_t) response.getNumBytesAsUTF8() > limits::messageOutBytes)
    {
        response = rpcError (request.id, -32603, "Resposta maior que 16 MB: peça menos de cada vez (os filtros do project.inspect)."_t);
        outcome = "resposta grande demais";
    }

    send (request.connection, response);

    if (request.method == "command.batch")
        activity.write (request.method, "\"" + request.params["title"].toString() + "\"", outcome,
                        juce::Time::getMillisecondCounterHiRes() - t0, request.params["steps"]);
    else if (request.method == "command.run")
        activity.write (request.method, request.params["id"].toString(), outcome, juce::Time::getMillisecondCounterHiRes() - t0, request.params["params"]);
    else
        activity.write (request.method, {}, outcome, juce::Time::getMillisecondCounterHiRes() - t0, request.params);

    const juce::ScopedLock sl (queueLock);

    if (! queue.empty())
        triggerAsyncUpdate();   // um pedido por vez, para a tela não engasgar
}

bool ControlServer::popIfFront (std::uint64_t serial)
{
    const juce::ScopedLock sl (queueLock);

    if (queue.empty() || queue.front().serial != serial)
        return false;

    queue.pop_front();
    return true;
}

juce::String ControlServer::dispatch (const Request& request)
{
    const auto& p = request.params;
    const auto& method = request.method;

    if (method == "hello")
    {
        if (p.hasProperty ("protocol") && (int) p["protocol"] != protocolVersion)
            return rpcError (request.id, -32602, "Protocolo diferente: a ponte fala o "_t + juce::String (protocolVersion)
                                                   + " e o cliente pediu o " + p["protocol"].toString() + ".");

        return rpcResult (request.id, hello());
    }

    if (method == "app.status")
        return rpcResult (request.id, status());

    if (method == "command.list")
        return rpcResult (request.id, commandList (p));

    if (method == "command.run")
        return runCommand (request);

    if (method == "command.batch")
        return runBatch (request);

    if (method == "render.preview")
        return rpcResult (request.id, renderPreview (p));

    if (method == "ui.screenshot")
        return rpcResult (request.id, screenshot (p));

    if (method == "project.inspect")
    {
        juce::StringArray sections;

        if (auto* list = p["sections"].getArray())
            for (auto& s : *list)
                sections.add (s.toString());

        return rpcResult (request.id, inspectProject (app.getModel().get(), p["pattern"].toString(), p["channel"].toString(), sections));
    }

    // app.quit
    if (! options.onQuitRequested)
        return rpcResult (request.id, failure ("Com janela, quem fecha a WHYNOT é você."_t));

    juce::MessageManager::callAsync (options.onQuitRequested);
    return rpcResult (request.id, object ({ { "ok", true } }));
}

//==============================================================================
// Ver e ouvir (fatia 4.6): só gravam na pasta da IA
juce::var ControlServer::renderPreview (const juce::var& p)
{
    const auto& project = app.getModel().get();
    const auto what = p["what"].toString();

    if (what.isNotEmpty() && what != "pattern" && what != "song")
    {
        outcome = "recusado: what";
        return failure ("what é \"pattern\" (o padrão atual) ou \"song\" (a playlist)."_t);
    }

    const bool song = what.isEmpty() ? project.songMode : what == "song";
    const int loops = juce::jlimit (1, 16, p.hasProperty ("loops") ? (int) p["loops"] : (song ? 1 : 2));

    cleanScratch();
    const auto file = scratch.getChildFile ("previa-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S")
                                            + "-" + juce::String (++scratchSerial) + ".wav");
    const auto rendered = app.exportWav (file, loops, song);

    if (rendered.failed())
    {
        outcome = "erro: "_t + rendered.getErrorMessage();
        return failure (rendered.getErrorMessage());
    }

    const auto measures = measureAudio (file, project.bpm, project.timeSigNum);

    if (! measures.ok)
    {
        outcome = "erro: "_t + measures.error;
        return failure (measures.error);
    }

    auto out = toVar (measures);
    auto* o = out.getDynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("path", file.getFullPathName());
    o->setProperty ("what", song ? "song" : "pattern");
    o->setProperty ("loops", loops);
    o->setProperty ("bpm", project.bpm);
    o->setProperty ("beatsPerBar", project.timeSigNum);
    outcome = "ok: "_t + juce::String (measures.seconds, 1) + " s, pico " + juce::String (measures.peakDb, 1) + " dBFS";
    return out;
}

juce::var ControlServer::screenshot (const juce::var& p)
{
    if (! options.capture)
    {
        outcome = "sem captura";
        return failure ("Esta WHYNOT não tem captura de tela."_t);
    }

    const auto view = p["view"].toString();
    const int width = juce::jlimit (320, 1600, p.hasProperty ("width") ? (int) p["width"] : 1280);

    cleanScratch();
    const auto file = scratch.getChildFile ("tela-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S")
                                            + "-" + juce::String (++scratchSerial) + ".png");
    juce::String error;
    const auto size = options.capture (view, width, file, error);

    if (size.isEmpty() || ! file.existsAsFile())
    {
        outcome = "erro: "_t + error;
        return failure (error.isNotEmpty() ? error : juce::String ("A captura da tela não saiu."_t));
    }

    outcome = "ok: "_t + juce::String (size.getWidth()) + "x" + juce::String (size.getHeight());
    return object ({ { "ok", true }, { "path", file.getFullPathName() }, { "width", size.getWidth() }, { "height", size.getHeight() },
                     { "bytes", (juce::int64) file.getSize() }, { "view", view } });
}

juce::var ControlServer::hello() const
{
    const auto& model = app.getModel();

    return object ({ { "app", "WHYNOT LAB" },
                     { "version", ProjectInfo::versionString },
                     { "protocol", protocolVersion },
                     { "schemaVersion", wnl::currentSchemaVersion },
                     { "headless", app.isHeadless() },
                     { "scratch", scratch.getFullPathName() },
                     // quem pergunta quando um comando precisa do seu ok: a janela da WHYNOT ou o cliente de IA (ticket)
                     { "confirmations", options.askUser ? "window" : "client" },
                     { "confirmSeconds", options.confirmSeconds },
                     { "project", object ({ { "name", app.projectTitle() },
                                            { "file", model.file.getFullPathName() },
                                            { "dirty", model.dirty } }) } });
}

juce::var ControlServer::status() const
{
    auto& registry = app.getCommands();
    const auto& model = app.getModel();

    return object ({ { "playing", app.isPlaying() },
                     { "positionSteps", app.positionInSteps() },
                     { "dirty", model.dirty },
                     { "file", model.file.getFullPathName() },
                     { "undo", registry.undoDescription() },
                     { "redo", registry.redoDescription() },
                     { "aiUndo", registry.undoDescription().startsWith (aiPrefix) },
                     { "gestureOpen", registry.isInGesture() },
                     { "awaitingConfirmation", awaiting != nullptr },
                     { "headless", app.isHeadless() } });
}

juce::var ControlServer::commandList (const juce::var& p) const
{
    const auto domain = p["domain"].toString();
    juce::StringArray ids;

    if (auto* list = p["ids"].getArray())
        for (auto& id : *list)
            ids.add (id.toString());

    juce::Array<juce::var> out;
    const auto all = app.getCommands().describe();   // o var fica vivo enquanto a lista é lida

    if (auto* list = all.getArray())
    {
        for (auto& entry : *list)
        {
            const auto id = entry["id"].toString();

            if ((domain.isNotEmpty() && ! id.startsWith (domain + ".")) || (! ids.isEmpty() && ! ids.contains (id)))
                continue;

            const auto& rule = ruleFor (id.toStdString());
            auto* o = entry.getDynamicObject();
            o->setProperty ("domain", id.upToFirstOccurrenceOf (".", false, false));
            o->setProperty ("risk", fromUtf8 (riskName (rule.risk)));

            if (! rule.why.empty())
                o->setProperty ("why", fromUtf8 (rule.why));

            if (! rule.path.name.empty())
                o->setProperty ("paths", juce::Array<juce::var> { fromUtf8 (rule.path.name) });

            out.add (entry);
        }
    }

    return object ({ { "commands", out } });
}

juce::String ControlServer::runCommand (const Request& request)
{
    const auto& p = request.params;
    const auto id = p["id"].toString();
    const auto args = p["params"];

    if (id.isEmpty() || ! (args.isVoid() || args.getDynamicObject() != nullptr))
        return rpcError (request.id, -32602, "command.run pede { id, params }."_t);

    auto& registry = app.getCommands();
    const auto* info = registry.find (id);

    if (info == nullptr)
    {
        outcome = "comando desconhecido";
        return rpcResult (request.id, failure ("Comando desconhecido: "_t + id + ". Veja a lista com command.list."));
    }

    // a IA só desfaz e refaz o que ela mesma fez
    if (id == "edit.undo" || id == "edit.redo")
    {
        const bool undo = id == "edit.undo";
        const auto top = undo ? registry.undoDescription() : registry.redoDescription();

        if (top.isNotEmpty() && ! top.startsWith (aiPrefix))
        {
            outcome = "recusado: o passo é da pessoa"_t;
            return rpcResult (request.id, failure ("O último passo foi feito por você (\""_t + top + "\"); a IA só "_t
                                                   + (undo ? "desfaz" : "refaz") + " o que ela mesma fez."));
        }
    }

    const auto given = args.getDynamicObject() != nullptr ? args : juce::var (new juce::DynamicObject());
    const auto gate = evaluate (id, given, app, allowedFolders (app, scratch));

    if (gate.kind == GateDecision::Kind::refuse)
    {
        outcome = "recusado: "_t + gate.reason;
        return rpcResult (request.id, object ({ { "ok", false }, { "error", gate.reason }, { "refused", true } }));
    }

    if (gate.kind == GateDecision::Kind::confirm)
    {
        // sem janela: a pessoa já disse sim no cliente de IA, e o pedido volta com o ticket
        if (const auto ticket = p["confirm"].toString(); ticket.isNotEmpty())
        {
            if (! takeTicket (ticket, id, given))
            {
                outcome = "recusado: confirmação que não vale"_t;
                return rpcResult (request.id, object ({ { "ok", false }, { "refused", true },
                                                        { "error", "A confirmação não vale mais: passou do prazo, já foi usada ou era de outro pedido. "_t
                                                                   "Peça de novo, se a pessoa ainda quiser."_t } }));
            }

            const auto response = runGated (request, id, gate.params);
            outcome = "confirmado no cliente de IA; "_t + outcome;
            return response;
        }

        // com janela: a pergunta aparece na WHYNOT, e a resposta sai quando você responder
        if (options.askUser)
        {
            ask (request, id, gate);
            return {};
        }

        outcome = "pede confirmação ao cliente de IA (ticket)"_t;
        return rpcResult (request.id, object ({ { "ok", false },
                                                { "error", "Este comando precisa da confirmação da pessoa: "_t + gate.reason },
                                                { "needsConfirmation", object ({ { "summary", gate.reason },
                                                                                 { "title", info->title },
                                                                                 { "ticket", issueTicket (id, given) },
                                                                                 { "expiresInSeconds", options.confirmSeconds } }) } }));
    }

    return runGated (request, id, gate.params);
}

juce::String ControlServer::runGated (const Request& request, const juce::String& id, const juce::var& params)
{
    auto& registry = app.getCommands();
    const auto* info = registry.find (id);

    if (info == nullptr)   // sumiu enquanto você decidia (não acontece hoje: os comandos são fixos)
    {
        outcome = "comando desconhecido";
        return rpcResult (request.id, failure ("Comando desconhecido: "_t + id + "."));
    }

    CommandResult result;

    if (info->changesModel)
    {
        // um passo de desfazer próprio, com o nome que você vê no menu: "IA: <título>"
        CommandRegistry::ScopedGesture gesture (registry, aiPrefix + info->title);
        result = registry.run (id, params);
    }
    else
    {
        result = registry.run (id, params);
    }

    outcome = result.ok ? juce::String ("ok") : "erro: "_t + result.error;
    return rpcResult (request.id, describeResult (result, app));
}

//==============================================================================
// A confirmação (seção 5.5)
void ControlServer::ask (const Request& request, const juce::String& command, const GateDecision& gate)
{
    const auto* info = app.getCommands().find (command);
    const auto now = juce::Time::getMillisecondCounterHiRes();

    awaiting = std::make_unique<Awaiting>();
    awaiting->request = request;
    awaiting->command = command;
    awaiting->params = gate.params;
    awaiting->started = now;
    awaiting->deadline = now + options.confirmSeconds * 1000.0;
    awaiting->settled = std::make_shared<bool> (false);

    notifyWaiting (request, options.confirmSeconds);
    activity.write (request.method, command, "pergunta na janela: "_t + gate.reason, -1.0, request.params["params"]);
    startTimer (250);   // antes da pergunta: a resposta pode vir na hora (o autoteste responde assim)

    const auto settled = awaiting->settled;
    const Confirmation question { command, info != nullptr ? info->title : command, gate.reason, gate.params, options.confirmSeconds };

    // a resposta chega pela janela, na thread de mensagem; depois do prazo, da desistência ou do
    // "Desligar a IA", ela não vale mais (settled), e a ponte pode até já ter saído
    options.askUser (question, [this, settled] (bool allowed)
    {
        if (! *settled)
            settle (allowed, allowed ? juce::String() : juce::String ("A pessoa recusou na janela da WHYNOT. Não tente de novo pelo mesmo caminho."_t), true);
    });
}

void ControlServer::settle (bool allowed, const juce::String& why, bool answeredInWindow)
{
    if (awaiting == nullptr)
        return;

    const auto done = std::move (awaiting);   // daqui em diante não há pergunta aberta
    *done->settled = true;
    stopTimer();

    if (! answeredInWindow && options.onAskCancelled)
        options.onAskCancelled();   // a janela fecha o cartão

    const auto ms = juce::Time::getMillisecondCounterHiRes() - done->started;
    const auto& request = done->request;
    juce::String response;

    if (allowed && enabled && connectionAlive (request.connection))
    {
        response = runGated (request, done->command, done->params);
        activity.write (request.method, done->command, "permitido na janela; "_t + outcome, ms, request.params["params"]);
    }
    else
    {
        const auto reason = allowed ? juce::String ("A IA saiu antes da resposta: não rodou."_t) : why;
        response = rpcResult (request.id, object ({ { "ok", false }, { "error", reason }, { "refused", true },
                                                    { "declined", ! allowed && answeredInWindow } }));
        activity.write (request.method, done->command, (answeredInWindow ? "recusado na janela"_t : "sem resposta: "_t + reason), ms,
                        request.params["params"]);
    }

    send (request.connection, response);
    triggerAsyncUpdate();   // o próximo da fila
}

void ControlServer::checkAwaiting()
{
    if (awaiting == nullptr)
    {
        stopTimer();
        return;
    }

    bool gaveUp = false;

    {
        const juce::ScopedLock sl (queueLock);

        for (auto& [connection, id] : cancelled)
            gaveUp = gaveUp || (connection == awaiting->request.connection && id == awaiting->request.id);

        cancelled.clear();
    }

    if (gaveUp)
        settle (false, "A IA desistiu do pedido antes da resposta."_t, false);
    else if (! connectionAlive (awaiting->request.connection))
        settle (false, "A IA saiu antes da resposta."_t, false);
    else if (juce::Time::getMillisecondCounterHiRes() > awaiting->deadline)
        settle (false, "Sem resposta da pessoa em "_t + juce::String (options.confirmSeconds) + " s na janela da WHYNOT: não rodou. "_t
                       "Peça de novo, se ainda quiser."_t, false);
}

juce::String ControlServer::issueTicket (const juce::String& command, const juce::var& params)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    tickets.erase (std::remove_if (tickets.begin(), tickets.end(), [now] (const Ticket& t) { return t.expires < now; }), tickets.end());

    if (tickets.size() >= 16)
        tickets.erase (tickets.begin());   // o mais velho

    std::array<std::uint8_t, 32> bytes {};

    if (! secureRandomBytes (bytes.data(), bytes.size()))
        return {};   // sem número aleatório não há ticket; o comando volta recusado pelo takeTicket

    tickets.push_back ({ fromUtf8 (encodeToken (bytes)), command, canonicalJson (params), now + options.confirmSeconds * 1000.0 });
    return tickets.back().id;
}

bool ControlServer::takeTicket (const juce::String& ticketId, const juce::String& command, const juce::var& params)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto wanted = ticketId.toStdString();

    for (auto it = tickets.begin(); it != tickets.end(); ++it)
    {
        if (constantTimeEquals (it->id.toStdString(), wanted))
        {
            // uso único: o ticket sai mesmo se for de outro comando ou de outros parâmetros
            const bool valid = it->expires >= now && it->command == command && it->params == canonicalJson (params);
            tickets.erase (it);
            return valid;
        }
    }

    return false;
}

juce::String ControlServer::runBatch (const Request& request)
{
    const auto& p = request.params;
    const auto title = p["title"].toString().trim();
    const auto* steps = p["steps"].getArray();

    if (title.isEmpty() || steps == nullptr || steps->isEmpty())
        return rpcError (request.id, -32602, "command.batch pede { title, steps: [{ id, params }] }."_t);

    if (steps->size() > limits::batchSteps)
        return rpcResult (request.id, failure ("Lote com mais de 200 passos: divida em lotes menores."_t));

    auto& registry = app.getCommands();
    const auto folders = allowedFolders (app, scratch);
    juce::Array<juce::var> gated;

    for (int i = 0; i < steps->size(); ++i)
    {
        const auto& step = steps->getReference (i);
        const auto id = step["id"].toString();
        const auto args = step["params"];
        const auto where = "Passo "_t + juce::String (i + 1) + " (" + id + "): ";

        if (id.isEmpty() || ! (args.isVoid() || args.getDynamicObject() != nullptr))
            return rpcError (request.id, -32602, "Passo "_t + juce::String (i + 1) + ": cada passo é { id, params }."_t);

        if (registry.find (id) == nullptr)
        {
            outcome = "passo "_t + juce::String (i + 1) + ": comando desconhecido";
            return rpcResult (request.id, object ({ { "ok", false }, { "error", where + "comando desconhecido." }, { "failedStep", i + 1 } }));
        }

        const auto gate = evaluate (id, args, app, folders);

        if (gate.kind != GateDecision::Kind::run)
        {
            const auto why = gate.kind == GateDecision::Kind::confirm
                               ? "precisa da sua confirmação, e um lote não pede confirmação: "_t + gate.reason
                               : gate.reason;

            outcome = "recusado: passo "_t + juce::String (i + 1) + " (" + id + ")";
            return rpcResult (request.id, object ({ { "ok", false }, { "error", where + why }, { "failedStep", i + 1 }, { "refused", true } }));
        }

        gated.add (object ({ { "id", id }, { "params", gate.params } }));
    }

    const auto result = registry.runBatch (gated, aiPrefix + title);
    outcome = result.ok ? juce::String ("ok") : "erro: "_t + result.error;
    return rpcResult (request.id, describeResult (result, app));
}

//==============================================================================
ParentWatch::ParentWatch (std::uint32_t parentPid, std::function<void()> onGone)
    : pid (parentPid), gone (std::move (onGone))
{
    startTimer (2000);
}

void ParentWatch::timerCallback()
{
    if (! isProcessAlive (pid))
    {
        stopTimer();

        if (gone)
            gone();
    }
}

} // namespace whynot::control
