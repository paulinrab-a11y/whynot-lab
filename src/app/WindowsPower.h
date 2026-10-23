#pragma once

namespace whynot
{

/** Pede ao Windows para nunca pôr o app no modo de economia (EcoQoS).

    Em CPU híbrida (núcleos P + E, como os Intel de 12ª geração em diante) o Windows 11
    pode classificar um processo minimizado ou sem janela à frente como "em segundo plano"
    e mandar as threads dele para os núcleos de eficiência, em frequência baixa. Para uma
    DAW isso vira estalo e falha de buffer. O JUCE já põe a thread do WASAPI no MMCSS
    "Pro Audio" e o Tracktion sobe as threads do grafo para tempo real, mas nenhum dos dois
    desliga esse modo.

    Chamar uma vez, cedo, na thread de mensagem. Devolve false se o Windows recusou
    (versão antiga sem esse controle); nesse caso nada muda.
*/
bool keepFullCpuSpeed() noexcept;

} // namespace whynot
