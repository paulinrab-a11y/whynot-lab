#include "PresetBuilder.h"

// Presets "3 OSC" (pedido de 26/10): timbres feitos só com as três primeiras camadas analógicas, como num sintetizador
// simples de três osciladores. Abrem na página "3 OSC" do editor, com tudo à vista. Notas de desenho:
// - o sub e o 808 são senos com fase do zero (o ataque igual a cada nota) e mono com glide;
// - o supersaw caseiro são três serras levemente desafinadas, com fase livre e abertas no estéreo;
// - o pluck e o chip mostram o filtro e o envelope curtos, que num synth de três osciladores ficam em volta deles.

namespace whynot::synth::presets
{

void add3Osc (std::vector<Preset>& out)
{
    out.push_back (B ("3osc-init", "3 OSC Init", "lead", ALL,
                      "Ponto de partida: três serras (a segunda um pouco desafinada, a terceira uma oitava abaixo), sem filtro.")
                       .va (0, Wave::saw, -6).va (1, Wave::saw, -12).tune (1, 0, 0, 7).va (2, Wave::saw, -12).tune (2, -1)
                       .noFilter().amp (2, 300, 80, 200).volume (-6).build());

    out.push_back (B ("3osc-sub", "3 OSC Sub", "bass", TRAP | DRILL | RAP | RNB,
                      "Só o seno do primeiro oscilador, mono: o grave limpo por baixo do 808 ou do baixo.")
                       .va (0, Wave::sine, 0).noFilter().amp (2, 400, 100, 120).mono (40).drift (0).velAmp (30).build());

    out.push_back (B ("3osc-808", "3 OSC 808", "808", TRAP | DRILL | RAP,
                      "Seno com um triângulo uma oitava acima, a queda curta de afinação e a cauda longa.")
                       .va (0, Wave::sine, 0).va (1, Wave::triangle, -10).tune (1, 1)
                       .noFilter().amp (0.5f, 4000, 0, 150, 60).pitchDrop (2, 25).mono (70).drift (0).velAmp (35).build());

    out.push_back (B ("3osc-supersaw", "3 OSC Supersaw", "lead", POP | TRAP | FUNK,
                      "Três serras a ±15 cents, com fase livre e abertas no estéreo: o supersaw caseiro.")
                       .va (0, Wave::saw, -9).va (1, Wave::saw, -9).tune (1, 0, 0, 15).pan (1, -60)
                       .va (2, Wave::saw, -9).tune (2, 0, 0, -15).pan (2, 60)
                       .freePhase (0).freePhase (1).freePhase (2).lp (9000, 10).amp (8, 300, 85, 350).volume (-6).build());

    out.push_back (B ("3osc-pluck", "3 OSC Pluck", "pluck", POP | TRAP | RNB,
                      "Serra e quadrada com o filtro fechando rápido: o pluck de três osciladores.")
                       .va (0, Wave::saw, -6).va (1, Wave::pulse, -10, 50).tune (1, 1)
                       .lp (700, 25).fenv (4, 0, 220, 0, 200).amp (1, 450, 0, 250).volume (-4).build());

    out.push_back (B ("3osc-chip", "3 OSC Chip", "lead", POP | PHONK | LOFI,
                      "Quadradas secas, uma oitava acima uma da outra, sem filtro: o som de videogame antigo.")
                       .va (0, Wave::pulse, -8, 50).va (1, Wave::pulse, -14, 25).tune (1, 1)
                       .noFilter().amp (1, 120, 70, 60).drift (0).volume (-8).build());
}

} // namespace whynot::synth::presets
