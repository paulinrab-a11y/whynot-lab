#include "SynthParams.h"
#include "Wavetables.h"

#include "../dsp/Color.h"
#include "../dsp/Space.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>

namespace whynot::synth
{

namespace
{
    const char* const layerTypes[] = { "Analógico", "Wavetable", "FM", "Ruído" };
    const char* const waves[]      = { "Serra", "Pulso", "Triângulo", "Seno" };
    const char* const octaves[]    = { "-3", "-2", "-1", "0", "+1", "+2", "+3" };
    const char* const semitones[]  = { "-12", "-11", "-10", "-9", "-8", "-7", "-6", "-5", "-4", "-3", "-2", "-1", "0",
                                       "+1", "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10", "+11", "+12" };
    const char* const counts[]     = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
                                       "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32" };
    const char* const fmAlgos[]    = { "2 op: B→A", "3 op: C→B→A", "3 op: B+C→A", "4 op: D→C→B→A", "4 op: B+C+D→A", "2 pares: B→A + D→C" };
    const char* const subOctaves[] = { "-1 oitava", "-2 oitavas" };
    const char* const subWaves[]   = { "Seno", "Triângulo", "Quadrada" };
    const char* const fltModes[]   = { "Passa-baixas", "Passa-altas", "Passa-banda", "Rejeita-banda" };
    const char* const fltSlopes[]  = { "12 dB/oit", "24 dB/oit" };
    const char* const lfoShapes[]  = { "Seno", "Triângulo", "Rampa sobe", "Rampa desce", "Quadrada", "Aleatório", "Aleatório suave" };
    const char* const lfoDivs[]    = { "4 compassos", "2 compassos", "1 compasso", "1/2", "1/2T", "1/4.", "1/4", "1/4T",
                                       "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    const double lfoDivBeats[]     = { 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    const char* const retrigModes[] = { "Livre", "Por nota" };
    const char* const portaModes[] = { "Só no slide", "Sempre" };

    const char* const sources[] = { "Nenhuma", "Env filtro", "Env mod", "LFO 1", "LFO 2", "Força", "Nota", "Mod wheel",
                                    "Aftertouch", "Aleatório", "Env volume" };
    const char* const dests[]   = { "Nenhum", "Afinação",
                                    "Afinação 1", "Afinação 2", "Afinação 3", "Afinação 4",
                                    "Volume 1", "Volume 2", "Volume 3", "Volume 4",
                                    "Forma 1", "Forma 2", "Forma 3", "Forma 4",
                                    "Corte do filtro", "Ressonância", "Drive do filtro", "Volume", "Pan", "Desafinação",
                                    "Taxa LFO 1", "Taxa LFO 2", "Volume do sub" };

    static_assert (std::size (sources) == (size_t) Source::count);
    static_assert (std::size (dests) == (size_t) Dest::count);
    static_assert (std::size (fmAlgos) == (size_t) FmAlgo::count);
    static_assert (std::size (lfoDivs) == std::size (lfoDivBeats));

    template <size_t N>
    std::span<const char* const> list (const char* const (&a)[N])   { return { a, N }; }

    struct Table
    {
        std::vector<ParamInfo> params;
        std::vector<const char*> sections;
        std::vector<float> variation;
        std::vector<bool> numeric;
        std::deque<std::string> strings;   // ids e nomes gerados (deque: as referências não mudam)

        const char* keep (std::string s)   { strings.push_back (std::move (s)); return strings.back().c_str(); }

        void add (const char* section, ParamInfo p, float vary = 0.0f, bool numericChoices = false)
        {
            params.push_back (p);
            sections.push_back (section);
            variation.push_back (vary);
            numeric.push_back (numericChoices);
        }
    };

    ParamInfo cont (const char* id, const char* name, float lo, float hi, float def, const char* unit,
                    bool logScale = false, const char* help = "")
    {
        return { id, name, lo, hi, def, unit, ParamKind::continuous, logScale, {}, help };
    }

    ParamInfo toggle (const char* id, const char* name, float def, const char* help = "")
    {
        return { id, name, 0, 1, def, "", ParamKind::toggle, false, {}, help };
    }

    ParamInfo choice (const char* id, const char* name, std::span<const char* const> choices, float def, const char* help = "")
    {
        return { id, name, 0, (float) choices.size() - 1, def, "", ParamKind::choice, false, choices, help };
    }

    Table build()
    {
        Table t;
        t.params.reserve (numParams);

        const auto tables = Wavetables::names();
        const auto satModes = dsp::SaturatorProcessor::staticInfo().params[dsp::SaturatorProcessor::mode].choices;
        const auto delayDivs = dsp::DelayProcessor::staticInfo().params[dsp::DelayProcessor::division].choices;

        //----------------------------------------------------------------------
        // Camadas
        for (int l = 0; l < numLayers; ++l)
        {
            const std::string pre = "l" + std::to_string (l + 1) + ".";
            const auto* section = t.keep ("Camada " + std::to_string (l + 1));
            auto id = [&] (const char* s) { return t.keep (pre + s); };
            auto nm = [&] (const char* s) { return t.keep (std::string (s) + " " + std::to_string (l + 1)); };

            t.add (section, toggle (id ("on"), nm ("Camada"), l == 0 ? 1.0f : 0.0f, "Liga ou desliga a camada (desligada não gasta CPU)."));
            t.add (section, choice (id ("type"), nm ("Tipo"), list (layerTypes), 0,
                                    "Analógico: ondas clássicas. Wavetable: tabelas que mudam com a Forma. FM: operadores (sinos, pianos elétricos, graves). Ruído: ar e textura."));
            t.add (section, cont (id ("level"), nm ("Volume"), -36, 6, -6, "dB"), 0.06f);
            t.add (section, cont (id ("pan"), nm ("Pan"), -100, 100, 0, "%"), 0.1f);
            t.add (section, choice (id ("oct"), nm ("Oitava"), list (octaves), 3), 0.0f, true);
            t.add (section, choice (id ("semi"), nm ("Semitom"), list (semitones), 12), 0.0f, true);
            t.add (section, cont (id ("fine"), nm ("Ajuste fino"), -100, 100, 0, "ct"), 0.08f);
            t.add (section, choice (id ("voices"), nm ("Vozes"), list (counts).first (maxUnison), 0,
                                    "Unison: cópias desafinadas da camada, abertas no estéreo (som largo)."), 0.0f, true);
            t.add (section, cont (id ("detune"), nm ("Desafinação"), 0, 100, 20, "ct", false, "Distância entre as vozes do unison."), 0.25f);
            t.add (section, cont (id ("width"), nm ("Abertura"), 0, 100, 70, "%", false, "Quanto as vozes do unison se abrem para os lados."), 0.2f);
            t.add (section, choice (id ("wave"), nm ("Onda"), list (waves), 0));
            t.add (section, cont (id ("shape"), nm ("Forma"), 0, 100, 50, "%", false,
                                  "Analógico: largura do pulso. Wavetable: posição na tabela. FM: brilho (índice). Ruído: cor (escuro a claro)."), 0.18f);
            t.add (section, choice (id ("table"), nm ("Tabela"), tables, 0));
            t.add (section, cont (id ("phase"), nm ("Fase livre"), 0, 100, 0, "%", false,
                                  "0% = toda nota começa igual (ataque firme, 808). 100% = começo aleatório (pads e unison)."));
            t.add (section, toggle (id ("filter"), nm ("Pelo filtro"), 1, "Desligado: a camada pula o filtro (grave limpo)."));
            t.add (section, choice (id ("fmAlgo"), nm ("Algoritmo"), list (fmAlgos), 0, "Quem modula quem (A é a portadora que soa)."));
            t.add (section, cont (id ("fmRatio2"), nm ("Razão B"), 0.5f, 16, 1, "x", true, "Frequência do operador B em relação à nota."));
            t.add (section, cont (id ("fmRatio3"), nm ("Razão C"), 0.5f, 16, 2, "x", true));
            t.add (section, cont (id ("fmRatio4"), nm ("Razão D"), 0.5f, 16, 3, "x", true));
            t.add (section, cont (id ("fmAmt2"), nm ("Índice B"), 0, 100, 30, "%", false, "Quanto o operador B modula: mais = mais brilho e metal."), 0.15f);
            t.add (section, cont (id ("fmAmt3"), nm ("Índice C"), 0, 100, 0, "%"), 0.15f);
            t.add (section, cont (id ("fmAmt4"), nm ("Índice D"), 0, 100, 0, "%"), 0.15f);
            t.add (section, cont (id ("fmFeedback"), nm ("Realimentação"), 0, 100, 0, "%", false, "O último operador modula a si mesmo (som mais áspero)."), 0.12f);
            t.add (section, cont (id ("fmDecay"), nm ("Queda do brilho"), 5, 10000, 600, "ms", true, "Tempo para o índice cair até o brilho sustentado."), 0.2f);
            t.add (section, cont (id ("fmSustain"), nm ("Brilho sustentado"), 0, 100, 50, "%"), 0.12f);
        }

        //----------------------------------------------------------------------
        t.add ("Sub", toggle ("sub.on", "Sub", 0, "Oscilador uma ou duas oitavas abaixo, sem passar pelo filtro."));
        t.add ("Sub", cont ("sub.level", "Volume do sub", -36, 6, -6, "dB"), 0.05f);
        t.add ("Sub", choice ("sub.oct", "Oitava do sub", list (subOctaves), 0));
        t.add ("Sub", choice ("sub.wave", "Onda do sub", list (subWaves), 0));

        t.add ("Filtro", toggle ("flt.on", "Filtro", 1));
        t.add ("Filtro", choice ("flt.mode", "Tipo do filtro", list (fltModes), 0));
        t.add ("Filtro", choice ("flt.slope", "Inclinação", list (fltSlopes), 1));
        t.add ("Filtro", cont ("flt.cutoff", "Corte", 20, 20000, 20000, "Hz", true, "Frequência do filtro."), 0.12f);
        t.add ("Filtro", cont ("flt.reso", "Ressonância", 0, 100, 10, "%", false, "Realça a região do corte."), 0.15f);
        t.add ("Filtro", cont ("flt.drive", "Drive", 0, 100, 0, "%", false, "Satura antes do filtro."), 0.15f);
        t.add ("Filtro", cont ("flt.key", "Seguir nota", 0, 100, 0, "%", false, "100% = o corte acompanha a nota (o timbre fica igual em toda a extensão)."), 0.1f);
        t.add ("Filtro", cont ("flt.env", "Envelope", -8, 8, 0, "oit", false, "Quantas oitavas a Env filtro abre (ou fecha) o corte."), 0.1f);
        t.add ("Filtro", cont ("flt.vel", "Força abre", 0, 100, 0, "%", false, "Notas fortes abrem mais o filtro."), 0.1f);

        //----------------------------------------------------------------------
        const char* envNames[] = { "Env volume", "Env filtro", "Env mod" };
        const float envDefaults[numEnvs][envParamCount] = { { 2, 300, 100, 120, 50 }, { 2, 400, 0, 300, 50 }, { 2, 300, 0, 300, 50 } };

        for (int e = 0; e < numEnvs; ++e)
        {
            const std::string pre = "env" + std::to_string (e + 1) + ".";
            const char* section = envNames[e];
            auto id = [&] (const char* s) { return t.keep (pre + s); };
            auto nm = [&] (const char* s) { return t.keep (std::string (s) + " (" + envNames[e] + ")"); };
            const auto* d = envDefaults[e];

            t.add (section, cont (id ("attack"), nm ("Ataque"), 0.5f, 10000, d[0], "ms", true), 0.15f);
            t.add (section, cont (id ("decay"), nm ("Decaimento"), 1, 20000, d[1], "ms", true), 0.15f);
            t.add (section, cont (id ("sustain"), nm ("Sustentação"), 0, 100, d[2], "%"), 0.12f);
            t.add (section, cont (id ("release"), nm ("Soltura"), 1, 20000, d[3], "ms", true), 0.15f);
            t.add (section, cont (id ("curve"), nm ("Curva"), 0, 100, d[4], "%", false, "0% = reta; 100% = bem exponencial (ataque estalado, queda natural)."), 0.12f);
        }

        for (int l = 0; l < numLfos; ++l)
        {
            const std::string pre = "lfo" + std::to_string (l + 1) + ".";
            const auto* section = t.keep ("LFO " + std::to_string (l + 1));
            auto id = [&] (const char* s) { return t.keep (pre + s); };
            auto nm = [&] (const char* s) { return t.keep (std::string (s) + " (LFO " + std::to_string (l + 1) + ")"); };

            t.add (section, choice (id ("shape"), nm ("Onda"), list (lfoShapes), 0));
            t.add (section, toggle (id ("sync"), nm ("Sincronizar"), 0, "Segue o BPM do projeto."));
            t.add (section, cont (id ("rate"), nm ("Taxa"), 0.02f, 30, 4, "Hz", true), 0.1f);
            t.add (section, choice (id ("div"), nm ("Divisão"), list (lfoDivs), 6, "T = tercina; ponto = pontuada."));
            t.add (section, choice (id ("retrig"), nm ("Reinício"), list (retrigModes), 1, "Por nota: cada nota começa o LFO do zero."));
            t.add (section, cont (id ("fade"), nm ("Entrada"), 0, 5000, 0, "ms", false, "Tempo para o LFO chegar ao máximo depois da nota (vibrato que entra devagar)."), 0.1f);
        }

        for (int s = 0; s < numSlots; ++s)
        {
            const std::string pre = "mod" + std::to_string (s + 1) + ".";
            auto id = [&] (const char* x) { return t.keep (pre + x); };
            auto nm = [&] (const char* x) { return t.keep (std::string (x) + " " + std::to_string (s + 1)); };

            t.add ("Matriz", choice (id ("src"), nm ("Fonte"), list (sources), 0));
            t.add ("Matriz", choice (id ("dst"), nm ("Destino"), list (dests), 0));
            t.add ("Matriz", cont (id ("amt"), nm ("Quantidade"), -100, 100, 0, "%"), 0.12f);
        }

        //----------------------------------------------------------------------
        t.add ("Voz", cont ("volume", "Volume", -36, 24, 0, "dB", false, "Volume do timbre (os presets já vêm nivelados)."));
        t.add ("Voz", choice ("poly", "Polifonia", list (counts), 15, "Quantas notas soam juntas; a mais antiga sai com um fade curto."), 0.0f, true);
        t.add ("Voz", toggle ("legato", "Legato", 1, "No mono, nota ligada não reinicia os envelopes."));
        t.add ("Voz", choice ("porta", "Glide", list (portaModes), 0, "Só no slide: desliza nas notas ligadas (slide do piano roll). Sempre: toda nota desliza."));
        t.add ("Voz", cont ("velAmp", "Força no volume", 0, 100, 50, "%", false, "Quanto a força da nota muda o volume."), 0.1f);
        t.add ("Voz", cont ("bend", "Pitch bend", 0, 24, 2, "st"));
        t.add ("Voz", cont ("drift", "Deriva", 0, 100, 10, "%", false, "Pequenas diferenças de afinação a cada nota, como num synth analógico."), 0.2f);

        t.add ("Saturação", toggle ("sat.on", "Saturação", 0));
        t.add ("Saturação", choice ("sat.mode", "Tipo da saturação", satModes, 0));
        t.add ("Saturação", cont ("sat.drive", "Drive da saturação", 0, 36, 6, "dB"), 0.12f);
        t.add ("Saturação", cont ("sat.mix", "Mistura da saturação", 0, 100, 100, "%"), 0.1f);

        t.add ("EQ", toggle ("eq.on", "EQ", 0));
        t.add ("EQ", cont ("eq.hp", "Corte grave do EQ", 20, 1000, 20, "Hz", true));
        t.add ("EQ", cont ("eq.low", "Grave", -12, 12, 0, "dB"), 0.08f);
        t.add ("EQ", cont ("eq.mid", "Médio", -12, 12, 0, "dB"), 0.08f);
        t.add ("EQ", cont ("eq.midFreq", "Frequência do médio", 200, 8000, 1000, "Hz", true));
        t.add ("EQ", cont ("eq.high", "Agudo", -12, 12, 0, "dB"), 0.08f);

        t.add ("Estéreo", cont ("width", "Largura", 0, 200, 100, "%", false, "0% = mono, 100% = original, 200% = bem aberto."), 0.1f);

        t.add ("Delay", toggle ("dly.on", "Delay", 0));
        t.add ("Delay", choice ("dly.div", "Divisão do delay", delayDivs, 5));
        t.add ("Delay", cont ("dly.fb", "Repetições", 0, 95, 30, "%"), 0.1f);
        t.add ("Delay", cont ("dly.mix", "Mistura do delay", 0, 100, 20, "%"), 0.1f);

        t.add ("Reverb", toggle ("rev.on", "Reverb", 0));
        t.add ("Reverb", cont ("rev.size", "Tamanho", 0, 100, 50, "%"), 0.1f);
        t.add ("Reverb", cont ("rev.decay", "Decaimento do reverb", 0.2f, 12, 2, "s", true), 0.1f);
        t.add ("Reverb", cont ("rev.mix", "Mistura do reverb", 0, 100, 20, "%"), 0.1f);

        auto hidden = [] (ParamInfo p) { p.hidden = true; return p; };
        t.add ("App", hidden (toggle ("mono", "Mono", 0, "Vem do canal (channel.setMono).")));
        t.add ("App", hidden (cont ("glide", "Tempo do glide", 0, 2000, 80, "ms", false, "Vem do canal (channel.setGlide).")));
        t.add ("App", hidden (cont ("bpm", "Andamento", 20, 999, 120, "BPM", false, "Vem do projeto.")));

        return t;
    }

    const Table& table()
    {
        static const Table t = build();
        return t;
    }

    /** Posição 0..1 do valor na faixa (logarítmica quando o parâmetro é). */
    float toNormalised (const ParamInfo& p, float v)
    {
        if (p.logScale && p.min > 0.0f)
            return std::log (std::max (v, p.min) / p.min) / std::log (p.max / p.min);

        return (v - p.min) / (p.max - p.min);
    }

    float fromNormalised (const ParamInfo& p, float u)
    {
        u = std::clamp (u, 0.0f, 1.0f);

        if (p.logScale && p.min > 0.0f)
            return p.min * std::pow (p.max / p.min, u);

        return p.min + u * (p.max - p.min);
    }
}

std::span<const ParamInfo> allParams()
{
    const auto& t = table();
    return { t.params.data(), t.params.size() };
}

const ParamInfo& param (int index)
{
    return table().params[(size_t) std::clamp (index, 0, numParams - 1)];
}

int indexOf (std::string_view id) noexcept
{
    const auto& params = table().params;

    for (size_t i = 0; i < params.size(); ++i)
        if (id == params[i].id)
            return (int) i;

    return -1;
}

std::vector<float> defaultValues()
{
    std::vector<float> v;
    v.reserve (numParams);

    for (auto& p : allParams())
        v.push_back (p.def);

    return v;
}

const char* sectionOf (int index)
{
    return table().sections[(size_t) std::clamp (index, 0, numParams - 1)];
}

float variationRange (int index) noexcept
{
    return index >= 0 && index < numParams ? table().variation[(size_t) index] : 0.0f;
}

bool hasNumericChoices (int index) noexcept
{
    return index >= 0 && index < numParams && table().numeric[(size_t) index];
}

std::span<const char* const> sourceNames()   { return list (sources); }
std::span<const char* const> destNames()     { return list (dests); }

double lfoDivisionBeats (int division) noexcept
{
    return lfoDivBeats[std::clamp (division, 0, (int) std::size (lfoDivBeats) - 1)];
}

std::vector<float> varied (const std::vector<float>& values, float amount, std::uint32_t seed)
{
    auto out = values;
    out.resize (numParams, 0.0f);
    amount = std::clamp (amount, 0.0f, 1.0f);
    Random rng (seed * 2654435761u + 12345u);

    for (int i = 0; i < numParams; ++i)
    {
        const float range = variationRange (i);
        const auto& p = param (i);

        if (range <= 0.0f || p.kind != ParamKind::continuous || p.hidden)
        {
            rng.next();   // mesma sequência para a mesma semente, mexa ou não
            continue;
        }

        // Quantidades de modulação e envelope do filtro: zero continua zero (não cria rota nova)
        // e o sinal não troca (um envelope que abre não passa a fechar).
        const bool signedAmount = i == P::fltEnv
                                  || (i >= P::slotBase && i < P::globalBase && (i - P::slotBase) % slotParamCount == mAmt);
        const float v = out[(size_t) i];
        const float r = rng.bipolar();

        if (signedAmount)
        {
            if (v == 0.0f)
                continue;

            const float magnitude = std::abs (v) * (1.0f + r * range * amount * 2.0f);
            out[(size_t) i] = std::copysign (std::clamp (magnitude, 0.0f, p.max), v);
            continue;
        }

        const float u = toNormalised (p, v) + r * range * amount;
        out[(size_t) i] = p.clamp (fromNormalised (p, u));
    }

    // Limites musicais: o filtro de um passa-baixas não fecha a ponto de sumir o som.
    const float cutoff = values[(size_t) P::fltCutoff];
    out[(size_t) P::fltCutoff] = std::max (out[(size_t) P::fltCutoff], std::min (cutoff, 120.0f));
    return out;
}

} // namespace whynot::synth
