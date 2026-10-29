#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace whynot::audio
{

/** Uma zona do piano (o WHYNOT Piano, pedido do usuário de 21/10): as teclas e as forças que ela cobre,
    a nota em que o sample soa e o começo dele já na memória. O resto do sample vem do disco, lido por
    uma thread de fundo para o anel de cada voz (VoiceStream). Imutável depois de carregada. */
struct PianoZone
{
    int rootKey = 60, loKey = 0, hiKey = 127, loVel = 1, hiVel = 127;
    bool release = false;            // toca ao soltar a nota (ressonância das cordas, o martelo voltando)
    float gain = 1.0f;               // volume da zona, linear
    float velTrack = 1.0f;           // 0 a 1: quanto a força muda o volume (o amp_veltrack do SFZ)
    float releaseSeconds = 1.0f;     // nota: tempo de soltura até -60 dB (os abafadores)
    float rtDecayDb = 0.0f;          // release: dB a menos por segundo que a nota ficou segurada
    float keyTrack = 1.0f;           // 0 = o tom não muda com a tecla (o martelo)
    double sampleRate = 44100.0;
    std::int64_t totalFrames = 0;    // o sample inteiro, em quadros
    std::vector<std::int16_t> head;  // os primeiros quadros, estéreo intercalado
    std::string file;                // de onde vem o resto

    std::int64_t headFrames() const noexcept   { return (std::int64_t) head.size() / 2; }
};

/** As zonas de um piano e as tabelas para achar a certa sem procurar (thread de áudio). */
struct PianoSet
{
    std::vector<PianoZone> zones;
    std::array<std::int16_t, 128 * 128> attack {};        // [tecla * 128 + força] → zona, ou -1
    std::array<std::array<std::int16_t, 6>, 128> releases {};   // por tecla, até 6 zonas de release (-1 = fim)

    /** Monta as tabelas. Chamar depois de encher `zones`, fora da thread de áudio. */
    void buildTables()
    {
        attack.fill (-1);

        for (auto& r : releases)
            r.fill (-1);

        for (int i = 0; i < (int) zones.size(); ++i)
        {
            const auto& z = zones[(std::size_t) i];

            for (int key = std::max (0, z.loKey); key <= std::min (127, z.hiKey); ++key)
            {
                if (z.release)
                {
                    for (auto& slot : releases[(std::size_t) key])
                        if (slot < 0) { slot = (std::int16_t) i; break; }
                }
                else
                {
                    for (int vel = std::max (1, z.loVel); vel <= std::min (127, z.hiVel); ++vel)
                        if (attack[(std::size_t) (key * 128 + vel)] < 0)
                            attack[(std::size_t) (key * 128 + vel)] = (std::int16_t) i;
                }
            }
        }
    }

    int attackFor (int key, int velocity) const noexcept
    {
        if (key < 0 || key > 127)
            return -1;

        return attack[(std::size_t) (key * 128 + std::clamp (velocity, 1, 127))];
    }
};

/** O anel de uma voz: o streamer (thread de fundo) escreve os quadros do sample que vêm depois do
    começo; a voz (thread de áudio) lê. Um produtor e um consumidor, sem trava:
    - a voz pede um sample com request(geração, zona) e diz até onde já leu com markConsumed();
    - o streamer escreve só quadros que a voz não vai mais precisar e publica até onde escreveu,
      com a geração do pedido; a voz só lê o que foi publicado para a geração dela. */
class VoiceStream
{
public:
    static constexpr int capacity = 1 << 15;   // quadros (0,74 s a 44,1 kHz)

    // thread de áudio
    void request (std::uint32_t generation, int zone) noexcept
    {
        consumed.store (0, std::memory_order_relaxed);
        job.store (((std::uint64_t) generation << 32) | (std::uint32_t) (zone + 1), std::memory_order_release);
    }

    void stop() noexcept                                { job.store (0, std::memory_order_release); }
    void markConsumed (std::int64_t frame) noexcept     { consumed.store (std::max<std::int64_t> (0, frame), std::memory_order_release); }

    /** Até qual quadro (exclusivo) a geração já tem dados no anel; -1 se o streamer ainda não chegou nela. */
    std::int64_t available (std::uint32_t generation) const noexcept
    {
        const auto f = filled.load (std::memory_order_acquire);
        return (std::uint32_t) (f >> 40) == (generation & 0xffffffu) ? (std::int64_t) (f & ((1ull << 40) - 1)) : -1;
    }

    const float* frame (std::int64_t f) const noexcept   { return data.data() + (std::size_t) ((f & (capacity - 1)) * 2); }

    // streamer
    std::uint64_t currentJob() const noexcept        { return job.load (std::memory_order_acquire); }
    std::int64_t consumedFrame() const noexcept      { return consumed.load (std::memory_order_acquire); }
    float* slot (std::int64_t f) noexcept            { return data.data() + (std::size_t) ((f & (capacity - 1)) * 2); }

    void publish (std::uint32_t generation, std::int64_t filledUpTo) noexcept
    {
        filled.store (((std::uint64_t) (generation & 0xffffffu) << 40) | (std::uint64_t) filledUpTo, std::memory_order_release);
    }

    static std::uint32_t generationOf (std::uint64_t jobValue) noexcept   { return (std::uint32_t) (jobValue >> 32); }
    static int zoneOf (std::uint64_t jobValue) noexcept                   { return (int) (jobValue & 0xffffffffu) - 1; }

private:
    std::vector<float> data = std::vector<float> ((std::size_t) capacity * 2, 0.0f);
    std::atomic<std::uint64_t> job { 0 }, filled { 0 };
    std::atomic<std::int64_t> consumed { 0 };
};

/** O que o streamer guarda de cada voz entre uma passada e outra. */
struct StreamCursor
{
    std::uint64_t job = 0;
    std::int64_t next = 0;
};

/** Uma passada do streamer numa voz. Se a voz pediu um sample novo, recomeça logo depois do começo
    dele. Diz qual trecho do arquivo ler agora, [from, to), com no máximo maxChunk quadros e sem passar
    por cima do que a voz ainda vai ler. Devolve false quando não há nada a ler. Depois de copiar o
    trecho para stream.slot(), chamar finishFill(). Só a thread do streamer (ou o render offline). */
inline bool planFill (VoiceStream& stream, const PianoSet& set, StreamCursor& cursor, std::int64_t maxChunk,
                      int& zone, std::int64_t& from, std::int64_t& to) noexcept
{
    const auto job = stream.currentJob();

    if (job == 0)
    {
        cursor.job = 0;
        return false;
    }

    zone = VoiceStream::zoneOf (job);

    if (zone < 0 || zone >= (int) set.zones.size())
        return false;

    const auto& z = set.zones[(std::size_t) zone];

    if (job != cursor.job)
    {
        cursor.job = job;
        cursor.next = z.headFrames();
        stream.publish (VoiceStream::generationOf (job), cursor.next);
    }

    const auto consumed = stream.consumedFrame();
    const auto limit = std::min (z.totalFrames, consumed + VoiceStream::capacity - 8);

    // se faltou dado e a voz passou na frente, não lê o que ela já pulou (ela nunca volta para trás)
    cursor.next = std::max (cursor.next, consumed);

    if (cursor.next >= limit)
        return false;

    from = cursor.next;
    to = std::min (limit, from + std::max<std::int64_t> (1, maxChunk));
    return true;
}

inline void finishFill (VoiceStream& stream, StreamCursor& cursor, std::int64_t to) noexcept
{
    cursor.next = to;
    stream.publish (VoiceStream::generationOf (cursor.job), to);
}

/** Uma voz do piano. Lê o começo do sample da zona e o resto do anel (VoiceStream). Se o anel não
    chegou a tempo, toca silêncio naquele trecho e conta uma falta (underruns) em vez de esperar.
    Tudo é pré-alocado; render() não aloca, não trava e não faz I/O. FTZ/DAZ por conta de quem chama. */
class PianoVoice
{
public:
    void prepare (double outputSampleRate) noexcept
    {
        outRate = outputSampleRate > 0.0 ? outputSampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        active = releasing = false;
        zone = -1;
        noteKey = -1;
        env = 0.0f;
        position = 0.0;
    }

    /** Começa a zona `zoneIndex` na tecla `key`, força de 0 a 1. `extraGain` entra no volume
        (o rt_decay dos releases). A geração identifica este começo no anel. */
    void start (const PianoSet& set, int zoneIndex, int key, float velocity01, float tuneSemis, float extraGain,
                VoiceStream& stream, std::uint32_t generation) noexcept
    {
        if (zoneIndex < 0 || zoneIndex >= (int) set.zones.size())
            return;

        const auto& z = set.zones[(std::size_t) zoneIndex];
        const float v = std::isfinite (velocity01) ? std::clamp (velocity01, 0.0f, 1.0f) : 0.0f;
        zone = zoneIndex;
        noteKey = key;
        gen = generation;
        gain = z.gain * std::pow (v, 2.0f * std::clamp (z.velTrack, 0.0f, 1.0f)) * (std::isfinite (extraGain) ? extraGain : 0.0f);
        const float semis = z.keyTrack * (float) (key - z.rootKey) + (std::isfinite (tuneSemis) ? tuneSemis : 0.0f);
        ratio = baseRatio = std::exp2 ((double) semis / 12.0) * z.sampleRate / outRate;
        offsetSemis = 0.0f;
        bendScale = std::isfinite (z.keyTrack) ? std::clamp (z.keyTrack, 0.0f, 1.0f) : 1.0f;
        position = 0.0;
        env = 0.0f;
        envTarget = 1.0f;
        envStep = 1.0f / std::max (1.0f, 0.001f * (float) outRate);   // 1 ms de entrada, sem estalo
        releaseCoeff = 1.0f;
        releasing = false;
        active = true;
        stream.request (generation, zoneIndex);
    }

    /** Solta a nota: decai até -60 dB em `seconds` (os abafadores caindo). */
    void release (float seconds) noexcept
    {
        if (! active || releasing)
            return;

        releasing = true;
        envStep = 0.0f;
        const float s = std::isfinite (seconds) ? std::clamp (seconds, 0.01f, 30.0f) : 1.0f;
        releaseCoeff = (float) std::exp (-6.907755278982137 / (s * outRate));   // ln(1000)
    }

    /** Corte rápido (nota repetida, voz roubada, All Sound Off). */
    void fadeOut (float ms) noexcept
    {
        if (! active)
            return;

        releasing = true;
        envStep = 0.0f;
        const float s = (std::isfinite (ms) ? std::clamp (ms, 0.5f, 200.0f) : 3.0f) * 0.001f;
        releaseCoeff = (float) std::exp (-6.907755278982137 / (s * outRate));
    }

    bool isActive() const noexcept      { return active; }
    bool isReleasing() const noexcept   { return releasing; }
    int key() const noexcept            { return noteKey; }
    int zoneIndex() const noexcept      { return zone; }
    float level() const noexcept        { return env; }
    float loudness() const noexcept     { return env * gain; }   // para escolher a voz que sai primeiro

    /** Soma a voz em out[canal][quadro]. `underruns` conta os quadros que faltaram no anel.
        `pitchOffset` (27/10): o desvio de tom do canal em semitons, um por quadro (o pitch wheel e o vibrato do CC1,
        ChannelPitch.h), vezes o keyTrack da zona (o martelo, que não segue a tecla, também não dobra); nulo = nenhum,
        e o som sai bit a bit igual ao de antes. */
    void render (const PianoSet& set, VoiceStream& stream, float* const* out, int numOutChannels, int numFrames,
                 std::uint32_t& underruns, const float* pitchOffset = nullptr) noexcept
    {
        if (! active || numOutChannels <= 0 || zone < 0 || zone >= (int) set.zones.size())
            return;

        if (pitchOffset == nullptr && offsetSemis != 0.0f)   // o canal voltou ao descanso: o tom da nota, exato
        {
            offsetSemis = 0.0f;
            ratio = baseRatio;
        }

        const auto& z = set.zones[(std::size_t) zone];
        const auto total = z.totalFrames;
        const auto headFrames = z.headFrames();
        const auto streamed = stream.available (gen);   // -1: o streamer ainda não chegou nesta geração
        constexpr float scale = 1.0f / 32768.0f;

        auto sampleAt = [&] (std::int64_t f, int ch, bool& missing) -> float
        {
            if (f < 0)
                f = 0;

            if (f < headFrames)
                return (float) z.head[(std::size_t) (f * 2 + ch)] * scale;

            if (f >= total)
                return 0.0f;

            if (f < streamed)
                return stream.frame (f)[ch];

            missing = true;
            return 0.0f;
        };

        for (int i = 0; i < numFrames; ++i)
        {
            if (envStep != 0.0f)
            {
                env += envStep;

                if (env >= envTarget)
                {
                    env = envTarget;
                    envStep = 0.0f;
                }
            }
            else if (releasing)
            {
                env *= releaseCoeff;

                if (env < 1.0e-4f)   // -80 dB: a nota acabou
                {
                    finish (stream);
                    return;
                }
            }

            const auto index = (std::int64_t) position;

            if (index + 2 >= total)
            {
                finish (stream);
                return;
            }

            const float frac = (float) (position - (double) index);
            const float amp = env * gain;
            bool missing = false;

            float y[2];

            for (int ch = 0; ch < 2; ++ch)
                y[ch] = hermite (sampleAt (index - 1, ch, missing), sampleAt (index, ch, missing),
                                 sampleAt (index + 1, ch, missing), sampleAt (index + 2, ch, missing), frac);

            if (numOutChannels >= 2)
            {
                out[0][i] += y[0] * amp;
                out[1][i] += y[1] * amp;
            }
            else
            {
                out[0][i] += 0.5f * (y[0] + y[1]) * amp;   // saída mono: a média dos dois lados
            }

            if (missing)
                ++underruns;

            if (pitchOffset != nullptr)
            {
                if (const float semis = pitchOffset[i] * bendScale; semis != offsetSemis)
                {
                    offsetSemis = semis;
                    ratio = semis == 0.0f ? baseRatio : baseRatio * std::exp2 ((double) semis / 12.0);
                }
            }

            position += ratio;
        }

        stream.markConsumed ((std::int64_t) position - 4);
    }

private:
    static float hermite (float xm1, float x0, float x1, float x2, float frac) noexcept
    {
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * frac + c2) * frac + c1) * frac + x0;
    }

    void finish (VoiceStream& stream) noexcept
    {
        stream.stop();
        reset();
    }

    double outRate = 48000.0;
    double position = 0.0, ratio = 1.0, baseRatio = 1.0;   // baseRatio: sem o bend do canal
    float offsetSemis = 0.0f, bendScale = 1.0f;            // o desvio que está na razão agora e o keyTrack da zona
    float env = 0.0f, envTarget = 1.0f, envStep = 0.0f, releaseCoeff = 1.0f, gain = 1.0f;
    int zone = -1, noteKey = -1;
    std::uint32_t gen = 0;
    bool active = false, releasing = false;
};

} // namespace whynot::audio
