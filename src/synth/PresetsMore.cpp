#include "PresetBuilder.h"

// Segunda leva de presets (rumo aos ~400): mais variedade em cada categoria, com a mesma régua
// de qualidade (nivelados por medição, testados no Variar).

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

void addSecondWave (std::vector<Preset>& out)
{
    //==========================================================================
    // 808
    out.push_back (base808 ("808-sub-grave", "808 Sub Grave", TRAP | RNB | DRILL,
                            "Um seno uma oitava abaixo por baixo do principal: grave que se sente no peito.", 4200, 4, 50, 80)
                       .sub (-8).build());

    out.push_back (base808 ("808-fita-saturada", "808 Fita Saturada", TRAP | LOFI,
                            "Fita forte e agudo cortado: grave gordo com sujeira macia.", 3000, 5, 45, 70)
                       .sat (tape, 16, 100).eq (25, 1, 0, 1000, -3).build());

    out.push_back (base808 ("808-rasgado", "808 Rasgado", DRILL | PHONK,
                            "Fuzz com médio na frente: o 808 corta até em fone ruim.", 2600, 7, 40, 90)
                       .sat (fuzz, 14, 100).eq (30, 0, 4, 800, 2).build());

    out.push_back (base808 ("808-funk-arrastado", "808 Funk Arrastado", FUNK,
                            "Curto, com queda forte e glide em toda nota: grave que arrasta no batidão.", 900, 10, 35, 120)
                       .mono (120, true, true).sat (hard, 10, 100).build());

    out.push_back (B ("808-pop-redondo", "808 Pop Redondo", "808", POP | RNB,
                      "Seno com um triângulo bem baixo e saturação suave: redondo e limpo.")
                       .va (0, Wave::sine, 0).va (1, Wave::triangle, -14).noFilter()
                       .amp (0.5f, 3200, 0, 160, 60).pitchDrop (3, 50).mono (70).drift (0).velAmp (35)
                       .sat (soft, 4, 100).build());

    out.push_back (B ("808-zumbido", "808 Zumbido", "808", DRILL | TRAP,
                      "Pulso fino filtrado por cima do seno: um zumbido grave que aparece no celular.")
                       .va (0, Wave::sine, 0).bypassFilter (0)
                       .va (1, Wave::pulse, -18, 10).lp (420, 10, true).keyTrack (100)
                       .amp (0.5f, 3000, 0, 150, 60).pitchDrop (5, 45).mono (90).drift (0).velAmp (35).build());

    out.push_back (B ("808-macio-rnb", "808 Macio R&B", "808", RNB | POP,
                      "Sem queda e com ataque suave: grave que abraça o acorde.")
                       .va (0, Wave::sine, 0).noFilter()
                       .amp (3, 3800, 0, 300, 55).mono (60).drift (0).velAmp (30).sat (tube, 4, 100).build());

    out.push_back (B ("808-boom-longo", "808 Boom Longo", "808", TRAP | DRILL,
                      "Decai e depois segura um quarto do volume enquanto a nota estiver apertada.")
                       .va (0, Wave::sine, 0).noFilter()
                       .amp (0.5f, 2500, 25, 220, 60).pitchDrop (5, 45).mono (80).drift (0).velAmp (35)
                       .sat (tape, 6, 100).build());

    out.push_back (base808 ("808-phonk-deriva", "808 Phonk Deriva", PHONK,
                            "Fuzz, queda de uma oitava e glide longo em toda nota.", 2400, 12, 40, 200)
                       .mono (200, true, true).sat (fuzz, 18, 100).build());

    out.push_back (B ("808-triangulo-sujo", "808 Triângulo Sujo", "808", DRILL | TRAP,
                      "Triângulo no clipper: mais harmônicos que o seno, ainda grave.")
                       .va (0, Wave::triangle, 0).noFilter()
                       .amp (0.5f, 2800, 0, 140, 60).pitchDrop (6, 45).mono (90).drift (0).velAmp (35)
                       .sat (hard, 12, 100).build());

    out.push_back (base808 ("808-drill-curto", "808 Drill Curto", DRILL,
                            "Curto e deslizante, para os padrões picados do drill.", 900, 7, 40, 100)
                       .sat (tape, 10, 100).build());

    out.push_back (B ("808-lofi-vinil", "808 Lo-fi Vinil", "808", LOFI | RAP,
                      "Fita, agudo cortado, deriva e um chiado de disco junto da nota.")
                       .va (0, Wave::sine, 0).noise (1, 30, -32, 40).noFilter()
                       .amp (0.5f, 1800, 0, 140, 60).pitchDrop (5, 50).mono (60).drift (25).velAmp (35)
                       .sat (tape, 12, 100).eq (20, 2, 0, 1000, -10).build());

    //==========================================================================
    // Baixos
    out.push_back (B ("baixo-reese-largo", "Reese Largo", "bass", DRILL | TRAP,
                      "Duas pilhas de serras desafinadas que se mexem devagar sobre um sub.")
                       .va (0, Wave::saw, -8).unison (0, 3, 30, 50).va (1, Wave::saw, -8).unison (1, 3, 18, 50).tune (1, 0, 0, -9)
                       .lp (600, 15, true, 15).keyTrack (40)
                       .lfo (0, LfoShape::sine, 0.3f, false).mod (Source::lfo1, Dest::detune, 20)
                       .sub (-4).amp (4, 400, 100, 140).mono (90).drift (5).build());

    out.push_back (B ("baixo-sub-triangulo", "Baixo Sub Triângulo", "bass", RAP | LOFI,
                      "Triângulo fechado e um sub: grave quente e discreto.")
                       .va (0, Wave::triangle, -4).lp (300, 0, true).keyTrack (60)
                       .sub (-10).amp (3, 500, 90, 100).mono (40).drift (10).build());

    out.push_back (B ("baixo-slap", "Baixo Slap Synth", "bass", FUNK | POP,
                      "FM com estalo forte que some em 120 ms e um pulso filtrado por baixo.")
                       .fm (0, FmAlgo::twoOp, 1, 70, 2, 0, 3, 0, 120, 5, 0, -6)
                       .va (1, Wave::pulse, -12, 40).lp (700, 10, true).keyTrack (40).fenv (2.0f, 0.5f, 150, 10, 80)
                       .amp (0.5f, 700, 40, 90, 65).mono (25).velAmp (60).build());

    out.push_back (B ("baixo-lamento", "Baixo Lamento", "bass", RAP | FUNK,
                      "Seno com um pouco de serra, glide em toda nota e vibrato: baixo que canta.")
                       .va (0, Wave::sine, -2).va (1, Wave::saw, -16).lp (500, 5, true).keyTrack (40)
                       .amp (3, 500, 90, 140).mono (90, true, true).vibrato (12, 5.0f, 400).build());

    out.push_back (B ("baixo-pop-pluckado", "Baixo Pop Pluckado", "bass", POP,
                      "Serra e quadrada uma oitava acima, filtro que estala e fecha.")
                       .va (0, Wave::saw, -6).va (1, Wave::pulse, -14, 50).tune (1, 1)
                       .lp (400, 15, true).keyTrack (50).fenv (3.0f, 0.5f, 200, 10, 120)
                       .sub (-10).amp (0.5f, 600, 40, 100).mono (25).build());

    out.push_back (B ("baixo-drill-deslizante", "Baixo Drill Deslizante", "bass", DRILL,
                      "Seno com uma serra baixinha e glide longo: as escaladas do baixo do drill.")
                       .va (0, Wave::sine, -2).bypassFilter (0).va (1, Wave::saw, -14).lp (700, 10, true).keyTrack (50)
                       .amp (2, 600, 90, 120).mono (160).sat (soft, 8, 100).build());

    out.push_back (B ("baixo-phonk-rasgado", "Baixo Phonk Rasgado", "bass", PHONK,
                      "Serras no fuzz com corte em 900 Hz: o baixo cru do phonk.")
                       .va (0, Wave::saw, -8).unison (0, 2, 12, 30).lp (900, 15, true)
                       .sub (-6).amp (1, 400, 90, 80).mono (50).sat (fuzz, 20, 100).build());

    out.push_back (B ("baixo-fm-grave", "Baixo FM Grave", "bass", RNB | TRAP,
                      "Três operadores em série, brilho que cai em 400 ms.")
                       .fm (0, FmAlgo::threeSerial, 1, 30, 2, 20, 3, 0, 400, 15, 0, -6).noFilter()
                       .amp (1, 800, 70, 110).mono (40).velAmp (50).mod (Source::velocity, Dest::shape1, 25).build());

    out.push_back (B ("baixo-wavetable-sync", "Baixo Wavetable Sync", "bass", TRAP | DRILL,
                      "Tabela Sync aberta pelo envelope a cada nota, filtrada, com sub.")
                       .wt (0, sync, 20, -6).modEnv (1, 300, 10, 200).mod (Source::env3, Dest::shape1, 40)
                       .lp (800, 15, true).keyTrack (40).sub (-6)
                       .amp (1, 500, 90, 100).mono (60).build());

    out.push_back (B ("baixo-acustico-synth", "Baixo Acústico Synth", "bass", LOFI | RNB,
                      "Triângulo e seno com corpo curto e um estalo de corda no ataque.")
                       .va (0, Wave::triangle, -4).va (1, Wave::sine, -6).bypassFilter (1)
                       .lp (900, 5, true).keyTrack (40).velToCutoff (40)
                       .pitchDrop (1, 30).amp (1, 700, 20, 150, 60).mono (20).drift (15).build());

    out.push_back (B ("baixo-funk-ondulado", "Baixo Funk Ondulado", "bass", FUNK,
                      "Pulso com largura mexendo devagar e filtro com envelope.")
                       .va (0, Wave::pulse, -6, 35).lfo (0, LfoShape::sine, 0.5f, false).mod (Source::lfo1, Dest::shape1, 20)
                       .lp (700, 20, true).keyTrack (40).fenv (2.0f, 1, 250, 20, 100)
                       .sub (-8).amp (1, 400, 80, 90).mono (35).build());

    out.push_back (B ("baixo-rnb-redondo", "Baixo R&B Redondo", "bass", RNB,
                      "Seno com um triângulo uma oitava acima e glide curto.")
                       .va (0, Wave::sine, -2).va (1, Wave::triangle, -16).tune (1, 1).lp (1200, 0, false)
                       .amp (4, 500, 90, 160).mono (60).build());

    //==========================================================================
    // Leads
    out.push_back (B ("lead-ocarina", "Lead Ocarina", "lead", TRAP | LOFI,
                      "Triângulo com uma oitava acima bem baixa e um sopro: ocarina de barro.")
                       .va (0, Wave::triangle, -4).va (1, Wave::sine, -18).tune (1, 1).noise (2, 70, -30, 20)
                       .lp (2500, 0, false).amp (15, 300, 85, 200).mono (40).vibrato (20, 5.0f, 300)
                       .reverb (40, 1.8f, 18).build());

    out.push_back (B ("lead-supersaw-escuro", "Lead Supersaw Escuro", "lead", DRILL | TRAP,
                      "Seis serras abertas e fechadas no filtro: tensão sem estridência.")
                       .va (0, Wave::saw, -6).unison (0, 6, 24, 90).freePhase (0)
                       .lp (1800, 15, true).amp (5, 500, 90, 400).poly (10).reverb (55, 2.4f, 22).build());

    out.push_back (B ("lead-chiclete", "Lead Chiclete", "lead", POP | FUNK,
                      "Pulso elástico: glide em toda nota e vibrato rápido.")
                       .va (0, Wave::pulse, -6, 30).lp (5000, 5, false)
                       .amp (2, 300, 90, 150).mono (50, true, true).vibrato (18, 7.0f, 100).delay (d8, 20, 15).build());

    out.push_back (B ("lead-robotico", "Lead Robótico", "lead", PHONK | POP,
                      "Degraus digitais com clipper e glide curto.")
                       .wt (0, digital, 30, -6).lp (6000, 0, false)
                       .amp (1, 300, 90, 100).mono (30).sat (hard, 8, 100).build());

    out.push_back (B ("lead-fm-brilhante", "Lead FM Brilhante", "lead", POP | TRAP,
                      "Três operadores em série com brilho que fica: lead que atravessa a mixagem.")
                       .fm (0, FmAlgo::threeSerial, 2, 30, 3, 20, 3, 0, 1500, 60, 0, -6).noFilter()
                       .amp (2, 500, 90, 250).mono (40).vibrato (12, 5.5f, 400).reverb (40, 1.6f, 15).build());

    out.push_back (B ("lead-agudo-drill", "Lead Agudo Drill", "lead", DRILL,
                      "Serra e pulso sem grave, eco em tercinas: o lead tenso do drill.")
                       .va (0, Wave::saw, -6).unison (0, 2, 10, 50).va (1, Wave::pulse, -10, 40)
                       .filter (FilterMode::highpass, 400, 5, false)
                       .amp (2, 300, 90, 150).mono (70).sat (soft, 6, 100).delay (d8T, 30, 20).build());

    out.push_back (B ("lead-funk-sintetico", "Lead Funk Sintético", "lead", FUNK,
                      "Quadrada e serra uma oitava acima, filtro que estala e eco em 1/16.")
                       .va (0, Wave::pulse, -6, 50).va (1, Wave::saw, -12).tune (1, 1)
                       .lp (1500, 15, true).fenv (2.0f, 0.5f, 150, 40, 100)
                       .amp (1, 300, 90, 120).mono (40).delay (d16, 25, 15).build());

    out.push_back (B ("lead-lofi-seno", "Lead Lo-fi Seno", "lead", LOFI | RNB,
                      "Seno e triângulo oscilando como fita velha.")
                       .va (0, Wave::sine, -4).va (1, Wave::triangle, -10).lp (4000, 0, false)
                       .lfo (0, LfoShape::sine, 0.6f, false).mod (Source::lfo1, Dest::pitch, 0.35f)
                       .amp (5, 300, 90, 200).mono (45).drift (50).sat (tape, 8, 100).build());

    out.push_back (B ("lead-suave-rnb", "Lead Suave R&B", "lead", RNB,
                      "Triângulo com uma serra baixinha, entrada macia e vibrato lento.")
                       .va (0, Wave::triangle, -4).va (1, Wave::saw, -12).lp (1500, 5, true)
                       .amp (30, 300, 90, 300).mono (60).vibrato (12, 4.8f, 500).reverb (45, 2.0f, 20).build());

    out.push_back (B ("lead-vogal-aberta", "Lead Vogal Aberta", "lead", POP,
                      "Vogais andando a cada compasso, três vozes abertas.")
                       .wt (0, vogais, 30, -6).unison (0, 3, 10, 70)
                       .lfoSync (0, LfoShape::triangle, bar1, false).mod (Source::lfo1, Dest::shape1, 30)
                       .lp (6000, 0, false).amp (5, 400, 90, 300).mono (50).reverb (45, 1.8f, 18).build());

    out.push_back (B ("lead-metalico-phonk", "Lead Metálico Phonk", "lead", PHONK,
                      "FM com realimentação e clipper: metal áspero.")
                       .fm (0, FmAlgo::twoOp, 2.5f, 40, 2, 0, 3, 0, 800, 40, 30, -6).noFilter()
                       .amp (1, 400, 85, 150).mono (40).sat (hard, 10, 100).build());

    out.push_back (B ("lead-pulso-trap", "Lead Pulso Trap", "lead", TRAP,
                      "Três pulsos em 25% com vibrato, eco pontuado e reverb.")
                       .va (0, Wave::pulse, -6, 25).unison (0, 3, 10, 60)
                       .lp (5000, 5, false).amp (3, 400, 90, 250).mono (55).vibrato (14, 5.5f, 350)
                       .delay (d8Dot, 25, 18).reverb (45, 1.8f, 18).build());

    //==========================================================================
    // Pads
    out.push_back (B ("pad-fm-sombrio", "Pad FM Sombrio", "pad", DRILL | TRAP,
                      "FM com modulador meia oitava abaixo: timbre grave e inquieto.")
                       .fm (0, FmAlgo::twoOp, 0.5f, 25, 2, 0, 3, 0, 4000, 70, 0, -6).unison (0, 2, 10, 70).freePhase (0)
                       .lp (1500, 10, true).amp (900, 1000, 90, 2000).reverb (70, 4.0f, 35).build());

    out.push_back (B ("pad-coral-largo", "Pad Coral Largo", "pad", RNB | POP,
                      "Seis vozes de vogal \"a\" bem abertas.")
                       .wt (0, vogais, 20, -6).unison (0, 6, 18, 100).freePhase (0)
                       .lp (4000, 0, false).amp (600, 800, 90, 1500).reverb (65, 3.2f, 30).build());

    out.push_back (B ("pad-varredura", "Pad Varredura", "pad", POP | TRAP,
                      "Uma faixa de harmônicos que passeia devagar.")
                       .wt (0, varredura, 30, -6).unison (0, 3, 12, 80).freePhase (0)
                       .lfo (0, LfoShape::sine, 0.08f, false).mod (Source::lfo1, Dest::shape1, 50)
                       .lp (8000, 0, false).amp (500, 800, 90, 1600).reverb (60, 3.0f, 28).build());

    out.push_back (B ("pad-orgao-lofi", "Pad Órgão Lo-fi", "pad", LOFI | RAP,
                      "Órgão macio com oscilação de fita e saturação.")
                       .wt (0, orgao, 40, -6).unison (0, 2, 8, 60).freePhase (0)
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.35f)
                       .lp (3000, 0, false).amp (300, 600, 90, 900).drift (40).sat (tape, 8, 100).build());

    out.push_back (B ("pad-sinos", "Pad Sinos", "pad", TRAP | RNB,
                      "Tabela de sino aberta, eco e reverb grandes.")
                       .wt (0, sino, 40, -6).unison (0, 3, 10, 80).freePhase (0)
                       .lp (7000, 0, false).amp (400, 1000, 85, 1800).delay (d4Dot, 35, 18).reverb (70, 4.0f, 35).build());

    out.push_back (B ("pad-analogico-quente", "Pad Analógico Quente", "pad", RNB | LOFI | RAP,
                      "Triângulos e uma serra fechados: calor sem brilho.")
                       .va (0, Wave::triangle, -4).unison (0, 4, 14, 80).freePhase (0)
                       .va (1, Wave::saw, -12).freePhase (1)
                       .lp (1200, 5, true).amp (500, 800, 90, 1400).drift (30).reverb (55, 2.6f, 25).build());

    out.push_back (B ("pad-pwm", "Pad PWM", "pad", POP | FUNK,
                      "Pulsos com largura mexendo (PWM): o pad clássico que se mexe sozinho.")
                       .va (0, Wave::pulse, -6, 40).unison (0, 3, 10, 80).freePhase (0)
                       .lfo (0, LfoShape::triangle, 0.4f, false).mod (Source::lfo1, Dest::shape1, 30)
                       .lp (3500, 5, false).amp (300, 600, 90, 900).reverb (50, 2.2f, 22).build());

    out.push_back (B ("pad-cristalino", "Pad Cristalino", "pad", POP,
                      "Vidro aberto com um FM agudo brilhando por cima.")
                       .wt (0, vidro, 60, -6).unison (0, 4, 12, 90).freePhase (0)
                       .fm (1, FmAlgo::twoOp, 4, 15, 2, 0, 3, 0, 2000, 40, 0, -16).freePhase (1)
                       .lp (9000, 0, false).amp (500, 800, 90, 1500).reverb (65, 3.2f, 30).build());

    out.push_back (B ("pad-neblina", "Pad Neblina", "pad", DRILL | PHONK,
                      "Serras fechadas com ar por cima e reverb enorme.")
                       .va (0, Wave::saw, -6).unison (0, 3, 15, 80).freePhase (0).noise (1, 40, -18, 90)
                       .lp (700, 10, true).amp (1000, 1000, 90, 2500).reverb (80, 5.0f, 40).build());

    out.push_back (B ("pad-brilho-funk", "Pad Brilho Funk", "pad", FUNK | POP,
                      "Serras abertas sem grave, com eco em 1/8: brilho por cima do batidão.")
                       .va (0, Wave::saw, -6).unison (0, 5, 16, 100).freePhase (0)
                       .filter (FilterMode::highpass, 300, 5, false)
                       .amp (60, 500, 85, 600).delay (d8, 25, 15).reverb (45, 2.0f, 18).build());

    //==========================================================================
    // Plucks
    out.push_back (B ("pluck-marimba-trap", "Pluck Marimba Trap", "pluck", TRAP,
                      "Madeira afinada bem curta: o toque de marimba das melodias de trap.")
                       .fm (0, FmAlgo::twoOp, 4, 25, 2, 0, 3, 0, 90, 0, 0, -6).va (1, Wave::sine, -6).noFilter()
                       .amp (0.5f, 400, 0, 200, 75).reverb (40, 1.6f, 18).build());

    out.push_back (B ("pluck-fm-vitreo", "Pluck FM Vítreo", "pluck", POP | RNB,
                      "FM de razão 7: um toque de vidro fino.")
                       .fm (0, FmAlgo::twoOp, 7, 20, 2, 0, 3, 0, 200, 0, 0, -6).noFilter()
                       .amp (0.5f, 600, 0, 350, 75).reverb (50, 2.0f, 22).build());

    out.push_back (B ("pluck-supersaw", "Pluck Supersaw", "pluck", POP | TRAP,
                      "Cinco serras abertas num golpe de filtro.")
                       .va (0, Wave::saw, -6).unison (0, 5, 20, 100).freePhase (0)
                       .lp (700, 10, true).keyTrack (40).fenv (4.0f, 0.5f, 200, 0, 200)
                       .amp (0.5f, 500, 0, 300, 70).reverb (50, 2.0f, 20).build());

    out.push_back (B ("pluck-agogo", "Pluck Agogô Synth", "pluck", FUNK | PHONK,
                      "Toque metálico curto e inarmônico, como um agogô eletrônico.")
                       .fm (0, FmAlgo::twoOp, 3.3f, 35, 2, 0, 3, 0, 150, 0, 0, -6)
                       .filter (FilterMode::bandpass, 1400, 15, false)
                       .amp (0.5f, 250, 0, 120, 80).build());

    out.push_back (B ("pluck-violao", "Pluck Violão Synth", "pluck", LOFI | RNB | RAP,
                      "Serra filtrada e triângulo, com um leve puxão de corda no ataque.")
                       .va (0, Wave::saw, -8).va (1, Wave::triangle, -6)
                       .lp (900, 5, true).keyTrack (60).fenv (2.5f, 0.5f, 300, 0, 300)
                       .pitchDrop (0.5f, 40).amp (0.5f, 1200, 0, 500, 65).drift (15).build());

    out.push_back (B ("pluck-drill-sino", "Pluck Drill Sino", "pluck", DRILL,
                      "Sino curto e fechado num reverb grande.")
                       .fm (0, FmAlgo::twoOp, 3.5f, 30, 2, 0, 3, 0, 200, 0, 0, -6).lp (2000, 5, false)
                       .amp (0.5f, 500, 0, 400, 70).reverb (65, 2.8f, 28).build());

    out.push_back (B ("pluck-pop-brilhante", "Pluck Pop Brilhante", "pluck", POP,
                      "Pulso e serra com filtro rápido e eco em 1/8.")
                       .va (0, Wave::pulse, -8, 35).va (1, Wave::saw, -8)
                       .lp (1200, 10, true).fenv (3.5f, 0.5f, 120, 0, 120)
                       .amp (0.5f, 350, 0, 200, 70).delay (d8, 25, 18).build());

    out.push_back (B ("pluck-phonk-seco", "Pluck Phonk Seco", "pluck", PHONK,
                      "Quadrada em faixa estreita, curtíssima, com clipper.")
                       .va (0, Wave::pulse, -6, 50).filter (FilterMode::bandpass, 900, 25, false)
                       .amp (0.5f, 180, 0, 80, 80).sat (hard, 8, 100).build());

    out.push_back (B ("pluck-harpa-escura", "Pluck Harpa Escura", "pluck", DRILL | RNB,
                      "Triângulo que abre pouco e soa longo num reverb escuro.")
                       .va (0, Wave::triangle, -4).lp (600, 5, true).keyTrack (60).fenv (2.0f, 0.5f, 400, 0, 600)
                       .amp (0.5f, 1600, 0, 1000, 65).reverb (60, 3.0f, 28).build());

    out.push_back (B ("pluck-piano-eletrico", "Pluck Piano Elétrico", "pluck", RNB | RAP,
                      "O piano elétrico de FM em versão curta, para acordes picados.")
                       .fm (0, FmAlgo::twoOp, 1, 30, 2, 0, 3, 0, 400, 0, 0, -6)
                       .fm (1, FmAlgo::twoOp, 14, 18, 2, 0, 3, 0, 120, 0, 0, -18).noFilter()
                       .amp (0.5f, 700, 0, 300, 70).velAmp (65).build());

    //==========================================================================
    // Keys
    out.push_back (B ("keys-ep-brilhante", "Piano Elétrico Brilhante", "keys", POP | RNB,
                      "Mais lâmina e mais brilho: piano elétrico que aparece na mixagem.")
                       .fm (0, FmAlgo::twoOp, 1, 45, 2, 0, 3, 0, 1300, 15, 0, -6)
                       .fm (1, FmAlgo::twoOp, 14, 30, 2, 0, 3, 0, 300, 0, 0, -14).noFilter()
                       .amp (0.5f, 2800, 0, 400, 65).velAmp (70).mod (Source::velocity, Dest::shape1, 40)
                       .reverb (35, 1.5f, 12).build());

    out.push_back (B ("keys-orgao-funk", "Órgão Funk", "keys", FUNK | RAP,
                      "Registro aberto com tremolo rápido e um estalo no ataque.")
                       .wt (0, orgao, 85, -6).noFilter()
                       .fm (1, FmAlgo::twoOp, 3, 20, 2, 0, 3, 0, 60, 0, 0, -18)
                       .lfo (0, LfoShape::sine, 6.8f, false).mod (Source::lfo1, Dest::volume, 10)
                       .amp (1, 100, 100, 80).build());

    out.push_back (B ("keys-vibrafone", "Vibrafone Synth", "keys", RNB | LOFI,
                      "Metal macio com tremolo, como um vibrafone.")
                       .fm (0, FmAlgo::twoOp, 4, 18, 2, 0, 3, 0, 800, 5, 0, -6).va (1, Wave::sine, -6).noFilter()
                       .lfo (0, LfoShape::sine, 5.0f, false).mod (Source::lfo1, Dest::volume, 15)
                       .amp (0.5f, 3000, 0, 700, 70).reverb (45, 2.0f, 18).build());

    out.push_back (B ("keys-cravo", "Cravo Synth", "keys", POP | TRAP,
                      "Pulso fino e serra sem grave, ataque beliscado.")
                       .va (0, Wave::pulse, -6, 15).va (1, Wave::saw, -10)
                       .filter (FilterMode::highpass, 500, 5, false)
                       .amp (0.5f, 1200, 0, 300, 70).reverb (35, 1.4f, 12).build());

    out.push_back (B ("keys-piano-lofi", "Piano Lo-fi Abafado", "keys", LOFI,
                      "Triângulo com um FM 1:2, fechado, com fita e oscilação.")
                       .va (0, Wave::triangle, -4).fm (1, FmAlgo::twoOp, 2, 12, 2, 0, 3, 0, 600, 5, 0, -12)
                       .lp (1500, 0, false)
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.3f)
                       .amp (0.5f, 1800, 10, 400).drift (45).sat (tape, 9, 100).build());

    out.push_back (B ("keys-sombrio", "Keys Sombrio", "keys", TRAP | DRILL | PHONK,
                      "FM 1:1 e um pulso fechados, cauda longa.")
                       .fm (0, FmAlgo::twoOp, 1, 20, 2, 0, 3, 0, 1000, 20, 0, -6).va (1, Wave::pulse, -12, 40)
                       .lp (1200, 5, true).amp (1, 2500, 10, 800).reverb (60, 2.6f, 25).build());

    out.push_back (B ("keys-cristal-rnb", "Keys Cristal R&B", "keys", RNB,
                      "FM 1:3 delicado sobre um seno.")
                       .fm (0, FmAlgo::twoOp, 3, 15, 2, 0, 3, 0, 1200, 10, 0, -6).va (1, Wave::sine, -8).noFilter()
                       .amp (0.5f, 2600, 0, 600, 65).reverb (45, 2.0f, 18).build());

    out.push_back (B ("keys-tambor-aco", "Tambor de Aço Synth", "keys", FUNK | POP,
                      "Toque redondo e metálico, como um tambor de aço afinado.")
                       .fm (0, FmAlgo::twoOp, 2, 30, 2, 0, 3, 0, 250, 5, 0, -6).va (1, Wave::sine, -8)
                       .filter (FilterMode::bandpass, 1800, 5, false)
                       .amp (0.5f, 900, 0, 300, 70).reverb (35, 1.5f, 15).build());

    //==========================================================================
    // Sinos
    out.push_back (B ("sino-cristal-trap", "Sino Cristal Trap", "bell", TRAP,
                      "Sino agudo e limpo, com eco pontuado e sala.")
                       .fm (0, FmAlgo::twoOp, 5.4f, 25, 2, 0, 3, 0, 700, 10, 0, -6).va (1, Wave::sine, -10).noFilter()
                       .amp (0.5f, 2200, 0, 800, 70).delay (d8Dot, 30, 18).reverb (55, 2.4f, 22).build());

    out.push_back (B ("sino-grande", "Sino Grande", "bell", DRILL | POP,
                      "Três operadores inarmônicos e cauda longa: sino de torre.")
                       .fm (0, FmAlgo::threeSerial, 1.4f, 40, 2.8f, 20, 3, 0, 3000, 25, 0, -6).noFilter()
                       .amp (0.5f, 6000, 0, 2000, 70).reverb (65, 3.5f, 28).build());

    out.push_back (B ("agogo-funk", "Agogô Funk", "bell", FUNK,
                      "Sino curto em faixa média: marca o contratempo do funk.")
                       .fm (0, FmAlgo::twoOp, 1.48f, 45, 2, 0, 3, 0, 150, 5, 0, -6)
                       .filter (FilterMode::bandpass, 1200, 20, false)
                       .amp (0.5f, 300, 0, 120, 80).build());

    out.push_back (B ("celesta-lofi", "Celesta Lo-fi", "bell", LOFI | RAP,
                      "Celesta gravada em fita: deriva e agudo cortado.")
                       .fm (0, FmAlgo::twoOp, 4, 20, 2, 0, 3, 0, 300, 5, 0, -6).noFilter()
                       .amp (0.5f, 1000, 0, 500, 70).drift (40).sat (tape, 8, 100).eq (40, 0, 0, 1000, -6).build());

    out.push_back (B ("sino-suave-rnb", "Sino Suave R&B", "bell", RNB,
                      "FM 1:2 discreto com cauda longa.")
                       .fm (0, FmAlgo::twoOp, 2, 15, 2, 0, 3, 0, 1500, 10, 0, -6).va (1, Wave::sine, -8).noFilter()
                       .amp (0.5f, 3500, 0, 1200, 65).reverb (55, 2.6f, 25).build());

    out.push_back (B ("sino-phonk-metalico", "Sino Phonk Metálico", "bell", PHONK,
                      "Razão 3,7 com realimentação e clipper: sino que arranha.")
                       .fm (0, FmAlgo::twoOp, 3.7f, 60, 2, 0, 3, 0, 500, 10, 25, -6).noFilter()
                       .amp (0.5f, 900, 0, 300, 75).sat (hard, 8, 100).build());

    //==========================================================================
    // Metais
    out.push_back (B ("naipe-funk", "Naipe Funk", "brass", FUNK,
                      "Golpe curto de metais, para respostas no refrão.")
                       .va (0, Wave::saw, -6).unison (0, 3, 10, 70).va (1, Wave::pulse, -12, 40)
                       .lp (1000, 10, true).fenv (2.5f, 3, 180, 30, 120)
                       .amp (3, 300, 40, 120).build());

    out.push_back (B ("metais-sombrios", "Metais Sombrios", "brass", DRILL,
                      "Metais graves e lentos, fechados.")
                       .va (0, Wave::saw, -6).unison (0, 3, 12, 70).lp (500, 10, true).fenv (1.5f, 200, 800, 60, 500)
                       .amp (200, 600, 90, 600).reverb (60, 3.0f, 25).build());

    out.push_back (B ("brass-pop-brilhante", "Brass Pop Brilhante", "brass", POP,
                      "Quatro serras com o filtro abrindo bem: metais de refrão.")
                       .va (0, Wave::saw, -6).unison (0, 4, 12, 80).lp (1500, 10, true).fenv (2.0f, 30, 400, 60, 300)
                       .amp (30, 300, 90, 300).reverb (40, 1.8f, 15).build());

    out.push_back (B ("trombone-synth", "Trombone Synth", "brass", RAP | LOFI,
                      "Serra mono grave com glide e vibrato que entra devagar.")
                       .va (0, Wave::saw, -6).tune (0, -1).lp (700, 10, true).fenv (1.5f, 40, 400, 50, 200)
                       .amp (40, 300, 90, 200).mono (60).vibrato (15, 5.0f, 400).build());

    out.push_back (B ("sopros-suaves", "Sopros Suaves", "brass", RNB | LOFI,
                      "Triângulo e serra macios, filtro que abre pouco.")
                       .va (0, Wave::triangle, -4).va (1, Wave::saw, -10).lp (900, 5, true).fenv (1.0f, 60, 400, 60, 300)
                       .amp (60, 400, 85, 400).reverb (45, 2.0f, 18).build());

    //==========================================================================
    // Cordas
    out.push_back (B ("cordas-graves", "Cordas Graves", "strings", DRILL | TRAP,
                      "Naipe uma oitava abaixo, fechado: violoncelos sintéticos.")
                       .va (0, Wave::saw, -6).unison (0, 4, 12, 80).freePhase (0).tune (0, -1)
                       .lp (900, 5, true).amp (300, 600, 90, 900).reverb (60, 2.8f, 25).build());

    out.push_back (B ("cordas-tremolo", "Cordas Tremolo", "strings", TRAP | DRILL,
                      "Cordas tremendo em 1/16: tensão de cena de filme.")
                       .va (0, Wave::saw, -6).unison (0, 4, 14, 80).freePhase (0)
                       .lfoSync (0, LfoShape::rampDown, sixteenth, false).mod (Source::lfo1, Dest::volume, 60)
                       .lp (2500, 5, false).amp (100, 500, 90, 700).reverb (60, 2.8f, 25).build());

    out.push_back (B ("cordas-sedosas", "Cordas Sedosas", "strings", RNB | POP,
                      "Seis serras e um triângulo, entrada lenta e sala grande.")
                       .va (0, Wave::saw, -6).unison (0, 6, 18, 100).freePhase (0).va (1, Wave::triangle, -10).freePhase (1)
                       .lp (3500, 0, false).amp (400, 800, 90, 1200).vibrato (8, 5.0f, 600).reverb (65, 3.2f, 30).build());

    out.push_back (B ("cordas-funk-disco", "Cordas Funk Disco", "strings", FUNK | POP,
                      "Cordas sem grave e com ataque rápido, para subidas e frases.")
                       .va (0, Wave::saw, -6).unison (0, 4, 14, 90).freePhase (0)
                       .filter (FilterMode::highpass, 200, 5, false)
                       .amp (20, 400, 85, 400).delay (d8, 20, 12).reverb (45, 2.0f, 18).build());

    out.push_back (B ("cordas-antigas", "Cordas Antigas", "strings", LOFI | RAP,
                      "Naipe de disco velho: fita, oscilação e agudo cortado.")
                       .va (0, Wave::saw, -6).unison (0, 3, 10, 70).freePhase (0)
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.35f)
                       .lp (1800, 0, false).amp (250, 600, 90, 800).drift (45).sat (tape, 10, 100).eq (60, 0, 0, 1000, -7).build());

    //==========================================================================
    // Vozes
    out.push_back (B ("coro-grave", "Coro Grave", "vox", DRILL | TRAP,
                      "Vogal \"u\" uma oitava abaixo, em quatro vozes.")
                       .wt (0, vozEscura, 0, -6).unison (0, 4, 10, 80).freePhase (0).tune (0, -1)
                       .lp (2000, 0, false).amp (400, 800, 90, 1200).reverb (65, 3.5f, 32).build());

    out.push_back (B ("coro-brilhante", "Coro Brilhante", "vox", POP,
                      "Vogal \"a\" aberta em cinco vozes, sem grave.")
                       .wt (0, vogais, 10, -6).unison (0, 5, 14, 90).freePhase (0)
                       .filter (FilterMode::highpass, 250, 5, false)
                       .amp (200, 600, 90, 900).vibrato (10, 5.0f, 500).reverb (60, 2.8f, 28).build());

    out.push_back (B ("vox-wah-funk", "Vox Wah Funk", "vox", FUNK,
                      "Vogal que abre rápido a cada nota: wah de boca.")
                       .wt (0, vogais, 60, -4).modEnv (0.5f, 180, 0, 150).mod (Source::env3, Dest::shape1, -55)
                       .lp (4000, 5, false).amp (1, 300, 80, 100).mono (30).build());

    out.push_back (B ("vox-iii", "Vox Iii", "vox", POP | TRAP,
                      "Vogal \"i\" aguda com eco: voz sintética fina.")
                       .wt (0, vogais, 50, -6).unison (0, 2, 8, 60).freePhase (0)
                       .lp (7000, 0, false).amp (30, 500, 90, 600).vibrato (12, 5.5f, 300).delay (d8Dot, 30, 18).build());

    out.push_back (B ("coro-lofi", "Coro Lo-fi", "vox", LOFI | RNB,
                      "Coro escuro gravado em fita.")
                       .wt (0, vozEscura, 30, -6).unison (0, 3, 10, 70).freePhase (0)
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.35f)
                       .lp (2500, 0, false).amp (300, 600, 90, 900).drift (40).sat (tape, 8, 100).build());

    //==========================================================================
    // Arp / Seq
    out.push_back (B ("arp-fm-sino", "Arp FM Sino", "arp", POP | TRAP,
                      "Sino curtíssimo de FM com eco em 1/16.")
                       .fm (0, FmAlgo::twoOp, 4, 25, 2, 0, 3, 0, 100, 0, 0, -6).noFilter()
                       .amp (0.5f, 220, 0, 150, 75).delay (d16, 30, 18).build());

    out.push_back (B ("seq-portao-escuro", "Seq Portão Escuro", "arp", DRILL,
                      "Serras fechadas cortadas em tercinas de 1/16.")
                       .va (0, Wave::saw, -6).unison (0, 3, 12, 70).freePhase (0).lp (900, 15, true)
                       .lfoSync (0, LfoShape::rampDown, sixteenthT, false).mod (Source::lfo1, Dest::volume, 90)
                       .amp (5, 300, 100, 300).reverb (55, 2.4f, 22).build());

    out.push_back (B ("arp-funk-curto", "Arp Funk Curto", "arp", FUNK,
                      "Pulso estalado, curtíssimo, para linhas rápidas.")
                       .va (0, Wave::pulse, -6, 25).lp (1500, 15, true).fenv (2.5f, 0.5f, 80, 0, 60)
                       .amp (0.5f, 150, 0, 60, 75).build());

    out.push_back (B ("arp-caixinha-lofi", "Arp Caixinha Lo-fi", "arp", LOFI | RAP,
                      "Lâmina de FM curta com fita e deriva.")
                       .fm (0, FmAlgo::twoOp, 5, 18, 2, 0, 3, 0, 150, 0, 0, -6).noFilter()
                       .amp (0.5f, 400, 0, 200, 75).drift (35).sat (tape, 6, 100).eq (40, 0, 0, 1000, -5).build());

    out.push_back (B ("seq-acido-escuro", "Seq Ácido Escuro", "arp", PHONK | DRILL,
                      "Serra com ressonância altíssima e corte baixo.")
                       .va (0, Wave::saw, -6).lp (300, 80, false, 30).keyTrack (30).fenv (2.0f, 0.5f, 120, 0, 80)
                       .amp (0.5f, 200, 50, 80).sat (tube, 10, 100).build());

    out.push_back (B ("arp-cristal-trap", "Arp Cristal Trap", "arp", TRAP | RNB,
                      "Vidro curto com eco em 1/16 e reverb.")
                       .wt (0, vidro, 50, -6).noFilter()
                       .amp (0.5f, 250, 0, 200, 70).delay (d16, 35, 22).reverb (45, 1.8f, 18).build());

    //==========================================================================
    // FX
    out.push_back (B ("fx-subida-rapida", "Subida Rápida", "fx", TRAP | POP,
                      "Serras que sobem duas oitavas em um segundo.")
                       .va (0, Wave::saw, -6).unison (0, 3, 20, 80)
                       .modEnv (1000, 500, 100, 300, 30).mod (Source::env3, Dest::pitch, 100)
                       .lp (2000, 10, true).fenv (3.0f, 1000, 500, 100, 300)
                       .amp (100, 500, 100, 300).reverb (50, 2.0f, 22).build());

    out.push_back (B ("fx-tiro", "Tiro", "fx", FUNK | DRILL,
                      "Estouro de ruído com um seno despencando: o tiro dos efeitos do baile.")
                       .va (0, Wave::sine, -2).noise (1, 70, -6, 80).noFilter()
                       .modEnv (0.5f, 70, 0, 70, 80).mod (Source::env3, Dest::pitch, 100).mod (Source::env3, Dest::pitch, 50)
                       .amp (0.5f, 350, 0, 150, 75).reverb (50, 1.8f, 22).build());

    out.push_back (B ("fx-sirene-lenta", "Sirene Lenta", "fx", DRILL | TRAP,
                      "Seno subindo e descendo devagar quase uma oitava.")
                       .va (0, Wave::sine, -4).noFilter()
                       .lfo (0, LfoShape::sine, 0.3f, true).mod (Source::lfo1, Dest::pitch, 40)
                       .amp (50, 100, 100, 400).reverb (55, 2.5f, 25).build());

    out.push_back (B ("fx-disco-voador", "Disco Voador", "fx", PHONK | POP,
                      "FM com vibrato rápido e eco: som de ficção científica.")
                       .fm (0, FmAlgo::twoOp, 1.5f, 40, 2, 0, 3, 0, 2000, 60, 10, -6).noFilter()
                       .lfo (0, LfoShape::sine, 6, true).mod (Source::lfo1, Dest::pitch, 8)
                       .amp (100, 500, 100, 500).delay (d8, 40, 25).build());

    out.push_back (B ("fx-explosao", "Explosão Grave", "fx", TRAP,
                      "Seno caindo três oitavas com ruído escuro e sala enorme.")
                       .va (0, Wave::sine, 0).noise (1, 25, -6, 90).noFilter()
                       .modEnv (0.5f, 1200, 0, 600, 60).mod (Source::env3, Dest::pitch, 100).mod (Source::env3, Dest::pitch, 50)
                       .amp (0.5f, 2500, 0, 1000).sat (tape, 8, 100).reverb (80, 5.0f, 35).build());

    //==========================================================================
    // Texturas
    out.push_back (B ("textura-mar", "Textura Mar", "texture", LOFI | RNB,
                      "Ruído escuro que cresce e recua como onda.")
                       .noise (0, 35, -6, 100).lp (1500, 0, false)
                       .lfo (0, LfoShape::sine, 0.1f, false).mod (Source::lfo1, Dest::volume, 80).mod (Source::lfo1, Dest::cutoff, 10)
                       .amp (1500, 500, 100, 2500).reverb (60, 3.0f, 30).build());

    out.push_back (B ("textura-fita", "Textura Fita Chiando", "texture", LOFI,
                      "Chiado claro de fita, baixinho e oscilando.")
                       .noise (0, 85, -12, 70).noFilter()
                       .lfo (0, LfoShape::smoothRandom, 4, false).mod (Source::lfo1, Dest::volume, 30)
                       .amp (300, 500, 100, 800).build());

    out.push_back (B ("textura-cosmica", "Textura Cósmica", "texture", TRAP | POP,
                      "Harmônicos passeando ao acaso, com eco e reverb enormes.")
                       .wt (0, varredura, 50, -6).unison (0, 3, 15, 100).freePhase (0)
                       .lfo (0, LfoShape::smoothRandom, 0.3f, false).mod (Source::lfo1, Dest::shape1, 50)
                       .noFilter().amp (1500, 1000, 90, 3000).delay (d4Dot, 45, 25).reverb (85, 6.0f, 45).build());

    out.push_back (B ("textura-sombra", "Textura Sombra", "texture", DRILL | PHONK,
                      "FM inarmônico grave com ar escuro.")
                       .fm (0, FmAlgo::twoOp, 1.27f, 40, 2, 0, 3, 0, 5000, 60, 10, -6).tune (0, -1).freePhase (0)
                       .noise (1, 25, -16, 80).lp (1200, 15, true)
                       .amp (1500, 1000, 90, 2500).reverb (75, 5.0f, 35).build());
}

} // namespace whynot::synth::presets
