#pragma once

#include "DspCommon.h"

#include <array>
#include <vector>

namespace whynot::dsp
{

/** Produto interno do FIR com N coeficientes, com x[0] sendo a amostra mais recente do histórico.

    Passada de desempenho de 10/11. O laço antigo somava um coeficiente por vez numa cadeia só (cada soma
    esperando a anterior), e isso era quase todo o custo do sobreamostrador. Aqui são 4 produtos por instrução
    (SSE, que todo x86 de 64 bits tem) em dois acumuladores, com N fixo na compilação (sem laço de tamanho
    variável: os dois estágios têm tamanhos diferentes e o desvio do laço errava a previsão).

    As amostras mais recentes (as 4 primeiras, mais o resto de N / 4) entram uma a uma, de propósito: elas
    acabaram de ser gravadas no histórico amostra a amostra, e uma leitura de 16 bytes por cima de uma
    gravação de 4 bytes que ainda não saiu do processador tem que esperar a gravação terminar (não há
    "store forwarding"). Medido na ida e volta, contra a versão antiga: 2,1x com a leitura larga desde x[0],
    2,2 a 2,4x deixando 4 de fora, 2,1x deixando 8 (a cadeia escalar fica comprida demais).

    Não lê nada além de c[N - 1] e x[N - 1], não exige alinhamento (loadu) e não aloca. A ordem das somas
    muda em relação à cadeia antiga, então o resultado difere no arredondamento do float (medido no teste,
    com sinal de fundo de escala: -123 dBFS no pior ponto, -144 dBFS rms). dotScalar faz as mesmas somas na mesma ordem, sem SSE: dá o mesmo resultado bit a bit
    (conferido no teste) e é o caminho dos alvos que não são x86. */
namespace fir
{
    /** Quantas amostras do começo entram uma a uma; o resto é múltiplo de 4. */
    constexpr int scalarHead (int n) noexcept   { return n < 8 ? n : 4 + (n & 3); }

    template <int N>
    inline float dotScalar (const float* c, const float* x) noexcept
    {
        constexpr int head = scalarHead (N);
        float recent = 0.0f;

        for (int i = 0; i < head; ++i)
            recent += c[i] * x[i];

        float a[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        for (int i = head, g = 0; i < N; i += 4, ++g)
        {
            float* acc = a + (g & 1) * 4;
            acc[0] += c[i] * x[i];
            acc[1] += c[i + 1] * x[i + 1];
            acc[2] += c[i + 2] * x[i + 2];
            acc[3] += c[i + 3] * x[i + 3];
        }

        return (((a[0] + a[4]) + (a[2] + a[6])) + ((a[1] + a[5]) + (a[3] + a[7]))) + recent;
    }

    template <int N>
    inline float dot (const float* c, const float* x) noexcept
    {
       #if WHYNOT_DSP_X86
        constexpr int head = scalarHead (N);
        float recent = 0.0f;

        for (int i = 0; i < head; ++i)
            recent += c[i] * x[i];

        __m128 a0 = _mm_setzero_ps(), a1 = _mm_setzero_ps();
        int i = head;

        for (; i + 8 <= N; i += 8)
        {
            a0 = _mm_add_ps (a0, _mm_mul_ps (_mm_loadu_ps (c + i), _mm_loadu_ps (x + i)));
            a1 = _mm_add_ps (a1, _mm_mul_ps (_mm_loadu_ps (c + i + 4), _mm_loadu_ps (x + i + 4)));
        }

        if (i < N)
            a0 = _mm_add_ps (a0, _mm_mul_ps (_mm_loadu_ps (c + i), _mm_loadu_ps (x + i)));

        const __m128 v = _mm_add_ps (a0, a1);
        const __m128 pairs = _mm_add_ps (v, _mm_movehl_ps (v, v));                 // [v0 + v2, v1 + v3, ...]
        return _mm_cvtss_f32 (_mm_add_ss (pairs, _mm_shuffle_ps (pairs, pairs, 1))) + recent;
       #else
        return dotScalar<N> (c, x);
       #endif
    }
}

/** Filtro de meia banda FIR de fase linear (janela de Kaiser), usado para subir e descer
    a taxa de amostragem em 2x. Metade dos coeficientes é zero; guardamos só os outros.

    Num meia banda de comprimento ímpar, os coeficientes não nulos são o do centro (0,5 exato) e os de
    distância ímpar do centro, que caem todos na mesma paridade de índice. Esses ficam também em `dense`,
    um ao lado do outro, para o produto interno andar em memória contígua (estrutura polifásica). */
struct HalfbandDesign
{
    struct Tap { int index; float coeff; };

