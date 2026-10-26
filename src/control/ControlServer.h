#pragma once

// A ponte de controle da Fase 4: um WebSocket só em 127.0.0.1, com um token novo a cada vez que o app abre,
// para o servidor MCP (mcp/) rodar os comandos do app. A rede fica em threads de trabalho; os pedidos rodam na
// thread de mensagem, um por vez, na ordem de chegada, e esperam o seu gesto (fader, arrasto) acabar.
// Ver docs/fase-4-plano.md, seção 3.

#include <JuceHeader.h>

#include "CommandGate.h"
#include "ControlLog.h"
#include "ControlPolicy.h"

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace whynot
{
class AppController;
}

namespace whynot::control
{

/** Um pedido da IA que precisa do seu ok (seção 5.5), como a janela mostra. */
struct Confirmation
{
    juce::String command;    // o id: "channel.addPlugin"
    juce::String title;      // o título do comando: "Adicionar plugin"
    juce::String reason;     // por que confirma, em português
    juce::var params;        // os parâmetros (com o caminho normalizado)
    int seconds = 120;       // sem resposta nesse tempo, fica recusado
};

class ControlServer : private juce::Thread,
                      private juce::AsyncUpdater,
                      private juce::Timer
{
public:
    struct Options
    {
        int port = 47830;                   // 0 = o sistema escolhe uma porta livre
        juce::File controlFile;             // vazio = <dados do app>/control/<porta>.json
        juce::File scratchFolder;           // vazio = %LOCALAPPDATA%\WHYNOT LAB\IA
        juce::File logFile;                 // vazio = <dados do app>/control/ia.log
        double requestsPerSecond = limits::requestsPerSecond;
        double requestBurst = limits::requestBurst;
        int gestureWaitMs = 10000;          // quanto um pedido espera o seu gesto acabar
        int confirmSeconds = 120;           // quanto a confirmação espera (na janela ou o ticket do cliente)
        std::function<void()> onQuitRequested;   // app.quit: só sem janela; nullptr = recusado

        /** Com janela: mostra a pergunta na WHYNOT e chama `answer` na thread de mensagem (true = Permitir).
            Sem isto (sem janela), o pedido volta com um ticket, e quem pergunta é o cliente de IA. */
        std::function<void (const Confirmation&, std::function<void (bool allowed)> answer)> askUser;

        /** A pergunta da janela não vale mais: a IA desistiu, caiu, foi desligada ou passou o tempo. */
        std::function<void()> onAskCancelled;

        /** ui.screenshot (fatia 4.6): mostra a tela pedida (vazio = a atual), grava o PNG com até `maxWidth` de
            largura e devolve o tamanho; vazio = não deu (o motivo em `error`). Com janela, a tela que você vê;
            sem janela, uma tela montada fora da área de trabalho. nullptr = sem captura.
            `channel` entra com o canal pedido para o piano roll (vazio = o escolhido) e volta com o que a tela mostra. */
        std::function<juce::Rectangle<int> (const juce::String& view, juce::String& channel, int maxWidth,
                                            const juce::File& png, juce::String& error)> capture;
    };

    ControlServer (AppController&, Options);
    ~ControlServer() override;

    /** Abre a porta e grava o arquivo de controle (com o token). false = não abriu; o motivo vai em `error`. */
    bool start (juce::String& error);

    /** Fecha as conexões e a porta e apaga o arquivo de controle. Chamado também pelo destrutor. */
    void stop();

    int getPort() const noexcept                    { return port.load(); }
    const juce::String& getToken() const noexcept   { return token; }
    const juce::File& getControlFile() const noexcept   { return controlFile; }
    const juce::File& getScratchFolder() const noexcept { return scratch; }
    const juce::File& getLogFile() const noexcept       { return activity.getFile(); }

    /** Há um cliente autenticado agora? (para o aviso "IA conectada") */
    bool hasClient() const;

    /** "Desligar a IA" (seção 5.7): false fecha a conexão, recusa as próximas (503) e cancela a pergunta
        aberta; true volta a aceitar. Só na thread de mensagem. */
    void setEnabled (bool);
    bool isEnabled() const noexcept                 { return enabled.load(); }

    /** Um pedido está esperando o seu ok na janela. */
    bool isAwaitingConfirmation() const noexcept    { return awaiting != nullptr; }

    static constexpr int protocolVersion = 1;

private:
    struct Connection;

    struct Request
    {
        int connection = 0;
        std::uint64_t serial = 0;    // ordem de chegada, para saber se o da frente ainda é o mesmo
        juce::var id;
        juce::String method;
        juce::var params;
    };

    // aceita conexões (a thread desta classe)
    void run() override;

    // roda os pedidos na thread de mensagem
    void handleAsyncUpdate() override;
    void timerCallback() override;
    bool uiBusy() const;
    bool popIfFront (std::uint64_t serial);

    // chamados pelas threads das conexões
    HandshakeDecision judge (const ws::HttpRequest&);
    bool authPaused();
    void promote (Connection&);
    void received (Connection&, const std::string& text);
    bool allowPing();
    void reap();

    void send (int connection, const juce::String& json);
    void notifyWaiting (const Request&, int seconds);
    juce::String dispatch (const Request&);
    juce::String runCommand (const Request&);
    juce::String runBatch (const Request&);
    juce::String runGated (const Request&, const juce::String& id, const juce::var& params);
    juce::var renderPreview (const juce::var& params);
    juce::var screenshot (const juce::var& params);
    juce::var commandList (const juce::var& params) const;
    juce::var status() const;
    juce::var hello() const;
    bool writeControlFileNow();
    void cleanScratch() const;
    bool connectionAlive (int connection) const;

    // a confirmação (seção 5.5): na janela (askUser) ou com o ticket do cliente (sem janela)
    struct Awaiting
    {
        Request request;
        juce::String command;
        juce::var params;           // os do portão, com o caminho normalizado
        double deadline = 0.0;      // ms (getMillisecondCounterHiRes)
        double started = 0.0;
        std::shared_ptr<bool> settled;
    };

    struct Ticket
    {
        juce::String id, command, params;   // params: o canonicalJson do que a IA mandou
        double expires = 0.0;               // ms
    };

    void ask (const Request&, const juce::String& command, const GateDecision&);
    void settle (bool allowed, const juce::String& why, bool answeredInWindow);
    void checkAwaiting();
    juce::String issueTicket (const juce::String& command, const juce::var& params);
    bool takeTicket (const juce::String& ticketId, const juce::String& command, const juce::var& params);

    AppController& app;
    Options options;
    juce::StreamingSocket listener;
    std::atomic<int> port { 0 };
    juce::String token;
    juce::File controlFile, scratch;
    bool started = false;
    std::atomic<bool> enabled { true };

    ActivityLog activity { {} };   // o ia.log (seção 5.7)
    juce::String outcome;                 // o resultado do pedido que acabou de rodar, para o registro
    bool hadClient = false;               // para registrar "conectou" e "desconectou"
    std::unique_ptr<Awaiting> awaiting;   // só na thread de mensagem
    std::vector<Ticket> tickets;          // idem
    int scratchSerial = 0;                // nomes únicos para as prévias e as capturas

    juce::CriticalSection connectionsLock;
    std::vector<std::unique_ptr<Connection>> connections;
    int nextConnectionId = 0;

    juce::CriticalSection queueLock;
    std::deque<Request> queue;
    std::uint64_t nextSerial = 0;
    RateLimiter limiter;

    juce::CriticalSection authLock;
    AuthThrottle throttle;

    double waitingSince = 0.0;   // o pedido da frente espera um gesto desde (ms)
    std::vector<std::pair<int, juce::var>> cancelled;   // $/cancel de pedidos já fora da fila (connection, id); com queueLock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlServer)
};

/** Fecha o app sem janela quando o processo que o abriu (o servidor MCP) morre: assim não fica um app
    esquecido gastando RAM. Confere a cada 2 s. */
class ParentWatch : private juce::Timer
{
public:
    ParentWatch (std::uint32_t parentPid, std::function<void()> onGone);
    ~ParentWatch() override   { stopTimer(); }

private:
    void timerCallback() override;

    const std::uint32_t pid;
    std::function<void()> gone;
};

} // namespace whynot::control
