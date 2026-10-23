#include "KeyDetect.h"

#include "Theory.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

namespace whynot::ideas
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double targetRate = 11025.0;
    constexpr int fftSize = 4096;
    constexpr int hopSize = 512;
    constexpr int timeMedian = 17;      // quadros (~0,8 s): o que dura isso é nota
    constexpr int freqMedian = 17;      // bins (~45 Hz): o que se espalha isso é batida
    constexpr double minHz = 30.0, maxHz = 5000.0;
    constexpr double fullLowHz = 35.0, bassHighHz = 220.0;
    constexpr float prominence = 3.0f;  // pico ~10 dB acima da mediana em volta: nota, não ruído

    //==========================================================================
    // FFT real de fftSize pontos por uma complexa de fftSize/2 (radix 2, iterativa).
    class RealFft
    {
    public:
        explicit RealFft (int n) : size (n), half (n / 2), twiddle ((size_t) half / 2), post ((size_t) half), rev ((size_t) half), buf ((size_t) half)
        {
            for (int k = 0; k < half / 2; ++k)
                twiddle[(size_t) k] = std::polar (1.0f, (float) (-2.0 * pi * k / half));

            for (int k = 0; k < half; ++k)
                post[(size_t) k] = std::polar (1.0f, (float) (-2.0 * pi * k / size));

            int bits = 0;

            while ((1 << bits) < half)
                ++bits;

            for (int i = 0; i < half; ++i)
            {
                int r = 0;

                for (int b = 0; b < bits; ++b)
                    if (i & (1 << b))
                        r |= 1 << (bits - 1 - b);

                rev[(size_t) i] = r;
            }
        }

        /** Módulo dos bins 0..size/2 de x (size amostras). */
        void magnitudes (const float* x, float* out)
        {
            for (int m = 0; m < half; ++m)
                buf[(size_t) rev[(size_t) m]] = { x[2 * m], x[2 * m + 1] };

            for (int len = 2; len <= half; len <<= 1)
            {
                const int step = half / len;

                for (int i = 0; i < half; i += len)
                    for (int j = 0; j < len / 2; ++j)
                    {
                        const auto w = twiddle[(size_t) (j * step)];
                        auto& a = buf[(size_t) (i + j)];
                        auto& b = buf[(size_t) (i + j + len / 2)];
                        const auto t = w * b;
                        b = a - t;
                        a = a + t;
                    }
            }

            const auto z0 = buf[0];
            out[0] = std::abs (z0.real() + z0.imag());
            out[half] = std::abs (z0.real() - z0.imag());

            for (int k = 1; k < half; ++k)
            {
                const auto zk = buf[(size_t) k];
                const auto zc = std::conj (buf[(size_t) (half - k)]);
                const auto even = (zk + zc) * 0.5f;
                const auto odd = (zk - zc) * std::complex<float> (0.0f, -0.5f);
                out[k] = std::abs (even + post[(size_t) k] * odd);
            }
        }

    private:
        int size, half;
        std::vector<std::complex<float>> twiddle, post;
        std::vector<int> rev;
        std::vector<std::complex<float>> buf;
    };


    /** Janela ordenada de N valores que desliza: tira um, põe outro, e a mediana fica no meio.
        Bem mais rápida que um nth_element por bin (a janela anda um passo de cada vez). */
    template <int N>
    struct SortedWindow
    {
        std::array<float, N> v {};

        void replace (float out, float in) noexcept
        {
            int i = 0;

            while (i < N - 1 && v[(size_t) i] != out)
                ++i;

            // tira o que sai e desliza o que for preciso para o que entra caber em ordem
            while (i > 0 && v[(size_t) i - 1] > in)
            {
                v[(size_t) i] = v[(size_t) i - 1];
                --i;
            }

            while (i < N - 1 && v[(size_t) i + 1] < in)
            {
                v[(size_t) i] = v[(size_t) i + 1];
                ++i;
            }

            v[(size_t) i] = in;
        }

        float median() const noexcept   { return v[(size_t) N / 2]; }
    };

    struct Peak
    {
        float hz, amp;
    };

    //==========================================================================
    // Perfis de tom, do Dó ao Si a partir da tônica.
    const double kkMajor[] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    const double kkMinor[] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    // Temperley (2001), do corpus de Kostka e Payne.
    const double tpMajor[] = { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 };
    const double tpMinor[] = { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 };

    double correlation (const Chroma& a, int root, const double* profile)
    {
        double ma = 0.0, mb = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            ma += a[(size_t) pitchClass (root + i)];
            mb += profile[i];
        }

        ma /= 12.0;
        mb /= 12.0;
        double num = 0.0, da = 0.0, db = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            const double x = a[(size_t) pitchClass (root + i)] - ma, y = profile[i] - mb;
            num += x * y;
            da += x * x;
            db += y * y;
        }

        return da > 0.0 && db > 0.0 ? num / std::sqrt (da * db) : 0.0;
    }

    double sumOf (const Chroma& c)
    {
        double s = 0.0;

        for (float v : c)
            s += v;

        return s;
    }

    void normaliseMax (Chroma& c)
    {
        const float m = *std::max_element (c.begin(), c.end());

        if (m > 0.0f)
            for (auto& v : c)
                v /= m;
    }

    bool isDiatonic (int chordRoot, bool chordMinor, const KeyCandidate& key)
    {
        // as tríades do tom (menor natural, mais o V maior da harmônica)
        const int rel = pitchClass (chordRoot - key.root);

        if (key.major)
            return chordMinor ? (rel == 2 || rel == 4 || rel == 9) : (rel == 0 || rel == 5 || rel == 7);

        return chordMinor ? (rel == 0 || rel == 5 || rel == 7) : (rel == 3 || rel == 8 || rel == 10 || rel == 7);
    }

    struct Triad
    {
        bool found = false;
        int root = 0;
        bool minor = false;
        double confidence = 0.0;   // 0..1, entre as 24 tríades
    };

    /** A tríade maior ou menor que melhor explica um cromagrama: semelhança com o molde (fundamental,
        terça e quinta), mais o grave na fundamental e um empurrão pequeno para os acordes do tom. */
    Triad bestTriad (const Chroma& c, const Chroma& bass, const KeyCandidate* key)
    {
        Triad t;
        double norm = 0.0;

        for (float v : c)
            norm += (double) v * v;

        norm = std::sqrt (norm);

        if (norm <= 0.0)
            return t;

        const float bassMax = *std::max_element (bass.begin(), bass.end());
        constexpr double tRoot = 1.0, tThird = 1.0, tFifth = 0.8;
        const double tNorm = std::sqrt (tRoot * tRoot + tThird * tThird + tFifth * tFifth);
        std::array<double, 24> scores {};
        double best = -10.0;
        int bestIndex = 0;

        for (int root = 0; root < 12; ++root)
            for (int q = 0; q < 2; ++q)
            {
                const bool minor = q == 1;
                const double dot = tRoot * c[(size_t) root] + tThird * c[(size_t) pitchClass (root + (minor ? 3 : 4))]
                                 + tFifth * c[(size_t) pitchClass (root + 7)];
                double s = dot / (norm * tNorm);

                if (bassMax > 0.0f)
                    s += 0.12 * bass[(size_t) root] / bassMax;

                if (key != nullptr && isDiatonic (root, minor, *key))
                    s += 0.03;

                scores[(size_t) (root * 2 + q)] = s;

                if (s > best)
                {
                    best = s;
                    bestIndex = root * 2 + q;
                }
            }

        constexpr double temperature = 0.03;
        double total = 0.0;

        for (double s : scores)
            total += std::exp ((s - best) / temperature);

        t.found = true;
        t.root = bestIndex / 2;
        t.minor = bestIndex % 2 == 1;
        t.confidence = 1.0 / total;
        return t;
    }
}

