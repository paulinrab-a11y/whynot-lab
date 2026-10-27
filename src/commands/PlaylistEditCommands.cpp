// Edição da voz na playlist (frente A de 27/10): editar o áudio gravado como numa fita, por trecho de tempo.
// - playlist.cutAt: corta tudo o que passa por um ponto (a agulha, ou as bordas da seleção de tempo);
// - playlist.removeSpace: apaga um trecho e puxa o resto (Ctrl+Delete na playlist);
// - playlist.insertSpace: abre um espaço vazio (Ctrl+Insert);
// - audio.findSilence e audio.removeSilence: acha (a prévia) e tira o silêncio e a respiração de um clipe de voz,
//   com o limiar ajustável; tirar corta o clipe e apaga (ou abaixa) os trechos quietos.
// As contas ficam em PlaylistEdit.h. Cada comando é um passo de desfazer. Dono: Voz na playlist.

#include "CommandRegistry.h"
#include "../model/PlaylistEdit.h"
#include "../record/ReversedAudio.h"

namespace whynot
{

namespace
{
    using params::schema;

    constexpr double maxTicks = (double) maxTimelineTicks;

    int toTicks (const juce::var& args, double value)
    {
        const auto unit = params::string (args, "unit", "ticks");
        const double scale = unit == "steps" ? ticksPerStep : unit == "beats" ? ticksPerBeat : unit == "bars" ? ticksPerBar4 : 1.0;
        const double ticks = value * scale;
        return std::isfinite (ticks) ? juce::roundToInt (juce::jlimit (-maxTicks, maxTicks, ticks)) : 0;
    }

    /** Um trecho quieto de um clipe, em ticks da playlist. */
    struct Quiet
    {
        int from = 0, to = 0;
    };

    /** O que o audio.findSilence e o audio.removeSilence leem dos parâmetros. */
    struct SilenceSettings
    {
        double thresholdDb = -45.0;   // abaixo disso (RMS em janelas de 10 ms) é silêncio; uns -30 dB pegam a respiração
        double minSeconds = 0.25;     // só trechos quietos pelo menos deste tamanho
        double padSeconds = 0.04;     // a sobra que fica de cada lado (o fim da palavra e o ataque da próxima)
    };

    SilenceSettings silenceSettings (const juce::var& a)
    {
        SilenceSettings s;
        s.thresholdDb = juce::jlimit (-90.0, -6.0, params::number (a, "thresholdDb", s.thresholdDb));
        s.minSeconds = juce::jlimit (0.05, 10.0, params::number (a, "minMs", 250.0) / 1000.0);
        s.padSeconds = juce::jlimit (0.0, 0.5, params::number (a, "padMs", 40.0) / 1000.0);
        return s;
    }

    /** Os trechos quietos do que o bloco toca, em ticks da playlist, do começo para o fim. Falha se o arquivo não abre. */
    juce::Result findQuiet (CommandContext& c, const PlaylistItem& item, const SilenceSettings& s, std::vector<Quiet>& out)
    {
        const auto& p = c.model.get();
        const auto* channel = p.findChannel (item.ref);

        if (channel == nullptr)
            return juce::Result::fail ("Canal do clipe não encontrado."_t);

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const auto file = reversed::playedFile (c.model.resolveSample (channel->samplePath), item.reversed);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->sampleRate <= 0.0)
            return juce::Result::fail ("Não consegui ler o áudio do bloco."_t);

        // o envelope em janelas de 10 ms do trecho que o bloco toca (no tempo do arquivo)
        const double rate = reader->sampleRate;
        const int window = juce::jmax (1, (int) std::round (0.010 * rate));
        const auto first = (juce::int64) std::floor (item.offsetSeconds * rate);
        const auto last = juce::jmin (reader->lengthInSamples,
                                      first + (juce::int64) std::ceil (item.fileSeconds (ticksToSeconds (item.length, p.bpm)) * rate));
        const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        juce::AudioBuffer<float> block (channels, window);
        std::vector<bool> quiet;
        const double threshold = std::pow (10.0, s.thresholdDb / 20.0);

        for (auto pos = first; pos < last; pos += window)
        {
            const int n = (int) juce::jmin ((juce::int64) window, last - pos);
            reader->read (&block, 0, n, pos, true, channels > 1);
            float rms = 0.0f;

            for (int ch = 0; ch < channels; ++ch)
                rms = juce::jmax (rms, block.getRMSLevel (ch, 0, n));

            quiet.push_back (rms < threshold);
        }

