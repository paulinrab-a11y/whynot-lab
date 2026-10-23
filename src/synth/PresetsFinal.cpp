#include "PresetBuilder.h"

// Quarta leva: completa a biblioteca perto de 400 com timbres que ainda faltavam em cada
// categoria. Mesma régua: nivelados por medição e testados no Variar.

namespace whynot::synth::presets
{

namespace
{
    B base808 (const char* id, const char* name, std::uint32_t genres, const char* description,
               float decayMs, float dropSemis, float dropMs, float glideMs)
    {
        B b (id, name, "808", genres, description);
        b.va (0, Wave::sine, 0.0f).bypassFilter (0).noFilter()
         .amp (0.5f, decayMs, 0, 140, 60).pitchDrop (dropSemis, dropMs)
         .mono (glideMs).drift (0).velAmp (35);
        return b;
    }
}

void addFinalWave (std::vector<Preset>& out)
{
    //==========================================================================
    // 808
    out.push_back (base808 ("808-trap-classico", "808 Trap Clássico", TRAP,
                            "Seno com tum firme e fita: o ponto de partida mais comum do trap.", 2600, 7, 35, 70)
                       .sat (tape, 10, 100).build());

    out.push_back (base808 ("808-brilho-saturado", "808 Brilho Saturado", TRAP | POP,
                            "Válvula forte e agudo realçado: o grave aparece em qualquer caixa.", 2800, 5, 45, 70)
                       .sat (tube, 14, 100).eq (25, 0, 0, 1000, 3).build());

    out.push_back (B ("808-reese-sujo", "808 Reese Sujo", "808", DRILL,
                      "Sub com serras desafinadas por cima, no clipper: rosna e corta.")
                       .va (0, Wave::sine, 0).bypassFilter (0)
                       .va (1, Wave::saw, -14).unison (1, 2, 25, 30)
                       .lp (500, 15, true).keyTrack (60)
                       .amp (0.5f, 3000, 0, 150, 60).pitchDrop (5, 45).mono (100).drift (0).velAmp (35)
                       .sat (hard, 8, 100).build());

    out.push_back (base808 ("808-glide-curto", "808 Glide Curto", TRAP | DRILL,
                            "Glide rápido (40 ms): o slide quase vira um salto, bom para padrões rápidos.", 2800, 5, 40, 40)
                       .sat (tape, 8, 100).build());

    out.push_back (base808 ("808-funk-pesado", "808 Funk Pesado", FUNK,
                            "Curto, com queda forte e grave reforçado.", 700, 14, 30, 40)
                       .sat (soft, 16, 100).eq (25, 3, -2, 400, 0).build());

    out.push_back (B ("808-lofi-redondo", "808 Lo-fi Redondo", "808", LOFI,
                      "Seno e triângulo abafados, com fita e deriva.")
                       .va (0, Wave::sine, 0).va (1, Wave::triangle, -12).noFilter()
                       .amp (0.5f, 1800, 0, 150, 60).pitchDrop (4, 50).mono (60).drift (20).velAmp (35)
                       .sat (tape, 8, 100).eq (20, 1, 0, 1000, -8).build());

    out.push_back (B ("808-rnb-longo", "808 R&B Longo", "808", RNB,
                      "Ataque macio, cauda de seis segundos e válvula leve.")
                       .va (0, Wave::sine, 0).noFilter()
                       .amp (2, 6000, 0, 250, 55).pitchDrop (2, 50).mono (70).drift (0).velAmp (30)
                       .sat (tube, 5, 100).build());

    out.push_back (base808 ("808-phonk-curto", "808 Phonk Curto", PHONK,
                            "Fuzz no máximo e cauda curta: estoura e sai.", 800, 8, 35, 60)
                       .sat (fuzz, 20, 100).build());

    //==========================================================================
    // Baixos
    out.push_back (B ("reese-phonk", "Reese Phonk", "bass", PHONK,
                      "Serras desafinadas no fuzz, sobre um sub.")
                       .va (0, Wave::saw, -8).unison (0, 3, 22, 40).lp (900, 15, true).keyTrack (40)
                       .sub (-4).amp (2, 400, 100, 100).mono (70).sat (fuzz, 14, 100).build());

    out.push_back (B ("baixo-wobble-lento", "Baixo Wobble Lento", "bass", TRAP | DRILL,
                      "O filtro abre e fecha em semínimas.")
                       .va (0, Wave::saw, -6).va (1, Wave::pulse, -10, 50)
                       .lp (300, 35, true, 20).keyTrack (40)
                       .lfoSync (0, LfoShape::sine, quarter).mod (Source::lfo1, Dest::cutoff, 25)
                       .sub (-6).amp (2, 300, 100, 100).mono (50).build());

    out.push_back (B ("baixo-funk-fm", "Baixo Funk FM", "bass", FUNK,
                      "FM estalado com uma quadrada grave por baixo.")
                       .fm (0, FmAlgo::twoOp, 1, 50, 2, 0, 3, 0, 200, 10, 5, -6).noFilter()
                       .sub (-8, 1, SubWave::square).amp (0.5f, 500, 40, 90, 65).mono (25).velAmp (55).build());

    out.push_back (B ("baixo-pluck-fm", "Baixo Pluck FM", "bass", POP | RNB,
                      "FM 1:1 curto: baixo pontuado e redondo.")
                       .fm (0, FmAlgo::twoOp, 1, 35, 2, 0, 3, 0, 180, 0, 0, -6).noFilter()
                       .sub (-10).amp (0.5f, 600, 20, 120, 65).mono (20).build());

    out.push_back (B ("baixo-sint-escuro", "Baixo Sintético Escuro", "bass", DRILL | TRAP,
                      "Quadrada fechada com ressonância e envelope.")
                       .va (0, Wave::pulse, -6, 50).lp (300, 30, true).keyTrack (40).fenv (2.0f, 1, 250, 10, 120)
                       .sub (-6).amp (1, 500, 90, 100).mono (60).build());

    out.push_back (B ("baixo-rap-quente", "Baixo Rap Quente", "bass", RAP,
                      "Triângulo e serra com válvula: grave quente para boom bap.")
                       .va (0, Wave::triangle, -4).va (1, Wave::saw, -12).lp (600, 5, true).keyTrack (40)
                       .amp (2, 500, 80, 120).mono (30).sat (tube, 8, 100).build());

    //==========================================================================
    // Leads
    out.push_back (B ("lead-trap-glide", "Lead Trap Glide", "lead", TRAP,
                      "Duas serras com glide em toda nota e vibrato discreto.")
                       .va (0, Wave::saw, -6).unison (0, 2, 10, 50).lp (4500, 5, false)
                       .amp (3, 300, 90, 200).mono (70, true, true).vibrato (12, 5.5f, 350).delay (d8Dot, 20, 15).build());

    out.push_back (B ("lead-sino-funk", "Lead Sino Funk", "lead", FUNK,
                      "Sino de FM mono, para melodias marcantes no funk.")
                       .fm (0, FmAlgo::twoOp, 3.5f, 35, 2, 0, 3, 0, 900, 30, 0, -6).noFilter()
                       .amp (1, 1000, 50, 200).mono (40).delay (d16, 20, 12).build());

    out.push_back (B ("lead-plucado", "Lead Plucado", "lead", POP | TRAP,
                      "Ataque de pluck que assenta num sustentado: serve para frases e notas longas.")
                       .va (0, Wave::saw, -6).unison (0, 2, 10, 50).lp (1500, 10, true).fenv (2.5f, 0.5f, 250, 30, 200)
                       .amp (0.5f, 600, 40, 250).mono (40).reverb (40, 1.6f, 15).build());

    out.push_back (B ("lead-voz-escura", "Lead Voz Escura", "lead", DRILL | RNB,
                      "Voz escura mono com vibrato lento.")
                       .wt (0, vozEscura, 40, -6).lp (4000, 0, false)
                       .amp (15, 400, 90, 300).mono (70).vibrato (14, 4.5f, 500).reverb (50, 2.2f, 20).build());

    out.push_back (B ("lead-sync-suave", "Lead Sync Suave", "lead", POP,
                      "Tabela Sync num ponto suave, filtrada.")
                       .wt (0, sync, 30, -6).lp (3000, 5, false)
                       .amp (3, 300, 90, 200).mono (40).vibrato (10, 5.5f, 300).build());

    out.push_back (B ("lead-quadrado-drill", "Lead Quadrado Drill", "lead", DRILL,
                      "Quadradas sem grave com eco em tercinas.")
                       .va (0, Wave::pulse, -6, 50).unison (0, 2, 8, 50).filter (FilterMode::highpass, 300, 5, false)
                       .amp (2, 300, 90, 150).mono (60).delay (d8T, 30, 20).build());

    out.push_back (B ("lead-flauta-lofi", "Lead Flauta Lo-fi", "lead", LOFI,
                      "Flauta sintética oscilando como fita e saturada.")
                       .va (0, Wave::triangle, -4).noise (1, 70, -28, 20).lp (2500, 0, false)
                       .lfo (0, LfoShape::sine, 0.6f, false).mod (Source::lfo1, Dest::pitch, 0.35f)
                       .amp (20, 300, 85, 200).mono (40).drift (35).vibrato (18, 5.0f, 300).sat (tape, 8, 100).build());

    out.push_back (B ("lead-seno-rap", "Lead Seno Rap", "lead", RAP,
                      "Seno com um pulso fino baixinho e glide.")
                       .va (0, Wave::sine, -4).va (1, Wave::pulse, -18, 10).lp (4000, 0, false)
                       .amp (5, 300, 95, 200).mono (60).vibrato (20, 5.5f, 300).build());

    //==========================================================================
    // Pads
    out.push_back (B ("pad-vidro-escuro", "Pad Vidro Escuro", "pad", TRAP | DRILL,
                      "Vidro fechado no filtro, entrada lenta e sala grande.")
                       .wt (0, vidro, 30, -6).unison (0, 3, 12, 80).freePhase (0)
                       .lp (1500, 10, true).amp (800, 1000, 90, 2000).reverb (70, 4.0f, 35).build());

    out.push_back (B ("pad-ensolarado", "Pad Ensolarado", "pad", FUNK | POP,
                      "Serras e triângulos abertos e claros, sem grave.")
                       .va (0, Wave::saw, -6).unison (0, 4, 14, 90).freePhase (0).va (1, Wave::triangle, -8).tune (1, 1)
                       .filter (FilterMode::highpass, 250, 5, false)
                       .amp (150, 600, 90, 800).reverb (50, 2.2f, 20).build());

    out.push_back (B ("pad-sino-lofi", "Pad Sino Lo-fi", "pad", LOFI,
                      "Tabela de sino com fita oscilando e saturação.")
                       .wt (0, sino, 40, -6).unison (0, 2, 8, 70).freePhase (0)
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.3f)
                       .lp (3500, 0, false).amp (400, 800, 85, 1400).drift (35).sat (tape, 8, 100).build());

