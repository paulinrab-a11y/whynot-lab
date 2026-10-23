#include "FxCatalog.h"

#include "Color.h"
#include "Console.h"
#include "Denoise.h"
#include "Dynamics.h"
#include "Enhance.h"
#include "EqProcessor.h"
#include "Gate.h"
#include "Master.h"
#include "Multiband.h"
#include "Space.h"
#include "Tune.h"
#include "Vintage.h"
#include "Vocal.h"
#include "VoicePitch.h"

#include <cctype>
#include <string>

namespace whynot::dsp
{

//==============================================================================
// Efeitos

std::span<const FxInfo* const> allEffects()
{
    // Por categoria (o menu do mixer abre uma seção a cada categoria nova) e, dentro dela, na ordem
    // de uma cadeia típica: limpar, controlar, colorir, espacializar, finalizar.
    static const FxInfo* const effects[] = {
        &TuneProcessor::staticInfo(),
        &VoicePitchProcessor::staticInfo(),
        &HarmonyProcessor::staticInfo(),
        &DenoiseProcessor::staticInfo(),
        &LevelerProcessor::staticInfo(),
        &ConsoleProcessor::staticInfo(),
        &DoublerProcessor::staticInfo(),
        &EqProcessor::staticInfo(),
        &VintageEqProcessor::staticInfo(),
        &GateProcessor::staticInfo(),
        &CompressorProcessor::staticInfo(),
        &OptoProcessor::staticInfo(),
        &FetProcessor::staticInfo(),
        &MultibandProcessor::staticInfo(),
        &DeEsserProcessor::staticInfo(),
        &TransientProcessor::staticInfo(),
        &SaturatorProcessor::staticInfo(),
        &BassEnhancerProcessor::staticInfo(),
        &FilterProcessor::staticInfo(),
        &ReverbProcessor::staticInfo(),
        &DelayProcessor::staticInfo(),
        &StereoProcessor::staticInfo(),
        &LimiterProcessor::staticInfo(),
        &MeterProcessor::staticInfo(),
    };

    return effects;
}

const FxInfo* findEffect (std::string_view type)
{
    for (auto* e : allEffects())
        if (type == e->type)
            return e;

    return nullptr;
}

std::unique_ptr<Processor> createProcessor (std::string_view type)
{
    if (type == "tune")      return std::make_unique<TuneProcessor>();
    if (type == "eq")        return std::make_unique<EqProcessor>();
    if (type == "comp")      return std::make_unique<CompressorProcessor>();
    if (type == "deesser")   return std::make_unique<DeEsserProcessor>();
    if (type == "transient") return std::make_unique<TransientProcessor>();
    if (type == "sat")       return std::make_unique<SaturatorProcessor>();
    if (type == "filter")    return std::make_unique<FilterProcessor>();
    if (type == "reverb")    return std::make_unique<ReverbProcessor>();
    if (type == "delay")     return std::make_unique<DelayProcessor>();
    if (type == "stereo")    return std::make_unique<StereoProcessor>();
    if (type == "limiter")   return std::make_unique<LimiterProcessor>();
    if (type == "meter")     return std::make_unique<MeterProcessor>();
    if (type == "gate")      return std::make_unique<GateProcessor>();
    if (type == "opto")      return std::make_unique<OptoProcessor>();
    if (type == "fet")       return std::make_unique<FetProcessor>();
    if (type == "multiband") return std::make_unique<MultibandProcessor>();
    if (type == "leveler")   return std::make_unique<LevelerProcessor>();
    if (type == "doubler")   return std::make_unique<DoublerProcessor>();
    if (type == "bassplus")  return std::make_unique<BassEnhancerProcessor>();
    if (type == "denoise")   return std::make_unique<DenoiseProcessor>();
    if (type == "console")   return std::make_unique<ConsoleProcessor>();
    if (type == "vintageEq") return std::make_unique<VintageEqProcessor>();
    if (type == "voicepitch") return std::make_unique<VoicePitchProcessor>();
    if (type == "harmony")   return std::make_unique<HarmonyProcessor>();
    return nullptr;
}

//==============================================================================
// Presets. Ponto de partida pensado para cada papel e gênero; o ouvido decide o resto.

#define WHYNOT_VALUES(...) \
    [] { static const PresetValue v[] = { __VA_ARGS__ }; return std::span<const PresetValue> (v); }()

std::span<const Preset> allPresets()
{
    static const Preset presets[] = {
        // EQ ------------------------------------------------------------------
        { "eq", "kick-trap", "Kick trap", "Peso em 60 Hz, tira o encaixotado e realça o clique.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 30 }, { "b1Freq", 60 }, { "b1Gain", 2 }, { "b1Q", 1.2f },
                         { "b2Freq", 320 }, { "b2Gain", -3 }, { "b2Q", 1.4f }, { "b3Freq", 4500 }, { "b3Gain", 2.5f }) },
        { "eq", "kick-funk", "Kick funk", "Médio-grave para o tamborzão e ataque em 2,5 kHz.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 35 }, { "b1Freq", 80 }, { "b1Gain", 2 }, { "b1Q", 1.2f },
                         { "b2Freq", 450 }, { "b2Gain", -3 }, { "b2Q", 1.3f }, { "b3Freq", 2500 }, { "b3Gain", 3 }, { "b3Q", 0.9f }) },
        { "eq", "kick-boombap", "Kick rap", "Grave redondo e clique médio, estilo boom bap.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 30 }, { "b1Freq", 70 }, { "b1Gain", 2.5f },
                         { "b2Freq", 350 }, { "b2Gain", -3 }, { "b2Q", 1.4f }, { "b3Freq", 3000 }, { "b3Gain", 2 }) },
        { "eq", "kick-pop", "Kick pop", "Firme e controlado, com clique discreto.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 30 }, { "b1Freq", 55 }, { "b1Gain", 1.5f }, { "b1Q", 1.2f },
                         { "b2Freq", 300 }, { "b2Gain", -2.5f }, { "b2Q", 1.4f }, { "b3Freq", 5000 }, { "b3Gain", 2 }) },
        { "eq", "808-trap", "808 trap", "Sub firme, menos embolado e presença em 900 Hz para aparecer no celular.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 25 }, { "b1Freq", 55 }, { "b1Gain", 1.5f },
                         { "b2Freq", 250 }, { "b2Gain", -2 }, { "b2Q", 1.5f }, { "b3Freq", 900 }, { "b3Gain", 1.5f }) },
        { "eq", "bass-funk", "Grave funk", "Grave com ataque médio, sem lama.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 30 }, { "lowFreq", 80 }, { "lowGain", 1 },
                         { "b1Freq", 200 }, { "b1Gain", -2 }, { "b1Q", 1.4f }, { "b2Freq", 1200 }, { "b2Gain", 2 }) },
        { "eq", "bass-warm", "Baixo quente", "Grave cheio e limpo, com um pouco de definição.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 30 }, { "b1Freq", 80 }, { "b1Gain", 1.5f },
                         { "b2Freq", 250 }, { "b2Gain", -2 }, { "b2Q", 1.4f }, { "b3Freq", 1500 }, { "b3Gain", 1 }) },
        { "eq", "snare-clap", "Caixa / clap", "Corpo em 200 Hz, menos fanho e brilho em cima.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 120 }, { "b1Freq", 200 }, { "b1Gain", 1.5f }, { "b1Q", 1.2f },
                         { "b2Freq", 800 }, { "b2Gain", -2 }, { "b2Q", 1.4f }, { "highFreq", 8000 }, { "highGain", 2.5f }) },
        { "eq", "hats", "Chimbal", "Sem grave nenhum e com brilho suave.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 400 }, { "b2Freq", 2500 }, { "b2Gain", -1.5f },
                         { "highFreq", 12000 }, { "highGain", 1.5f }) },
        { "eq", "melody", "Melodia (abre espaço)", "Corta o grave e um pouco dos médios onde a voz mora.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 150 }, { "b1Freq", 300 }, { "b1Gain", -2 }, { "b1Q", 1.2f },
                         { "b2Freq", 2500 }, { "b2Gain", -1 }, { "highFreq", 10000 }, { "highGain", 1 }) },
        { "eq", "sample-lofi", "Sample lo-fi", "Agudos cortados e grave limpo, para sample de rap.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 120 }, { "b1Freq", 400 }, { "b1Gain", -1.5f },
                         { "lpOn", 1 }, { "lpFreq", 9000 }) },
        { "eq", "vocal-trap", "Voz trap", "Limpa o grave, presença em 3 kHz e muito ar.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 100 }, { "b1Freq", 250 }, { "b1Gain", -3 }, { "b1Q", 1.2f },
                         { "b2Freq", 3000 }, { "b2Gain", 2 }, { "highFreq", 11000 }, { "highGain", 4 }) },
        { "eq", "vocal-funk", "Voz funk", "Voz na frente, com médio agressivo em 2 kHz.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 110 }, { "b1Freq", 300 }, { "b1Gain", -2.5f }, { "b1Q", 1.2f },
                         { "b2Freq", 2000 }, { "b2Gain", 3 }, { "b2Q", 0.9f }, { "highFreq", 10000 }, { "highGain", 2 }) },
        { "eq", "vocal-rap", "Voz rap", "Inteligibilidade: cada palavra clara.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 90 }, { "b1Freq", 200 }, { "b1Gain", -2 }, { "b1Q", 1.2f },
                         { "b2Freq", 3500 }, { "b2Gain", 2.5f }, { "highFreq", 12000 }, { "highGain", 2 }) },
        { "eq", "vocal-pop", "Voz pop", "Suave nos médios e bastante ar em cima.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 90 }, { "b1Freq", 250 }, { "b1Gain", -2.5f }, { "b1Q", 1.2f },
                         { "b2Freq", 5000 }, { "b2Gain", 1.5f }, { "highFreq", 12000 }, { "highGain", 4.5f }) },
        { "eq", "backing", "Vozes de apoio", "Mais finas que a voz principal, para ficarem atrás dela.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 200 }, { "b1Freq", 400 }, { "b1Gain", -2 },
                         { "highFreq", 10000 }, { "highGain", 2 }) },
        { "eq", "fx-clean", "Efeitos", "Tira o grave de risers, impactos e ruídos.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 200 }, { "b1Freq", 500 }, { "b1Gain", -1.5f }) },
        { "eq", "master-clean", "Master limpo", "Só tira o sub-grave inútil e abre um pouco o topo.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 20 }, { "hpSlope", 0 }, { "highFreq", 12000 }, { "highGain", 1 }) },
        { "eq", "master-funk", "Master funk", "Médio para frente, como pede o funk.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 28 }, { "b2Freq", 2500 }, { "b2Gain", 1 }, { "b2Q", 0.8f },
                         { "highFreq", 12000 }, { "highGain", 1 }) },
        { "eq", "master-pop", "Master pop (ar)", "Polimento: um pouco menos de lama e mais ar.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 20 }, { "hpSlope", 0 }, { "b1Freq", 300 }, { "b1Gain", -0.5f }, { "b1Q", 0.9f },
                         { "highFreq", 14000 }, { "highGain", 1.5f }) },
        { "eq", "reverb-return", "Retorno de reverb", "Reverb sem grave e sem chiado: não embola a mix.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 300 }, { "b1Freq", 500 }, { "b1Gain", -2 },
                         { "lpOn", 1 }, { "lpFreq", 10000 }) },
        { "eq", "delay-return", "Retorno de delay", "Eco mais escuro e magro, atrás da voz.",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 250 }, { "lpOn", 1 }, { "lpFreq", 6000 }) },
        { "eq", "beat-space", "Beat: espaço para a voz", "Beat pronto de 2 pistas: um vale largo e leve onde a voz mora (2 a 4 kHz).",
          WHYNOT_VALUES ({ "hpOn", 1 }, { "hpFreq", 25 }, { "b1Freq", 350 }, { "b1Gain", -1 }, { "b1Q", 0.8f },
                         { "b2Freq", 3000 }, { "b2Gain", -2 }, { "b2Q", 0.7f }) },

        // Compressor ----------------------------------------------------------
        { "comp", "vocal-aggressive", "Voz agressiva", "Voz sempre na cara: trap e funk.",
          WHYNOT_VALUES ({ "threshold", -24 }, { "ratio", 6 }, { "attack", 3 }, { "release", 60 }, { "knee", 4 }, { "makeup", 6 }) },
        { "comp", "vocal-control", "Voz controle", "Primeiro estágio: segura os picos sem achatar.",
          WHYNOT_VALUES ({ "threshold", -18 }, { "ratio", 3 }, { "attack", 8 }, { "release", 100 }, { "makeup", 3 }) },
        { "comp", "vocal-smooth", "Voz suave", "Segundo estágio: nivela o volume das frases.",
          WHYNOT_VALUES ({ "threshold", -14 }, { "ratio", 2 }, { "attack", 20 }, { "release", 200 }, { "knee", 9 },
                         { "makeup", 2 }, { "detector", 1 }) },
        { "comp", "kick-punch", "Kick punch", "Ataque lento deixa o estalo passar.",
          WHYNOT_VALUES ({ "threshold", -14 }, { "ratio", 4 }, { "attack", 15 }, { "release", 80 }, { "knee", 4 }, { "makeup", 2 }) },
        { "comp", "snare-punch", "Caixa punch", "Caixa ou clap mais firme.",
          WHYNOT_VALUES ({ "threshold", -16 }, { "ratio", 4 }, { "attack", 8 }, { "release", 100 }, { "knee", 4 }, { "makeup", 3 }) },
        { "comp", "bass-control", "808 / baixo controle", "Volume do grave por igual em todas as notas.",
          WHYNOT_VALUES ({ "threshold", -14 }, { "ratio", 3 }, { "attack", 20 }, { "release", 150 }, { "makeup", 2 }, { "detector", 1 }) },
        { "comp", "sidechain-duck", "Sidechain (kick abaixa o 808)", "Ligue o sidechain no kick: o 808 abre espaço a cada batida.",
          WHYNOT_VALUES ({ "threshold", -30 }, { "ratio", 10 }, { "attack", 0.5f }, { "release", 90 }, { "knee", 2 }) },
        { "comp", "sidechain-soft", "Sidechain leve", "Ducking discreto do grave pelo kick (pop).",
          WHYNOT_VALUES ({ "threshold", -26 }, { "ratio", 3 }, { "attack", 1 }, { "release", 120 }, { "knee", 4 }) },
        { "comp", "drum-glue", "Cola de bateria", "Junta kick, caixa e chimbal como um kit só.",
          WHYNOT_VALUES ({ "threshold", -12 }, { "ratio", 2 }, { "attack", 30 }, { "release", 150 }, { "makeup", 1.5f },
                         { "detector", 1 }, { "scHpf", 90 }) },
        { "comp", "drum-parallel", "Paralelo NY", "Compressão pesada misturada com o original: punch sem perder o ataque.",
          WHYNOT_VALUES ({ "threshold", -30 }, { "ratio", 10 }, { "attack", 5 }, { "release", 80 }, { "knee", 2 },
                         { "makeup", 10 }, { "mix", 35 }) },
        { "comp", "master-glue", "Cola de master", "1 a 2 dB de redução que deixam a mix inteira coesa.",
          WHYNOT_VALUES ({ "threshold", -10 }, { "ratio", 2 }, { "attack", 30 }, { "release", 200 }, { "knee", 8 },
                         { "makeup", 1 }, { "detector", 1 }, { "scHpf", 100 }) },
        { "comp", "funk-smash", "Esmagar (funk)", "Paralelo bem pesado, som estourado.",
          WHYNOT_VALUES ({ "threshold", -30 }, { "ratio", 12 }, { "attack", 1 }, { "release", 50 }, { "knee", 2 },
                         { "makeup", 12 }, { "mix", 60 }) },
        { "comp", "melody-glue", "Melodia cola", "Segura sample e synth sem tirar a vida.",
          WHYNOT_VALUES ({ "threshold", -16 }, { "ratio", 2 }, { "attack", 25 }, { "release", 180 }, { "knee", 8 },
                         { "makeup", 1 }, { "detector", 1 }) },

        // Saturador -----------------------------------------------------------
        { "sat", "808-trap", "808 trap (aparece no celular)", "Harmônicos acima de 100 Hz, sub limpo.",
          WHYNOT_VALUES ({ "mode", 0 }, { "drive", 12 }, { "tone", 15 }, { "protect", 100 }) },
        { "sat", "808-hard", "808 distorcido", "Clip duro com o sub protegido: agressivo sem embolar.",
          WHYNOT_VALUES ({ "mode", 3 }, { "drive", 18 }, { "tone", 20 }, { "protect", 80 }) },
        { "sat", "bass-warm", "Baixo quente", "Válvula leve, sub protegido.",
          WHYNOT_VALUES ({ "mode", 1 }, { "drive", 6 }, { "protect", 80 }) },
        { "sat", "kick-weight", "Peso no kick", "Clip suave: kick mais cheio e mais alto.",
          WHYNOT_VALUES ({ "mode", 2 }, { "drive", 8 }, { "tone", 10 }) },
        { "sat", "kick-funk", "Kick funk estourado", "Clip duro, bem na cara.",
          WHYNOT_VALUES ({ "mode", 3 }, { "drive", 12 }, { "tone", 15 }) },
        { "sat", "vocal-warm", "Voz calor", "Fita em paralelo: presença sem soar distorcido.",
          WHYNOT_VALUES ({ "mode", 0 }, { "drive", 5 }, { "tone", 5 }, { "mix", 40 }) },
        { "sat", "vocal-funk", "Voz funk", "Válvula em paralelo: voz grossa e agressiva.",
          WHYNOT_VALUES ({ "mode", 1 }, { "drive", 10 }, { "tone", 10 }, { "mix", 50 }) },
        { "sat", "drum-glue", "Bateria fita", "Fita leve em paralelo.",
          WHYNOT_VALUES ({ "mode", 0 }, { "drive", 5 }, { "mix", 60 }) },
        { "sat", "master-tape", "Master fita", "Quase nada: só cola e arredonda os picos.",
          WHYNOT_VALUES ({ "mode", 0 }, { "drive", 2 }) },
        { "sat", "master-funk", "Master funk", "Clip suave no master, mais pressão.",
          WHYNOT_VALUES ({ "mode", 2 }, { "drive", 4 }) },
        { "sat", "lofi-sample", "Sample lo-fi", "Válvula escura em paralelo.",
          WHYNOT_VALUES ({ "mode", 1 }, { "drive", 10 }, { "tone", -30 }, { "mix", 70 }) },
        { "sat", "funk-blown", "Estourado (funk)", "Fuzz em paralelo para caixa e percussão.",
          WHYNOT_VALUES ({ "mode", 4 }, { "drive", 14 }, { "tone", 10 }, { "protect", 60 }, { "mix", 60 }) },

        // Transiente ----------------------------------------------------------
        { "transient", "kick-punch", "Kick punch", "Mais ataque, cauda um pouco menor.",
          WHYNOT_VALUES ({ "attack", 40 }, { "sustain", -15 }) },
        { "transient", "snare-snap", "Caixa estalo", "Estalo e um pouco mais de corpo.",
          WHYNOT_VALUES ({ "attack", 35 }, { "sustain", 10 }) },
        { "transient", "hats-short", "Chimbal curto", "Chimbal seco e rítmico.",
          WHYNOT_VALUES ({ "attack", 10 }, { "sustain", -35 }) },
        { "transient", "drums-punch", "Bateria punch", "Realce geral de ataque.",
          WHYNOT_VALUES ({ "attack", 20 }) },
        { "transient", "funk-perc", "Percussão funk", "Ataque bem duro e seco.",
          WHYNOT_VALUES ({ "attack", 50 }, { "sustain", -20 }) },
        { "transient", "soften", "Amaciar", "Tira a agressividade do ataque.",
          WHYNOT_VALUES ({ "attack", -40 }) },

        // De-esser ------------------------------------------------------------
        { "deesser", "male", "Voz grave", "Sibilância por volta de 5,5 kHz.",
          WHYNOT_VALUES ({ "freq", 5500 }, { "threshold", -28 }, { "range", 6 }) },
        { "deesser", "female", "Voz aguda", "Sibilância por volta de 7,5 kHz.",
          WHYNOT_VALUES ({ "freq", 7500 }, { "threshold", -28 }, { "range", 6 }) },
        { "deesser", "aggressive", "Forte", "Para vozes muito comprimidas e brilhantes.",
          WHYNOT_VALUES ({ "freq", 6500 }, { "threshold", -34 }, { "range", 10 }) },

        // Reverb --------------------------------------------------------------
        { "reverb", "room-short", "Sala curta", "Ambiência natural e discreta (bus).",
          WHYNOT_VALUES ({ "mode", 0 }, { "size", 35 }, { "decay", 0.8f }, { "predelay", 10 }, { "damping", 7000 },
                         { "lowCut", 250 }, { "width", 80 }, { "mix", 100 }) },
        { "reverb", "plate-vocal", "Placa de voz", "O reverb clássico de voz de trap e pop (bus).",
          WHYNOT_VALUES ({ "mode", 1 }, { "size", 55 }, { "decay", 1.8f }, { "predelay", 30 }, { "damping", 9000 },
                         { "lowCut", 300 }, { "mix", 100 }) },
        { "reverb", "hall-pop", "Salão pop", "Grande e suave, atrás de tudo (bus).",
          WHYNOT_VALUES ({ "mode", 2 }, { "size", 75 }, { "decay", 2.8f }, { "predelay", 40 }, { "damping", 6500 },
                         { "lowCut", 250 }, { "mix", 100 }) },
        { "reverb", "funk-ambience", "Ambiente funk", "Quase seco: só tira a sensação de estúdio (bus).",
          WHYNOT_VALUES ({ "mode", 0 }, { "size", 25 }, { "decay", 0.5f }, { "predelay", 0 }, { "damping", 8000 },
                         { "lowCut", 400 }, { "width", 70 }, { "mix", 100 }) },
        { "reverb", "clap-plate", "Palma com placa", "Direto no clap ou na caixa (insert).",
          WHYNOT_VALUES ({ "mode", 1 }, { "size", 45 }, { "decay", 1.2f }, { "predelay", 5 }, { "damping", 9000 },
                         { "lowCut", 400 }, { "mix", 18 }) },
        { "reverb", "insert-room", "Sala (insert)", "Um toque de sala direto na trilha.",
          WHYNOT_VALUES ({ "mode", 0 }, { "size", 40 }, { "decay", 1.0f }, { "predelay", 10 }, { "mix", 15 }) },
        { "reverb", "huge", "Cauda enorme", "Para transições e finais (bus).",
          WHYNOT_VALUES ({ "mode", 2 }, { "size", 100 }, { "decay", 7 }, { "predelay", 60 }, { "damping", 5000 },
                         { "lowCut", 300 }, { "mix", 100 }) },

        // Delay ---------------------------------------------------------------
        { "delay", "quarter-vocal", "1/4 na voz", "Eco de semínima, pingue-pongue, abaixa enquanto a voz canta (bus).",
          WHYNOT_VALUES ({ "sync", 1 }, { "division", 8 }, { "feedback", 30 }, { "mode", 1 }, { "hpf", 300 }, { "lpf", 5000 },
                         { "ducking", 60 }, { "mix", 100 }) },
        { "delay", "dotted-eighth", "1/8 pontuado", "O eco do pop (bus).",
          WHYNOT_VALUES ({ "sync", 1 }, { "division", 6 }, { "feedback", 25 }, { "mode", 1 }, { "hpf", 300 }, { "lpf", 6000 },
                         { "ducking", 50 }, { "mix", 100 }) },
        { "delay", "slap", "Slap (rap)", "Uma repetição curta que engrossa a voz (bus).",
          WHYNOT_VALUES ({ "sync", 0 }, { "time", 95 }, { "feedback", 0 }, { "mode", 2 }, { "hpf", 200 }, { "lpf", 4000 },
                         { "ducking", 30 }, { "mix", 100 }) },
        { "delay", "funk-echo", "Eco funk", "Colcheia escura no fim das frases (bus).",
          WHYNOT_VALUES ({ "sync", 1 }, { "division", 5 }, { "feedback", 20 }, { "mode", 1 }, { "hpf", 400 }, { "lpf", 4000 },
                         { "ducking", 70 }, { "mix", 100 }) },
        { "delay", "throw", "Repetição longa (throw)", "Para a última palavra da frase ecoar (bus).",
          WHYNOT_VALUES ({ "sync", 1 }, { "division", 8 }, { "feedback", 55 }, { "mode", 1 }, { "hpf", 350 }, { "lpf", 4500 },
                         { "ducking", 40 }, { "mix", 100 }) },
        { "delay", "insert-eighth", "1/8 (insert)", "Direto na trilha, discreto.",
          WHYNOT_VALUES ({ "sync", 1 }, { "division", 5 }, { "feedback", 25 }, { "mode", 1 }, { "mix", 20 }) },

        // Estéreo -------------------------------------------------------------
        { "stereo", "bass-mono", "Grave mono (808)", "Grave sempre no centro: forte em qualquer sistema.",
          WHYNOT_VALUES ({ "monoBass", 200 }) },
        { "stereo", "hats-wide", "Chimbal aberto", "Um pouco mais largo que o original.",
          WHYNOT_VALUES ({ "width", 140 }) },
        { "stereo", "melody-wide", "Melodia larga", "Abre para os lados e deixa o centro para a voz.",
          WHYNOT_VALUES ({ "width", 130 }, { "monoBass", 150 }) },
        { "stereo", "backing-wide", "Apoio bem aberto", "Vozes de apoio nos lados.",
          WHYNOT_VALUES ({ "width", 160 }, { "monoBass", 150 }) },
        { "stereo", "master-mono-low", "Master: grave mono", "Grave do master no centro (trap e funk).",
          WHYNOT_VALUES ({ "monoBass", 100 }) },
        { "stereo", "master-pop", "Master pop", "Um pouco mais de abertura e grave no centro.",
          WHYNOT_VALUES ({ "width", 110 }, { "monoBass", 80 }) },

        // Limitador -----------------------------------------------------------
        { "limiter", "master-trap", "Master trap", "Alto e com clipper. Alvo perto de -7 LUFS.",
          WHYNOT_VALUES ({ "gain", 4 }, { "ceiling", -1 }, { "release", 50 }, { "clip", 50 }, { "target", -7 }) },
        { "limiter", "master-funk", "Master funk", "O mais alto: clipper forte. Alvo perto de -5,5 LUFS.",
          WHYNOT_VALUES ({ "gain", 6 }, { "ceiling", -0.8f }, { "release", 40 }, { "clip", 70 }, { "target", -5.5f }) },
        { "limiter", "master-rap", "Master rap", "Alto mas com respiro. Alvo perto de -8 LUFS.",
          WHYNOT_VALUES ({ "gain", 3 }, { "ceiling", -1 }, { "release", 80 }, { "clip", 20 }, { "target", -8 }) },
        { "limiter", "master-pop", "Master pop", "Mais dinâmico e limpo. Alvo perto de -9 LUFS.",
          WHYNOT_VALUES ({ "gain", 2 }, { "ceiling", -1 }, { "release", 120 }, { "clip", 10 }, { "target", -9 }) },
        { "limiter", "streaming", "Streaming (-14)", "Para quem prefere dinâmica: as plataformas normalizam em torno de -14.",
          WHYNOT_VALUES ({ "gain", 0 }, { "ceiling", -1 }, { "release", 150 }, { "clip", 0 }, { "target", -14 }) },

        // Filtro --------------------------------------------------------------
        { "filter", "phone", "Voz de telefone", "Só os médios, com um pouco de sujeira.",
          WHYNOT_VALUES ({ "mode", 2 }, { "cutoff", 1500 }, { "resonance", 25 }, { "drive", 6 }) },
        { "filter", "lofi", "Lo-fi abafado", "Agudos fora, como disco velho.",
          WHYNOT_VALUES ({ "mode", 0 }, { "cutoff", 4000 }, { "resonance", 10 }) },
        { "filter", "intro-muffled", "Intro abafada", "Como se a música tocasse na outra sala.",
          WHYNOT_VALUES ({ "mode", 0 }, { "cutoff", 600 }, { "resonance", 15 }, { "slope", 1 }) },
        { "filter", "radio", "Rádio", "Sem grave, médio na frente.",
          WHYNOT_VALUES ({ "mode", 1 }, { "cutoff", 700 }, { "resonance", 20 }, { "drive", 3 }) },

        // Medidor -------------------------------------------------------------
        { "meter", "streaming", "Streaming (-14)", "Referência das plataformas.", WHYNOT_VALUES ({ "target", -14 }) },
        { "meter", "trap", "Trap (-7)", "Referência de trap.", WHYNOT_VALUES ({ "target", -7 }) },
        { "meter", "funk", "Funk (-5,5)", "Referência de funk.", WHYNOT_VALUES ({ "target", -5.5f }) },
        { "meter", "rap", "Rap (-8)", "Referência de rap.", WHYNOT_VALUES ({ "target", -8 }) },
        { "meter", "pop", "Pop (-9)", "Referência de pop.", WHYNOT_VALUES ({ "target", -9 }) },

        // Afinação (tom e escala vêm do projeto e não mudam com o preset) ---------
        { "tune", "natural", "Natural", "Corrige sem aparecer: a voz continua humana.",
          WHYNOT_VALUES ({ "speed", 90 }, { "amount", 80 }) },
        { "tune", "pop", "Pop", "Afinado e firme, sem soar robô.",
          WHYNOT_VALUES ({ "speed", 40 }, { "amount", 100 }) },
        { "tune", "trap-funk", "Trap / funk (rápido)", "Correção rápida: o som de voz afinada de trap e funk.",
          WHYNOT_VALUES ({ "speed", 12 }, { "amount", 100 }) },
        { "tune", "robot", "Robô", "Salto na hora entre as notas: o efeito de voz robótica.",
          WHYNOT_VALUES ({ "speed", 0 }, { "amount", 100 }) },
        { "tune", "touch", "Só um toque", "Puxa um pouco para a nota, para quem já canta afinado.",
          WHYNOT_VALUES ({ "speed", 180 }, { "amount", 60 }) },
        { "tune", "hard", "Duro (trap)", "Salto na hora e nota reta: o efeito de voz afinada do trap, sem vibrato.",
          WHYNOT_VALUES ({ "speed", 0 }, { "amount", 100 }, { "humanize", 0 }) },
        { "tune", "hard-human", "Duro com vibrato", "Troca de nota na hora, mas as notas longas mantêm o vibrato (trap melódico).",
          WHYNOT_VALUES ({ "speed", 3 }, { "amount", 100 }, { "humanize", 70 }) },
        { "tune", "natural-human", "Natural humanizado", "Corrige a entrada das notas e deixa o resto da voz como está.",
          WHYNOT_VALUES ({ "speed", 40 }, { "amount", 90 }, { "humanize", 80 }) },

        // Tom da voz ----------------------------------------------------------
        { "voicepitch", "deep", "Voz grossa (funk)", "5 semitons abaixo e a voz maior: o ad-lib grosso do funk.",
          WHYNOT_VALUES ({ "semitones", -5 }, { "formant", -3 }) },
        { "voicepitch", "thin", "Voz fina", "5 semitons acima e a voz menor.",
          WHYNOT_VALUES ({ "semitones", 5 }, { "formant", 3 }) },
        { "voicepitch", "octave-down", "Oitava abaixo (dobra)", "Uma oitava abaixo, junto com a voz original: refrão pesado.",
          WHYNOT_VALUES ({ "semitones", -12 }, { "formant", -2 }, { "mix", 50 }) },
        { "voicepitch", "monster", "Ad-lib monstro", "7 semitons abaixo e a voz bem maior.",
          WHYNOT_VALUES ({ "semitones", -7 }, { "formant", -6 }) },
        { "voicepitch", "chipmunk", "Ad-lib agudo", "7 semitons acima, com a voz menor junto.",
          WHYNOT_VALUES ({ "semitones", 7 }, { "formant", 6 }) },
        { "voicepitch", "other-timbre", "Outro timbre", "A mesma nota com a voz um pouco maior: soa como outra pessoa na dobra.",
          WHYNOT_VALUES ({ "formant", -3 }) },

        // Harmonia (tom e escala vêm do projeto e não mudam com o preset) -------
        { "harmony", "third-up", "Terça acima", "Uma voz uma terça acima, na escala: o refrão com coro.",
          WHYNOT_VALUES ({ "voices", 0 }, { "level", 60 }) },
        { "harmony", "fifth-up", "Quinta acima", "Uma voz uma quinta acima: aberta e brilhante.",
          WHYNOT_VALUES ({ "voices", 1 }, { "level", 50 }) },
        { "harmony", "chord", "Terça e quinta (acorde)", "Duas vozes, uma de cada lado: o acorde inteiro em cima da voz.",
          WHYNOT_VALUES ({ "voices", 2 }, { "level", 50 }, { "width", 80 }) },
        { "harmony", "third-down", "Terça abaixo", "Uma voz por baixo, mais escura.",
          WHYNOT_VALUES ({ "voices", 3 }, { "level", 55 }, { "formant", -1 }) },
        { "harmony", "octave-down", "Oitava abaixo (peso)", "A mesma melodia uma oitava abaixo, com a voz maior: ad-lib pesado.",
          WHYNOT_VALUES ({ "voices", 4 }, { "level", 60 }, { "formant", -3 }) },
        { "harmony", "deep-choir", "Coro grosso", "Terça acima e oitava abaixo, com as vozes maiores.",
          WHYNOT_VALUES ({ "voices", 5 }, { "level", 50 }, { "formant", -2 }, { "width", 80 }) },

        // Gate ----------------------------------------------------------------
        { "gate", "vocal-home", "Voz em casa", "Expansor suave: abaixa o quarto entre as frases sem cortar o fim das palavras.",
          WHYNOT_VALUES ({ "mode", 1 }, { "threshold", -50 }, { "range", 15 }, { "ratio", 2 }, { "attack", 1 }, { "hold", 60 },
                         { "release", 200 }, { "scHpf", 80 }) },
        { "gate", "vocal-tight", "Voz seca (rap)", "Fecha de vez entre as frases: rap sem vazamento do fone.",
          WHYNOT_VALUES ({ "mode", 0 }, { "threshold", -42 }, { "range", 25 }, { "attack", 0.5f }, { "hold", 40 }, { "release", 120 },
                         { "scHpf", 100 }) },
        { "gate", "breath", "Abaixar respiração", "Expansor leve: as respirações ficam uns 8 dB mais baixas.",
          WHYNOT_VALUES ({ "mode", 1 }, { "threshold", -38 }, { "range", 8 }, { "ratio", 1.5f }, { "hold", 20 }, { "release", 150 },
                         { "scHpf", 120 }) },
        { "gate", "kick-tight", "Kick curto", "Corta a cauda e o vazamento do kick.",
          WHYNOT_VALUES ({ "mode", 0 }, { "threshold", -30 }, { "range", 40 }, { "attack", 0.1f }, { "hold", 30 }, { "release", 60 }) },
        { "gate", "snare-clean", "Caixa sem vazamento", "Só a caixa passa; o chimbal vazando fica de fora.",
          WHYNOT_VALUES ({ "mode", 0 }, { "threshold", -28 }, { "range", 30 }, { "hold", 40 }, { "release", 90 }, { "scHpf", 150 }) },
        { "gate", "808-chop", "808 picotado (com disparo)", "Ligue o disparo no chimbal ou no kick: o 808 só toca junto com eles.",
          WHYNOT_VALUES ({ "mode", 0 }, { "threshold", -30 }, { "range", 80 }, { "attack", 0.1f }, { "hold", 10 }, { "release", 40 },
                         { "hysteresis", 2 }) },

        // Compressor óptico ----------------------------------------------------
        { "opto", "vocal-smooth", "Voz suave", "Segura a voz sem esmagar: o primeiro compressor de quase toda voz.",
          WHYNOT_VALUES ({ "reduction", 45 }, { "gain", 4 }) },
        { "opto", "vocal-level", "Voz nivelada", "Mais redução e os agudos no detector: a voz sempre do mesmo tamanho.",
          WHYNOT_VALUES ({ "reduction", 60 }, { "gain", 6 }, { "emphasis", 30 }) },
        { "opto", "vocal-limit", "Voz limitada (refrão)", "Modo limitar: o refrão gritado não passa da conta.",
          WHYNOT_VALUES ({ "reduction", 55 }, { "gain", 5 }, { "mode", 1 }) },
        { "opto", "bass-even", "Baixo por igual", "Todas as notas do baixo ou do 808 no mesmo volume, sem perder o ataque.",
          WHYNOT_VALUES ({ "reduction", 50 }, { "gain", 4 }) },
        { "opto", "bus-glue", "Cola suave (bus)", "Pouca redução num grupo de vozes ou de instrumentos.",
          WHYNOT_VALUES ({ "reduction", 30 }, { "gain", 2 }) },

        // Compressor FET -------------------------------------------------------
        { "fet", "rap-front", "Rap na cara", "Rápido e com cor: cada sílaba na frente do beat.",
          WHYNOT_VALUES ({ "threshold", -24 }, { "ratio", 1 }, { "attack", 0.2f }, { "release", 120 }, { "makeup", 7 }, { "color", 40 }) },
        { "fet", "vocal-fast", "Voz controle rápido", "4:1 rápido, para depois do compressor óptico.",
          WHYNOT_VALUES ({ "threshold", -20 }, { "ratio", 0 }, { "attack", 0.4f }, { "release", 250 }, { "makeup", 4 }, { "color", 25 }) },
        { "fet", "parallel-vocal", "Voz paralela", "20:1 bem rápido misturado com o original: corpo sem perder a dinâmica.",
          WHYNOT_VALUES ({ "threshold", -32 }, { "ratio", 3 }, { "attack", 0.05f }, { "release", 150 }, { "makeup", 12 }, { "color", 40 },
                         { "mix", 35 }) },
        { "fet", "smash", "Esmagar (todos)", "Todas as razões ao mesmo tempo, em paralelo: bateria ou voz estourada.",
          WHYNOT_VALUES ({ "threshold", -30 }, { "ratio", 4 }, { "attack", 0.1f }, { "release", 100 }, { "makeup", 10 }, { "color", 50 },
                         { "mix", 50 }) },
        { "fet", "drums-punch", "Bateria com pressão", "Ataque mais lento deixa o estalo; a soltura curta traz o ambiente.",
          WHYNOT_VALUES ({ "threshold", -18 }, { "ratio", 0 }, { "attack", 0.8f }, { "release", 80 }, { "makeup", 4 }, { "color", 35 }) },

        // Multibanda -----------------------------------------------------------
        { "multiband", "vocal-harsh", "Voz: segura o médio duro", "Só os 1,8 a 6 kHz descem quando a voz fica estridente.",
          WHYNOT_VALUES ({ "x2", 1800 }, { "x3", 6000 }, { "b3Thr", -28 }, { "b3Ratio", 3 }, { "attack", 5 }, { "release", 80 }) },
        { "multiband", "vocal-sibilance", "Voz: sibilância e chiado", "A banda de cima segura os \"s\" e o chiado (um de-esser largo).",
          WHYNOT_VALUES ({ "x3", 6500 }, { "b4Thr", -32 }, { "b4Ratio", 4 }, { "attack", 1 }, { "release", 60 }) },
        { "multiband", "vocal-mud", "Voz: grave embolado", "Os 120 a 500 Hz descem quando a voz encosta no microfone.",
          WHYNOT_VALUES ({ "x1", 120 }, { "x2", 500 }, { "b2Thr", -26 }, { "b2Ratio", 2.5f }, { "attack", 10 }, { "release", 120 }) },
        { "multiband", "beat-space", "Beat: abre espaço para a voz", "Ligue o disparo na voz: o beat abaixa só os 1,8 a 5 kHz enquanto ela canta.",
          WHYNOT_VALUES ({ "x2", 1800 }, { "x3", 5000 }, { "b3Thr", -36 }, { "b3Ratio", 3 }, { "attack", 10 }, { "release", 150 }) },
        { "multiband", "808-control", "808: segura o sub", "Só o sub fica por igual; o resto do 808 respira.",
          WHYNOT_VALUES ({ "x1", 90 }, { "b1Thr", -20 }, { "b1Ratio", 3 }, { "attack", 20 }, { "release", 150 }) },
        { "multiband", "master-gentle", "Master: multibanda leve", "1 a 2 dB por banda: o master mais equilibrado, sem som de compressor.",
          WHYNOT_VALUES ({ "b1Thr", -16 }, { "b1Ratio", 2 }, { "b2Thr", -18 }, { "b2Ratio", 1.5f }, { "b3Thr", -18 }, { "b3Ratio", 1.5f },
                         { "b4Thr", -20 }, { "b4Ratio", 2 }, { "attack", 20 }, { "release", 200 }) },

        // Nivelador de voz -----------------------------------------------------
        { "leveler", "vocal-rap", "Rap (rápido)", "Segue as frases de perto: cada palavra no mesmo volume.",
          WHYNOT_VALUES ({ "target", -18 }, { "range", 8 }, { "speed", 250 }) },
        { "leveler", "vocal-sung", "Canto (natural)", "Iguala as frases sem mexer na dinâmica de cada nota.",
          WHYNOT_VALUES ({ "target", -20 }, { "range", 10 }, { "speed", 800 }) },
        { "leveler", "adlibs", "Ad-libs", "Ad-libs e vozes de apoio mais baixas que a principal, mas sempre audíveis.",
          WHYNOT_VALUES ({ "target", -24 }, { "range", 12 }, { "speed", 300 }) },
        { "leveler", "speech", "Fala / intro", "Para fala: mais faixa, ignora o ruído mais baixo.",
          WHYNOT_VALUES ({ "target", -18 }, { "range", 14 }, { "speed", 400 }, { "sensitivity", -55 }) },

        // Dobrador -------------------------------------------------------------
        { "doubler", "double-tight", "Dobra justa", "Uma dobra de cada lado, bem perto da voz: engrossa sem aparecer.",
          WHYNOT_VALUES ({ "delay", 14 }, { "detune", 6 }, { "width", 80 }, { "level", 35 }) },
        { "doubler", "double-wide", "Dobra larga (refrão)", "Quatro cópias abertas: refrão cheio.",
          WHYNOT_VALUES ({ "voices", 1 }, { "delay", 20 }, { "detune", 10 }, { "level", 45 }) },
        { "doubler", "adlib-wide", "Ad-lib aberto", "Ad-lib nos lados, mais desafinado e sem grave.",
          WHYNOT_VALUES ({ "voices", 1 }, { "delay", 25 }, { "detune", 14 }, { "level", 60 }, { "hpf", 200 }) },
        { "doubler", "chorus-soft", "Coro suave", "Ondulação leve e regular, para voz cantada.",
          WHYNOT_VALUES ({ "mode", 1 }, { "delay", 12 }, { "detune", 6 }, { "level", 35 }) },
        { "doubler", "melody-wide", "Melodia larga", "Pad, guitarra ou synth mais abertos, sem mexer no grave.",
          WHYNOT_VALUES ({ "mode", 1 }, { "voices", 1 }, { "delay", 16 }, { "detune", 9 }, { "level", 50 }, { "hpf", 250 }) },

        // Grave+ ---------------------------------------------------------------
        { "bassplus", "808-phone", "808 no celular", "O 808 aparece no fone e no celular, com o sub inteiro.",
          WHYNOT_VALUES ({ "freq", 90 }, { "amount", 55 }, { "color", 35 }) },
        { "bassplus", "808-hard", "808 que corta", "Mais 3º harmônico: o 808 corta a mix.",
          WHYNOT_VALUES ({ "freq", 100 }, { "amount", 70 }, { "color", 70 }) },
        { "bassplus", "kick-body", "Kick com corpo", "O corpo do kick aparece em caixa pequena.",
          WHYNOT_VALUES ({ "freq", 120 }, { "amount", 40 }, { "color", 30 }) },
        { "bassplus", "bass-warm", "Baixo quente", "Só o 2º harmônico: redondo.",
          WHYNOT_VALUES ({ "freq", 110 }, { "amount", 40 }, { "color", 10 }) },
        { "bassplus", "headroom", "Menos sub, mais volume", "Troca parte do sub por harmônicos: o master fica mais alto sem distorcer.",
          WHYNOT_VALUES ({ "freq", 80 }, { "amount", 60 }, { "color", 40 }, { "sub", 60 }) },

        // Limpa-voz ------------------------------------------------------------
        { "denoise", "home-light", "Quarto (leve)", "Só o chiado do quarto, sem mexer na voz.",
          WHYNOT_VALUES ({ "reduction", 8 }, { "threshold", 5 }, { "smoothing", 60 }) },
        { "denoise", "fan", "Ventilador / ar", "Ruído constante de ventilador ou ar-condicionado.",
          WHYNOT_VALUES ({ "reduction", 14 }, { "threshold", 6 }, { "smoothing", 60 }) },
        { "denoise", "hiss", "Chiado da placa", "Chiado agudo de placa barata ou ganho alto.",
          WHYNOT_VALUES ({ "reduction", 12 }, { "threshold", 4 }, { "smoothing", 40 }) },
        { "denoise", "street", "Rua (forte)", "Mais redução para ruído alto; confira se a voz não ficou artificial.",
          WHYNOT_VALUES ({ "reduction", 22 }, { "threshold", 8 }, { "smoothing", 70 }) },

        // Console --------------------------------------------------------------
        { "console", "vocal-rap", "Voz rap", "Corte, gate, compressor rápido, presença e um pouco de cor.",
          WHYNOT_VALUES ({ "hpf", 100 }, { "gate", -55 }, { "comp", -20 }, { "ratio", 4 }, { "fast", 1 }, { "low", -1 },
                         { "mid", 3 }, { "midFreq", 3200 }, { "high", 3 }, { "drive", 20 }) },
        { "console", "vocal-trap", "Voz trap", "Mais compressão e mais ar no topo.",
          WHYNOT_VALUES ({ "hpf", 110 }, { "gate", -55 }, { "comp", -22 }, { "ratio", 5 }, { "fast", 1 }, { "mid", 2 },
                         { "midFreq", 3500 }, { "high", 5 }, { "drive", 25 }) },
        { "console", "vocal-funk", "Voz funk", "Médio na frente e bastante cor.",
          WHYNOT_VALUES ({ "hpf", 120 }, { "comp", -24 }, { "ratio", 6 }, { "fast", 1 }, { "mid", 4 }, { "midFreq", 2000 },
                         { "high", 2 }, { "drive", 35 }) },
        { "console", "vocal-sung", "Voz cantada", "Compressão lenta, menos embolado e ar.",
          WHYNOT_VALUES ({ "hpf", 90 }, { "comp", -18 }, { "ratio", 3 }, { "mid", -2 }, { "midFreq", 300 }, { "high", 4 }, { "drive", 10 }) },
        { "console", "drums", "Bateria", "Compressor lento (o estalo passa), peso e cor.",
          WHYNOT_VALUES ({ "hpf", 30 }, { "comp", -16 }, { "ratio", 4 }, { "low", 2 }, { "mid", -2 }, { "midFreq", 400 },
                         { "high", 2 }, { "drive", 30 }) },
        { "console", "beat", "Beat (2 pistas)", "Beat pronto: compressão leve e um pouco menos de 3 kHz para a voz.",
          WHYNOT_VALUES ({ "hpf", 25 }, { "comp", -12 }, { "ratio", 2 }, { "mid", -1.5f }, { "midFreq", 3000 }, { "drive", 15 }) },

        // EQ vintage -----------------------------------------------------------
        { "vintageEq", "kick-trick", "Kick: peso sem embolar", "Reforço e atenuação juntos em 60 Hz: mais peso e menos lama.",
          WHYNOT_VALUES ({ "lowFreq", 2 }, { "lowBoost", 6 }, { "lowAtten", 4 }) },
        { "vintageEq", "808-trick", "808: o truque do grave", "O mesmo truque em 30 Hz, para o sub.",
          WHYNOT_VALUES ({ "lowFreq", 1 }, { "lowBoost", 5 }, { "lowAtten", 4 }) },
        { "vintageEq", "vocal-air", "Voz: ar no topo", "Sino largo em 16 kHz: brilho suave, sem chiado.",
          WHYNOT_VALUES ({ "highFreq", 6 }, { "highBoost", 4 }, { "highWidth", 8 }) },
        { "vintageEq", "vocal-presence", "Voz: presença", "5 kHz para frente e um pouco menos de grave.",
          WHYNOT_VALUES ({ "lowFreq", 3 }, { "lowAtten", 2 }, { "highFreq", 2 }, { "highBoost", 3 }) },
        { "vintageEq", "master-sheen", "Master: brilho e peso", "Bem pouco de tudo, como no master de estúdio.",
          WHYNOT_VALUES ({ "lowFreq", 1 }, { "lowBoost", 2 }, { "lowAtten", 1.5f }, { "highFreq", 5 }, { "highBoost", 2 }, { "highWidth", 9 }) },
        { "vintageEq", "beat-dark", "Beat escuro (lo-fi)", "Agudo atenuado a partir de 5 kHz.",
          WHYNOT_VALUES ({ "airFreq", 0 }, { "highAtten", 5 }) },
    };

    return presets;
}

