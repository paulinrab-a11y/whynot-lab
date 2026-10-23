#include "SampleAnalysis.h"
#include "../common/Text.h"
#include "../dsp/FxCatalog.h"

#include <cctype>
#include <cstring>
#include <map>

namespace whynot
{

namespace
{
    /** Os primeiros segundos do arquivo, até 2 canais. */
    struct Opening
    {
        std::vector<std::vector<float>> channels;
        double sampleRate = 0.0;
        double seconds = 0.0;        // do arquivo inteiro
        juce::int64 totalFrames = 0; // do arquivo inteiro
    };

    Opening readOpening (const juce::File& file, double maxSeconds = 6.0)
    {
        Opening o;

        if (! file.existsAsFile())
            return o;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return o;

        const int numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        const int length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));

        juce::AudioBuffer<float> buffer (numChannels, length);
        reader->read (&buffer, 0, length, 0, true, numChannels > 1);

        o.channels.resize ((size_t) numChannels);

        for (int ch = 0; ch < numChannels; ++ch)
            o.channels[(size_t) ch].assign (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + length);

        // O mesmo corte do começo que o SampleCache faz (o atraso do MP3): as frações do laço e
        // das fatias valem para o sample que toca.
        const int trim = leadingFramesToTrim (file, o.channels, reader->sampleRate);
        trimLeadingFrames (o.channels, trim);

        o.totalFrames = reader->lengthInSamples - trim;
        o.sampleRate = reader->sampleRate;
        o.seconds = (double) o.totalFrames / reader->sampleRate;
        return o;
    }

    double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        to = std::min (to, x.size());

        if (from >= to)
            return 0.0;

        double sum = 0.0;

        for (auto i = from; i < to; ++i)
            sum += (double) x[i] * (double) x[i];

        return std::sqrt (sum / (double) (to - from));
    }

    std::vector<float> mixToMono (const Opening& o)
    {
        const auto length = o.channels.front().size();
        std::vector<float> mono (length, 0.0f);
        const float scale = 1.0f / (float) o.channels.size();

        for (auto& ch : o.channels)
            for (std::size_t i = 0; i < length; ++i)
                mono[i] += ch[i] * scale;

        return mono;
    }
}

//==============================================================================
namespace
{
    /** O atraso do MP3 e a taxa dele, lidos do cabeçalho (frames -1 sem a etiqueta do LAME). */
    struct Mp3Priming
    {
        int frames = -1;
        double sampleRate = 0.0;
    };

    Mp3Priming readMp3Priming (const juce::File& file);

    /** O Projector pergunta a cada mudança do projeto: o cabeçalho só é lido de novo se o arquivo mudou. */
    Mp3Priming cachedMp3Priming (const juce::File& file)
    {
        struct Entry { juce::Time modified; juce::int64 size = 0; Mp3Priming value; };
        static juce::CriticalSection lock;
        static std::map<juce::String, Entry> cache;

        const auto modified = file.getLastModificationTime();
        const auto size = file.getSize();
        const juce::ScopedLock sl (lock);

        if (auto it = cache.find (file.getFullPathName()); it != cache.end() && it->second.modified == modified && it->second.size == size)
            return it->second.value;

        const auto value = readMp3Priming (file);
        cache[file.getFullPathName()] = { modified, size, value };
        return value;
    }
}

int mp3PrimingFrames (const juce::File& file)
{
    return file.hasFileExtension ("mp3") ? cachedMp3Priming (file).frames : -1;
}

double mp3EncoderDelaySeconds (const juce::File& file)
{
    if (! file.hasFileExtension ("mp3"))
        return 0.0;

    const auto p = cachedMp3Priming (file);
    return p.frames > 0 && p.sampleRate > 0.0 ? (double) p.frames / p.sampleRate : 0.0;
}

