#pragma once

#include <JuceHeader.h>

namespace whynot::synthfavorites
{

/** Presets favoritos do WHYNOT Synth: preferência do usuário (não do projeto). Desde 21/10 ficam no
    favoritos.json, com os outros favoritos (Favorites, tipo synthPreset); o synth-favoritos.txt antigo
    é migrado na primeira leitura. Thread de mensagem. */

/** A pasta de dados do app (o AppController informa; o autoteste usa uma pasta própria). */
void setFolder (const juce::File& folder);

bool isFavorite (const juce::String& presetId);
void set (const juce::String& presetId, bool favorite);
juce::StringArray all();

} // namespace whynot::synthfavorites
