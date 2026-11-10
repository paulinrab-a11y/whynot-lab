#pragma once

#include <atomic>
#include <cstdint>

namespace whynot::audio
{

/** Pedido de parar amarrado ao que já existia quando ele foi feito.

    Quem toca coisas numeradas em ordem (as prévias do navegador: cada play() ganha um id maior)
    marca aqui "pare tudo até o id N". A thread de áudio pergunta se o id que ela está tocando está
    coberto. Uma coisa pedida depois do stop() tem id maior que a marca e nasce sem o pedido de
    parar, mesmo que a thread de áudio só veja os dois juntos, no mesmo bloco. (Um contador de
    pedidos, como era antes, não sabe a quem o pedido se refere: a prévia nova o lia e nascia já em
    fade-out.)

    - Thread de mensagem: request().
    - Thread de áudio: covers(). Só um atomic sem trava.

    Os ids começam em 1 (0 = nada). A comparação é pela diferença, então segue certa quando o
    contador de 32 bits dá a volta, desde que a marca não fique 2^31 ids para trás (dois bilhões de
    prévias sem nenhum stop()). */
class StopMark
{
public:
    /** Para tudo o que tem id até `newestId` (o maior id já entregue). */
    void request (std::uint32_t newestId) noexcept   { mark.store (newestId, std::memory_order_release); }

    /** O id está coberto por um pedido de parar? */
    bool covers (std::uint32_t id) const noexcept
    {
        return (std::int32_t) (mark.load (std::memory_order_acquire) - id) >= 0;
    }

private:
    static_assert (std::atomic<std::uint32_t>::is_always_lock_free, "a marca viraria uma trava escondida na thread de audio");
    std::atomic<std::uint32_t> mark { 0 };
};

} // namespace whynot::audio
