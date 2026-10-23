#include "Genres.h"

#include <algorithm>
#include <array>

namespace whynot::ideas
{

namespace
{
    bool hasWord (std::string_view list, std::string_view word)
    {
        size_t pos = 0;

        while (pos <= list.size())
        {
            const auto end = std::min (list.find (' ', pos), list.size());

            if (list.substr (pos, end - pos) == word)
                return true;

            pos = end + 1;
        }

        return false;
    }

    // Ordem das células: quarter, eighths, sixteenths, eighthTwoSixteenths, twoSixteenthsEighth,
    //                    dotted, snap, offEighth, offSixteenths, triplet, rest, hold

    std::vector<GenreProfile> buildGenres()
    {
        std::vector<GenreProfile> g;

        //======================================================================
        g.push_back ({ "trap", "Trap", "Grave de 808, caixa no tempo 3 e chimbal com rolls; melodias curtas em loop.",
            130, 160, 140, Mood::dark, true,
            0.05f, 0.4f, 0.25f, false, false, 55, 72, 4,
            { { "x---------------", 0.1f }, { "x-------x-------", 0.4f }, { "x-----x-----x---", 0.65f }, { "x--x--x---x--x--", 0.9f } },
            72, 88, 1, "AaAB", 0.7f, 0.2f, 0.25f, 0.85f,
            { { 3, 3, 0.5f, 1, 1, 2, 0.5f, 1.5f, 0.5f, 0.5f, 2.5f, 1.5f } },
            { { 1, 3, 2.5f, 2.5f, 2.5f, 2, 1, 1, 1.5f, 1.5f, 0.8f, 0.5f } },
            28, 52,
            { { "r---------r-----", 0.2f }, { "r-----r---r-----", 0.5f }, { "r-----r--r---o--", 0.8f } },
            0.35f, 0.25f, 1.0f, false,
            240,
            {
                { "x.........x....." "x......x..x.....",
                  "........x......." "........x.......",
                  "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.3.",
                  "", "", 0.3f },
                { "x.....x...x....." "x.x.......x..x..",
                  "........x......." "........x.....x.",
                  "x.xxx.x.x.xxx.x." "x.x.x.x.x.x.2.4.",
                  "................" "...............x", "", 0.55f },
                { "x.....x.x.x....." "x..x......x.x...",
                  "........x......." "........x...g..x",
                  "xxxxxxxxxxxx3.xx" "xxxxxx4.xxxx6.3.",
                  "", "", 0.8f },
                { "x..............." "x.........x.....",
                  "........x......." "........x.......",
                  "x...x...x...x..." "x...x...x...x.2.",
                  "......x........." "......x.........", "", 0.15f },
            } });

        //======================================================================
        g.push_back ({ "drill", "Drill", "Escuro e frio: chimbal em grupos de 3, caixa deslocada e 808 que desliza entre as notas.",
            138, 145, 142, Mood::dark, true,
            0.0f, 0.25f, 0.1f, false, false, 53, 72, 4,
            { { "x---------------", 0.1f }, { "x-------x-------", 0.4f }, { "x--x--x-x--x--x-", 0.85f } },
            67, 84, 1, "AaAB", 0.65f, 0.15f, 0.3f, 0.9f,
            { { 2, 2, 0.3f, 1, 1, 2.5f, 1, 2, 0.5f, 2, 3, 2 } },
            { { 1, 2, 1.5f, 2, 2, 2, 1.5f, 1.5f, 1.5f, 3, 1, 0.6f } },
            28, 52,
            { { "r---------r-----", 0.2f }, { "r-----r---r---a-", 0.6f } },
            0.75f, 0.2f, 1.0f, true,
            320,
            {
                { "x..........x...." "..x.......x.....",
                  "........x......." "........x..g....",
                  "x..x..x.x..x..x." "x..x..x.x..x.3x.",
                  "", "", 0.4f },
                { "x......x..x....." "x.........x..x..",
                  "........x......." "........x.....x.",
                  "x.x..x.x..x.x..x" "x.x..x.x..x.3..4",
                  "", "...x..........x." "......x.........", 0.65f },
                { "x..x......x....." "x......x..x..x..",
                  "........x......g" "........x...x...",
                  "x.xx.xx.x.xx.x3." "x.xx.xx.x.x.4.6.",
                  "", "", 0.85f },
            } });

        //======================================================================
        g.push_back ({ "funk", "Funk", "Funk BR: tamborzão com o grave junto do bumbo, palmas no 2 e no 4 e percussão no contratempo.",
            125, 150, 130, Mood::dark, true,
            0.0f, 0.2f, 0.05f, false, false, 57, 74, 4,
            { { "x---....x---....", 0.2f }, { "x-..x-..x-..x-..", 0.5f }, { "..x-..x-..x-..x-", 0.6f }, { "x-.x-.x-..x-.x-.", 0.85f } },
            69, 86, 1, "AAaB", 0.7f, 0.3f, 0.2f, 0.6f,
            { { 1.5f, 2, 0.5f, 1, 1, 2, 1, 3, 1.5f, 0, 3, 0.5f } },
            { { 0.5f, 2, 2, 2, 2, 2, 1.5f, 2.5f, 2.5f, 0.2f, 1.2f, 0.2f } },
            28, 52,
            { { "r.......r.r...r.", 0.3f }, { "r--r--r---r-r---", 0.6f } },
            0.08f, 0.1f, 0.35f, false,
            240,
            {
                { "x.......x.x...x." "x.......x.x...x.",
                  "....x.......x..." "....x.......x...",
                  "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.x.",
                  "", "...x...x...x...x" "...x...x...x...x", 0.5f },
                { "x..x..x...x..x.." "x..x..x...x.x.x.",
                  "....x.......x..." "....x.......x...",
                  "xgxgxgxgxgxgxgxg" "xgxgxgxgxgxgxgxg",
                  "", "..x...x...x...x." "..x...x...x...x.", 0.7f },
                { "x...x...x...x..." "x...x...x...x.x.",
                  "....x.......x..." "....x.......x...",
                  "..x...x...x...x." "..x...x...x...x.",
                  "", "...x......x....." "...x......x..x..", 0.4f },
            } });

        //======================================================================
        g.push_back ({ "rap", "Rap", "Rap de piano e cordas: caixa no 2 e no 4, bumbo que conversa com o baixo.",
            85, 100, 90, Mood::dark, true,
            0.15f, 0.5f, 0.3f, true, false, 52, 71, 4,
            { { "x---------------", 0.1f }, { "x-------x-----..", 0.35f }, { "x-----x-------..", 0.5f }, { "x--.x-.x--.x-...", 0.8f } },
            67, 84, 2, "ABAb", 0.65f, 0.15f, 0.2f, 0.85f,
            { { 3, 3, 0.3f, 0.8f, 0.8f, 2, 0.5f, 1.5f, 0.3f, 0.3f, 2.5f, 2 } },
            { { 1.5f, 3, 1.5f, 2, 2, 2, 1, 1.5f, 1, 0.6f, 1, 1 } },
            28, 52,
            { { "r-------r-------", 0.15f }, { "r------.r-r-----", 0.4f }, { "r--r----r-r---a-", 0.7f } },
            0.15f, 0.1f, 0.9f, false,
            480,
            {
                { "x......x..x....." "x......x..x.....",
                  "....x.......x..." "....x.......x...",
                  "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.x.",
                  "", "", 0.35f },
                { "x.x.......x....." "x......x.xx.....",
                  "....x.......x..." "....x.......x..g",
                  "x.xgx.xgx.xgx.xg" "x.xgx.xgx.xgx...",
                  "................" "..............x.", "", 0.6f },
                { "x.....x...x..x.." "x.x.......x.....",
                  "....x.......x..." "....x.......x...",
                  "xxxxxxxxxxxx3.xx" "xxxxxxxxxx2.4.xx",
                  "", "", 0.8f },
            } });

        //======================================================================
        g.push_back ({ "pop", "Pop", "Refrão que gruda: tonalidade maior, frases de dois compassos e baixo marcando o tempo.",
            95, 128, 110, Mood::happy, false,
            0.0f, 0.3f, 0.3f, true, false, 55, 72, 4,
            { { "x---------------", 0.1f }, { "x-------x-------", 0.3f }, { "x---x---x---x---", 0.55f }, { "x-x-x-x-x-x-x-x-", 0.85f } },
            64, 79, 2, "ABab", 0.7f, 0.12f, 0.15f, 0.95f,
            { { 3, 3, 0.2f, 1, 1, 2, 0.3f, 1.5f, 0.2f, 0.1f, 2, 2.5f } },
            { { 1.5f, 3.5f, 1, 2, 2, 2.5f, 0.8f, 2, 0.8f, 0.3f, 1, 1.2f } },
            28, 52,
            { { "r-------r-------", 0.1f }, { "r-r-r-r-r-r-r-r-", 0.5f }, { "r---r-f-r---o-a-", 0.75f }, { "r--r--r-r--r--a-", 0.9f } },
            0.05f, 0.05f, 0.9f, false,
            240,
            {
                { "x.......x.x....." "x.......x.x.....",
                  "....x.......x..." "....x.......x...",
                  "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.x.",
                  "", "", 0.3f },
                { "x...x...x...x..." "x...x...x...x...",
                  "....x.......x..." "....x.......x...",
                  "xx.xxx.xxx.xxx.x" "xx.xxx.xxx.xxx.x",
                  "..x...x...x...x." "..x...x...x...x.", "", 0.6f },
                { "x.....x.x.x....." "x.....x.x...x...",
                  "....x.......x..." "....x.......x.x.",
                  "xgxgxgxgxgxgxgxg" "xgxgxgxgxgxgxgxg",
                  "", "", 0.8f },
            } });

        //======================================================================
        g.push_back ({ "rnb", "R&B", "Acordes com sétima e nona, voicings abertos e grooves sem pressa.",
            60, 100, 80, Mood::chill, true,
            0.6f, 0.4f, 0.7f, true, true, 55, 74, 5,
            { { "x---------------", 0.1f }, { "x-----x---------", 0.35f }, { "x-----x-----x---", 0.55f }, { "x--x----x--x-x--", 0.85f } },
            65, 81, 2, "ABab", 0.55f, 0.12f, 0.2f, 0.98f,
            { { 2, 2, 0.3f, 1, 1, 2.5f, 1, 2, 1, 1, 2, 2.5f } },
            { { 1, 2, 1.5f, 2, 2.5f, 2.5f, 1.5f, 2, 2, 1.5f, 0.8f, 1.2f } },
            28, 52,
            { { "r-------r-------", 0.15f }, { "r-----r--f--o-a-", 0.6f }, { "r--.r-.f--r-o-a-", 0.85f } },
            0.2f, 0.1f, 1.0f, false,
            240,
            {
                { "x......x.x......" "x......x..x.....",
                  "....x.......x..." "....x.......x...",
                  "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.3.",
                  "", "", 0.3f },
                { "x..x......x....." "x......x.x......",
                  "....x.......x..." "....x.......x...",
                  "xgxgxgxgxgxgxgxg" "xgxgxgxgxgx.3.4.",
                  "", "......x........." "......x.........", 0.6f },
                { "x..x...x.x....x." "x......x..x..x..",
                  "....x.......x..." "....x..g....x...",
                  "xxxxxxxx2.xxxxxx" "xxxxxxxxxxxx4.6.",
                  "", "", 0.85f },
            } });

        //======================================================================
        g.push_back ({ "phonk", "Phonk", "Cowbell em loop no modo frígio, 808 distorcido e bateria pesada.",
            130, 160, 150, Mood::dark, true,
            0.0f, 0.2f, 0.05f, false, false, 55, 72, 4,
            { { "x---------------", 0.1f }, { "x-------x-------", 0.5f }, { "x--x--x-x--x--x-", 0.85f } },
            72, 89, 1, "AAaB", 0.6f, 0.35f, 0.25f, 0.45f,
            { { 1, 3, 1.5f, 2, 2, 1.5f, 0.5f, 2.5f, 2, 0.3f, 1.5f, 0.2f } },
            { { 0.3f, 2, 4, 3, 3, 1, 0.5f, 1.5f, 3, 0.5f, 0.5f, 0.1f } },
            28, 52,
            { { "r-----r---r-----", 0.4f }, { "r--r--r-r--r--r-", 0.8f } },
            0.4f, 0.3f, 1.0f, false,
            240,
            {
                { "x.....x...x....." "x.x...x...x.....",
                  "....x.......x..." "....x.......x...",
                  "xxxxxxxxxxxxxxxx" "xxxxxxxxxxxx2.4.",
                  "", "", 0.5f },
                { "x.........x....." "x.....x...x.....",
                  "........x......." "........x.......",
                  "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x3x.",
                  "", "", 0.3f },
                { "x.x...x...x.x..." "x.x...x.x.x.....",
                  "....x.......x..." "....x.......x.x.",
                  "xgxgxgxgxgxgxgxg" "xgxgxgxgxg3.4.6.",
                  "", "", 0.8f },
            } });

        //======================================================================
        g.push_back ({ "lofi", "Lo-fi / boom bap", "Acordes jazzy com sétima e nona, poucas notas e muito espaço.",
            70, 92, 82, Mood::chill, true,
            0.7f, 0.3f, 0.6f, true, true, 53, 72, 5,
            { { "x---------------", 0.1f }, { "x------.x--x----", 0.45f }, { "x--x----..x-----", 0.6f }, { "x-.x-.x-x-.x-...", 0.85f } },
            67, 84, 2, "ABab", 0.55f, 0.12f, 0.2f, 0.9f,
            { { 3, 2.5f, 0.2f, 0.6f, 0.6f, 2, 0.8f, 2, 0.3f, 0.8f, 3, 2.5f } },
            { { 2, 3, 0.8f, 1.5f, 1.5f, 2.5f, 1.2f, 2, 1, 1.2f, 1.5f, 1.5f } },
            28, 52,
            { { "r-------r-------", 0.15f }, { "r---------r--a--", 0.4f }, { "r-----.r--f-----", 0.5f }, { "r--.r--f--r-..a-", 0.8f } },
            0.05f, 0.05f, 0.8f, false,
            480,
            {
                { "x.......x.x....." "x......x..x.....",
                  "....x.......x..." "....x.......x...",
                  "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.x.",
                  "", "", 0.3f },
                { "x.....x...x....." "x.x.......x..x..",
                  "....x.......x..." "....x.......x..g",
                  "x.xgx.xgx.xgx.xg" "x.xgx.xgx.xgx...",
                  "................" "..............x.", "...........x...." "...........x....", 0.55f },
                { "x..x....x.x....." "x......x.x..x...",
                  "....x..g....x..." "....x.......x.g.",
                  "xgxgxgxgxgxgxgxg" "xgxgxgxgxgxgxgxg",
                  "", "", 0.8f },
            } });

        return g;
    }
}

//==============================================================================
std::string_view moodId (Mood m)
{
    switch (m)
    {
        case Mood::dark:  return "dark";
        case Mood::sad:   return "sad";
        case Mood::chill: return "chill";
        case Mood::happy: return "happy";
        case Mood::epic:  return "epic";
    }

    return "dark";
}

std::string_view moodName (Mood m)
{
    switch (m)
    {
        case Mood::dark:  return "Sombrio";
        case Mood::sad:   return "Triste";
        case Mood::chill: return "Calmo";
        case Mood::happy: return "Alegre";
        case Mood::epic:  return "Épico";
    }

    return "Sombrio";
}

bool parseMood (std::string_view id, Mood& out)
{
    for (auto m : allMoods())
        if (moodId (m) == id)
        {
            out = m;
            return true;
        }

    return false;
}

std::span<const Mood> allMoods()
{
    static const std::array<Mood, 5> moods { Mood::dark, Mood::sad, Mood::chill, Mood::happy, Mood::epic };
    return moods;
}

std::string_view partId (Part p)
{
    switch (p)
    {
        case Part::chords:       return "chords";
        case Part::melody:       return "melody";
        case Part::bass:         return "bass";
        case Part::eightOhEight: return "808";
        case Part::arp:          return "arp";
        case Part::drums:        return "drums";
    }

    return "chords";
}

std::string_view partName (Part p)
{
    switch (p)
    {
        case Part::chords:       return "Acordes";
        case Part::melody:       return "Melodia";
        case Part::bass:         return "Baixo";
        case Part::eightOhEight: return "808";
        case Part::arp:          return "Arpejo";
        case Part::drums:        return "Bateria";
    }

    return "Acordes";
}

bool parsePart (std::string_view id, Part& out)
{
    for (auto p : allParts())
        if (partId (p) == id)
        {
            out = p;
            return true;
        }

    return false;
}

std::span<const Part> allParts()
{
    static const std::array<Part, 6> parts { Part::chords, Part::melody, Part::bass, Part::eightOhEight, Part::arp, Part::drums };
    return parts;
}

std::vector<std::string_view> soundCategories (Part part, std::string_view genre)
{
    const auto is = [genre] (std::initializer_list<std::string_view> ids)
    {
        return std::find (ids.begin(), ids.end(), genre) != ids.end();
    };

    switch (part)
    {
        case Part::chords:
            if (is ({ "rnb", "lofi", "rap" }))  return { "keys", "pad" };
            if (is ({ "drill" }))               return { "strings", "pad", "keys" };
            return { "pad", "keys" };

        case Part::melody:
            if (is ({ "trap" }))                return { "bell", "pluck", "lead" };
            if (is ({ "phonk" }))               return { "bell", "lead", "pluck" };
            if (is ({ "drill" }))               return { "keys", "strings", "bell" };
            if (is ({ "rnb", "lofi", "rap" }))  return { "keys", "pluck", "lead" };
            if (is ({ "funk" }))                return { "lead", "pluck", "brass" };
            return { "lead", "pluck", "keys" };

        case Part::arp:          return { "arp", "pluck", "bell" };
        case Part::bass:         return { "bass", "808" };
        case Part::eightOhEight: return { "808", "bass" };
        case Part::drums:        return {};
    }

    return {};
}

std::span<const GenreProfile> allGenres()
{
    static const std::vector<GenreProfile> genres = buildGenres();
    return genres;
}

const GenreProfile* findGenre (std::string_view id)
{
    for (auto& g : allGenres())
        if (id == g.id)
            return &g;

    return nullptr;
}

//==============================================================================
std::span<const ProgressionEntry> allProgressions()
{
    static const ProgressionEntry table[] = {
        // Menor
        { "i VI III VII",       true,  "trap pop drill rnb phonk funk",  "sad epic" },
        { "i VII VI VII",       true,  "trap rap funk",                  "epic dark" },
        { "i iv VI V",          true,  "trap drill rap",                 "dark sad" },
        { "i VI iv V",          true,  "drill trap",                     "dark" },
        { "i iv i V",           true,  "drill phonk rap",                "dark" },
        { "i bII i bII",        true,  "phonk drill trap",               "dark" },
        { "i bII VI V",         true,  "drill phonk",                    "dark epic" },
        { "i*2 VI*2",           true,  "trap rnb pop",                   "sad chill" },
        { "i*2 iv*2",           true,  "rap funk phonk lofi",            "dark chill" },
        { "i VII VI V",         true,  "drill trap phonk funk rap",      "dark epic" },
        { "VI VII i i",         true,  "trap pop",                       "epic sad" },
        { "i VI III V",         true,  "trap drill",                     "epic" },
        { "i III VII iv",       true,  "pop rap trap",                   "sad" },
        { "i*4",                true,  "funk phonk",                     "dark" },
        { "i*2 VII*2",          true,  "funk trap rap",                  "epic dark" },
        { "i7*2 iv7*2",         true,  "lofi rap rnb",                   "chill sad" },
        { "i9 iv9 VII7 IIImaj7", true, "rnb lofi",                       "chill sad" },
        { "iih7 V7 i7 i7",      true,  "lofi rap rnb",                   "chill dark" },
        { "VImaj7 v7 iv7 i7",   true,  "lofi rnb",                       "sad chill" },

        // Maior
        { "I V vi IV",          false, "pop rnb funk trap",              "happy epic" },
        { "vi IV I V",          false, "pop trap rnb",                   "sad epic" },
        { "I vi IV V",          false, "pop",                            "happy" },
        { "IV I V vi",          false, "pop trap",                       "epic happy" },
        { "vi V IV V",          false, "pop trap",                       "sad epic" },
        { "Imaj7 vi7 ii7 V7",   false, "lofi rnb",                       "happy chill" },
        { "ii7 V7 Imaj7 Imaj7", false, "lofi rnb rap",                   "chill happy" },
        { "IVmaj7 iii7 vi7 vi7", false, "rnb lofi",                      "chill sad" },
        { "IVmaj7 III7 vi7 vi7", false, "rnb lofi pop",                  "sad chill" },
        { "I iii IV iv",        false, "pop rnb",                        "sad happy" },
        { "IVmaj7 V iii7 vi7",  false, "pop rnb",                        "sad chill" },
        { "I*2 IV*2",           false, "funk pop",                       "happy" },
        { "vi*2 IV*2",          false, "trap drill phonk funk rap",      "dark sad" },
    };

    return table;
}

std::vector<const ProgressionEntry*> progressionsFor (std::string_view genre, bool minor, Mood mood)
{
    std::vector<const ProgressionEntry*> out;

    for (auto& e : allProgressions())
        if (e.minor == minor && hasWord (e.genres, genre))
            out.push_back (&e);

    if (out.empty())
        for (auto& e : allProgressions())
            if (e.minor == minor)
                out.push_back (&e);

    std::stable_sort (out.begin(), out.end(), [mood] (auto* a, auto* b)
    {
        return hasWord (a->moods, moodId (mood)) && ! hasWord (b->moods, moodId (mood));
    });

    return out;
}

} // namespace whynot::ideas
