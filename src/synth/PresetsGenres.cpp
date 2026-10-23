#include "PresetBuilder.h"

// Terceira leva: reforça os gêneros que tinham menos timbres (rap e boom bap, phonk, funk BR,
// lo-fi e drill), em todas as categorias. Mesma régua: nivelados e testados no Variar.

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

    /** Duas quadradas em intervalo torto (a "campana" do phonk), filtradas em faixa. */
    B campana (const char* id, const char* name, const char* category, std::uint32_t genres, const char* description,
               float bandHz, float decayMs)
    {
        B b (id, name, category, genres, description);
        b.va (0, Wave::pulse, -6, 50).va (1, Wave::pulse, -6, 50).tune (1, 0, 7, -32)
         .filter (FilterMode::bandpass, bandHz, 30, false)
         .amp (0.5f, decayMs, 0, 120, 80).sat (hard, 6, 100);
        return b;
    }
}

void addGenreWave (std::vector<Preset>& out)
{
    //==========================================================================
    // 808
    out.push_back (base808 ("808-rap-classico", "808 Rap Clássico", RAP | LOFI,
                            "Médio e firme, com um pouco de fita: o 808 das batidas de rap.", 1400, 6, 40, 60)
                       .sat (tape, 8, 100).build());

    out.push_back (base808 ("808-memphis", "808 Memphis Sujo", PHONK | RAP,
                            "Fuzz e grave reforçado, glide em toda nota: o 808 cru das fitas de Memphis.", 2000, 8, 40, 120)
                       .mono (120, true, true).sat (fuzz, 16, 100).eq (25, 3, 0, 1000, -2).build());

    out.push_back (base808 ("808-grave-seco-funk", "808 Grave Seco Funk", FUNK,
                            "Curtíssimo, com queda forte e clipper: o grave socado do funk.", 350, 18, 20, 30)
                       .sat (hard, 14, 100).build());

    out.push_back (base808 ("808-drill-escorregadio", "808 Drill Escorregadio", DRILL,
                            "Glide em toda nota e cauda longa: o 808 que escorrega entre as notas.", 3000, 4, 40, 180)
                       .mono (180, true, true).sat (tape, 9, 100).build());

    out.push_back (B ("808-trap-melodico", "808 Trap Melódico", "808", TRAP | RNB,
                      "Segura um pouco enquanto a nota estiver apertada: bom para linhas de 808 cantadas.")
                       .va (0, Wave::sine, 0).noFilter()
                       .amp (0.5f, 2200, 30, 200, 60).pitchDrop (4, 45).mono (90).drift (0).velAmp (35)
                       .sat (tube, 6, 100).build());

    out.push_back (B ("808-bumbo-afinado", "808 Bumbo Afinado", "808", RAP | LOFI,
                      "Triângulo e seno com queda grande: entre bumbo e 808, afinado.")
                       .va (0, Wave::sine, 0).va (1, Wave::triangle, -10).noFilter()
                       .amp (0.5f, 700, 0, 120, 65).pitchDrop (18, 80).mono (40).drift (0).velAmp (40)
                       .sat (tape, 8, 100).build());

    out.push_back (base808 ("808-pop-curto", "808 Pop Curto", POP,
                            "Seno limpo e curto que não briga com o vocal.", 1200, 4, 40, 50)
                       .build());

    out.push_back (base808 ("808-phonk-longo", "808 Phonk Longo", PHONK,
                            "Fuzz pesado, cauda longa e glide em toda nota.", 4500, 6, 45, 160)
                       .mono (160, true, true).sat (fuzz, 20, 100).build());

    //==========================================================================
    // Baixos
    out.push_back (B ("baixo-jazz-synth", "Baixo Jazz Synth", "bass", RAP | LOFI,
                      "Triângulo e seno curtos com estalo de corda: baixo de sample de jazz.")
                       .va (0, Wave::triangle, -4).va (1, Wave::sine, -6).bypassFilter (1)
                       .lp (800, 5, true).keyTrack (40).velToCutoff (50)
                       .pitchDrop (0.7f, 30).amp (1, 600, 15, 120, 60).mono (20).drift (20).sat (tape, 6, 100).build());

    out.push_back (B ("baixo-quadrado-phonk", "Baixo Quadrado Phonk", "bass", PHONK,
                      "Quadrada fechada no fuzz: grave áspero e baixo.")
                       .va (0, Wave::pulse, -6, 50).lp (600, 10, true).keyTrack (40)
                       .sub (-6).amp (1, 400, 90, 80).mono (60).sat (fuzz, 12, 100).build());

    out.push_back (B ("baixo-funk-batuque", "Baixo Funk Batuque", "bass", FUNK,
                      "Pulso curto que morre rápido, com sub: baixo que bate junto do tamborzão.")
                       .va (0, Wave::pulse, -6, 40).lp (700, 15, true).keyTrack (40).fenv (2.0f, 0.5f, 120, 0, 80)
                       .sub (-6).amp (0.5f, 300, 0, 80, 70).mono (20).build());

    out.push_back (B ("baixo-grosso-rap", "Baixo Grosso Rap", "bass", RAP | TRAP,
                      "Duas serras num filtro de 24 dB com drive: grosso e escuro.")
                       .va (0, Wave::saw, -6).va (1, Wave::saw, -6).tune (1, 0, 0, -6)
                       .lp (400, 15, true, 30).keyTrack (40)
                       .amp (2, 500, 90, 100).mono (40).build());

    out.push_back (B ("baixo-drill-picado", "Baixo Drill Picado", "bass", DRILL,
                      "Seno e quadrada em golpes curtos, para padrões picados.")
                       .va (0, Wave::sine, -2).bypassFilter (0).va (1, Wave::pulse, -14, 50)
                       .lp (500, 10, true).keyTrack (50)
                       .amp (0.5f, 250, 0, 80, 70).mono (60).build());

    out.push_back (B ("baixo-dub-lofi", "Baixo Dub Lo-fi", "bass", LOFI | RAP,
                      "Seno e triângulo fechados, com fita: grave redondo de dub.")
                       .va (0, Wave::sine, -2).va (1, Wave::triangle, -8).lp (400, 5, true).keyTrack (40)
                       .amp (3, 600, 80, 250).mono (40).drift (25).sat (tape, 9, 100).build());

    out.push_back (B ("baixo-fm-trap", "Baixo FM Trap", "bass", TRAP,
                      "FM 1:2 com brilho que cai e um sub: baixo que corta sem embolar.")
                       .fm (0, FmAlgo::twoOp, 2, 25, 2, 0, 3, 0, 300, 10, 0, -6).noFilter()
                       .sub (-6).amp (1, 500, 80, 100).mono (50).build());

    out.push_back (B ("baixo-pop-sub", "Baixo Pop Sub", "bass", POP,
                      "Serra bem fechada sobre um sub: firme e moderno.")
                       .va (0, Wave::saw, -6).lp (250, 10, true).keyTrack (60)
                       .sub (-4).amp (2, 400, 95, 100).mono (30).build());

    //==========================================================================
    // Leads
    out.push_back (campana ("lead-campana-phonk", "Lead Campana Phonk", "lead", PHONK,
                            "A campana do phonk sustentada e mono, para melodias.", 1100, 2000)
                       .amp (0.5f, 2000, 50, 150, 70).mono (30).build());

    out.push_back (B ("lead-memphis", "Lead Memphis", "lead", PHONK | RAP,
                      "Serra e pulso gravados em fita velha, desafinando: o lead das fitas de Memphis.")
                       .va (0, Wave::saw, -6).va (1, Wave::pulse, -10, 30).lp (1800, 10, true)
                       .amp (2, 300, 90, 150).mono (40).drift (30).vibrato (10, 4.5f, 300)
                       .sat (tape, 10, 100).eq (40, 0, 2, 1200, -4).build());

    out.push_back (B ("lead-costa-oeste", "Lead Costa Oeste", "lead", RAP,
                      "Triângulo com um pulso fino, glide em toda nota: lead fino e chorado.")
                       .va (0, Wave::triangle, -4).va (1, Wave::pulse, -16, 10).lp (3000, 5, false)
                       .amp (5, 300, 95, 200).mono (100, true, true).vibrato (28, 5.5f, 200).build());

    out.push_back (B ("lead-flauta-funk", "Lead Flauta Funk", "lead", FUNK,
                      "Flauta sintética clara, com vibrato rápido e um pouco de saturação.")
                       .va (0, Wave::triangle, -4).noise (1, 75, -26, 20).lp (4500, 0, false)
                       .amp (10, 300, 85, 150).mono (40).vibrato (22, 6.5f, 150).sat (soft, 6, 100).build());

    out.push_back (B ("lead-sino-drill", "Lead Sino Drill", "lead", DRILL,
                      "FM 1:3 com uma quadrada baixinha, glide e reverb.")
                       .fm (0, FmAlgo::twoOp, 3, 30, 2, 0, 3, 0, 800, 30, 0, -6).va (1, Wave::pulse, -14, 50).noFilter()
                       .amp (1, 900, 50, 300).mono (80).reverb (55, 2.2f, 20).build());

    out.push_back (B ("lead-lofi-quadrado", "Lead Lo-fi Quadrado", "lead", LOFI,
                      "Quadrada fechada com fita e oscilação.")
                       .va (0, Wave::pulse, -6, 50).lp (2000, 5, false)
                       .lfo (0, LfoShape::sine, 0.6f, false).mod (Source::lfo1, Dest::pitch, 0.3f)
                       .amp (3, 300, 90, 200).mono (40).drift (40).sat (tape, 8, 100).build());

    out.push_back (B ("lead-pop-serra-mono", "Lead Pop Serra Mono", "lead", POP,
                      "Duas serras com o filtro abrindo no ataque e glide curto.")
                       .va (0, Wave::saw, -6).unison (0, 2, 8, 40).lp (3500, 10, true).fenv (1.5f, 1, 300, 40, 200)
                       .amp (2, 400, 90, 200).mono (30).reverb (35, 1.4f, 12).build());

    out.push_back (B ("lead-trap-sujo", "Lead Trap Sujo", "lead", TRAP,
                      "Serra e pulso no clipper com eco: agressivo sem perder a nota.")
                       .va (0, Wave::saw, -6).va (1, Wave::pulse, -10, 35).lp (4000, 10, false)
                       .amp (2, 300, 90, 150).mono (50).sat (hard, 12, 100).delay (d8Dot, 25, 15).build());

    out.push_back (B ("lead-glide-rnb", "Lead Glide R&B", "lead", RNB,
                      "Seno e triângulo com glide em toda nota e vibrato discreto.")
                       .va (0, Wave::sine, -4).va (1, Wave::triangle, -10).lp (4000, 0, false)
                       .amp (10, 300, 95, 250).mono (80, true, true).vibrato (10, 5.0f, 400).reverb (40, 1.8f, 15).build());

    out.push_back (B ("lead-uivo-phonk", "Lead Uivo Phonk", "lead", PHONK,
                      "Serra com vibrato largo e lento no fuzz: um uivo.")
                       .va (0, Wave::saw, -6).lp (2500, 15, true)
                       .lfo (1, LfoShape::sine, 3.0f, true, 300).mod (Source::lfo2, Dest::pitch, 1.6f)
                       .amp (20, 300, 90, 250).mono (60).sat (fuzz, 12, 100).build());

    //==========================================================================
    // Pads
    out.push_back (B ("pad-soul", "Pad Soul", "pad", RAP | LOFI,
                      "Órgão e serra fechados com deriva: a cama de acordes do rap soul.")
                       .wt (0, orgao, 50, -6).unison (0, 2, 8, 60).freePhase (0)
                       .va (1, Wave::saw, -12).freePhase (1)
                       .lp (1500, 0, true).amp (200, 600, 90, 800).drift (30).sat (tape, 6, 100).build());

    out.push_back (B ("pad-phonk-escuro", "Pad Phonk Escuro", "pad", PHONK,
                      "Serras fechadas com ressonância, fita e reverb.")
                       .va (0, Wave::saw, -6).unison (0, 3, 14, 80).freePhase (0)
                       .lp (600, 20, true).amp (600, 800, 90, 1500).sat (tape, 8, 100).reverb (65, 3.5f, 30).build());

    out.push_back (B ("pad-acorde-funk", "Pad Acorde Funk", "pad", FUNK,
                      "Pulsos com ataque rápido que assentam: acorde que acompanha o batidão.")
                       .va (0, Wave::pulse, -6, 35).unison (0, 3, 12, 80).freePhase (0)
                       .lp (2500, 5, false).amp (20, 600, 60, 400).width (140).build());

    out.push_back (B ("pad-chuvoso", "Pad Chuvoso", "pad", LOFI,
                      "Triângulos abertos com um chuvisco de ruído e sala.")
                       .va (0, Wave::triangle, -4).unison (0, 3, 12, 80).freePhase (0).noise (1, 70, -24, 100)
                       .lp (2500, 0, false).amp (500, 800, 90, 1500).reverb (60, 3.0f, 30).build());

    out.push_back (B ("pad-voz-trap", "Pad Voz Trap", "pad", TRAP,
                      "Duas tabelas de voz juntas, entrada lenta e reverb grande.")
                       .wt (0, vozEscura, 30, -6).unison (0, 3, 10, 80).freePhase (0)
                       .wt (1, vogais, 20, -10).unison (1, 2, 8, 80).freePhase (1)
                       .lp (4000, 0, false).amp (700, 1000, 90, 2000).reverb (70, 4.0f, 35).build());

    out.push_back (B ("pad-tensao-drill", "Pad Tensão Drill", "pad", DRILL,
                      "Serras bem desafinadas (quase dissonantes), fechadas: tensão pura.")
                       .va (0, Wave::saw, -6).unison (0, 4, 38, 90).freePhase (0)
                       .lp (1000, 10, true).amp (800, 1000, 90, 1800).reverb (65, 3.5f, 30).build());

    out.push_back (B ("pad-fm-suave", "Pad FM Suave", "pad", RNB,
                      "FM 1:1 bem leve em três vozes: aveludado.")
                       .fm (0, FmAlgo::twoOp, 1, 10, 2, 0, 3, 0, 3000, 70, 0, -6).unison (0, 3, 10, 80).freePhase (0).noFilter()
                       .amp (400, 800, 90, 1400).reverb (60, 3.0f, 28).build());

    out.push_back (B ("pad-abrindo", "Pad Abrindo", "pad", POP,
                      "O filtro abre por quatro segundos e fica aberto.")
                       .va (0, Wave::saw, -6).unison (0, 5, 18, 90).freePhase (0)
                       .lp (250, 15, true).fenv (5.0f, 4000, 1000, 80, 2000)
                       .amp (300, 800, 90, 1500).reverb (55, 2.6f, 25).build());

    //==========================================================================
    // Plucks
    out.push_back (B ("pluck-boom-bap", "Pluck Boom Bap", "pluck", RAP | LOFI,
                      "Triângulo e pulso curtos com fita: acorde picado de sample.")
                       .va (0, Wave::triangle, -4).va (1, Wave::pulse, -12, 30)
                       .lp (1200, 5, false).fenv (1.5f, 0.5f, 200, 0, 150)
                       .amp (0.5f, 500, 0, 250).drift (25).sat (tape, 10, 100).eq (60, 0, 0, 1000, -5).build());

    out.push_back (B ("pluck-phonk-metalico", "Pluck Phonk Metálico", "pluck", PHONK,
                      "FM inarmônico curto no clipper.")
                       .fm (0, FmAlgo::twoOp, 2.7f, 40, 2, 0, 3, 0, 150, 0, 10, -6).noFilter()
                       .amp (0.5f, 300, 0, 150, 75).sat (hard, 8, 100).build());

    out.push_back (B ("pluck-cavaquinho", "Pluck Cavaquinho Synth", "pluck", FUNK,
                      "Serra e pulso sem grave, curtinhos e brilhantes.")
                       .va (0, Wave::saw, -6).va (1, Wave::pulse, -10, 30)
                       .filter (FilterMode::highpass, 400, 5, false)
                       .amp (0.5f, 300, 0, 150, 75).build());

    out.push_back (B ("pluck-kalimba-escura", "Pluck Kalimba Escura", "pluck", TRAP | DRILL | PHONK,
                      "Lâmina de FM fechada no filtro, reverb grande.")
                       .fm (0, FmAlgo::twoOp, 3, 25, 2, 0, 3, 0, 200, 0, 0, -6).lp (1500, 5, false)
                       .amp (0.5f, 600, 0, 400, 75).reverb (60, 2.6f, 25).build());

    out.push_back (B ("pluck-harpa-lofi", "Pluck Harpa Lo-fi", "pluck", LOFI,
                      "Triângulo que abre e fecha, com fita oscilando.")
                       .va (0, Wave::triangle, -4).lp (900, 5, true).keyTrack (50).fenv (2.0f, 0.5f, 300, 0, 400)
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.3f)
                       .amp (0.5f, 1200, 0, 700, 65).drift (40).sat (tape, 6, 100).build());

    out.push_back (B ("pluck-pizzicato-drill", "Pluck Pizzicato Drill", "pluck", DRILL,
                      "Serra beliscada e fechada, num reverb grande.")
                       .va (0, Wave::saw, -6).lp (500, 10, true).keyTrack (70).fenv (2.5f, 0.5f, 90, 0, 100)
                       .amp (0.5f, 300, 0, 200, 70).reverb (65, 2.8f, 28).build());

    out.push_back (B ("pluck-madeira-pop", "Pluck Madeira Pop", "pluck", POP,
                      "FM 1:1 curto com triângulo: madeira clara.")
                       .fm (0, FmAlgo::twoOp, 1, 30, 2, 0, 3, 0, 100, 0, 0, -6).va (1, Wave::triangle, -8).noFilter()
                       .amp (0.5f, 350, 0, 200, 75).delay (d8, 20, 12).build());

    out.push_back (B ("pluck-sino-rnb", "Pluck Sino R&B", "pluck", RNB,
                      "FM 1:2 curto sobre um seno, macio.")
                       .fm (0, FmAlgo::twoOp, 2, 18, 2, 0, 3, 0, 250, 0, 0, -6).va (1, Wave::sine, -8).noFilter()
                       .amp (0.5f, 700, 0, 400, 70).reverb (45, 1.8f, 18).build());

    //==========================================================================
    // Keys
    out.push_back (B ("piano-soul", "Piano Soul", "keys", RAP | RNB,
                      "Dois FMs (corpo e brilho) com válvula: piano elétrico de disco de soul.")
                       .fm (0, FmAlgo::twoOp, 1, 25, 2, 0, 3, 0, 1400, 15, 0, -6)
                       .fm (1, FmAlgo::twoOp, 3, 10, 2, 0, 3, 0, 500, 0, 0, -16).noFilter()
                       .amp (0.5f, 2500, 0, 400, 65).velAmp (65).sat (tube, 6, 100).build());

    out.push_back (B ("orgao-phonk", "Órgão Phonk", "keys", PHONK,
                      "Órgão no fuzz com tremolo: igreja assombrada.")
                       .wt (0, orgao, 50, -6).noFilter()
                       .lfo (0, LfoShape::sine, 5.5f, false).mod (Source::lfo1, Dest::volume, 12)
                       .amp (2, 100, 100, 150).sat (fuzz, 8, 100).build());

    out.push_back (B ("clavinete-wah", "Teclado Funk Wah", "keys", FUNK,   // id antigo: já vai nos .wnl
                      "Teclado estalado com o filtro de banda abrindo a cada nota: wah automático.")
                       .va (0, Wave::pulse, -6, 20).va (1, Wave::saw, -12)
                       .filter (FilterMode::bandpass, 500, 35, false).fenv (2.0f, 1, 200, 20, 100)
                       .amp (0.5f, 600, 30, 100).build());

    out.push_back (B ("keys-chorus-lofi", "Keys Chorus Lo-fi", "keys", LOFI,
                      "Piano elétrico de FM dobrado, oscilando como fita.")
                       .fm (0, FmAlgo::twoOp, 1, 28, 2, 0, 3, 0, 1200, 10, 0, -6).unison (0, 2, 8, 70).noFilter()
                       .lfo (1, LfoShape::sine, 0.5f, false).mod (Source::lfo2, Dest::pitch, 0.3f)
                       .amp (0.5f, 2200, 0, 400, 65).drift (35).sat (tape, 8, 100).build());

    out.push_back (B ("keys-brilho-trap", "Keys Brilho Trap", "keys", TRAP,
                      "FM 1:4 com uma serra fechada embaixo: piano de vidro para trap.")
                       .fm (0, FmAlgo::twoOp, 4, 15, 2, 0, 3, 0, 900, 10, 0, -6).va (1, Wave::saw, -18)
                       .lp (5000, 0, false).amp (0.5f, 2200, 0, 700, 65).reverb (50, 2.2f, 20).build());

    out.push_back (B ("piano-drill", "Piano Drill", "keys", DRILL | PHONK,
                      "FM leve com triângulo, fechado e com reverb grande.")
                       .fm (0, FmAlgo::twoOp, 1, 15, 2, 0, 3, 0, 1200, 15, 0, -6).va (1, Wave::triangle, -8)
                       .lp (2000, 0, false).amp (0.5f, 2600, 0, 900, 65).reverb (65, 3.0f, 28).build());

    out.push_back (B ("keys-stab-pop", "Keys Stab Pop", "keys", POP,
                      "Três serras num golpe curto e aberto.")
                       .va (0, Wave::saw, -6).unison (0, 3, 12, 80).lp (2500, 5, false).fenv (1.5f, 0.5f, 150, 10, 120)
                       .amp (0.5f, 300, 20, 150).build());

    out.push_back (B ("keys-palheta-brilhante", "Keys Palheta Brilhante", "keys", RNB,
                      "FM áspero 1:1 com um pulso: palheta mais aberta.")
                       .fm (0, FmAlgo::twoOp, 1, 55, 2, 0, 3, 0, 400, 25, 10, -6).va (1, Wave::pulse, -16, 40)
                       .lp (5000, 0, false).amp (0.5f, 2000, 10, 350).velAmp (65).build());

    //==========================================================================
    // Sinos
    out.push_back (B ("sino-boom-bap", "Sino Boom Bap", "bell", RAP | LOFI,
                      "Sino de FM com fita e agudo cortado, como um sample velho.")
                       .fm (0, FmAlgo::twoOp, 3.5f, 30, 2, 0, 3, 0, 600, 10, 0, -6).noFilter()
                       .amp (0.5f, 1500, 0, 500, 70).drift (25).sat (tape, 9, 100).eq (60, 0, 0, 1000, -6).build());

    out.push_back (campana ("campana-aguda", "Campana Aguda", "bell", PHONK,
                            "A campana do phonk mais aguda e mais curta.", 1600, 250).build());

    out.push_back (B ("triangulo-funk", "Triângulo Funk", "bell", FUNK,
                      "Metal fino que soa longo, como o triângulo do baile.")
                       .fm (0, FmAlgo::twoOp, 8, 40, 2, 0, 3, 0, 1500, 20, 0, -6)
                       .filter (FilterMode::highpass, 2000, 5, false)
                       .amp (0.5f, 1500, 0, 600, 70).build());

    out.push_back (B ("glock-drill", "Glock Drill", "bell", DRILL,
                      "Glockenspiel curto num reverb enorme.")
                       .fm (0, FmAlgo::twoOp, 8, 15, 2, 0, 3, 0, 150, 0, 0, -6).va (1, Wave::sine, -8).noFilter()
                       .amp (0.5f, 900, 0, 500, 80).reverb (70, 3.5f, 32).build());

    out.push_back (B ("sino-faisca", "Sino Faísca", "bell", POP,
                      "FM 1:6 curtinho com eco: faíscas no refrão.")
                       .fm (0, FmAlgo::twoOp, 6, 20, 2, 0, 3, 0, 150, 0, 0, -6).noFilter()
                       .amp (0.5f, 500, 0, 300, 80).delay (d8, 30, 20).build());

    out.push_back (B ("sino-caixinha-trap", "Sino Caixinha Trap", "bell", TRAP,
                      "Caixinha de música com eco pontuado.")
                       .fm (0, FmAlgo::twoOp, 5, 18, 2, 0, 3, 0, 250, 0, 0, -6).va (1, Wave::sine, -6).noFilter()
                       .amp (0.5f, 1000, 0, 500, 75).delay (d8Dot, 30, 18).reverb (45, 2.0f, 20).build());

    //==========================================================================
    // Metais
    out.push_back (B ("stab-phonk", "Stab Phonk", "brass", PHONK,
                      "Golpe curto de metais no fuzz.")
                       .va (0, Wave::saw, -6).unison (0, 2, 10, 50).lp (1500, 10, true).fenv (2.0f, 3, 150, 20, 100)
                       .amp (3, 200, 20, 100).sat (fuzz, 12, 100).build());

    out.push_back (B ("metais-soul", "Metais Soul", "brass", RAP | LOFI,
                      "Serra e triângulo com fita: metais de sample de soul.")
                       .va (0, Wave::saw, -6).va (1, Wave::triangle, -8).lp (900, 10, true).fenv (1.5f, 20, 300, 50, 200)
                       .amp (20, 300, 85, 200).drift (25).sat (tape, 8, 100).build());

    out.push_back (B ("metais-baile", "Metais Baile", "brass", FUNK,
                      "Pulso e serra num golpe curto e brilhante.")
                       .va (0, Wave::pulse, -6, 35).va (1, Wave::saw, -8)
                       .lp (1800, 10, true).fenv (2.0f, 2, 150, 30, 100)
                       .amp (2, 250, 40, 100).build());

    out.push_back (B ("trompa-trap", "Trompa Trap", "brass", TRAP,
                      "Serra fechada com entrada média e sala grande.")
                       .va (0, Wave::saw, -6).unison (0, 2, 8, 50).lp (800, 5, true).fenv (1.2f, 100, 600, 60, 400)
                       .amp (100, 500, 90, 500).reverb (60, 2.8f, 25).build());

    out.push_back (B ("brass-sintetico-largo", "Brass Sintético Largo", "brass", POP,
                      "Três pulsos abertos com o filtro abrindo.")
                       .va (0, Wave::pulse, -6, 40).unison (0, 3, 12, 90).lp (1200, 10, true).fenv (2.0f, 20, 300, 50, 200)
                       .amp (15, 300, 85, 250).build());

    //==========================================================================
    // Cordas
    out.push_back (B ("cordas-soul", "Cordas Soul", "strings", RAP | RNB,
                      "Naipe aberto com fita: cordas de sample de soul.")
                       .va (0, Wave::saw, -6).unison (0, 4, 14, 80).freePhase (0)
                       .lp (2500, 0, false).amp (250, 600, 90, 800).sat (tape, 6, 100).reverb (50, 2.4f, 22).build());

    out.push_back (B ("cordas-arrastadas", "Cordas Arrastadas Phonk", "strings", PHONK,
                      "Cordas oscilando como fita esticada, saturadas.")
                       .va (0, Wave::saw, -6).unison (0, 3, 12, 70).freePhase (0)
                       .lfo (1, LfoShape::sine, 0.35f, false).mod (Source::lfo2, Dest::pitch, 0.8f)
                       .lp (2000, 5, false).amp (300, 600, 90, 900).sat (tape, 10, 100).build());

    out.push_back (B ("cordas-curtas-funk", "Cordas Curtas Funk", "strings", FUNK,
                      "Golpes de cordas sem grave, bem curtos.")
                       .va (0, Wave::saw, -6).unison (0, 4, 14, 80).freePhase (0)
                       .filter (FilterMode::highpass, 250, 5, false)
                       .amp (5, 180, 0, 120, 70).build());

    out.push_back (B ("cordas-agudas-trap", "Cordas Agudas Trap", "strings", TRAP,
                      "Violinos sintéticos uma oitava acima, com vibrato.")
                       .va (0, Wave::saw, -6).unison (0, 4, 12, 90).freePhase (0).tune (0, 1)
                       .filter (FilterMode::highpass, 500, 5, false)
                       .amp (150, 500, 90, 700).vibrato (12, 5.5f, 400).reverb (60, 2.8f, 28).build());

    out.push_back (B ("cordas-lentas-lofi", "Cordas Lentas Lo-fi", "strings", LOFI,
                      "Naipe que entra devagar, saturado e sem agudo.")
                       .va (0, Wave::saw, -6).unison (0, 3, 10, 70).freePhase (0)
                       .lp (1500, 0, false).amp (800, 800, 90, 1200).drift (35).sat (tape, 8, 100).build());

    //==========================================================================
    // Vozes
    out.push_back (B ("vox-assombrada", "Vox Assombrada", "vox", PHONK,
                      "Voz escura mudando ao acaso, saturada.")
                       .wt (0, vozEscura, 40, -6).unison (0, 2, 10, 70).freePhase (0)
                       .lfo (0, LfoShape::smoothRandom, 0.8f, false).mod (Source::lfo1, Dest::shape1, 40)
                       .lp (3000, 0, false).amp (200, 600, 90, 900).sat (tape, 10, 100).reverb (60, 3.0f, 28).build());

    out.push_back (B ("vox-grito-funk", "Vox Grito Funk", "vox", FUNK,
                      "Vogal \"a\" com pulso, glide e clipper: o grito sintético.")
                       .wt (0, vogais, 0, -4).va (1, Wave::pulse, -12, 30)
                       .lp (5000, 0, false).amp (2, 300, 90, 120).mono (40).sat (hard, 10, 100).build());

    out.push_back (B ("coro-soul", "Coro Soul", "vox", RAP | RNB,
                      "Quatro vozes de vogal fechadas: coro de sample de soul.")
                       .wt (0, vogais, 10, -6).unison (0, 4, 12, 80).freePhase (0)
                       .lp (3000, 0, false).amp (200, 600, 90, 800).sat (tape, 6, 100).reverb (55, 2.6f, 25).build());

    out.push_back (B ("coro-ooh-trap", "Coro Ooh Trap", "vox", TRAP,
                      "Vogal \"u\" em três vozes com eco e reverb.")
                       .wt (0, vozEscura, 0, -6).unison (0, 3, 10, 80).freePhase (0)
                       .lp (3500, 0, false).amp (300, 600, 90, 1000).delay (d4, 30, 18).reverb (65, 3.2f, 30).build());

    out.push_back (B ("vox-sussurro-drill", "Vox Sussurro Drill", "vox", DRILL,
                      "Ar em faixa estreita sobre uma voz escura grave.")
                       .noise (0, 40, -12, 60).wt (1, vozEscura, 60, -14).tune (1, -1)
                       .filter (FilterMode::bandpass, 900, 30, false)
                       .amp (200, 600, 80, 900).reverb (65, 3.0f, 30).build());

    //==========================================================================
    // Arp / Seq
    out.push_back (campana ("arp-campana-phonk", "Arp Campana Phonk", "arp", PHONK,
                            "Campana curtíssima para arpejos rápidos.", 1100, 150)
                       .delay (d16, 25, 15).build());

    out.push_back (B ("arp-boom-bap", "Arp Boom Bap", "arp", RAP | LOFI,
                      "Triângulo curto com fita, para frases repetidas.")
                       .va (0, Wave::triangle, -4).lp (1800, 0, false)
                       .amp (0.5f, 250, 0, 150, 70).drift (20).sat (tape, 8, 100).build());

    out.push_back (B ("seq-funk-16", "Seq Funk 1/16", "arp", FUNK,
                      "Pulso segurado vira semicolcheias no tempo.")
                       .va (0, Wave::pulse, -6, 35).lp (1500, 10, true)
                       .lfoSync (0, LfoShape::rampDown, sixteenth, false).mod (Source::lfo1, Dest::volume, 95)
                       .amp (2, 300, 100, 200).build());

    out.push_back (B ("seq-drill-sino", "Seq Drill Sino", "arp", DRILL,
                      "Sino de FM cortado em tercinas de colcheia.")
                       .fm (0, FmAlgo::twoOp, 3.5f, 25, 2, 0, 3, 0, 2000, 50, 0, -6).noFilter()
                       .lfoSync (0, LfoShape::rampDown, eighthT, false).mod (Source::lfo1, Dest::volume, 90)
                       .amp (2, 300, 100, 300).reverb (55, 2.4f, 22).build());

    out.push_back (B ("arp-suave-rnb", "Arp Suave R&B", "arp", RNB | RAP | LOFI,
                      "Piano elétrico de FM curtinho, com eco discreto.")
                       .fm (0, FmAlgo::twoOp, 1, 25, 2, 0, 3, 0, 300, 0, 0, -6).noFilter()
                       .amp (0.5f, 400, 0, 250, 70).delay (d8Dot, 20, 12).build());

    out.push_back (B ("arp-supersaw-pop", "Arp Supersaw Pop", "arp", POP,
                      "Serras abertas curtíssimas com eco em 1/8.")
                       .va (0, Wave::saw, -6).unison (0, 5, 18, 100).freePhase (0)
                       .lp (2000, 10, false).fenv (2.0f, 0.5f, 100, 0, 80)
                       .amp (0.5f, 220, 0, 150, 70).delay (d8, 30, 20).build());

    //==========================================================================
    // FX
    out.push_back (B ("fx-arranhao", "Arranhão", "fx", RAP | LOFI,
                      "Ruído em faixa estreita indo e voltando rápido: arranhado de disco.")
                       .noise (0, 55, -4, 30).filter (FilterMode::bandpass, 1500, 45, false)
                       .lfo (0, LfoShape::triangle, 7, true).mod (Source::lfo1, Dest::cutoff, 25)
                       .amp (2, 300, 0, 80, 60).build());

    out.push_back (B ("fx-subida-fantasma", "Subida Fantasma", "fx", PHONK,
                      "Voz escura que cresce por dois segundos num reverb enorme.")
                       .wt (0, vozEscura, 50, -6).unison (0, 3, 15, 90).freePhase (0)
                       .modEnv (2000, 500, 100, 500, 30).mod (Source::env3, Dest::pitch, 25)
                       .lp (3000, 0, false).amp (2000, 500, 100, 800).reverb (85, 5.0f, 50).build());

    out.push_back (B ("fx-apito", "Apito", "fx", FUNK,
                      "Seno agudo trinando: o apito do baile.")
                       .va (0, Wave::sine, -4).tune (0, 2).noise (1, 85, -28, 20).noFilter()
                       .lfo (0, LfoShape::square, 14, true).mod (Source::lfo1, Dest::volume, 70)
                       .amp (5, 100, 100, 80).build());

    out.push_back (B ("fx-sirene-policial", "Sirene Policial", "fx", DRILL | FUNK,
                      "Pulso subindo e descendo devagar.")
                       .va (0, Wave::pulse, -6, 40).lp (4000, 0, false)
                       .lfo (0, LfoShape::triangle, 0.8f, true).mod (Source::lfo1, Dest::pitch, 30)
                       .amp (20, 100, 100, 300).reverb (40, 1.5f, 15).build());

    out.push_back (B ("fx-riser-branco", "Riser Branco", "fx", TRAP | POP,
                      "Ruído claro que abre por três segundos.")
                       .noise (0, 80, -6, 100).filter (FilterMode::highpass, 4000, 10, false).fenv (-5.0f, 3000, 500, 100, 400)
                       .amp (2500, 500, 100, 500).reverb (55, 2.5f, 25).build());

    //==========================================================================
    // Texturas
    out.push_back (B ("textura-rua", "Textura Rua", "texture", RAP | LOFI,
                      "Ruído escuro de cidade, oscilando ao acaso.")
                       .noise (0, 30, -6, 100).lp (1200, 0, false)
                       .lfo (0, LfoShape::smoothRandom, 0.5f, false).mod (Source::lfo1, Dest::volume, 50)
                       .amp (1000, 500, 100, 2000).build());

    out.push_back (B ("textura-fita-assombrada", "Textura Fita Assombrada", "texture", PHONK,
                      "Voz grave escondida no chiado, com reverb.")
                       .wt (0, vozEscura, 60, -12).tune (0, -1).freePhase (0).noise (1, 60, -16, 100)
                       .lp (2500, 0, false).amp (1500, 1000, 90, 2500).sat (tape, 8, 100).reverb (75, 5.0f, 40).build());

    out.push_back (B ("textura-eco-baile", "Textura Eco do Baile", "texture", FUNK,
                      "Acorde abafado bombeando em semínimas, como o baile ouvido de longe.")
                       .va (0, Wave::saw, -6).unison (0, 4, 15, 90).freePhase (0)
                       .lp (700, 10, true)
                       .lfoSync (0, LfoShape::rampUp, quarter, false).mod (Source::lfo1, Dest::volume, 70)
                       .amp (800, 1000, 90, 2000).reverb (75, 4.0f, 40).build());

    out.push_back (B ("textura-chuva-escura", "Textura Chuva Escura", "texture", DRILL,
                      "Chuva grave em faixa estreita e reverb longo.")
                       .noise (0, 40, -6, 100).filter (FilterMode::bandpass, 600, 15, false)
                       .lfo (0, LfoShape::stepRandom, 12, false).mod (Source::lfo1, Dest::volume, 25)
                       .amp (1500, 500, 100, 2500).reverb (70, 4.0f, 35).build());
}

} // namespace whynot::synth::presets
