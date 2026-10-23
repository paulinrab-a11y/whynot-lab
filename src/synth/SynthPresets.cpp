#include "SynthPresets.h"
#include "PresetBuilder.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace whynot::synth
{

namespace
{
    const GenreInfo genreTable[] = {
        { "trap", "Trap", presets::TRAP },
        { "funk", "Funk BR", presets::FUNK },
        { "rap", "Rap", presets::RAP },
        { "pop", "Pop", presets::POP },
        { "drill", "Drill", presets::DRILL },
        { "rnb", "R&B", presets::RNB },
        { "phonk", "Phonk", presets::PHONK },
        { "lofi", "Lo-fi / boom bap", presets::LOFI },
    };

    const CategoryInfo categories[] = {
        { "808", "808", 36 },
        { "bass", "Baixo", 36 },
        { "lead", "Lead", 60 },
        { "pad", "Pad", 60 },
        { "pluck", "Pluck", 60 },
        { "keys", "Keys", 60 },
        { "bell", "Bell", 72 },
        { "brass", "Brass", 60 },
        { "strings", "Cordas synth", 60 },
        { "vox", "Vox synth", 60 },
        { "arp", "Arp/Seq", 60 },
        { "fx", "FX", 60 },
        { "texture", "Textura", 60 },
    };

    struct Level { const char* id; float volumeDb; };

    const Level levels[] = {
#include "PresetLevels.inc"
        { nullptr, 0.0f }
    };

    /** Minúsculas e sem acento (UTF-8 do português), para a busca. */
    std::string fold (std::string_view s)
    {
        static const std::map<std::string, char> accents = {
            { "á", 'a' }, { "à", 'a' }, { "â", 'a' }, { "ã", 'a' }, { "ä", 'a' }, { "Á", 'a' }, { "À", 'a' }, { "Â", 'a' }, { "Ã", 'a' },
            { "é", 'e' }, { "ê", 'e' }, { "è", 'e' }, { "É", 'e' }, { "Ê", 'e' },
            { "í", 'i' }, { "Í", 'i' },
            { "ó", 'o' }, { "ô", 'o' }, { "õ", 'o' }, { "Ó", 'o' }, { "Ô", 'o' }, { "Õ", 'o' },
            { "ú", 'u' }, { "ü", 'u' }, { "Ú", 'u' },
            { "ç", 'c' }, { "Ç", 'c' },
        };

        std::string out;
        out.reserve (s.size());

        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto c = (unsigned char) s[i];

            if (c >= 0xc0 && i + 1 < s.size())
            {
                if (auto it = accents.find (std::string (s.substr (i, 2))); it != accents.end())
                {
                    out += it->second;
                    ++i;
                    continue;
                }
            }

            out += (char) std::tolower (c);
        }

        return out;
    }

    std::vector<Preset> build()
    {
        std::vector<Preset> list;
        list.reserve (512);

        presets::add808AndBass (list);
        presets::addLeadsAndPlucks (list);
        presets::addKeysAndOrchestral (list);
        presets::addPadsAndTextures (list);
        presets::addSecondWave (list);
        presets::addGenreWave (list);
        presets::addFinalWave (list);

        for (auto& p : list)
        {
            for (const auto* l = levels; l->id != nullptr; ++l)
            {
                if (p.id == l->id)
                {
                    p.values[(size_t) P::volume] = param (P::volume).clamp (l->volumeDb);
                    p.levelled = true;
                    break;
                }
            }
        }

        return list;
    }

    const std::vector<Preset>& library()
    {
        static const std::vector<Preset> list = build();
        return list;
    }
}

bool Preset::hasGenre (std::string_view genreId) const
{
    const auto* g = findGenre (genreId);
    return g != nullptr && (genres & g->bit) != 0;
}

std::vector<std::string> Preset::genreIds() const
{
    std::vector<std::string> out;

    for (auto& g : genreTable)
        if ((this->genres & g.bit) != 0)
            out.push_back (g.id);

    return out;
}

std::span<const GenreInfo> allGenres()           { return genreTable; }
std::span<const CategoryInfo> allCategories()    { return categories; }

const GenreInfo* findGenre (std::string_view id)
{
    for (auto& g : genreTable)
        if (id == g.id)
            return &g;

    return nullptr;
}

const CategoryInfo* findCategory (std::string_view id)
{
    for (auto& c : categories)
        if (id == c.id)
            return &c;

    return nullptr;
}

bool isChordCategory (std::string_view category) noexcept
{
    return category == "pad" || category == "keys" || category == "strings" || category == "brass" || category == "vox";
}

std::span<const Preset> allPresets()
{
    const auto& list = library();
    return { list.data(), list.size() };
}

const Preset* findPreset (std::string_view id)
{
    if (id == initPreset().id)
        return &initPreset();

    for (auto& p : library())
        if (p.id == id)
            return &p;

    return nullptr;
}

std::vector<const Preset*> findPresets (std::string_view category, std::string_view genre, std::string_view search)
{
    std::vector<const Preset*> out;
    const auto needle = fold (search);

    for (auto& p : library())
    {
        if (! category.empty() && p.category != category)
            continue;

        if (! genre.empty() && ! p.hasGenre (genre))
            continue;

        if (! needle.empty())
        {
            const auto* c = findCategory (p.category);
            const auto haystack = fold (p.name + " " + p.description + " " + (c != nullptr ? c->name : ""));

            // Cada palavra da busca precisa aparecer (em qualquer ordem).
            bool all = true;
            size_t start = 0;

            while (start < needle.size() && all)
            {
                const auto end = needle.find (' ', start);
                const auto word = needle.substr (start, end == std::string::npos ? std::string::npos : end - start);

                if (! word.empty() && haystack.find (word) == std::string::npos)
                    all = false;

                start = end == std::string::npos ? needle.size() : end + 1;
            }

            if (! all)
                continue;
        }

        out.push_back (&p);
    }

    return out;
}

const Preset& initPreset()
{
    static const Preset init = []
    {
        Preset p;
        p.id = "init";
        p.name = "Init";
        p.category = "lead";
        p.genres = 0;
        p.description = "Ponto de partida: uma serra, filtro aberto.";
        p.values = defaultValues();

        for (const auto* l = levels; l->id != nullptr; ++l)
            if (p.id == l->id)
            {
                p.values[(size_t) P::volume] = param (P::volume).clamp (l->volumeDb);
                p.levelled = true;
            }

        return p;
    }();

    return init;
}

} // namespace whynot::synth
