#pragma once

#include <cstring>

namespace whynot::audio
{

/** Os timbres do WHYNOT Piano: o mesmo piano gravado, com outra resposta à força, outro brilho e
    outra largura. Sem JUCE: o formato .wnl valida o id e o plugin lê os valores. */
struct PianoPreset
{
    const char* id;
    const char* name;      // UTF-8, para a tela
    float velocityCurve;   // força^curva: acima de 1 puxa para as camadas mais suaves
    float toneHz;          // passa-baixas de 12 dB/oitava; 0 = aberto
    float width;           // 0 = mono, 1 = o estéreo gravado
    float releaseScale;    // multiplica o tempo dos abafadores
    float drive;           // 0 a 1: saturação leve
    bool hammer;           // o ruído do martelo voltando ao soltar a tecla
};

inline constexpr PianoPreset pianoPresets[] = {
    { "grand", "Cauda", 1.0f,  0.0f,    1.0f,  1.0f, 0.0f, true },
    { "suave", "Suave", 1.35f, 5200.0f, 0.85f, 1.4f, 0.0f, true },
    { "lofi",  "Lo-fi", 1.1f,  2600.0f, 0.35f, 0.8f, 0.4f, false },
};

inline const PianoPreset* findPianoPreset (const char* id) noexcept
{
    if (id != nullptr)
        for (auto& p : pianoPresets)
            if (std::strcmp (p.id, id) == 0)
                return &p;

    return nullptr;
}

} // namespace whynot::audio
