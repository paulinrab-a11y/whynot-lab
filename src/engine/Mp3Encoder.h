#pragma once

#include <JuceHeader.h>

#include "../common/Text.h"

namespace whynot
{

/** Como codificar o MP3. Padrão: 320 kbps constante, o que as plataformas e os DJs esperam. */
struct Mp3Settings
{
    int bitrateKbps = 320;      // CBR: 128, 192, 256 ou 320
    bool vbr = false;           // true = VBR com a qualidade abaixo (0 = melhor)
    int vbrQuality = 0;
    juce::String title, artist;
};

/** true quando o app foi compilado com o LAME (external/lame-3.100). */
bool mp3EncoderAvailable() noexcept;

/** Codifica um WAV em MP3 com o LAME, na mesma taxa do WAV (use 44,1 ou 48 kHz).
    Grava as tags ID3 (título e artista) e o quadro de informação do LAME no começo do arquivo.
    Thread de mensagem ou de trabalho, nunca a de áudio.
    shouldStop (opcional) é consultado a cada bloco: true para, esvazia o MP3 e devolve "Exportação
    cancelada." (o Parar da exportação em segundo plano, e o app fechando); quem chamou apaga o arquivo.
*/
juce::Result encodeMp3 (const juce::File& wavIn, const juce::File& mp3Out, const Mp3Settings&,
                        const std::function<bool()>& shouldStop = {});

} // namespace whynot