//==============================================================================
const KeyCandidate* BeatAnalysis::find (int root, bool major) const noexcept
{
    for (auto& k : keys)
        if (k.root == pitchClass (root) && k.major == major)
            return &k;

    return nullptr;
}

namespace
{
    struct WeightedTriad
    {
        Triad triad;
        double weight = 0.0;
    };

    /** Quanto um acorde é "da casa" num tom: as funções comuns pesam mais, e acorde de fora pesa contra.
        É o que separa o I IV V IV de Lá maior (o perfil sozinho puxa para Ré maior, com o IV duas vezes)
        e o i VII VI VII de Lá menor (o perfil puxa para Sol maior, com o VII duas vezes). */
    double functionWeight (const Triad& t, int keyRoot, bool keyMajor)
    {
        const int rel = pitchClass (t.root - keyRoot);

        if (keyMajor)
        {
            if (! t.minor)
                switch (rel) { case 0: return 1.0; case 7: return 0.9; case 5: return 0.8; case 10: return 0.1; default: return -0.3; }

            switch (rel) { case 9: return 0.7; case 2: return 0.6; case 4: return 0.45; case 5: return 0.25; default: return -0.3; }
        }

        if (t.minor)
            switch (rel) { case 0: return 1.0; case 5: return 0.75; case 7: return 0.6; default: return -0.3; }

        switch (rel) { case 7: case 8: case 10: return 0.8; case 3: return 0.7; case 1: return 0.3; case 5: return 0.25; default: return -0.3; }
    }