    out.push_back (B ("pad-coro-rnb", "Pad Coro R&B", "pad", RNB,
                      "Vogal clara e voz escura juntas: coro macio.")
                       .wt (0, vogais, 15, -6).unison (0, 3, 10, 80).freePhase (0)
                       .wt (1, vozEscura, 20, -10).unison (1, 2, 8, 80).freePhase (1)
                       .lp (3500, 0, false).amp (500, 800, 90, 1500).reverb (65, 3.2f, 30).build());

    out.push_back (B ("pad-orgao-assombrado", "Pad Órgão Assombrado", "pad", PHONK,
                      "Órgão com afinação balançando ao acaso e fita.")
                       .wt (0, orgao, 40, -6).unison (0, 2, 10, 70).freePhase (0)
                       .lfo (0, LfoShape::smoothRandom, 0.7f, false).mod (Source::lfo1, Dest::pitch, 0.5f)
                       .lp (2500, 0, false).amp (500, 800, 90, 1500).sat (tape, 10, 100).reverb (65, 3.5f, 30).build());

    out.push_back (B ("pad-pop-largo", "Pad Pop Largo", "pad", POP,
                      "Oito serras bem abertas: parede de refrão.")
                       .va (0, Wave::saw, -6).unison (0, 8, 25, 100).freePhase (0)
                       .lp (5000, 0, false).amp (300, 800, 90, 1200).reverb (55, 2.6f, 25).build());