        // as corridas quietas, menos a sobra de cada lado; as que encostam no começo ou no fim do bloco vão até a borda
        const double windowSeconds = (double) window / rate;
        auto toTick = [&] (double fileSecondsFromOffset)
        {
            return item.start + juce::roundToInt (secondsToTicks (fileSecondsFromOffset * item.stretch, p.bpm));
        };

        for (size_t i = 0; i < quiet.size();)
        {
            if (! quiet[i])
            {
                ++i;
                continue;
            }

            size_t j = i;

            while (j < quiet.size() && quiet[j])
                ++j;

            const double from = (double) i * windowSeconds, to = (double) j * windowSeconds;

            if (to - from >= s.minSeconds)
            {
                const int a = i == 0 ? item.start : toTick (from + s.padSeconds);
                const int b = j == quiet.size() ? item.end() : toTick (to - s.padSeconds);

                if (b - a > 0)
                    out.push_back ({ juce::jlimit (item.start, item.end(), a), juce::jlimit (item.start, item.end(), b) });
            }

            i = j;
        }

        return juce::Result::ok();
    }

    juce::var quietVar (const std::vector<Quiet>& regions, double bpm)
    {
        juce::Array<juce::var> list;
        double seconds = 0.0;

        for (auto& q : regions)
        {
            auto o = new juce::DynamicObject();
            o->setProperty ("from", q.from);
            o->setProperty ("to", q.to);
            list.add (juce::var (o));
            seconds += ticksToSeconds (q.to - q.from, bpm);
        }

        auto o = new juce::DynamicObject();
        o->setProperty ("regions", list);
        o->setProperty ("seconds", std::round (seconds * 100.0) / 100.0);
        return juce::var (o);
    }

    juce::var spaceResult (const playlistedit::SpaceEdit& e)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("cut", e.cut);
        o->setProperty ("removed", e.removed);
        o->setProperty ("moved", e.moved);
        return juce::var (o);
    }
}

