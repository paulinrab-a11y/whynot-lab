#pragma once

#include <JuceHeader.h>

#include "../audio/SpscQueue.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace whynot::library
{

/** Os decks da tela Projetos: tocam a prévia (FLAC) de um projeto sem abrir o projeto.

    - Thread de mensagem: load, unload, release, play, pause, seek, setLoop e service (um Timer de ~100 ms).
    - Thread de leitura (própria): decodifica o arquivo à frente, num anel por deck (~2,7 s a 48 kHz, 1 MB).
      O loop também é dela: ao chegar no fim do trecho, volta a ler do começo, e o anel continua contínuo.
    - Thread de áudio: só lê os anéis. Sem alocação, trava nem disco. A "cena" (o arquivo de cada deck) chega
      por uma fila sem trava e sai pela fila de descarte, apagada na thread de mensagem.

    Os decks andam no mesmo relógio, então a troca entre A e B é só de ganho, com rampa de 20 ms.
    Quando uma cena nova troca o arquivo ou a posição de um deck que está soando, só esse deck desce 5 ms,
    troca e sobe; o deck mudo troca na hora, e o que não mudou continua sem nenhuma emenda.
    Na mesma taxa da placa a amostra sai idêntica à do arquivo; em outra taxa, interpolação cúbica (Hermite).
*/
class DeckPlayer : public juce::AudioIODeviceCallback
{
public:
    static constexpr int numDecks = 2;

    /** Trecho que repete, em segundos do arquivo (end <= start = sem loop). */
    struct Loop
    {
        double start = 0.0, end = 0.0;
        bool active() const noexcept   { return end > start; }
    };

    explicit DeckPlayer (juce::AudioFormatManager&);
    ~DeckPlayer() override;

    //==========================================================================
    // Thread de mensagem

    /** Põe o arquivo no deck, a partir de `startSeconds` (dentro do loop, se houver). Continua tocando se já
        estava. false = não abriu. */
    bool load (int deck, const juce::File&, double startSeconds = 0.0, Loop = {});
    void unload (int deck);

    /** Tira o arquivo de todos os decks (para regravar a prévia). true se ele estava em algum. */
    bool release (const juce::File&);

    /** Junta as mudanças de vários decks numa cena só: elas entram no mesmo bloco de áudio. */
    void beginChange() noexcept   { ++batchDepth; }
    void endChange();

    void play();
    void pause();
    bool isPlaying() const noexcept   { return playing.load(); }

    /** Leva o deck para outro ponto do arquivo, mantendo o loop e o que estiver tocando. */
    void seek (int deck, double seconds);

    /** Liga, troca ou desliga o loop do deck, a partir de onde ele está. */
    void setLoop (int deck, Loop);
    Loop loopOf (int deck) const;

    /** Ganho do deck (1 = como foi renderizado). A troca anda em rampa de 20 ms. */
    void setDeckGain (int deck, float newGain);
    float deckGain (int deck) const;

    void service();

    bool isLoaded (int deck) const;
    juce::File fileOf (int deck) const;
    double lengthSeconds (int deck) const;

    /** Onde o deck está no arquivo (com loop, dentro do trecho). */
    double positionSeconds (int deck) const;

    /** Todo deck carregado chegou ao fim do arquivo (com loop, nunca). */
    bool reachedEnd() const;

    /** Este deck chegou ao fim do arquivo (o combo passa para o próximo). */
    bool deckFinished (int deck) const;

    /** Blocos em que algum deck ficou sem áudio pronto (o disco não acompanhou). Para o autoteste. */
    int underruns() const noexcept    { return underrunCount.load(); }

    /** Para o autoteste, sem placa: a taxa que o callback vai simular. */
    void prepareForTest (double sampleRate)   { deviceRate.store (sampleRate); }

    //==========================================================================
    // juce::AudioIODeviceCallback (thread de áudio)
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

private:
    struct Stream;
    struct Scene;
    class Reader;

    std::shared_ptr<Stream> openStream (const juce::File&, double startSeconds, Loop);
    void publishScene();
    bool publishPending();
    void collectGarbage();
    void takeScenes() noexcept;
    /** Ganho que anda em linha reta até o alvo em um tempo fixo, qualquer que seja a distância: na troca A/B os dois
        decks andam juntos, e a soma de dois sinais iguais não afunda no meio. Só a thread de áudio. */
    struct Ramp
    {
        float now = 0.0f, target = 0.0f, step = 0.0f;
        int left = 0;

        void aim (float t, int samples) noexcept
        {
            if (t == target)
                return;

            target = t;
            left = juce::jmax (1, samples);
            step = (target - now) / (float) left;
        }

        void jump() noexcept        { now = target; left = 0; }

        float next() noexcept
        {
            if (left > 0 && --left == 0)
                now = target;   // chega exato, sem resto de arredondamento
            else if (left > 0)
                now += step;

            return now;
        }
    };

    void renderStream (Stream&, float* left, float* right, int numSamples, double step, Ramp& ramp,
                       float& fade, float fadeTarget, float fadeStep, bool& underrun) noexcept;

    juce::AudioFormatManager& formats;
    std::unique_ptr<Reader> reader;

    // Thread de mensagem: a última cena pedida (a tela lê daqui; o áudio pode estar um bloco atrás).
    std::array<std::shared_ptr<Stream>, numDecks> current;
    std::unique_ptr<const Scene> pending;   // "o mais novo vence" se a fila estiver cheia
    int batchDepth = 0;
    bool batchDirty = false;
    size_t inFlight = 0;                    // cenas entregues ao áudio e ainda não devolvidas

    // Mesma regra do RtObjectSlot: com inFlight < tamanho da fila, a fila de descarte nunca enche.
    static constexpr size_t queueSize = 8;
    audio::SpscQueue<const Scene*, queueSize> toAudio, garbage;

    std::atomic<bool> playing { false };
    std::array<std::atomic<float>, numDecks> gainTarget;
    std::atomic<double> deviceRate { 48000.0 };
    std::atomic<int> underrunCount { 0 };

    // Só a thread de áudio (o destrutor apaga as duas cenas depois que a placa parou)
    const Scene* scene = nullptr;   // a que soa
    const Scene* next = nullptr;    // a que entra quando os decks que mudam descerem
    float masterFade = 0.0f;
    std::array<Ramp, numDecks> gain {};
    std::array<float, numDecks> deckFade {};
    std::vector<float> mixLeft, mixRight;   // tamanho fixo, alocado no construtor

    static constexpr int maxBlock = 8192;   // bloco maior que isto é tocado em pedaços

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckPlayer)
};

} // namespace whynot::library
