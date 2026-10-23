#pragma once

#include <JuceHeader.h>

#include "TakeMath.h"
#include "../audio/SpscQueue.h"
#include "../common/Text.h"

#include <atomic>
#include <cstdint>

namespace whynot::record
{

/** Grava a entrada de áudio num WAV sem que a thread de áudio aloque, trave ou toque no disco.

    - Thread de áudio: pushInput() a cada bloco da placa, gravando ou não. Quando grava, copia as
      amostras para pedaços fixos numa SpscQueue (sem trava) e anota o pico para o medidor.
    - Thread de fundo (o escritor): tira os pedaços da fila e escreve o WAV (32 bits float, igual à
      entrada, amostra por amostra).
    - Thread de mensagem: start() e stop().

    Cada pedaço leva a posição dele no relógio da placa. Se o relógio pular para a frente (falha de
    buffer, ou fila cheia porque o disco atrasou), o escritor preenche o buraco com silêncio: o take
    continua no tempo certo. Quem chama decide onde o take cai na linha do tempo (ver TakeMath.h).

    No autoteste não há placa: quem chama pushInput() é o próprio teste, com blocos de um WAV conhecido.
*/
class InputRecorder
{
public:
    static constexpr int chunkFrames = 256;
    static constexpr int maxChannels = 2;
    static constexpr std::size_t queueChunks = 1024;   // 2 MB, ~5 s de folga a 48 kHz se o disco engasgar

    /** O que ficou gravado. */
    struct Take
    {
        juce::File file;
        double sampleRate = 0.0;
        int numChannels = 0;
        int64_t firstStreamSample = -1;   // posição da primeira amostra do arquivo no relógio da placa; -1 = nada chegou
        int64_t numSamples = 0;           // amostras no arquivo, com os buracos preenchidos
        int gaps = 0;                     // vezes em que o relógio pulou
        int64_t lostSamples = 0;          // amostras que não couberam na fila (viraram silêncio)
        bool truncated = false;           // o relógio pulou sem explicação (a placa reiniciou): o take para ali
        juce::String error;

        double seconds() const noexcept   { return sampleRate > 0.0 ? (double) numSamples / sampleRate : 0.0; }
    };

    InputRecorder();
    ~InputRecorder();

    //==========================================================================
    // Thread de mensagem

    /** Abre o arquivo e começa a aceitar a entrada. firstInput é o índice (0 = entrada 1) do primeiro
        canal da placa; numChannels é 1 (mono) ou 2. */
    juce::Result start (const juce::File& file, double sampleRate, int firstInput, int numChannels);

    /** Para de aceitar a entrada, escreve o que estava na fila e fecha o arquivo.
        waitForAudioThread: espera (até 100 ms) a thread de áudio terminar o bloco que estava copiando.
        Sem placa rodando (autoteste) não há o que esperar. */
    Take stop (bool waitForAudioThread);

    bool isCapturing() const noexcept           { return capturing.load (std::memory_order_acquire); }

    /** Segundos já escritos no disco (para a tela). */
    double writtenSeconds() const noexcept;

    /** Pico da entrada escolhida desde a última leitura (0 a 1+), gravando ou não: o medidor da trilha armada
        (leitor 0) e o do assistente de gravação (leitor 1) leem cada um o seu. Lê e zera. */
    float takePeak (int reader = 0) noexcept    { return peaks[juce::jlimit (0, 1, reader)].exchange (0.0f, std::memory_order_relaxed); }

    /** A entrada que o medidor mede fora da gravação (gravando, vale a do start()). Thread de mensagem. */
    void setMeteredInput (int firstInput, int numChannels) noexcept;

    //==========================================================================
    // Thread de áudio (ou o autoteste)

    /** Um bloco da entrada. streamSample: posição da primeira amostra no relógio da placa. */
    void pushInput (const float* const* channels, int numChannels, int numSamples, int64_t streamSample) noexcept;

    /** Quantos blocos a thread de áudio já entregou (gravando ou não): o stop() usa para saber
        que um bloco começado antes dele terminou. */
    uint32_t blocksSeen() const noexcept        { return blocks.load (std::memory_order_acquire); }

private:
    struct Chunk
    {
        int64_t streamSample;
        int32_t numFrames;
        int32_t pad;
        float data[maxChannels][chunkFrames];
    };

    using Queue = audio::SpscQueue<Chunk, queueChunks>;
    class Writer;

    std::unique_ptr<Queue> queue;
    std::unique_ptr<Writer> writer;

    std::atomic<bool> capturing { false };
    std::atomic<uint32_t> blocks { 0 };
    std::atomic<int64_t> lost { 0 };
    std::atomic<float> peaks[2] { 0.0f, 0.0f };

    // Escritos pela thread de mensagem antes de capturing = true; a thread de áudio lê uma vez por bloco.
    // Atômicos: um bloco atrasado de um take anterior não lê um valor pela metade.
    std::atomic<int> firstChannel { 0 }, numChannelsToRecord { 1 };
    juce::File writerFile;     // só a thread de mensagem
    double writerRate = 0.0;
    Chunk staging {};   // só a thread de áudio

    JUCE_DECLARE_NON_COPYABLE (InputRecorder)
};

} // namespace whynot::record
