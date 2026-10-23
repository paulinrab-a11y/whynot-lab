#include "ControlPlatform.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <bcrypt.h>
 #pragma comment (lib, "bcrypt.lib")
#else
 #include <fstream>
 #include <signal.h>
 #include <unistd.h>
#endif

namespace whynot::control
{

bool secureRandomBytes (std::uint8_t* out, std::size_t size) noexcept
{
   #if defined (_WIN32)
    while (size > 0)
    {
        const auto chunk = (ULONG) (size > 0x10000 ? 0x10000 : size);

        if (! BCRYPT_SUCCESS (BCryptGenRandom (nullptr, out, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
            return false;

        out += chunk;
        size -= chunk;
    }

    return true;
   #else
    std::ifstream urandom ("/dev/urandom", std::ios::binary);
    return urandom.read (reinterpret_cast<char*> (out), (std::streamsize) size).good();
   #endif
}

bool isProcessAlive (std::uint32_t pid) noexcept
{
   #if defined (_WIN32)
    if (pid == 0)
        return false;

    const HANDLE h = OpenProcess (SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);

    if (h == nullptr)
        return false;

    const bool alive = WaitForSingleObject (h, 0) == WAIT_TIMEOUT;
    CloseHandle (h);
    return alive;
   #else
    return pid != 0 && kill ((pid_t) pid, 0) == 0;
   #endif
}

std::uint32_t currentProcessId() noexcept
{
   #if defined (_WIN32)
    return (std::uint32_t) GetCurrentProcessId();
   #else
    return (std::uint32_t) getpid();
   #endif
}

} // namespace whynot::control
