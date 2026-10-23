#pragma once

// FFT complexa de raiz 2, sem JUCE (os testes de DSP compilam sozinhos). Aloca só em prepare().

#include "DspCommon.h"

#include <complex>
#include <vector>

namespace whynot::dsp
{

class Fft
{
public:
    /** size: potência de 2. */
    void prepare (int size)
    {
        n = size;
        twiddles.resize ((size_t) n / 2);

        for (int k = 0; k < n / 2; ++k)
            twiddles[(size_t) k] = std::polar (1.0f, (float) (-2.0 * pi * k / n));

        reversed.resize ((size_t) n);
        int bits = 0;

        while ((1 << bits) < n)
            ++bits;

        for (int i = 0; i < n; ++i)
        {
            int r = 0;

            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);

            reversed[(size_t) i] = r;
        }
    }

    int size() const noexcept   { return n; }

    /** No lugar. inverse = true devolve a transformada inversa já dividida por n. */
    void transform (std::complex<float>* x, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int r = reversed[(size_t) i];

            if (r > i)
                std::swap (x[i], x[r]);
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, stride = n / len;

            for (int start = 0; start < n; start += len)
                for (int k = 0; k < half; ++k)
                {
                    // Multiplicação escrita à mão: sem as checagens de NaN do operador da biblioteca.
                    const auto& t = twiddles[(size_t) (k * stride)];
                    const float wr = t.real(), wi = inverse ? -t.imag() : t.imag();
                    const auto a = x[start + k];
                    const auto& c = x[start + k + half];
                    const std::complex<float> b (c.real() * wr - c.imag() * wi, c.real() * wi + c.imag() * wr);
                    x[start + k] = a + b;
                    x[start + k + half] = a - b;
                }
        }

        if (inverse)
        {
            const float scale = 1.0f / (float) n;

            for (int i = 0; i < n; ++i)
                x[i] *= scale;
        }
    }

private:
    int n = 0;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> reversed;
};

} // namespace whynot::dsp
