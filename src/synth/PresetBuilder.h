#pragma once

// Monta presets em poucas linhas: cada método liga uma parte do motor com valores na unidade
// que aparece na tela (dB, Hz, ms, %). Uso interno dos arquivos de presets.

#include "SynthPresets.h"

namespace whynot::synth::presets
{

// Gêneros (bits) para escrever as listas curtas.
constexpr std::uint32_t TRAP = 1, FUNK = 2, RAP = 4, POP = 8, DRILL = 16, RNB = 32, PHONK = 64, LOFI = 128;
constexpr std::uint32_t ALL = 255;

class B
{
public:
    B (const char* id, const char* name, const char* category, std::uint32_t genres, const char* description)
    {
        p.id = id;
        p.name = name;
        p.category = category;
        p.genres = genres;
        p.description = description;
        p.values = defaultValues();
        set (P::layer (0, lOn), 0);   // cada preset liga as camadas que usa
    }

    Preset build() const   { return p; }

    B& set (int index, float value)   { p.values[(size_t) index] = param (index).clamp (value); return *this; }

    //--------------------------------------------------------------------------
    // Camadas (l = 0..3)
    B& va (int l, Wave w, float levelDb = -6.0f, float shape = 50.0f)
    {
        on (l, LayerType::analog, levelDb);
        set (L (l, lWave), (float) w);
        return set (L (l, lShape), shape);
    }

    B& wt (int l, int table, float position, float levelDb = -6.0f)
    {
        on (l, LayerType::wavetable, levelDb);
        set (L (l, lTable), (float) table);
        return set (L (l, lShape), position);
    }

    /** FM: razões e índices (0..100%) de B, C, D; decay e sustain do brilho; realimentação. */
    B& fm (int l, FmAlgo algo, float rB, float iB, float rC = 2, float iC = 0, float rD = 3, float iD = 0,
           float decayMs = 600, float sustainPct = 50, float feedbackPct = 0, float levelDb = -6.0f)
    {
        on (l, LayerType::fm, levelDb);
        set (L (l, lFmAlgo), (float) algo);
        set (L (l, lFmRatio2), rB); set (L (l, lFmAmt2), iB);
        set (L (l, lFmRatio3), rC); set (L (l, lFmAmt3), iC);
        set (L (l, lFmRatio4), rD); set (L (l, lFmAmt4), iD);
        set (L (l, lFmDecay), decayMs);
        set (L (l, lFmSustain), sustainPct);
        set (L (l, lFmFeedback), feedbackPct);
        return set (L (l, lShape), 50);
    }

    B& noise (int l, float colour, float levelDb = -18.0f, float stereo = 60.0f)
    {
        on (l, LayerType::noise, levelDb);
        set (L (l, lShape), colour);
        return set (L (l, lWidth), stereo);
    }

    B& shape (int l, float pct)                  { return set (L (l, lShape), pct); }
    B& level (int l, float db)                   { return set (L (l, lLevel), db); }
    B& pan (int l, float pct)                    { return set (L (l, lPan), pct); }
    B& tune (int l, int octave, int semitone = 0, float fineCents = 0.0f)
    {
        set (L (l, lOct), (float) (octave + 3));
        set (L (l, lSemi), (float) (semitone + 12));
        return set (L (l, lFine), fineCents);
    }

    B& unison (int l, int voices, float detuneCents, float widthPct = 80.0f)
    {
        set (L (l, lVoices), (float) (voices - 1));
        set (L (l, lDetune), detuneCents);
        return set (L (l, lWidth), widthPct);
    }

    B& freePhase (int l, float pct = 100.0f)     { return set (L (l, lPhase), pct); }
    B& bypassFilter (int l)                      { return set (L (l, lFilter), 0); }

    //--------------------------------------------------------------------------
    B& sub (float levelDb, int octaves = 1, SubWave w = SubWave::sine)
    {
        set (P::subOn, 1);
        set (P::subLevel, levelDb);
        set (P::subOct, octaves >= 2 ? 1.0f : 0.0f);
        return set (P::subWave, (float) w);
    }

    B& filter (FilterMode m, float cutoffHz, float resoPct = 10.0f, bool steep = true, float drivePct = 0.0f)
    {
        set (P::fltOn, 1);
        set (P::fltMode, (float) m);
        set (P::fltCutoff, cutoffHz);
        set (P::fltReso, resoPct);
        set (P::fltSlope, steep ? 1.0f : 0.0f);
        return set (P::fltDrive, drivePct);
    }

    B& lp (float cutoffHz, float resoPct = 10.0f, bool steep = true, float drivePct = 0.0f)   { return filter (FilterMode::lowpass, cutoffHz, resoPct, steep, drivePct); }
    B& noFilter()                                { return set (P::fltOn, 0); }
    B& keyTrack (float pct)                      { return set (P::fltKey, pct); }
    B& velToCutoff (float pct)                   { return set (P::fltVel, pct); }

    /** Envelope do filtro: quantas oitavas abre e o ADSR dele. */
    B& fenv (float octaves, float a, float d, float s, float r, float curve = 50.0f)
    {
        set (P::fltEnv, octaves);
        return env (1, a, d, s, r, curve);
    }

    B& amp (float a, float d, float s, float r, float curve = 50.0f)   { return env (0, a, d, s, r, curve); }
    B& modEnv (float a, float d, float s, float r, float curve = 50.0f) { return env (2, a, d, s, r, curve); }

    B& env (int e, float a, float d, float s, float r, float curve)
    {
        set (P::env (e, eAttack), a);
        set (P::env (e, eDecay), d);
        set (P::env (e, eSustain), s);
        set (P::env (e, eRelease), r);
        return set (P::env (e, eCurve), curve);
    }

