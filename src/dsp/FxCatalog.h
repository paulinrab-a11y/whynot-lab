#pragma once

// Catálogo dos efeitos nativos, presets e estilos de mixagem por gênero (sem JUCE).

#include "Processor.h"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace whynot::dsp
{

//==============================================================================
// Efeitos

std::span<const FxInfo* const> allEffects();
const FxInfo* findEffect (std::string_view type);
std::unique_ptr<Processor> createProcessor (std::string_view type);

//==============================================================================
// Presets

struct PresetValue
{
    const char* param;
    float value;
};

struct Preset
{
    const char* type;          // efeito
    const char* id;            // estável
    const char* name;
    const char* description;
    std::span<const PresetValue> values;   // só o que muda em relação ao padrão
};

std::span<const Preset> allPresets();
const Preset* findPreset (std::string_view type, std::string_view id);
std::vector<const Preset*> presetsFor (std::string_view type);

/** Todos os valores (padrão + preset, se houver) na ordem dos parâmetros do efeito. */
std::vector<float> presetValues (const FxInfo&, const Preset* = nullptr);

//==============================================================================
// Papéis das trilhas e estilos de mixagem

struct RoleInfo
{
    const char* id;            // "kick", "bass", "vocal"...
    const char* name;          // "Kick / bumbo"
};

std::span<const RoleInfo> allRoles();
const RoleInfo* findRole (std::string_view id);

/** Adivinha o papel de uma trilha pelo nome ("808" = bass, "Voz" = vocal...). */
const char* guessRole (std::string_view trackName);

struct ChainItem
{
    const char* type;
    const char* preset;                  // nullptr = valores padrão
    const char* sidechainRole = nullptr; // compressor disparado pela trilha deste papel (ex.: "kick")
};

constexpr float noSend = -100.0f;

struct RoleRecipe
{
    const char* role;
    std::span<const ChainItem> chain;
    float reverbSendDb = noSend;
    float delaySendDb = noSend;
};

struct MixStyle
{
    const char* id;            // "trap", "funk", "rap", "pop"
    const char* name;
    const char* description;
    float targetLufs;
    const char* loudnessNote;
    std::span<const RoleRecipe> roles;
    std::span<const ChainItem> master;
    std::span<const ChainItem> reverbBus;
    std::span<const ChainItem> delayBus;

    const RoleRecipe* recipeFor (std::string_view role) const noexcept;
};

std::span<const MixStyle> allStyles();
const MixStyle* findStyle (std::string_view id);

} // namespace whynot::dsp
