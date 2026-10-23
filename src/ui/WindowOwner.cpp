// Arquivo à parte, sem JUCE: o <windows.h> fica isolado aqui.

#include "WindowOwner.h"

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace whynot
{

void setNativeWindowOwner (void* window, void* owner)
{
#if defined(_WIN32)
    if (window != nullptr && owner != nullptr)
        SetWindowLongPtrW (static_cast<HWND> (window), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR> (owner));
#else
    (void) window;
    (void) owner;
#endif
}

} // namespace whynot
