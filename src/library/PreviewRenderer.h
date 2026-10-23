#pragma once

#include <JuceHeader.h>

#include "ProjectIndex.h"

#include <deque>
#include <map>

namespace whynot
{
class PluginCatalog;
class SampleCache;
}

namespace whynot::library
{

/** A fila das prévias: um projeto por vez, renderizado a partir do .wnl salvo (sem abrir o projeto)
    e gravado em preview/<nome>.flac (48 kHz, 16 bits), com o .preview.json ao lado.

    - O render é o da exportação em segundo plano (RenderJob): o Edit do render nasce e morre na thread
      de mensagem; o laço do render e a codificação do FLAC (com o LUFS e a onda) rodam numa thread de trabalho.
    - Espera enquanto `shouldWait` disser (a música tocando, uma exportação): se já começou, o render é
      cancelado e o projeto volta para a frente da fila (decisão do usuário: o play pausa o render).
    - Espera também com pouca memória livre (minFreeMemoryMB): o render carrega os plugins do projeto de novo.
    - Só troca o FLAC depois de tirá-lo dos decks. Se o arquivo ainda estiver aberto, tenta de novo.
*/
class PreviewRenderer : private juce::Timer
{
public:
    struct Host
    {
        tracktion::Engine& engine;
        SampleCache& cache;
        const PluginCatalog* catalog = nullptr;
        std::function<bool()> shouldWait;                       // tocando ou exportando
        std::function<void (const juce::File& flac)> release;   // tira o FLAC dos decks antes de trocar
    };

    explicit PreviewRenderer (Host);
    ~PreviewRenderer() override;

    /** Põe o projeto na fila (urgent = na frente). Se já está na fila ou renderizando, nada muda. */
    void enqueue (const juce::File& wnl, bool urgent = false);

    /** queued, rendering ou failed; none se a fila não sabe dele (aí vale o que está no disco). */
    PreviewState stateOf (const juce::File& wnl, float* progress = nullptr, juce::String* error = nullptr) const;

    /** Nada na fila, nada renderizando, nenhum FLAC esperando para trocar. */
    bool isIdle() const noexcept;
    int numQueued() const noexcept           { return (int) queue.size(); }

    /** O projeto renderizando agora (File() = nenhum) e quanto já foi, de 0 a 1. */
    juce::File renderingNow (float* progress = nullptr) const;

    /** Por que a fila está parada agora ("a música está tocando"...); vazio = andando ou sem nada. */
    juce::String waitingReason() const       { return waitReason; }

    /** Um projeto terminou (ok ou não): a biblioteca relê a entrada dele. */
    std::function<void (const juce::File& wnl, bool ok)> onFinished;

    /** Abaixo disto (MB livres) a fila espera: o render carrega os plugins do projeto uma segunda vez.
        0 = não olha (o autoteste). */
    int minFreeMemoryMB = 512;

    static constexpr int numPeaks = 600;
    static constexpr int patternLoops = 4;   // projeto sem blocos na playlist: o padrão atual 4 vezes

    /** A codificação, separada para o autoteste: WAV → FLAC de 16 bits, com o LUFS, o pico real e a onda. */
    static juce::Result encode (const juce::File& wav, const juce::File& flac, PreviewInfo&);

private:
    struct Running;

    void timerCallback() override;
    void startNext();
    void finishRunning();
    bool commit (const juce::String& path);   // troca o .flac e grava o json; false = ainda em uso
    void fail (const juce::File& wnl, const juce::String& why);

    Host host;
    std::deque<juce::File> queue;
    std::unique_ptr<Running> running;
    std::map<juce::String, juce::String> failures;       // caminho → por que falhou
    std::map<juce::String, PreviewInfo> waitingCommit;   // caminho → o json que entra quando o FLAC trocar
    juce::String waitReason;
};

} // namespace whynot::library
