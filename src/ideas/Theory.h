#pragma once

// Teoria musical do WHYNOT Ideias, sem JUCE: escalas, acordes, graus romanos e condução de vozes.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace whynot::ideas
{

/** Mesma grade do projeto (Project.h): 960 ticks por semínima, passo = semicolcheia. */
constexpr int ticksPerBeat = 960;
constexpr int ticksPerStep = ticksPerBeat / 4;
constexpr int ticksPerBar = ticksPerBeat * 4;

/** Uma nota gerada, com os mesmos campos do piano roll. slide = desliza da nota anterior (808). */
struct GenNote
{
    int start = 0;
    int length = ticksPerStep;
    int key = 60;
    int velocity = 100;
    bool slide = false;

    int end() const noexcept   { return start + length; }
    bool operator== (const GenNote&) const = default;
};

/** Classe de altura (0 = Dó ... 11 = Si) de uma nota MIDI. */
constexpr int pitchClass (int key) noexcept   { return ((key % 12) + 12) % 12; }

/** "C", "C#" ou "Db"... */
std::string pitchName (int pitchClass, bool flats);

/** Nome em português: "Dó", "Dó#", "Réb"... */
std::string pitchNamePt (int pitchClass, bool flats);

/** Escala: tônica (classe de altura) e graus em semitons. Os nomes são os do projeto. */
struct Scale
{
    int root = 0;
    std::vector<int> degrees { 0, 2, 3, 5, 7, 8, 10 };
    std::string name { "minor" };

    /** minor, major, harmonicMinor, phrygian, dorian, pentatonicMinor ou chromatic (desconhecido = minor). */
    static Scale fromName (int root, std::string_view name);

    /** Terça maior sobre a tônica. */
    bool isMajor() const;
    bool contains (int key) const;

    /** As 7 notas que dão nome aos graus dos acordes: maior natural ou menor natural. */
    Scale harmonic() const;

    /** Bemóis na armadura (para escrever Bb em vez de A#). */
    bool prefersFlats() const;

    /** Índice do grau da nota (0..n-1), ou -1 fora da escala. */
    int degreeOf (int key) const;

    /** A nota da escala mais próxima (empate: a de baixo). */
    int snap (int key) const;

    /** Anda `steps` graus a partir de key (que é ajustada à escala antes). */
    int stepFrom (int key, int steps) const;

    bool operator== (const Scale&) const = default;
};

/** Um acorde: fundamental, intervalos (classes a partir da fundamental, com 0, ordenados)
    e baixo opcional (inversão). */
struct Chord
{
    int root = 0;
    std::vector<int> intervals { 0, 3, 7 };
    int bass = -1;

    bool has (int interval) const;
    bool isMinor() const         { return has (3) && ! has (4); }
    bool isDiminished() const    { return has (3) && has (6) && ! has (7); }
    bool contains (int key) const;

    std::vector<int> pitchClasses() const;

    /** "m7", "maj7", "sus4"... ("" = maior). */
    std::string suffix() const;

    /** "Am7", "Bbmaj7", "C/E". */
    std::string name (bool flats) const;

    bool operator== (const Chord&) const = default;
};

/** Um acorde da progressão: o acorde e quantas unidades de tempo ele dura ("i*2" = 2). */
struct ProgressionChord
{
    Chord chord;
    int weight = 1;
    bool explicitExtension = false;   // "i7", "IVmaj7": as extensões automáticas não mexem nele
};

/** Lê um grau romano no modo da escala: maiúsculo = maior, minúsculo = menor;
    b/# antes alteram a fundamental; depois: o, o7, h7 (meio-diminuto), +, 7, maj7, 9, maj9,
    add9, sus2, sus4, 7sus4, 6. Ex.: "i", "VI", "bII", "iih7", "V7", "IVmaj7". */
std::optional<ProgressionChord> parseRoman (std::string_view token, const Scale& key);

/** "i VI III VII" ou "i-iv-V", com "*n" para durar n unidades. Vazio = texto inválido. */
std::vector<ProgressionChord> parseProgression (std::string_view text, const Scale& key);

/** Notas do acorde entre low e high, conduzindo as vozes a partir de `previous` (vazio = começo).
    maxNotes limita as vozes (a quinta sai primeiro); spread abre a voz do meio uma oitava abaixo. */
std::vector<int> voiceChord (const Chord&, const std::vector<int>& previous, int low, int high,
                             int maxNotes, bool spread);

} // namespace whynot::ideas
