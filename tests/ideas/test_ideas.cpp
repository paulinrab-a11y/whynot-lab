// Testes do WHYNOT Ideias (sem JUCE): teoria, dados dos gêneros, gerador, bateria,
// detecção de acordes e tonalidade, e a leitura de pedidos em português.

#include "../../src/ideas/Analysis.h"
#include "../../src/ideas/Generator.h"
#include "../../src/ideas/Genres.h"
#include "../../src/ideas/Prompt.h"
#include "../../src/ideas/Theory.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace whynot::ideas;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static const ChordEvent* chordAtTime (const std::vector<ChordEvent>& chords, int t)
{
    for (auto& e : chords)
        if (t >= e.start && t < e.end())
            return &e;

    return nullptr;
}

static Request request (const char* genre, Part part, uint32_t seed, int root = 9, const char* scale = "minor")
{
    Request r;
    r.genre = genre;
    r.part = part;
    r.seed = seed;
    r.scale = Scale::fromName (root, scale);
    return r;
}

//==============================================================================
static void testTheory()
{
    std::printf ("Teoria\n");

    const auto cMinor = Scale::fromName (0, "minor");
    CHECK (cMinor.contains (63) && ! cMinor.contains (64), "Dó menor tem Mib e não tem Mi");
    CHECK (cMinor.snap (64) == 63, "Mi vira Mib em Dó menor (empate vai para baixo)");
    CHECK (cMinor.stepFrom (60, 1) == 62 && cMinor.stepFrom (60, -1) == 58 && cMinor.stepFrom (60, 7) == 72,
           "andar pela escala: +1 = Ré, -1 = Sib, +7 = Dó acima");
    CHECK (Scale::fromName (0, "pentatonicMinor").harmonic().degrees.size() == 7, "pentatônica usa a menor natural para os acordes");

    const auto aMinor = Scale::fromName (9, "minor");
    auto vi = parseRoman ("VI", aMinor);
    CHECK (vi && vi->chord.root == 5 && vi->chord.name (false) == "F", "VI em Lá menor = F");
    auto bII = parseRoman ("bII", aMinor);
    CHECK (bII && bII->chord.root == 10 && bII->chord.suffix().empty(), "bII em Lá menor = Sib maior");
    auto v7 = parseRoman ("V7", aMinor);
    CHECK (v7 && v7->chord.name (false) == "E7", "V7 em Lá menor = E7 (dominante da harmônica)");
    auto ii = parseRoman ("iih7", Scale::fromName (0, "minor"));
    CHECK (ii && ii->chord.name (true) == "Dm7b5", "iih7 em Dó menor = Dm7b5");
    auto iv = parseRoman ("IVmaj7", Scale::fromName (0, "major"));
    CHECK (iv && iv->chord.name (false) == "Fmaj7" && iv->explicitExtension, "IVmaj7 em Dó maior = Fmaj7");
    CHECK (! parseRoman ("Iv", aMinor) && ! parseRoman ("VIII", aMinor) && ! parseRoman ("i13", aMinor), "graus inválidos são recusados");

    auto prog = parseProgression ("i VI III VII", aMinor);
    CHECK (prog.size() == 4 && prog[0].chord.root == 9 && prog[1].chord.root == 5 && prog[2].chord.root == 0 && prog[3].chord.root == 7,
           "i VI III VII em Lá menor = Am F C G");
    auto weighted = parseProgression ("i*2 - iv*2", aMinor);
    CHECK (weighted.size() == 2 && weighted[0].weight == 2 && weighted[1].weight == 2, "*2 dura duas unidades e '-' separa");
    CHECK (parseProgression ("i XX", aMinor).empty() && parseProgression ("", aMinor).empty(), "progressão inválida volta vazia");

    Chord am { 9, { 0, 3, 7 } }, f { 5, { 0, 4, 7 } }, c { 0, { 0, 4, 7 } }, g { 7, { 0, 4, 7 } };
    std::vector<int> prev;
    int worst = 0;
    bool inRange = true, rightNotes = true;

    for (auto* ch : { &am, &f, &c, &g, &am })
    {
        auto v = voiceChord (*ch, prev, 55, 72, 4, false);

        for (int k : v)
        {
            inRange = inRange && k >= 55 && k <= 72;
            rightNotes = rightNotes && ch->contains (k);
        }

        if (! prev.empty())
        {
            int moved = 0;

            for (size_t i = 0; i < std::min (v.size(), prev.size()); ++i)
                moved += std::abs (v[i] - prev[i]);

            worst = std::max (worst, moved);
        }

        prev = v;
    }

    CHECK (inRange && rightNotes, "voicings dentro da faixa e só com notas do acorde");
    CHECK (worst <= 7, "condução de vozes: no máximo %d semitons somados por troca (pior: %d)", 7, worst);

    Chord nine { 0, { 0, 2, 4, 7, 11 } };
    auto v5 = voiceChord (nine, {}, 55, 74, 4, false);
    CHECK (v5.size() == 4 && ! std::any_of (v5.begin(), v5.end(), [] (int k) { return pitchClass (k) == 7; }),
           "Cmaj9 em 4 vozes tira a quinta");
}