#undef WHYNOT_VALUES

const Preset* findPreset (std::string_view type, std::string_view id)
{
    for (auto& p : allPresets())
        if (type == p.type && id == p.id)
            return &p;

    return nullptr;
}

std::vector<const Preset*> presetsFor (std::string_view type)
{
    std::vector<const Preset*> out;

    for (auto& p : allPresets())
        if (type == p.type)
            out.push_back (&p);

    return out;
}

std::vector<float> presetValues (const FxInfo& info, const Preset* preset)
{
    std::vector<float> v;

    for (auto& p : info.params)
        v.push_back (p.def);

    if (preset != nullptr)
        for (auto& pv : preset->values)
            if (const int i = info.indexOf (pv.param); i >= 0)
                v[(size_t) i] = info.params[(size_t) i].clamp (pv.value);

    return v;
}

//==============================================================================
// Papéis

std::span<const RoleInfo> allRoles()
{
    static const RoleInfo roles[] = {
        { "kick",      "Kick / bumbo" },
        { "bass",      "808 / baixo" },
        { "snare",     "Caixa / clap" },
        { "hats",      "Chimbal / percussão" },
        { "melody",    "Melodia / sample" },
        { "beat",      "Beat pronto (2 pistas)" },
        { "vocal",     "Voz principal" },
        { "backing",   "Vozes de apoio / ad-libs" },
        { "fx",        "Efeitos" },
        { "reverbBus", "Envio de reverb" },
        { "delayBus",  "Envio de delay" },
        { "bus",       "Bus / grupo (sem receita)" },
    };

    return roles;
}

