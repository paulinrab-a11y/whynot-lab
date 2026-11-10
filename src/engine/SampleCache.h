#pragma once

#include <JuceHeader.h>

#include "SampleAnalysis.h"
#include "SampleLoads.h"
#include "../audio/SampleVoice.h"
#include "../common/Text.h"

#include <functional>
#include <map>
#include <memory>

namespace whynot
{

/** Decodifica arquivos de áudio uma vez e compartilha o resultado entre canais e Edits.

    Dois caminhos, com a mesma decodificação (o sample sai igual, amostra a amostra):
    - get(): na hora, na thread de quem chama. É o do render, do export, do --render e do autoteste, que
      precisam do dado antes de seguir.
    - request(): numa thread de trabalho (um juce::ThreadPool de 2 threads, criado no primeiro pedido), com a
      resposta na thread de mensagem. É o do projeto ao vivo: um arquivo longo não trava a tela
      (docs/arquitetura.md, seção 2: acima de ~10 ms vai para uma thread de trabalho).

    Tudo aqui é da thread de mensagem, menos a decodificação do request(). Nunca a thread de áudio.
*/
class SampleCache
{
public:
    using Data = std::shared_ptr<const audio::SampleData>;

    /** A resposta de um request(): o sample, ou nullptr com o motivo. Sempre na thread de mensagem. */
    using Callback = std::function<void (Data, const juce::String& error)>;

    explicit SampleCache (juce::AudioFormatManager& formats);

    /** Cancela os pedidos em voo (ninguém é avisado) e espera a thread de trabalho largar o arquivo. */
    ~SampleCache();

    /** Devolve o sample decodificado; recarrega se o arquivo mudou no disco. Decodifica aqui, na hora. */
    Data get (const juce::File& file, juce::String& error);

    /** Só o que já está pronto, sem decodificar: o sample se ele está no cache e o arquivo não mudou;
        nullptr com `error` se o arquivo não existe; nullptr sem `error` se é preciso carregar (request). */
    Data getIfReady (const juce::File& file, juce::String& error) const;

    /** Decodifica numa thread de trabalho e avisa na thread de mensagem, nunca dentro desta chamada. O mesmo
        arquivo pedido várias vezes enquanto carrega decodifica uma vez só, e todos são avisados. */
    void request (const juce::File& file, Callback callback);

    /** Arquivos pedidos por request() que ainda não foram entregues. */
    int pendingLoads() const;

    /** Espera os pedidos em voo e entrega todos agora (os callbacks rodam aqui dentro). Para quem precisa do
        som completo antes de seguir: render do Edit ao vivo, congelar, autoteste. */
    void flushPendingLoads();

    void clear() { entries.clear(); }

    /** Esquece os samples que só o cache ainda segura (caça de bugs do produtor, 21/10): sem isto, cada
        projeto aberto deixava os samples dele na memória até o app fechar. */
    void prune()
    {
        for (auto it = entries.begin(); it != entries.end();)
            it = it->second.data.use_count() <= 1 ? entries.erase (it) : std::next (it);
    }

    size_t size() const noexcept   { return entries.size(); }

private:
    struct Entry
    {
        juce::Time modified;
        juce::int64 size = 0;
        Data data;
    };

    /** O que a decodificação devolve (na thread de trabalho, com a data e o tamanho lidos antes de abrir). */
    struct Loaded
    {
        Data data;
        juce::String error;
        juce::Time modified;
        juce::int64 size = 0;
    };

    /** O que a thread de trabalho e a de mensagem dividem; vive enquanto houver trabalho rodando. */
    struct Shared
    {
        loads::InFlight<juce::String, Loaded, Callback> board;
        SampleCache* owner = nullptr;   // só a thread de mensagem; nullptr depois que o cache saiu
    };

    class DecodeJob;

    /** O arquivo inteiro decodificado, com o corte do MP3, sem denormais e com os mipmaps. `job` (pode ser
        nullptr) deixa a thread de trabalho desistir no meio quando o cache está saindo. */
    static Data decode (juce::AudioFormatManager&, const juce::File&, juce::String& error, const juce::ThreadPoolJob* job);

    void deliverFinished();

    juce::AudioFormatManager& formatManager;
    std::map<juce::String, Entry> entries;
    std::shared_ptr<Shared> shared;
    std::unique_ptr<juce::ThreadPool> pool;   // criado no primeiro request(): sem janela, nenhuma thread nasce

    JUCE_DECLARE_NON_COPYABLE (SampleCache)
};

} // namespace whynot