//==============================================================================
static void testGenreData()
{
    std::printf ("Dados dos gêneros\n");

    bool gridsOk = true, kickOnOne = true, progsOk = true;
    std::string bad;

    auto validGrid = [] (const char* grid, const char* allowed, bool allowEmpty, std::initializer_list<size_t> sizes)
    {
        const std::string g (grid);

        if (g.empty())
            return allowEmpty;

        if (std::find (sizes.begin(), sizes.end(), g.size()) == sizes.end())
            return false;

        return g.find_first_not_of (allowed) == std::string::npos;
    };

    for (auto& g : allGenres())
    {
        for (auto& r : g.chordRhythms)
            if (! validGrid (r.grid, "x-.", false, { 16, 32 }))  { gridsOk = false; bad = std::string (g.id) + " acordes " + r.grid; }

        for (auto& r : g.bassGrooves)
            if (! validGrid (r.grid, "rfoa-.", false, { 16, 32 })) { gridsOk = false; bad = std::string (g.id) + " baixo " + r.grid; }

        for (auto& b : g.beats)
        {
            for (auto* lane : { b.kick, b.snare, b.hat, b.openhat, b.perc })
                if (! validGrid (lane, "xXg.234568", true, { 32 }))  { gridsOk = false; bad = std::string (g.id) + " batida " + lane; }

            kickOnOne = kickOnOne && b.kick[0] == 'x';
        }

        for (bool minor : { true, false })
            for (auto* e : progressionsFor (g.id, minor, g.defaultMood))
                if (parseProgression (e->text, Scale::fromName (0, minor ? "minor" : "major")).empty())
                {
                    progsOk = false;
                    bad = e->text;
                }

        CHECK (! g.beats.empty() && ! g.chordRhythms.empty() && ! g.bassGrooves.empty(), "%s tem batidas, ritmos de acorde e de baixo", g.id);
    }

    CHECK (allGenres().size() == 8, "8 gêneros: trap, drill, funk, rap, pop, R&B, phonk, lo-fi");
    CHECK (gridsOk, "todas as grades têm 16/32 passos e só símbolos válidos %s", bad.c_str());
    CHECK (kickOnOne, "toda batida tem bumbo no primeiro passo");
    CHECK (progsOk, "todas as progressões do banco são lidas nos dois modos %s", bad.c_str());

    bool soundsOk = true;
    const std::set<std::string> synthCategories { "808", "bass", "lead", "pad", "pluck", "keys", "bell", "brass",
                                                  "strings", "vox", "arp", "fx", "texture" };

    for (auto& g : allGenres())
        for (auto part : allParts())
        {
            const auto cats = soundCategories (part, g.id);
            soundsOk = soundsOk && (part == Part::drums ? cats.empty() : ! cats.empty());

            for (auto c : cats)
                soundsOk = soundsOk && synthCategories.count (std::string (c)) == 1;
        }

    CHECK (soundsOk, "toda parte com notas tem categorias de som do synth (e a bateria nenhuma)");

    CHECK (progressionsFor ("trap", true, Mood::sad).front()->moods != nullptr
           && std::string (progressionsFor ("trap", true, Mood::sad).front()->moods).find ("sad") != std::string::npos,
           "o clima pedido vem primeiro na lista");
}