    //==========================================================================
    // Plucks
    out.push_back (B ("pluck-sync", "Pluck Sync", "pluck", TRAP,
                      "Tabela Sync aberta no ataque e fechando rápido.")
                       .wt (0, sync, 60, -6).modEnv (0.5f, 150, 0, 150).mod (Source::env3, Dest::shape1, 40)
                       .lp (1500, 10, true).fenv (2.5f, 0.5f, 150, 0, 150)
                       .amp (0.5f, 400, 0, 250, 70).delay (d8Dot, 25, 18).build());

    out.push_back (B ("pluck-marimba-funk", "Pluck Marimba Funk", "pluck", FUNK,
                      "Marimba curtinha com eco em 1/16.")
                       .fm (0, FmAlgo::twoOp, 4, 25, 2, 0, 3, 0, 80, 0, 0, -6).va (1, Wave::sine, -6).noFilter()
                       .amp (0.5f, 300, 0, 150, 75).delay (d16, 25, 15).build());

    out.push_back (B ("pluck-arpa-vidro", "Pluck Arpa de Vidro", "pluck", RNB | POP,
                      "Vidro e um FM agudo num toque longo.")
                       .wt (0, vidro, 50, -6).fm (1, FmAlgo::twoOp, 6, 12, 2, 0, 3, 0, 300, 0, 0, -16).noFilter()
                       .amp (0.5f, 1200, 0, 700, 70).reverb (55, 2.4f, 25).build());

