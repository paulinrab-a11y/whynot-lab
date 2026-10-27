#include "Tune.h"

#include <bit>
#include <cmath>
#include <iterator>

namespace whynot::dsp
{

namespace
{
    const char* const keyNames[] = { "Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si" };
    const char* const scaleNames[] = { "Menor", "Maior", "Menor harmônica", "Frígio", "Dórico", "Pentatônica menor", "Cromática",
                                       "Personalizada" };
    const char* const rangeNames[] = { "Ampla", "Grave", "Aguda" };
    const char* const harmonyNames[] = { "Sem", "+3ª", "+5ª", "-3ª", "-8ª" };   // curtos: cabem na caixa de 80 px do knob

    constexpr const char* noteHelp = "Na escala Personalizada: esta nota vale (ligada) ou a voz não vai para ela (desligada).";

    const ParamInfo tuneParams[] = {
        { "key",    "Tom",        0, 11, 9,    "",   ParamKind::choice,     false, keyNames,
          "Tônica da música. Ao entrar no mixer, o efeito pega o tom do projeto." },
        { "scale",  "Escala",     0, 7, 0,     "",   ParamKind::choice,     false, scaleNames,
          "As notas permitidas. Cromática corrige para o semitom mais perto. Personalizada: as notas ligadas no teclado." },
        { "speed",  "Velocidade", 0, 400, 25,  "ms", ParamKind::continuous, false, {},
          "Quanto tempo a correção leva. 0 ms = salto na hora (o efeito robô); 50 a 150 ms soa natural." },
        { "amount", "Força",      0, 100, 100, "%",  ParamKind::continuous, false, {},
          "Quanto da distância até a nota certa é corrigido." },
        { "range",  "Voz",        0, 2, 0,     "",   ParamKind::choice,     false, rangeNames,
          "Faixa de tom procurada: grave 60-500 Hz, aguda 120-1000 Hz, ampla 60-1000 Hz." },
        { "mix",    "Mistura",    0, 100, 100, "%",  ParamKind::continuous, false, {},
          "Quanto da voz corrigida entra; o resto é a voz original, atrasada junto. Abaixo de 100%, enquanto corrige, "
          "as duas somam com um atraso que varia uns milissegundos: soa como uma dobra." },
        { "humanize", "Humanizar", 0, 100, 0, "%", ParamKind::continuous, false, {},
          "Nas notas longas a correção fica mais lenta e deixa o vibrato passar; a troca de nota continua rápida. "
          "0% com velocidade 0 = o efeito duro do trap." },
        { "follow",   "Seguir o tom do projeto", 0, 1, 1, "", ParamKind::toggle, false, {},
          "Ligado: quando o tom ou a escala do projeto mudam, a afinação muda junto. Escolher o tom aqui desliga." },
        { "reference", "Lá de referência", 430, 450, 440, "Hz", ParamKind::continuous, false, {},
          "A frequência do Lá. 440 Hz é o padrão; mude se o beat foi afinado em outra referência (432, 443...)." },
        { "fine",   "Ajuste fino", -50, 50, 0,  "cents", ParamKind::continuous, false, {},
          "Desloca todas as notas alvo em cents: para casar com um sample ou instrumento um pouco fora." },
        { "note_c",  "Nota Dó",   0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_cs", "Nota Dó#",  0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_d",  "Nota Ré",   0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_ds", "Nota Ré#",  0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_e",  "Nota Mi",   0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_f",  "Nota Fá",   0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_fs", "Nota Fá#",  0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_g",  "Nota Sol",  0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_gs", "Nota Sol#", 0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_a",  "Nota Lá",   0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_as", "Nota Lá#",  0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "note_b",  "Nota Si",   0, 1, 1, "", ParamKind::toggle, false, {}, noteHelp },
        { "formant", "Formante", -12, 12, 0, "st", ParamKind::continuous, false, {},
          "O timbre da voz, à parte da nota. 0 = o timbre fica igual ao corrigir; negativo = voz maior e mais grossa; "
          "positivo = menor e mais fina." },
        { "guide",   "Seguir a guia", 0, 1, 0, "", ParamKind::toggle, false, {},
          "Ligado: a nota vem da trilha escolhida em Guia (um synth ou piano tocando a melodia do piano roll), na oitava "
          "da voz. Quando a guia para, volta para a escala." },
        { "harmony", "Harmonia", 0, 4, 0, "", ParamKind::choice, false, harmonyNames,
          "Uma voz a mais, já afinada, a partir da nota corrigida: +3ª e +5ª = terça e quinta da escala acima; -3ª = terça abaixo; "
          "-8ª = a oitava exata abaixo." },
        { "harmonyLevel", "Nível da harmonia", 0, 100, 60, "%", ParamKind::continuous, false, {},
          "O volume da voz de harmonia. A voz principal continua igual." },
    };

    static_assert (std::size (tuneParams) == TuneProcessor::count);

    int pitchClassOf (int key) noexcept   { return ((key % 12) + 12) % 12; }
}

const FxInfo& TuneProcessor::staticInfo()
{
    static const FxInfo info { "tune", "Afinação", "Afin.", "Voz",
                               "Corrige a afinação da voz para as notas da escala, do natural ao efeito robô, com a formante "
                               "no lugar. A nota também pode vir de uma trilha guia (a melodia do piano roll).",
                               tuneParams, true };
    return info;
}

int TuneProcessor::scaleMask (int scaleIndex) noexcept
{
    static const int degrees[7][8] = {
        { 0, 2, 3, 5, 7, 8, 10, -1 },   // menor
        { 0, 2, 4, 5, 7, 9, 11, -1 },   // maior
        { 0, 2, 3, 5, 7, 8, 11, -1 },   // menor harmônica
        { 0, 1, 3, 5, 7, 8, 10, -1 },   // frígio
        { 0, 2, 3, 5, 7, 9, 10, -1 },   // dórico
        { 0, 3, 5, 7, 10, -1, -1, -1 }, // pentatônica menor
        { -1, -1, -1, -1, -1, -1, -1, -1 },
    };

    if (scaleIndex < 0 || scaleIndex >= 6)
        return 0xFFF;   // cromática

    int mask = 0;

    for (int d : degrees[scaleIndex])
        if (d >= 0)
            mask |= 1 << d;

    return mask;
}

int TuneProcessor::nearestInScale (float midi, int keyRoot, int mask) noexcept
{
    const int centre = (int) std::lround (midi);
    int best = centre;
    float bestDist = 1.0e9f;

    for (int k = centre - 6; k <= centre + 6; ++k)
    {
        if (((mask >> pitchClassOf (k - keyRoot)) & 1) == 0)
            continue;

        const float d = std::abs (midi - (float) k);

        if (d < bestDist)
        {
            bestDist = d;
            best = k;
        }
    }

    return best;
}

int TuneProcessor::activeNotes (const float* v) noexcept
{
    int mask = 0;

    if ((int) v[scale] == customScale)
    {
        for (int n = 0; n < 12; ++n)
            if (v[noteC + n] >= 0.5f)
                mask |= 1 << n;

        return mask;
    }

    // A máscara da escala é relativa à tônica: gira para as classes absolutas.
    const int relative = scaleMask ((int) v[scale]), root = pitchClassOf ((int) v[key]);

    for (int d = 0; d < 12; ++d)
        if ((relative >> d) & 1)
            mask |= 1 << pitchClassOf (root + d);

    return mask;
}

float TuneProcessor::gridOffset (const float* v) noexcept
{
    return 12.0f * std::log2 (std::max (1.0f, v[reference]) / 440.0f) + v[fine] * 0.01f;
}

//==============================================================================
namespace
{
    constexpr int controlBlock = 32;   // a correção vai para o PSOLA a cada 32 amostras (0,7 ms)
}

void TuneProcessor::prepare (double rate, int)
{
    sampleRate = rate;

    for (int i = 0; i < count; ++i)
        values[(size_t) i] = tuneParams[i].def;

    // Grão de até 2 períodos: a formante desce até -12 st sem cortar o grão.
    core.prepare (rate, 2, 2.0f);
    guideYin.prepare (rate);
    guideYin.setRange (60.0, 2000.0);   // a oitava da guia não importa: até o Si 6; abaixo de 60 Hz o YIN não mede
    halfMaxPeriod = (float) (0.5 * rate / 60.0);
    releaseCoeff = onePoleCoeff (60.0f, rate);
    formantFactor.prepare (rate, 30.0f);
    wet.prepare (rate, 20.0f);
    harmonyShift.prepare (rate, 15.0f);
    harmonyGain.prepare (rate, 20.0f);
    harmonyGate.prepare (rate, 20.0f);
    snapToTargets();
    reset();
}

void TuneProcessor::reset() noexcept
{
    core.reset();
    guideYin.reset();
    guiding = guideListening = false;
    harmonyTarget = -1;
    harmonyShift.snap (0.0f);
    harmonyGate.snap (0.0f);
    history.fill ({});
    historyPos = 0;
    lastTarget = -1;
    detectedMidi = targetMidi = -1.0f;
    voiced = false;
    correctionTarget = correction = 0.0f;
    heldSamples = 0;

    setMeter (detectedNote, -1.0f);
    setMeter (targetNote, -1.0f);
    setMeter (correctionCents, 0.0f);
    setMeter (voicedMeter, 0.0f);
    setMeter (guideMeter, 0.0f);
    setMeter (harmonyNote, -1.0f);
}

void TuneProcessor::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= count)
        return;

