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

//==============================================================================
// Tetos de um .wnl que vem de fora (10/11): generosos para um projeto de verdade, e um arquivo estragado ou
// montado de propósito é recusado com mensagem em vez de consumir a memória ou travar a abertura.
namespace limits
{
    constexpr juce::int64 fileBytes = 512LL * 1024 * 1024;   // o estado dos plugins vai dentro, em base64
    constexpr int channels = 1000;
    constexpr int mixerTracks = 1000;
    constexpr int patterns = 2000;
    constexpr int lanes = 250000;             // canais x padrões: cada par ganha uma raia de passos
    constexpr int stepCells = 8000000;        // a soma dos passos de todas as raias
    constexpr int notes = 1000000;            // no projeto inteiro
    constexpr int playlistItems = 50000;
    constexpr int markers = 10000;
    constexpr int automationLanes = 5000;
    constexpr int automationPoints = 100000;  // por curva
    constexpr int tunedNotes = 20000;         // por canal
    constexpr int sends = 256;                // por trilha do mixer
    constexpr int midiLinks = 4096;           // lidos; ficam os maxMidiLinks
    constexpr int smallList = 4096;           // fatias, camadas, links de macro: listas que o normalise corta
    constexpr int idLength = 128;             // id maior que isso é como id vazio: o item não entra
    constexpr int nameLength = 256;           // nome maior é cortado
}

/** O que a leitura de um .wnl de fora teve para contar, além do projeto. Quem abre o arquivo para o usuário
    (AppController::openProject) passa um e mostra o aviso; os outros podem ignorar. */
struct LoadReport
{
    /** Caminhos do arquivo que o app não usa (de rede, que saem da pasta do projeto com "..", inválidos):
        ficou só o nome do arquivo, e o sample aparece como ausente. Até 50, cada um com até 300 letras. */
    juce::StringArray refusedPaths;
    int refusedPathCount = 0;

    /** Recursos que o arquivo declara em app.features e este app não conhece: ele foi salvo por um app mais
        novo, e salvar por cima apaga o que este não entende. A interface avisa antes de salvar; o save()
        guarda o original em history/ de qualquer jeito (keepOlderSchema). */
    juce::StringArray unknownFeatures;

    bool savedByNewerApp() const noexcept   { return ! unknownFeatures.isEmpty(); }
};

/** O que o save() deixou de coletar, e por quê: a interface mostra para o usuário saber que a pasta do
    projeto não leva esses arquivos. */
struct SaveReport
{
    juce::StringArray notCollected;   // caminho completo de cada arquivo que ficou de fora de samples/
};

/** Texto JSON de um arquivo → projeto. Migra versões antigas; recusa versões mais novas que este app, arquivo
    acima dos tetos (wnl::limits) e, nos caminhos de sample, troca pelo nome do arquivo o que é de rede, sai
    da pasta do projeto ou é inválido (src/model/WnlPaths.h): abrir um projeto de terceiros não toca a rede. */
juce::Result fromJson (const juce::String& json, Project& out, LoadReport* report = nullptr);

/** O mesmo, para o texto que este app acabou de escrever com o toJson nesta sessão (o desfazer): sem os tetos
    e sem mexer nos caminhos. O sample que o usuário arrastou de uma pasta de rede continua onde está. */
juce::Result fromSnapshot (const juce::String& json, Project& out);

/** Recursos opcionais, mais novos que o esquema atual, que este projeto usa: vão em app.features. Ver a
    tabela em WnlFormat.cpp. */
juce::StringArray usedFeatures (const Project&);

/** Garante as invariantes do modelo: master existe, todo padrão tem uma raia de
    passos (do tamanho certo) para cada canal, o padrão atual existe, saídas válidas.
*/
void normalise (Project&);

/** Grava o projeto em `file`. Com collectSamples, copia para `samples/` os samples
    que estão fora da pasta do projeto e passa a referenciá-los por caminho relativo
    (o original fica em originalPath). `previousFolder` resolve caminhos relativos
    de um projeto que está sendo salvo em outra pasta ("Salvar como").
    Só entra em samples/ arquivo com extensão de áudio (wav, aif, aiff, flac, ogg, mp3): o resto fica
    referenciado onde está e sai no `report` (um .wnl de terceiros não leva documentos junto).
    A gravação é atômica: arquivo temporário e troca.
*/
juce::Result save (Project&, const juce::File& file, bool collectSamples, const juce::File& previousFolder = {},
                   SaveReport* report = nullptr);

/** Antes de gravar por cima de um .wnl de esquema mais antigo, copia o original para
    `history/<nome> (esquema N).wnl` (uma vez por esquema): um app antigo não abre o convertido.
    O mesmo para um .wnl do esquema atual que declara recursos que este app não conhece (app.features):
    a cópia vai para `history/<nome> (app mais novo).wnl`, com número se já existir.
    Devolve a cópia, ou File() se não precisou. save() já chama. */
juce::File keepOlderSchema (const juce::File& file);

/** Lê um .wnl do disco (com as regras do fromJson e o teto de tamanho do arquivo). */
juce::Result load (const juce::File& file, Project& out, LoadReport* report = nullptr);

} // namespace whynot::wnl