    std::vector<KeyCandidate> rankKeys (const BeatAnalysis& a, const std::vector<WeightedTriad>& chords)
    {
        std::vector<KeyCandidate> keys;
        const double bassSum = sumOf (a.bassProfile);
        double chordWeight = 0.0;

        for (auto& c : chords)
            chordWeight += c.weight;

        for (int root = 0; root < 12; ++root)
            for (bool major : { false, true })
            {
                double s = 0.5 * (correlation (a.profile, root, major ? kkMajor : kkMinor)
                                  + correlation (a.profile, root, major ? tpMajor : tpMinor));

                // os acordes que tocam, pela função de cada um neste tom
                if (chordWeight > 0.0)
                {
                    double fit = 0.0;

                    for (auto& c : chords)
                        fit += c.weight * functionWeight (c.triad, root, major);

                    s = 0.5 * s + 0.5 * fit / chordWeight;
                }

                // o grave (808, baixo) na tônica
                if (bassSum > 0.0)
                    s += 0.10 * a.bassProfile[(size_t) root] / bassSum;

                keys.push_back ({ root, major, s, 0.0 });
            }

        // O relativo (Lá menor e Dó maior) tem as mesmas notas e os mesmos acordes: Am F C G e C G Am F são
        // o mesmo laço, começando em outro lugar. Quem decide é o acorde que abre o beat. Só entre o melhor
        // e o relativo dele, para um começo no IV não virar outro tom.
        if (const auto open = bestTriad (a.opening, a.openingBass, nullptr); open.found)
        {
            auto top = std::max_element (keys.begin(), keys.end(), [] (const KeyCandidate& x, const KeyCandidate& y) { return x.score < y.score; });
            const auto rel = relativeOf (*top);
            auto relIt = std::find (keys.begin(), keys.end(), rel);
            const auto isTonic = [&open] (const KeyCandidate& k) { return open.root == k.root && open.minor == ! k.major; };

            if (relIt != keys.end() && isTonic (*relIt) && ! isTonic (*top) && top->score - relIt->score < 0.35)
                relIt->score = top->score + 0.06 * open.confidence;
            else if (isTonic (*top))
                top->score += 0.04 * open.confidence;
        }

        double best = -10.0;

        for (auto& k : keys)
            best = std::max (best, k.score);

        constexpr double temperature = 0.05;
        double total = 0.0;

        for (auto& k : keys)
            total += (k.probability = std::exp ((k.score - best) / temperature));

        for (auto& k : keys)
            k.probability /= total;

        std::stable_sort (keys.begin(), keys.end(), [] (const KeyCandidate& x, const KeyCandidate& y) { return x.score > y.score; });
        return keys;
    }

