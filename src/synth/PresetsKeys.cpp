#include "PresetBuilder.h"

// Keys, sinos, metais, cordas e vozes sintéticas.
// - Pianos elétricos e sinos usam FM: o brilho (índice) cai mais rápido que o volume, como uma
//   lâmina ou um sino de verdade; a força da nota aumenta o brilho.
// - Metais: o filtro abre um pouco depois do ataque (o "bwaa").
// - Cordas e vozes: unison com fase livre, ataque lento, vibrato que entra devagar.

namespace whynot::synth::presets
{

void addKeysAndOrchestral (std::vector<Preset>& out)
{
    //==========================================================================
    // Keys
    out.push_back (B ("keys-ep-fm", "Piano Elétrico FM", "keys", RNB | LOFI | POP,
                      "Corpo de FM 1:1 e uma lâmina 1:14 que some rápido; a força abre o brilho.")
                       .fm (0, FmAlgo::twoOp, 1, 32, 2, 0, 3, 0, 1500, 10, 0, -6)
                       .fm (1, FmAlgo::twoOp, 14, 22, 2, 0, 3, 0, 250, 0, 0, -16).noFilter()
                       .amp (0.5f, 3000, 0, 450, 65).velAmp (70)
                       .mod (Source::velocity, Dest::shape1, 40)
                       .lfo (0, LfoShape::sine, 4.5f, false).mod (Source::lfo1, Dest::pan, 15)
                       .reverb (35, 1.5f, 12).build());

    out.push_back (B ("keys-ep-lofi", "Piano Elétrico Lo-fi", "keys", LOFI | RAP,
                      "O piano elétrico gravado em fita: deriva, oscilação e agudo cortado.")
                       .fm (0, FmAlgo::twoOp, 1, 30, 2, 0, 3, 0, 1300, 10, 0, -6)
                       .fm (1, FmAlgo::twoOp, 14, 18, 2, 0, 3, 0, 220, 0, 0, -18).noFilter()
                       .amp (0.5f, 2600, 0, 400, 65).velAmp (65).drift (45)
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.35f)
                       .sat (tape, 8, 100).eq (40, 1, 0, 1000, -5).build());

    out.push_back (B ("keys-clavinete", "Teclado Funk Estalado", "keys", FUNK | RNB,   // id antigo: já vai nos .wnl
                      "Pulso fino sem graves, ataque estalado: o teclado do funk de groove.")
                       .va (0, Wave::pulse, -6, 20).va (1, Wave::saw, -12)
                       .filter (FilterMode::highpass, 250, 15, false)
                       .amp (0.5f, 600, 20, 80, 70).eq (20, 0, 4, 1500, 2).build());

    out.push_back (B ("keys-orgao-drawbar", "Órgão Drawbar", "keys", RNB | RAP | POP,
                      "Registro cheio com tremolo e abertura estéreo.")
                       .wt (0, orgao, 70, -6).noFilter()
                       .amp (2, 100, 100, 80)
                       .lfo (0, LfoShape::sine, 6.2f, false).mod (Source::lfo1, Dest::volume, 8)
                       .width (130).drift (5).build());

    out.push_back (B ("keys-orgao-igreja", "Órgão de Igreja", "keys", RNB | POP,
                      "Todos os registros e uma oitava abaixo, com sala grande.")
                       .wt (0, orgao, 100, -6).wt (1, orgao, 30, -10).tune (1, -1).noFilter()
                       .amp (15, 100, 100, 600).reverb (70, 3.5f, 30).build());

    out.push_back (B ("keys-piano-synth", "Piano Synth Pop", "keys", POP | TRAP,
                      "Serra e triângulo com filtro que fecha devagar; a força abre.")
                       .va (0, Wave::saw, -8).va (1, Wave::triangle, -6)
                       .lp (2200, 5, true).keyTrack (50).velToCutoff (40).fenv (2.0f, 0.5f, 600, 0, 400)
                       .amp (0.5f, 1800, 0, 500, 65).reverb (40, 1.6f, 15).build());

    out.push_back (B ("keys-vidro", "Keys de Vidro", "keys", TRAP | POP,
                      "FM 1:2 com uma camada de vidro por cima: limpo e brilhante.")
                       .fm (0, FmAlgo::twoOp, 2, 22, 2, 0, 3, 0, 700, 15, 0, -6)
                       .wt (1, vidro, 50, -14).noFilter()
                       .amp (0.5f, 2000, 0, 600, 65).reverb (50, 2.0f, 20).build());