namespace
{
Mp3Priming readMp3Priming (const juce::File& file)
{
    Mp3Priming result;

    juce::FileInputStream in (file);
    juce::MemoryBlock head;

    if (! in.openedOk() || in.readIntoMemoryBlock (head, 256 * 1024) < 64)
        return result;

    const auto* d = static_cast<const juce::uint8*> (head.getData());
    const size_t n = head.getSize();
    size_t pos = 0;

    // Pula a etiqueta ID3v2 (tamanho "syncsafe", 7 bits por byte, mais o rodapé opcional).
    if (n >= 10 && d[0] == 'I' && d[1] == 'D' && d[2] == '3')
        pos = 10 + (((size_t) (d[6] & 0x7f) << 21) | ((size_t) (d[7] & 0x7f) << 14) | ((size_t) (d[8] & 0x7f) << 7) | (size_t) (d[9] & 0x7f))
                 + ((d[5] & 0x10) != 0 ? 10 : 0);

    // O primeiro quadro: 11 bits de sincronismo.
    while (pos + 4 <= n && ! (d[pos] == 0xff && (d[pos + 1] & 0xe0) == 0xe0))
        ++pos;

    if (pos + 4 > n)
        return result;

    // O quadro de informação ("Xing" ou "Info") vem depois do cabeçalho, do CRC e da side info.
    const int version = (d[pos + 1] >> 3) & 3;   // 3 = MPEG 1, 2 = MPEG 2, 0 = MPEG 2.5
    const bool mpeg1 = version == 3;
    const int rateIndex = (d[pos + 2] >> 2) & 3;

    if (rateIndex < 3 && version != 1)
        result.sampleRate = (rateIndex == 0 ? 44100.0 : rateIndex == 1 ? 48000.0 : 32000.0) / (mpeg1 ? 1.0 : version == 2 ? 2.0 : 4.0);

    const bool mono = ((d[pos + 3] >> 6) & 3) == 3;
    const size_t crc = (d[pos + 1] & 1) == 0 ? 2 : 0;
    const size_t xing = pos + 4 + crc + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));

    if (xing + 8 > n || ! (std::memcmp (d + xing, "Xing", 4) == 0 || std::memcmp (d + xing, "Info", 4) == 0))
        return result;

    const auto flags = juce::ByteOrder::bigEndianInt (d + xing + 4);
    const size_t lame = xing + 8 + ((flags & 1) != 0 ? 4 : 0) + ((flags & 2) != 0 ? 4 : 0) + ((flags & 4) != 0 ? 100 : 0)
                        + ((flags & 8) != 0 ? 4 : 0);

    // A extensão do LAME (o FFmpeg com o libmp3lame grava a mesma): o atraso do codificador em 12 bits.
    if (lame + 24 > n || ! (std::memcmp (d + lame, "LAME", 4) == 0 || std::memcmp (d + lame, "Lavf", 4) == 0
                            || std::memcmp (d + lame, "Lavc", 4) == 0))
        return result;

    const int encoderDelay = (d[lame + 21] << 4) | (d[lame + 22] >> 4);
    result.frames = encoderDelay + (mpeg1 ? 1152 : 576);   // + o quadro da etiqueta, que o JUCE decodifica como silêncio
    return result;
}
}

int leadingFramesToTrim (const juce::File& file, const std::vector<std::vector<float>>& channels, double sampleRate)
{
    if (! file.hasFileExtension ("mp3") || channels.empty())
        return 0;

    const int n = (int) channels.front().size();
    int skip = mp3PrimingFrames (file);

    if (skip < 0)
    {
        // Sem a etiqueta (outro codificador): o silêncio do começo, abaixo de -80 dBFS, até 50 ms.
        const int limit = juce::jmin (n, (int) (0.05 * sampleRate));
        skip = 0;

        auto silent = [&channels] (int i)
        {
            for (auto& ch : channels)
                if (std::abs (ch[(size_t) i]) >= 1.0e-4f)
                    return false;

            return true;
        };

        while (skip < limit && silent (skip))
            ++skip;
    }

    return juce::jlimit (0, juce::jmax (0, n - 2), skip);
}

void trimLeadingFrames (std::vector<std::vector<float>>& channels, int frames)
{
    if (frames <= 0)
        return;

    for (auto& ch : channels)
        ch.erase (ch.begin(), ch.begin() + juce::jmin ((std::ptrdiff_t) frames, (std::ptrdiff_t) ch.size()));
}

dsp::PitchEstimate detectRootOf (const juce::File& file)
{
    const auto o = readOpening (file);
    return o.channels.empty() ? dsp::PitchEstimate {} : dsp::detectSamplePitch (o.channels, o.sampleRate);
}

//==============================================================================
const char* roleId (SampleRole r) noexcept
{
    switch (r)
    {
        case SampleRole::kick:    return "kick";
        case SampleRole::bass:    return "808";
        case SampleRole::snare:   return "snare";
        case SampleRole::clap:    return "clap";
        case SampleRole::hat:     return "hat";
        case SampleRole::openHat: return "openHat";
        case SampleRole::perc:    return "perc";
        case SampleRole::cymbal:  return "cymbal";
        case SampleRole::fx:      return "fx";
        case SampleRole::vocal:   return "vocal";
        case SampleRole::loop:    return "loop";
        case SampleRole::unknown: break;
    }

    return "";
}

