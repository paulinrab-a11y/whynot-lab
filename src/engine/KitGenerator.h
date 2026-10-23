#pragma once

#include <JuceHeader.h>

#include "../common/Text.h"

namespace whynot
{

/** Kit próprio da WHYNOT, sintetizado na hora (nenhum sample de terceiros): bateria, 808 e um
    sino para melodias. Gera WAVs de 48 kHz / 24 bits na pasta pedida, só se ainda não existirem.
*/
struct KitGenerator
{
    struct Sound
    {
        juce::String name;
        juce::File file;
        int rootKey = 60;   // nota em que o sample soa no tom original
    };

    /** Garante os arquivos e devolve a lista na ordem do channel rack. */
    static juce::Array<Sound> ensureKit (const juce::File& folder, juce::String& error);
};

} // namespace whynot
