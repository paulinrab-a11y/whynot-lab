#include "SynthFavorites.h"
#include "../app/Favorites.h"

namespace whynot::synthfavorites
{

// Desde 21/10 os favoritos do synth moram no favoritos.json, com os outros (src/app/Favorites.*).
// Esta API continua para o synth não precisar mudar; o synth-favoritos.txt antigo é migrado lá.

void setFolder (const juce::File& folder)
{
    Favorites::get().setFolder (folder);
}

bool isFavorite (const juce::String& presetId)
{
    return Favorites::get().contains (Favorites::Kind::synthPreset, presetId);
}

void set (const juce::String& presetId, bool favorite)
{
    Favorites::get().set (Favorites::Kind::synthPreset, presetId, favorite);
}

juce::StringArray all()
{
    return Favorites::get().list (Favorites::Kind::synthPreset);
}

} // namespace whynot::synthfavorites