const RoleInfo* findRole (std::string_view id)
{
    for (auto& r : allRoles())
        if (id == r.id)
            return &r;

    return nullptr;
}

const char* guessRole (std::string_view trackName)
{
    std::string name (trackName);

    for (auto& ch : name)
        ch = (char) std::tolower ((unsigned char) ch);

    // Palavra inteira (para siglas curtas como "bd" e "hh").
    auto hasWord = [&name] (std::string_view w)
    {
        for (size_t pos = name.find (w); pos != std::string::npos; pos = name.find (w, pos + 1))
        {
            const bool startOk = pos == 0 || ! std::isalnum ((unsigned char) name[pos - 1]);
            const bool endOk = pos + w.size() == name.size() || ! std::isalnum ((unsigned char) name[pos + w.size()]);

            if (startOk && endOk)
                return true;
        }

        return false;
    };

    auto hasAny = [&name] (std::initializer_list<std::string_view> words)
    {
        for (auto w : words)
            if (name.find (w) != std::string::npos)
                return true;

        return false;
    };

    if (hasAny ({ "backing", "adlib", "ad-lib", "ad lib", "dobra", "coro", "choir", "harmonia", "apoio" }) || hasWord ("bv"))
        return "backing";
    // Beat pronto (o instrumental inteiro num arquivo): antes de "kick", "bass" e "melody".
    if (hasAny ({ "instrumental", "type beat", "typebeat", "2track", "2 track", "2-track", "two track" })
        || hasWord ("beat") || hasWord ("instru")
        || ((hasWord ("base") || hasWord ("prod")) && ! hasAny ({ "808", "bass", "baixo", "kick", "bumbo" })))
        return "beat";
    if (hasAny ({ "vocal", "voz", "vox", "acapella", "acappella", "refr" }) || hasWord ("verso"))
        return "vocal";
    if (hasAny ({ "kick", "bumbo", "kik" }) || hasWord ("bd"))
        return "kick";
    if (hasAny ({ "808", "bass", "baixo", "sub" }) || hasWord ("grave"))
        return "bass";
    if (hasAny ({ "snare", "caixa", "clap", "palma" }) || hasWord ("rim"))
        return "snare";
    if (hasAny ({ "hat", "chimbal", "perc", "shaker", "tamb", "conga", "bongo", "cymbal", "prato", "ride", "crash" })
        || hasWord ("hh") || hasWord ("tom"))
        return "hats";
    if (hasAny ({ "reverb", "verb" }))
        return "reverbBus";
    if (hasAny ({ "delay" }) || hasWord ("eco"))
        return "delayBus";
    if (hasAny ({ "riser", "sweep", "impact", "efeito", "transi", "vinil", "vinyl", "noise", "ruido" }) || hasWord ("fx"))
        return "fx";

    return "melody";
}

