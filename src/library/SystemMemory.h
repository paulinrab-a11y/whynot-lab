#pragma once

namespace whynot::library
{

/** Memória física livre agora, em MB (-1 = não deu para saber). A fila de prévias espera quando falta:
    o render carrega os plugins do projeto uma segunda vez. */
int availableMemoryMB() noexcept;

} // namespace whynot::library
