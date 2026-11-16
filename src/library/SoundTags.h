#pragma once

#include <juce_core/juce_core.h>

#include "SoundTagRules.h"

#include <map>

// Tags dos sons (16/11): o tipo que as regras leem no nome e na pasta (SoundTagRules.h, sem JUCE) mais o que o
// usuário marca: tags próprias ("meu kick", "escuro", "já usei") e a correção do tipo, que vale mais que a regra.
// Guardado em <dados>/tags-sons.json, por caminho do arquivo, com gravação atômica. Arquivo estragado não derruba
// nem apaga: vira vazio e o original é guardado ao lado (tags-sons.estragado.json). Só na thread de mensagem.
namespace whynot::soundtags
{

struct UserTags
{
    juce::StringArray labels;     // as tags do usuário, na ordem em que entraram
    bool hasTypes = false;        // o usuário corrigiu o tipo
    Mask types = 0;               // o tipo corrigido (vale no lugar do automático)

    bool empty() const noexcept   { return labels.isEmpty() && ! hasTypes; }
};

class Store
{
public:
    static Store& get();

    /** <dados>/tags-sons.json; lê na hora. O autoteste aponta para a pasta dele. */
    void setFolder (const juce::File& dataFolder);
    juce::File file() const   { return storeFile; }

    /** O que o usuário marcou nesse arquivo (vazio = nada). */
    UserTags of (const juce::File&) const;

    /** As tags do arquivo: as regras pelo nome e pela pasta, com a correção do usuário por cima. */
    Tags tagsFor (const juce::File&) const;

    /** Acrescenta e tira tags do usuário em vários arquivos de uma vez; grava. false = não gravou. */
    bool change (const juce::Array<juce::File>& files, const juce::StringArray& add, const juce::StringArray& remove);

    /** Corrige o tipo (types = 0 com clear = true volta ao automático); grava. */
    bool setTypes (const juce::Array<juce::File>& files, Mask types, bool clear);

    /** Todas as tags do usuário, sem repetir, em ordem natural (para o menu e o filtro). */
    juce::StringArray allLabels() const;

    /** Muda a cada gravação: quem guarda resultado por arquivo sabe quando refazer. */
    int revision() const noexcept   { return rev; }

    /** O aviso da última leitura (arquivo estragado), ou vazio. */
    juce::String loadProblem() const   { return problem; }

private:
    Store() = default;
    void load();
    bool save();
    static juce::String keyOf (const juce::File&);

    juce::File storeFile;
    std::map<juce::String, UserTags> entries;   // chave: o caminho em minúsculas (o Windows não diferencia)
    juce::String problem;
    int rev = 0;
};

} // namespace whynot::soundtags
