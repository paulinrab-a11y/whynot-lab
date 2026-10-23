#pragma once

#include "SpscQueue.h"

#include <cstddef>
#include <memory>

namespace whynot::audio
{

/** Entrega objetos grandes (por exemplo, um sample decodificado) para a thread
    de áudio sem que ela aloque ou libere memória.

    - Thread de mensagem: publish() e collectGarbage().
    - Thread de áudio: current(), no começo de cada bloco.

    A thread de áudio só troca ponteiros; o objeto substituído volta por uma
    fila de descarte e é apagado na thread de mensagem. O contador inFlight
    garante que a fila de descarte nunca enche, então a thread de áudio nunca
    precisa decidir o que fazer com um objeto que não coube.

    Destruir o slot só com o áudio parado.
*/
template <typename T>
class RtObjectSlot
{
public:
    static constexpr std::size_t queueSize = 8;

    RtObjectSlot() = default;
    RtObjectSlot (const RtObjectSlot&) = delete;
    RtObjectSlot& operator= (const RtObjectSlot&) = delete;

    ~RtObjectSlot()
    {
        const T* pending = nullptr;

        while (toAudio.pop (pending))
            delete pending;

        collectGarbage();
        delete active;
    }

    /** Thread de mensagem. Devolve false se já houver trocas demais em trânsito;
        nesse caso o objeto continua com quem chamou e basta tentar de novo
        depois que a thread de áudio tiver rodado mais um bloco.

        Com o áudio parado nada é consumido: depois de queueSize - 1 trocas
        tudo falha. Quem chama deve guardar só o objeto pendente mais novo
        ("o mais novo vence") e reenviar quando o áudio voltar.
    */
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

    /** Thread de áudio. Aplica as trocas pendentes e devolve o objeto atual
        (pode ser nullptr). O ponteiro vale até a próxima chamada.
    */
    const T* current() noexcept
    {
        const T* incoming = nullptr;

        while (toAudio.pop (incoming))
        {
            if (active != nullptr)
                garbage.push (active); // nunca falha: inFlight < queueSize

            active = incoming;
        }

        return active;
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
    const T* active = nullptr;   // só a thread de áudio lê e escreve
    std::size_t inFlight = 0;    // só a thread de mensagem lê e escreve
};

} // namespace whynot::audio
