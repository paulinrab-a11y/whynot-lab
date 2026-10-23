#include "PresetBuilder.h"

// Leads, plucks e arpejos/sequências.
// - Leads são mono com glide (os slides do piano roll deslizam) e vibrato que entra devagar.
// - Plucks: o filtro abre e fecha rápido; o volume cai até zero (a nota termina sozinha).
// - Arp/Seq: timbres curtos para notas rápidas, ou portões no tempo (LFO no BPM no volume).

namespace whynot::synth::presets
{

void addLeadsAndPlucks (std::vector<Preset>& out)
{
    //==========================================================================
    // Leads
    out.push_back (B ("lead-serra-trap", "Lead Serra Trap", "lead", TRAP | DRILL,
                      "Três serras juntas, vibrato discreto e eco pontuado: o lead clássico de trap.")
                       .va (0, Wave::saw, -6).unison (0, 3, 12, 60)
                       .lp (4500, 10, false).keyTrack (30)
                       .amp (3, 400, 90, 250).mono (60).vibrato (15, 5.5f, 350)
                       .delay (d8Dot, 25, 18).reverb (40, 1.6f, 15).build());

    out.push_back (B ("lead-flauta", "Lead Flauta Synth", "lead", TRAP | RNB | DRILL,
                      "Triângulo com sopro de ruído e vibrato: a flauta sintética das melodias de trap.")
                       .va (0, Wave::triangle, -4).noise (1, 72, -26, 30)
                       .lp (3200, 0, false)
                       .amp (25, 300, 85, 220).mono (45).vibrato (22, 5.2f, 300)
                       .reverb (45, 2.2f, 20).build());

    out.push_back (B ("lead-quadrado-retro", "Lead Quadrado Retrô", "lead", POP | FUNK,
                      "Quadrada com vibrato rápido, som de videogame antigo.")
                       .va (0, Wave::pulse, -6, 50).lp (6000, 5, false)
                       .amp (2, 200, 90, 120).mono (30).vibrato (12, 6.0f, 250)
                       .delay (d8, 20, 15).build());

    out.push_back (B ("lead-supersaw", "Lead Supersaw", "lead", POP | TRAP,
                      "Sete serras abertas mais uma oitava acima: parede larga para refrão.")
                       .va (0, Wave::saw, -6).unison (0, 7, 28, 100).freePhase (0)
                       .va (1, Wave::saw, -12).tune (1, 1).unison (1, 5, 22, 100).freePhase (1)
                       .lp (9000, 5, false)
                       .amp (5, 500, 85, 400).poly (12)
                       .reverb (55, 2.5f, 22).delay (d8Dot, 30, 15).build());

    out.push_back (B ("lead-assobio", "Lead Assobio", "lead", FUNK | TRAP | PHONK,
                      "Seno agudo com vibrato largo e um fio de ar: o assobio do funk.")
                       .va (0, Wave::sine, -4).tune (0, 1).noise (1, 78, -32, 20)
                       .lp (6000, 0, false)
                       .amp (15, 200, 90, 200).mono (90).vibrato (30, 6.0f, 150)
                       .reverb (35, 1.4f, 15).build());

    out.push_back (B ("lead-sync", "Lead Sync Agressivo", "lead", DRILL | TRAP,
                      "Tabela Sync varrida pelo envelope a cada nota, no clipper.")
                       .wt (0, sync, 20, -6).modEnv (1, 450, 20, 200).mod (Source::env3, Dest::shape1, 60)
                       .lp (7000, 5, false)
                       .amp (1, 300, 85, 150).mono (50).sat (hard, 10, 100).build());

    out.push_back (B ("lead-vocal", "Lead Vocal", "lead", RNB | POP,
                      "Vogais andando devagar e vibrato: um lead que parece cantar.")
                       .wt (0, vogais, 30, -6).unison (0, 2, 8, 40)
                       .lfo (0, LfoShape::sine, 0.4f, false).mod (Source::lfo1, Dest::shape1, 25)
                       .lp (5000, 5, false)
                       .amp (8, 400, 90, 250).mono (70).vibrato (18, 5.0f, 400)
                       .reverb (45, 2.0f, 20).build());

    out.push_back (B ("lead-sino-mono", "Lead Sino Mono", "lead", TRAP | RNB,
                      "FM com razão 3,5 sobre um seno: lead de sino que desliza.")
                       .fm (0, FmAlgo::twoOp, 3.5f, 35, 2, 0, 3, 0, 900, 25, 0, -6)
                       .va (1, Wave::sine, -10).noFilter()
                       .amp (1, 1200, 40, 300).mono (40)
                       .reverb (45, 1.8f, 20).delay (d8Dot, 25, 15).build());

    out.push_back (B ("lead-pulso-fino", "Lead Pulso Fino", "lead", PHONK | LOFI,
                      "Pulso estreito, fita e deriva: nasal e meio torto.")
                       .va (0, Wave::pulse, -6, 10).lp (2500, 15, false)
                       .amp (2, 300, 90, 150).mono (35).drift (35).vibrato (10, 5.0f, 200)
                       .sat (tape, 8, 100).build());

    out.push_back (B ("lead-lamento", "Lead Lamento Glide", "lead", RAP | FUNK,
                      "Seno com glide em toda nota e vibrato largo: o lead chorado da costa oeste.")
                       .va (0, Wave::sine, -4).va (1, Wave::triangle, -10)
                       .lp (5000, 0, false)
                       .amp (8, 300, 95, 250).mono (120, true, true).vibrato (25, 5.5f, 250)
                       .reverb (30, 1.5f, 12).build());

    out.push_back (B ("lead-phonk", "Lead Phonk Distorcido", "lead", PHONK | DRILL,
                      "Serra e pulso no fuzz com médio na cara.")
                       .va (0, Wave::saw, -8).va (1, Wave::pulse, -8, 40)
                       .lp (3500, 20, true)
                       .amp (1, 300, 90, 120).mono (40)
                       .sat (fuzz, 14, 100).eq (150, -2, 3, 1500, 2).build());

    out.push_back (B ("lead-orgao", "Lead Órgão", "lead", RAP | LOFI | RNB,
                      "Registro de órgão com tremolo, para frases de soul.")
                       .wt (0, orgao, 60, -6).lp (6000, 0, false)
                       .lfo (0, LfoShape::sine, 5.8f, false).mod (Source::lfo1, Dest::volume, 12)
                       .amp (3, 200, 100, 150).mono (35).drift (15).build());

    out.push_back (B ("lead-dobra", "Lead Dobra", "lead", TRAP | POP,
                      "Seno dobrado que respira no tempo (LFO em 1/4 na forma).")
                       .wt (0, dobra, 30, -6)
                       .lfoSync (0, LfoShape::triangle, quarter).mod (Source::lfo1, Dest::shape1, 35)
                       .lp (8000, 0, false)
                       .amp (3, 300, 90, 200).mono (55).delay (d8, 25, 15).build());

    out.push_back (B ("lead-pop-brilhante", "Lead Pop Brilhante", "lead", POP,
                      "Serras com um pulso uma oitava acima e filtro que abre no ataque.")
                       .va (0, Wave::saw, -6).unison (0, 2, 10, 50)
                       .va (1, Wave::pulse, -12, 35).tune (1, 1)
                       .lp (6500, 10, false).fenv (1.0f, 1, 300, 50, 200)
                       .amp (2, 400, 90, 250).mono (30).reverb (40, 1.6f, 15).build());

    out.push_back (B ("lead-digital-lofi", "Lead Digital Lo-fi", "lead", LOFI | PHONK,
                      "Tabela digital em degraus, com oscilação de fita.")
                       .wt (0, digital, 40, -6).lp (4000, 5, false)
                       .lfo (0, LfoShape::sine, 0.8f, false).mod (Source::lfo1, Dest::pitch, 0.4f)
                       .amp (2, 400, 80, 200).mono (40).drift (40).sat (tape, 6, 100).build());

    out.push_back (B ("lead-sopro", "Lead Sopro", "lead", RNB | POP,
                      "Serra com ar e filtro que abre devagar: quase um metal de sopro.")
                       .va (0, Wave::saw, -6).noise (1, 55, -22, 20)
                       .lp (1400, 10, true).fenv (1.5f, 60, 400, 60, 200)
                       .amp (40, 300, 90, 250).mono (50).vibrato (15, 5.0f, 500)
                       .reverb (40, 1.8f, 15).build());

    //==========================================================================
    // Plucks
    out.push_back (B ("pluck-trap", "Pluck Trap Clássico", "pluck", TRAP | DRILL,
                      "Serra e pulso com o filtro estalando e eco pontuado.")
                       .va (0, Wave::saw, -6).va (1, Wave::pulse, -10, 30)
                       .lp (500, 15, true).keyTrack (50).fenv (4.5f, 0.5f, 180, 0, 150, 75)
                       .amp (0.5f, 600, 0, 300, 70)
                       .delay (d8Dot, 30, 20).reverb (45, 1.8f, 18).build());

    out.push_back (B ("pluck-madeira", "Pluck Madeira", "pluck", TRAP | RNB,
                      "FM curto com corpo de triângulo: soa como madeira batida.")
                       .fm (0, FmAlgo::twoOp, 1, 40, 2, 0, 3, 0, 120, 0, 0, -6)
                       .va (1, Wave::triangle, -10).lp (6000, 0, false)
                       .amp (0.5f, 450, 0, 200, 70).reverb (30, 1.2f, 12).build());

    out.push_back (B ("pluck-cristal", "Pluck Cristal", "pluck", POP | TRAP,
                      "Tabela de vidro, curta e brilhante, com espaço.")
                       .wt (0, vidro, 60, -6).unison (0, 2, 6, 60)
                       .lp (2500, 10, true).fenv (3.0f, 0.5f, 250, 0, 200)
                       .amp (0.5f, 700, 0, 400, 70)
                       .reverb (55, 2.2f, 25).delay (d8Dot, 30, 18).build());

    out.push_back (B ("pluck-harpa", "Pluck Harpa Synth", "pluck", RNB | POP,
                      "Serra filtrada que se abre e um seno por baixo; cauda longa.")
                       .va (0, Wave::saw, -8).lp (800, 5, true).keyTrack (50).fenv (3.0f, 0.5f, 400, 0, 600)
                       .va (1, Wave::sine, -8)
                       .amp (0.5f, 1400, 0, 900, 65).reverb (50, 2.0f, 22).build());

    out.push_back (B ("pluck-kalimba", "Pluck Kalimba Digital", "pluck", TRAP | LOFI | RAP,
                      "FM de razão 3 que some rápido sobre um seno: lâmina de metal.")
                       .fm (0, FmAlgo::twoOp, 3, 25, 2, 0, 3, 0, 200, 0, 0, -6)
                       .va (1, Wave::sine, -8).noFilter()
                       .amp (0.5f, 600, 0, 300, 75).reverb (40, 1.5f, 18).build());

    out.push_back (B ("pluck-drill-escuro", "Pluck Drill Escuro", "pluck", DRILL,
                      "Quadrada fechada com ressonância e reverb grande: sombrio.")
                       .va (0, Wave::pulse, -6, 50).va (1, Wave::saw, -10)
                       .lp (300, 25, true).keyTrack (40).fenv (3.0f, 0.5f, 140, 0, 150)
                       .amp (0.5f, 500, 0, 350, 70).reverb (60, 2.5f, 25).build());

    out.push_back (B ("pluck-funk-stab", "Pluck Funk Stab", "pluck", FUNK | POP,
                      "Serras abertas num golpe curto, para contratempos.")
                       .va (0, Wave::saw, -6).unison (0, 3, 15, 70)
                       .lp (900, 15, true).fenv (3.5f, 0.5f, 90, 0, 80)
                       .amp (0.5f, 200, 0, 120, 70).build());

    out.push_back (B ("pluck-lofi", "Pluck Lo-fi Fita", "pluck", LOFI | RAP,
                      "Triângulo e pulso com fita, deriva e oscilação lenta.")
                       .va (0, Wave::triangle, -4).va (1, Wave::pulse, -14, 25)
                       .lp (1600, 5, false).fenv (1.5f, 0.5f, 250, 0, 200)
                       .lfo (0, LfoShape::sine, 0.6f, false).mod (Source::lfo1, Dest::pitch, 0.3f)
                       .amp (1, 600, 0, 300).drift (50).sat (tape, 10, 100).build());

    out.push_back (B ("pluck-pizzicato", "Pluck Pizzicato Synth", "pluck", POP | TRAP,
                      "Serra beliscada: o filtro acompanha a nota e fecha em 80 ms.")
                       .va (0, Wave::saw, -6).lp (700, 5, true).keyTrack (80).fenv (3.0f, 0.5f, 80, 0, 100)
                       .amp (0.5f, 250, 0, 150, 70).reverb (40, 1.4f, 20).build());

    out.push_back (B ("pluck-sino-curto", "Pluck Sino Curto", "pluck", TRAP | RNB,
                      "FM de razão 4, um toque de sino que não se arrasta.")
                       .fm (0, FmAlgo::twoOp, 4, 30, 2, 0, 3, 0, 180, 0, 0, -6)
                       .va (1, Wave::sine, -12).noFilter()
                       .amp (0.5f, 400, 0, 250, 70).reverb (45, 1.6f, 20).build());

    out.push_back (B ("pluck-gota", "Pluck Gota", "pluck", POP | TRAP,
                      "Seno que sobe de afinação num instante: gota d'água.")
                       .va (0, Wave::sine, -2).noFilter()
                       .modEnv (0.5f, 60, 0, 60, 70).mod (Source::env3, Dest::pitch, -25)
                       .amp (0.5f, 300, 0, 200, 70).reverb (50, 1.8f, 25).delay (d8, 25, 15).build());

    out.push_back (B ("pluck-vocal", "Pluck Vocal", "pluck", RNB | POP,
                      "Vogal que muda no ataque, curta.")
                       .wt (0, vogais, 10, -6).modEnv (0.5f, 200, 0, 200).mod (Source::env3, Dest::shape1, 60)
                       .lp (4000, 5, false)
                       .amp (0.5f, 500, 0, 300, 70).reverb (45, 1.8f, 20).build());

    out.push_back (B ("pluck-macio", "Pluck Macio", "pluck", RAP | LOFI | RNB,
                      "Triângulo com ataque suave: pluck que não briga com a voz.")
                       .va (0, Wave::triangle, -4).lp (1200, 0, false).fenv (1.0f, 2, 300, 0, 200)
                       .amp (4, 700, 0, 400, 60).reverb (35, 1.5f, 15).build());

    out.push_back (B ("pluck-elastico", "Pluck Elástico", "pluck", FUNK | TRAP,
                      "Afinação que cai três semitons no ataque e filtro rápido: pula como borracha.")
                       .va (0, Wave::saw, -6).va (1, Wave::pulse, -12, 40)
                       .lp (800, 20, true).keyTrack (40).fenv (3.0f, 0.5f, 120, 0, 120)
                       .pitchDrop (3, 70, 70)
                       .amp (0.5f, 350, 0, 200, 70).build());

    //==========================================================================
    // Arp / Seq
    out.push_back (B ("arp-pluck-trap", "Arp Pluck Trap", "arp", TRAP,
                      "Curtinho, com eco em 1/16: para arpejos rápidos.")
                       .va (0, Wave::saw, -6).lp (900, 10, true).fenv (3.0f, 0.5f, 120, 0, 100)
                       .amp (0.5f, 250, 0, 150, 70).delay (d16, 35, 22).reverb (40, 1.5f, 15).build());

    out.push_back (B ("seq-gate-16", "Seq Portão 1/16", "arp", POP | TRAP,
                      "Acorde segurado vira ritmo: o volume corta em semicolcheias.")
                       .va (0, Wave::saw, -6).unison (0, 4, 15, 80).freePhase (0)
                       .lp (2500, 10, false)
                       .lfoSync (0, LfoShape::rampDown, sixteenth, false).mod (Source::lfo1, Dest::volume, 90)
                       .amp (5, 300, 100, 300).reverb (40, 1.8f, 15).build());

    out.push_back (B ("arp-sino", "Arp Sino", "arp", TRAP | RNB,
                      "Sino de FM curto com eco pontuado.")
                       .fm (0, FmAlgo::twoOp, 3.5f, 30, 2, 0, 3, 0, 250, 0, 0, -6).noFilter()
                       .amp (0.5f, 350, 0, 200, 70).delay (d8Dot, 35, 20).reverb (45, 1.8f, 18).build());

    out.push_back (B ("seq-baixo-funk", "Seq Baixo Funk", "arp", FUNK,
                      "Pulso curto com sub, para linhas de baixo em semicolcheia.")
                       .va (0, Wave::pulse, -6, 35).lp (600, 20, true).keyTrack (40).fenv (2.5f, 0.5f, 100, 0, 80)
                       .sub (-10).amp (0.5f, 180, 0, 80, 70).build());

    out.push_back (B ("arp-cristal-pop", "Arp Cristal Pop", "arp", POP,
                      "Tabela de vidro bem brilhante, eco e reverb.")
                       .wt (0, vidro, 70, -6).noFilter()
                       .amp (0.5f, 300, 0, 250, 70).delay (d8, 30, 20).reverb (50, 2.0f, 20).build());

    out.push_back (B ("seq-acido", "Seq Ácido", "arp", PHONK | FUNK,
                      "Linha ácida para sequências: ressonância alta e filtro que passeia.")
                       .va (0, Wave::saw, -6).lp (600, 75, false, 20).keyTrack (30).fenv (2.5f, 0.5f, 150, 0, 100)
                       .lfo (0, LfoShape::sine, 0.1f, false).mod (Source::lfo1, Dest::cutoff, 15)
                       .amp (0.5f, 200, 50, 80).sat (tube, 8, 100).build());

    out.push_back (B ("arp-lofi", "Arp Lo-fi", "arp", LOFI | RAP,
                      "Triângulo com uma oitava acima, fita e deriva.")
                       .va (0, Wave::triangle, -4).va (1, Wave::sine, -14).tune (1, 1)
                       .lp (2000, 0, false)
                       .lfo (0, LfoShape::sine, 0.7f, false).mod (Source::lfo1, Dest::pitch, 0.3f)
                       .amp (1, 300, 0, 200).drift (40).sat (tape, 6, 100).build());

    out.push_back (B ("seq-drill", "Seq Pulsante Drill", "arp", DRILL,
                      "Quadrada com portão em tercinas: o pulso nervoso do drill.")
                       .va (0, Wave::pulse, -6, 50).lp (1200, 15, true)
                       .lfoSync (0, LfoShape::rampDown, eighthT, false).mod (Source::lfo1, Dest::volume, 85)
                       .amp (3, 300, 100, 200).reverb (50, 2.2f, 20).build());

    out.push_back (B ("arp-digital", "Arp Digital", "arp", POP | TRAP,
                      "Degraus digitais com filtro curto e eco em 1/16.")
                       .wt (0, digital, 60, -6).lp (6000, 5, false).fenv (1.0f, 0.5f, 150, 0, 100)
                       .amp (0.5f, 250, 0, 150, 70).delay (d16, 30, 18).build());

    out.push_back (B ("seq-portao-largo", "Seq Portão Largo", "arp", POP | RNB,
                      "Parede de serras abertas cortada em 1/16, com eco e reverb.")
                       .va (0, Wave::saw, -6).unison (0, 5, 20, 100).freePhase (0)
                       .va (1, Wave::pulse, -12, 40).tune (1, 1)
                       .lp (4000, 5, false)
                       .lfoSync (0, LfoShape::rampDown, sixteenth, false).mod (Source::lfo1, Dest::volume, 95)
                       .amp (5, 300, 100, 300).delay (d8Dot, 30, 20).reverb (55, 2.5f, 20).build());
}

} // namespace whynot::synth::presets
