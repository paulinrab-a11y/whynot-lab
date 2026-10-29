// Edição da voz na playlist (frente A de 27/10): editar o áudio gravado como numa fita, por trecho de tempo.
// - playlist.cutAt: corta tudo o que passa por um ponto (a agulha, ou as bordas da seleção de tempo);
// - playlist.removeSpace: apaga um trecho e puxa o resto (Ctrl+Delete na playlist);
// - playlist.insertSpace: abre um espaço vazio (Ctrl+Insert);
// - audio.findSilence e audio.removeSilence: acha (a prévia) e tira o silêncio e a respiração de um clipe de voz,
//   com o limiar ajustável; tirar corta o clipe e apaga (ou abaixa) os trechos quietos.
// As contas ficam em PlaylistEdit.h. Cada comando é um passo de desfazer. Dono: Voz na playlist.
// Playlist fatia 6 (27/10, noite):
// - audio.setProperties: o painel de propriedades do bloco (nome e cor do canal, ganho, pan, tom e afinação fina,
//   fades com curva, mudo e travar), tudo num passo de desfazer;
// - playlist.gotoMarker e playlist.selectMarkers: pular de marcador em marcador, e o trecho (ou o laço) entre dois.

#include "CommandRegistry.h"
#include "../model/PlaylistEdit.h"
#include "../record/BlockFile.h"
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
        const auto file = blockfile::playedOf (c.model, *channel, item);
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

    /** Abaixar sem degrau (playlist fatia 6, 27/10 noite): cada pedaço abaixado avança 10 ms sobre o vizinho de cada
        lado, e os dois fazem ali um fade reto, um descendo e o outro subindo. O som é o mesmo trecho do arquivo nos
        dois, então a soma é uma rampa reta do ganho cheio para o abaixado (sem o salto, e sem o buraco de um fade
        até zero). O fade reto um microssegundo mais longo que a sobra vale no lugar do crossfade automático. */
    template <typename Inside>
    void rampLowered (Project& p, const std::set<juce::String>& pieces, Inside inside)
    {
        const int ramp = juce::jmax (1, juce::roundToInt (secondsToTicks (0.010, p.bpm)));
        std::vector<size_t> lowered;

        for (size_t k = 0; k < p.playlistItems.size(); ++k)
            if (pieces.count (p.playlistItems[k].id) != 0 && inside (p.playlistItems[k]))
                lowered.push_back (k);

        for (auto k : lowered)
        {
            auto& low = p.playlistItems[k];
            const int start = low.start, end = low.end(), length = low.length;

            for (auto& n : p.playlistItems)
            {
                if (n.id == low.id || pieces.count (n.id) == 0 || inside (n))
                    continue;

                // o +1 µs dos dois fades do vizinho cabe nele (um tick é muito mais que 2 µs)
                const int r = juce::jmin (ramp, (n.length - 1) / 2, (length - 1) / 2);
                const double seconds = roundToMicroseconds (ticksToSeconds (r, p.bpm)) + 1.0e-6;
                const double back = low.fileSeconds (ticksToSeconds (r, p.bpm));

                if (r <= 0)
                    continue;

                if (n.end() == start && low.offsetSeconds >= back)
                {
                    low.start -= r;
                    low.length += r;
                    low.offsetSeconds = roundToMicroseconds (low.offsetSeconds - back);
                    low.fadeIn = seconds, low.fadeInCurve = fadeCurveLinear;
                    n.fadeOut = seconds, n.fadeOutCurve = fadeCurveLinear;
                }
                else if (n.start == end)
                {
                    low.length += r;
                    low.fadeOut = seconds, low.fadeOutCurve = fadeCurveLinear;
                    n.fadeIn = seconds, n.fadeInCurve = fadeCurveLinear;
                }
            }
        }
    }

    juce::var spaceResult (const playlistedit::SpaceEdit& e)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("cut", e.cut);
        o->setProperty ("removed", e.removed);
        o->setProperty ("moved", e.moved);
        return juce::var (o);
    }

    juce::var markerVar (const Marker& m)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("marker", m.id);
        o->setProperty ("name", m.name);
        o->setProperty ("at", m.t);
        return juce::var (o);
    }

    // Marcadores com ações (playlist fatia 6, 27/10 noite). Criar, renomear e apagar já são playlist.addMarker,
    // updateMarker e removeMarker; o Alt+T cria na agulha.
    void registerMarkerCommands (CommandRegistry& r)
    {
        r.add ({ "playlist.gotoMarker", "Ir ao marcador"_t,
                 schema ({ { "direction", "string", false, "next (o próximo depois da agulha) ou previous (o anterior); ausente com marker"_t },
                           { "marker", "string", false, "Id do marcador (no lugar de direction)"_t },
                           { "from", "number", false, "De onde contar o próximo e o anterior, em ticks (ausente = a agulha; tocando, passe a posição)"_t } }), false,
                 [] (CommandContext& c, const juce::var& a)
                 {
                     const auto& p = c.model.get();
                     const Marker* found = nullptr;

                     if (params::has (a, "marker"))
                     {
                         found = p.findMarker (params::string (a, "marker"));

                         if (found == nullptr)
                             return CommandResult::fail ("Marcador não encontrado."_t);
                     }
                     else
                     {
                         const auto direction = params::string (a, "direction");
                         const int from = params::has (a, "from") ? (int) params::number (a, "from", 0) : c.app.playStart();

                         if (direction != "next" && direction != "previous")
                             return CommandResult::fail ("Diga direction (next ou previous) ou o marker."_t);

                         // os marcadores estão em ordem: o próximo é o primeiro depois, o anterior o último antes
                         for (auto& m : p.markers)
                             if (direction == "next" ? (m.t > from && found == nullptr) : m.t < from)
                                 found = &m;

                         if (found == nullptr)
                             return CommandResult::fail (p.markers.empty() ? "A música não tem marcadores (Alt+T cria um na agulha)."_t
                                                         : direction == "next" ? "Não há marcador depois daqui."_t
                                                                               : "Não há marcador antes daqui."_t);
                     }

                     // a agulha vai para lá (o play começa dela); tocando, o som pula junto
                     c.app.setPlayStart (found->t);

                     return CommandResult::success (markerVar (*found));
                 } });

        r.add ({ "playlist.selectMarkers", "Trecho entre marcadores"_t,
                 schema ({ { "from", "string", true, "Id do marcador do começo"_t },
                           { "to", "string", false, "Id do marcador do fim (ausente = o seguinte, ou o fim da música)"_t },
                           { "loop", "boolean", false, "true = o trecho vira o laço: liga o loop e o modo SONG (a playlist toca o trecho em volta)"_t } }), true,
                 [] (CommandContext& c, const juce::var& a)
                 {
                     auto& p = c.model.data();
                     const auto* from = p.findMarker (params::string (a, "from"));
                     const auto* to = params::has (a, "to") ? p.findMarker (params::string (a, "to")) : nullptr;

                     if (from == nullptr || (params::has (a, "to") && to == nullptr))
                         return CommandResult::fail ("Marcador não encontrado."_t);

                     int start = from->t, end = to != nullptr ? to->t : p.markerEnd (*from);

                     if (end < start)
                         std::swap (start, end);   // a ordem dos dois não importa

                     if (end <= start)
                         return CommandResult::fail ("Os dois marcadores estão no mesmo lugar."_t);

                     if (params::boolean (a, "loop", false))
                     {
                         p.songMode = true;
                         p.loop = true;
                     }

                     c.app.setTimeSelection ({ start, end });
                     c.app.setPlayStart (start);

                     auto o = new juce::DynamicObject();
                     o->setProperty ("from", start);
                     o->setProperty ("to", end);
                     o->setProperty ("loop", p.songMode && p.loop);
                     return CommandResult::success (juce::var (o));
                 } });
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
                 else
                     rampLowered (p, pieces, inside);

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

    //==========================================================================
    // O painel de propriedades do bloco (playlist fatia 6, 27/10 noite). Só muda o que vier nos parâmetros; o nome e a
    // cor são do canal do bloco (o nome que aparece no cabeçalho). O resto é do bloco: o arquivo não muda.
    r.add ({ "audio.setProperties", "Propriedades do bloco"_t,
             schema ({ { "item", "string", true, "Id do bloco (mudo e travar valem em qualquer bloco; o resto, só no áudio)"_t },
                       { "name", "string", false, "Nome do canal do bloco (o do cabeçalho), até 40 letras"_t },
                       { "colour", "string", false, "Cor do canal do bloco, \"#RRGGBB\""_t },
                       { "gainDb", "number", false, "Ganho do bloco em dB, de -60 a +24"_t },
                       { "pan", "number", false, "Pan do bloco, de -1 (esquerda) a 1 (direita)"_t },
                       { "semitones", "number", false, "Tom do bloco em semitons inteiros, de -24 a +24 (soma com cents)"_t },
                       { "cents", "number", false, "Afinação fina em centésimos, de -100 a +100 (soma com semitones)"_t },
                       { "fadeIn", "number", false, "Fade de entrada, em segundos"_t },
                       { "fadeOut", "number", false, "Fade de saída, em segundos"_t },
                       { "inCurve", "string", false, "Curva do fade de entrada: linear, convex (rápida), concave (lenta) ou s"_t },
                       { "outCurve", "string", false, "Curva do fade de saída: linear, convex, concave ou s"_t },
                       { "muted", "boolean", false, "Mudo só neste bloco"_t },
                       { "locked", "boolean", false, "Travado: o mouse não move, não apara, não corta nem apaga"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* item = p.findItem (params::string (a, "item"));

                 if (item == nullptr)
                     return CommandResult::fail ("Bloco não encontrado."_t);

                 const bool audio = item->kind == "audio";
                 auto* channel = audio ? p.findChannel (item->ref) : nullptr;

                 for (auto* key : { "name", "colour", "gainDb", "pan", "semitones", "cents", "fadeIn", "fadeOut", "inCurve", "outCurve" })
                     if (params::has (a, key) && channel == nullptr)
                         return CommandResult::fail ("Só mudo e travar valem num bloco de padrão."_t);

                 if (params::has (a, "name"))
                 {
                     const auto name = params::string (a, "name").trim().substring (0, maxMarkerName).trim();

                     if (name.isEmpty())
                         return CommandResult::fail ("O nome não pode ficar vazio."_t);

                     // a trilha automática acompanha o nome do canal, como no channel.rename
                     if (auto* mx = p.findMixer (channel->output); mx != nullptr && mx->autoFor == channel->id && mx->name == channel->name)
                         mx->name = name;

                     channel->name = name;
                 }

                 if (params::has (a, "colour"))
                 {
                     const auto colour = params::string (a, "colour").trim();

                     if (! (colour.length() == 7 && colour.startsWithChar ('#') && colour.substring (1).containsOnly ("0123456789abcdefABCDEF")))
                         return CommandResult::fail ("A cor vai como \"#RRGGBB\"."_t);

                     channel->colour = colour.toUpperCase();
                 }

                 auto number = [&a] (const char* key)
                 {
                     const double v = params::number (a, key, 0.0);
                     return std::isfinite (v) ? v : 0.0;
                 };

                 if (params::has (a, "gainDb"))
                     item->gainDb = clampClipGain ((float) number ("gainDb"));

                 if (params::has (a, "pan"))
                     item->pan = (float) (std::round (juce::jlimit (-1.0, 1.0, number ("pan")) * 100.0) / 100.0);

                 if (params::has (a, "semitones") || params::has (a, "cents"))
                 {
                     // o tom guarda os dois juntos (semitons com centésimos): o que não veio fica como estava
                     const double whole = std::round ((double) item->pitch), fine = std::round (((double) item->pitch - whole) * 100.0);
                     const double st = params::has (a, "semitones") ? std::round (number ("semitones")) : whole;
                     const double cents = params::has (a, "cents") ? juce::jlimit (-100.0, 100.0, std::round (number ("cents"))) : fine;
                     const double total = st + cents / 100.0;

                     if (std::abs (total) > maxClipPitch)
                         return CommandResult::fail ("O tom vai de -24 a +24 semitons."_t);

                     item->pitch = (float) (std::round (total * 100.0) / 100.0);
                 }

                 const double length = ticksToSeconds (item->length, p.bpm);

                 if (params::has (a, "fadeIn"))
                     item->fadeIn = roundToMicroseconds (juce::jlimit (0.0, length, number ("fadeIn")));

                 if (params::has (a, "fadeOut"))
                     item->fadeOut = roundToMicroseconds (juce::jlimit (0.0, length, number ("fadeOut")));

                 if (item->fadeIn + item->fadeOut > length)   // os dois cabem no bloco: o que não veio nos parâmetros cede
                 {
                     if (params::has (a, "fadeOut") && ! params::has (a, "fadeIn"))
                         item->fadeIn = roundToMicroseconds (juce::jmax (0.0, length - item->fadeOut));
                     else
                         item->fadeOut = roundToMicroseconds (juce::jmax (0.0, length - item->fadeIn));
                 }

                 for (auto [key, curve] : { std::pair<const char*, int*> { "inCurve", &item->fadeInCurve }, std::pair<const char*, int*> { "outCurve", &item->fadeOutCurve } })
                 {
                     if (! params::has (a, key))
                         continue;

                     const auto name = params::string (a, key);

                     if (name != "linear" && fadeCurveFromName (name) == fadeCurveLinear)
                         return CommandResult::fail ("Curva desconhecida: use linear, convex, concave ou s."_t);

                     *curve = fadeCurveFromName (name);
                 }

                 if (params::has (a, "muted"))
                     item->muted = params::boolean (a, "muted", false);

                 if (params::has (a, "locked"))
                     item->locked = params::boolean (a, "locked", false);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("item", item->id);
                 o->setProperty ("muted", item->muted);
                 o->setProperty ("locked", item->locked);

                 if (channel != nullptr)
                 {
                     o->setProperty ("name", channel->name);
                     o->setProperty ("colour", channel->colour);
                     o->setProperty ("gainDb", item->gainDb);
                     o->setProperty ("pan", item->pan);
                     o->setProperty ("semitones", item->pitch);
                     o->setProperty ("fadeIn", item->fadeIn);
                     o->setProperty ("fadeOut", item->fadeOut);
                     o->setProperty ("inCurve", fadeCurveName (item->fadeInCurve));
                     o->setProperty ("outCurve", fadeCurveName (item->fadeOutCurve));
                     o->setProperty ("reversed", item->reversed);
                 }

                 return CommandResult::success (juce::var (o));
             } });

    registerMarkerCommands (r);
}

} // namespace whynot