SampleRole roleFromName (const juce::String& text)
{
    // "Dark_808-G#.wav" -> "dark 808 g#": separadores viram espaço para achar as palavras.
    auto name = text.toLowerCase().replaceCharacters ("_-.()[]", "       ").toStdString();

    auto hasAny = [&name] (std::initializer_list<std::string_view> words)
    {
        for (auto w : words)
            if (name.find (w) != std::string::npos)
                return true;

        return false;
    };

    // Palavra inteira, para siglas curtas ("bd", "oh", "sub") não acharem pedaços de outras.
    auto hasWord = [&name] (std::string_view w)
    {
        for (auto pos = name.find (w); pos != std::string::npos; pos = name.find (w, pos + 1))
        {
            const bool startOk = pos == 0 || ! std::isalnum ((unsigned char) name[pos - 1]);
            const bool endOk = pos + w.size() == name.size() || ! std::isalpha ((unsigned char) name[pos + w.size()]);

            if (startOk && endOk)
                return true;
        }

        return false;
    };

    if (hasAny ({ "loop", "bpm" }))
        return SampleRole::loop;

    if (hasAny ({ "kick", "kik", "bumbo", "bassdrum", "bass drum" }) || hasWord ("bd"))
        return SampleRole::kick;

    if (hasAny ({ "openhat", "open hat", "open hh", "ohat", "ohh" }) || hasWord ("oh") || (hasWord ("open") && hasAny ({ "hat", "hh" })))
        return SampleRole::openHat;

    if (hasAny ({ "hat", "chimbal", "chimbau", "hihat" }) || hasWord ("hh") || hasWord ("chh") || hasWord ("ch"))
        return SampleRole::hat;

    if (hasAny ({ "clap", "palma", "snap" }))
        return SampleRole::clap;

    if (hasAny ({ "snare", "caixa", "snr", "rimshot" }) || hasWord ("sd") || hasWord ("rim") || hasWord ("rims"))
        return SampleRole::snare;

    if (hasAny ({ "crash", "ride", "cymbal", "prato" }))
        return SampleRole::cymbal;

    if (hasAny ({ "perc", "shaker", "tamb", "conga", "bongo", "cowbell", "clave", "block" }) || hasWord ("tom") || hasWord ("toms"))
        return SampleRole::perc;

    if (hasAny ({ "808", "bass", "baixo", "subbass" }) || hasWord ("sub") || hasWord ("grave"))
        return SampleRole::bass;

    // O resto pelo papel que o mixer adivinha (voz, efeitos).
    const std::string_view mixerRole = dsp::guessRole (name);

    if (mixerRole == "vocal" || mixerRole == "backing")
        return SampleRole::vocal;

    if (mixerRole == "fx")
        return SampleRole::fx;

    return SampleRole::unknown;
}

//==============================================================================
SampleProfile analyseSample (const juce::File& file)
{
    SampleProfile p;
    const auto o = readOpening (file);

    if (o.channels.empty() || o.channels.front().size() < 64)
        return p;

    p.valid = true;
    p.seconds = o.seconds;
    p.pitch = dsp::detectSamplePitch (o.channels, o.sampleRate);

    // Mistura em mono, acha o pico e mede o corpo e o grave a partir dele.
    const auto length = o.channels.front().size();
    const auto mono = mixToMono (o);

    std::size_t peakAt = 0;
    float peak = 0.0f;

    for (std::size_t i = 0; i < length; ++i)
    {
        if (std::abs (mono[i]) > peak)
        {
            peak = std::abs (mono[i]);
            peakAt = i;
        }
    }

    if (peak <= 1.0e-6f)
        return p;

    auto at = [&] (double seconds) { return peakAt + (std::size_t) (seconds * o.sampleRate); };
    const double head = rms (mono, peakAt, at (0.050));
    const double body = rms (mono, at (0.300), at (0.450));
    p.bodyDb = head > 0.0 && body > 0.0 ? 20.0 * std::log10 (body / head) : -200.0;

    // Dois passa-baixas de um polo em 150 Hz: a parte do som que é "grave".
    const double k = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 150.0 / o.sampleRate);
    double z1 = 0.0, z2 = 0.0, low = 0.0, all = 0.0;
    const auto end = std::min (length, at (1.0));

    for (std::size_t i = peakAt; i < end; ++i)
    {
        z1 += k * ((double) mono[i] - z1);
        z2 += k * (z1 - z2);
        low += z2 * z2;
        all += (double) mono[i] * (double) mono[i];
    }

    p.lowShare = all > 0.0 ? juce::jlimit (0.0, 1.0, low / all) : 0.0;
    return p;
}

