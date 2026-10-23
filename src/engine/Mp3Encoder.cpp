#include "Mp3Encoder.h"

#if WHYNOT_HAS_LAME
 #include <lame.h>
#endif

namespace whynot
{

bool mp3EncoderAvailable() noexcept
{
   #if WHYNOT_HAS_LAME
    return true;
   #else
    return false;
   #endif
}

#if WHYNOT_HAS_LAME
namespace
{
    // Tag ID3v2 em UTF-16 com BOM: o LAME só aceita acentos por esse caminho.
    void setTag (lame_global_flags* lame, const char* frame, const juce::String& text)
    {
        if (text.isEmpty())
            return;

        juce::MemoryBlock utf16;
        const juce::uint16 bom = 0xfeff;
        utf16.append (&bom, sizeof (bom));
        const auto wide = text.toUTF16();
        utf16.append (wide.getAddress(), wide.sizeInBytes());   // inclui o zero final
        id3tag_set_textinfo_utf16 (lame, frame, static_cast<const unsigned short*> (utf16.getData()));
    }

    constexpr int maxTagChars = 256;

    struct LameHandle
    {
        LameHandle() : flags (lame_init()) {}
        ~LameHandle() { if (flags != nullptr) lame_close (flags); }
        lame_global_flags* flags;
    };
}
#endif

juce::Result encodeMp3 (const juce::File& wavIn, const juce::File& mp3Out, const Mp3Settings& settings,
                        const std::function<bool()>& shouldStop)
{
   #if ! WHYNOT_HAS_LAME
    juce::ignoreUnused (wavIn, mp3Out, settings, shouldStop);
    return juce::Result::fail ("Este build da WHYNOT LAB não tem o codificador de MP3 (LAME). Veja docs/como-rodar.md."_t);
   #else
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (wavIn.createInputStream().release(), true));

    if (reader == nullptr)
        return juce::Result::fail ("Não consegui ler o áudio para o MP3: "_t + wavIn.getFullPathName());

    const int rate = (int) reader->sampleRate;
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);

    LameHandle lame;

    if (lame.flags == nullptr)
        return juce::Result::fail ("O LAME não iniciou."_t);

    lame_set_in_samplerate (lame.flags, rate);
    lame_set_out_samplerate (lame.flags, rate);
    lame_set_num_channels (lame.flags, 2);
    lame_set_mode (lame.flags, JOINT_STEREO);
    lame_set_quality (lame.flags, 2);   // 0 = mais lento; 2 = o "alta qualidade" do LAME

    if (settings.vbr)
    {
        lame_set_VBR (lame.flags, vbr_default);
        lame_set_VBR_quality (lame.flags, (float) juce::jlimit (0, 9, settings.vbrQuality));
    }
    else
    {
        lame_set_VBR (lame.flags, vbr_off);
        lame_set_brate (lame.flags, juce::jlimit (32, 320, settings.bitrateKbps));
    }

    // As tags vão à mão, para o quadro de informação do LAME poder ser regravado depois delas.
    id3tag_init (lame.flags);
    id3tag_v2_only (lame.flags);
    // título e artista com teto: um nome de projeto gigante não vira uma tag de megabytes
    setTag (lame.flags, "TIT2", settings.title.substring (0, maxTagChars));
    setTag (lame.flags, "TPE1", settings.artist.substring (0, maxTagChars));
    setTag (lame.flags, "TSSE", "WHYNOT LAB");
    lame_set_write_id3tag_automatic (lame.flags, 0);

    if (lame_init_params (lame.flags) < 0)
        return juce::Result::fail ("O LAME recusou as configurações (taxa "_t + juce::String (rate) + " Hz).");

    mp3Out.deleteFile();
    juce::FileOutputStream out (mp3Out);

    // O JUCE abre o arquivo para escrever no fim: se o antigo não saiu (aberto num player que deixa
    // escrever), o MP3 novo iria depois dele, e o player tocaria o antigo. Esvazia antes.
    if (! out.openedOk() || ! out.setPosition (0) || out.truncate().failed())
        return juce::Result::fail ("Não consegui criar o arquivo "_t + mp3Out.getFullPathName()
                                   + ". Ele pode estar aberto em outro programa, ou a pasta está sem permissão."_t);

    // o LAME devolve o tamanho de que precisa e não escreve nada se não couber: aumenta e pede de novo
    std::vector<unsigned char> tag (64 * 1024);
    auto tagSize = lame_get_id3v2_tag (lame.flags, tag.data(), tag.size());

    if (tagSize > tag.size())
    {
        tag.resize (tagSize);
        tagSize = lame_get_id3v2_tag (lame.flags, tag.data(), tag.size());
    }

    tagSize = juce::jmin (tagSize, tag.size());
    out.write (tag.data(), tagSize);

    constexpr int block = 8192;
    juce::AudioBuffer<float> buffer (2, block);
    std::vector<unsigned char> mp3 ((size_t) (1.25 * block + 7200));
    const auto total = reader->lengthInSamples;

    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        if (shouldStop && shouldStop())
        {
            out.setPosition (0);
            out.truncate();
            return juce::Result::fail ("Exportação cancelada."_t);   // quem chamou apaga o arquivo vazio
        }

        const int n = (int) juce::jmin ((juce::int64) block, total - pos);
        reader->read (&buffer, 0, n, pos, true, channels > 1);

        if (channels == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, n);

        const int bytes = lame_encode_buffer_ieee_float (lame.flags, buffer.getReadPointer (0), buffer.getReadPointer (1),
                                                         n, mp3.data(), (int) mp3.size());
        if (bytes < 0)
            return juce::Result::fail ("O LAME falhou ao codificar (erro "_t + juce::String (bytes) + ").");

        out.write (mp3.data(), (size_t) bytes);
    }

    const int last = lame_encode_flush (lame.flags, mp3.data(), (int) mp3.size());

    if (last > 0)
        out.write (mp3.data(), (size_t) last);

    // Quadro de informação (duração, atraso do codificador): troca o quadro vazio logo depois das tags.
    const auto infoSize = lame_get_lametag_frame (lame.flags, mp3.data(), mp3.size());

    if (infoSize > 0 && infoSize <= mp3.size())
    {
        out.flush();

        if (out.setPosition ((juce::int64) tagSize))
            out.write (mp3.data(), infoSize);
    }

    out.flush();
    return out.getStatus().wasOk() ? juce::Result::ok()
                                   : juce::Result::fail ("Erro ao gravar o MP3: "_t + out.getStatus().getErrorMessage());
   #endif
}

} // namespace whynot
