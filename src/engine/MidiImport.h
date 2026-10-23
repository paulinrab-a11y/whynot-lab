#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

namespace whynot
{

/** O que tem num .mid, para escolher o que importar: uma linha por trilha com notas (as trilhas só com
    nome, andamento ou compasso ficam de fora). No tipo 0, a trilha única com vários canais MIDI vira uma
    linha por canal, como os programas fazem ao abrir esse tipo.
*/
struct MidiFileInfo
{
    struct Track
    {
        juce::String name;           // o nome da trilha no arquivo, ou "Trilha N"
        int fileTrack = 0;           // a trilha no arquivo (0 = a primeira)
        int midiChannel = 0;         // 1 a 16: só esse canal da trilha; 0 = a trilha inteira
        int notes = 0;
        int lowKey = 127, highKey = 0;
    };

    int format = 1;                  // 0, 1 ou 2
    int ticksPerBeat = 0;            // o do arquivo; 0 = tempo em SMPTE (por segundos)
    double bpm = 0.0;                // o primeiro andamento do arquivo; 0 = o arquivo não diz
    int timeSigNum = 4, timeSigDen = 4;
    std::vector<Track> tracks;

    int totalNotes() const noexcept;
};

/** Como as notas entram no padrão. */
struct MidiImportOptions
{
    int track = 0;                   // 0 = todas as trilhas; N = só a N-ª de MidiFileInfo::tracks
    int at = 0;                      // ticks do padrão onde entra o primeiro compasso com notas do arquivo
    int limitTicks = 256 * ticksPerStep;   // o padrão vai até 16 compassos
    int maxNotes = 4000;
    double projectBpm = 140.0;       // só para arquivo em SMPTE: os segundos viram ticks neste andamento
};

struct MidiImportResult
{
    MidiFileInfo info;
    std::vector<Note> notes;         // em 960 por tempo, a partir de `at`, ordenadas; sem id
    int dropped = 0;                 // notas que passaram do fim do padrão ou do teto
};

/** Lê só o resumo do arquivo (trilhas, notas, andamento). */
juce::Result readMidiInfo (const juce::File&, MidiFileInfo&);

/** Lê as notas para o piano roll: tom, força e duração em 960 por tempo (do PPQ do arquivo; SMPTE por
    segundos no andamento do projeto). Uma nota com força 0 é o fim da nota anterior, como manda o MIDI.
    Os compassos vazios do começo do arquivo saem (contados em compassos inteiros do compasso do arquivo,
    pela primeira nota do arquivo inteiro, para trilhas importadas uma a uma continuarem alinhadas) e o
    primeiro compasso com notas entra em `at`. Duas notas iguais (tecla e início) viram uma, a mais longa.
*/
juce::Result readMidiNotes (const juce::File&, const MidiImportOptions&, MidiImportResult&);

} // namespace whynot
