#pragma once

#include "../model/ProjectChange.h"

namespace whynot
{

/** Quais telas atualizar a cada mudança do projeto (frente ui-atualizacao, 10/11). Sem JUCE: tests/ui.

    Duas regras:
    - a máscara da mudança (change::Mask) diz quais telas dependem do que mudou;
    - tela escondida (aba de trás, mixer recolhido) não é atualizada na hora: fica pendente e é
      atualizada quando aparece, ou no MainComponent::flushPendingRefresh().

    A tabela é conservadora: na dúvida, a tela depende. O que cada tela lê do projeto foi conferido no
    refresh de cada uma (ChannelRack::refresh, PianoRollGrid::refresh, PlaylistGrid::refresh com as
    AutomationLanes, MixerStrip::update e InsertPanel::refresh).
*/
namespace refreshplan
{
    using Screens = unsigned;

    constexpr Screens rack      = 1u << 0;
    constexpr Screens pianoRoll = 1u << 1;
    constexpr Screens playlist  = 1u << 2;
    constexpr Screens mixer     = 1u << 3;
    constexpr Screens allScreens = rack | pianoRoll | playlist | mixer;

    /** As telas que desenham algo do que mudou. */
    constexpr Screens screensFor (change::Mask changed) noexcept
    {
        const auto m = change::normalised (changed);
        Screens s = 0;

        // rack: os canais, os passos e as notas do padrão atual, o nome e o congelado da trilha de saída, o swing
        if ((m & (change::channels | change::patterns | change::mixer | change::transport)) != 0)
            s |= rack;

        // piano roll: as notas e os passos do padrão atual, o canal (mono, glide, groove), o tom, o compasso e o andamento
        if ((m & (change::channels | change::patterns | change::transport)) != 0)
            s |= pianoRoll;

        // playlist: os blocos, as prévias dos padrões, os canais de áudio, o compasso e o andamento, e as curvas
        // de automação (que leem os nomes e os efeitos do mixer): depende de tudo
        s |= playlist;

        // mixer: as trilhas e os efeitos; os canais entram porque um canal novo ganha a trilha dele
        if ((m & (change::mixer | change::channels)) != 0)
            s |= mixer;

        return s;
    }

    constexpr Screens visibleScreens (bool rackShown, bool pianoRollShown, bool playlistShown, bool mixerShown) noexcept
    {
        return (rackShown ? rack : 0u) | (pianoRollShown ? pianoRoll : 0u) | (playlistShown ? playlist : 0u) | (mixerShown ? mixer : 0u);
    }

    struct Plan
    {
        Screens now = 0;       // atualizar agora
        Screens pending = 0;   // o que continua pendente (escondidas)
    };

    /** Das pendentes, as que estão em `allowed` saem agora; o resto espera. */
    constexpr Plan split (Screens pending, Screens allowed) noexcept
    {
        return { pending & allowed & allScreens, pending & ~allowed & allScreens };
    }

    /** Uma mudança do projeto: soma as telas que dependem dela às pendentes e separa as que estão à vista. */
    constexpr Plan onChange (Screens pendingBefore, change::Mask changed, Screens visible) noexcept
    {
        return split (pendingBefore | screensFor (changed), visible);
    }
}

} // namespace whynot