    out.push_back (B ("pluck-sino-lofi", "Pluck Sino Lo-fi", "pluck", LOFI,
                      "Sino curto com fita e agudo cortado.")
                       .fm (0, FmAlgo::twoOp, 3, 25, 2, 0, 3, 0, 150, 0, 0, -6).noFilter()
                       .amp (0.5f, 500, 0, 250, 75).drift (30).sat (tape, 8, 100).eq (40, 0, 0, 1000, -6).build());

    out.push_back (B ("pluck-drill-pulso", "Pluck Drill Pulso", "pluck", DRILL,
                      "Pulso fino fechado com ressonância, reverb grande.")
                       .va (0, Wave::pulse, -6, 20).lp (400, 30, true).keyTrack (40).fenv (3.0f, 0.5f, 120, 0, 120)
                       .amp (0.5f, 450, 0, 300, 70).reverb (60, 2.6f, 25).build());

    out.push_back (B ("pluck-phonk-vidro", "Pluck Phonk Vidro", "pluck", PHONK,
                      "Degraus digitais curtos no clipper.")
                       .wt (0, digital, 70, -6).lp (5000, 0, false)
                       .amp (0.5f, 250, 0, 120, 75).sat (hard, 8, 100).build());

    //==========================================================================
    // Keys
    out.push_back (B ("piano-eletrico-trap", "Piano Elétrico Trap", "keys", TRAP,
                      "Piano elétrico de FM com sala grande: acordes de trap melódico.")
                       .fm (0, FmAlgo::twoOp, 1, 28, 2, 0, 3, 0, 1300, 10, 0, -6)
                       .fm (1, FmAlgo::twoOp, 14, 20, 2, 0, 3, 0, 250, 0, 0, -16).noFilter()
                       .amp (0.5f, 2800, 0, 600, 65).velAmp (65).reverb (60, 2.6f, 25).build());

    out.push_back (B ("orgao-baile", "Órgão Baile", "keys", FUNK,
                      "Todos os registros num toque que cai: órgão picado para o funk.")
                       .wt (0, orgao, 100, -6).noFilter()
                       .amp (1, 500, 30, 100).build());