    out.push_back (B ("keys-palheta", "Keys Palheta", "keys", LOFI | RNB,
                      "FM áspero com um pulso: lembra piano de palheta com válvula.")
                       .fm (0, FmAlgo::twoOp, 1, 45, 2, 0, 3, 0, 600, 25, 10, -6)
                       .va (1, Wave::pulse, -16, 40).lp (3500, 5, false)
                       .amp (0.5f, 2200, 10, 350, 65).velAmp (65).sat (tube, 6, 100).build());

    out.push_back (B ("keys-marimba", "Keys Marimba", "keys", TRAP | FUNK,
                      "FM de razão 4 bem curto sobre um seno: madeira afinada.")
                       .fm (0, FmAlgo::twoOp, 4, 28, 2, 0, 3, 0, 120, 0, 0, -6)
                       .va (1, Wave::sine, -6).noFilter()
                       .amp (0.5f, 500, 0, 250, 75).reverb (35, 1.3f, 15).build());

    out.push_back (B ("keys-trap-escuro", "Keys Trap Escuro", "keys", TRAP | DRILL | PHONK,
                      "Quadrada e seno fechados, cauda longa num reverb grande.")
                       .va (0, Wave::pulse, -8, 50).va (1, Wave::sine, -6)
                       .lp (900, 10, true).keyTrack (40).fenv (1.5f, 0.5f, 700, 0, 500)
                       .amp (2, 1600, 20, 600).reverb (60, 2.6f, 22).build());

    out.push_back (B ("keys-lofi-fita", "Keys Lo-fi Fita", "keys", LOFI,
                      "Triângulo com um toque de FM, fita, deriva e oscilação.")
                       .va (0, Wave::triangle, -4).fm (1, FmAlgo::twoOp, 1, 20, 2, 0, 3, 0, 800, 10, 0, -10)
                       .lp (2500, 0, false)
                       .lfo (1, LfoShape::sine, 0.55f, false).mod (Source::lfo2, Dest::pitch, 0.35f)
                       .amp (1, 1500, 20, 400).drift (50).sat (tape, 9, 100).eq (60, 1, 0, 1000, -6).build());

    out.push_back (B ("keys-funk-melodico", "Keys Funk Melódico", "keys", FUNK | POP,
                      "Serra e pulso em golpes curtos, abertos no estéreo.")
                       .va (0, Wave::saw, -8).va (1, Wave::pulse, -8, 40)
                       .lp (1600, 10, true).fenv (2.0f, 0.5f, 200, 20, 150)
                       .amp (0.5f, 400, 40, 150).width (140).build());

    //==========================================================================
    // Sinos
    out.push_back (B ("sino-trap", "Sino Trap", "bell", TRAP | DRILL | RAP,
                      "FM de razão 3,5 com cauda longa: o sino das melodias de trap.")
                       .fm (0, FmAlgo::twoOp, 3.5f, 40, 2, 0, 3, 0, 800, 15, 0, -6)
                       .va (1, Wave::sine, -10).noFilter()
                       .amp (0.5f, 2500, 0, 800, 70).reverb (55, 2.4f, 22).build());

    out.push_back (B ("sino-vidro", "Sino de Vidro", "bell", POP | RNB,
                      "Tabela de sino com um FM agudo por cima e eco.")
                       .wt (0, sino, 60, -6).fm (1, FmAlgo::twoOp, 5, 15, 2, 0, 3, 0, 500, 10, 0, -14).noFilter()
                       .amp (0.5f, 2200, 0, 900, 70).reverb (55, 2.5f, 25).delay (d8Dot, 25, 15).build());

    out.push_back (B ("celesta", "Celesta Synth", "bell", TRAP | POP,
                      "Toque leve e curto de FM 1:4.")
                       .fm (0, FmAlgo::twoOp, 4, 20, 2, 0, 3, 0, 300, 5, 0, -6).noFilter()
                       .amp (0.5f, 900, 0, 500, 70).reverb (45, 1.8f, 20).build());

    out.push_back (B ("sino-escuro-drill", "Sino Escuro Drill", "bell", DRILL,
                      "Razão inarmônica (1,41) e filtro fechado: sino sombrio e desafinado de propósito.")
                       .fm (0, FmAlgo::twoOp, 1.41f, 50, 2, 0, 3, 0, 1200, 20, 0, -6)
                       .lp (3000, 0, false)
                       .amp (0.5f, 3000, 0, 1000, 70).reverb (65, 3.0f, 28).build());

    out.push_back (B ("campana-phonk", "Campana Phonk", "bell", PHONK | DRILL,
                      "Duas quadradas em intervalo torto, filtro de banda: o sino metálico do phonk.")
                       .va (0, Wave::pulse, -6, 50).va (1, Wave::pulse, -6, 50).tune (1, 0, 7, -32)
                       .filter (FilterMode::bandpass, 900, 30, false)
                       .amp (0.5f, 350, 0, 150, 80).sat (hard, 6, 100).build());

