#pragma once

// Presets do WHYNOT Synth: todos próprios, montados em código a partir dos parâmetros do motor.
// Volume nivelado por medição (PresetLevels.inc, gerado por whynot_synth_tests --levels): a mesma
// nota e força dão loudness parecida em todos, sem clipar.

#include "SynthParams.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace whynot::synth
{

struct GenreInfo
{
    const char* id;        // "trap", "funk", "rap", "pop", "drill", "rnb", "phonk", "lofi"
    const char* name;      // "Trap", "Funk BR"...
    std::uint32_t bit;
};

struct CategoryInfo
{
    const char* id;        // "808", "bass", "lead"...
    const char* name;      // "808", "Baixo", "Lead"...
    int testKey;           // nota usada para medir e nivelar (808 e baixo em Dó2, o resto em Dó4)
};

std::span<const GenreInfo> allGenres();
std::span<const CategoryInfo> allCategories();
const GenreInfo* findGenre (std::string_view id);
const CategoryInfo* findCategory (std::string_view id);

struct Preset
{
    std::string id;            // estável: vai para o .wnl ("808-limpo")
    std::string name;          // "808 Limpo"
    std::string category;      // id da categoria
    std::uint32_t genres = 0;  // bits de GenreInfo
    std::string description;
    std::vector<float> values; // todos os parâmetros, com o volume já nivelado
    bool levelled = false;     // tem volume medido em PresetLevels.inc

    bool hasGenre (std::string_view genreId) const;
    std::vector<std::string> genreIds() const;
};

/** Todos os presets (montados na primeira chamada, fora da thread de áudio). */
std::span<const Preset> allPresets();
const Preset* findPreset (std::string_view id);

/** Filtra por categoria, gênero e texto (vazio = qualquer). A busca ignora acentos e maiúsculas. */
std::vector<const Preset*> findPresets (std::string_view category, std::string_view genre, std::string_view search);

/** O timbre "Init" (uma serra, filtro aberto). */
const Preset& initPreset();

//==============================================================================
// Nivelamento

/** Alvos da medição: loudness momentânea máxima (LUFS) de uma nota e pico máximo (dBFS). */
constexpr float levelTargetLufs = -16.0f;
constexpr float levelPeakCeilingDb = -3.0f;

/** Nota de teste: duração e força usadas para medir cada preset. */
constexpr double levelNoteSeconds = 1.5;
constexpr float levelVelocity = 100.0f / 127.0f;

/** Timbres de acorde (pad, keys, cordas, metais, vox): um acorde de 4 notas (menor com sétima) também
    precisa caber abaixo deste pico. Medido em 16/10: com o nível de uma nota só, 64 dos 135 passavam
    do joelho do soft clip (-1,9 dBFS) num acorde, e 11 chegavam a 0 dBFS. */
constexpr float levelChordPeakCeilingDb = -2.5f;
bool isChordCategory (std::string_view category) noexcept;

} // namespace whynot::synth
