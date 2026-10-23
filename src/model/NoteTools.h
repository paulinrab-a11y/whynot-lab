#pragma once

#include "Project.h"

#include <set>

/** As ferramentas de edição do piano roll que o FL tem e a comunidade pede (bloco 1, 22/10): legato,
    juntar, mudo, humanizar, levar para a escala e o carimbo de acorde. Funções puras sobre a lista de
    notas de uma raia: o comando pianoroll.transform e o autoteste usam as mesmas.
    `ids` vazio = todas as notas da raia. */
namespace whynot::notetools
{

inline bool picked (const std::set<int>& ids, const Note& n)   { return ids.empty() || ids.count (n.id) != 0; }

/** Legato (Ctrl+L): cada nota vai até o começo da nota seguinte (a primeira que começa depois dela).
    As notas de um acorde vão juntas até o próximo acorde; as do último começo ficam como estão. */
inline int legato (std::vector<Note>& notes, const std::set<int>& ids)
{
    std::vector<int> starts;

    for (auto& n : notes)
        if (picked (ids, n))
            starts.push_back (n.start);

    std::sort (starts.begin(), starts.end());
    int changed = 0;

    for (auto& n : notes)
    {
        if (! picked (ids, n))
            continue;

        const auto next = std::upper_bound (starts.begin(), starts.end(), n.start);

        if (next != starts.end() && *next - n.start != n.length)
        {
            n.length = *next - n.start;
            ++changed;
        }
    }

    return changed;
}

/** Juntar (Ctrl+G): notas da mesma tecla que se encostam ou se cruzam viram uma só, com o começo, a força
    e o slide da primeira. Devolve os ids das notas que saíram. */
inline std::vector<int> glue (std::vector<Note>& notes, const std::set<int>& ids)
{
    std::vector<Note*> pickedNotes;

    for (auto& n : notes)
        if (picked (ids, n))
            pickedNotes.push_back (&n);

    std::stable_sort (pickedNotes.begin(), pickedNotes.end(), [] (const Note* a, const Note* b)
                      { return a->key != b->key ? a->key < b->key : a->start < b->start; });

    std::vector<int> removed;
    Note* current = nullptr;

    for (auto* n : pickedNotes)
    {
        if (current != nullptr && current->key == n->key && n->start <= current->end())
        {
            current->length = juce::jmax (current->end(), n->end()) - current->start;
            removed.push_back (n->id);
            continue;
        }

        current = n;
    }

    std::erase_if (notes, [&removed] (const Note& n) { return std::find (removed.begin(), removed.end(), n.id) != removed.end(); });
    return removed;
}

/** Mudo (Alt+M) e tirar o mudo (Alt+Shift+M). Devolve quantas mudaram. */
inline int setMuted (std::vector<Note>& notes, const std::set<int>& ids, bool muted)
{
    int changed = 0;

    for (auto& n : notes)
        if (picked (ids, n) && n.muted != muted)
        {
            n.muted = muted;
            ++changed;
        }

    return changed;
}

/** Humanizar: cada nota anda até `timingTicks` para os lados e a força muda até `velocityAmount`, ao acaso.
    Nenhuma nota vai para antes do começo do padrão. */
inline int humanize (std::vector<Note>& notes, const std::set<int>& ids, int timingTicks, int velocityAmount, juce::Random& rng)
{
    int changed = 0;

    for (auto& n : notes)
    {
        if (! picked (ids, n))
            continue;

        const auto before = n;
        const int dt = timingTicks > 0 ? rng.nextInt (2 * timingTicks + 1) - timingTicks : 0;
        const int dv = velocityAmount > 0 ? rng.nextInt (2 * velocityAmount + 1) - velocityAmount : 0;
        n.start = juce::jmax (0, n.start + dt);
        n.velocity = juce::jlimit (1, 127, n.velocity + dv);

        if (! (n == before))
            ++changed;
    }

    return changed;
}

/** Strum (dedilhar, bloco 3, 23/10; elogiado no piano roll do Live): nas notas que começam juntas (um acorde),
    cada uma entra `stepTicks` depois da de baixo, da grave para a aguda, e o fim de cada uma fica onde estava.
    Devolve quantas mudaram. */
inline int strum (std::vector<Note>& notes, const std::set<int>& ids, int stepTicks)
{
    std::map<int, std::vector<Note*>> chords;   // por começo

    for (auto& n : notes)
        if (picked (ids, n))
            chords[n.start].push_back (&n);

    int changed = 0;

    for (auto& [start, list] : chords)
    {
        if (list.size() < 2)
            continue;

        std::sort (list.begin(), list.end(), [] (const Note* a, const Note* b) { return a->key < b->key; });

        for (size_t i = 1; i < list.size(); ++i)
        {
            auto& n = *list[i];
            const int end = n.end();
            n.start = start + juce::jmin ((int) i * juce::jmax (0, stepTicks), n.length - 1);
            n.length = juce::jmax (1, end - n.start);
            ++changed;
        }
    }

    return changed;
}

/** A nota da escala mais perto de `key` (empate: a de baixo). */
inline int nearestInScale (int key, int scaleRoot, const std::vector<int>& degrees)
{
    auto inScale = [&] (int k)
    {
        const int d = ((k - scaleRoot) % 12 + 12) % 12;
        return std::find (degrees.begin(), degrees.end(), d) != degrees.end();
    };

    for (int distance = 0; distance < 12; ++distance)
    {
        if (key - distance >= 0 && inScale (key - distance))     return key - distance;
        if (key + distance <= 127 && inScale (key + distance))   return key + distance;
    }

    return key;
}

/** Levar para a escala: cada nota fora da escala vai para a nota da escala mais perto. */
inline int toScale (std::vector<Note>& notes, const std::set<int>& ids, int scaleRoot, const std::vector<int>& degrees)
{
    int changed = 0;

    for (auto& n : notes)
    {
        if (! picked (ids, n))
            continue;

        const int k = nearestInScale (n.key, scaleRoot, degrees);

        if (k != n.key)
        {
            n.key = k;
            ++changed;
        }
    }

    return changed;
}

/** Um acorde do carimbo: id (usado no app) e nome na tela. */
struct ChordShape { const char* id; const char* label; };

inline const std::vector<ChordShape>& chordShapes()
{
    // os da escala primeiro: montam o acorde com as notas do tom do projeto (como o carimbo do FL)
    static const std::vector<ChordShape> list {
        { "scale3", "Tríade da escala" },
        { "scale7", "Da escala, com 7ª" },
        { "scale9", "Da escala, com 9ª" },
        { "maj",    "Maior" },
        { "min",    "Menor" },
        { "power",  "Quinta (power)" },
        { "sus2",   "Sus2" },
        { "sus4",   "Sus4" },
        { "maj7",   "Maior com 7ª maior" },
        { "min7",   "Menor com 7ª" },
        { "dom7",   "Com 7ª (dominante)" },
        { "dim",    "Diminuto" },
        { "oct",    "Oitava" },
    };

    return list;
}

/** As teclas do acorde `id` com o baixo em `rootKey`. Nos acordes da escala, o baixo vai para a nota da
    escala mais perto e as outras sobem de terça em terça dentro da escala. Teclas fora de 0 a 127 saem. */
inline std::vector<int> chordKeys (const juce::String& id, int rootKey, int scaleRoot, const std::vector<int>& degrees)
{
    std::vector<int> intervals;

    if (id.startsWith ("scale"))
    {
        const int size = id == "scale9" ? 5 : id == "scale7" ? 4 : 3;

        if (degrees.size() >= 12)   // escala cromática: sem terças da escala, vai a maior
            intervals = std::vector<int> { 0, 4, 7, 11, 14 };
        else
        {
            const int bass = nearestInScale (rootKey, scaleRoot, degrees);
            const int bassDegree = ((bass - scaleRoot) % 12 + 12) % 12;
            const int count = (int) degrees.size();
            const int index = (int) (std::find (degrees.begin(), degrees.end(), bassDegree) - degrees.begin());
            std::vector<int> keys;

            for (int i = 0; i < size; ++i)
            {
                const int step = index + 2 * i;
                const int k = bass - bassDegree + degrees[(size_t) (step % count)] + 12 * (step / count);

                if (k >= 0 && k <= 127)
                    keys.push_back (k);
            }

            return keys;
        }

        intervals.resize ((size_t) size);
    }
    else if (id == "maj")    intervals = { 0, 4, 7 };
    else if (id == "min")    intervals = { 0, 3, 7 };
    else if (id == "power")  intervals = { 0, 7, 12 };
    else if (id == "sus2")   intervals = { 0, 2, 7 };
    else if (id == "sus4")   intervals = { 0, 5, 7 };
    else if (id == "maj7")   intervals = { 0, 4, 7, 11 };
    else if (id == "min7")   intervals = { 0, 3, 7, 10 };
    else if (id == "dom7")   intervals = { 0, 4, 7, 10 };
    else if (id == "dim")    intervals = { 0, 3, 6 };
    else if (id == "oct")    intervals = { 0, 12 };
    else                     intervals = { 0 };

    std::vector<int> keys;

    for (int i : intervals)
        if (rootKey + i >= 0 && rootKey + i <= 127)
            keys.push_back (rootKey + i);

    return keys;
}

} // namespace whynot::notetools