bool soundsLikeBass (const SampleProfile& p) noexcept
{
    // Um kick também é grave, mas some antes de 300 ms; um pad é longo, mas não é grave.
    // Com nota, ela precisa estar abaixo do Mi 3 (~165 Hz): o corpo de um 808 fica entre 30 e 80 Hz.
    return p.valid && p.seconds >= 0.45 && p.bodyDb > -18.0 && p.lowShare >= 0.6
        && (! rootIsReliable (p.pitch) || p.pitch.key <= 52);
}

SampleRole guessSampleRole (const juce::File& file, const SampleProfile& profile, const juce::String& givenName)
{
    for (auto& name : { file.getFileNameWithoutExtension(), givenName, file.getParentDirectory().getFileName() })
        if (name.isNotEmpty())
            if (const auto role = roleFromName (name); role != SampleRole::unknown)
                return role;

    return soundsLikeBass (profile) ? SampleRole::bass : SampleRole::unknown;
}

//==============================================================================
LoopSuggestion suggestLoop (const juce::File& file)
{
    LoopSuggestion s;
    const auto o = readOpening (file, 30.0);

    if (o.channels.empty() || o.totalFrames < 2)
        return s;

    const double rate = o.sampleRate;
    const auto length = o.channels.front().size();
    const auto mono = mixToMono (o);

    // Envelope em janelas de 10 ms e o pico dele.
    const auto hop = (std::size_t) std::max (1.0, 0.010 * rate);
    std::vector<double> env;

    for (std::size_t pos = 0; pos + hop <= length; pos += hop)
        env.push_back (rms (mono, pos, pos + hop));

    if (env.empty())
        return s;

    const auto peakWindow = (std::size_t) std::distance (env.begin(), std::max_element (env.begin(), env.end()));

    if (env[peakWindow] <= 1.0e-6)
        return s;

    // O laço fica depois do ataque e da queda de tom (120 ms depois do pico) e acaba onde o som
    // cai abaixo de -12 dB do pico; num som que não cai tanto, 10 ms antes do fim.
    const double minStart = (double) (peakWindow * hop) + 0.12 * rate;
    double endAt = (double) length - 0.010 * rate - 2.0;

    for (auto w = peakWindow; w < env.size(); ++w)
    {
        if (env[w] < env[peakWindow] * 0.25)
        {
            endAt = std::min (endAt, (double) (w * hop));
            break;
        }
    }

    if (endAt - minStart < 0.03 * rate)
        return s;   // curto demais para um laço depois do ataque

    // Com nota (YIN nos primeiros 6 s), o laço tem períodos inteiros dela; o tamanho é um quarto
    // do corpo, entre 60 e 250 ms.
    std::vector<std::vector<float>> head;

    for (auto& ch : o.channels)
        head.emplace_back (ch.begin(), ch.begin() + (std::ptrdiff_t) std::min (length, (std::size_t) (6.0 * rate)));

    const auto pitch = dsp::detectSamplePitch (head, rate);
    const double period = rootIsReliable (pitch) && pitch.hz > 0.0 ? rate / pitch.hz : 0.0;
    double target = std::clamp (0.25 * (endAt - minStart) / rate, 0.06, 0.25) * rate;

    if (period > 0.0)
        target = std::max (2.0, std::floor (std::min (target, endAt - minStart) / period)) * period;

    // Cruzamento de zero subindo mais perto de `around` (com fração), até `window` frames de distância.
    auto crossing = [&mono, length] (double around, double window)
    {
        const auto from = (std::size_t) std::max (1.0, around - window);
        const auto to = (std::size_t) std::min ((double) length - 1.0, around + window);
        double best = around, bestDistance = window + 1.0;

        for (auto i = from; i <= to; ++i)
        {
            if (mono[i - 1] <= 0.0f && mono[i] > 0.0f)
            {
                const double x = (double) (i - 1) + (double) mono[i - 1] / ((double) mono[i - 1] - (double) mono[i]);

                if (std::abs (x - around) < bestDistance)
                {
                    best = x;
                    bestDistance = std::abs (x - around);
                }
            }
        }

        return best;
    };

    const double window = period > 0.0 ? period * 0.5 : 0.002 * rate;
    const double end = crossing (endAt, window);
    const double start = crossing (std::max (minStart, end - target), window);

    if (end - start < 16.0 || start <= 0.0)
        return s;

    const double last = (double) (o.totalFrames - 1);
    s.found = true;
    s.start = (float) (start / last);
    s.end = (float) (end / last);
    s.seconds = (end - start) / rate;
    s.xfadeMs = (float) std::min (30.0, 500.0 * s.seconds);
    return s;
}

