#pragma once

#include "DspCommon.h"

#include <array>
#include <vector>

namespace whynot::dsp
{

/** Filtro de meia banda FIR de fase linear (janela de Kaiser), usado para subir e descer
    a taxa de amostragem em 2x. Metade dos coeficientes é zero; guardamos só os outros. */
struct HalfbandDesign
{
    struct Tap { int index; float coeff; };

    int numTaps = 0;
    int centre = 0;                  // atraso do filtro, em amostras da taxa alta
    std::vector<Tap> phase[2];       // coeficientes não nulos por paridade do índice

    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;

            if (term < sum * 1.0e-12)
                break;
        }

        return sum;
    }

    static HalfbandDesign make (int taps, double beta = 8.0)
    {
        HalfbandDesign d;
        d.numTaps = taps;
        d.centre = (taps - 1) / 2;

        std::vector<double> h ((size_t) taps, 0.0);
        double sums[2] = { 0.0, 0.0 };

        for (int n = 0; n < taps; ++n)
        {
            const int offset = n - d.centre;

            if (offset != 0 && offset % 2 == 0)
                continue;   // zero exato num meia banda

            const double x = 0.5 * offset;
            const double sinc = offset == 0 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double r = 2.0 * n / (taps - 1) - 1.0;
            const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            h[(size_t) n] = 0.5 * sinc * window;
            sums[n & 1] += h[(size_t) n];
        }

        // Cada fase soma exatamente 0,5: sem ondulação de DC entre amostras pares e ímpares.
        for (int n = 0; n < taps; ++n)
        {
            if (h[(size_t) n] == 0.0)
                continue;

            d.phase[n & 1].push_back ({ n, (float) (h[(size_t) n] * 0.5 / sums[n & 1]) });
        }

        return d;
    }
};

/** Histórico circular simples, sem módulo: escreve duas vezes para ler contíguo. */
class FirHistory
{
public:
    void prepare (int length)
    {
        size = length;
        data.assign ((size_t) length * 2, 0.0f);
        pos = 0;
    }

    void reset() noexcept   { std::fill (data.begin(), data.end(), 0.0f); pos = 0; }

    void push (float x) noexcept
    {
        pos = pos == 0 ? size - 1 : pos - 1;
        data[(size_t) pos] = x;
        data[(size_t) (pos + size)] = x;
    }

    /** ago = 0 é a mais recente. */
    const float* recent() const noexcept   { return data.data() + pos; }

private:
    std::vector<float> data;
    int size = 0, pos = 0;
};

/** Sobe 2x: uma amostra entra, duas saem. */
class HalfbandUp
{
public:
    void prepare (const HalfbandDesign& d)
    {
        design = &d;
        history.prepare (d.numTaps / 2 + 2);
    }

    void reset() noexcept   { history.reset(); }

    void process (float x, float& out0, float& out1) noexcept
    {
        history.push (x);
        const float* h = history.recent();
        float acc[2] = { 0.0f, 0.0f };

        // y[2n + p] = 2 * soma dos h[k] com k = p (mod 2) vezes x[n - (k - p) / 2]
        for (int p = 0; p < 2; ++p)
            for (auto& t : design->phase[p])
                acc[p] += t.coeff * h[(t.index - p) / 2];

        out0 = 2.0f * acc[0];
        out1 = 2.0f * acc[1];
    }

private:
    const HalfbandDesign* design = nullptr;
    FirHistory history;
};

/** Desce 2x: duas amostras entram, uma sai (a de índice par, para o atraso ser inteiro). */
class HalfbandDown
{
public:
    void prepare (const HalfbandDesign& d)
    {
        design = &d;
        history.prepare (d.numTaps + 2);
    }

    void reset() noexcept   { history.reset(); }

    float process (float in0, float in1) noexcept
    {
        history.push (in0);
        const float* h = history.recent();
        float acc = 0.0f;

        for (int p = 0; p < 2; ++p)
            for (auto& t : design->phase[p])
                acc += t.coeff * h[t.index];

        history.push (in1);
        return acc;
    }

private:
    const HalfbandDesign* design = nullptr;
    FirHistory history;
};

/** Sobreamostragem 4x em dois estágios de meia banda (63 e 29 coeficientes).
    Ida e volta atrasam exatamente latency() amostras na taxa original. */
class Oversampler4x
{
public:
    static constexpr int factor = 4;

    void prepare()
    {
        stage1 = HalfbandDesign::make (63);
        stage2 = HalfbandDesign::make (29);   // centro par: o atraso total fica inteiro

        up1.prepare (stage1);
        down1.prepare (stage1);

        up2.prepare (stage2);
        down2.prepare (stage2);
    }

    void reset() noexcept
    {
        up1.reset();
        down1.reset();

        up2.reset();
        down2.reset();
    }

    /** Amostras de atraso da ida e volta, na taxa original. */
    int latency() const noexcept   { return stage1.centre + stage2.centre / 2; }

    /** Só a ida: quatro amostras sobreamostradas por amostra (medidor de pico real). */
    void upsample (float x, float* out4) noexcept
    {
        float a0, a1;
        up1.process (x, a0, a1);
        up2.process (a0, out4[0], out4[1]);
        up2.process (a1, out4[2], out4[3]);
    }

    /** Atraso só da ida, na taxa original: as 4 amostras de upsample() cobrem o instante
        x[n - upsampleLatency()] até 3/4 de amostra depois. */
    int upsampleLatency() const noexcept   { return (2 * stage1.centre + stage2.centre) / 4; }

    /** Ida, `shape` em cada uma das 4 amostras, e volta. */
    template <typename Shape>
    float process (float x, Shape&& shape) noexcept
    {
        float a[2];
        up1.process (x, a[0], a[1]);

        float back[2];

        for (int i = 0; i < 2; ++i)
        {
            float b0, b1;
            up2.process (a[i], b0, b1);
            back[i] = down2.process (shape (b0), shape (b1));
        }

        return down1.process (back[0], back[1]);
    }

private:
    HalfbandDesign stage1, stage2;
    HalfbandUp up1;
    HalfbandDown down1;
    HalfbandUp up2;
    HalfbandDown down2;
};

} // namespace whynot::dsp
