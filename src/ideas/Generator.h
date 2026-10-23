#pragma once

// O gerador do WHYNOT Ideias: acordes, melodia, arpejo, baixo, 808 e bateria por gênero.
// Determinístico: a mesma semente e os mesmos ajustes dão sempre as mesmas notas.

#include "Genres.h"
#include "Theory.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace whynot::ideas
{

/** Um acorde no tempo (ticks desde o começo do padrão). */
struct ChordEvent
{
    int start = 0;
    int length = ticksPerBar;
    Chord chord;

    int end() const noexcept   { return start + length; }
};

struct Request
{
    std::string genre { "trap" };
    Part part = Part::chords;
    std::optional<Mood> mood;            // vazio = o clima padrão do gênero
    int bars = 4;                        // 1 a 16
    Scale scale;                         // tonalidade (a do projeto, normalmente)
    double complexity = 0.5;             // 0..1: extensões, notas de passagem, variações
    double density = 0.5;                // 0..1: quantas notas
    uint32_t seed = 1;                   // a ideia: progressão e batida (todas as partes juntas)
    uint32_t variation = 0;              // a variação desta parte (botão "Variar")
    std::string progression;             // graus romanos; vazio = escolher pelo gênero e clima
    std::vector<ChordEvent> chords;      // acordes dados (de outro canal); vazio = progressão
    int octave = 0;                      // -2..2
};

struct Result
{
    bool ok = true;
    std::string error;
    std::vector<GenNote> notes;          // ordenadas por início
    std::vector<ChordEvent> chords;      // a harmonia usada
    std::string progression;             // o texto da progressão (ou os nomes, se vieram de fora)
    int beat = 0;                        // a batida da bateria que o 808 segue
};

struct DrumStep
{
    bool on = false;
    int velocity = 100;
    int roll = 1;
};

struct DrumResult
{
    bool ok = true;
    std::string error;
    int lengthSteps = 0;
    std::vector<std::pair<std::string, std::vector<DrumStep>>> lanes;   // kick, snare, hat, openhat, perc
    int beat = 0;
};

/** Gera uma parte com notas (qualquer uma menos a bateria). */
Result generate (const Request&);

/** Gera a bateria nos passos do channel rack (16 por compasso). */
DrumResult generateDrums (const Request&);

/** A harmonia no tempo: os acordes dados, ou a progressão (escolhida ou pedida) com as
    extensões que a complexidade pede. `text` recebe a progressão usada. */
std::vector<ChordEvent> chordTimeline (const Request&, std::string& text, std::string& error);

/** Qual das batidas do gênero esta semente e esta densidade escolhem. */
int chooseBeat (const Request&);

/** "Am7 F C G": os nomes, na grafia da tonalidade. */
std::string chordNames (const std::vector<ChordEvent>&, const Scale&);

} // namespace whynot::ideas
