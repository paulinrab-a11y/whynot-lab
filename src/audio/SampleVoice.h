#pragma once

#include "Bandlimit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace whynot::audio
{

/** Sample decodificado e imutável. É criado fora da thread de áudio e entregue
    a ela por RtObjectSlot.
*/
struct SampleData
{
    std::vector<std::vector<float>> channels;   // [canal][frame]
    double sampleRate = 44100.0;
    int rootKey = 60;                           // nota MIDI em que o sample soa no tom original

    // Níveis de oitava para tocar mais agudo sem aliasing: mips[n][canal][frame] tem a taxa / 2^(n+1).
    // Vazio = acima da raiz só a sinc no nível 0 (sample longo, ou feito sem buildMipmaps).
    std::vector<std::vector<std::vector<float>>> mips;

    int numChannels() const noexcept         { return (int) channels.size(); }
    std::int64_t numFrames() const noexcept  { return channels.empty() ? 0 : (std::int64_t) channels.front().size(); }
};

/** Zera valores denormais do sample. Chamar na thread de trabalho, ao carregar,
    antes de publicar o sample para a thread de áudio: caudas muito baixas
    multiplicadas sem FTZ/DAZ custam até ~100× mais CPU.
*/
inline void flushDenormals (SampleData& sample) noexcept
{
    for (auto& channel : sample.channels)
        for (auto& x : channel)
            if (std::abs (x) < 1.0e-30f)
                x = 0.0f;
}

/** Faz os níveis de oitava (até 4: quatro oitavas acima, razão 16) de um sample de até 30 s; mais
    longo, fica sem (a memória cresceria quase o dobro). Thread de trabalho ou de mensagem, antes de
    publicar o sample para a thread de áudio. */
inline void buildMipmaps (SampleData& sample, int maxLevels = 4, double maxSeconds = 30.0)
{
    sample.mips.clear();

    if (sample.sampleRate <= 0.0 || sample.channels.empty() || (double) sample.numFrames() / sample.sampleRate > maxSeconds)
        return;

    sample.mips.reserve ((size_t) maxLevels);   // o ponteiro para o nível anterior não muda de lugar

    for (const auto* previous = &sample.channels; (int) sample.mips.size() < maxLevels; previous = &sample.mips.back())
    {
        if (previous->empty() || previous->front().size() < 64)
            break;

        std::vector<std::vector<float>> next;

        for (auto& channel : *previous)
        {
            next.push_back (halfRate (channel));

            for (auto& x : next.back())   // a cauda filtrada também não pode ter denormais
                if (std::abs (x) < 1.0e-30f)
                    x = 0.0f;
        }

        sample.mips.push_back (std::move (next));
    }
}

struct VoiceParams
{
    float glideMs   = 80.0f;   // tempo para ir de uma nota à outra num slide
    float attackMs  = 1.0f;    // rampa de entrada, evita estalo no começo
    float releaseMs = 15.0f;   // rampa de saída, evita estalo no fim
    float gain      = 1.0f;

    // Os valores padrão abaixo deixam o som igual ao de antes deles existirem.
    float tuneSemis     = 0.0f;   // afinação em semitons, com fração (0,37 = 37 cents)
    float sustain       = 1.0f;   // nível depois do decaimento; 1 = sem decaimento
    float decayMs       = 300.0f; // tempo até -60 dB do caminho entre o pico e `sustain`
    float pitchEnvSemis = 0.0f;   // a nota começa tantos semitons acima (ou abaixo) e cai até ela: o "drop" do 808
    float pitchEnvMs    = 60.0f;  // tempo da queda: aos pitchEnvMs falta 1% do caminho
    float startFraction = 0.0f;   // onde a leitura começa, de 0 (início) a 0,99 do sample

    // Laço (os pontos de loop do sample): enquanto a voz soa, a leitura volta do fim do laço
    // para o começo dele, com um crossfade antes do fim. Desligado, o som fica igual ao de antes.
    bool  loop        = false;
    float loopStart   = 0.0f;    // fração do sample, de 0 a 1
    float loopEnd     = 1.0f;    // fração do sample; o laço precisa de pelo menos 16 amostras
    float loopXfadeMs = 20.0f;   // crossfade, no tempo do arquivo (sem a afinação); no máximo metade do laço

    // Fatia (o fatiador): a voz toca só de regionStart a regionEnd do sample e sai com um fade curto
    // que acaba no fim da fatia, para a fatia seguinte não vazar nem estalar. Com uma região, o
    // início (startFraction) e o laço ficam de fora. Os padrões (0 e 1) deixam o som igual ao de antes.
    float regionStart  = 0.0f;
    float regionEnd    = 1.0f;
    float regionFadeMs = 4.0f;

    // Pan da nota (o pan do passo, 26/10): -1 esquerda .. 1 direita. No centro, os dois lados ficam em 1 e
    // o som sai bit a bit igual ao de antes; fora dele, o lado oposto cai pela lei do cosseno.
    float pan = 0.0f;
};

/** Troca valores não finitos pelo padrão e limita as faixas. Os parâmetros vêm
    da UI e da IA; um NaN aqui viraria NaN no mixer inteiro.
*/
inline VoiceParams sanitized (VoiceParams p) noexcept
{
    const VoiceParams defaults;
    auto fix = [] (float v, float fallback, float lo, float hi) { return std::isfinite (v) ? std::clamp (v, lo, hi) : fallback; };

    p.glideMs   = fix (p.glideMs,   defaults.glideMs,   0.0f, 10000.0f);
    p.attackMs  = fix (p.attackMs,  defaults.attackMs,  0.0f, 1000.0f);
    p.releaseMs = fix (p.releaseMs, defaults.releaseMs, 0.0f, 10000.0f);
    p.gain      = fix (p.gain,      defaults.gain,      0.0f, 4.0f);
    p.tuneSemis     = fix (p.tuneSemis,     defaults.tuneSemis,     -48.0f, 48.0f);
    p.sustain       = fix (p.sustain,       defaults.sustain,       0.0f, 1.0f);
    p.decayMs       = fix (p.decayMs,       defaults.decayMs,       1.0f, 20000.0f);
    p.pitchEnvSemis = fix (p.pitchEnvSemis, defaults.pitchEnvSemis, -48.0f, 48.0f);
    p.pitchEnvMs    = fix (p.pitchEnvMs,    defaults.pitchEnvMs,    1.0f, 5000.0f);
    p.startFraction = fix (p.startFraction, defaults.startFraction, 0.0f, 0.99f);
    p.loopStart     = fix (p.loopStart,     defaults.loopStart,     0.0f, 1.0f);
    p.loopEnd       = fix (p.loopEnd,       defaults.loopEnd,       0.0f, 1.0f);
    p.loopXfadeMs   = fix (p.loopXfadeMs,   defaults.loopXfadeMs,   0.0f, 2000.0f);
    p.regionStart   = fix (p.regionStart,   defaults.regionStart,   0.0f, 1.0f);
    p.regionEnd     = fix (p.regionEnd,     defaults.regionEnd,     0.0f, 1.0f);
    p.regionFadeMs  = fix (p.regionFadeMs,  defaults.regionFadeMs,  0.5f, 50.0f);
    p.pan           = fix (p.pan,           defaults.pan,           -1.0f, 1.0f);
    return p;
}

/** Uma voz do sampler da WHYNOT, com glide de tempo constante (o "slide" do 808).

    Tudo é pré-alocado; render() não aloca, não trava e não faz I/O.
    Num slide a posição de leitura continua, então a forma de onda não salta:
    só a velocidade de leitura (o tom) muda ao longo de glideMs.

    Envelope: ataque e liberação em rampa reta (como sempre); entre eles, se sustain < 1,
    um decaimento exponencial até `sustain`. Com sustain 0 a voz termina sozinha quando
    passa de -100 dB, mesmo com a nota segurada (o "decay" de um 808 one-shot).
    A queda de tom (pitchEnv) só começa numa nota nova; um slide não a reinicia.

    Laço: com params.loop, a leitura que chega ao fim do laço volta ao começo dele, e a nota
    dura o quanto for segurada. Nos `xfade` frames antes do fim, a saída mistura em linha reta
    o trecho atual com o trecho um laço antes (o que vem logo antes do começo do laço): no fim
    do crossfade a mistura já é o começo do laço, e a volta não salta. A mistura linear mantém
    o volume quando os dois trechos estão em fase (laço de períodos inteiros, como o que
    analyseSample sugere); fora de fase, o volume cai um pouco no meio, mas sem estalo.
    O laço continua no release (o fim da nota sai pela rampa, nunca pelo fim do arquivo).
    leaveLoop() (nota solta num canal one-shot) sai do laço sem salto e toca até o fim.

    Fatia: com uma região, a leitura começa em regionStart e, quando falta regionFadeMs (em tempo de
    saída, já com a afinação) para regionEnd, a voz sai por uma rampa que chega a zero no fim da fatia.

    Leitura: com a razão até 1 (no tom original ou mais grave), o Hermite de sempre. Acima, a sinc
    janelada do Bandlimit.h, no nível de oitava certo do sample, com o corte em 1/razão: um chimbal
    sete semitons acima ou um sample de 96 kHz numa saída de 48 não dobram os agudos (aliasing).

    Força: num slide, a força nova chega numa rampa de 5 ms (um salto de volume estalava).
    Voz roubada (nota nova numa voz que ainda soa): o som velho sai em 3 ms por cima do novo.

    Uso exclusivo da thread de áudio (noteOn/noteOff vêm do MIDI do próprio
    bloco). O processo que chama render() deve ativar FTZ/DAZ
    (juce::ScopedNoDenormals), e o sample deve passar por flushDenormals().
*/
class SampleVoice
{
public:
    void prepare (double outputSampleRate) noexcept
    {
        outRate = outputSampleRate > 0.0 ? outputSampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        active = releasing = decaying = needsSetup = false;
        loopOn = leavingLoop = hasRegion = regionFading = false;
        position = 0.0;
        env = envTarget = envStep = 0.0f;
        glideSamplesLeft = 0;
        pitchEnvOffset = 0.0f;
        velocitySamplesLeft = tailSamplesLeft = 0;
    }

    bool isActive() const noexcept   { return active; }
    float currentKey() const noexcept { return currentSemis; }
    bool isLooping() const noexcept  { return active && loopOn; }

    /** slide = true e a voz tocando: desliza até a nota nova sem reiniciar o sample.
        Sem slide, a voz reinicia do zero; quem gerencia as vozes (o sampler) deve
        mandar a nota antiga para noteOff() numa segunda voz, para o corte não estalar.
    */
    void noteOn (int key, float velocity, bool slide, const VoiceParams& p) noexcept
    {
        const float playingSustain = params.sustain;
        const float playingGain = params.gain * velocityGain;
        params = sanitized (p);
        targetSemis = (float) key;
        const float newVelocity = std::isfinite (velocity) ? std::clamp (velocity, 0.0f, 1.0f) : 0.0f;

        if (slide && active && ! releasing && params.glideMs > 0.0f)
        {
            // O envelope continua o da nota que já toca: um sustain novo no meio do
            // decaimento faria o volume saltar (estalo). O laço também continua o mesmo.
            // A força nova também chega numa rampa (5 ms), pelo mesmo motivo.
            params.sustain = playingSustain;
            velocityTarget = newVelocity;
            velocitySamplesLeft = std::max (1, (int) std::lround (0.005 * outRate));
            velocityStep = (velocityTarget - velocityGain) / (float) velocitySamplesLeft;
            glideFrom = currentSemis;
            glideDelta = targetSemis - currentSemis;
            glideTotal = glideSamplesLeft = std::max (1, (int) std::lround (params.glideMs * 0.001 * outRate));
            invGlideTotal = 1.0f / (float) glideTotal;
            return;
        }

        // Voz roubada ainda soando: o som velho sai em 3 ms por cima da nota nova, no lugar de sumir seco.
        if (active && env > 0.0f && ! needsSetup)
        {
            tailPosition = position;
            tailRatio = ratio;
            tailSamplesLeft = std::max (1, (int) std::lround (0.003 * outRate));
            tailAmp = env * playingGain;
            tailStep = tailAmp / (float) tailSamplesLeft;
            tailPan = panGain;
        }
        else
        {
            tailSamplesLeft = 0;
        }

        velocityGain = velocityTarget = newVelocity;
        velocitySamplesLeft = 0;

        // Lei do cosseno só no lado oposto: no centro os dois ficam em 1 (multiplicar por 1 não muda nada).
        panGain[0] = params.pan > 0.0f ? (float) std::cos (params.pan * 1.5707963267948966) : 1.0f;
        panGain[1] = params.pan < 0.0f ? (float) std::cos (-params.pan * 1.5707963267948966) : 1.0f;
        position = 0.0;
        needsSetup = true;   // início, laço e fatia dependem do tamanho do sample, que só se sabe no render()
        loopOn = leavingLoop = hasRegion = regionFading = false;
        currentSemis = targetSemis;
        glideSamplesLeft = 0;
        env = 0.0f;
        startRamp (1.0f, params.attackMs);
        releasing = decaying = false;

        // Os coeficientes saem daqui, uma vez por nota: render() só multiplica.
        decayCoeff = (float) std::exp (-6.907755278982137 / (params.decayMs * 0.001 * outRate));       // ln(1000)
        pitchEnvOffset = params.pitchEnvSemis;
        pitchEnvCoeff = (float) std::exp (-4.605170185988092 / (params.pitchEnvMs * 0.001 * outRate)); // ln(100)
        active = true;
    }

    /** Corte rápido (choke, All Sound Off): sai em `ms` a partir do nível atual, sem estalo. */
    void fadeOut (float ms) noexcept
    {
        if (! active)
            return;

        releasing = true;
        decaying = false;
        startRamp (0.0f, std::isfinite (ms) ? std::clamp (ms, 0.5f, 100.0f) : 3.0f);
    }

    void noteOff() noexcept
    {
        if (! active)
            return;

        releasing = true;
        decaying = false;
        startRamp (0.0f, params.releaseMs);
    }

    /** Sai do laço e deixa o sample tocar até o fim (a nota de um canal one-shot foi solta).
        Se a leitura está no crossfade, termina a volta antes: o som não salta. */
    void leaveLoop() noexcept
    {
        if (active && (loopOn || needsSetup))
            leavingLoop = true;
    }

    /** Soma a voz em out[canal][frame]. Não sobrescreve o que já está no buffer. */
    void render (const SampleData& sample, float* const* out, int numOutChannels, int numFrames) noexcept
    {
        const auto length = sample.numFrames();
        const int srcChannels = sample.numChannels();

        if (! active || numOutChannels <= 0)
            return;

        if (length < 2 || srcChannels == 0 || sample.sampleRate <= 0.0)
        {
            reset();
            return;
        }

        const double rateRatio = sample.sampleRate / outRate;

        if (needsSetup)
        {
            needsSetup = false;
            hasRegion = params.regionStart > 0.0f || params.regionEnd < 1.0f;

            if (hasRegion)
            {
                // A fatia: começa no começo dela e acaba no fim dela; sem início próprio nem laço.
                const double last = (double) (length - 1);
                position = (double) params.regionStart * last;
                regionTo = std::min ((double) params.regionEnd * last, last);
                regionFadeFrames = (double) params.regionFadeMs * 0.001 * outRate;
                loopOn = leavingLoop = false;

                if (regionTo - position < 1.0)   // fatia vazia
                {
                    reset();
                    return;
                }
            }
            else
            {
                if (params.startFraction > 0.0f)
                    position = (double) params.startFraction * (double) (length - 1);

                setupLoop (sample.sampleRate, length);
            }
        }

        for (int i = 0; i < numFrames; ++i)
        {
            if (glideSamplesLeft > 0)
            {
                // Calculado a partir do início do glide, sem acumular erro de arredondamento.
                --glideSamplesLeft;
                currentSemis = glideSamplesLeft == 0
                                 ? targetSemis
                                 : glideFrom + glideDelta * (float) (glideTotal - glideSamplesLeft) * invGlideTotal;
            }

            if (pitchEnvOffset != 0.0f)
            {
                pitchEnvOffset *= pitchEnvCoeff;

                if (std::abs (pitchEnvOffset) < 1.0e-3f)   // 0,1 cent: o resto não se ouve
                    pitchEnvOffset = 0.0f;
            }

            // Sem afinação nem queda de tom a soma é exata: o som fica igual ao de antes.
            const float semis = currentSemis + params.tuneSemis + pitchEnvOffset;

            if (semis != ratioSemis)
            {
                ratioSemis = semis;
                ratio = std::exp2 ((double) (semis - (float) sample.rootKey) / 12.0) * rateRatio;
            }

            if (envStep != 0.0f)
            {
                env += envStep;

                if ((envStep > 0.0f && env >= envTarget) || (envStep < 0.0f && env <= envTarget))
                {
                    env = envTarget;
                    envStep = 0.0f;
                    decaying = ! releasing && params.sustain < 1.0f;   // fim do ataque
                }
            }
            else if (decaying)
            {
                const float level = params.sustain;
                env = level + (env - level) * decayCoeff;

                if (env - level <= 1.0e-5f)
                {
                    env = level;
                    decaying = false;

                    if (level <= 1.0e-5f)   // decaiu até -100 dB sem sustentação: a nota acabou
                    {
                        reset();
                        return;
                    }
                }
            }

            if (hasRegion)
            {
                if (position >= regionTo)
                {
                    reset();
                    return;
                }

                // Faltando o fade (em frames de saída, no tom de agora): a rampa chega a zero no fim da fatia.
                if (! regionFading)
                {
                    const double left = (regionTo - position) / ratio;

                    if (left <= regionFadeFrames)
                    {
                        regionFading = releasing = true;
                        decaying = false;
                        startRamp (0.0f, (float) (left * 1000.0 / outRate));
                    }
                }
            }

            if (releasing && env <= 0.0f)
            {
                reset();
                return;
            }

            const auto index = (std::int64_t) position;

            if (index >= length - 1)
            {
                reset();
                return;
            }

            // Sair do laço só fora do crossfade: ali a saída já é o sample puro, e segue igual.
            if (leavingLoop && position < loopFadeFrom)
                loopOn = leavingLoop = false;

            // No crossfade, o peso do trecho um laço antes vai de 0 a 1 até o fim do laço.
            float blend = 0.0f;
            double back = 0.0;

            if (loopOn && position >= loopFadeFrom && xfade > 0.0)
            {
                blend = (float) std::min (1.0, (position - loopFadeFrom) * invXfade);
                back = position - loopLength;   // >= loopFrom - xfade >= 0 (setupLoop garante)
            }

            if (velocitySamplesLeft > 0)
            {
                velocityGain += velocityStep;

                if (--velocitySamplesLeft == 0)
                    velocityGain = velocityTarget;
            }

            const float amp = env * params.gain * velocityGain;

            if (ratio <= 1.0)
            {
                // No tom original ou mais grave: o Hermite de sempre (bit a bit igual ao de antes).
                const float frac = (float) (position - (double) index);
                const auto i0 = (std::size_t) index;
                const auto im1 = index > 0 ? i0 - 1 : i0;                        // antes do começo: repete a primeira
                const auto i2 = index + 2 < length ? i0 + 2 : i0 + 1;            // depois do fim: repete a última

                for (int ch = 0; ch < numOutChannels; ++ch)
                {
                    const auto& src = sample.channels[(size_t) std::min (ch, srcChannels - 1)];
                    float v = hermite (src[im1], src[i0], src[i0 + 1], src[i2], frac);

                    if (blend > 0.0f)
                        v += (read (sample, ch, length, back, ratio) - v) * blend;

                    out[ch][i] += v * amp * (numOutChannels > 1 ? panGain[(size_t) std::min (ch, 1)] : 1.0f);
                }
            }
            else
            {
                for (int ch = 0; ch < numOutChannels; ++ch)
                {
                    float v = read (sample, ch, length, position, ratio);

                    if (blend > 0.0f)
                        v += (read (sample, ch, length, back, ratio) - v) * blend;

                    out[ch][i] += v * amp * (numOutChannels > 1 ? panGain[(size_t) std::min (ch, 1)] : 1.0f);
                }
            }

            // O som da voz roubada, saindo em 3 ms (lê reto, sem laço nem fatia: é curto demais para isso).
            if (tailSamplesLeft > 0)
            {
                if (tailPosition < (double) (length - 1))
                    for (int ch = 0; ch < numOutChannels; ++ch)
                        out[ch][i] += read (sample, ch, length, tailPosition, tailRatio) * tailAmp * (numOutChannels > 1 ? tailPan[(size_t) std::min (ch, 1)] : 1.0f);

                tailPosition += tailRatio;
                tailAmp = std::max (0.0f, tailAmp - tailStep);
                --tailSamplesLeft;
            }

            position += ratio;

            // Passou do fim do laço: volta o tamanho do laço (o resto da divisão, se o passo for maior que ele).
            if (loopOn && position >= loopTo)
                position = loopFrom + std::fmod (position - loopTo, loopLength);
        }
    }

private:
    /** Interpolação de Hermite de 4 pontos (3ª ordem, "x-form"): passa pelos pontos e pela inclinação
        média deles. Contra a linear, o erro ao ler o sample fora do tom original cai de 17 a 41 dB nos
        casos medidos (3 kHz +7 semitons: 43 → 60 dB; 200 Hz +12: 90 → 131 dB), menos perto do topo do
        espectro (10 kHz: +7 dB). Com frac = 0 devolve x0 exato: o sample no tom
        original e na mesma taxa sai bit a bit igual. */
    static float hermite (float xm1, float x0, float x1, float x2, float frac) noexcept
    {
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * frac + c2) * frac + c1) * frac + x0;
    }

    /** Uma amostra do canal na posição `pos` (frames do sample) e na razão `r`: até 1, o Hermite;
        acima, a sinc janelada no nível de oitava em que a razão fica entre 1 e 2. */
    float read (const SampleData& sample, int ch, std::int64_t length, double pos, double r) const noexcept
    {
        const int srcCh = std::min (ch, sample.numChannels() - 1);

        if (r <= 1.0)
            return readAt (sample.channels[(size_t) srcCh], length, pos);

        int level = 0;

        while (r >= 2.0 && level < (int) sample.mips.size())
        {
            r *= 0.5;
            pos *= 0.5;
            ++level;
        }

        const auto& channels = level == 0 ? sample.channels : sample.mips[(size_t) level - 1];
        return table->read (channels[(size_t) std::min (srcCh, (int) channels.size() - 1)], pos, std::min (r, 2.0));
    }

    /** O mesmo Hermite numa posição qualquer de 0 a length - 1. */
    static float readAt (const std::vector<float>& src, std::int64_t length, double pos) noexcept
    {
        const auto index = std::clamp ((std::int64_t) pos, (std::int64_t) 0, length - 1);
        const float frac = (float) (pos - (double) index);
        const auto i0 = (std::size_t) index;
        const auto im1 = index > 0 ? i0 - 1 : i0;
        const auto i1 = index + 1 < length ? i0 + 1 : i0;
        const auto i2 = index + 2 < length ? i0 + 2 : i1;
        return hermite (src[im1], src[i0], src[i1], src[i2], frac);
    }

    /** Põe o laço em frames do sample, uma vez por nota. Laço com menos de 16 frames, ou que
        o início da leitura já passou, fica desligado nesta nota. O crossfade cabe na metade do
        laço e no trecho antes dele (é de lá que a mistura lê). */
    void setupLoop (double sourceRate, std::int64_t length) noexcept
    {
        loopOn = false;

        if (! params.loop)
        {
            leavingLoop = false;
            return;
        }

        const double last = (double) (length - 1);
        const double from = (double) params.loopStart * last;
        const double to = std::min ((double) params.loopEnd * last, (double) (length - 2));   // a leitura usa a amostra seguinte
        const double len = to - from;

        if (len < 16.0 || position >= to || leavingLoop)
        {
            leavingLoop = false;
            return;
        }

        loopFrom = from;
        loopTo = to;
        loopLength = len;
        xfade = std::min ({ (double) params.loopXfadeMs * 0.001 * sourceRate, len * 0.5, from });
        loopFadeFrom = to - xfade;
        invXfade = xfade > 0.0 ? 1.0 / xfade : 0.0;
        loopOn = true;
    }

    void startRamp (float target, float ms) noexcept
    {
        envTarget = target;
        const float samples = std::max (1.0f, ms * 0.001f * (float) outRate);
        envStep = (target - env) / samples;

        if (envStep == 0.0f)
            env = target;
    }

    VoiceParams params;
    double outRate = 48000.0;
    double position = 0.0;
    double ratio = 1.0;
    float ratioSemis = -1000.0f;   // força o cálculo da razão no primeiro frame
    float currentSemis = 60.0f, targetSemis = 60.0f;
    float glideFrom = 60.0f, glideDelta = 0.0f, invGlideTotal = 0.0f;
    int glideTotal = 0, glideSamplesLeft = 0;
    float env = 0.0f, envTarget = 0.0f, envStep = 0.0f;
    float decayCoeff = 0.0f;
    float pitchEnvOffset = 0.0f, pitchEnvCoeff = 0.0f;
    float velocityGain = 1.0f, velocityTarget = 1.0f, velocityStep = 0.0f;   // força, com a rampa do slide
    int velocitySamplesLeft = 0;
    double tailPosition = 0.0, tailRatio = 1.0;                              // a voz roubada saindo
    float tailAmp = 0.0f, tailStep = 0.0f;
    std::array<float, 2> panGain { 1.0f, 1.0f }, tailPan { 1.0f, 1.0f };   // o pan desta nota e o da voz roubada
    int tailSamplesLeft = 0;
    const BandlimitTable* table = &bandlimitTable();   // feita na construção da voz, fora da thread de áudio
    double loopFrom = 0.0, loopTo = 0.0, loopLength = 1.0, loopFadeFrom = 0.0, xfade = 0.0, invXfade = 0.0;   // em frames do sample
    double regionTo = 0.0, regionFadeFrames = 0.0;   // fim da fatia (frames do sample) e o fade (frames de saída)
    bool active = false, releasing = false, decaying = false, needsSetup = false;
    bool loopOn = false, leavingLoop = false, hasRegion = false, regionFading = false;
};

} // namespace whynot::audio
