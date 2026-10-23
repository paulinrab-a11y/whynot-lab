#pragma once

// Medidor de CPU e de falhas de buffer (entrega 3.5), sem JUCE (testado em tests/perf).
// A thread de mensagem chama update() a cada 100 ms com o que a placa informa; a tela lê snapshot().

#include <algorithm>
#include <cmath>

namespace whynot::perf
{

/** O que a tela mostra. */
struct Snapshot
{
    float cpu = 0.0f;                 // carga da thread de áudio, 0 a 1 (suavizada)
    float cpuPeak = 0.0f;             // o maior valor dos últimos peakHoldSeconds
    int xruns = 0;                    // falhas de buffer desde o último reset
    double secondsSinceXrun = -1.0;   // -1 = nenhuma desde o reset
    bool overloaded = false;          // CPU acima do limite ou falha nos últimos alertSeconds: a tela acende o aviso
};

class PerformanceMonitor
{
public:
    static constexpr double peakHoldSeconds = 2.0;
    static constexpr double alertSeconds = 3.0;
    static constexpr float overloadCpu = 0.85f;

    /** now: segundos de um relógio qualquer que só anda para a frente. cpuNow: 0 a 1.
        deviceXruns: o contador da placa (só cresce; volta a 0 quando a placa reinicia; < 0 = a placa não informa). */
    void update (double now, float cpuNow, int deviceXruns) noexcept
    {
        if (! std::isfinite (cpuNow) || cpuNow < 0.0f)
            cpuNow = 0.0f;

        // suavização de ~300 ms com chamadas a cada 100 ms; subida na hora, para o pico não sumir
        snap.cpu = cpuNow > snap.cpu ? cpuNow : snap.cpu + 0.3f * (cpuNow - snap.cpu);

        if (cpuNow >= peakValue || now - peakTime > peakHoldSeconds)
        {
            peakValue = cpuNow;
            peakTime = now;
        }

        snap.cpuPeak = peakValue;

        if (deviceXruns >= 0)
        {
            if (lastDeviceXruns < 0 || deviceXruns < lastDeviceXruns)
                lastDeviceXruns = deviceXruns;   // primeira leitura, ou a placa reiniciou o contador

            if (deviceXruns > lastDeviceXruns)
            {
                snap.xruns += deviceXruns - lastDeviceXruns;
                lastXrunTime = now;
            }

            lastDeviceXruns = deviceXruns;
        }

        snap.secondsSinceXrun = lastXrunTime >= 0.0 ? now - lastXrunTime : -1.0;
        snap.overloaded = snap.cpu >= overloadCpu || (lastXrunTime >= 0.0 && now - lastXrunTime <= alertSeconds);
    }

    /** Zera o contador de falhas (o botão da tela). */
    void resetXruns() noexcept
    {
        snap.xruns = 0;
        lastXrunTime = -1.0;
        snap.secondsSinceXrun = -1.0;
    }

    const Snapshot& snapshot() const noexcept   { return snap; }

private:
    Snapshot snap;
    float peakValue = 0.0f;
    double peakTime = 0.0;
    int lastDeviceXruns = -1;
    double lastXrunTime = -1.0;
};

} // namespace whynot::perf
