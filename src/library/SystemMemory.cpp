#include "SystemMemory.h"

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace whynot::library
{

int availableMemoryMB() noexcept
{
   #if defined(_WIN32)
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof (status);

    if (GlobalMemoryStatusEx (&status) != 0)
        return (int) (status.ullAvailPhys / (1024ull * 1024ull));
   #endif

    return -1;
}

} // namespace whynot::library