//==============================================================================
static void testGenerator()
{
    std::printf ("Gerador\n");

    const Part noteParts[] = { Part::chords, Part::melody, Part::arp, Part::bass, Part::eightOhEight };
    int combos = 0, okCount = 0, nonEmpty = 0, inBounds = 0, melodyInKey = 0, melodyNotes = 0, chordToneOk = 0, chordNotes = 0;

    for (auto& g : allGenres())
        for (auto part : noteParts)
            for (auto mood : allMoods())
                for (uint32_t seed : { 1u, 77u })
                {
                    auto r = request (g.id, part, seed, (int) (seed % 12), g.preferMinor ? "minor" : "major");
                    r.mood = mood;
                    const auto res = generate (r);
                    ++combos;
                    okCount += res.ok ? 1 : 0;
                    nonEmpty += res.notes.empty() ? 0 : 1;

                    bool bounds = true;

                    for (auto& n : res.notes)
                        bounds = bounds && n.start >= 0 && n.end() <= r.bars * ticksPerBar && n.length > 0
                                 && n.velocity >= 1 && n.velocity <= 127 && n.key >= 0 && n.key <= 127;

                    inBounds += bounds ? 1 : 0;

                    for (auto& n : res.notes)
                    {
                        auto* ev = chordAtTime (res.chords, n.start);

                        if (part == Part::melody)
                        {
                            ++melodyNotes;
                            melodyInKey += (r.scale.contains (n.key) || (ev != nullptr && ev->chord.contains (n.key))) ? 1 : 0;
                        }
                        else if (part == Part::chords || part == Part::arp)
                        {
                            ++chordNotes;
                            chordToneOk += (ev != nullptr && ev->chord.contains (n.key)) ? 1 : 0;
                        }
                    }
                }

    CHECK (okCount == combos, "todas as %d combinações (gênero × parte × clima × semente) geram", combos);
    CHECK (nonEmpty == combos, "nenhuma combinação sai vazia (%d de %d)", nonEmpty, combos);
    CHECK (inBounds == combos, "notas dentro do padrão, com duração, força e altura válidas");
    CHECK (melodyInKey == melodyNotes, "melodia só com notas da escala ou do acorde (%d de %d)", melodyInKey, melodyNotes);
    CHECK (chordToneOk == chordNotes, "acordes e arpejos só com notas do acorde (%d de %d)", chordToneOk, chordNotes);

    // Determinismo e variação
    auto r = request ("trap", Part::melody, 1234);
    const auto a = generate (r), b = generate (r);
    CHECK (a.notes == b.notes, "mesma semente, mesmas notas");

    int different = 0;

    for (uint32_t s = 1; s <= 20; ++s)
    {
        auto x = request ("trap", Part::melody, s), y = request ("trap", Part::melody, s + 1000);
        different += generate (x).notes != generate (y).notes ? 1 : 0;
    }

    CHECK (different >= 19, "sementes diferentes dão melodias diferentes (%d de 20)", different);

    auto v1 = r;
    v1.variation = 1;
    const auto varied = generate (v1);
    CHECK (varied.notes != a.notes && varied.progression == a.progression, "Variar muda a melodia e mantém a progressão");

    auto chordsReq = r;
    chordsReq.part = Part::chords;
    CHECK (generate (chordsReq).progression == a.progression, "acordes e melodia da mesma ideia seguem a mesma progressão");

    // Tamanhos
    bool coverage = true;

    for (int bars : { 1, 2, 3, 4, 8, 16 })
    {
        auto rb = request ("pop", Part::chords, 9, 0, "major");
        rb.bars = bars;
        const auto res = generate (rb);
        int total = 0, expectStart = 0;

        for (auto& e : res.chords)
        {
            coverage = coverage && e.start == expectStart;
            expectStart = e.end();
            total += e.length;
        }

        coverage = coverage && total == bars * ticksPerBar;
    }

    CHECK (coverage, "a harmonia cobre exatamente 1, 2, 3, 4, 8 e 16 compassos, sem buraco");

    // Densidade
    auto countNotes = [] (double density)
    {
        size_t total = 0;

        for (uint32_t s = 1; s <= 30; ++s)
        {
            auto rd = request ("trap", Part::melody, s);
            rd.density = density;
            total += generate (rd).notes.size();
        }

        return total;
    };

    const auto sparse = countNotes (0.1), busy = countNotes (0.9);
    CHECK (busy > sparse * 1.3, "mais densidade, mais notas (%zu contra %zu)", busy, sparse);

    // Complexidade: R&B cheio de sétimas, trap simples em tríade
    auto fourNotes = [] (const char* genre, double complexity)
    {
        int rich = 0, total = 0;

        for (uint32_t s = 1; s <= 30; ++s)
        {
            auto rc = request (genre, Part::chords, s);
            rc.complexity = complexity;

            for (auto& e : generate (rc).chords)
            {
                ++total;
                rich += e.chord.intervals.size() >= 4 ? 1 : 0;
            }
        }

        return (double) rich / std::max (1, total);
    };

    const double rnbRich = fourNotes ("rnb", 1.0), trapRich = fourNotes ("trap", 0.0);
    CHECK (rnbRich > 0.8, "R&B complexo: %.0f%% dos acordes com sétima ou nona", rnbRich * 100);
    CHECK (trapRich < 0.25, "trap simples: só %.0f%% com extensão", trapRich * 100);

    // Modos de 7 notas: as extensões automáticas e a melodia não saem da escala
    int extOutside = 0, melodyOutside = 0;

    for (const char* scaleName : { "phrygian", "dorian", "harmonicMinor" })
        for (uint32_t s = 1; s <= 20; ++s)
        {
            auto rm = request (s % 2 ? "phonk" : "lofi", Part::melody, s, (int) (s % 12), scaleName);
            rm.complexity = 1.0;
            rm.progression = "i iv VI VII";   // sem extensões escritas: todas vêm do gerador
            const auto res = generate (rm);

            for (auto& e : res.chords)
                for (int iv : e.chord.intervals)
                    if ((iv == 2 || iv == 9 || iv == 10 || iv == 11) && ! rm.scale.contains (e.chord.root + iv))
                        ++extOutside;

            rm.progression = "i9 iv9 VII7 IIImaj7";   // extensões escritas (de menor natural)
            for (auto& n : generate (rm).notes)
            {
                const auto* ev = chordAtTime (generate (rm).chords, n.start);
                const int rel = ev != nullptr ? pitchClass (n.key - ev->chord.root) : 0;

                if (! rm.scale.contains (n.key) && ! (rel == 0 || rel == 3 || rel == 4 || rel == 6 || rel == 7 || rel == 8))
                    ++melodyOutside;
            }
        }

    CHECK (extOutside == 0, "frígio, dórico e menor harmônica: extensões automáticas dentro da escala (%d fora)", extOutside);
    CHECK (melodyOutside == 0, "melodia não usa extensão do acorde que sai da escala (%d notas)", melodyOutside);

    // Progressão pedida
    auto rp = request ("trap", Part::chords, 5);
    rp.progression = "i iv VI V";
    const auto withProg = generate (rp);
    CHECK (withProg.ok && withProg.chords.size() == 4 && withProg.chords[3].chord.root == 4, "progressão pedida é respeitada (V de Lá menor = Mi)");
    rp.progression = "isso não é progressão";
    CHECK (! generate (rp).ok, "progressão inválida volta erro");
}