    values[(size_t) index] = value;

    if (index == range)
    {
        const int r = (int) value;
        core.setRange (r == 2 ? 120.0 : 60.0, r == 1 ? 500.0 : 1000.0);
    }
    else if (index == formant)
    {
        formantFactor.setTarget (std::exp2 (value / 12.0f));
    }

    notesMask = activeNotes (values.data());
    updateNeutral();
}

void TuneProcessor::updateNeutral() noexcept
{
    // Sem nada a fazer, a saída é a entrada atrasada, exata (o PSOLA continua rodando por baixo).
    const bool correcting = values[amount] > 0.05f && (notesMask != 0 || values[guide] >= 0.5f);
    const bool shaping = std::abs (values[formant]) > 0.005f;
    wet.setTarget (correcting || shaping ? values[mix] * 0.01f : 0.0f);
    harmonyGain.setTarget ((int) values[harmony] > 0 ? values[harmonyLevel] * 0.01f : 0.0f);
    core.setEnabled (1, (int) values[harmony] > 0);   // sem harmonia, a 2ª leitura do PSOLA não roda
}

void TuneProcessor::snapToTargets() noexcept
{
    for (int i = 0; i < count; ++i)
        setParam (i, values[(size_t) i]);

    currentAttack = values[speed] < 0.5f ? 0.0f : onePoleCoeff (values[speed], sampleRate);
    formantFactor.snap (formantFactor.getTarget());
    wet.snap (wet.getTarget());
    harmonyGain.snap (harmonyGain.getTarget());
}

