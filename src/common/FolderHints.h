#pragma once

#include <JuceHeader.h>

#include "Text.h"

namespace whynot
{

/** Música, Documentos, Área de Trabalho, Imagens e Vídeos: as pastas que o "Acesso controlado a pastas"
    do Windows Defender protege. Um app novo não grava nelas até ser liberado. */
inline bool inProtectedUserFolder (const juce::File& f)
{
    for (auto type : { juce::File::userMusicDirectory, juce::File::userDocumentsDirectory, juce::File::userDesktopDirectory,
                       juce::File::userPicturesDirectory, juce::File::userMoviesDirectory })
    {
        const auto folder = juce::File::getSpecialLocation (type);

        if (folder != juce::File() && (f == folder || f.isAChildOf (folder)))
            return true;
    }

    return false;
}

/** O que dizer quando não deu para gravar em `folder`: o que fazer, em português. `action` completa a frase
    do fim ("exporte", "salve"). Começa com espaço, para ir depois da mensagem de erro. */
inline juce::String noPermissionHint (const juce::File& folder, const juce::String& action = "exporte")
{
    auto hint = " Escolha outra pasta."_t;

    if (inProtectedUserFolder (folder))
        hint << " Se o Windows Defender estiver com o \"Acesso controlado a pastas\" ligado, ele bloqueia apps novos"_t
             << " nas pastas Música, Documentos e Área de Trabalho: libere a WHYNOT LAB lá ou "_t << action << " em outra pasta.";

    return hint;
}

} // namespace whynot