    out.push_back (B ("caixinha-musica", "Caixinha de Música", "bell", TRAP | LOFI,
                      "Lâmina fina de FM sobre um seno, com sala.")
                       .fm (0, FmAlgo::twoOp, 5, 18, 2, 0, 3, 0, 250, 0, 0, -6)
                       .va (1, Wave::sine, -6).noFilter()
                       .amp (0.5f, 1200, 0, 600, 75).drift (20).reverb (50, 2.0f, 25).build());

    out.push_back (B ("sino-tubular", "Sino Tubular", "bell", POP | RNB,
                      "Três operadores em série, cauda de cinco segundos.")
                       .fm (0, FmAlgo::threeSerial, 3.5f, 25, 1.4f, 20, 3, 0, 1800, 20, 0, -6).noFilter()
                       .amp (0.5f, 5000, 0, 1500, 70).reverb (60, 3.0f, 25).build());

    out.push_back (B ("sino-lofi", "Sino Lo-fi", "bell", LOFI | RAP,
                      "Sino de FM com fita, deriva e agudo cortado.")
                       .fm (0, FmAlgo::twoOp, 3, 30, 2, 0, 3, 0, 500, 5, 0, -6).noFilter()
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.3f)
                       .amp (0.5f, 1500, 0, 500, 70).drift (50).sat (tape, 8, 100).eq (40, 0, 0, 1000, -6).build());

    out.push_back (B ("glockenspiel", "Glockenspiel Synth", "bell", POP | TRAP,
                      "Metal pequeno e agudo: FM 1:8 com dois senos.")
                       .fm (0, FmAlgo::twoOp, 8, 15, 2, 0, 3, 0, 150, 0, 0, -6)
                       .va (1, Wave::sine, -8).va (2, Wave::sine, -18).tune (2, 2).noFilter()
                       .amp (0.5f, 800, 0, 400, 80).reverb (45, 1.8f, 20).build());

    out.push_back (B ("sino-metalico-funk", "Sino Metálico Funk", "bell", FUNK | PHONK,
                      "Razão 2,76 com realimentação: metal estridente para contratempo.")
                       .fm (0, FmAlgo::twoOp, 2.76f, 55, 2, 0, 3, 0, 400, 10, 20, -6).noFilter()
                       .amp (0.5f, 700, 0, 300, 70).delay (d16, 25, 15).build());

    //==========================================================================
    // Metais
    out.push_back (B ("brass-largo", "Brass Synth Largo", "brass", POP | RNB,
                      "Três serras com o filtro abrindo depois do ataque.")
                       .va (0, Wave::saw, -6).unison (0, 3, 10, 70)
                       .lp (900, 10, true).fenv (2.0f, 60, 400, 60, 300)
                       .amp (40, 300, 90, 300).reverb (40, 1.8f, 15).build());

    out.push_back (B ("brass-epico", "Brass Trap Épico", "brass", TRAP | DRILL,
                      "Serras e uma quadrada uma oitava abaixo, entrada lenta e sala grande.")
                       .va (0, Wave::saw, -6).unison (0, 3, 12, 70)
                       .va (1, Wave::pulse, -10, 50).tune (1, -1)
                       .lp (700, 10, true).fenv (2.5f, 80, 600, 50, 400)
                       .amp (60, 400, 90, 500).reverb (60, 2.8f, 25).build());

    out.push_back (B ("brass-stab", "Stab de Metais", "brass", FUNK | RAP,
                      "Golpe curto e gordo, para acentos.")
                       .va (0, Wave::saw, -6).unison (0, 2, 8, 50)
                       .lp (800, 10, true).fenv (3.0f, 5, 160, 20, 120)
                       .amp (5, 250, 30, 120).sat (tape, 4, 100).build());

    out.push_back (B ("trompete-synth", "Trompete Synth", "brass", RAP | LOFI,
                      "Serra mono com sopro e vibrato que entra devagar.")
                       .va (0, Wave::saw, -6).noise (1, 60, -30, 20)
                       .lp (1000, 15, true).fenv (2.0f, 30, 300, 50, 150)
                       .amp (30, 200, 90, 150).mono (40).vibrato (20, 5.5f, 350).build());

    out.push_back (B ("brass-80", "Brass 80", "brass", POP | FUNK,
                      "Pulso e serra levemente desafinados, abertos no estéreo.")
                       .va (0, Wave::pulse, -6, 40).va (1, Wave::saw, -6).tune (1, 0, 0, 8)
                       .lp (1200, 10, true).fenv (2.0f, 20, 300, 40, 200)
                       .amp (15, 300, 80, 200).width (130).build());

    out.push_back (B ("trompa-escura", "Trompa Escura", "brass", DRILL | TRAP,
                      "Serra e triângulo graves que nascem devagar.")
                       .va (0, Wave::saw, -6).va (1, Wave::triangle, -8).tune (1, -1)
                       .lp (600, 5, true).fenv (1.0f, 150, 800, 60, 400)
                       .amp (150, 500, 90, 500).reverb (65, 3.0f, 25).build());

    out.push_back (B ("metais-phonk", "Metais Phonk", "brass", PHONK,
                      "Metais estourados no fuzz.")
                       .va (0, Wave::saw, -6).unison (0, 2, 10, 50).va (1, Wave::pulse, -12, 40)
                       .lp (1500, 10, true).fenv (1.5f, 10, 300, 50, 200)
                       .amp (10, 300, 85, 150).sat (fuzz, 12, 100).build());

    out.push_back (B ("brass-suave", "Brass Suave R&B", "brass", RNB,
                      "Serra e triângulo macios, filtro que abre pouco.")
                       .va (0, Wave::saw, -8).va (1, Wave::triangle, -6)
                       .lp (700, 5, true).fenv (1.2f, 80, 500, 50, 300)
                       .amp (80, 400, 85, 400).reverb (45, 2.0f, 18).build());

    //==========================================================================
    // Cordas
    out.push_back (B ("cordas-analogicas", "Cordas Analógicas", "strings", POP | RNB,
                      "Cinco serras desafinadas com fase livre: o naipe de cordas de sintetizador.")
                       .va (0, Wave::saw, -6).unison (0, 5, 20, 90).freePhase (0)
                       .lp (3000, 5, false)
                       .amp (300, 600, 90, 900).vibrato (8, 5.0f, 600).reverb (55, 2.5f, 25).build());

    out.push_back (B ("cordas-trap", "Cordas Trap", "strings", TRAP | DRILL,
                      "Serras e um pulso, entrada média e sala grande.")
                       .va (0, Wave::saw, -6).unison (0, 4, 14, 80).freePhase (0)
                       .va (1, Wave::pulse, -12, 30).freePhase (1)
                       .lp (2500, 5, false)
                       .amp (150, 500, 90, 700).vibrato (10, 5.2f, 500).reverb (60, 2.8f, 25).build());

    out.push_back (B ("cordas-escuras", "Cordas Escuras Drill", "strings", DRILL,
                      "Cordas graves e fechadas que crescem devagar.")
                       .va (0, Wave::saw, -6).unison (0, 4, 12, 80).freePhase (0)
                       .lp (1200, 5, true)
                       .amp (500, 800, 90, 1000).reverb (65, 3.2f, 30).build());

    out.push_back (B ("cordas-staccato", "Cordas Staccato", "strings", TRAP | FUNK,
                      "Golpes curtos de cordas, para ostinatos.")
                       .va (0, Wave::saw, -6).unison (0, 4, 15, 80).freePhase (0)
                       .lp (2800, 5, false).fenv (1.0f, 5, 150, 0, 100)
                       .amp (5, 220, 0, 150, 70).reverb (45, 1.8f, 20).build());

    out.push_back (B ("cordas-lofi", "Cordas Lo-fi", "strings", LOFI,
                      "Naipe gravado em fita: oscila, deriva e perde o agudo.")
                       .va (0, Wave::saw, -6).unison (0, 3, 10, 70).freePhase (0)
                       .lp (2000, 0, false)
                       .lfo (1, LfoShape::sine, 0.45f, false).mod (Source::lfo2, Dest::pitch, 0.4f)
                       .amp (200, 600, 90, 800).drift (50).sat (tape, 8, 100).eq (40, 0, 0, 1000, -5).build());

    out.push_back (B ("cordas-altas", "Cordas Altas", "strings", POP | RNB,
                      "Cordas uma oitava acima, sem grave, para brilho no refrão.")
                       .va (0, Wave::saw, -8).unison (0, 4, 14, 90).freePhase (0).tune (0, 1)
                       .filter (FilterMode::highpass, 300, 5, false)
                       .amp (250, 600, 90, 900).reverb (60, 2.8f, 30).build());

    out.push_back (B ("cordas-cinema", "Cordas Cinema", "strings", TRAP | RNB,
                      "Serras e uma tabela de ar juntas, entrada longa e reverb enorme.")
                       .va (0, Wave::saw, -6).unison (0, 5, 16, 90).freePhase (0)
                       .wt (1, ar, 40, -10).unison (1, 3, 10, 90).freePhase (1)
                       .lp (2600, 5, false)
                       .amp (600, 800, 90, 1500).reverb (75, 4.0f, 35).build());

    out.push_back (B ("cordas-phonk", "Cordas Phonk", "strings", PHONK | DRILL,
                      "Cordas sem grave e saturadas: tensas e ásperas.")
                       .va (0, Wave::saw, -6).unison (0, 3, 12, 70).freePhase (0)
                       .filter (FilterMode::highpass, 250, 5, false)
                       .amp (80, 400, 85, 400).sat (hard, 8, 100).reverb (50, 2.0f, 20).build());

    //==========================================================================
    // Vozes sintéticas
    out.push_back (B ("coro-aah", "Coro Synth Aah", "vox", RNB | POP,
                      "Vogal \"a\" em três vozes abertas, com vibrato.")
                       .wt (0, vogais, 0, -6).unison (0, 3, 12, 80).freePhase (0)
                       .lp (4500, 0, false)
                       .amp (200, 500, 90, 700).vibrato (12, 5.0f, 500).reverb (60, 2.8f, 30).build());

    out.push_back (B ("coro-ooh", "Coro Ooh", "vox", RNB | LOFI,
                      "Vogal \"u\" escura, macia e larga.")
                       .wt (0, vozEscura, 0, -6).unison (0, 3, 10, 80).freePhase (0)
                       .lp (3000, 0, false)
                       .amp (250, 600, 90, 800).vibrato (10, 4.8f, 600).reverb (60, 3.0f, 30).build());

    out.push_back (B ("vox-falante", "Vox Falante", "vox", FUNK | RNB,
                      "A vogal muda a cada nota (envelope na forma): parece falar.")
                       .wt (0, vogais, 0, -4).va (1, Wave::saw, -14)
                       .modEnv (5, 400, 40, 300).mod (Source::env3, Dest::shape1, 70)
                       .lp (3500, 5, false)
                       .amp (5, 300, 90, 150).mono (50).vibrato (10, 5.5f, 300).build());

    out.push_back (B ("vox-trap-eterea", "Vox Trap Etérea", "vox", TRAP | RNB,
                      "Voz que muda devagar, com eco e reverb longos.")
                       .wt (0, vogais, 25, -6).unison (0, 2, 8, 60).freePhase (0)
                       .lfo (0, LfoShape::sine, 0.2f, false).mod (Source::lfo1, Dest::shape1, 20)
                       .lp (6000, 0, false)
                       .amp (300, 600, 90, 1200).reverb (70, 3.5f, 35).delay (d4, 30, 20).build());

    out.push_back (B ("vox-formante", "Vox Formante Varrida", "vox", POP | TRAP,
                      "As vogais passeiam a cada compasso.")
                       .wt (0, vogais, 50, -6)
                       .lfoSync (0, LfoShape::triangle, bar1, false).mod (Source::lfo1, Dest::shape1, 45)
                       .lp (6000, 0, false)
                       .amp (50, 500, 90, 500).reverb (50, 2.2f, 22).build());

    out.push_back (B ("vox-robo", "Vox Robô", "vox", PHONK | FUNK,
                      "Vogais picotadas em 1/16 com pulso e clipper: voz de máquina.")
                       .wt (0, vogais, 40, -6).va (1, Wave::pulse, -10, 20)
                       .lfoSync (0, LfoShape::square, sixteenth).mod (Source::lfo1, Dest::shape1, 25)
                       .lp (5000, 5, false)
                       .amp (2, 300, 90, 100).mono (30).sat (hard, 8, 100).build());

    out.push_back (B ("vox-sussurro", "Vox Sussurro", "vox", LOFI | RNB,
                      "Ar filtrado com uma voz escura bem baixa: sussurro.")
                       .noise (0, 45, -12, 50).wt (1, vozEscura, 50, -16)
                       .filter (FilterMode::bandpass, 1200, 30, false)
                       .amp (150, 600, 80, 700).reverb (55, 2.5f, 25).build());

    out.push_back (B ("vox-drill-sombria", "Vox Drill Sombria", "vox", DRILL,
                      "Coro escuro e fechado, cauda muito longa.")
                       .wt (0, vozEscura, 50, -6).unison (0, 3, 10, 80).freePhase (0)
                       .lp (1600, 5, false)
                       .amp (300, 800, 90, 1200).vibrato (8, 4.5f, 800).reverb (70, 3.6f, 32).build());
}

} // namespace whynot::synth::presets