//==============================================================================
static void test808AndDrums()
{
    std::printf ("808 e bateria\n");

    bool mono = true, firstNoSlide = true, range = true, aligned = true;
    int drillSlides = 0, funkSlides = 0, funkNotes = 0, drillNotes = 0;

    for (auto& g : allGenres())
        for (uint32_t s = 1; s <= 12; ++s)
        {
            auto r = request (g.id, Part::eightOhEight, s);
            const auto res = generate (r);

            for (size_t i = 0; i < res.notes.size(); ++i)
            {
                const auto& n = res.notes[i];
                range = range && n.key >= 26 && n.key <= 52;

                if (i + 1 < res.notes.size())
                    mono = mono && n.end() <= res.notes[i + 1].start;

                if (std::string (g.id) == "drill") { drillSlides += n.slide ? 1 : 0; ++drillNotes; }
                if (std::string (g.id) == "funk")  { funkSlides += n.slide ? 1 : 0; ++funkNotes; }
            }

            firstNoSlide = firstNoSlide && (res.notes.empty() || ! res.notes.front().slide);

            if (std::string (g.id) == "trap")
            {
                const auto drums = generateDrums (r);
                const auto& kick = drums.lanes[0].second;

                for (auto& n : res.notes)
                {
                    const int step = n.start / ticksPerStep;
                    aligned = aligned && n.start % ticksPerStep == 0 && (step == 0 || kick[(size_t) step].on);
                }
            }
        }

    CHECK (mono, "808 é monofônico: uma nota termina antes da próxima");
    CHECK (firstNoSlide, "a primeira nota do 808 não desliza");
    CHECK (range, "808 no registro grave (Ré1 a Mi3)");
    CHECK (aligned, "no trap, o 808 cai nos bumbos da batida da mesma ideia");
    CHECK ((double) drillSlides / std::max (1, drillNotes) > 0.3 && (double) funkSlides / std::max (1, funkNotes) < 0.15,
           "drill desliza muito (%d/%d), funk quase nada (%d/%d)", drillSlides, drillNotes, funkSlides, funkNotes);

    bool shape = true, rolls = false;

    for (auto& g : allGenres())
    {
        auto r = request (g.id, Part::drums, 3);
        r.bars = 4;
        r.complexity = 0.8;
        const auto d = generateDrums (r);
        shape = shape && d.ok && d.lengthSteps == 64 && d.lanes.size() == 5 && d.lanes[0].first == "kick" && d.lanes[0].second[0].on;

        for (auto& [role, steps] : d.lanes)
        {
            shape = shape && (int) steps.size() == 64;

            for (auto& st : steps)
            {
                shape = shape && (! st.on || (st.velocity >= 1 && st.velocity <= 127));
                rolls = rolls || (role == "hat" && st.roll > 1);
            }
        }
    }

    CHECK (shape, "bateria: 5 raias com 16 passos por compasso, bumbo no 1");
    CHECK (rolls, "o chimbal ganha rolls");

    auto r = request ("trap", Part::drums, 8);
    const auto d0 = generateDrums (r);
    auto rv = r;
    rv.variation = 2;
    const auto d1 = generateDrums (rv);
    bool kickSame = true, somethingChanged = false;

    for (size_t i = 0; i < d0.lanes[0].second.size(); ++i)
        kickSame = kickSame && d0.lanes[0].second[i].on == d1.lanes[0].second[i].on;

    for (size_t l = 1; l < d0.lanes.size(); ++l)
        for (size_t i = 0; i < d0.lanes[l].second.size(); ++i)
            somethingChanged = somethingChanged || d0.lanes[l].second[i].on != d1.lanes[l].second[i].on
                                                || d0.lanes[l].second[i].roll != d1.lanes[l].second[i].roll;

    CHECK (kickSame && somethingChanged, "Variar a bateria mexe no chimbal e na caixa, não no bumbo");
}

