#include "MidiImport.h"
#include "../model/NoteCurves.h"

#include <array>
#include <limits>

namespace whynot
{

namespace
{
    juce::Result load (const juce::File& file, juce::MidiFile& midi, int& format)
    {
        juce::FileInputStream in (file);

        if (! in.openedOk())
            return juce::Result::fail ("Não deu para abrir "_t + file.getFileName() + ".");

        // com true, a nota que o arquivo não desliga ganha o note-off no fim da trilha
        if (! midi.readFrom (in, true, &format))
            return juce::Result::fail (file.getFileName() + " não é um arquivo MIDI válido."_t);

        return juce::Result::ok();
    }

    void describe (const juce::MidiFile& midi, int format, MidiFileInfo& info)
    {
        info = {};
        info.format = format;
        info.ticksPerBeat = juce::jmax (0, (int) midi.getTimeFormat());   // negativo = SMPTE

        juce::MidiMessageSequence events;
        midi.findAllTempoEvents (events);

        if (events.getNumEvents() > 0)
            if (const double spq = events.getEventPointer (0)->message.getTempoSecondsPerQuarterNote(); spq > 0.0)
                info.bpm = std::round (60.0 / spq * 100.0) / 100.0;

        events.clear();
        midi.findAllTimeSigEvents (events);

        if (events.getNumEvents() > 0)
        {
            int num = 4, den = 4;
            events.getEventPointer (0)->message.getTimeSignatureInfo (num, den);

            if (num > 0 && den > 0)
            {
                info.timeSigNum = num;
                info.timeSigDen = den;
            }
        }

        struct Scan
        {
            juce::String name;
            int notes = 0;
            std::array<int, 17> perChannel {}, low {}, high {};   // índice = canal MIDI (1 a 16)
        };

        std::vector<Scan> scans;

        for (int t = 0; t < midi.getNumTracks(); ++t)
        {
            Scan s;
            s.low.fill (127);
            const auto& seq = *midi.getTrack (t);

            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                const auto& m = seq.getEventPointer (i)->message;

                if (m.isTrackNameEvent() && s.name.isEmpty())
                {
                    s.name = m.getTextFromTextMetaEvent().trim();
                }
                else if (m.isNoteOn())   // força 0 não conta: é nota desligada
                {
                    const auto c = (size_t) m.getChannel();
                    ++s.notes;
                    ++s.perChannel[c];
                    s.low[c] = juce::jmin (s.low[c], m.getNoteNumber());
                    s.high[c] = juce::jmax (s.high[c], m.getNoteNumber());
                }
            }

            scans.push_back (s);
        }

        const auto withNotes = std::count_if (scans.begin(), scans.end(), [] (const Scan& s) { return s.notes > 0; });

        for (int t = 0; t < (int) scans.size(); ++t)
        {
            const auto& s = scans[(size_t) t];

            if (s.notes == 0)
                continue;   // o maestro do tipo 1 (andamento, compasso) e as trilhas só com nome

            std::vector<int> channels;

            for (int c = 1; c <= 16; ++c)
                if (s.perChannel[(size_t) c] > 0)
                    channels.push_back (c);

            // uma trilha só com vários canais (o tipo 0): uma linha por canal
            if (withNotes == 1 && channels.size() > 1)
            {
                for (int c : channels)
                {
                    MidiFileInfo::Track line;
                    line.name = (s.name.isNotEmpty() ? s.name + ", " : juce::String())
                                + (c == 10 ? "bateria (canal 10)"_t : "canal MIDI "_t + juce::String (c));
                    line.fileTrack = t;
                    line.midiChannel = c;
                    line.notes = s.perChannel[(size_t) c];
                    line.lowKey = s.low[(size_t) c];
                    line.highKey = s.high[(size_t) c];
                    info.tracks.push_back (line);
                }

                continue;
            }

            MidiFileInfo::Track line;
            line.name = s.name.isNotEmpty() ? s.name : "Trilha "_t + juce::String (t + 1);
            line.fileTrack = t;
            line.notes = s.notes;

            for (int c : channels)
            {
                line.lowKey = juce::jmin (line.lowKey, s.low[(size_t) c]);
                line.highKey = juce::jmax (line.highKey, s.high[(size_t) c]);
            }

            info.tracks.push_back (line);
        }
    }

