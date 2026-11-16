#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

#include <vector>

namespace whynot
{
class CommandRegistry;
class ProjectModel;
}

namespace whynot::templates
{

/** Templates de projeto (frente templates, 16/11): o produtor monta uma vez o ponto de partida dele (kit carregado,
    canais roteados, mixer com cadeias, BPM, tom, raias nomeadas) e começa todo beat dali.

    - Do usuário: `<dados>/Templates/<nome>/<nome>.wnl`, uma pasta de projeto como outra qualquer, gravada pelo
      mesmo wnl::save do projeto (os samples coletados em `samples/`, o estado dos plugins dentro do .wnl). A pasta
      pode ir para outra pessoa. Apagar move a pasta para `<dados>/Templates/apagados` (nada some).
    - De fábrica: montados por código a partir do que o app já tem (a tabela fica em TemplateCore.h, sem JUCE).
    - O padrão de "novo projeto": `<dados>/Templates/padrao.txt` com o nome; sem o arquivo, o de fábrica de sempre.

    O que NÃO entra num template: o nome do projeto (o template leva o nome dele e o projeto novo nasce "Sem
    título"), o histórico de desfazer, os autosaves, a pasta history/ e os renders (só o .wnl e samples/ são
    gravados), os canais e as trilhas armados para gravar e o punch. As notas, os padrões, os blocos da playlist,
    a automação e os marcadores ENTRAM (um esqueleto de arranjo); com clearNotes ficam só os canais, o mixer, os
    padrões vazios (com nome e tamanho), as raias e os marcadores.

    Tudo na thread de mensagem (lê e grava arquivo), como as cadeias do usermix. */

void setDataFolder (const juce::File&);
juce::File folder();           // <dados>/Templates
juce::File deletedFolder();    // <dados>/Templates/apagados

/** O nome que o usuário digitou → o nome da pasta: juce::File::createLegalFileName e as regras do TemplateCore.h
    (sem ponto nem espaço no fim, até 64 letras). Pode sair vazio. */
juce::String folderNameOf (const juce::String& name);

/** O .wnl de um template do usuário (exista ou não): <dados>/Templates/<nome>/<nome>.wnl. */
juce::File fileFor (const juce::String& name);

struct Info
{
    juce::String name, description;
    bool factory = false;
    bool isDefault = false;   // o "novo projeto" começa deste
};

/** Os de fábrica na ordem da tabela, depois os do usuário em ordem natural. */
std::vector<Info> list();
juce::StringArray userNames();
bool exists (const juce::String& name);   // de fábrica ou do usuário

/** O template do "novo projeto": "" = o de fábrica de sempre (o kit em 140 BPM). */
juce::String defaultName();
juce::Result setDefault (const juce::String& name);

struct SaveOptions
{
    bool clearNotes = false;   // só a estrutura: sem notas, passos, blocos nem automação
    bool replace = false;      // o nome de um template do usuário que já existe: o antigo vai para os apagados
};

/** Grava `current` como template. `projectFolder` resolve os caminhos relativos do projeto aberto (File() num
    projeto nunca salvo). O projeto aberto não muda. savedName recebe o nome com que ficou (o da pasta). */
juce::Result save (const juce::String& name, const Project& current, const juce::File& projectFolder,
                   const SaveOptions& options, juce::String* savedName = nullptr);

/** Tira o template do usuário da lista: a pasta vai para Templates/apagados. Se era o padrão, o padrão volta
    ao de fábrica. Os de fábrica não se apagam. */
juce::Result remove (const juce::String& name);

/** O que o clearNotes tira de um projeto (usado pelo save; aqui para o autoteste conferir). */
void clearNotes (Project&);

/** Lê um template do usuário como um projeto novo sem nome: os caminhos dos samples viram absolutos, apontando
    para a pasta do template (o projeto ainda não tem pasta). Ao ser salvo em outro lugar, o wnl::save coleta
    esses arquivos para a samples/ do projeto novo; o template não é tocado. */
juce::Result loadForNew (const juce::String& name, Project& out);

//==============================================================================
// O "novo projeto" a partir de um template. O template.new faz request(nome) e chama o AppServices::newProject;
// o AppController::newProject chama os dois ganchos abaixo. Sem pedido, vale o padrão do usuário (se houver).

/** Pede que o próximo newProject comece do template. Confere antes de largar o projeto aberto: o nome existe
    e, no do usuário, o arquivo abre. */
juce::Result request (const juce::String& name);

/** Fecha o pedido (atendido ou não). true se um newProject passou pelo gancho desde o request. */
bool endRequest();

/** Gancho 1, no começo do AppController::newProject: true = `fresh` já é o projeto do template (do usuário, ou
    o "Vazio") e o kit de fábrica não entra. false = segue o projeto de fábrica (e, num template de fábrica com
    kit, o gancho 2 aplica a receita). */
bool beginNewProject (Project& fresh);

/** Gancho 2, depois do kit e antes de limpar o desfazer: a receita do template de fábrica (BPM, tom, glide do 808,
    grupo de corte dos hats, estilo de mixagem e nomes das raias), pelos comandos do app. Sem receita, não faz nada. */
void finishNewProject (CommandRegistry&, const ProjectModel&);

} // namespace whynot::templates
