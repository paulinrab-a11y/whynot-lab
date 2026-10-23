#pragma once

// A classe de risco de cada comando quando quem pede é a IA (Fase 4; docs/fase-4-plano.md, seção 5.3), sem JUCE.
// É "de todos, só acrescentar": quem cria um comando põe uma linha em CommandPolicy.cpp, em ordem alfabética.
// Comando sem linha pede confirmação, e o autoteste da ponte avisa quais estão faltando.

#include <cstddef>
#include <string_view>

namespace whynot::control
{

enum class Risk { free, confirm, blocked };
enum class Access { read, write };

struct PathParam
{
    std::string_view name;          // vazio = o comando não recebe caminho
    Access access = Access::read;
    std::string_view extension;     // o comando troca a extensão antes de gravar (".wav"); vazio = como veio
};

struct CommandRule
{
    Risk risk = Risk::confirm;
    std::string_view why;           // por que confirma ou é bloqueado, em português
    PathParam path;
    bool confirmIfUnsaved = false;  // project.new e project.open: perderia o que não foi salvo
    bool savesProject = false;      // project.save: sem path, grava por cima do projeto aberto
    bool writesIntoFolder = false;  // project.exportStems: a pasta já com arquivos pede confirmação
};

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
