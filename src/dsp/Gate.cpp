#include "Gate.h"

namespace whynot::dsp
{

namespace
{
    const char* const gateModes[] = { "Gate", "Expansor" };

    const ParamInfo gateParams[] = {
        { "mode",       "Modo",       0, 1, 0,        "",   ParamKind::choice,     false, gateModes,
          "Gate fecha de vez abaixo do limiar; o expansor abaixa aos poucos (mais natural na voz)." },
        { "threshold",  "Limiar",     -80, 0, -45,    "dB", ParamKind::continuous, false, {},
          "Abaixo disto o som é abaixado: o ruído e o vazamento entre as frases." },
        { "range",      "Redução",    0, 80, 30,      "dB", ParamKind::continuous, false, {},
          "Quanto fecha. 80 dB é mudo; 10 a 20 dB soa natural na voz." },
        { "ratio",      "Razão",      1, 8, 2,        ":1", ParamKind::continuous, false, {},
          "Só no expansor: cada 1 dB abaixo do limiar vira esta quantidade." },
        { "attack",     "Ataque",     0.05f, 50, 0.5f, "ms", ParamKind::continuous, true, {},
          "Quanto tempo leva para abrir. Com a antecipação de 1,5 ms, o começo da palavra passa inteiro." },
        { "hold",       "Segurar",    0, 500, 40,     "ms", ParamKind::continuous, false, {},
          "Fica aberto este tempo depois que o som cai, para não cortar o fim das palavras." },
        { "release",    "Soltura",    5, 2000, 150,   "ms", ParamKind::continuous, true,  {},
          "Quanto tempo leva para fechar de todo (no expansor, o tempo de reação)." },
        { "hysteresis", "Histerese",  0, 12, 4,       "dB", ParamKind::continuous, false, {},
          "Abre no limiar e só fecha alguns dB abaixo: não fica abrindo e fechando no fim das notas." },
        { "scHpf",      "Filtro do detector", 20, 2000, 20, "Hz", ParamKind::continuous, true, {},
          "Graves abaixo daqui não abrem o gate (o grave do beat vazando do fone, por exemplo)." },
    };

    static_assert (std::size (gateParams) == GateProcessor::count);
}

const FxInfo& GateProcessor::staticInfo()
{
    static const FxInfo info { "gate", "Gate", "Gate", "Dinâmica",
                               "Tira o ruído e o vazamento entre as frases da voz gravada em casa. Aceita disparo de outra trilha.",
                               gateParams, true };
    return info;
}

void GateProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = gateParams[i].def;

    lookahead = std::max (1, (int) std::lround (lookaheadSeconds * rate));
    delayL.prepare (lookahead + 4);
    delayR.prepare (lookahead + 4);
    core.prepare (rate);
    dirty = true;
    update();
    reset();
}

void GateProcessor::reset() noexcept
{
    core.reset();
    hpL.reset();
    hpR.reset();
    delayL.reset();
    delayR.reset();
    setMeter (gainReduction, 0.0f);
    setMeter (inputLevel, -120.0f);
    setMeter (openState, 0.0f);
}

void GateProcessor::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < count)
    {
        values[(size_t) index] = value;
        dirty = true;
    }
}

void GateProcessor::update() noexcept
{
    dirty = false;
    core.set (values[mode] >= 0.5f, values[threshold], values[range], values[ratio],
              values[attack], values[hold], values[release], values[hysteresis]);

    const bool wasUsingHp = useHp;
    useHp = values[scHpf] > 21.0f;

    if (useHp && ! wasUsingHp)
    {
        hpL.reset();
        hpR.reset();
    }

    hpCoeffs = SvfCoeffs::make (FilterType::highpass, values[scHpf], 0.7071, 0.0, sampleRate);
}

void GateProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;

    if (dirty)
        update();

    const bool external = block.sideLeft != nullptr && block.sideRight != nullptr;
    float minGain = 1.0f, inPeak = 0.0f;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float l = block.left[i], r = block.right[i];
        float kl = external ? block.sideLeft[i] : l;
        float kr = external ? block.sideRight[i] : r;

        if (useHp)
        {
            kl = hpL.process (kl, hpCoeffs);
            kr = hpR.process (kr, hpCoeffs);
        }

        const float key = std::max (std::abs (kl), std::abs (kr));
        inPeak = std::max (inPeak, key);
        const float g = core.next (key);
        minGain = std::min (minGain, g);

        // O áudio sai atrasado pela antecipação: o detector já viu o que vem.
        delayL.push (l);
        delayR.push (r);
        block.left[i] = delayL.read (lookahead) * g;
        block.right[i] = delayR.read (lookahead) * g;
    }

    setMeter (gainReduction, -gainToDb (minGain, -80.0f));
    setMeter (inputLevel, gainToDb (inPeak));
    setMeter (openState, core.isOpen() ? 1.0f : 0.0f);
}

} // namespace whynot::dsp
