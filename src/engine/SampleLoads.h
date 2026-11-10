#pragma once

// A contabilidade do carregamento assíncrono de samples, sem JUCE (testada em tests/samples, com o cl):
//  - InFlight: um trabalho por chave (o caminho do arquivo), por mais gente que peça; a thread de trabalho
//    entrega, a de mensagem recolhe;
//  - Batches: o que cada dono (um canal) espera, com número de geração: o pedido antigo que chega depois de
//    o canal mudar de sample, ou de ele sair, é descartado.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace whynot::loads
{

/** Trabalhos em voo, um por chave.

    - Thread de mensagem: request(), drain(), waitUntilIdle(), cancelAll() e as contagens.
    - Thread de trabalho: complete(), uma vez por chave.

    Quem espera (Waiter) entra e sai sempre pela thread de mensagem: a de trabalho só guarda o resultado.
    Usa mutex: nunca na thread de áudio.
*/
template <typename Key, typename Result, typename Waiter>
class InFlight
{
public:
    struct Finished
    {
        Key key;
        Result result;
        std::vector<Waiter> waiters;
    };

    /** true: é o primeiro pedido desta chave, quem chamou põe o trabalho para rodar.
        false: o trabalho dela já está em voo (ou pronto, esperando o drain) e este pedido só entrou na fila. */
    bool request (const Key& key, Waiter waiter)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        auto [it, isNew] = jobs.try_emplace (key);
        it->second.waiters.push_back (std::move (waiter));
        return isNew;
    }

    /** Thread de trabalho: o resultado da chave. Chave desconhecida (cancelada) ou já entregue: ignora. */
    void complete (const Key& key, Result result)
    {
        {
            const std::lock_guard<std::mutex> lock (mutex);
            auto it = jobs.find (key);

            if (it == jobs.end() || it->second.done)
                return;

            it->second.result = std::move (result);
            it->second.done = true;
        }

        changed.notify_all();
    }

    /** Tira os trabalhos prontos, com quem esperava cada um. Quem chama avisa os Waiters, fora da trava. */
    std::vector<Finished> drain()
    {
        std::vector<Finished> out;
        const std::lock_guard<std::mutex> lock (mutex);

        for (auto it = jobs.begin(); it != jobs.end();)
        {
            if (! it->second.done)
            {
                ++it;
                continue;
            }

            out.push_back ({ it->first, std::move (it->second.result), std::move (it->second.waiters) });
            it = jobs.erase (it);
        }

        return out;
    }

    /** Espera todos os trabalhos pedidos ficarem prontos (não recolhe: depois vem o drain). false = o tempo acabou. */
    bool waitUntilIdle (std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock (mutex);
        return changed.wait_for (lock, timeout, [this] { return countRunning() == 0; });
    }

    /** Esquece tudo (o dono está saindo): o complete() que chegar depois não acha a chave. */
    void cancelAll()
    {
        std::map<Key, Job> dropped;

        {
            const std::lock_guard<std::mutex> lock (mutex);
            dropped.swap (jobs);
        }

        changed.notify_all();
    }   // os Waiters morrem aqui, fora da trava, na thread de quem chamou

    /** Trabalhos pedidos e ainda não recolhidos pelo drain. */
    std::size_t size() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return jobs.size();
    }

    /** Trabalhos que a thread de trabalho ainda não entregou. */
    std::size_t running() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return countRunning();
    }

private:
    struct Job
    {
        Result result {};
        std::vector<Waiter> waiters;
        bool done = false;
    };

    std::size_t countRunning() const   // com a trava
    {
        std::size_t n = 0;

        for (auto& [key, job] : jobs)
            if (! job.done)
                ++n;

        return n;
    }

    mutable std::mutex mutex;
    std::condition_variable changed;
    std::map<Key, Job> jobs;
};

/** O lote que cada dono espera (o sample do canal e as camadas dele), publicado de uma vez só.

    begin() abre um lote e devolve a geração dele; um lote novo do mesmo dono aposenta o anterior. deliver()
    só devolve o lote quando o último item do lote vigente chega: entrega de geração antiga, de dono cancelado
    ou repetida não devolve nada. A geração nunca se repete, nem depois de cancel(): A → B → A não confunde.

    Só a thread de mensagem.
*/
template <typename Owner, typename Result>
class Batches
{
public:
    using Generation = std::uint64_t;

    /** Abre o lote do dono com `numItems` itens (0 = não fica nada pendente) e aposenta o que ele esperava. */
    Generation begin (const Owner& owner, std::size_t numItems)
    {
        const auto generation = ++lastGeneration;
        open.erase (owner);

        if (numItems > 0)
        {
            auto& b = open[owner];
            b.generation = generation;
            b.items.resize (numItems);
            b.arrived.assign (numItems, false);
            b.missing = numItems;
        }

        return generation;
    }

    /** O item `index` do lote chegou. Devolve os itens, na ordem do begin(), se este fechou o lote vigente. */
    std::optional<std::vector<Result>> deliver (const Owner& owner, Generation generation, std::size_t index, Result result)
    {
        auto it = open.find (owner);

        if (it == open.end() || it->second.generation != generation)
            return std::nullopt;   // obsoleto: o dono mudou de pedido ou saiu

        auto& b = it->second;

        if (index >= b.items.size() || b.arrived[index])
            return std::nullopt;

        b.items[index] = std::move (result);
        b.arrived[index] = true;

        if (--b.missing > 0)
            return std::nullopt;

        auto items = std::move (b.items);
        open.erase (it);
        return items;
    }

    /** O dono saiu (canal removido) ou não espera mais nada. true se havia lote aberto. */
    bool cancel (const Owner& owner)             { return open.erase (owner) > 0; }
    void cancelAll()                             { open.clear(); }

    bool isPending (const Owner& owner) const    { return open.count (owner) != 0; }
    std::size_t pending() const noexcept         { return open.size(); }

private:
    struct Batch
    {
        Generation generation = 0;
        std::vector<Result> items;
        std::vector<bool> arrived;
        std::size_t missing = 0;
    };

    std::map<Owner, Batch> open;
    Generation lastGeneration = 0;
};

} // namespace whynot::loads
