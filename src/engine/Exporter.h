#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"
#include "SampleCache.h"

#include <optional>
#include <thread>

namespace whynot
{

namespace te = tracktion;

/** Renderiza para WAV (padrão 48 kHz / 24 bits; o MP3 pede 44,1 kHz):
    - song = false: o padrão atual, repetido `loops` vezes;
    - song = true: a playlist inteira, do início até o fim do último bloco.
    Usa um Edit só para o render (o Edit ao vivo continua tocando).
    Thread de mensagem; bloqueia até terminar.
*/
class PluginCatalog;
class Projector;

/** Comparação A/B da mixagem aplicada a um render (para medir o loudness de cada lado). */
struct RenderAudition
{
    bool bypassInserts = false;   // todos os inserts em bypass e os envios desligados
    float trimDb = 0.0f;          // ganho no fim do master
};

juce::Result renderToWav (te::Engine&, SampleCache&, const Project&, const juce::File& projectFolder,
                          bool song, int loops, const juce::File& outFile, const PluginCatalog* = nullptr,
                          double sampleRate = 48000.0, int bitDepth = 24, const RenderAudition& = {});

/** Nome de arquivo que o Windows aceita (caça de bugs do produtor, 21/10): sem caracteres proibidos,
    sem ponto nem espaço no fim (o Windows corta, e o arquivo sai com outro nome) e sem os nomes
    reservados (CON, PRN, AUX, NUL, COM0 a COM9, LPT0 a LPT9), que ganham um "_" ("AUX" vira "AUX_"). */
juce::String safeFileName (const juce::String& name);

/** true se o nome (sem a extensão) é um dos nomes de aparelho que o Windows reserva. */
bool isReservedFileName (const juce::String& fileName);

/** O nome do pacote para vender (bloco 2, 23/10): "Título 140bpm Cm", com o BPM e o tom do projeto, como quem compra
    beat procura. A escala cromática fica sem tom; as escalas menores (menor, harmônica, frígio, dórico, pentatônica
    menor) levam o "m". Já passa pelo safeFileName. */
juce::String packBaseName (const Project&, const juce::String& title);

/** Confere, antes do render, se dá para gravar `file`, e explica em português quando não dá:
    caminho com mais de 259 caracteres, nome reservado, arquivo aberto em outro programa ou só de
    leitura, pasta que não pode ser criada ou sem permissão (inclusive o "Acesso controlado a pastas"
    do Windows). Um arquivo que já existe e está aberto num programa que deixa escrever é esvaziado:
    o render novo não pode ir para o fim do antigo (o JUCE abre o arquivo para escrever no fim).
    Apaga o arquivo antigo, a não ser com keepExisting (a conferência do MP3 antes do render: o arquivo
    só é trocado no fim, e um render que falha não pode levar o MP3 antigo junto). */
juce::Result checkWritableTarget (const juce::File& file, bool keepExisting = false);

/** O mesmo render em duas partes, para a tela não travar enquanto exporta (pedido do usuário, 21/10):
    - prepare(), na thread de mensagem, monta o Edit, o projetor e a tarefa do Tracktion;
    - run(), em qualquer thread, roda o render até o fim (é o laço que o renderToWav roda);
    - cancel() pode vir de outra thread: o render para no próximo bloco;
    - o objeto é destruído na thread de mensagem, depois do run(). Num render cancelado ou que falhou,
      é no destrutor que o grafo do Tracktion sai e o arquivo pela metade é apagado: desmontar o grafo
      precisa da thread de mensagem, que pode estar parada esperando o run() acabar (fechar o app no
      meio da exportação travava o app para sempre).
    O renderToWav é prepare() seguido de run(), na mesma thread.

    No Edit do render, os clipes de MP3, FLAC e OGG não usam o arquivo "proxy" do Tracktion (caça de
    bugs do produtor, 21/10): o proxy é um WAV que só um Timer da thread de mensagem começa, e o render
    esperava por ele para sempre quando rodava nela (a exportação antiga, o --render e os comandos sem
    "background"). Sem proxy, o clipe é lido direto do arquivo. */
class RenderJob
{
public:
    struct Settings
    {
        double sampleRate = 48000.0;
        int bitDepth = 24;
        RenderAudition audition;
        double fromSeconds = 0.0, toSeconds = 0.0;   // só um trecho (a seleção da régua); to <= from = tudo

        // A música inteira (song, sem trecho) renderiza até tailSeconds além do último bloco, para o reverb,
        // o delay e o 808 do fim tocarem até acabar (revisão profissional, 22/10). Com trimTail, o silêncio
        // que sobra é cortado (abaixo de -90 dBFS); sem ele (os stems), todos ficam do mesmo tamanho.
        double tailSeconds = 0.0;
        bool trimTail = true;
    };

    static std::unique_ptr<RenderJob> prepare (te::Engine&, SampleCache&, const Project&, const juce::File& projectFolder,
                                               bool song, int loops, const juce::File& outFile, const PluginCatalog*,
                                               const Settings&, juce::Result& error);
    ~RenderJob();

    juce::Result run();
    void cancel() noexcept                   { cancelled = true; }
    bool wasCancelled() const noexcept       { return cancelled.load(); }
    float getProgress() const noexcept       { return progress.load(); }   // 0 a 1
    double getLengthSeconds() const noexcept { return length; }

    /** Aviso para mostrar depois de um render que deu certo (por exemplo, o WAV clipou). Vazio = nada. */
    const juce::String& getWarning() const noexcept   { return warning; }

private:
    RenderJob() = default;

    // na ordem de criação: a tarefa sai antes do estado de render, o projetor antes do Edit
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<Projector> projector;
    std::unique_ptr<te::Edit::ScopedRenderStatus> renderStatus;
    std::unique_ptr<te::Renderer::RenderTask> task;
    juce::File outFile;
    double length = 0.0;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancelled { false };
    bool removeOutput = false;   // o render não terminou: o destrutor apaga o arquivo pela metade

    // depois do render, na mesma thread: cortar o silêncio da cauda e procurar clipe (WAV de 16 e 24 bits)
    double keepSeconds = 0.0;    // o que fica sempre, mesmo em silêncio (a música sem a cauda)
    bool trimAfter = false, checkClip = false;
    juce::String warning;

    JUCE_DECLARE_NON_COPYABLE (RenderJob)
};

/** A thread de trabalho que roda o RenderJob::run() (e o que vem depois, como o LAME), e que o dono
    pode interromper ao fechar (caça de bugs do produtor, 21/10). O Tracktion monta e desmonta o grafo
    do render na thread de mensagem e espera por ela; se a thread de mensagem estiver parada no join
    (o app fechando no meio da exportação), as duas esperavam uma pela outra para sempre. Aqui a thread
    é registrada antes de começar, e join (true) avisa a saída: as esperas dela desistem e o join volta.
    Depois do join, destrua o RenderJob na thread de mensagem (o grafo que sobrou sai ali).
    Use também na prévia da tela Projetos, que tem o mesmo laço. */
class RenderThread
{
public:
    RenderThread() = default;
    ~RenderThread()   { join (true); }

    void start (std::function<void()> body);
    void join (bool interrupt);
    bool isRunning() const noexcept   { return thread.joinable(); }

private:
    std::thread thread;
    std::optional<te::ScopedThreadExitStatusEnabler> exitStatus;

    JUCE_DECLARE_NON_COPYABLE (RenderThread)
};

} // namespace whynot
