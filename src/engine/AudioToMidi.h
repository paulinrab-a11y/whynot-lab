#pragma once

// Áudio para MIDI (bloco 4e, 23/10): o beatbox do MC vira o kit, e uma melodia cantada ou tocada vira notas.
// - Bateria: os ataques (audio::detectOnsets), cada um com o papel pelo timbre dos primeiros 60 ms: o grave manda no
//   kick, o ruído agudo (que cruza o zero quase a cada amostra) no chimbal, o resto é a caixa.
// - Melodia: a altura a cada 10 ms pelo YIN (dsp::detectFramePitch), uma voz só; a mesma nota por 50 ms ou mais vira
//   uma nota, e um buraco curto na mesma nota não corta.
// Puro (sem o Tracktion), na thread de mensagem: aloca e não serve para a thread de áudio.

#include <JuceHeader.h>

#include <vector>

namespace whynot::audiomidi
{

enum class DrumRole { kick, snare, hat };

struct DrumHit
{
    double seconds = 0.0;     // desde o começo do trecho
    DrumRole role = DrumRole::snare;
    int velocity = 100;       // 1..127, pelo pico do ataque
    float lowShare = 0.0f, highShare = 0.0f;   // o que decidiu o papel (para o autoteste e a IA)
    float crossings = 0.0f;                    // cruzamentos do zero por amostra: o ruído do chimbal passa de 0,3
};

struct MelodyNote
{
    double start = 0.0, end = 0.0;   // segundos desde o começo do trecho
    int key = 60;
    int velocity = 100;
};

/** O som em mono (a média dos canais), de fromSeconds até toSeconds (no máximo 120 s). Falso se não abriu. */
bool readMono (const juce::File&, double fromSeconds, double toSeconds, std::vector<float>& out, double& rate);

/** Os ataques e o papel de cada um. */
std::vector<DrumHit> drumHits (const std::vector<float>& mono, double rate);

/** As notas de uma voz só (cantada, assobiada ou um instrumento), de Mi 1 a Ré 6. */
std::vector<MelodyNote> melodyNotes (const std::vector<float>& mono, double rate);

const char* roleName (DrumRole) noexcept;   // "kick", "snare", "hat"

} // namespace whynot::audiomidi