//==============================================================================
// Estilos de mixagem

const RoleRecipe* MixStyle::recipeFor (std::string_view role) const noexcept
{
    for (auto& r : roles)
        if (role == r.role)
            return &r;

    return nullptr;
}

#define WHYNOT_CHAIN(...) \
    [] { static const ChainItem c[] = { __VA_ARGS__ }; return std::span<const ChainItem> (c); }()

#define WHYNOT_ROLES(...) \
    [] { static const RoleRecipe r[] = { __VA_ARGS__ }; return std::span<const RoleRecipe> (r); }()

std::span<const MixStyle> allStyles()
{
    static const MixStyle styles[] = {
        { "trap", "Trap",
          "808 saturado e no centro, abaixando quando o kick bate; chimbal seco e aberto; voz comprimida, "
          "brilhante, com placa e eco de 1/4. Master alto com clipper.",
          -7.0f, "Trap costuma ficar entre -8 e -6 LUFS integrados.",
          WHYNOT_ROLES (
              { "kick",    WHYNOT_CHAIN ({ "eq", "kick-trap" }, { "transient", "kick-punch" }, { "comp", "kick-punch" }) },
              { "bass",    WHYNOT_CHAIN ({ "eq", "808-trap" }, { "sat", "808-trap" }, { "comp", "sidechain-duck", "kick" }, { "stereo", "bass-mono" }) },
              { "snare",   WHYNOT_CHAIN ({ "eq", "snare-clap" }, { "transient", "snare-snap" }, { "comp", "snare-punch" }), -14.0f },
              { "hats",    WHYNOT_CHAIN ({ "eq", "hats" }, { "transient", "hats-short" }, { "stereo", "hats-wide" }) },
              { "melody",  WHYNOT_CHAIN ({ "eq", "melody" }, { "stereo", "melody-wide" }), -12.0f, -18.0f },
              { "beat",    WHYNOT_CHAIN ({ "eq", "beat-space" }, { "multiband", "beat-space", "vocal" }, { "opto", "bus-glue" },
                                         { "stereo", "master-mono-low" }) },
              { "vocal",   WHYNOT_CHAIN ({ "eq", "vocal-trap" }, { "comp", "vocal-aggressive" }, { "deesser", "male" }, { "sat", "vocal-warm" }), -14.0f, -12.0f },
              { "backing", WHYNOT_CHAIN ({ "eq", "backing" }, { "comp", "vocal-control" }, { "stereo", "backing-wide" }), -10.0f, -14.0f },
              { "fx",      WHYNOT_CHAIN ({ "eq", "fx-clean" }), -12.0f }),
          WHYNOT_CHAIN ({ "eq", "master-clean" }, { "comp", "master-glue" }, { "sat", "master-tape" }, { "stereo", "master-mono-low" }, { "limiter", "master-trap" }),
          WHYNOT_CHAIN ({ "reverb", "plate-vocal" }, { "eq", "reverb-return" }),
          WHYNOT_CHAIN ({ "delay", "quarter-vocal" }, { "eq", "delay-return" }) },

        { "funk", "Funk",
          "Kick e grave estourados, percussão seca e agressiva, voz na cara com eco de colcheia e quase "
          "nenhum reverb. Master no limite, com muito clipper.",
          -5.5f, "Funk costuma ser o mais alto: entre -6 e -4 LUFS integrados.",
          WHYNOT_ROLES (
              { "kick",    WHYNOT_CHAIN ({ "eq", "kick-funk" }, { "sat", "kick-funk" }, { "transient", "kick-punch" }) },
              { "bass",    WHYNOT_CHAIN ({ "eq", "bass-funk" }, { "sat", "808-hard" }, { "comp", "bass-control" }, { "stereo", "bass-mono" }) },
              { "snare",   WHYNOT_CHAIN ({ "eq", "snare-clap" }, { "transient", "funk-perc" }, { "sat", "funk-blown" }), -20.0f },
              { "hats",    WHYNOT_CHAIN ({ "eq", "hats" }, { "transient", "funk-perc" }) },
              { "melody",  WHYNOT_CHAIN ({ "eq", "melody" }, { "comp", "melody-glue" }, { "stereo", "melody-wide" }), -16.0f, -20.0f },
              { "beat",    WHYNOT_CHAIN ({ "eq", "beat-space" }, { "multiband", "beat-space", "vocal" }, { "comp", "melody-glue" },
                                         { "stereo", "master-mono-low" }) },
              { "vocal",   WHYNOT_CHAIN ({ "eq", "vocal-funk" }, { "comp", "vocal-aggressive" }, { "deesser", "aggressive" }, { "sat", "vocal-funk" }), -20.0f, -14.0f },
              { "backing", WHYNOT_CHAIN ({ "eq", "backing" }, { "comp", "vocal-aggressive" }, { "stereo", "backing-wide" }), noSend, -16.0f },
              { "fx",      WHYNOT_CHAIN ({ "eq", "fx-clean" }), -16.0f }),
          WHYNOT_CHAIN ({ "eq", "master-funk" }, { "comp", "master-glue" }, { "sat", "master-funk" }, { "stereo", "master-mono-low" }, { "limiter", "master-funk" }),
          WHYNOT_CHAIN ({ "reverb", "funk-ambience" }, { "eq", "reverb-return" }),
          WHYNOT_CHAIN ({ "delay", "funk-echo" }, { "eq", "delay-return" }) },

        { "rap", "Rap",
          "Voz clara e na frente, cada palavra inteligível; bateria com compressão paralela; sample "
          "aquecido e um pouco abafado; slap curto na voz e sala discreta.",
          -8.0f, "Rap costuma ficar entre -9 e -7 LUFS integrados.",
          WHYNOT_ROLES (
              { "kick",    WHYNOT_CHAIN ({ "eq", "kick-boombap" }, { "comp", "drum-parallel" }, { "sat", "drum-glue" }) },
              { "bass",    WHYNOT_CHAIN ({ "eq", "bass-warm" }, { "comp", "bass-control" }, { "sat", "bass-warm" }, { "stereo", "bass-mono" }) },
              { "snare",   WHYNOT_CHAIN ({ "eq", "snare-clap" }, { "transient", "snare-snap" }, { "comp", "drum-parallel" }), -16.0f },
              { "hats",    WHYNOT_CHAIN ({ "eq", "hats" }, { "transient", "hats-short" }) },
              { "melody",  WHYNOT_CHAIN ({ "eq", "sample-lofi" }, { "sat", "lofi-sample" }, { "comp", "melody-glue" }), -18.0f },
              { "beat",    WHYNOT_CHAIN ({ "eq", "beat-space" }, { "multiband", "beat-space", "vocal" }, { "opto", "bus-glue" }) },
              { "vocal",   WHYNOT_CHAIN ({ "eq", "vocal-rap" }, { "comp", "vocal-control" }, { "deesser", "male" }, { "comp", "vocal-smooth" }), -20.0f, -18.0f },
              { "backing", WHYNOT_CHAIN ({ "eq", "backing" }, { "comp", "vocal-control" }, { "stereo", "backing-wide" }), -14.0f },
              { "fx",      WHYNOT_CHAIN ({ "eq", "fx-clean" }), -14.0f }),
          WHYNOT_CHAIN ({ "eq", "master-clean" }, { "comp", "master-glue" }, { "sat", "master-tape" }, { "limiter", "master-rap" }),
          WHYNOT_CHAIN ({ "reverb", "room-short" }, { "eq", "reverb-return" }),
          WHYNOT_CHAIN ({ "delay", "slap" }, { "eq", "delay-return" }) },

        { "pop", "Pop",
          "Equilíbrio e polimento: voz suave com dois compressores, salão e 1/8 pontuado; grave "
          "controlado com sidechain leve; imagem larga; master mais dinâmico.",
          -9.0f, "Pop costuma ficar entre -10 e -8 LUFS integrados.",
          WHYNOT_ROLES (
              { "kick",    WHYNOT_CHAIN ({ "eq", "kick-pop" }, { "comp", "kick-punch" }, { "transient", "drums-punch" }) },
              { "bass",    WHYNOT_CHAIN ({ "eq", "bass-warm" }, { "comp", "sidechain-soft", "kick" }, { "sat", "bass-warm" }, { "stereo", "bass-mono" }) },
              { "snare",   WHYNOT_CHAIN ({ "eq", "snare-clap" }, { "comp", "snare-punch" }), -12.0f },
              { "hats",    WHYNOT_CHAIN ({ "eq", "hats" }, { "stereo", "hats-wide" }) },
              { "melody",  WHYNOT_CHAIN ({ "eq", "melody" }, { "comp", "melody-glue" }, { "stereo", "melody-wide" }), -12.0f, -18.0f },
              { "beat",    WHYNOT_CHAIN ({ "eq", "beat-space" }, { "multiband", "beat-space", "vocal" }, { "opto", "bus-glue" },
                                         { "stereo", "master-pop" }) },
              { "vocal",   WHYNOT_CHAIN ({ "eq", "vocal-pop" }, { "comp", "vocal-control" }, { "deesser", "female" }, { "comp", "vocal-smooth" }, { "sat", "vocal-warm" }), -10.0f, -14.0f },
              { "backing", WHYNOT_CHAIN ({ "eq", "backing" }, { "comp", "vocal-smooth" }, { "stereo", "backing-wide" }), -8.0f, -16.0f },
              { "fx",      WHYNOT_CHAIN ({ "eq", "fx-clean" }), -10.0f }),
          WHYNOT_CHAIN ({ "eq", "master-pop" }, { "comp", "master-glue" }, { "stereo", "master-pop" }, { "limiter", "master-pop" }),
          WHYNOT_CHAIN ({ "reverb", "hall-pop" }, { "eq", "reverb-return" }),
          WHYNOT_CHAIN ({ "delay", "dotted-eighth" }, { "eq", "delay-return" }) },
    };

    return styles;
}

#undef WHYNOT_CHAIN
#undef WHYNOT_ROLES

const MixStyle* findStyle (std::string_view id)
{
    for (auto& s : allStyles())
        if (id == s.id)
            return &s;

    return nullptr;
}

} // namespace whynot::dsp
