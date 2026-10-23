#pragma once

// Parâmetros do WHYNOT Synth (sem JUCE).
//
// Um timbre é uma lista plana de valores (float), na ordem de allParams(). Os ids são estáveis:
// vão para o .wnl, para os presets e para a IA ("l1.wave", "flt.cutoff", "env2.decay"...).
// Os índices têm nome (P::...) para o motor achar cada valor sem procurar por texto.

#include "../dsp/Processor.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace whynot::synth
{

using dsp::ParamInfo;
using dsp::ParamKind;

constexpr int numLayers = 4;
constexpr int numEnvs = 3;
constexpr int numLfos = 2;
constexpr int numSlots = 8;
constexpr int maxUnison = 8;

/** Parâmetros de cada camada, na ordem (a camada L começa em L * layerParamCount). */
enum LayerParam
{
    lOn, lType, lLevel, lPan, lOct, lSemi, lFine,
    lVoices, lDetune, lWidth,
    lWave, lShape, lTable, lPhase, lFilter,
    lFmAlgo, lFmRatio2, lFmRatio3, lFmRatio4, lFmAmt2, lFmAmt3, lFmAmt4, lFmFeedback, lFmDecay, lFmSustain,
    layerParamCount
};

enum EnvParam   { eAttack, eDecay, eSustain, eRelease, eCurve, envParamCount };
enum LfoParam   { lfoShape, lfoSync, lfoRate, lfoDiv, lfoRetrig, lfoFade, lfoParamCount };
enum SlotParam  { mSrc, mDst, mAmt, slotParamCount };

enum class LayerType { analog, wavetable, fm, noise };
enum class Wave      { saw, pulse, triangle, sine };
enum class SubWave   { sine, triangle, square };
enum class FilterMode { lowpass, highpass, bandpass, notch };
enum class LfoShape  { sine, triangle, rampUp, rampDown, square, stepRandom, smoothRandom };

/** Fontes da matriz de modulação (o valor da escolha é a posição aqui). */
enum class Source { none, env2, env3, lfo1, lfo2, velocity, note, modWheel, aftertouch, random, env1, count };

/** Destinos da matriz de modulação. */
enum class Dest
{
    none, pitch,
    pitch1, pitch2, pitch3, pitch4,
    level1, level2, level3, level4,
    shape1, shape2, shape3, shape4,
    cutoff, resonance, drive, volume, pan, detune, lfo1Rate, lfo2Rate, subLevel,
    count
};

/** Algoritmos de FM (operadores A, B, C, D; A sempre soa). */
enum class FmAlgo { twoOp, threeSerial, threeParallel, fourSerial, fourParallel, twoPairs, count };

namespace P
{
    constexpr int layer (int l, int p) noexcept   { return l * layerParamCount + p; }
    constexpr int layersEnd = numLayers * layerParamCount;

    enum : int
    {
        subOn = layersEnd, subLevel, subOct, subWave,
        fltOn, fltMode, fltSlope, fltCutoff, fltReso, fltDrive, fltKey, fltEnv, fltVel,
        envBase
    };

    constexpr int env (int e, int p) noexcept   { return envBase + e * envParamCount + p; }
    constexpr int lfoBase = envBase + numEnvs * envParamCount;
    constexpr int lfo (int l, int p) noexcept   { return lfoBase + l * lfoParamCount + p; }
    constexpr int slotBase = lfoBase + numLfos * lfoParamCount;
    constexpr int slot (int s, int p) noexcept  { return slotBase + s * slotParamCount + p; }
    constexpr int globalBase = slotBase + numSlots * slotParamCount;

    enum : int
    {
        volume = globalBase, poly, legato, porta, velAmp, bend, drift,
        satOn, satMode, satDrive, satMix,
        eqOn, eqHp, eqLow, eqMid, eqMidFreq, eqHigh,
        width,
        dlyOn, dlyDiv, dlyFb, dlyMix,
        revOn, revSize, revDecay, revMix,
        mono, glide, bpm,      // controlados pelo app (canal e projeto): hidden
        count
    };
}

constexpr int numParams = P::count;

/** Todos os parâmetros, na ordem dos índices. */
std::span<const ParamInfo> allParams();

const ParamInfo& param (int index);
int indexOf (std::string_view id) noexcept;

/** Valores padrão (o timbre "Init"). */
std::vector<float> defaultValues();

/** Seção do parâmetro, para agrupar na interface e na IA ("Camada 1", "Filtro", "Efeitos"...). */
const char* sectionOf (int index);

/** Quanto o botão "Variar" pode mexer neste parâmetro (fração da faixa; 0 = nunca). */
float variationRange (int index) noexcept;

/** Lista de escolha com rótulos numéricos ("1", "2"...): a IA pode mandar o número em vez da posição. */
bool hasNumericChoices (int index) noexcept;

/** Rótulos curtos das listas, para a interface e a IA. */
std::span<const char* const> sourceNames();
std::span<const char* const> destNames();

/** Tempo musical das divisões de LFO, em batidas (semínimas) por ciclo. */
double lfoDivisionBeats (int division) noexcept;

/** Gerador pseudoaleatório pequeno e determinístico (xorshift32), igual em todo lugar. */
struct Random
{
    std::uint32_t state = 0x9e3779b9u;

    explicit Random (std::uint32_t seed = 0x9e3779b9u) noexcept : state (seed != 0 ? seed : 0x9e3779b9u) {}

    std::uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** [0, 1) */
    float uniform() noexcept   { return (float) (next() >> 8) * (1.0f / 16777216.0f); }

    /** [-1, 1) */
    float bipolar() noexcept   { return uniform() * 2.0f - 1.0f; }
};

/** "Variar": muda o timbre dentro de limites musicais. amount 0..1 (0,3 = sutil, 1 = bem diferente).
    Não mexe na estrutura (tipos de camada, ondas, tabelas, roteamento, oitavas, mono). */
std::vector<float> varied (const std::vector<float>& values, float amount, std::uint32_t seed);

} // namespace whynot::synth
