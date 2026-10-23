#include "PresetBuilder.h"

// 808 e baixos. Notas de desenho:
// - 808 é um seno grave com uma queda rápida de afinação no ataque (o "tum"), decaimento longo
//   e soltura curta; mono com glide para os slides do piano roll; fase sempre do zero (ataque igual).
// - Saturação dá harmônicos que aparecem em caixinha de celular sem mudar a nota.
// - Baixos usam o sub sem filtro e camadas filtradas por cima.

namespace whynot::synth::presets
{

namespace
{
    /** Base comum dos 808: seno, sem filtro, mono com glide, sem deriva. */
    B base808 (const char* id, const char* name, std::uint32_t genres, const char* description,
               float decayMs = 3000.0f, float dropSemis = 5.0f, float dropMs = 45.0f, float glideMs = 70.0f)
    {
        B b (id, name, "808", genres, description);
        b.va (0, Wave::sine, 0.0f).bypassFilter (0).noFilter()
         .amp (0.5f, decayMs, 0, 140, 60).pitchDrop (dropSemis, dropMs)
         .mono (glideMs).drift (0).velAmp (35);
        return b;
    }
}

void add808AndBass (std::vector<Preset>& out)
{
    //==========================================================================
    // 808
    out.push_back (base808 ("808-limpo", "808 Limpo", TRAP | DRILL | RAP,
                            "Seno puro com o tum no ataque. Deixa espaço para o kick.").build());

    out.push_back (base808 ("808-quente", "808 Quente", TRAP | RAP | RNB,
                            "Válvula leve: harmônicos pares que fazem o grave aparecer no celular.")
                       .sat (tube, 10, 100).build());

    out.push_back (base808 ("808-sujo", "808 Sujo", TRAP | DRILL | PHONK,
                            "Distorcido e agressivo, para 808 que manda na faixa.", 2600, 6, 40)
                       .sat (hard, 20, 100).eq (25, 2, -2, 350, 1).build());

    out.push_back (base808 ("808-longo", "808 Longo", TRAP | RNB,
                            "Cauda comprida para notas longas e melódicas.", 7000, 4, 50, 90)
                       .sat (tape, 6, 100).build());

    out.push_back (base808 ("808-curto", "808 Curto", RAP | FUNK | LOFI,
                            "Soco curto, encaixa entre os kicks do boom bap.", 650, 12, 30)
                       .sat (tape, 8, 100).build());

    out.push_back (base808 ("808-drill-slide", "808 Drill Deslizante", DRILL | TRAP,
                            "Glide longo para os slides do drill; um pouco de fita.", 3500, 3, 40, 140)
                       .sat (tape, 9, 100).build());

    out.push_back (base808 ("808-phonk", "808 Phonk Estourado", PHONK | DRILL,
                            "Fuzz pesado com brilho: o 808 estourado do phonk.", 2200, 7, 35)
                       .sat (fuzz, 22, 100).eq (30, 0, 3, 900, 4).build());

    out.push_back (B ("808-tom", "808 Tom", "808", TRAP | RAP, "Triângulo com queda longa: parece um tom grave afinado.")
                       .va (0, Wave::triangle, 0).noFilter()
                       .amp (0.5f, 1800, 0, 120, 65).pitchDrop (12, 110).mono (60).drift (0).velAmp (35)
                       .sat (tape, 4, 100).build());

    out.push_back (B ("808-reese", "808 Reese", "808", DRILL | TRAP, "Sub com duas serras desafinadas por cima, filtradas: rosna.")
                       .va (0, Wave::sine, 0).bypassFilter (0)
                       .va (1, Wave::saw, -14).unison (1, 2, 18, 30)
                       .lp (380, 15, true).keyTrack (60)
                       .amp (0.5f, 3200, 0, 150, 60).pitchDrop (4, 45).mono (90).drift (0).velAmp (35)
                       .sat (soft, 6, 100).build());

    out.push_back (base808 ("808-batidao", "808 Batidão", FUNK,
                            "Grave curto e socado do funk: queda forte de afinação e corte duro.", 480, 24, 28, 40)
                       .sat (hard, 12, 100).eq (25, 3, -3, 300, 0).build());

    out.push_back (base808 ("808-sub-puro", "808 Sub Puro", RNB | POP | LOFI,
                            "Sem queda e sem distorção: só o sub, macio.", 4500, 0, 40, 60)
                       .amp (2, 4500, 0, 200, 55).build());

    out.push_back (B ("808-clique", "808 Clique", "808", TRAP | DRILL, "Clique duro no ataque (afinação despencando em milissegundos).")
                       .va (0, Wave::sine, 0).noFilter()
                       .amp (0.5f, 2800, 0, 140, 60)
                       .modEnv (0.5f, 14, 0, 14, 90).mod (Source::env3, Dest::pitch, 100).mod (Source::env3, Dest::pitch, 60)
                       .mono (70).drift (0).velAmp (35).sat (tape, 7, 100).build());

    out.push_back (base808 ("808-lofi", "808 Abafado", LOFI | RAP,
                            "Fita e corte de agudos: grave gordo e discreto do boom bap.", 1600, 5, 50, 60)
                       .sat (tape, 10, 100).eq (20, 2, 0, 1000, -8).drift (15).build());

    out.push_back (B ("808-brilhante", "808 Brilhante", "808", POP | TRAP,
                      "Seno com uma quadrada filtrada baixinha: o grave aparece em fone pequeno.")
                       .va (0, Wave::sine, 0).bypassFilter (0)
                       .va (1, Wave::pulse, -20, 50).lp (900, 5, true).keyTrack (100)
                       .amp (0.5f, 3000, 0, 140, 60).pitchDrop (5, 45).mono (70).drift (0).velAmp (35).build());

    out.push_back (base808 ("808-glide-sempre", "808 Glide Sempre", TRAP | PHONK,
                            "Desliza em toda nota nova (glide Sempre), para linhas de 808 cantadas.", 3200, 3, 40, 160)
                       .mono (160, true, true).sat (tube, 8, 100).build());

    out.push_back (base808 ("808-pesado", "808 Pesado", DRILL | TRAP | RAP,
                            "Saturação suave e reforço no grave: denso sem rasgar.", 3400, 6, 50)
                       .sat (soft, 14, 100).eq (25, 3, -2, 500, 2).build());

    out.push_back (B ("808-escuro", "808 Escuro", "808", RNB | LOFI | TRAP,
                      "Seno e triângulo abafados: corpo redondo, quase sem agudo.")
                       .va (0, Wave::sine, 0).bypassFilter (0)
                       .va (1, Wave::triangle, -12).lp (220, 0, true).keyTrack (100)
                       .amp (0.5f, 3600, 0, 160, 60).pitchDrop (4, 55).mono (80).drift (0).velAmp (35).build());

    out.push_back (base808 ("808-punch", "808 Punch", TRAP | DRILL | FUNK,
                            "Ataque com queda de uma oitava: soca como kick e segura como 808.", 2400, 12, 55)
                       .sat (tape, 12, 100).build());

    //==========================================================================
    // Baixos
    out.push_back (B ("baixo-sub-seno", "Baixo Sub Seno", "bass", POP | RNB | TRAP,
                      "Seno sustentado, para linhas de baixo que seguram a nota.")
                       .va (0, Wave::sine, 0).noFilter().amp (3, 300, 100, 90, 50).mono (40).drift (0).velAmp (30).build());

    out.push_back (B ("baixo-reese-drill", "Reese Drill", "bass", DRILL | TRAP,
                      "Serras desafinadas filtradas sobre um sub: o baixo que rosna do drill.")
                       .va (0, Wave::saw, -6).unison (0, 4, 24, 40)
                       .lp (420, 22, true, 20).keyTrack (40)
                       .sub (-3).amp (4, 400, 100, 120, 50).mono (90).drift (5)
                       .lfo (0, LfoShape::sine, 0.25f, false).mod (Source::lfo1, Dest::cutoff, 4)
                       .sat (soft, 6, 100).build());

    out.push_back (B ("baixo-funk-synth", "Baixo Funk Synth", "bass", FUNK | RNB | POP,
                      "Pulso e serra com o filtro estalando a cada nota; força abre o filtro.")
                       .va (0, Wave::pulse, -6, 30).va (1, Wave::saw, -10)
                       .lp (380, 30, true).keyTrack (50).velToCutoff (40)
                       .fenv (3.2f, 1, 220, 15, 120, 70)
                       .amp (1, 600, 60, 80, 50).mono (35).drift (8).build());

    out.push_back (B ("baixo-pluck", "Baixo Pluck", "bass", POP | RAP | TRAP,
                      "Ataque de filtro rápido que some: baixo pontuado e limpo.")
                       .va (0, Wave::saw, -6).lp (260, 12, true).keyTrack (60)
                       .fenv (4.0f, 0.5f, 160, 0, 120, 75)
                       .sub (-8).amp (0.5f, 500, 30, 90, 60).mono (30).drift (6).build());

    out.push_back (B ("baixo-acido", "Baixo Ácido", "bass", PHONK | FUNK,
                      "Serra com ressonância alta e envelope curto: a linha que canta e chia.")
                       .va (0, Wave::saw, -6).lp (450, 72, false, 25).keyTrack (40)
                       .fenv (3.0f, 0.5f, 190, 0, 100, 70).velToCutoff (50)
                       .amp (1, 300, 80, 60, 50).mono (60).drift (5).sat (tube, 8, 100).build());

    out.push_back (B ("baixo-analogico-gordo", "Baixo Analógico Gordo", "bass", RAP | LOFI | RNB,
                      "Duas serras quase juntas e um sub, filtro de 24 dB: grave encorpado.")
                       .va (0, Wave::saw, -6).va (1, Wave::saw, -6).tune (1, 0, 0, 7)
                       .lp (650, 18, true, 15).keyTrack (40).fenv (2.0f, 2, 320, 30, 150, 60)
                       .sub (-6).amp (2, 600, 85, 100, 50).mono (45).drift (20).build());

    out.push_back (B ("baixo-fm-eletrico", "Baixo FM Elétrico", "bass", RNB | POP | FUNK,
                      "FM de dois operadores com estalo que cai rápido, estilo baixo dos anos 80.")
                       .fm (0, FmAlgo::twoOp, 1, 55, 2, 0, 3, 0, 260, 12, 10, -6)
                       .noFilter().amp (0.5f, 1200, 40, 100, 60).mono (25).drift (3).velAmp (60)
                       .mod (Source::velocity, Dest::shape1, 30).build());

    out.push_back (B ("baixo-wobble", "Baixo Wobble", "bass", TRAP | PHONK,
                      "LFO no tempo (1/8) abrindo e fechando o filtro.")
                       .va (0, Wave::saw, -6).va (1, Wave::pulse, -8, 50).tune (1, -1)
                       .lp (380, 40, true, 30).keyTrack (40)
                       .lfoSync (0, LfoShape::triangle, eighth).mod (Source::lfo1, Dest::cutoff, 22)
                       .sub (-6).amp (2, 300, 100, 80, 50).mono (40).sat (soft, 8, 100).build());

    out.push_back (B ("baixo-sujo-phonk", "Baixo Sujo Phonk", "bass", PHONK | DRILL,
                      "Quadrada e serra no fuzz, corte em 1,2 kHz: cru e estourado.")
                       .va (0, Wave::pulse, -8, 45).va (1, Wave::saw, -8)
                       .lp (1200, 10, true).keyTrack (30)
                       .sub (-6).amp (1, 400, 90, 80, 50).mono (50).sat (fuzz, 18, 100).build());

    out.push_back (B ("baixo-redondo-lofi", "Baixo Redondo Lo-fi", "bass", LOFI | RAP,
                      "Triângulo e sub com fita e deriva: o baixo gravado num gravador velho.")
                       .va (0, Wave::triangle, -4).lp (700, 5, true).keyTrack (50)
                       .sub (-6).amp (3, 700, 70, 120, 50).mono (40).drift (45)
                       .sat (tape, 10, 100).eq (20, 1, 0, 1000, -6).build());

    out.push_back (B ("baixo-palheta", "Baixo de Palheta", "bass", POP | RNB,
                      "Pulso fino com filtro rápido: ataque de dedo e cauda curta.")
                       .va (0, Wave::pulse, -6, 18).lp (500, 15, true).keyTrack (60)
                       .fenv (2.8f, 0.5f, 120, 10, 80, 70).sub (-10)
                       .amp (0.5f, 900, 40, 90, 55).mono (20).drift (6).build());

    out.push_back (B ("baixo-sint-80", "Baixo Sintético 80", "bass", POP | FUNK,
                      "Quadrada com uma serra uma oitava acima: brilho de sintetizador de época.")
                       .va (0, Wave::pulse, -6, 50).va (1, Wave::saw, -14).tune (1, 1)
                       .lp (900, 20, true).keyTrack (50).fenv (1.5f, 1, 260, 40, 100, 60)
                       .amp (1, 400, 90, 80, 50).mono (30).drift (10).build());

    out.push_back (B ("baixo-vocal", "Baixo Vocal", "bass", RNB | POP,
                      "Tabela de vogais que vai de \"a\" a \"u\" a cada nota.")
                       .wt (0, vogais, 0, -4).modEnv (2, 350, 30, 200, 60).mod (Source::env3, Dest::shape1, 70)
                       .lp (2500, 10, true).keyTrack (40).sub (-6)
                       .amp (2, 500, 90, 100, 50).mono (45).drift (8).build());

    out.push_back (B ("baixo-distorcido-drill", "Baixo Distorcido Drill", "bass", DRILL | TRAP,
                      "Serra e seno no clipper, com corte de agudos: grave cortante.")
                       .va (0, Wave::saw, -8).va (1, Wave::sine, -2).bypassFilter (1)
                       .lp (1800, 8, true).keyTrack (30)
                       .amp (1, 500, 90, 100, 50).mono (110).sat (hard, 15, 100).eq (30, 0, 0, 1000, -2).build());

    out.push_back (B ("baixo-sub-quadrado", "Baixo Sub Quadrado", "bass", TRAP | FUNK,
                      "Quadrada filtrada bem grave com seno: firme e um pouco mais áspero que o seno.")
                       .va (0, Wave::pulse, -6, 50).lp (240, 5, true).keyTrack (100)
                       .va (1, Wave::sine, -4).bypassFilter (1)
                       .amp (1, 600, 90, 90, 50).mono (40).drift (4).build());

    out.push_back (B ("baixo-boom-bap", "Baixo Seco Boom Bap", "bass", LOFI | RAP,
                      "Corpo curto e seco que encaixa entre bumbo e caixa.")
                       .va (0, Wave::triangle, -4).va (1, Wave::sine, -4).bypassFilter (1)
                       .lp (600, 5, true).keyTrack (40)
                       .amp (1, 380, 35, 90, 60).mono (25).drift (25).sat (tape, 9, 100).eq (25, 0, 0, 1000, -7).build());
}

} // namespace whynot::synth::presets