    int numTaps = 0;
    int centre = 0;                  // atraso do filtro, em amostras da taxa alta
    std::vector<Tap> phase[2];       // coeficientes não nulos por paridade do índice

    int densePhase = 0;              // paridade dos coeficientes de distância ímpar do centro
    std::vector<float> dense;        // dense[j] = coeficiente do índice 2j + densePhase
    float centreCoeff = 0.5f;

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

        // Os mesmos coeficientes (mesmos floats), só arrumados para o produto interno contíguo.
        d.densePhase = (d.centre & 1) ^ 1;
        d.centreCoeff = d.phase[d.centre & 1].empty() ? 0.0f : d.phase[d.centre & 1].front().coeff;

        for (auto& t : d.phase[d.densePhase])
            d.dense.push_back (t.coeff);

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

/** Sobe 2x: uma amostra entra, duas saem. NumTaps é fixo na compilação para o produto interno sair sem
    laço de tamanho variável (dois filtros de tamanhos diferentes no mesmo laço erravam a previsão de desvio). */
template <int NumTaps>
class HalfbandUp
{
public:
    static constexpr int centre = (NumTaps - 1) / 2;
    static constexpr int numDense = 2 * ((centre + 1) / 2);   // coeficientes de distância ímpar do centro
    static constexpr int densePhase = (centre & 1) ^ 1;

    void prepare (const HalfbandDesign& d)
    {
        std::copy_n (d.dense.begin(), std::min ((size_t) numDense, d.dense.size()), dense.begin());
        centreCoeff = d.centreCoeff;
        history.prepare (NumTaps / 2 + 2);
    }

    void reset() noexcept   { history.reset(); }

    void process (float x, float& out0, float& out1) noexcept
    {
        history.push (x);
        const float* h = history.recent();

        // y[2n + p] = 2 * soma dos h[k] com k = p (mod 2) vezes x[n - (k - p) / 2]:
        // uma fase é o produto interno com os coeficientes densos, a outra é só o centro (um atraso).
        const float filtered = 2.0f * fir::dot<numDense> (dense.data(), h);
        const float delayed = 2.0f * (centreCoeff * h[centre / 2]);

        out0 = densePhase == 0 ? filtered : delayed;
        out1 = densePhase == 0 ? delayed : filtered;
    }

private:
    std::array<float, (size_t) numDense> dense {};
    float centreCoeff = 0.5f;
    FirHistory history;
};

/** Desce 2x: duas amostras entram, uma sai (a de índice par, para o atraso ser inteiro). */
template <int NumTaps>
class HalfbandDown
{
public:
    static constexpr int centre = (NumTaps - 1) / 2;
    static constexpr int numDense = 2 * ((centre + 1) / 2);
    static constexpr int densePhase = (centre & 1) ^ 1;

    void prepare (const HalfbandDesign& d)
    {
        std::copy_n (d.dense.begin(), std::min ((size_t) numDense, d.dense.size()), dense.begin());
        centreCoeff = d.centreCoeff;
        even.prepare (NumTaps / 2 + 2);
        odd.prepare (NumTaps / 2 + 2);
    }

    void reset() noexcept   { even.reset(); odd.reset(); }

    float process (float in0, float in1) noexcept
    {
        // Dois históricos, um por paridade da amostra de entrada: com in0 já guardado e in1 ainda não, a
        // amostra de atraso k (na taxa alta, 0 = in0) é e[k / 2] se k é par e o[(k - 1) / 2] se é ímpar.
        // Os coeficientes densos caem todos num histórico só, lado a lado, e o centro no outro.
        even.push (in0);
        const float* e = even.recent();
        const float* o = odd.recent();

        const float filtered = fir::dot<numDense> (dense.data(), densePhase == 0 ? e : o);
        const float delayed = centreCoeff * ((centre & 1) != 0 ? o[(centre - 1) / 2] : e[centre / 2]);

        odd.push (in1);
        return filtered + delayed;
    }

private:
    std::array<float, (size_t) numDense> dense {};
    float centreCoeff = 0.5f;
    FirHistory even, odd;
};

/** Sobreamostragem 4x em dois estágios de meia banda (63 e 29 coeficientes).
    Ida e volta atrasam exatamente latency() amostras na taxa original. */
class Oversampler4x
{
public:
    static constexpr int factor = 4;
    static constexpr int taps1 = 63, taps2 = 29;

    void prepare()
    {
        stage1 = HalfbandDesign::make (taps1);
        stage2 = HalfbandDesign::make (taps2);   // centro par: o atraso total fica inteiro

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
    HalfbandUp<taps1> up1;
    HalfbandDown<taps1> down1;
    HalfbandUp<taps2> up2;
    HalfbandDown<taps2> down2;
};

} // namespace whynot::dsp
