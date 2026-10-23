#include "PresetBuilder.h"

// Pads, texturas e efeitos.
// - Pads: unison com fase livre, ataque e soltura longos, deriva e espaço.
// - Texturas: ruído e tabelas mexidas por LFOs lentos ou aleatórios (ambiente, não melodia).
// - FX: subidas, quedas, sirenes e impactos feitos com envelopes e LFOs na afinação.

namespace whynot::synth::presets
{

void addPadsAndTextures (std::vector<Preset>& out)
{
    //==========================================================================
    // Pads
    out.push_back (B ("pad-quente", "Pad Quente", "pad", RNB | LOFI | POP | RAP,
                      "Serras desafinadas e fechadas, entrada lenta: abraço de fundo para acordes.")
                       .va (0, Wave::saw, -6).unison (0, 4, 15, 80).freePhase (0)
                       .va (1, Wave::pulse, -10, 40).tune (1, -1).freePhase (1)
                       .lp (1800, 5, true)
                       .amp (600, 800, 90, 1500).drift (25).reverb (60, 3.0f, 30).build());

    out.push_back (B ("pad-escuro-trap", "Pad Escuro Trap", "pad", TRAP | DRILL,
                      "Tabela de ar fechada que respira devagar, reverb enorme.")
                       .wt (0, ar, 30, -6).unison (0, 3, 12, 80).freePhase (0)
                       .lp (1200, 10, true)
                       .lfo (0, LfoShape::sine, 0.15f, false).mod (Source::lfo1, Dest::cutoff, 10)
                       .amp (800, 1000, 90, 2000).reverb (70, 4.0f, 35).build());

    out.push_back (B ("pad-coral", "Pad Coral", "pad", RNB | POP,
                      "Vozes escuras que trocam de vogal bem devagar.")
                       .wt (0, vozEscura, 30, -6).unison (0, 3, 12, 80).freePhase (0)
                       .lfo (0, LfoShape::sine, 0.12f, false).mod (Source::lfo1, Dest::shape1, 20)
                       .lp (3500, 0, false)
                       .amp (800, 800, 90, 1600).reverb (65, 3.5f, 30).build());

    out.push_back (B ("pad-vidro", "Pad de Vidro", "pad", POP | TRAP,
                      "Tabela de vidro aberta com eco pontuado.")
                       .wt (0, vidro, 40, -6).unison (0, 3, 10, 80).freePhase (0)
                       .lp (5000, 0, false)
                       .amp (400, 800, 85, 1400).reverb (60, 3.0f, 30).delay (d4Dot, 30, 15).build());

    out.push_back (B ("pad-analogico", "Pad Analógico Largo", "pad", POP | RNB,
                      "Seis serras e dois pulsos bem abertos: o pad largo dos sintetizadores.")
                       .va (0, Wave::saw, -6).unison (0, 6, 22, 100).freePhase (0)
                       .va (1, Wave::pulse, -10, 40).unison (1, 2, 12, 100).freePhase (1)
                       .lp (3000, 5, true)
                       .amp (500, 800, 90, 1500).drift (30).width (150).reverb (55, 2.6f, 25).build());

    out.push_back (B ("pad-sombrio-drill", "Pad Sombrio Drill", "pad", DRILL,
                      "Serras fechadas e um pulso com largura mexendo (PWM).")
                       .va (0, Wave::saw, -6).unison (0, 4, 14, 80).freePhase (0)
                       .va (1, Wave::pulse, -8, 50).freePhase (1)
                       .lfo (0, LfoShape::sine, 0.3f, false).mod (Source::lfo1, Dest::shape2, 30)
                       .lp (800, 15, true)
                       .amp (900, 1000, 90, 2000).reverb (70, 4.0f, 35).build());

    out.push_back (B ("pad-lofi-fita", "Pad Lo-fi Fita", "pad", LOFI,
                      "Pad gravado em fita velha: oscila, deriva e satura.")
                       .va (0, Wave::triangle, -4).unison (0, 3, 10, 70).freePhase (0)
                       .va (1, Wave::saw, -12).freePhase (1)
                       .lp (1500, 0, true)
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.4f)
                       .amp (500, 800, 90, 1200).drift (60).sat (tape, 9, 100).eq (40, 1, 0, 1000, -6).build());

