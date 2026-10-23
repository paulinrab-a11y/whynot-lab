#include "WindowsPower.h"

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace whynot
{

bool keepFullCpuSpeed() noexcept
{
   #if defined(_WIN32)
    // ControlMask diz quais mecanismos o app controla; StateMask = 0 desliga todos eles.
    // EXECUTION_SPEED: EcoQoS (núcleo de eficiência, frequência baixa).
    // IGNORE_TIMER_RESOLUTION: o Windows 11 ignora a resolução de timer pedida por janela
    // minimizada; desligado, os timers da interface e do motor continuam precisos.
    PROCESS_POWER_THROTTLING_STATE state {};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;

   #ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
    state.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
   #endif

    return SetProcessInformation (GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof (state)) != FALSE;
   #else
    return true;
   #endif
}

} // namespace whynot
