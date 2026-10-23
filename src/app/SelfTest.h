#pragma once

#include <JuceHeader.h>

namespace whynot
{

/** Roda os testes headless e grava o relatório. Devolve o número de falhas. */
int runSelfTest (const juce::File& reportFile, const juce::File& workFolder);

} // namespace whynot
