#pragma once

// O pouco de Windows que a ponte de controle usa, sem JUCE (testável com `cl`): o aleatório do sistema para
// o token e a conferência de que o processo que abriu o app sem janela ainda existe.

#include <cstddef>
#include <cstdint>

namespace whynot::control
{

/** Bytes do gerador do sistema (BCryptGenRandom). false se o Windows não deu. */
bool secureRandomBytes (std::uint8_t* out, std::size_t size) noexcept;

/** O processo existe e ainda não saiu? */
bool isProcessAlive (std::uint32_t pid) noexcept;

std::uint32_t currentProcessId() noexcept;

} // namespace whynot::control