    out.push_back (B ("pad-etereo", "Pad Etéreo", "pad", RNB | POP,
                      "Sino e ar com entrada muito lenta, eco e reverb longos.")
                       .wt (0, sino, 50, -6).unison (0, 2, 8, 80).freePhase (0)
                       .wt (1, ar, 60, -8).unison (1, 3, 12, 90).freePhase (1)
                       .lp (7000, 0, false)
                       .amp (1200, 1000, 90, 2500).reverb (80, 5.0f, 40).delay (d4, 35, 18).build());

    out.push_back (B ("pad-pulsante", "Pad Pulsante", "pad", TRAP | POP,
                      "Pad que bombeia em semínimas, como se tivesse sidechain do kick.")
                       .va (0, Wave::saw, -6).unison (0, 5, 18, 90).freePhase (0)
                       .lp (3000, 5, false)
                       .lfoSync (0, LfoShape::rampUp, quarter, false).mod (Source::lfo1, Dest::volume, 60)
                       .amp (300, 800, 90, 1200).reverb (55, 2.5f, 25).build());

    out.push_back (B ("pad-orgao", "Pad Órgão", "pad", RNB | RAP,
                      "Órgão macio com entrada lenta e tremolo largo.")
                       .wt (0, orgao, 40, -6).unison (0, 2, 6, 60).freePhase (0)
                       .lp (5000, 0, false)
                       .lfo (0, LfoShape::sine, 4.0f, false).mod (Source::lfo1, Dest::volume, 6)
                       .amp (300, 600, 90, 900).reverb (60, 2.8f, 25).build());

    out.push_back (B ("pad-sweep", "Pad Sweep", "pad", POP | TRAP,
                      "O filtro abre ao longo de três segundos em cada acorde.")
                       .va (0, Wave::saw, -6).unison (0, 5, 20, 90).freePhase (0)
                       .lp (300, 25, true).fenv (4.0f, 3000, 2000, 60, 2000)
                       .amp (400, 1000, 90, 1800).reverb (60, 3.0f, 28).build());

    out.push_back (B ("pad-noturno", "Pad Noturno", "pad", PHONK | TRAP,
                      "Distorção de fase que muda devagar, eco pontuado.")
                       .wt (0, fase, 40, -6).unison (0, 3, 12, 80).freePhase (0)
                       .lfo (0, LfoShape::sine, 0.2f, false).mod (Source::lfo1, Dest::shape1, 25)
                       .lp (2500, 5, false)
                       .amp (700, 1000, 90, 1800).reverb (70, 4.0f, 30).delay (d4Dot, 35, 18).build());

    out.push_back (B ("pad-cristal", "Pad Cristal", "pad", POP | RNB,
                      "FM suave e serras ao fundo: brilho limpo que não cansa.")
                       .fm (0, FmAlgo::twoOp, 2, 15, 2, 0, 3, 0, 3000, 60, 0, -6).unison (0, 2, 8, 70).freePhase (0)
                       .va (1, Wave::saw, -14).unison (1, 3, 14, 90).freePhase (1)
                       .lp (4000, 0, false)
                       .amp (500, 800, 90, 1500).reverb (65, 3.2f, 30).build());

    out.push_back (B ("pad-funk-melodico", "Pad Funk Melódico", "pad", FUNK | POP,
                      "Pulso e serra com entrada rápida, bem largo: acorde que acompanha o batidão.")
                       .va (0, Wave::pulse, -6, 35).unison (0, 3, 12, 80).freePhase (0)
                       .va (1, Wave::saw, -10).freePhase (1)
                       .lp (2200, 5, false)
                       .amp (80, 500, 85, 600).width (150).reverb (45, 2.0f, 20).build());

    //==========================================================================
    // Texturas
    out.push_back (B ("textura-vinil", "Textura Vinil", "texture", LOFI | RAP,
                      "Chiado escuro com estalos aleatórios, como disco velho.")
                       .noise (0, 30, -10, 80).noise (1, 92, -18, 100)
                       .lfo (0, LfoShape::stepRandom, 28, false).mod (Source::lfo1, Dest::level2, 100)
                       .noFilter().amp (50, 100, 100, 500).build());

