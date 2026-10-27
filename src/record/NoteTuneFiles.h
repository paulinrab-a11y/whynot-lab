#pragma once

// Afinação nota a nota (frente C de 27/10): o lado dos arquivos.
// - a análise de um áudio (dsp::analyseNotes) com cache por arquivo: na memória e no disco (pasta temporária,
//   "WHYNOT LAB/notas"), pela chave caminho + tamanho + data; um arquivo regravado é analisado de novo;
// - o arquivo refeito com as edições: "<nome>_nota_<código>.wav" ao lado do original, em WAV 32 bits float. O código
//   sai das notas, então cada versão das edições tem o arquivo dela (o desfazer volta a uma que já existe). Se ele
//   sumir (o projeto foi copiado sem ele), playedFile escreve de novo. O original nunca muda.
// Tudo bloqueia: a tela chama a análise numa thread dela; os comandos e o Projector, na thread de mensagens.

#include <JuceHeader.h>

#include "../dsp/NoteTune.h"

#include <memory>

namespace whynot::notetune
{

/** O áudio de um arquivo, [canal][amostra] (um ou dois canais). */
struct SourceAudio
{
    std::vector<std::vector<float>> channels;
    double sampleRate = 0.0;
};

/** Lê o arquivo (até uma hora; os dois últimos ficam na memória). Num MP3, sem o atraso do codificador do começo
    (o mesmo que o clipe da playlist pula), então o tempo 0 é o mesmo do bloco. */
std::shared_ptr<const SourceAudio> loadAudio (const juce::File&, juce::String& error);

/** A análise do arquivo: do cache (memória ou disco) ou feita agora. Com `cancel` ligado no meio, devolve nullptr
    (e nada vai para o cache). */
std::shared_ptr<const dsp::NoteAnalysis> analyse (const juce::File&, juce::String& error, const std::atomic<bool>* cancel = nullptr);

/** Só da memória: nullptr se o arquivo ainda não foi analisado nesta execução. */
std::shared_ptr<const dsp::NoteAnalysis> cachedAnalysis (const juce::File&);

/** Para o autoteste: quantas análises saíram do cache do disco e quantas foram feitas. */
struct CacheCounts { int memory = 0, disk = 0, computed = 0; };
CacheCounts cacheCounts();
void forgetMemoryCache();   // o autoteste confere o cache do disco

/** O arquivo refeito para estas notas (ainda pode não existir). */
juce::File renderedFileFor (const juce::File& source, const std::vector<TunedNote>&, bool formantFollows);

/** Refaz `target` a partir do original com as notas. */
juce::Result render (const juce::File& source, const std::vector<TunedNote>&, bool formantFollows, const juce::File& target,
                     dsp::NoteRenderStats* stats = nullptr);

/** O arquivo que os blocos do canal tocam: o original sem nota editada; senão o refeito (escrito se ainda não existe). */
juce::File playedFile (const juce::File& source, const std::vector<TunedNote>&, bool formantFollows);

} // namespace whynot::notetune