    //--------------------------------------------------------------------------
    B& lfo (int l, LfoShape shape, float hz, bool retrig = true, float fadeMs = 0.0f)
    {
        set (P::lfo (l, lfoShape), (float) shape);
        set (P::lfo (l, synth::lfoSync), 0);
        set (P::lfo (l, lfoRate), hz);
        set (P::lfo (l, lfoRetrig), retrig ? 1.0f : 0.0f);
        return set (P::lfo (l, lfoFade), fadeMs);
    }

    /** LFO no BPM. division = posição na lista ("1/4" = 6, "1/8" = 9, "1/16" = 11...). */
    B& lfoSync (int l, LfoShape shape, int division, bool retrig = true)
    {
        set (P::lfo (l, lfoShape), (float) shape);
        set (P::lfo (l, synth::lfoSync), 1);
        set (P::lfo (l, lfoDiv), (float) division);
        return set (P::lfo (l, lfoRetrig), retrig ? 1.0f : 0.0f);
    }

    /** Uma rota na matriz (no próximo espaço livre). amount em % (-100..100). */
    B& mod (Source s, Dest d, float amountPct)
    {
        for (int i = 0; i < numSlots; ++i)
        {
            if (p.values[(size_t) P::slot (i, mSrc)] == 0.0f)
            {
                set (P::slot (i, mSrc), (float) s);
                set (P::slot (i, mDst), (float) d);
                return set (P::slot (i, mAmt), amountPct);
            }
        }

        return *this;
    }

    /** Queda de afinação no ataque (o "tum" do 808 e dos kicks): semitons e tempo. Usa a Env mod. */
    B& pitchDrop (float semitones, float decayMs, float curve = 80.0f)
    {
        modEnv (0.5f, decayMs, 0, decayMs, curve);
        return mod (Source::env3, Dest::pitch, semitones / 24.0f * 100.0f);
    }

    /** Vibrato pelo LFO 2: profundidade em centésimos, taxa e tempo para entrar. */
    B& vibrato (float cents, float hz = 5.5f, float fadeMs = 400.0f)
    {
        lfo (1, LfoShape::sine, hz, true, fadeMs);
        return mod (Source::lfo2, Dest::pitch, cents / 2400.0f * 100.0f);
    }

    //--------------------------------------------------------------------------
    B& mono (float glideMs = 80.0f, bool legato = true, bool always = false)
    {
        set (P::mono, 1);
        set (P::glide, glideMs);
        set (P::legato, legato ? 1.0f : 0.0f);
        return set (P::porta, always ? 1.0f : 0.0f);
    }

    B& poly (int voices)                         { return set (P::poly, (float) (voices - 1)); }
    B& drift (float pct)                         { return set (P::drift, pct); }
    B& velAmp (float pct)                        { return set (P::velAmp, pct); }
    B& volume (float db)                         { return set (P::volume, db); }

    B& sat (int mode, float driveDb, float mixPct = 100.0f)
    {
        set (P::satOn, 1);
        set (P::satMode, (float) mode);
        set (P::satDrive, driveDb);
        return set (P::satMix, mixPct);
    }

    B& eq (float hpHz, float lowDb, float midDb, float midHz, float highDb)
    {
        set (P::eqOn, 1);
        set (P::eqHp, hpHz);
        set (P::eqLow, lowDb);
        set (P::eqMid, midDb);
        set (P::eqMidFreq, midHz);
        return set (P::eqHigh, highDb);
    }

    B& width (float pct)                         { return set (P::width, pct); }

    /** Delay no BPM: divisão na lista do delay ("1/8" = 5, "1/8." = 6, "1/4" = 8...). */
    B& delay (int division, float feedbackPct, float mixPct)
    {
        set (P::dlyOn, 1);
        set (P::dlyDiv, (float) division);
        set (P::dlyFb, feedbackPct);
        return set (P::dlyMix, mixPct);
    }

    B& reverb (float sizePct, float decaySeconds, float mixPct)
    {
        set (P::revOn, 1);
        set (P::revSize, sizePct);
        set (P::revDecay, decaySeconds);
        return set (P::revMix, mixPct);
    }

private:
    static int L (int l, int param)   { return P::layer (l, param); }

    void on (int l, LayerType t, float levelDb)
    {
        set (L (l, lOn), 1);
        set (L (l, lType), (float) t);
        set (L (l, lLevel), levelDb);
    }

    Preset p;
};

// Tabelas (posição na lista de Wavetables::names()).
enum Table { classica, pulso, sync, vogais, vozEscura, orgao, dobra, saturada, vidro, sino, varredura, fase, ar, digital, pente };

// Divisões dos LFOs (SynthParams.cpp) e do delay (src/dsp/Space.cpp).
enum LfoDiv { bars4, bars2, bar1, half, halfT, quarterDot, quarter, quarterT, eighthDot, eighth, eighthT, sixteenth, sixteenthT, thirtySecond };
enum DelayDiv { d32, d16T, d16, d16Dot, d8T, d8, d8Dot, d4T, d4, d4Dot, d2, dBar };

// Modos do saturador (src/dsp/Color.h).
enum SatMode { tape, tube, soft, hard, fuzz };

// Cada arquivo de presets acrescenta os seus.
void add808AndBass (std::vector<Preset>&);
void addLeadsAndPlucks (std::vector<Preset>&);
void addKeysAndOrchestral (std::vector<Preset>&);
void addPadsAndTextures (std::vector<Preset>&);
void addSecondWave (std::vector<Preset>&);
void addGenreWave (std::vector<Preset>&);
void addFinalWave (std::vector<Preset>&);
void add3Osc (std::vector<Preset>&);   // os timbres da página "3 OSC" (26/10)

} // namespace whynot::synth::presets