    /** Pitch bend e CC1 de uma trilha viram as curvas das notas dela (26/10). Em cada nota: o valor que valia no começo
        dela e cada evento enquanto ela soa. Eventos seguidos (até 2 × stepTicks de distância) são uma rampa; um evento
        depois de um tempo parado é um salto, e ganha um ponto um tick antes com o valor de antes. O alcance do pitch
        bend vem do RPN 0 da trilha (2 semitons se ela não diz). No fim, só os cantos ficam (curves::simplify). */
    template <typename ToTicks>
    void readCurves (const juce::MidiMessageSequence& seq, const ToTicks& toOurTicks, std::vector<Note>& notes)
    {
        struct Event { int t; float v; };
        std::vector<Event> bends, mods;
        int rpnMsb = 127, rpnLsb = 127;
        float range = 2.0f;

        for (int e = 0; e < seq.getNumEvents(); ++e)
        {
            const auto& m = seq.getEventPointer (e)->message;

            if (m.isPitchWheel())
            {
                bends.push_back ({ toOurTicks (m.getTimeStamp()), curves::fromWheel (m.getPitchWheelValue(), range) });
            }
            else if (m.isController())
            {
                const int number = m.getControllerNumber(), value = m.getControllerValue();

                if (number == 101)      rpnMsb = value;
                else if (number == 100) rpnLsb = value;
                else if (number == 6 && rpnMsb == 0 && rpnLsb == 0) range = (float) juce::jlimit (1, 24, value);
                else if (number == 1)   mods.push_back ({ toOurTicks (m.getTimeStamp()), value / 127.0f });
            }
        }

        // jump: a diferença a partir da qual um evento depois de um tempo parado é um salto (uma rampa lenta, com um passo
        // de CC ou de wheel a cada tanto, não é); tolerance: o quanto o simplify deixa passar (mais que um passo de CC)
        auto attach = [&notes] (const std::vector<Event>& events, curves::Kind kind, float jump, float tolerance)
        {
            if (events.empty())
                return;

            for (auto& n : notes)
            {
                // o último evento até o começo da nota (os eventos estão em ordem)
                auto first = std::upper_bound (events.begin(), events.end(), n.start, [] (int t, const Event& ev) { return t < ev.t; });
                float value = first == events.begin() ? 0.0f : std::prev (first)->v;
                auto e = (size_t) (first - events.begin());

                if (value == 0.0f && (e == events.size() || events[e].t >= n.end()))
                    continue;   // a nota não dobra

                std::vector<CurvePoint> pts { { 0, value } };
                int lastT = n.start;

                for (; e < events.size() && events[e].t < n.end(); ++e)
                {
                    const auto& ev = events[e];

                    if (ev.t - lastT > 2 * curves::stepTicks && std::abs (ev.v - value) > jump)
                        pts.push_back ({ ev.t - n.start - 1, value });   // um salto, não uma rampa

                    pts.push_back ({ ev.t - n.start, ev.v });
                    value = ev.v;
                    lastT = ev.t;
                }

                auto& curve = curves::of (n, kind);
                curve = curves::simplify (pts, tolerance);
                curves::tidy (curve, kind);
            }
        };

        attach (bends, curves::Kind::pitch, 0.1f, 0.01f);
        attach (mods, curves::Kind::mod, 0.02f, 0.006f);
    }

    /** As notas de uma linha da lista: a trilha inteira, ou só um canal dela. */
    juce::MidiMessageSequence sequenceOf (const juce::MidiFile& midi, const MidiFileInfo::Track& line)
    {
        const auto& track = *midi.getTrack (line.fileTrack);

        if (line.midiChannel == 0)
            return track;   // a cópia mantém os pares de nota ligada e desligada

        juce::MidiMessageSequence seq;
        track.extractMidiChannelMessages (line.midiChannel, seq, false);
        seq.updateMatchedPairs();
        return seq;
    }
}

