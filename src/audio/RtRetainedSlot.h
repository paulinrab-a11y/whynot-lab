#pragma once

#include "SpscQueue.h"

#include <array>
#include <cstddef>
#include <memory>

namespace whynot::audio
{

/** Como o RtObjectSlot, mas o objeto substituído não vai direto para o descarte: fica "aposentado"
    na thread de áudio enquanto ela disser que ainda o usa (uma voz do sampler que continua lendo o
    sample velho depois da troca) e só então volta para a thread de mensagem apagar.

    - Thread de mensagem: publish() e collectGarbage().
    - Thread de áudio: current() no começo do bloco e releaseUnused() no fim dele.

    A thread de áudio só mexe em ponteiros: não aloca nem libera. O contador inFlight (objetos
    publicados e ainda não apagados: o ativo, os aposentados e os das duas filas) fica abaixo de
    queueSize, então nem a lista de aposentados nem a fila de descarte enchem.

    Enquanto um aposentado estiver em uso ele conta no inFlight: com queueSize - 1 objetos presos,
    publish() devolve false até a thread de áudio soltar algum. Quem usa deve limitar por quanto
    tempo segura um aposentado (o sampler tira em 3 ms as vozes de dois samples atrás).

    Destruir o slot só com o áudio parado.
*/
template <typename T>
class RtRetainedSlot
{
public:
    static constexpr std::size_t queueSize = 8;

    RtRetainedSlot() = default;
    RtRetainedSlot (const RtRetainedSlot&) = delete;
    RtRetainedSlot& operator= (const RtRetainedSlot&) = delete;

    ~RtRetainedSlot()
    {
        const T* pending = nullptr;

        while (toAudio.pop (pending))
            delete pending;

        collectGarbage();

        for (std::size_t i = 0; i < numRetired; ++i)
            delete retired[i];

        delete active;
    }

    /** Thread de mensagem. Devolve false se já houver objetos demais em trânsito ou presos; nesse
        caso o objeto continua com quem chamou ("o mais novo vence": guarde só o pendente mais novo
        e tente de novo num Timer). */
    bool publish (std::unique_ptr<const T>& next) noexcept
    {
        collectGarbage();

        if (next == nullptr || inFlight >= queueSize - 1)
            return false;

        if (! toAudio.push (next.get()))
            return false;

        next.release();
        ++inFlight;
        return true;
    }

    /** Thread de áudio. Aplica as trocas pendentes e devolve o objeto atual (pode ser nullptr).
        Cada objeto substituído entra na lista de aposentados, do mais velho para o mais novo, e
        continua válido até um releaseUnused() em que ninguém o use. */
    const T* current() noexcept
    {
        const T* incoming = nullptr;

        while (toAudio.pop (incoming))
        {
            if (active != nullptr)
                retired[numRetired++] = active;   // cabe: ativo + aposentados <= inFlight < queueSize

            active = incoming;
        }

        return active;
    }

    /** Thread de áudio: quantos objetos substituídos ainda estão guardados. */
    int numRetiredObjects() const noexcept   { return (int) numRetired; }

    /** Thread de áudio: o aposentado mais novo (o que o objeto atual substituiu, se ainda está
        guardado), ou nullptr. */
    const T* newestRetired() const noexcept   { return numRetired > 0 ? retired[numRetired - 1] : nullptr; }

    /** Thread de áudio, uma vez por bloco (depois de renderizar): manda para o descarte os
        aposentados que `inUse (const T*)` diz que ninguém mais lê. Depois disto a thread de áudio
        não pode mais tocar neles. */
    template <typename InUse>
    void releaseUnused (InUse&& inUse) noexcept
    {
        std::size_t kept = 0;

        for (std::size_t i = 0; i < numRetired; ++i)
        {
            if (inUse (retired[i]))
                retired[kept++] = retired[i];
            else
                garbage.push (retired[i]);   // nunca falha: inFlight < queueSize
        }

        numRetired = kept;
    }

    /** Thread de mensagem, periodicamente (um Timer de 100 ms basta). */
    void collectGarbage() noexcept
    {
        const T* old = nullptr;

        while (garbage.pop (old))
        {
            delete old;
            --inFlight;
        }
    }

private:
    SpscQueue<const T*, queueSize> toAudio;
    SpscQueue<const T*, queueSize> garbage;
    const T* active = nullptr;                       // só a thread de áudio
    std::array<const T*, queueSize> retired {};      // só a thread de áudio
    std::size_t numRetired = 0;                      // só a thread de áudio
    std::size_t inFlight = 0;                        // só a thread de mensagem
};

} // namespace whynot::audio