//==============================================================================
static void testAnalysis()
{
    std::printf ("Análise\n");

    // C Am F G, um por compasso
    std::vector<GenNote> notes;
    const int prog[4][3] = { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } };

    for (int bar = 0; bar < 4; ++bar)
        for (int k : prog[bar])
            notes.push_back ({ bar * ticksPerBar, ticksPerBar, k, 100, false });

    const auto det = detectChords (notes, 4 * ticksPerBar);
    std::string names;

    for (auto& d : det)
        names += d.event.chord.name (false) + " ";

    CHECK (names == "C Am F G ", "C Am F G detectados (%s)", names.c_str());

    const auto key = detectKey (notes);
    CHECK (key.root == 0 && key.major, "tonalidade de C Am F G = Dó maior");

    // Slash chord: C com Mi no baixo
    std::vector<GenNote> inv { { 0, ticksPerBar, 52, 100, false }, { 0, ticksPerBar, 60, 100, false }, { 0, ticksPerBar, 67, 100, false } };
    const auto slash = detectChords (inv, ticksPerBar);
    CHECK (slash.size() == 1 && slash[0].event.chord.name (false) == "C/E", "inversão vira C/E (%s)",
           slash.empty() ? "-" : slash[0].event.chord.name (false).c_str());

    // Linha de uma nota só (baixo, 808): vira tríade do tom, não power chord
    std::vector<GenNote> line { { 0, 2 * ticksPerBeat, 45, 100, false }, { 2 * ticksPerBeat, 2 * ticksPerBeat, 40, 100, false },
                                { ticksPerBar, ticksPerBar, 41, 100, false }, { 2 * ticksPerBar, ticksPerBar, 48, 100, false },
                                { 3 * ticksPerBar, ticksPerBar, 43, 100, false } };
    std::string lineNames;

    for (auto& d : detectChords (line, 4 * ticksPerBar))
        lineNames += d.event.chord.name (false) + " ";

    CHECK (lineNames.find ("Am") == 0 && lineNames.find ('5') == std::string::npos && lineNames.find (" F ") != std::string::npos,
           "linha de baixo Lá-Mi, Fá, Dó, Sol lida como acordes do tom (%s)", lineNames.c_str());

    // O que o gerador faz, a análise reconhece
    int matched = 0, total = 0, keysOk = 0;

    for (uint32_t s = 1; s <= 24; ++s)
    {
        auto r = request (s % 2 == 0 ? "trap" : "pop", Part::chords, s, (int) (s % 12), s % 2 == 0 ? "minor" : "major");
        r.complexity = 0.0;
        const auto res = generate (r);
        const auto found = detectChords (res.notes, r.bars * ticksPerBar);

        for (auto& e : res.chords)
        {
            ++total;
            const auto f = std::find_if (found.begin(), found.end(), [&] (const DetectedChord& d)
            {
                return e.start >= d.event.start && e.start < d.event.end();
            });

            if (f != found.end() && f->event.chord.root == e.chord.root && f->event.chord.isMinor() == e.chord.isMinor())
                ++matched;
        }

        // Tonalidade: acordes + baixo da mesma ideia
        auto rb = r;
        rb.part = Part::bass;
        auto all = res.notes;
        const auto bass = generate (rb).notes;
        all.insert (all.end(), bass.begin(), bass.end());
        const auto k = detectKey (all);
        const bool same = k.root == r.scale.root && k.major == r.scale.isMajor();
        const bool relative = k.major != r.scale.isMajor()
                              && k.root == pitchClass (r.scale.root + (r.scale.isMajor() ? 9 : 3));
        keysOk += same || relative ? 1 : 0;
    }

    CHECK (matched >= total * 9 / 10, "acordes gerados são reconhecidos (%d de %d)", matched, total);
    CHECK (keysOk >= 22, "tonalidade (ou a relativa) reconhecida em %d de 24 ideias", keysOk);
}

