#pragma once

#include "Project.h"

namespace whynot::wnl
{

// 1: entregas 3.1 e 3.2. 2: inserts, envios e papel das trilhas de mixer (3.3).
// 3: corte do clipe de áudio em segundos do arquivo ("offsetSeconds"; no 2 era "offset" em ticks).
// 4: canal kind "synth" (WHYNOT Synth). Só acréscimo, mas sobe a versão: um app no esquema 3 trocaria
//    o tipo desconhecido por sampler e perderia o canal ao salvar; no 4 ele recusa o arquivo.
constexpr int currentSchemaVersion = 4;

/** Projeto → texto JSON, com chaves em ordem fixa e indentação estável (diff limpo). */
juce::String toJson (const Project&);

/** O mesmo texto do toJson, pelo caminho antigo (árvore de juce::var e juce::JSON). Mais lento: existe só
    para o autoteste provar que o toJson escreve igual, byte a byte. Campo novo no .wnl entra nos dois. */
juce::String toJsonReference (const Project&);

/** Texto JSON → projeto. Migra versões antigas; recusa versões mais novas que este app. */
juce::Result fromJson (const juce::String& json, Project& out);

/** Garante as invariantes do modelo: master existe, todo padrão tem uma raia de
    passos (do tamanho certo) para cada canal, o padrão atual existe, saídas válidas.
*/
void normalise (Project&);

/** Grava o projeto em `file`. Com collectSamples, copia para `samples/` os samples
    que estão fora da pasta do projeto e passa a referenciá-los por caminho relativo
    (o original fica em originalPath). `previousFolder` resolve caminhos relativos
    de um projeto que está sendo salvo em outra pasta ("Salvar como").
    A gravação é atômica: arquivo temporário e troca.
*/
juce::Result save (Project&, const juce::File& file, bool collectSamples, const juce::File& previousFolder = {});

/** Antes de gravar por cima de um .wnl de esquema mais antigo, copia o original para
    `history/<nome> (esquema N).wnl` (uma vez por esquema): um app antigo não abre o convertido.
    Devolve a cópia, ou File() se não precisou. save() já chama. */
juce::File keepOlderSchema (const juce::File& file);

/** Lê um .wnl do disco. */
juce::Result load (const juce::File& file, Project& out);

} // namespace whynot::wnl
