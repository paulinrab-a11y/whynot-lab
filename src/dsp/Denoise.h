#pragma once

// Limpa-voz: tira o ruído de fundo da voz gravada em casa (ventilador, chiado da placa, rua),
// com um gate espectral numa STFT.

#include "Fft.h"
#include "Processor.h"

#include <array>
#include <complex>
#include <vector>

namespace whynot::dsp
{

/** - STFT de 1024 pontos (2048 a 96 kHz) com 75% de sobreposição e janela raiz de Hann na ida e na
      volta: com a redução em 0 dB a saída é a entrada atrasada, amostra por amostra.
    - Os dois canais vão numa FFT complexa só (esquerdo na parte real, direito na imaginária): o ganho
      de cada faixa é real e simétrico, então vale para os dois.
    - O ruído de cada faixa é estimado sozinho: o mínimo da energia suavizada em janelas de 1,5 s
      (estatística de mínimos), com uma folga de 6 dB. Ele desce na hora; onde o som está 10 dB acima
      dele, sobe no máximo 4 dB por segundo: uma nota longa (sem pausa) não vira "ruído" e não é comida.
    - Cada faixa abaixo de "sensibilidade" dB acima do ruído desce até a redução; entre o limiar e
      6 dB acima dele, a passagem é suave. Os ganhos abrem na hora, fecham com a suavidade e são
      alisados entre faixas vizinhas, para não sobrar o "ruído aquático".
    - Latência: o tamanho da FFT (1024 amostras a 48 kHz, 21 ms), informada ao Tracktion. */
class DenoiseProcessor final : public Processor
{
public:
    enum Param { reduction, threshold, smoothing, listen, count };
    enum Meter { reductionMeter, noiseMeter };

    static const FxInfo& staticInfo();
    const FxInfo& info() const noexcept override   { return staticInfo(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParam (int index, float value) noexcept override;
    void process (AudioBlock&) noexcept override;
    int latencySamples() const noexcept override   { return fftSize; }
    double tailSecondsFor (const float*) const noexcept override   { return 0.05; }

    static int fftSizeFor (double sampleRate) noexcept;

private:
    void processFrame() noexcept;

    double sampleRate = 48000.0;
    std::array<float, count> values {};
    int fftSize = 1024, hop = 256, bins = 513, pos = 0;
    int framesPerWindow = 282, frameCounter = 0;
    float riseLimit = 1.0f;   // quanto a estimativa de ruído pode subir por quadro

    Fft fft;
    std::vector<float> window, inL, inR, accL, accR, readyL, readyR;
    std::vector<std::complex<float>> spectrum;
    std::vector<float> smoothedPower, minCurrent, minPrevious, noise, gains, scratch;
    bool firstFrame = true;
};

} // namespace whynot::dsp
