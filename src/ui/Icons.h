#pragma once

#include <JuceHeader.h>

namespace whynot
{

/** Ícones próprios da WHYNOT LAB, desenhados em vetor numa grade de 24 x 24.
    Um traço só (2 unidades, pontas redondas) e, quando ajuda a leitura, formas cheias
    (transporte, notas, passos ligados). Nada copiado de outro app: cada ícone é geometria simples.
*/
enum class Icon
{
    none,

    // transporte
    play, pause, stop, loop, metronome,
    keyboard,   // teclado-piano: as letras do computador tocam o canal (21/10)

    // arquivo e edição
    undo, redo, save,

    // telas
    playlist, channelRack, pianoRoll, mixer, settings,
    projects,   // tela Projetos (21/10): um disco, a biblioteca das suas músicas

    // ferramentas
    pencil, brush, select, magnet, slide, timer, note, pattern, sparkle,
    scissors, eraser, slip,   // ferramentas da playlist (21/10): cortar, apagar e deslizar o conteúdo

    // plugins e arquivos
    plugin, search, folder, wave,

    // presets: favorito e variar
    star, starFilled, dice,

    // gravação e desempenho (3.5)
    record, snowflake, automation,
    headphones,   // monitorar a entrada (gravação de voz, 21/10)

    // gerais (warning fica por último: marca o tamanho da tabela)
    plus, chevronDown, chevronLeft, chevronRight, check, close, route, speaker, warning
};

namespace icons
{
    /** Desenha o ícone ocupando `area` (quadrado centrado, com o traço proporcional ao tamanho). */
    void draw (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour);
}

} // namespace whynot