int MidiFileInfo::totalNotes() const noexcept
{
    int total = 0;

    for (auto& t : tracks)
        total += t.notes;

    return total;
}

juce::Result readMidiInfo (const juce::File& file, MidiFileInfo& info)
{
    juce::MidiFile midi;
    int format = 1;

    if (auto r = load (file, midi, format); r.failed())
        return r;

    describe (midi, format, info);
    return juce::Result::ok();
}

juce::Result readMidiNotes (const juce::File& file, const MidiImportOptions& options, MidiImportResult& out)
{
    out = {};
    juce::MidiFile midi;
    int format = 1;

    if (auto r = load (file, midi, format); r.failed())
        return r;

    describe (midi, format, out.info);
    const auto& info = out.info;
    const int count = (int) info.tracks.size();

    if (count == 0)
        return juce::Result::fail (file.getFileName() + " não tem notas."_t);

    if (options.track < 0 || options.track > count)
        return juce::Result::fail ("O arquivo tem "_t + juce::String (count) + (count == 1 ? " trilha com notas" : " trilhas com notas")
                                   + ": escolha de 1 a "_t + juce::String (count) + ", ou 0 para todas."_t);

    // ticks do arquivo -> ticks da WHYNOT (960 por tempo); arquivo em SMPTE vai por segundos, no BPM do projeto
    if (info.ticksPerBeat == 0)
        midi.convertTimestampTicksToSeconds();

    auto toOurTicks = [&] (double t)
    {
        const double ticks = info.ticksPerBeat > 0 ? t * ticksPerBeat / info.ticksPerBeat : secondsToTicks (t, options.projectBpm);
        return juce::roundToInt (juce::jlimit (0.0, (double) maxTimelineTicks, ticks));   // arquivo estragado não estoura
    };

    std::vector<Note> notes;
    int first = std::numeric_limits<int>::max();

    for (int i = 0; i < count; ++i)
    {
        const auto seq = sequenceOf (midi, info.tracks[(size_t) i]);
        const bool wanted = options.track == 0 || options.track == i + 1;
        std::vector<Note> trackNotes;

        for (int e = 0; e < seq.getNumEvents(); ++e)
        {
            const auto& m = seq.getEventPointer (e)->message;

            if (! m.isNoteOn())
                continue;

            const int start = toOurTicks (m.getTimeStamp());
            first = juce::jmin (first, start);

            if (! wanted)
                continue;

            Note n;
            n.start = start;
            const double off = seq.getTimeOfMatchingKeyUp (e);
            n.length = off > m.getTimeStamp() ? juce::jmax (1, toOurTicks (off) - start) : ticksPerStep;
            n.key = juce::jlimit (0, 127, m.getNoteNumber());
            n.velocity = juce::jlimit (1, 127, (int) m.getVelocity());
            trackNotes.push_back (n);
        }

        // o pitch bend e o CC1 da trilha viram as curvas das notas dela (26/10)
        readCurves (seq, toOurTicks, trackNotes);
        notes.insert (notes.end(), trackNotes.begin(), trackNotes.end());
    }

    // o arquivo que começa depois de compassos vazios (comum ao exportar de outro app) vem para o começo
    const int barTicks = juce::jmax (1, juce::roundToInt (ticksPerBeat * 4.0 * info.timeSigNum / info.timeSigDen));
    const int shift = first == std::numeric_limits<int>::max() ? 0 : first / barTicks * barTicks;
    const int at = juce::jmax (0, options.at);

    // duas notas na mesma tecla e no mesmo tick (trilhas que dobram a mesma nota): fica a mais longa
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
    {
        return a.start != b.start ? a.start < b.start : a.key != b.key ? a.key < b.key : a.length > b.length;
    });

    notes.erase (std::unique (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start == b.start && a.key == b.key; }),
                 notes.end());

    for (auto n : notes)
    {
        n.start = n.start - shift + at;

        if (n.start >= options.limitTicks || (int) out.notes.size() >= options.maxNotes)
        {
            ++out.dropped;
            continue;
        }

        n.length = juce::jmin (n.length, options.limitTicks - n.start);
        out.notes.push_back (n);
    }

    return juce::Result::ok();
}

} // namespace whynot