//==============================================================================
const TuneProcessor::Measure& TuneProcessor::measureFor (bool guideTrack) const noexcept
{
    // O YIN olha a janela que termina agora (o meio dela fica ~1 período de 60 Hz mais o período medido atrás);
    // o grão que sai agora está 1,5 período de 60 Hz atrás. A medida usada é a de (0,5 período de 60 Hz − período) antes.
    const int size = (int) history.size(), newest = (historyPos - 1 + size) % size;
    const auto& latest = history[(size_t) newest];
    const bool on = guideTrack ? latest.guideVoiced : latest.voiced;
    const float period = guideTrack ? latest.guidePeriod : latest.period;
    const int lag = on ? std::clamp ((int) std::lround ((halfMaxPeriod - period) / (float) controlBlock), 0, size - 1) : 0;
    return history[(size_t) ((newest - lag + size) % size)];
}

void TuneProcessor::analyse (int elapsed) noexcept
{
    history[(size_t) historyPos] = { core.periodSamples(), guideYin.periodSamples(), core.voiced(), guideYin.voiced() };
    historyPos = (historyPos + 1) % (int) history.size();

    const auto& m = measureFor (false);
    const auto& g = measureFor (true);
    guiding = false;

    if (! m.voiced || m.period <= 0.0f)
    {
        voiced = false;
        harmonyTarget = -1;
        detectedMidi = targetMidi = -1.0f;
        correctionTarget = 0.0f;
        heldSamples = 0;
        return;
    }

    // A voz medida na grade de notas (Lá de referência e ajuste fino): a nota alvo é inteira nessa grade.
    const float offset = gridOffset (values.data());
    voiced = true;
    detectedMidi = 69.0f + 12.0f * std::log2 ((float) (sampleRate / m.period) / 440.0f) - offset;
    int target = -1;

    if (values[guide] >= 0.5f && g.guideVoiced && g.guidePeriod > 0.0f)
    {
        // A guia está tocando: a nota dela (fora da escala também), na oitava mais perto da voz.
        const float guideMidi = 69.0f + 12.0f * std::log2 ((float) (sampleRate / g.guidePeriod) / 440.0f) - offset;
        const int nearest = (int) std::lround (detectedMidi), guideClass = pitchClassOf ((int) std::lround (guideMidi));
        target = nearest + pitchClassOf (guideClass - nearest + 6) - 6;   // de -6 a +5 semitons da voz

        // Perto do trítono, a oitava não fica pulando com o vibrato: mantém a nota anterior da mesma classe.
        if (lastTarget >= 0 && pitchClassOf (lastTarget) == guideClass && std::abs ((float) lastTarget - detectedMidi) <= 6.5f)
            target = lastTarget;

        guiding = true;
    }
    else if (notesMask != 0)
    {
        target = nearestInScale (detectedMidi, 0, notesMask);

        // Histerese: fica na nota anterior enquanto a voz está a menos de 0,65 semitom dela.
        if (lastTarget >= 0 && target != lastTarget && ((notesMask >> pitchClassOf (lastTarget)) & 1) != 0
            && std::abs (detectedMidi - (float) lastTarget) < 0.65f)
            target = lastTarget;
    }

    if (target < 0)
    {
        // Nenhuma nota ligada: mostra a voz, mas não corrige.
        harmonyTarget = -1;
        targetMidi = -1.0f;
        lastTarget = -1;
        correctionTarget = 0.0f;
        heldSamples = 0;
        return;
    }

    // Humanizar: quanto mais a nota se sustenta, mais lenta a correção dentro dela.
    heldSamples = target == lastTarget ? std::min (heldSamples + elapsed, (int) sampleRate) : 0;
    float ms = values[speed];
    const float human = values[humanize] * 0.01f;

    if (human > 0.0f)
    {
        const float held = (float) heldSamples / (float) sampleRate;
        ms += human * 350.0f * std::clamp ((held - 0.12f) / 0.2f, 0.0f, 1.0f);
    }

    currentAttack = ms < 0.5f ? 0.0f : onePoleCoeff (ms, sampleRate);

    lastTarget = target;
    targetMidi = (float) target;
    const float wanted = std::clamp ((targetMidi - detectedMidi) * values[amount] * 0.01f, -6.0f, 6.0f);

    // Menos de 2 cents é inaudível: não corrige.
    correctionTarget = std::abs (wanted) < 0.02f ? 0.0f : wanted;

    // Harmonia: o intervalo da escala a partir da nota alvo (cromática ou sem notas: terça maior e quinta justa).
    const int choice = (int) values[harmony];

    if (choice > 0)
    {
        const int degrees[] = { 0, 2, 4, -2, 0 };
        // Com menos de 3 notas não há terça na escala: vale a terça maior e a quinta justa.
        const int mask = std::popcount ((unsigned) notesMask) >= 3 ? notesMask : 0xFFF;
        const int newHarmony = choice == 4 ? target - 12 : HarmonyProcessor::scaleStep (target, degrees[choice], 0, mask);
        const bool phraseStart = harmonyTarget < 0;
        harmonyTarget = newHarmony;
        harmonyShift.setTarget ((float) (newHarmony - target));

        if (phraseStart)
            harmonyShift.snap (harmonyShift.getTarget());   // começo de frase: a voz nova entra já na nota
    }
    else
    {
        harmonyTarget = -1;
    }
}

