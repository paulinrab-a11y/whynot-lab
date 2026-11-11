#pragma once

namespace whynot
{

/** O que mudou no projeto, em bits, para quem escuta o ProjectModel decidir o que refazer.
    Sem JUCE: o teste da lógica das telas (tests/ui) compila só com isto.

    Quem avisa sem dizer nada vale `all` (o comportamento de sempre). O 0 também vale `all`: máscara
    vazia é engano de quem chamou, e refazer a mais é melhor que deixar uma tela velha.
*/
namespace change
{
    using Mask = unsigned;

    constexpr Mask patterns  = 1u << 0;   // passos, notas, tamanho e nome dos padrões
    constexpr Mask playlist  = 1u << 1;   // blocos, raias, marcadores, punch e as curvas de automação
    constexpr Mask mixer     = 1u << 2;   // trilhas do mixer, efeitos, mandadas e rotas
    constexpr Mask channels  = 1u << 3;   // canais do rack: nome, cor, sample, mute, saída, instrumento
    constexpr Mask transport = 1u << 4;   // andamento, compasso, tom, swing, SONG/PAT, padrão atual, nome do projeto
    constexpr Mask all       = ~0u;       // tudo (abrir, desfazer, comando que não informa)

    constexpr Mask normalised (Mask m) noexcept   { return m == 0 ? all : m; }
}

} // namespace whynot
