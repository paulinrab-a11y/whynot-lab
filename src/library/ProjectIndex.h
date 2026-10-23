#pragma once

#include <JuceHeader.h>

#include <vector>

namespace whynot::library
{

/** Um projeto da tela Projetos: o que se lê do .wnl sem montar o motor (o JSON só como dado). */
struct ProjectEntry
{
    juce::File file;
    juce::String name;               // o nome do arquivo, sem o .wnl (o que a pessoa reconhece)
    double bpm = 0.0;
    int timeSigNum = 4, timeSigDen = 4;
    int keyRoot = 0;                 // 0 = Dó ... 11 = Si
    juce::String scale;
    bool hasSong = false;            // a playlist tem blocos: a prévia toca a música; senão, o padrão 4 vezes
    juce::int64 size = 0;
    juce::Time modified;
    juce::String hash;               // do conteúdo do .wnl: a prévia guarda o hash de quando foi feita
    juce::String error;              // não abriu (os outros campos ficam no padrão)

    /** "Am", "C", "F#m": a tônica e um "m" nas escalas menores. */
    juce::String keyName() const;
};

/** O que o .preview.json guarda ao lado do FLAC. */
struct PreviewInfo
{
    bool valid = false;
    juce::String wnlHash;            // o hash do .wnl que gerou a prévia; outro hash = desatualizada
    double seconds = 0.0;
    double sampleRate = 48000.0;
    float lufs = -100.0f;            // integrado (BS.1770); -100 = silêncio
    float truePeakDb = -100.0f;
    juce::String what;               // "song" ou "pattern"
    juce::Time renderedAt;
    std::vector<float> peaks;        // de 0 a 1, o pico de cada fatia: a onda da lista e do deck
};

/** Estado da prévia de um projeto, na ordem em que a tela mostra. */
enum class PreviewState { none, ok, stale, queued, rendering, failed };

/** <pasta do projeto>/preview, e lá dentro <nome>.flac e <nome>.preview.json (decisão do usuário, 21/10). */
juce::File previewFolder (const juce::File& wnl);
juce::File previewAudio (const juce::File& wnl);
juce::File previewJson (const juce::File& wnl);

/** FNV-1a de 64 bits do conteúdo, em hex. Basta para saber se o arquivo mudou. */
juce::String hashBytes (const void* data, size_t size);
juce::String hashFile (const juce::File&);

/** Lê o .wnl pelo WnlFormat, sem motor. Qualquer thread. */
ProjectEntry readEntry (const juce::File& wnl);

PreviewInfo readPreviewInfo (const juce::File& wnl);
juce::Result writePreviewInfo (const juce::File& wnl, const PreviewInfo&);

/** O estado pelo que está no disco: sem prévia, ok ou desatualizada (as filas ficam com a biblioteca). */
PreviewState diskState (const ProjectEntry&, const PreviewInfo&);

/** Os .wnl das pastas, até três níveis abaixo de cada raiz (<raiz>/<projeto>/<nome>.wnl), sem entrar
    em history, preview, samples e nas pastas escondidas. Sem repetir, em ordem de caminho. */
juce::Array<juce::File> findProjects (const juce::Array<juce::File>& roots);

} // namespace whynot::library