    /** Acordes em janelas de ~1 s (passo de 0,5 s), cada um pesado pela força e pela confiança. */
    std::vector<WeightedTriad> windowChords (const BeatAnalysis& a)
    {
        std::vector<WeightedTriad> out;
        const auto n = a.chroma.size();
        const auto len = (std::size_t) std::max (1.0, std::round (1.0 / a.hopSeconds));
        const auto step = std::max<std::size_t> (1, len / 2);

        for (std::size_t s = 0; s + len <= n; s += step)
        {
            Chroma c {}, bass {};

            for (std::size_t i = s; i < s + len; ++i)
                for (int k = 0; k < 12; ++k)
                {
                    c[(size_t) k] += a.chroma[i][(size_t) k];
                    bass[(size_t) k] += a.bass[i][(size_t) k];
                }

            const double energy = sumOf (c);
            const auto t = bestTriad (c, bass, nullptr);

            if (t.found && energy > 0.0)
                out.push_back ({ t, std::sqrt (energy) * t.confidence });
        }

        return out;
    }
}

//==============================================================================
Downsampler::Downsampler (double inputRate)
    : factor (std::max (1, (int) std::lround (inputRate / targetRate))), rate (inputRate / factor)
{
    if (factor <= 1)
        return;

    // passa-baixas: seno janelado de Blackman, corte em 90% do novo Nyquist
    const int taps = 16 * factor + 1;
    mid = taps / 2;
    const double fc = 0.45 / factor;
    h.resize ((size_t) taps);
    double sum = 0.0;

    for (int i = 0; i < taps; ++i)
    {
        const double t = i - mid;
        const double sinc = t == 0 ? 2.0 * fc : std::sin (2.0 * pi * fc * t) / (pi * t);
        const double w = 0.42 - 0.5 * std::cos (2.0 * pi * i / (taps - 1)) + 0.08 * std::cos (4.0 * pi * i / (taps - 1));
        h[(size_t) i] = (float) (sinc * w);
        sum += sinc * w;
    }

    for (auto& v : h)
        v = (float) (v / sum);
}

void Downsampler::push (const float* x, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i)   // arquivo estragado (NaN, infinito) não pode estragar as janelas ordenadas
        buffer.push_back (std::isfinite (x[i]) ? x[i] : 0.0f);

    count += n;
    produce (false);
}

void Downsampler::finish()
{
    produce (true);
    buffer.clear();
    buffer.shrink_to_fit();
}

void Downsampler::produce (bool final)
{
    if (factor <= 1)
    {
        out.insert (out.end(), buffer.begin(), buffer.end());
        bufferStart += (long long) buffer.size();
        buffer.clear();
        return;
    }

    const auto total = (long long) count;

    for (;; ++next)
    {
        const long long centre = next * factor;

        if (final ? centre >= total : centre + mid >= total)
            break;

        double acc = 0.0;

        for (int i = 0; i < (int) h.size(); ++i)
        {
            const long long k = centre + i - mid;

            if (k >= 0 && k < total)
                acc += (double) h[(size_t) i] * buffer[(size_t) (k - bufferStart)];
        }

        out.push_back ((float) acc);
    }

    // guarda só o que as próximas saídas ainda usam
    const long long keepFrom = std::max (bufferStart, next * factor - mid);

    if (keepFrom > bufferStart)
    {
        buffer.erase (buffer.begin(), buffer.begin() + (std::ptrdiff_t) std::min<long long> (keepFrom - bufferStart, (long long) buffer.size()));
        bufferStart = keepFrom;
    }
}

//==============================================================================
BeatAnalysis analyzeBeat (const float* mono, std::size_t numSamples, double sampleRate,
                          const KeyDetectOptions& options, const std::atomic<bool>* cancel)
{
    if (mono == nullptr || numSamples == 0 || ! (sampleRate > 1000.0))
        return {};

    const auto first = (std::size_t) std::clamp (options.startSeconds * sampleRate, 0.0, (double) numSamples);
    const auto count = std::min (numSamples - first, (std::size_t) std::max (0.0, options.maxSeconds * sampleRate));
    Downsampler down (sampleRate);
    down.push (mono + first, count);
    down.finish();
    return analyzeDownsampled (std::move (down.samples()), down.outputRate(), options.startSeconds, (double) count / sampleRate, cancel);
}

