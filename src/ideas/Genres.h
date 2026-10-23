#pragma once

// Os gêneros do WHYNOT Ideias: tudo o que muda de um estilo para outro é dado, não código.
// Progressões e ritmos são convenções gerais de cada gênero, escritos por nós.

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace whynot::ideas
{

enum class Mood { dark, sad, chill, happy, epic };

enum class Part { chords, melody, bass, eightOhEight, arp, drums };

/** id em inglês (comandos, .wnl) e nome em português (tela). */
std::string_view moodId (Mood);
std::string_view moodName (Mood);
bool parseMood (std::string_view id, Mood&);
std::span<const Mood> allMoods();

std::string_view partId (Part);
std::string_view partName (Part);
bool parsePart (std::string_view id, Part&);
std::span<const Part> allParts();

/** Um ritmo de 1 ou 2 compassos numa grade de semicolcheias (16 caracteres por compasso).
    Acordes e baixo: x = ataque, - = segura a nota anterior, . = pausa.
    No baixo: r = fundamental, f = quinta, o = oitava acima, a = aproximação do próximo acorde.
    Bateria: x = batida, X = acento, g = fantasma, 2/3/4/6/8 = roll dentro do passo. */
struct Groove
{
    const char* grid;
    float density;   // 0 = esparso ... 1 = cheio
};

/** Uma batida de 2 compassos (32 passos por raia; "" = raia vazia). */
struct Beat
{
    const char* kick;
    const char* snare;
    const char* hat;
    const char* openhat;
    const char* perc;
    float density;
};

/** Células rítmicas de um tempo (semínima) usadas para montar melodias. */
enum class Cell { quarter, eighths, sixteenths, eighthTwoSixteenths, twoSixteenthsEighth,
                  dotted, snap, offEighth, offSixteenths, triplet, rest, hold };

constexpr int numCells = 12;

struct CellWeights
{
    float w[numCells];
};

struct GenreProfile
{
    const char* id;
    const char* name;
    const char* description;
    int bpmLow, bpmHigh, bpmDefault;
    Mood defaultMood;
    bool preferMinor;

    // Harmonia
    float seventhBase, seventhPerComplexity, ninthPerComplexity;
    bool rootBelow, spreadVoicing;
    int chordLow, chordHigh, chordMaxNotes;
    std::vector<Groove> chordRhythms;

    // Melodia
    int melodyLow, melodyHigh;
    int motifBars;              // 1 = loop curto (trap, drill, phonk, funk); 2 = frase (pop, R&B)
    const char* form;           // por motivo: A, a (A variado), B, b (B variado)
    float chordToneBias, repeatWeight, leapWeight, legato;
    CellWeights cellsSparse, cellsBusy;

    // Baixo e 808
    int bassLow, bassHigh;
    std::vector<Groove> bassGrooves;
    float slideChance, octaveChance, eightHold;   // eightHold: 1 = o 808 segura até a próxima nota
    bool melodic808;            // o 808 ganha notas de passagem (drill)

    // Arpejo
    int arpTicks;               // 240 = semicolcheias, 320 = tercinas de colcheia, 480 = colcheias

    // Bateria
    std::vector<Beat> beats;
};

/** Categorias de som do WHYNOT Synth para uma parte, na ordem de preferência do gênero
    (ids do synth: 808, bass, lead, pad, pluck, keys, bell, brass, strings, vox, arp, fx, texture).
    Bateria: vazio (vai para o kit). */
std::vector<std::string_view> soundCategories (Part, std::string_view genre);

std::span<const GenreProfile> allGenres();
const GenreProfile* findGenre (std::string_view id);

/** Uma progressão do banco, em graus romanos do modo (menor ou maior). */
struct ProgressionEntry
{
    const char* text;
    bool minor;
    const char* genres;   // ids separados por espaço
    const char* moods;    // ids separados por espaço
};

std::span<const ProgressionEntry> allProgressions();

/** As progressões que combinam com o gênero no modo pedido, melhores para o clima primeiro. */
std::vector<const ProgressionEntry*> progressionsFor (std::string_view genre, bool minor, Mood);

} // namespace whynot::ideas
