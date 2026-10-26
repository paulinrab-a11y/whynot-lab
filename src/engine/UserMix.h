#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

#include <map>
#include <optional>
#include <vector>

namespace whynot::usermix
{

/** As cadeias e os presets de efeito que o artista salva (pedido de 21/10): ficam na pasta de dados do app,
    fora dos projetos, e valem para todos eles.

    - Cadeia: `<dados>/cadeias/<nome>.wnl`, um projeto mínimo cujo master leva os efeitos, no formato do .wnl
      (com a versão do esquema e o estado dos VST3). O arquivo pode ir para outra pessoa.
    - Preset de efeito nativo: `<dados>/meus-presets.json`, uma lista { type, id, name, params }. O id é
      "user:" + o nome simplificado; o .wnl guarda o id e os valores, então o projeto abre igual mesmo sem o preset.

    Tudo na thread de mensagem (lê e grava arquivo). A leitura dos presets fica em memória até o arquivo mudar. */

void setDataFolder (const juce::File&);
juce::File chainsFolder();
juce::File presetsFile();

constexpr const char* userPrefix = "user:";
inline bool isUserPresetId (const juce::String& id)   { return id.startsWith (userPrefix); }

struct UserPreset
{
    juce::String type, id, name;
    std::map<juce::String, float> params;
};

/** Os presets do artista para um efeito, em ordem alfabética. */
std::vector<UserPreset> presetsFor (const juce::String& type);
std::optional<UserPreset> findPreset (const juce::String& type, const juce::String& id);

/** Grava (ou substitui, se o nome já existe nesse efeito). idOut recebe o id. */
juce::Result savePreset (const juce::String& type, const juce::String& name, const std::map<juce::String, float>& params,
                         juce::String& idOut);
juce::Result deletePreset (const juce::String& type, const juce::String& id);

/** Nomes das cadeias salvas, em ordem alfabética. */
juce::StringArray chainNames();
/** A cadeia leva também o rack (os macros, bloco 4c), ligado aos efeitos pelos ids do arquivo. */
juce::Result saveChain (const juce::String& name, const std::vector<Insert>& inserts, const std::vector<Macro>& macros = {});
juce::Result loadChain (const juce::String& name, std::vector<Insert>& out, std::vector<Macro>* macros = nullptr);

/** Tira a cadeia da lista: o arquivo vai para `cadeias/apagadas` (dá para recuperar). */
juce::Result deleteChain (const juce::String& name);

} // namespace whynot::usermix