BeatAnalysis analyzeDownsampled (std::vector<float> x, double rate, double startSeconds, double inputSeconds,
                                 const std::atomic<bool>* cancel)
{
    BeatAnalysis out;
    const auto cancelled = [cancel] { return cancel != nullptr && cancel->load (std::memory_order_relaxed); };

    out.analysisRate = rate;
    out.hopSeconds = hopSize / std::max (1.0, rate);
    out.startSeconds = startSeconds;
    out.seconds = inputSeconds;

    if (! (rate > 1000.0))
        return out;

    if (x.size() < (std::size_t) hopSize || cancelled())
        return out;

    // 2. STFT com meio quadro de silêncio nas pontas: o centro do quadro i fica em i * passo
    const int maxBin = std::min (fftSize / 2 - 2, (int) std::floor (maxHz * fftSize / rate));
    const int minBin = std::max (1, (int) std::floor (minHz * fftSize / rate));
    const int bins = maxBin + 2;
    const int numFrames = (int) (x.size() / hopSize) + 1;

    std::vector<float> window ((size_t) fftSize);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / fftSize));

    RealFft fft (fftSize);
    std::vector<float> frame ((size_t) fftSize), spectrum ((size_t) fftSize / 2 + 1);
    std::vector<std::vector<float>> ring ((size_t) timeMedian, std::vector<float> ((size_t) bins, 0.0f));
    std::vector<SortedWindow<timeMedian>> timeWindows ((size_t) bins);   // por bin, os últimos timeMedian quadros
    std::vector<float> incoming ((size_t) bins);
    std::vector<std::vector<Peak>> peaks ((size_t) numFrames);
    std::vector<float> harmonic ((size_t) bins), percussive ((size_t) bins);
    double tonalEnergy = 0.0, totalEnergy = 0.0;
    const float scale = 4.0f / fftSize;   // seno de amplitude 1 → pico perto de 1

    auto computeFrame = [&] (int i, std::vector<float>& mag)
    {
        const std::ptrdiff_t start = (std::ptrdiff_t) i * hopSize - fftSize / 2;

        for (int k = 0; k < fftSize; ++k)
        {
            const std::ptrdiff_t s = start + k;
            frame[(size_t) k] = s >= 0 && s < (std::ptrdiff_t) x.size() ? x[(size_t) s] * window[(size_t) k] : 0.0f;
        }

        fft.magnitudes (frame.data(), spectrum.data());

        for (int b = 0; b < bins; ++b)
            mag[(size_t) b] = spectrum[(size_t) b] * scale;
    };

    auto pushFrame = [&] (std::vector<float>& slot, const std::vector<float>& mag)
    {
        // o quadro mais velho sai da janela de cada bin e o novo entra
        for (int b = minBin; b <= maxBin; ++b)
            timeWindows[(size_t) b].replace (slot[(size_t) b], mag[(size_t) b]);

        slot = mag;
    };

    auto analyseFrame = [&] (int c)
    {
        const auto& mag = ring[(size_t) (c % timeMedian)];

        // 3. harmônico (mediana no tempo) e percussivo (mediana na frequência), máscara suave
        SortedWindow<freqMedian> across;
        const auto at = [&] (int k) { return k >= 0 && k < bins ? mag[(size_t) k] : 0.0f; };

        for (int k = minBin - freqMedian / 2; k <= minBin + freqMedian / 2; ++k)
            across.replace (0.0f, at (k));

        for (int b = minBin; b <= maxBin; ++b)
        {
            if (b > minBin)
                across.replace (at (b - freqMedian / 2 - 1), at (b + freqMedian / 2));

            const float h = timeWindows[(size_t) b].median();
            const float p = across.median();
            percussive[(size_t) b] = p;
            const float h2 = h * h, p2 = p * p;
            harmonic[(size_t) b] = h2 + p2 > 1.0e-20f ? mag[(size_t) b] * h2 / (h2 + p2) : 0.0f;
        }

        float frameMax = 0.0f;

        for (int b = minBin; b <= maxBin; ++b)
        {
            frameMax = std::max (frameMax, harmonic[(size_t) b]);
            totalEnergy += (double) mag[(size_t) b] * mag[(size_t) b];
        }

        const float floor = std::max (1.0e-4f, frameMax * 0.003f);   // -50 dB do mais forte, -80 dBFS
        auto& list = peaks[(size_t) c];

        for (int b = minBin + 1; b < maxBin; ++b)
        {
            const float m = harmonic[(size_t) b];

            if (m <= floor || m <= harmonic[(size_t) b - 1] || m < harmonic[(size_t) b + 1])
                continue;

            if (mag[(size_t) b] < prominence * percussive[(size_t) b])
                continue;

            // interpolação parabólica no log do módulo
            const double a = std::log (std::max (1.0e-12f, harmonic[(size_t) b - 1]));
            const double bb = std::log (m);
            const double g = std::log (std::max (1.0e-12f, harmonic[(size_t) b + 1]));
            const double den = a - 2.0 * bb + g;
            const double delta = den < 0.0 ? std::clamp (0.5 * (a - g) / den, -0.5, 0.5) : 0.0;
            const double amp = std::exp (bb - 0.25 * (a - g) * delta);
            list.push_back ({ (float) ((b + delta) * rate / fftSize), (float) amp });

            for (int k = b - 2; k <= b + 2; ++k)
                tonalEnergy += (double) mag[(size_t) k] * mag[(size_t) k];
        }
    };

    for (int i = 0; i < numFrames + timeMedian / 2; ++i)
    {
        if ((i & 63) == 0 && cancelled())
            return BeatAnalysis {};

        if (i < numFrames)
            computeFrame (i, incoming);
        else
            std::fill (incoming.begin(), incoming.end(), 0.0f);

        pushFrame (ring[(size_t) (i % timeMedian)], incoming);

        // a mediana no tempo usa os timeMedian últimos quadros: os de antes do começo e depois do fim são zero
        if (const int c = i - timeMedian / 2; c >= 0)
            analyseFrame (c);
    }

    out.tonalness = totalEnergy > 0.0 ? std::clamp (tonalEnergy / totalEnergy, 0.0, 1.0) : 0.0;

    // 4. afinação: média circular dos desvios dos picos fortes, em cents
    {
        double cs = 0.0, sn = 0.0;

        for (auto& list : peaks)
        {
            float strongest = 0.0f;

            for (auto& p : list)
                strongest = std::max (strongest, p.amp);

            for (auto& p : list)
            {
                if (p.hz < 60.0f || p.hz > 2500.0f || p.amp < strongest * 0.25f)
                    continue;

                const double cents = 1200.0 * std::log2 (p.hz / 440.0);
                const double th = 2.0 * pi * cents / 100.0;
                cs += p.amp * std::cos (th);
                sn += p.amp * std::sin (th);
            }
        }

        out.tuningCents = cs != 0.0 || sn != 0.0 ? std::atan2 (sn, cs) * 100.0 / (2.0 * pi) : 0.0;
    }

    // 5. cromagramas. Um pico que é harmônico de outro bem mais forte (3º, 5º, 6º, 7º) pesa menos:
    //    a quinta e a terça que a dente de serra põe em cada nota não viram notas do acorde.
    out.chroma.assign ((size_t) numFrames, Chroma {});
    out.bass.assign ((size_t) numFrames, Chroma {});

    for (int c = 0; c < numFrames; ++c)
    {
        auto& list = peaks[(size_t) c];
        std::sort (list.begin(), list.end(), [] (const Peak& a, const Peak& b) { return a.hz < b.hz; });

        for (auto& p : list)
        {
            float weight = std::sqrt (p.amp);   // comprimido: o 808 alto não apaga os acordes

            const auto isHarmonicOfStronger = [&list, &p]
            {
                for (int h : { 3, 5, 6, 7 })
                {
                    const float f0 = p.hz / (float) h;
                    auto it = std::lower_bound (list.begin(), list.end(), f0 * 0.98f, [] (const Peak& a, float f) { return a.hz < f; });

                    for (; it != list.end() && it->hz <= f0 * 1.02f; ++it)
                        if (it->amp >= 1.8f * p.amp)
                            return true;
                }

                return false;
            };

            if (isHarmonicOfStronger())
                weight *= 0.3f;

            const double pitch = 69.0 + 12.0 * std::log2 (p.hz / 440.0) - out.tuningCents / 100.0;
            const double nearest = std::round (pitch);
            const double d = pitch - nearest;
            const double cw = std::cos (pi * d * 0.75);
            const float w = (float) (weight * cw * cw);
            const auto pc = (size_t) pitchClass ((int) nearest);

            if (p.hz >= fullLowHz)
                out.chroma[(size_t) c][pc] += w;

            if (p.hz <= bassHighHz)
                out.bass[(size_t) c][pc] += w;
        }
    }

    // 6. perfil do trecho, do grave e da abertura
    std::vector<double> energy ((size_t) numFrames);

    for (int c = 0; c < numFrames; ++c)
    {
        energy[(size_t) c] = sumOf (out.chroma[(size_t) c]);

        for (int k = 0; k < 12; ++k)
        {
            out.profile[(size_t) k] += out.chroma[(size_t) c][(size_t) k];
            out.bassProfile[(size_t) k] += out.bass[(size_t) c][(size_t) k];
        }
    }

    {
        auto sorted = energy;
        const auto q = sorted.begin() + (std::ptrdiff_t) (sorted.size() * 9 / 10);
        std::nth_element (sorted.begin(), q, sorted.end());
        const double loud = *q;
        int start = 0;

        while (start < numFrames && energy[(size_t) start] < 0.25 * loud)
            ++start;

        // 1,2 s: cabe no primeiro compasso até uns 200 BPM
        const int end = std::min (numFrames, start + (int) std::ceil (1.2 / out.hopSeconds));

        for (int c = start; c < end; ++c)
            for (int k = 0; k < 12; ++k)
            {
                out.opening[(size_t) k] += out.chroma[(size_t) c][(size_t) k];
                out.openingBass[(size_t) k] += out.bass[(size_t) c][(size_t) k];
            }
    }

    normaliseMax (out.profile);
    normaliseMax (out.bassProfile);
    normaliseMax (out.opening);
    normaliseMax (out.openingBass);

    if (sumOf (out.profile) > 0.0)
        out.keys = rankKeys (out, windowChords (out));

    return out;
}