void registerPlaylistEditCommands (CommandRegistry& r)
{
    r.add ({ "playlist.cutAt", "Cortar aqui"_t,
             schema ({ { "at", "number", true, "Onde cortar, na linha do tempo (a agulha, por exemplo)"_t },
                       { "unit", "string", false, "Unidade de at: ticks (padrão), steps, beats ou bars"_t },
                       { "items", "array", false, "Só estes blocos (ids); ausente = todos os que passam pelo ponto"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const int at = toTicks (a, params::number (a, "at", 0));
                 std::set<juce::String> only;

                 if (auto* list = a["items"].getArray())
                     for (auto& id : *list)
                         only.insert (id.toString());

                 if (params::has (a, "items") && only.empty())
                     return CommandResult::fail ("A lista de blocos está vazia."_t);

                 const auto rights = playlistedit::cutAt (p, at, only);

                 if (rights.isEmpty())
                     return CommandResult::fail ("Nenhum bloco passa por esse ponto."_t);

                 juce::Array<juce::var> ids;

                 for (auto& id : rights)
                     ids.add (id);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("items", ids);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "playlist.removeSpace", "Apagar o trecho e puxar o resto"_t,
             schema ({ { "from", "number", true, "Começo do trecho, na linha do tempo"_t },
                       { "to", "number", true, "Fim do trecho (depois do começo)"_t },
                       { "unit", "string", false, "Unidade: ticks (padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const int from = juce::jmax (0, toTicks (a, params::number (a, "from", 0)));
                 const int to = toTicks (a, params::number (a, "to", 0));

                 if (to <= from)
                     return CommandResult::fail ("O fim do trecho precisa vir depois do começo."_t);

                 return CommandResult::success (spaceResult (playlistedit::removeSpace (c.model.data(), from, to)));
             } });

    r.add ({ "playlist.insertSpace", "Abrir espaço"_t,
             schema ({ { "at", "number", true, "Onde o espaço começa, na linha do tempo"_t },
                       { "length", "number", true, "Tamanho do espaço"_t },
                       { "unit", "string", false, "Unidade de at e length: ticks (padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const int at = juce::jmax (0, toTicks (a, params::number (a, "at", 0)));
                 const int length = toTicks (a, params::number (a, "length", 0));

                 if (length <= 0)
                     return CommandResult::fail ("O espaço precisa ter tamanho."_t);

                 playlistedit::SpaceEdit e;

                 if (! playlistedit::insertSpace (c.model.data(), at, length, e))
                     return CommandResult::fail ("Não cabe: algum bloco passaria do fim da linha do tempo."_t);

                 return CommandResult::success (spaceResult (e));
             } });

    // Silêncio e respiração (voz na playlist, 27/10). Os dois leem os mesmos parâmetros; o find é a prévia.
#define WHYNOT_SILENCE_FIELDS \
        { "item", "string", true, "Id do bloco de áudio"_t }, \
        { "thresholdDb", "number", false, "Limiar em dBFS (padrão -45): abaixo dele é silêncio; uns -30 pegam também a respiração"_t }, \
        { "minMs", "number", false, "Só trechos quietos com pelo menos isso, em ms (padrão 250)"_t }, \
        { "padMs", "number", false, "A sobra que fica de cada lado de um trecho quieto, em ms (padrão 40)"_t }

    r.add ({ "audio.findSilence", "Achar silêncio e respiração"_t,
             schema ({ WHYNOT_SILENCE_FIELDS }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto* item = c.model.get().findItem (params::string (a, "item"));

                 if (item == nullptr || item->kind != "audio")
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 std::vector<Quiet> regions;

                 if (const auto found = findQuiet (c, *item, silenceSettings (a), regions); found.failed())
                     return CommandResult::fail (found.getErrorMessage());

                 return CommandResult::success (quietVar (regions, c.model.get().bpm));
             } });

    r.add ({ "audio.removeSilence", "Tirar silêncio e respiração"_t,
             schema ({ WHYNOT_SILENCE_FIELDS,
                       { "reduceDb", "number", false, "Em vez de apagar, abaixa os trechos quietos nisso, em dB (por exemplo -15); ausente = apaga"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto itemId = params::string (a, "item");
                 const auto* item = p.findItem (itemId);

                 if (item == nullptr || item->kind != "audio")
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 std::vector<Quiet> regions;

                 if (const auto found = findQuiet (c, *item, silenceSettings (a), regions); found.failed())
                     return CommandResult::fail (found.getErrorMessage());

                 if (regions.empty())
                     return CommandResult::fail ("Nenhum trecho quieto com esse limiar."_t);

                 if (regions.size() == 1 && regions[0].from <= item->start && regions[0].to >= item->end())
                     return CommandResult::fail ("Com esse limiar o clipe inteiro é silêncio: abaixe o limiar."_t);

                 // corta nas bordas de cada trecho, só nos pedaços deste clipe
                 std::set<juce::String> pieces { itemId };

                 for (auto& q : regions)
                     for (int at : { q.from, q.to })
                         for (auto& id : playlistedit::cutAt (p, at, pieces))
                             pieces.insert (id);

                 const bool reduce = params::has (a, "reduceDb");
                 const float reduceDb = (float) juce::jlimit (-60.0, 0.0, params::number (a, "reduceDb", -15.0));
                 constexpr double antiClick = 0.005;   // 5 ms nas bordas que encostam num trecho apagado
                 auto inside = [&regions] (const PlaylistItem& i)
                 {
                     return std::any_of (regions.begin(), regions.end(), [&i] (const Quiet& q) { return i.start >= q.from && i.end() <= q.to; });
                 };
                 int changed = 0;

                 for (auto& i : p.playlistItems)
                 {
                     if (pieces.count (i.id) == 0)
                         continue;

                     if (inside (i))
                     {
                         ++changed;

                         if (reduce)
                             i.gainDb = clampClipGain (i.gainDb + reduceDb);
                     }
                     else if (! reduce)
                     {
                         const double half = ticksToSeconds (i.length, p.bpm) / 2.0;

                         for (auto& q : regions)
                         {
                             if (i.end() == q.from)
                                 i.fadeOut = juce::jmax (i.fadeOut, juce::jmin (antiClick, half));

                             if (i.start == q.to)
                                 i.fadeIn = juce::jmax (i.fadeIn, juce::jmin (antiClick, half));
                         }
                     }
                 }

                 if (! reduce)
                     std::erase_if (p.playlistItems, [&] (const PlaylistItem& i) { return pieces.count (i.id) != 0 && inside (i); });

                 juce::Array<juce::var> left;

                 for (auto& i : p.playlistItems)
                     if (pieces.count (i.id) != 0)
                         left.add (i.id);

                 auto o = quietVar (regions, p.bpm);
                 o.getDynamicObject()->setProperty ("items", left);
                 o.getDynamicObject()->setProperty (reduce ? "lowered" : "removed", changed);
                 return CommandResult::success (o);
             } });

#undef WHYNOT_SILENCE_FIELDS
}

} // namespace whynot
