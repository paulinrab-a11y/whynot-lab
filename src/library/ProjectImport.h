#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

#include <vector>

namespace whynot::library
{

/** O outro projeto, lido só como dado (o WnlFormat, sem montar o motor): o que a tela oferece para trazer. */
struct ImportSource
{
    juce::File file;
    Project project;

    struct PatternInfo
    {
        juce::String id, name;
        juce::StringArray channels;   // os nomes dos canais que tocam nele
        int steps = 0, notes = 0;     // passos ligados e notas
    };

    std::vector<PatternInfo> patterns;   // na ordem do projeto; só os que têm alguma coisa
};

struct ImportOptions
{
    juce::StringArray patterns;   // ids ou nomes dos padrões a trazer
    bool mixer = true;            // a trilha de mixer de cada canal vem com os efeitos; sem isso, só o volume e o pan
    bool transpose = false;       // as notas vão para o tom do projeto aberto (o caminho mais curto, de -6 a +5)
};

struct ImportReport
{
    juce::StringArray patterns, channels, tracks;   // ids novos no projeto aberto
    int semitones = 0;
};

/** Lê o .wnl. Qualquer thread. */
juce::Result readImportSource (const juce::File& wnl, ImportSource& out);

/** Copia para `dest` (o projeto aberto) os padrões escolhidos e os canais que eles usam, com ids novos.
    - nome repetido ganha " (de <projeto>)";
    - o sample fica com o caminho absoluto do outro projeto: o próximo salvar copia para a pasta samples deste;
    - cada canal ganha uma trilha de mixer, cópia da dele (com os efeitos, se `mixer`), antes do master;
    - os envios e o sidechain só ficam entre as trilhas trazidas; a saída que ia para outra trilha vai para o master.
    Quem chama faz disso um passo de desfazer (o comando project.importFrom). */
juce::Result importInto (Project& dest, const ImportSource&, const ImportOptions&, ImportReport&);

/** De quantos semitons andar para levar a tônica `from` à `to`, pelo caminho mais curto (-6 a +5). */
int shortestTranspose (int fromRoot, int toRoot) noexcept;

} // namespace whynot::library
