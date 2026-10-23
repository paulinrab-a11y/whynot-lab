#pragma once

// Interface comum dos efeitos nativos da WHYNOT (sem JUCE).
//
// Cada efeito descreve os próprios parâmetros (FxInfo) e processa áudio estéreo.
// Quem hospeda (FxHost) entrega os valores novos com setParam() na thread de áudio,
// antes de process(); prepare() roda com o áudio parado e é o único lugar que aloca.

#include "DspCommon.h"

#include <atomic>
#include <span>
#include <string_view>

namespace whynot::dsp
{

enum class ParamKind { continuous, toggle, choice };

struct ParamInfo
{
    const char* id;          // estável: vai para o .wnl e para a IA
    const char* name;        // rótulo em português
    float min = 0.0f, max = 1.0f, def = 0.0f;
    const char* unit = "";   // "dB", "Hz", "ms", "%", ":1", "s", "LUFS"
    ParamKind kind = ParamKind::continuous;
    bool logScale = false;   // frequências e tempos: o knob anda em escala logarítmica
    std::span<const char* const> choices {};
    const char* help = "";
    bool hidden = false;     // controlado pelo app (ex.: BPM do delay): fora da interface e do .wnl

    float clamp (float v) const noexcept
    {
        v = std::clamp (v, min, max);
        return kind == ParamKind::continuous ? v : std::round (v);
    }
};

struct FxInfo
{
    const char* type;          // "eq", "comp"...: identificador estável
    const char* name;          // "Equalizador"
    const char* shortName;     // "EQ" (cabe no slot do mixer)
    const char* category;      // "Equalização", "Dinâmica", "Cor", "Espaço", "Imagem", "Master"
    const char* description;
    std::span<const ParamInfo> params;
    bool sidechain = false;    // aceita disparo externo (outra trilha)

    int indexOf (const char* id) const noexcept;
};

/** Um bloco estéreo. side* = sinal de sidechain (nullptr se não houver). */
struct AudioBlock
{
    float* left = nullptr;
    float* right = nullptr;
    int numSamples = 0;
    const float* sideLeft = nullptr;
    const float* sideRight = nullptr;
};

class Processor
{
public:
    virtual ~Processor() = default;

    virtual const FxInfo& info() const noexcept = 0;

    /** Com o áudio parado. Pode alocar. Depois dele, todos os parâmetros chegam de novo. */
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Zera o estado (caudas, envelopes) sem alocar. */
    virtual void reset() noexcept = 0;

    /** Thread de áudio, antes de process(). O valor já vem dentro da faixa. */
    virtual void setParam (int index, float value) noexcept = 0;

    virtual void process (AudioBlock&) noexcept = 0;

    /** Pula as rampas: os valores recebidos por setParam passam a valer já (depois do prepare,
        para o render não começar deslizando dos padrões). */
    virtual void snapToTargets() noexcept {}

    /** Zera o estado aos poucos, no máximo `budget` amostras de memória por chamada (para efeitos
        com linhas de atraso grandes não zerarem megabytes num bloco de áudio). true = terminou. */
    virtual bool clearStep (int /*budget*/) noexcept   { reset(); return true; }

    virtual int latencySamples() const noexcept   { return 0; }

    /** Duração da cauda para um conjunto de valores (pode ser chamada de qualquer thread:
        não lê o estado do efeito). */
    virtual double tailSecondsFor (const float* /*values*/) const noexcept   { return 0.0; }

    /** Medidores para a interface, lidos de qualquer thread (o sentido depende do efeito). */
    static constexpr int numMeters = 8;
    float meter (int index) const noexcept   { return meters[(size_t) index].load (std::memory_order_relaxed); }

protected:
    void setMeter (int index, float value) noexcept   { meters[(size_t) index].store (value, std::memory_order_relaxed); }

private:
    std::atomic<float> meters[numMeters] {};
};

inline int FxInfo::indexOf (const char* id) const noexcept
{
    for (size_t i = 0; i < params.size(); ++i)
        if (std::string_view (params[i].id) == id)
            return (int) i;

    return -1;
}

} // namespace whynot::dsp