    out.push_back (B ("keys-palheta-lofi", "Keys Palheta Lo-fi", "keys", LOFI,
                      "Piano de palheta de FM com fita oscilando.")
                       .fm (0, FmAlgo::twoOp, 1, 45, 2, 0, 3, 0, 500, 20, 8, -6).lp (3500, 0, false)
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.3f)
                       .amp (0.5f, 2000, 10, 350).drift (35).sat (tape, 9, 100).build());

    out.push_back (B ("keys-neo-soul", "Keys Neo Soul", "keys", RNB,
                      "Dois FMs (corpo 1:1 e brilho 1:2) com auto pan lento.")
                       .fm (0, FmAlgo::twoOp, 1, 25, 2, 0, 3, 0, 1400, 10, 0, -6)
                       .fm (1, FmAlgo::twoOp, 2, 12, 2, 0, 3, 0, 600, 0, 0, -14).noFilter()
                       .lfo (0, LfoShape::sine, 3.5f, false).mod (Source::lfo1, Dest::pan, 20)
                       .amp (0.5f, 2800, 0, 500, 65).velAmp (65).build());

    out.push_back (B ("piano-pop-brilhante", "Piano Pop Brilhante", "keys", POP,
                      "Serra e triângulo com uma faísca de FM.")
                       .va (0, Wave::saw, -10).va (1, Wave::triangle, -6).fm (2, FmAlgo::twoOp, 4, 10, 2, 0, 3, 0, 300, 0, 0, -18)
                       .lp (3000, 5, true).velToCutoff (40).fenv (1.5f, 0.5f, 800, 0, 400)
                       .amp (0.5f, 2000, 0, 500, 65).reverb (40, 1.6f, 15).build());

    out.push_back (B ("keys-caixinha-drill", "Keys Caixinha Drill", "keys", DRILL,
                      "Lâmina fina de FM num reverb enorme.")
                       .fm (0, FmAlgo::twoOp, 5, 15, 2, 0, 3, 0, 300, 0, 0, -6).va (1, Wave::sine, -8).noFilter()
                       .amp (0.5f, 1500, 0, 800, 75).reverb (70, 3.5f, 32).build());

    //==========================================================================
    // Sinos
    out.push_back (B ("sino-trap-escuro", "Sino Trap Escuro", "bell", TRAP,
                      "O sino do trap fechado no filtro.")
                       .fm (0, FmAlgo::twoOp, 3.5f, 40, 2, 0, 3, 0, 800, 15, 0, -6).lp (2500, 0, false)
                       .amp (0.5f, 2500, 0, 800, 70).reverb (60, 2.6f, 25).build());

    out.push_back (B ("sino-pop-doce", "Sino Pop Doce", "bell", POP,
                      "FM 1:4 com um seno e eco: doce e limpo.")
                       .fm (0, FmAlgo::twoOp, 4, 15, 2, 0, 3, 0, 500, 5, 0, -6).va (1, Wave::sine, -8).noFilter()
                       .amp (0.5f, 1500, 0, 600, 70).delay (d8, 25, 15).build());

    out.push_back (B ("sino-vibra", "Sino Vibra", "bell", RNB | RAP,
                      "Sino com tremolo, como um vibrafone sintético.")
                       .fm (0, FmAlgo::twoOp, 4, 18, 2, 0, 3, 0, 900, 5, 0, -6).noFilter()
                       .lfo (0, LfoShape::sine, 5.5f, false).mod (Source::lfo1, Dest::volume, 20)
                       .amp (0.5f, 2500, 0, 800, 70).build());

    out.push_back (B ("agogo-duplo", "Agogô Duplo", "bell", FUNK,
                      "Dois sinos de FM uma quarta acima: o agogô de duas bocas.")
                       .fm (0, FmAlgo::twoOp, 1.48f, 40, 2, 0, 3, 0, 150, 5, 0, -8)
                       .fm (1, FmAlgo::twoOp, 1.48f, 40, 2, 0, 3, 0, 150, 5, 0, -10).tune (1, 0, 5)
                       .filter (FilterMode::bandpass, 1300, 15, false)
                       .amp (0.5f, 300, 0, 120, 80).build());

    //==========================================================================
    // Metais
    out.push_back (B ("brass-trap-curto", "Brass Trap Curto", "brass", TRAP,
                      "Golpe de metais curto e encorpado.")
                       .va (0, Wave::saw, -6).unison (0, 3, 10, 70).va (1, Wave::pulse, -12, 50).tune (1, -1)
                       .lp (900, 10, true).fenv (2.0f, 3, 200, 30, 150)
                       .amp (3, 300, 30, 150).reverb (45, 1.8f, 15).build());

    out.push_back (B ("metais-phonk-longos", "Metais Phonk Longos", "brass", PHONK,
                      "Metais sustentados no fuzz, com sala.")
                       .va (0, Wave::saw, -6).unison (0, 2, 10, 60).lp (1200, 10, true).fenv (1.5f, 40, 500, 60, 300)
                       .amp (40, 400, 90, 400).sat (fuzz, 10, 100).reverb (55, 2.6f, 22).build());

    out.push_back (B ("sopro-lofi", "Sopro Lo-fi", "brass", LOFI,
                      "Metal macio gravado em fita.")
                       .va (0, Wave::saw, -6).noise (1, 55, -28, 20).lp (900, 5, true).fenv (1.0f, 50, 400, 60, 200)
                       .amp (50, 300, 85, 300).drift (35).sat (tape, 8, 100).build());

    //==========================================================================
    // Cordas
    out.push_back (B ("cordas-epicas", "Cordas Épicas", "strings", TRAP | DRILL,
                      "Naipe grande com oitava abaixo e sala enorme.")
                       .va (0, Wave::saw, -6).unison (0, 5, 16, 90).freePhase (0)
                       .va (1, Wave::saw, -10).unison (1, 3, 12, 80).tune (1, -1).freePhase (1)
                       .lp (2200, 5, false).amp (400, 800, 90, 1500).reverb (75, 4.0f, 35).build());

    out.push_back (B ("cordas-pop", "Cordas Pop", "strings", POP,
                      "Naipe claro com entrada média, para refrões.")
                       .va (0, Wave::saw, -6).unison (0, 5, 15, 90).freePhase (0)
                       .lp (4000, 0, false).amp (200, 600, 90, 900).vibrato (8, 5.0f, 500).reverb (55, 2.6f, 25).build());

    out.push_back (B ("cordas-funk-longas", "Cordas Funk Longas", "strings", FUNK,
                      "Cordas sustentadas e brilhantes, com eco.")
                       .va (0, Wave::saw, -6).unison (0, 4, 14, 90).freePhase (0)
                       .filter (FilterMode::highpass, 200, 5, false)
                       .amp (100, 500, 90, 700).delay (d8Dot, 20, 12).reverb (50, 2.2f, 20).build());

    //==========================================================================
    // Vozes
    out.push_back (B ("vox-ahh-trap", "Vox Ahh Trap", "vox", TRAP,
                      "Vogal \"a\" em quatro vozes, sala grande.")
                       .wt (0, vogais, 0, -6).unison (0, 4, 12, 90).freePhase (0)
                       .lp (5000, 0, false).amp (300, 600, 90, 1200).reverb (70, 3.8f, 35).build());

    out.push_back (B ("vox-robo-lento", "Vox Robô Lento", "vox", POP | PHONK,
                      "Vogais andando em semínimas, com pulso e glide.")
                       .wt (0, vogais, 40, -6).va (1, Wave::pulse, -14, 25)
                       .lfoSync (0, LfoShape::square, quarter).mod (Source::lfo1, Dest::shape1, 30)
                       .lp (5000, 0, false).amp (5, 300, 90, 200).mono (50).build());

    out.push_back (B ("coro-funk", "Coro Funk", "vox", FUNK,
                      "Coro de vogal com ataque rápido, para respostas.")
                       .wt (0, vogais, 10, -6).unison (0, 3, 12, 80).freePhase (0)
                       .lp (5000, 0, false).amp (30, 500, 85, 400).reverb (45, 2.0f, 20).build());

    //==========================================================================
    // Arp / Seq
    out.push_back (B ("arp-vidro-pop", "Arp Vidro Pop", "arp", POP,
                      "Vidro curto com eco em 1/16.")
                       .wt (0, vidro, 40, -6).noFilter()
                       .amp (0.5f, 200, 0, 150, 75).delay (d16, 30, 20).build());

    out.push_back (B ("arp-drill-escuro", "Arp Drill Escuro", "arp", DRILL,
                      "Serra fechada curtinha num reverb grande.")
                       .va (0, Wave::saw, -6).lp (600, 15, true).fenv (2.0f, 0.5f, 100, 0, 80)
                       .amp (0.5f, 250, 0, 200, 70).reverb (65, 2.8f, 28).build());

    out.push_back (B ("seq-trap-8", "Seq Trap 1/8", "arp", TRAP,
                      "Acorde segurado vira colcheias no tempo.")
                       .va (0, Wave::saw, -6).unison (0, 3, 12, 80).freePhase (0).lp (2500, 5, false)
                       .lfoSync (0, LfoShape::rampDown, eighth, false).mod (Source::lfo1, Dest::volume, 90)
                       .amp (5, 300, 100, 300).reverb (45, 2.0f, 18).build());

    out.push_back (B ("arp-phonk-digital", "Arp Phonk Digital", "arp", PHONK,
                      "Degraus digitais curtíssimos com fita.")
                       .wt (0, digital, 50, -6).lp (4000, 0, false)
                       .amp (0.5f, 180, 0, 100, 75).sat (tape, 10, 100).build());

    //==========================================================================
    // FX
    out.push_back (B ("fx-riser-sirene", "Riser Sirene", "fx", TRAP | FUNK,
                      "Sirene que acelera e sobe junto com o filtro.")
                       .va (0, Wave::pulse, -6, 50).lp (800, 10, true).fenv (3.0f, 3000, 500, 100, 300)
                       .lfo (0, LfoShape::triangle, 2, true).mod (Source::lfo1, Dest::pitch, 20)
                       .modEnv (3000, 500, 100, 300, 30).mod (Source::env3, Dest::lfo1Rate, 60).mod (Source::env3, Dest::pitch, 25)
                       .amp (500, 500, 100, 300).build());

    out.push_back (B ("fx-queda-ruido", "Queda de Ruído", "fx", DRILL,
                      "Ruído que fecha por dois segundos, como uma descida.")
                       .noise (0, 70, -4, 100).lp (12000, 10, true).fenv (-6.0f, 1, 2000, 0, 500)
                       .amp (1, 2200, 0, 400).reverb (55, 2.4f, 25).build());

    out.push_back (B ("fx-laser-curto", "Laser Curto", "fx", PHONK,
                      "Laser de 60 ms com eco.")
                       .va (0, Wave::pulse, -8, 30).noFilter()
                       .modEnv (0.5f, 60, 0, 60, 70).mod (Source::env3, Dest::pitch, 100).mod (Source::env3, Dest::pitch, 100)
                       .amp (0.5f, 120, 0, 60).delay (d16, 35, 25).build());

    out.push_back (B ("fx-vento-escuro", "Vento Escuro", "fx", LOFI | DRILL,
                      "Vento grave em faixa estreita, mudando ao acaso.")
                       .noise (0, 35, -6, 100).filter (FilterMode::bandpass, 400, 40, false)
                       .lfo (0, LfoShape::smoothRandom, 0.25f, false).mod (Source::lfo1, Dest::cutoff, 20)
                       .amp (1500, 500, 100, 2000).reverb (60, 3.0f, 30).build());

    //==========================================================================
    // Texturas
    out.push_back (B ("textura-drone-claro", "Textura Drone Claro", "texture", POP | RNB,
                      "Tabela de vidro sustentada e abrindo devagar.")
                       .wt (0, vidro, 60, -6).unison (0, 4, 15, 100).freePhase (0)
                       .lfo (0, LfoShape::sine, 0.06f, false).mod (Source::lfo1, Dest::shape1, 30)
                       .noFilter().amp (2000, 1000, 100, 3000).reverb (80, 5.0f, 45).build());

    out.push_back (B ("textura-ruido-rosa", "Textura Ruído Rosa", "texture", LOFI,
                      "Ruído escuro e estável, para dar ar a uma batida seca.")
                       .noise (0, 40, -8, 100).noFilter().amp (500, 500, 100, 1000).build());

    out.push_back (B ("textura-sirene-distante", "Textura Sirene Distante", "texture", DRILL | FUNK,
                      "Sirene lenta escondida num reverb enorme.")
                       .va (0, Wave::sine, -6).lp (2000, 0, false)
                       .lfo (0, LfoShape::sine, 0.2f, true).mod (Source::lfo1, Dest::pitch, 20)
                       .amp (1500, 500, 100, 2500).reverb (85, 6.0f, 60).build());
}

} // namespace whynot::synth::presets
