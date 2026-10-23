// Plugins VST3 quebrados de propósito, só para testar o scan isolado da WHYNOT LAB.
// Não são plugins de verdade: exportam as funções que um host VST3 chama ao abrir o arquivo
// e falham nelas. WHYNOT_BROKEN_HANG = trava para sempre; senão, derruba o processo.

extern "C" __declspec (dllimport) void __stdcall Sleep (unsigned long milliseconds);

extern "C" __declspec (dllexport) bool InitDll()   { return true; }
extern "C" __declspec (dllexport) bool ExitDll()   { return true; }

extern "C" __declspec (dllexport) void* GetPluginFactory()
{
#if WHYNOT_BROKEN_HANG
    for (;;)
        Sleep (1000);   // nunca responde: o scan tem que desistir por tempo
#else
    volatile int* nowhere = nullptr;
    *nowhere = 42;      // acesso inválido: derruba quem tentar ler o plugin
    return nullptr;
#endif
}
