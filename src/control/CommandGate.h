#pragma once

// O portão dos pedidos da IA (Fase 4): antes de um comando rodar pela ponte, decide se ele roda, se pede
// a sua confirmação ou se é recusado, pela tabela de risco (CommandPolicy) e pelas pastas permitidas.
// Também monta o project.inspect. Ver docs/fase-4-plano.md, seções 3.3, 5.3 e 5.4.

#include <JuceHeader.h>

#include <string>
#include <vector>

namespace whynot
{
class AppController;
struct Project;
}

namespace whynot::control
{

/** As pastas que a IA usa sem perguntar, já normalizadas e com os links resolvidos (seção 5.4). */
struct AllowedFolders
{
    std::vector<std::string> read, write;
};

/** A pasta do projeto aberto, Música\WHYNOT LAB e a pasta da IA (leitura e escrita); o kit e as pastas
    extras do navegador de sons (só leitura). Monte a cada pedido: a pasta do projeto muda ao salvar e abrir. */
AllowedFolders allowedFolders (AppController&, const juce::File& scratchFolder);

struct GateDecision
{
    enum class Kind { run, confirm, refuse };

    Kind kind = Kind::refuse;
    juce::String reason;     // confirm e refuse: em português, para a IA e para a tela
    juce::var params;        // run e confirm: os parâmetros com o caminho já normalizado (é com eles que o comando roda)
};

/** O que fazer com um pedido da IA (seção 5.3): rodar, pedir confirmação ou recusar. */
GateDecision evaluate (const juce::String& commandId, const juce::var& params, AppController&, const AllowedFolders&);

/** O JSON numa forma só (chaves em ordem, sem espaços): o ticket da confirmação fica preso a ele (seção 5.5). */
juce::String canonicalJson (const juce::var&);

/** Texto de até 4 KB e listas de até 5.000 itens nos parâmetros. Vazio = dentro dos limites. */
juce::String checkParamLimits (const juce::var& params);

/** O projeto em JSON, com o estado dos plugins (base64) trocado pelo tamanho. `sections` vazio = tudo;
    `pattern` e `channel` filtram os padrões e os canais (e as raias dos padrões). */
juce::var inspectProject (const Project&, const juce::String& pattern, const juce::String& channel, const juce::StringArray& sections);

} // namespace whynot::control
