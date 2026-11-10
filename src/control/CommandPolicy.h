#pragma once

// A classe de risco de cada comando quando quem pede é a IA (Fase 4; docs/fase-4-plano.md, seção 5.3), sem JUCE.
// É "de todos, só acrescentar": quem cria um comando põe uma linha em CommandPolicy.cpp, em ordem alfabética.
// Comando sem linha pede confirmação, e o autoteste da ponte avisa quais estão faltando.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace whynot::control
{

enum class Risk { free, confirm, blocked };
enum class Access { read, write };

struct PathParam
{
    std::string_view name;          // vazio = o comando não recebe caminho
    Access access = Access::read;
    std::string_view extension;     // o comando troca a extensão antes de gravar (".wav"); vazio = como veio
    bool list = false;              // o parâmetro é uma lista de caminhos (library.refresh: folders)
    bool relative = false;          // aceita caminho relativo à pasta do projeto (playlist.addItem: file)
    bool whenPathLike = false;      // só às vezes é caminho (favorites.set: id): confere quando começa com barra ou "X:"
};

/** Quantos parâmetros de caminho um comando pode ter (o `path` e os `more`). */
inline constexpr std::size_t maxPathParams = 3;

/** Quantos caminhos cabem numa lista (cada um custa uma consulta ao disco, na thread de mensagem). */
inline constexpr std::size_t maxPathsPerList = 64;

struct CommandRule
{
    Risk risk = Risk::confirm;
    std::string_view why;           // por que confirma ou é bloqueado, em português
    PathParam path;                 // o primeiro parâmetro de caminho (o command.list mostra este)
    std::array<PathParam, maxPathParams - 1> more {};   // os outros (project.exportPack: folder e tag); os vagos ficam no fim
    bool confirmIfUnsaved = false;  // project.new e project.open: perderia o que não foi salvo
    bool savesProject = false;      // project.save: sem path, grava por cima do projeto aberto
    bool writesIntoFolder = false;  // project.exportStems: a pasta (o parâmetro de escrita) já com arquivos pede confirmação

    /** O parâmetro de caminho de número `index` (0 = `path`), ou nullptr depois do último. */
    constexpr const PathParam* pathParam (std::size_t index) const noexcept
    {
        const auto* p = index == 0 ? &path : (index < maxPathParams ? &more[index - 1] : nullptr);
        return p != nullptr && ! p->name.empty() ? p : nullptr;
    }

    /** A regra do parâmetro com este nome, ou nullptr se ele não é caminho. */
    constexpr const PathParam* findPathParam (std::string_view name) const noexcept
    {
        for (std::size_t i = 0; i < maxPathParams; ++i)
            if (const auto* p = pathParam (i); p != nullptr && p->name == name)
                return p;

        return nullptr;
    }
};

//==============================================================================
// Os caminhos de um pedido contra a regra do comando. Sem JUCE: o CommandGate só tira os valores do JSON,
// chama o checkPathArguments e põe de volta os caminhos normalizados.

/** Um valor que veio num parâmetro de caminho. */
struct PathArgument
{
    std::string_view param;
    int index = -1;                 // posição na lista; -1 = o parâmetro é um texto só
    std::string value;              // UTF-8, como a IA mandou
};

struct CheckedPath
{
    std::string_view param;
    int index = -1;
    Access access = Access::read;
    std::string path;               // normalizado, com os links resolvidos: é com ele que o comando roda
    bool outside = false;           // fora das pastas permitidas: pede confirmação
};

struct PathsCheck
{
    bool ok = true;                 // false = recusado (UNC, fluxo alternativo, nome reservado, lista grande demais...)
    std::string reason;             // quando ok == false
    std::vector<CheckedPath> paths; // só os conferidos (valor vazio e id que não é caminho ficam de fora)

    bool anyOutside() const noexcept
    {
        for (auto& p : paths)
            if (p.outside)
                return true;

        return false;
    }
};

/** Confere cada valor pela regra do parâmetro dele: leitura contra `readRoots`, escrita contra `writeRoots`
    (as duas listas vêm do prepareRoots). Valor de parâmetro que a regra não conhece é ignorado; valor vazio
    também (o comando reclama sozinho). `projectFolder` (UTF-8; vazio = projeto nunca salvo) resolve os
    relativos dos parâmetros com `relative`. */
PathsCheck checkPathArguments (const CommandRule&, const std::vector<PathArgument>&,
                               const std::vector<std::string>& readRoots, const std::vector<std::string>& writeRoots,
                               std::string_view projectFolder);

/** A regra do comando, ou nullptr se ele ainda não foi classificado. */
const CommandRule* findRule (std::string_view id) noexcept;

/** A regra do comando; sem classe, confirma ("comando novo, ainda sem classe"). */
const CommandRule& ruleFor (std::string_view id) noexcept;

/** "free", "confirm" ou "blocked", como vai no command.list. */
std::string_view riskName (Risk) noexcept;

/** Para o autoteste: a tabela inteira, em ordem. */
std::size_t ruleCount() noexcept;
std::string_view ruleIdAt (std::size_t index) noexcept;

} // namespace whynot::control