//==============================================================================
static void testPrompt()
{
    std::printf ("Pedido em texto\n");

    CHECK (foldText ("Fá Menor Ação") == "fa menor acao", "foldText tira acento e põe em minúsculas");

    auto p = parsePrompt ("trap triste em Fá menor, 4 compassos");
    CHECK (p.genre == "trap" && p.mood == Mood::sad && p.root == 5 && p.scale == "minor" && p.bars == 4,
           "\"trap triste em Fá menor, 4 compassos\"");

    p = parsePrompt ("Faz uma melodia de drill sombria em F#m 140 bpm");
    CHECK (p.part == Part::melody && p.genre == "drill" && p.mood == Mood::dark && p.root == 6 && p.scale == "minor" && p.bpm == 140,
           "\"melodia de drill sombria em F#m 140 bpm\"");

    p = parsePrompt ("Em lá menor, um R&B calmo com acordes");
    CHECK (p.root == 9 && p.scale == "minor" && p.genre == "rnb" && p.mood == Mood::chill && p.part == Part::chords,
           "\"Em lá menor\": o Em é preposição, não Mi menor");

    p = parsePrompt ("batida de funk mandelão 150bpm");
    CHECK (p.part == Part::drums && p.genre == "funk" && p.bpm == 150, "\"batida de funk mandelão 150bpm\"");

    p = parsePrompt ("boom bap jazzy, bem minimalista");
    CHECK (p.genre == "lofi" && p.complexity && *p.complexity > 0.8 && p.density && *p.density < 0.3, "\"boom bap jazzy, bem minimalista\"");

    p = parsePrompt ("do zero, um trap pesado");
    CHECK (! p.root && p.genre == "trap" && p.mood == Mood::dark, "\"do zero\" não vira Dó");

    p = parsePrompt ("pop alegre em si bemol maior, 8 compassos");
    CHECK (p.root == 10 && p.scale == "major" && p.genre == "pop" && p.mood == Mood::happy && p.bars == 8, "\"si bemol maior\"");

    p = parsePrompt ("808 de phonk em Dó# frígio");
    CHECK (p.part == Part::eightOhEight && p.genre == "phonk" && p.root == 1 && p.scale == "phrygian", "\"808 de phonk em Dó# frígio\"");

    p = parsePrompt ("arpejo épico em Am");
    CHECK (p.part == Part::arp && p.mood == Mood::epic && p.root == 9 && ! p.understood.empty(), "\"arpejo épico em Am\" (%zu itens entendidos)", p.understood.size());
}