    out.push_back (B ("textura-chuva", "Textura Chuva", "texture", LOFI | RNB,
                      "Ruído em faixa que oscila ao acaso: chuva na janela.")
                       .noise (0, 60, -8, 100)
                       .filter (FilterMode::bandpass, 3000, 10, false)
                       .lfo (0, LfoShape::smoothRandom, 3, false).mod (Source::lfo1, Dest::cutoff, 8)
                       .lfo (1, LfoShape::stepRandom, 18, false).mod (Source::lfo2, Dest::volume, 30)
                       .amp (800, 500, 100, 1500).reverb (60, 2.5f, 30).build());

    out.push_back (B ("textura-drone", "Textura Drone Escuro", "texture", DRILL | TRAP,
                      "Serras graves fechadas e ruído, mexendo bem devagar.")
                       .va (0, Wave::saw, -6).unison (0, 5, 25, 90).freePhase (0).tune (0, -1)
                       .noise (1, 20, -20, 80)
                       .lp (300, 20, true)
                       .lfo (0, LfoShape::sine, 0.07f, false).mod (Source::lfo1, Dest::cutoff, 12)
                       .amp (2000, 1000, 100, 3000).reverb (75, 5.0f, 35).build());

    out.push_back (B ("textura-granulada", "Textura Granulada", "texture", PHONK | LOFI,
                      "Tabela digital pulando de posição em 1/16, com eco.")
                       .wt (0, digital, 50, -6).unison (0, 2, 10, 80).freePhase (0)
                       .lfoSync (0, LfoShape::stepRandom, sixteenth, false).mod (Source::lfo1, Dest::shape1, 60)
                       .lp (3000, 5, false)
                       .amp (300, 500, 90, 1000).delay (d16, 40, 25).reverb (60, 3.0f, 25).build());

    out.push_back (B ("textura-espacial", "Textura Espacial", "texture", TRAP | POP,
                      "Fase que muda e passeia entre os lados, eco e reverb gigantes.")
                       .wt (0, fase, 60, -6).unison (0, 4, 20, 100).freePhase (0)
                       .lfo (0, LfoShape::sine, 0.1f, false).mod (Source::lfo1, Dest::shape1, 40)
                       .lfo (1, LfoShape::sine, 0.17f, false).mod (Source::lfo2, Dest::pan, 30)
                       .noFilter()
                       .amp (1500, 1000, 90, 3000).delay (d4Dot, 45, 25).reverb (80, 6.0f, 45).build());

    out.push_back (B ("textura-respirando", "Textura Respirando", "texture", RNB | LOFI,
                      "Ar e um seno que crescem e somem, como respiração.")
                       .noise (0, 40, -12, 80).va (1, Wave::sine, -14)
                       .lp (2000, 0, false)
                       .lfo (0, LfoShape::sine, 0.25f, false).mod (Source::lfo1, Dest::volume, 70)
                       .amp (1000, 500, 100, 2000).reverb (60, 3.0f, 30).build());

    out.push_back (B ("textura-metalica", "Textura Metálica", "texture", PHONK | DRILL,
                      "FM inarmônico (1,73) com realimentação, filtro passeando.")
                       .fm (0, FmAlgo::twoOp, 1.73f, 60, 2, 0, 3, 0, 4000, 50, 15, -6).unison (0, 2, 10, 80).freePhase (0)
                       .lp (3500, 20, false)
                       .lfo (0, LfoShape::sine, 0.12f, false).mod (Source::lfo1, Dest::cutoff, 15)
                       .amp (1000, 1500, 80, 2500).reverb (75, 5.0f, 35).build());

    out.push_back (B ("textura-coral-distante", "Textura Coral Distante", "texture", RNB | LOFI,
                      "Vozes longe, vogais mudando, quase só reverb.")
                       .wt (0, vogais, 20, -6).unison (0, 4, 12, 90).freePhase (0)
                       .lfo (0, LfoShape::sine, 0.08f, false).mod (Source::lfo1, Dest::shape1, 35)
                       .lp (2500, 0, false)
                       .amp (1500, 1000, 90, 3000).reverb (85, 6.0f, 55).build());