void TuneProcessor::process (AudioBlock& block) noexcept
{
    const DenormalGuard guard;
    const bool listening = values[guide] >= 0.5f && block.sideLeft != nullptr;

    if (! listening && guideListening)
        guideYin.reset();   // ao voltar, a guia começa sem a medida velha

    guideListening = listening;

    for (int start = 0; start < block.numSamples; start += controlBlock)
    {
        const int n = std::min (controlBlock, block.numSamples - start);
        analyse (n);
        harmonyGate.setTarget (harmonyTarget >= 0 ? 1.0f : 0.0f);   // sem nota alvo, a harmonia cala

        // Correção em semitons, amostra a amostra: a velocidade vale ao corrigir; sem voz, volta a zero em ~60 ms.
        const float coeff = voiced ? currentAttack : releaseCoeff;

        for (int i = 0; i < n; ++i)
            correction = correctionTarget + (correction - correctionTarget) * coeff;

        if (std::abs (correction - correctionTarget) < 1.0e-5f)
            correction = correctionTarget;

        const float f = formantFactor.skip (n);
        core.setShift (0, std::exp2 (correction * (1.0f / 12.0f)), f, false);
        core.setShift (1, std::exp2 ((correction + harmonyShift.skip (n)) * (1.0f / 12.0f)), f, true);

        for (int i = start; i < start + n; ++i)
        {
            // Um NaN ou infinito na entrada vira silêncio antes de entrar no PSOLA e no detector.
            float l = block.left[i], r = block.right[i];

            if (! std::isfinite (l)) l = 0.0f;
            if (! std::isfinite (r)) r = 0.0f;

            // A guia (sidechain), em mono; o detector dela só roda com "Seguir a guia" ligado e uma trilha no sidechain.
            if (listening)
            {
                const float sl = block.sideLeft[i], sr = block.sideRight != nullptr ? block.sideRight[i] : sl;
                const float side = 0.5f * (sl + sr);
                guideYin.push (std::isfinite (side) ? side : 0.0f);
            }

            float wl[PsolaCore::maxReads] {}, wr[PsolaCore::maxReads] {}, dl = 0.0f, dr = 0.0f;
            core.process (l, r, wl, wr, dl, dr);
            const float w = wet.next(), h = harmonyGain.next() * harmonyGate.next();
            block.left[i] = dl + w * (wl[0] - dl) + h * wl[1];
            block.right[i] = dr + w * (wr[0] - dr) + h * wr[1];
        }
    }

    setMeter (detectedNote, detectedMidi);
    setMeter (targetNote, targetMidi);
    setMeter (correctionCents, correction * 100.0f);
    setMeter (voicedMeter, voiced ? 1.0f : 0.0f);
    setMeter (guideMeter, guiding ? 1.0f : 0.0f);
    setMeter (harmonyNote, (float) harmonyTarget);
}

} // namespace whynot::dsp