//==============================================================================
static void testReviewFixes()
{
    std::printf ("Revisão cruzada (Synth)\n");

    auto p = parsePrompt ("Em 140 bpm, um trap sombrio");
    auto q = parsePrompt ("Em 4 compassos, melodia de drill");
    auto k1 = parsePrompt ("um trap no tom Em");
    auto k2 = parsePrompt ("trap em Em, 4 compassos");
    CHECK (! p.root && ! q.root && p.bpm == 140 && q.bars == 4, "\"Em\" no começo da frase é preposição, não Mi menor");
    CHECK (k1.root == 4 && k1.scale == "minor" && k2.root == 4 && k2.scale == "minor", "\"tom Em\" e \"em Em\" continuam Mi menor");

    // Seguir um canal que só começa no compasso 2: os acordes e o arpejo tocam desde o compasso 1.
    std::vector<GenNote> source;

    for (int bar = 1; bar < 4; ++bar)
        for (int k : { 57, 60, 64 })
            source.push_back ({ bar * ticksPerBar, ticksPerBar, k, 100, false });

    Request r;
    r.genre = "pop";
    r.scale = Scale::fromName (9, "minor");

    for (auto& d : detectChords (source, 4 * ticksPerBar))
        r.chords.push_back (d.event);

    bool coversStart = true, contiguous = true;

    for (auto part : { Part::chords, Part::arp, Part::melody })
    {
        r.part = part;
        const auto res = generate (r);
        coversStart = coversStart && res.ok && ! res.notes.empty() && res.notes.front().start < ticksPerBar;
        int t = 0;

        for (auto& e : res.chords)
        {
            contiguous = contiguous && e.start == t;
            t = e.end();
        }

        contiguous = contiguous && t == 4 * ticksPerBar;
    }

    CHECK (coversStart && contiguous, "seguir um canal que começa no compasso 2: acordes cobrem o padrão e tocam no compasso 1");

    // Acorde dado com fundamental fora de 0..11 e com só a fundamental: não trava nem lê fora.
    Request odd;
    odd.genre = "drill";
    odd.complexity = 1.0;
    odd.scale = Scale::fromName (9, "minor");
    odd.chords.push_back ({ 0, 4 * ticksPerBar, Chord { 21, { 0 } } });
    bool oddOk = true;

    for (auto part : { Part::chords, Part::eightOhEight, Part::bass, Part::melody, Part::arp })
        for (uint32_t s = 1; s <= 12; ++s)
        {
            odd.part = part;
            odd.seed = s;
            const auto res = generate (odd);
            oddOk = oddOk && res.ok;
        }

    CHECK (oddOk, "acorde dado estranho (fundamental 21, uma nota só) não trava nem lê fora do vetor");

    // Vozes demais: a nona sai antes da sétima.
    const Chord m9 { 9, { 0, 2, 3, 7, 10 } };
    const auto v3 = voiceChord (m9, {}, 50, 80, 3, false);
    bool hasSeventh = false, hasNinth = false;

    for (int k : v3)
    {
        hasSeventh = hasSeventh || pitchClass (k - 9) == 10;
        hasNinth = hasNinth || pitchClass (k - 9) == 2;
    }

    CHECK (v3.size() == 3 && hasSeventh && ! hasNinth, "Am9 em 3 vozes guarda a sétima e tira a quinta e a nona");
}

int main()
{
    testTheory();
    testGenreData();
    testGenerator();
    test808AndDrums();
    testAnalysis();
    testPrompt();
    testReviewFixes();

    std::printf ("\n%s: %d falha(s)\n", failures == 0 ? "OK" : "FALHOU", failures);
    return failures == 0 ? 0 : 1;
}