//==============================================================================
double bpmFromName (const juce::String& text)
{
    // O número logo antes (ou logo depois) de "bpm", com espaço, "_" ou "-" no meio.
    const auto name = text.toLowerCase();

    for (int at = name.indexOf ("bpm"); at >= 0; at = name.indexOf (at + 1, "bpm"))
    {
        int end = at;

        while (end > 0 && juce::String (" _-").containsChar (name[end - 1]))
            --end;

        int begin = end;

        while (begin > 0 && juce::CharacterFunctions::isDigit (name[begin - 1]))
            --begin;

        double value = begin < end ? name.substring (begin, end).getDoubleValue() : 0.0;

        if (value == 0.0)   // "bpm140", "BPM_92"
        {
            int from = at + 3;

            while (from < name.length() && juce::String (" _-").containsChar (name[from]))
                ++from;

            int to = from;

            while (to < name.length() && juce::CharacterFunctions::isDigit (name[to]))
                ++to;

            value = to > from ? name.substring (from, to).getDoubleValue() : 0.0;
        }

        if (value >= 40.0 && value <= 300.0)
            return value;
    }

    return 0.0;
}

double bpmFromLength (double seconds, double projectBpm)
{
    if (! (seconds > 0.0) || ! (projectBpm > 0.0))
        return 0.0;

    double best = 0.0, bestDistance = 1.0e9;

    for (double beats : { 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 })
    {
        const double bpm = beats * 60.0 / seconds;

        if (bpm < 60.0 || bpm > 200.0)
            continue;

        if (const double d = std::abs (std::log2 (bpm / projectBpm)); d < bestDistance)
        {
            best = bpm;
            bestDistance = d;
        }
    }

    return best;
}

SliceResult sliceFile (const juce::File& file, SliceMode mode, audio::Sensitivity sensitivity, double divisionBeats,
                       double bpm, double projectBpm)
{
    SliceResult r;
    const auto o = readOpening (file, 60.0);

    if (o.channels.empty() || o.totalFrames < 2)
    {
        r.error = "Não deu para ler o sample: "_t + file.getFileName();
        return r;
    }

    r.seconds = o.seconds;
    r.bpm = bpm > 0.0 ? bpm : bpmFromName (file.getFileNameWithoutExtension());

    if (! (r.bpm > 0.0))
        r.bpm = bpmFromLength (o.seconds, projectBpm);

    if (! (r.bpm > 0.0))
        r.bpm = projectBpm > 0.0 ? projectBpm : 120.0;

    r.ok = true;

    if (mode == SliceMode::none)
        return r;

    const double rate = o.sampleRate;
    std::vector<double> marks;

    if (mode == SliceMode::attack)
    {
        const auto mono = mixToMono (o);
        marks = audio::detectOnsets (mono.data(), mono.size(), rate, sensitivity);
    }
    else
    {
        const double step = juce::jlimit (1.0 / 16.0, 16.0, divisionBeats) * 60.0 / r.bpm * rate;

        for (double at = step; at < (double) o.totalFrames; at += step)
            marks.push_back (at);
    }

    r.starts = audio::sliceStarts (marks, (std::size_t) o.totalFrames, 0.02 * rate);   // fatias de pelo menos 20 ms

    if (r.starts.size() > 128)
    {
        r.starts.resize (128);
        r.truncated = true;
    }

    return r;
}

std::vector<double> onsetsIn (const juce::File& file, double fromSeconds, double toSeconds, audio::Sensitivity sensitivity)
{
    std::vector<double> out;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || ! (toSeconds > fromSeconds))
        return out;

    const double rate = reader->sampleRate;
    const auto from = (juce::int64) std::llround (juce::jmax (0.0, fromSeconds) * rate);
    const auto to = juce::jmin (reader->lengthInSamples, (juce::int64) std::llround (juce::jmin (toSeconds, fromSeconds + 600.0) * rate));

    if (to - from < 64)
        return out;

    const int numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> buffer (numChannels, (int) (to - from));
    reader->read (&buffer, 0, (int) (to - from), from, true, numChannels > 1);

    std::vector<float> mono ((size_t) buffer.getNumSamples(), 0.0f);

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            mono[(size_t) i] += buffer.getSample (ch, i) / (float) numChannels;

    for (double frame : audio::detectOnsets (mono.data(), mono.size(), rate, sensitivity))
        out.push_back ((double) from / rate + frame / rate);

    return out;
}

} // namespace whynot