//==============================================================================
std::vector<double> barGrid (double firstBarSeconds, double bpm, int beatsPerBar, double stretch, int numBars)
{
    std::vector<double> starts;

    if (! (bpm > 0.0) || numBars <= 0)
        return starts;

    const double barFileSeconds = 60.0 / bpm * std::max (1, beatsPerBar) / std::max (0.01, stretch);

    for (int b = 0; b <= numBars; ++b)
        starts.push_back (firstBarSeconds + b * barFileSeconds);

    return starts;
}

std::vector<BarChord> chordsForBars (const BeatAnalysis& a, const std::vector<double>& barStarts, const KeyCandidate* key)
{
    std::vector<BarChord> out;

    if (barStarts.size() < 2 || a.chroma.empty() || ! (a.hopSeconds > 0.0))
        return out;

    const double halfWindow = 0.5 * fftSize / a.analysisRate;
    const double lastTime = a.frameTime (a.chroma.size() - 1);
    std::vector<double> energies;

    struct Acc
    {
        Chroma c {}, bass {};
        double energy = 0.0;
        bool any = false;
    };

    std::vector<Acc> accs (barStarts.size() - 1);

    for (std::size_t b = 0; b + 1 < barStarts.size(); ++b)
    {
        const double s0 = barStarts[b], s1 = barStarts[b + 1];
        auto& acc = accs[b];

        if (s1 <= s0 || s0 > lastTime || s1 < a.startSeconds)
            continue;

        const auto i0 = (std::size_t) std::max (0.0, std::floor ((s0 - a.startSeconds) / a.hopSeconds));
        const auto i1 = std::min (a.chroma.size() - 1, (std::size_t) std::max (0.0, std::ceil ((s1 - a.startSeconds) / a.hopSeconds)));

        for (std::size_t i = i0; i <= i1; ++i)
        {
            const double t = a.frameTime (i);

            if (t < s0 || t >= s1)
                continue;

            // quadro perto da divisa tem parte da janela no compasso vizinho
            const double inside = std::clamp (0.5 + std::min (t - s0, s1 - t) / (2.0 * halfWindow), 0.0, 1.0);

            for (int k = 0; k < 12; ++k)
            {
                acc.c[(size_t) k] += (float) (inside * a.chroma[i][(size_t) k]);
                acc.bass[(size_t) k] += (float) (inside * a.bass[i][(size_t) k]);
            }

            acc.any = true;
        }

        acc.energy = sumOf (acc.c) / std::max (1.0e-9, s1 - s0);

        if (acc.any)
            energies.push_back (acc.energy);
    }

    double typical = 0.0;

    if (! energies.empty())
    {
        auto e = energies;
        std::nth_element (e.begin(), e.begin() + (std::ptrdiff_t) (e.size() / 2), e.end());
        typical = e[e.size() / 2];
    }

    for (std::size_t b = 0; b < accs.size(); ++b)
    {
        BarChord bc;
        bc.bar = (int) b;
        bc.startSeconds = barStarts[b];
        bc.endSeconds = barStarts[b + 1];
        auto& acc = accs[b];

        if (! acc.any || acc.energy <= 0.08 * typical || acc.energy <= 0.0)
        {
            out.push_back (bc);
            continue;
        }

        const auto t = bestTriad (acc.c, acc.bass, key);
        bc.found = t.found;
        bc.root = t.root;
        bc.minor = t.minor;
        // compasso fraco (quase só bateria) fica com menos confiança
        const double strength = typical > 0.0 ? std::clamp (acc.energy / (0.5 * typical), 0.0, 1.0) : 1.0;
        bc.confidence = std::clamp (strength * t.confidence, 0.0, 1.0);
        out.push_back (bc);
    }

    return out;
}