    //==========================================================================
    // FX
    out.push_back (B ("fx-riser", "Riser de Ruído", "fx", TRAP | POP | DRILL,
                      "Ruído e serras subindo de filtro e afinação por quatro segundos.")
                       .noise (0, 60, -6, 100).va (1, Wave::saw, -14).unison (1, 3, 20, 90)
                       .lp (200, 15, true).fenv (6.0f, 4000, 1000, 100, 500)
                       .modEnv (4000, 1000, 100, 500, 40).mod (Source::env3, Dest::pitch, 50)
                       .amp (3000, 100, 100, 500).reverb (60, 3.0f, 30).build());

    out.push_back (B ("fx-queda", "Queda", "fx", TRAP | DRILL,
                      "Começa duas oitavas acima e despenca em dois segundos (downlifter).")
                       .va (0, Wave::saw, -6).unison (0, 3, 20, 80).noise (1, 50, -14, 100)
                       .lp (6000, 10, true).fenv (-3.0f, 1, 2000, 0, 500)
                       .modEnv (0.5f, 2000, 0, 500, 40).mod (Source::env3, Dest::pitch, 100)
                       .amp (1, 2500, 0, 500).reverb (60, 3.0f, 30).build());

    out.push_back (B ("fx-sirene-funk", "Sirene Funk", "fx", FUNK | PHONK,
                      "Quadrada subindo e descendo uma oitava: a sirene do baile.")
                       .va (0, Wave::pulse, -6, 50).lp (5000, 0, false)
                       .lfo (0, LfoShape::triangle, 1.2f, true).mod (Source::lfo1, Dest::pitch, 50)
                       .amp (5, 100, 100, 200).mono (0).sat (hard, 6, 100).build());

    out.push_back (B ("fx-laser", "Laser", "fx", PHONK | TRAP,
                      "Afinação despencando quatro oitavas em 150 ms.")
                       .va (0, Wave::sine, -4).va (1, Wave::pulse, -14, 30).noFilter()
                       .modEnv (0.5f, 150, 0, 150, 70).mod (Source::env3, Dest::pitch, 100).mod (Source::env3, Dest::pitch, 100)
                       .amp (0.5f, 250, 0, 100).delay (d16, 40, 25).build());

    out.push_back (B ("fx-impacto", "Impacto Sub", "fx", TRAP | DRILL,
                      "Golpe de seno que cai duas oitavas, com ruído escuro e sala grande.")
                       .va (0, Wave::sine, 0).noise (1, 30, -10, 80).noFilter()
                       .pitchDrop (24, 600, 60)
                       .amp (0.5f, 3000, 0, 800).mono (0).sat (tape, 6, 100).reverb (70, 4.0f, 30).build());

    out.push_back (B ("fx-vento", "Vento", "fx", LOFI | TRAP,
                      "Ruído em faixa estreita, filtro e volume mudando ao acaso.")
                       .noise (0, 50, -6, 100)
                       .filter (FilterMode::bandpass, 800, 40, false)
                       .lfo (0, LfoShape::smoothRandom, 0.3f, false).mod (Source::lfo1, Dest::cutoff, 25)
                       .lfo (1, LfoShape::sine, 0.13f, false).mod (Source::lfo2, Dest::volume, 40)
                       .amp (1500, 500, 100, 2000).reverb (60, 3.0f, 30).build());

    out.push_back (B ("fx-zap", "Zap Digital", "fx", POP | PHONK,
                      "Degrau digital que cai de afinação e de forma num instante.")
                       .wt (0, digital, 80, -6).noFilter()
                       .modEnv (0.5f, 80, 0, 80, 70).mod (Source::env3, Dest::pitch, 100).mod (Source::env3, Dest::shape1, -60)
                       .amp (0.5f, 200, 0, 100).build());

    out.push_back (B ("fx-sirene-alarme", "Sirene Alarme", "fx", DRILL | FUNK,
                      "Dois tons alternando em semínimas.")
                       .va (0, Wave::pulse, -6, 50).lp (4000, 0, false)
                       .lfoSync (0, LfoShape::square, quarter, true).mod (Source::lfo1, Dest::pitch, 10)
                       .amp (5, 100, 100, 200).reverb (40, 1.5f, 15).build());
}

} // namespace whynot::synth::presets
