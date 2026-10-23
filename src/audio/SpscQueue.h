#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace whynot::audio
{

/** Fila de um produtor e um consumidor, com capacidade fixa.

    push() e pop() não alocam, não travam e não esperam: podem ser chamados da
    thread de áudio. Cada lado deve ser usado por uma única thread.
*/
#if defined (_MSC_VER)
 #pragma warning (push)
 #pragma warning (disable : 4324) // o preenchimento por alignas(64) é proposital: separa os índices em linhas de cache
#endif

template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert (std::is_trivially_copyable_v<T>, "SpscQueue: use tipos copiáveis byte a byte");
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "SpscQueue: capacidade precisa ser potência de 2");
    static_assert (std::atomic<std::size_t>::is_always_lock_free, "SpscQueue: o índice atômico viraria uma trava escondida");

public:
    /** Produtor. Devolve false se a fila estiver cheia (nada é sobrescrito). */
    bool push (const T& item) noexcept
    {
        const auto write = writeIndex.load (std::memory_order_relaxed);

        if (write - readIndex.load (std::memory_order_acquire) == Capacity)
            return false;

        slots[write & (Capacity - 1)] = item;
        writeIndex.store (write + 1, std::memory_order_release);
        return true;
    }

    /** Consumidor. Devolve false se a fila estiver vazia. */
    bool pop (T& item) noexcept
    {
        const auto read = readIndex.load (std::memory_order_relaxed);

        if (read == writeIndex.load (std::memory_order_acquire))
            return false;

        item = slots[read & (Capacity - 1)];
        readIndex.store (read + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> slots {};
    alignas (64) std::atomic<std::size_t> writeIndex { 0 };
    alignas (64) std::atomic<std::size_t> readIndex { 0 };
};

#if defined (_MSC_VER)
 #pragma warning (pop)
#endif

} // namespace whynot::audio