//==============================================================================
bool keyPrefersFlats (int root, bool major) noexcept
{
    const int majorRoot = major ? pitchClass (root) : pitchClass (root + 3);

    switch (majorRoot)
    {
        case 5: case 10: case 3: case 8: case 1: case 6:   // F, Bb, Eb, Ab, Db, Gb
            return true;
        default:
            return false;
    }
}

std::string keyNamePt (int root, bool major)
{
    return pitchNamePt (root, keyPrefersFlats (root, major)) + (major ? " maior" : " menor");
}

std::string chordName (int root, bool minor, bool flats)
{
    return pitchName (root, flats) + (minor ? "m" : "");
}

KeyCandidate relativeOf (const KeyCandidate& k) noexcept
{
    KeyCandidate r;
    r.major = ! k.major;
    r.root = k.major ? pitchClass (k.root + 9) : pitchClass (k.root + 3);
    return r;
}

std::string keyRelation (const KeyCandidate& key, const KeyCandidate& alt)
{
    if (alt.root == key.root && alt.major == key.major)
        return {};

    if (alt.root == key.root)
        return "paralelo";

    if (alt == relativeOf (key))
        return "relativo";

    if (alt.major == key.major && (pitchClass (alt.root - key.root) == 7 || pitchClass (alt.root - key.root) == 5))
        return "vizinho";

    // o relativo do vizinho também divide 6 das 7 notas
    const auto rel = relativeOf (alt);

    if (rel.major == key.major && (pitchClass (rel.root - key.root) == 7 || pitchClass (rel.root - key.root) == 5))
        return "vizinho";

    return {};
}

} // namespace whynot::ideas
