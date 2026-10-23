#pragma once

// O andamento de um beat (gravar voz, fatia 6: o beat em um gesto), sem JUCE (testado em tests/record).
//
// Primeiro pelo nome do arquivo, que é como os produtores mandam o beat ("Trap 140bpm.wav", "beat_92_BPM",
// "140 BPM - Fm"). Sem nome, pela análise: o envelope de ataques (a subida da energia em janelas de ~5 ms),
// e um pente de pulsos entre 60 e 200 BPM que soma os ataques a 1, 2 e 4 tempos de distância. O dobro e a
// metade soam iguais para a conta; fica o que cai entre 85 e 170 BPM, a faixa do rap, do trap e do funk
// (o pente soma 1, 2 e 4 tempos: o pulso mais denso ganha, e um trap a 140 com os hats não vira 70). Perto de
// um inteiro, arredonda: beat é quase sempre inteiro.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace whynot::beat
{

/** O BPM escrito no nome do arquivo, ou 0. Aceita "140bpm", "140 bpm", "140-bpm", "bpm140", "bpm 92" e
    "140 BPM" (de 60 a 220; sem "bpm" no nome, nada: um número solto pode ser o take, a data ou o tom). */
inline double bpmFromName (const std::string& name)
{
    std::string s;

    for (char c : name)
        s += (char) std::tolower ((unsigned char) c);

    auto numberAt = [&s] (size_t from, int step) -> double
    {
        // o número logo antes (step -1) ou logo depois (step +1) de "bpm", pulando espaço, "_" e "-"
        long i = (long) from;

        while (i >= 0 && i < (long) s.size() && (s[(size_t) i] == ' ' || s[(size_t) i] == '_' || s[(size_t) i] == '-' || s[(size_t) i] == '.'))
            i += step;

        long a = i, b = i;

        while (a >= 0 && a < (long) s.size() && std::isdigit ((unsigned char) s[(size_t) a]))
            a += step;

        if (a == b)
            return 0.0;

        const long lo = std::min (a, b) + (step < 0 ? 1 : 0), hi = std::max (a, b) + (step < 0 ? 1 : 0);

        if (hi - lo < 2 || hi - lo > 3)
            return 0.0;

        const double value = std::stod (s.substr ((size_t) lo, (size_t) (hi - lo)));
        return value >= 60.0 && value <= 220.0 ? value : 0.0;
    };

    for (size_t at = s.find ("bpm"); at != std::string::npos; at = s.find ("bpm", at + 3))
    {
        if (at > 0)
            if (const double before = numberAt (at - 1, -1); before > 0.0)
                return before;

        if (const double after = numberAt (at + 3, 1); after > 0.0)
            return after;
    }

    return 0.0;
}

/** O BPM pela análise do áudio (mono, `n` amostras a `rate`), ou 0 se não deu: curto demais (menos de
    8 s) ou sem ataques claros. Usa até 60 s, depois dos 2 primeiros (introdução e silêncio do codificador). */
inline double bpmFromAudio (const float* x, int n, double rate)
{
    if (x == nullptr || rate <= 0.0 || n < (int) (8.0 * rate))
        return 0.0;

    const int hop = std::max (1, (int) std::lround (rate / 200.0));   // ~5 ms
    const int start = std::min (n / 4, (int) (2.0 * rate));
    const int end = std::min (n, start + (int) (60.0 * rate));
    const int frames = (end - start) / hop;

    if (frames < 800)
        return 0.0;

    // envelope de ataques: a subida do log da energia de cada janela (meia-onda)
    std::vector<double> energy ((size_t) frames, 0.0), onset ((size_t) frames, 0.0);

    for (int f = 0; f < frames; ++f)
    {
        double sum = 0.0;
        const float* w = x + start + f * hop;

        for (int i = 0; i < hop; ++i)
            sum += (double) w[i] * w[i];

        energy[(size_t) f] = std::log (1.0e-9 + sum / hop);
    }

    for (int f = 1; f < frames; ++f)
        onset[(size_t) f] = std::max (0.0, energy[(size_t) f] - energy[(size_t) f - 1]);

    double mean = 0.0;

    for (double v : onset)
        mean += v;

    mean /= frames;

    for (auto& v : onset)
        v = std::max (0.0, v - mean);   // só os ataques acima da média

    const double hopSeconds = hop / rate;

    auto at = [&onset, frames] (double position)   // o envelope num ponto fracionário
    {
        const int i = (int) position;

        if (i < 0 || i + 1 >= frames)
            return 0.0;

        const double t = position - i;
        return onset[(size_t) i] * (1.0 - t) + onset[(size_t) i + 1] * t;
    };

    auto score = [&] (double bpm)
    {
        const double period = 60.0 / bpm / hopSeconds;   // em janelas
        double s = 0.0;

        for (int f = 0; f + (int) (4.0 * period) + 2 < frames; ++f)
            if (const double o = onset[(size_t) f]; o > 0.0)
                s += o * (at (f + period) + 0.5 * at (f + 2.0 * period) + 0.25 * at (f + 4.0 * period));

        return s;
    };

    double best = 0.0, bestScore = 0.0, total = 0.0;
    int count = 0;

    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.25)
    {
        const double s = score (bpm);
        total += s;
        ++count;

        if (s > bestScore)
        {
            bestScore = s;
            best = bpm;
        }
    }

    if (best <= 0.0 || bestScore < 1.5 * total / std::max (1, count))
        return 0.0;   // nenhum pulso se destaca: sem ataques claros

    // refina em volta, em passos finos, com um pente longo (1 a 16 tempos): o erro se divide por 16
    auto fine = [&] (double bpm)
    {
        const double period = 60.0 / bpm / hopSeconds;
        double s = 0.0;

        for (int k = 1; k <= 16; ++k)
            for (int f = 0; f + (int) (k * period) + 2 < frames; ++f)
                if (const double o = onset[(size_t) f]; o > 0.0)
                    s += o * at (f + k * period);

        return s;
    };

    double fineBest = best, fineScore = 0.0;

    for (double bpm = best - 0.4; bpm <= best + 0.4; bpm += 0.02)
        if (const double s = fine (bpm); s > fineScore)
        {
            fineScore = s;
            fineBest = bpm;
        }

    best = fineBest;

    // o dobro e a metade: fica o que cai entre 85 e 170 (a faixa do rap, do trap e do funk)
    while (best < 85.0)
        best *= 2.0;

    while (best >= 170.0)
        best *= 0.5;

    const double rounded = std::round (best);
    return std::abs (best - rounded) < 0.35 ? rounded : std::round (best * 10.0) / 10.0;
}

} // namespace whynot::beat
