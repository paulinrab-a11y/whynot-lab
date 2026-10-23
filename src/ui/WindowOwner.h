#pragma once

namespace whynot
{

/** Faz `window` pertencer a `owner` (handles nativos de janela, getWindowHandle()).
    No Windows, uma janela com dono fica sempre acima dele, some quando ele é minimizado
    e não fica acima dos outros programas. Nos outros sistemas, não faz nada. */
void setNativeWindowOwner (void* window, void* owner);

} // namespace whynot
