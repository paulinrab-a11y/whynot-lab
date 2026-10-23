#pragma once

// Pedido em português ("trap triste em Fá menor, 4 compassos, melodia") → ajustes do gerador.
// Sem internet e sem modelo de linguagem: palavras-chave. A IA de verdade (Fase 4) chama os
// comandos ideas.* direto, com os mesmos ajustes.

#include "Genres.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace whynot::ideas
{

struct PromptSettings
{
    std::optional<std::string> genre;
    std::optional<Mood> mood;
    std::optional<Part> part;
    std::optional<int> root;              // 0..11
    std::optional<std::string> scale;     // nome do projeto: minor, major, phrygian...
    std::optional<int> bars;
    std::optional<int> bpm;
    std::optional<double> complexity;
    std::optional<double> density;
    std::vector<std::string> understood;  // o que foi entendido, em português, para mostrar
};

PromptSettings parsePrompt (std::string_view text);

/** Minúsculas e sem acento (UTF-8): "Fá Menor" → "fa menor". */
std::string foldText (std::string_view text, bool lower = true);

} // namespace whynot::ideas
