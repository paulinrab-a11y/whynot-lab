#pragma once

// Tabelas de onda do WHYNOT Synth, geradas por código (nenhuma tabela de terceiros).
//
// Cada tabela tem `framesPerTable` quadros (a posição "Forma" passa por eles) e cada quadro tem
// uma versão por oitava ("mipmap") com só os harmônicos que cabem abaixo de Nyquist: tocar agudo
// não gera aliasing. Geradas uma vez (na primeira chamada, fora da thread de áudio) e depois só lidas.

#include <algorithm>
#include <array>
#include <complex>
#include <span>
#include <vector>

namespace whynot::synth
{

class Wavetables
{
public:
    static constexpr int tableSize = 2048;               // amostras por ciclo
    static constexpr int framesPerTable = 8;
    static constexpr int numLevels = 11;                 // 1024, 512, ... 1 harmônico
    static constexpr int maxHarmonics = tableSize / 2;
    static constexpr int stride = tableSize + 1;         // +1: amostra de guarda para interpolar sem módulo

    /** Nomes das tabelas (a escolha "Tabela" de cada camada). */
    static std::span<const char* const> names();
    static int numTables()   { return (int) names().size(); }

    /** As tabelas prontas. A primeira chamada gera tudo (~0,1 s): chame fora da thread de áudio. */
    static const Wavetables& get();

    /** Um ciclo (tableSize + 1 amostras) de um quadro num nível de harmônicos. */
    const float* frame (int table, int frameIndex, int level) const noexcept
    {
        return data.data() + ((size_t) ((table * framesPerTable + frameIndex) * numLevels + level)) * stride;
    }

    /** Ondas analógicas de banda limitada (um quadro, mesmos níveis): serra subindo de -1 a +1
        (2t - 1, o salto em t = 0) e triângulo com o vale em t = 0 (1 - 4|t - 0,5|). Sem normalizar:
        a diferença de duas serras deslocadas dá o pulso com largura variável (PWM), já sem DC. */
    enum Basic { basicSaw, basicTriangle, numBasic };

    const float* basic (Basic which, int level) const noexcept
    {
        return basicData.data() + ((size_t) ((int) which * numLevels + level)) * stride;
    }

    /** Nível para uma frequência: o maior número de harmônicos que fica abaixo de ~0,47 * taxa. */
    static int levelFor (float phaseIncrement) noexcept
    {
        // harmônicos permitidos = 0,47 / incremento; nível k tem 1024 >> k harmônicos.
        const float allowed = 0.47f / std::max (phaseIncrement, 1.0e-7f);
        int level = 0;

        for (float h = (float) maxHarmonics; level < numLevels - 1 && h > allowed; h *= 0.5f)
            ++level;

        return level;
    }

private:
    Wavetables();

    /** Os níveis de um quadro a partir dos harmônicos (c_n de s(t) = Re soma c_n e^(i 2 pi n t)). */
    static void renderLevels (const std::vector<std::complex<double>>& harmonics, float* first, bool normalise,
                              std::vector<std::complex<double>>& scratch);

    std::vector<float> data, basicData;
};

} // namespace whynot::synth
