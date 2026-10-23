#include "BeatKey.h"

#include "../common/Text.h"
#include "../ideas/Theory.h"

namespace whynot
{

namespace ix = ideas;

namespace
{
    juce::String utf8 (const std::string& s)   { return juce::String::fromUTF8 (s.data(), (int) s.size()); }

    juce::String percent (double p)            { return juce::String (juce::roundToInt (juce::jlimit (0.0, 1.0, p) * 100.0)) + "%"; }

    double round2 (double v)                   { return std::round (v * 100.0) / 100.0; }

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    /** O compasso em ticks, igual ao do piano roll (6/8 = 12 passos). */
    int barTicksOf (const Project& p) noexcept
    {
        return ticksPerStep * juce::jmax (1, 16 / juce::jmax (1, p.timeSigDen)) * juce::jlimit (1, 16, p.timeSigNum);
    }

    /** Por que uma alternativa é parecida com o tom achado, em uma frase. */
    juce::String explainRelation (const ix::KeyCandidate& best, const ix::KeyCandidate& alt)
    {
        const auto relation = ix::keyRelation (best, alt);

        if (relation == "relativo")
            return keyName (alt) + " é o relativo de "_t + keyName (best) + ": as mesmas 7 notas. A escala do piano roll e "
                   "as dicas do 808 servem para os dois; muda só qual nota soa como casa."_t;

        if (relation == "paralelo")
            return "Mesma tônica, outra escala: a terça muda."_t;

        if (relation == "vizinho")
            return "Divide 6 das 7 notas com "_t + keyName (best) + "."_t;

        return {};
    }

    juce::var candidateVar (const ix::KeyCandidate& k, const ix::KeyCandidate* best)
    {
        auto v = object ({ { "root", utf8 (ix::pitchName (k.root, false)) },
                           { "rootIndex", k.root },
                           { "scale", k.major ? "major" : "minor" },
                           { "name", keyName (k) },
                           { "confidence", round2 (k.probability) } });

        if (best != nullptr)
        {
            v.getDynamicObject()->setProperty ("relation", utf8 (ix::keyRelation (*best, k)));
            v.getDynamicObject()->setProperty ("explain", explainRelation (*best, k));
        }

        return v;
    }
}

//==============================================================================
BeatKeyAnalyzer::BeatKeyAnalyzer() : juce::Thread ("WHYNOT tom do beat")
{
    startThread (juce::Thread::Priority::low);
}

BeatKeyAnalyzer::~BeatKeyAnalyzer()
{
    cancel = true;
    signalThreadShouldExit();
    notify();
    stopThread (5000);
    cancelPendingUpdate();
}

juce::String BeatKeyAnalyzer::stampOf (const juce::File& f)
{
    return juce::String (f.getLastModificationTime().toMilliseconds()) + "/" + juce::String (f.getSize());
}

void BeatKeyAnalyzer::request (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    const auto path = file.getFullPathName();
    const auto stamp = stampOf (file);
    const juce::ScopedLock sl (lock);

    if (pending.contains (path))
        return;

    if (auto it = results.find (path); it != results.end() && it->second->stamp == stamp)
        return;

    pending.add (path);
    queue.push_back (file);
    notify();
}

std::shared_ptr<const BeatKeyAnalyzer::Result> BeatKeyAnalyzer::find (const juce::File& file) const
{
    const juce::ScopedLock sl (lock);
    auto it = results.find (file.getFullPathName());
    return it != results.end() ? it->second : nullptr;
}

bool BeatKeyAnalyzer::isPending (const juce::File& file) const
{
    const juce::ScopedLock sl (lock);
    return pending.contains (file.getFullPathName());
}

bool BeatKeyAnalyzer::isIdle() const
{
    const juce::ScopedLock sl (lock);
    return pending.isEmpty();
}

std::shared_ptr<const BeatKeyAnalyzer::Result> BeatKeyAnalyzer::waitFor (const juce::File& file, int timeoutMs)
{
    request (file);
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);

    while (isPending (file) && juce::Time::getMillisecondCounter() < deadline)
        done.wait (50);

    return isPending (file) ? nullptr : find (file);
}

void BeatKeyAnalyzer::finishAll (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);

    while (! isIdle() && juce::Time::getMillisecondCounter() < deadline)
        done.wait (50);

    handleUpdateNowIfNeeded();
}

void BeatKeyAnalyzer::run()
{
    while (! threadShouldExit())
    {
        juce::File next;

        {
            const juce::ScopedLock sl (lock);

            if (! queue.empty())
            {
                next = queue.front();
                queue.pop_front();
            }
        }

        if (next == juce::File())
        {
            wait (-1);
            continue;
        }

        auto result = analyse (next);

        {
            const juce::ScopedLock sl (lock);

            if (! cancel)
                results[next.getFullPathName()] = result;

            pending.removeString (next.getFullPathName());
            finished.push_back (next);
        }

        done.signal();
        triggerAsyncUpdate();
    }
}

void BeatKeyAnalyzer::handleAsyncUpdate()
{
    std::vector<juce::File> files;

    {
        const juce::ScopedLock sl (lock);
        files.swap (finished);
    }

    for (auto& f : files)
        if (onAnalysed)
            onAnalysed (f);
}

std::shared_ptr<BeatKeyAnalyzer::Result> BeatKeyAnalyzer::analyse (const juce::File& file)
{
    auto r = std::make_shared<Result>();
    r->file = file;
    r->stamp = stampOf (file);
    const auto started = juce::Time::getMillisecondCounterHiRes();

    // um gerenciador por análise: esta thread não divide o leitor com ninguém (WAV, AIFF, FLAC, OGG e MP3)
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 1000.0 || reader->lengthInSamples <= 0)
    {
        r->error = "Não deu para ler esse arquivo de áudio: "_t + file.getFileName();
        return r;
    }

    r->fileSeconds = (double) reader->lengthInSamples / reader->sampleRate;
    const auto total = juce::jmin (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    constexpr int chunk = 32768;

    // lê em pedaços e já desce a taxa: 2 min 30 s ficam em ~6 MB, em vez de 50 MB do arquivo inteiro
    juce::AudioBuffer<float> buffer (channels, chunk);
    std::vector<float> mono ((size_t) chunk);
    ix::Downsampler down (reader->sampleRate);

    for (juce::int64 pos = 0; pos < total; pos += chunk)
    {
        if (cancel || threadShouldExit())
            return r;

        const int n = (int) juce::jmin ((juce::int64) chunk, total - pos);
        reader->read (&buffer, 0, n, pos, true, channels > 1);

        for (int i = 0; i < n; ++i)
            mono[(size_t) i] = channels > 1 ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)) : buffer.getSample (0, i);

        down.push (mono.data(), (size_t) n);
    }

    down.finish();
    r->analysis = ix::analyzeDownsampled (std::move (down.samples()), down.outputRate(), 0.0,
                                          (double) total / reader->sampleRate, &cancel);
    r->ok = ! cancel;
    r->millis = juce::Time::getMillisecondCounterHiRes() - started;
    return r;
}

double BeatKeyAnalyzer::fileSeconds (const juce::File& file)
{
    const auto path = file.getFullPathName();

    if (auto it = durations.find (path); it != durations.end())
        return it->second;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    const double seconds = reader != nullptr && reader->sampleRate > 0.0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
    durations[path] = seconds;
    return seconds;
}

std::vector<BeatSource> BeatKeyAnalyzer::beatsOf (const ProjectModel& model)
{
    const auto& p = model.get();
    std::vector<BeatSource> out;

    for (auto& ch : p.channels)
    {
        const bool audio = ch.isAudio();

        if (ch.samplePath.isEmpty() || (! audio && (ch.kind != "sampler" || ch.instrument.isNotEmpty())))
            continue;

        BeatSource s;
        s.channel = ch.id;
        s.name = ch.name;
        s.file = model.resolveSample (ch.samplePath);
        s.fileSeconds = fileSeconds (s.file);

        // sampler: só sample longo é beat (um kick, um 808 ou uma nota não são)
        if (s.fileSeconds <= 0.0 || (! audio && s.fileSeconds < longSampleSeconds))
            continue;

        const PlaylistItem* first = nullptr;

        for (auto& item : p.playlistItems)
            if (item.kind == "audio" && item.ref == ch.id && (first == nullptr || item.start < first->start))
                first = &item;

        if (first != nullptr)
        {
            s.inPlaylist = true;
            s.startTicks = first->start;
            s.lengthTicks = first->length;
            s.offsetSeconds = first->offsetSeconds;
            s.stretch = first->stretch;
        }
        else
        {
            s.lengthTicks = juce::roundToInt (secondsToTicks (s.fileSeconds, p.bpm));
        }

        out.push_back (s);
    }

    // o principal: o que está na playlist e, entre eles, o arquivo mais longo
    std::stable_sort (out.begin(), out.end(), [] (const BeatSource& a, const BeatSource& b)
    {
        if (a.inPlaylist != b.inPlaylist)
            return a.inPlaylist;

        return a.fileSeconds > b.fileSeconds;
    });

    return out;
}

//==============================================================================
juce::String keyName (const ix::KeyCandidate& k)
{
    return utf8 (ix::keyNamePt (k.root, k.major));
}

juce::String beatKeyText (const ix::BeatAnalysis& a)
{
    if (! a.found())
        return "Tom do beat: sem nota clara (só bateria?)"_t;

    const auto& best = a.best();
    return "Tom do beat: "_t + (best.probability < 0.5 ? "talvez "_t : juce::String()) + keyName (best) + " (" + percent (best.probability) + ")";
}

juce::String beatKeyDetails (const ix::BeatAnalysis& a)
{
    if (! a.found())
        return "Quase não há notas no beat (só bateria, ou silêncio): o tom não dá para ouvir."_t;

    const auto& best = a.best();
    juce::String text = keyName (best) + " (" + percent (best.probability) + ")";

    if (std::abs (a.tuningCents) >= 10.0)
        text << ", afinado " << (a.tuningCents > 0 ? "+" : "") << juce::String (juce::roundToInt (a.tuningCents)) << " cents fora de 440";

    text << ".";

    if (const auto* rel = a.find (ix::relativeOf (best).root, ! best.major))
        text << " " << explainRelation (best, *rel);

    text << " Outras possibilidades: "_t;

    for (int i = 1; i <= 3 && i < (int) a.keys.size(); ++i)
        text << (i > 1 ? ", " : "") << keyName (a.keys[(size_t) i]) << " (" << percent (a.keys[(size_t) i].probability) << ")";

    return text + ".";
}

std::vector<ix::BarChord> songBarChords (const BeatKeyAnalyzer::Result& r, const Project& p, const BeatSource& s)
{
    if (! r.ok || ! r.analysis.found())
        return {};

    const int barTicks = barTicksOf (p);
    const int firstBar = s.startTicks / barTicks;
    const int endTicks = s.startTicks + juce::jmax (barTicks, s.lengthTicks);

    // até onde a análise ouviu (os 2 min 30 s do começo do arquivo)
    int lastBar = (endTicks + barTicks - 1) / barTicks;

    for (int b = firstBar; b < lastBar; ++b)
        if (s.fileTimeAt ((double) b * barTicks, p.bpm) >= r.analysis.seconds)
        {
            lastBar = b;
            break;
        }

    lastBar = juce::jmin (lastBar, firstBar + 128);
    std::vector<double> starts;

    for (int b = firstBar; b <= lastBar; ++b)
        starts.push_back (s.fileTimeAt ((double) b * barTicks, p.bpm));

    auto chords = ix::chordsForBars (r.analysis, starts, &r.analysis.best());

    for (auto& c : chords)
        c.bar += firstBar;

    return chords;
}

juce::var describeBeatKey (const BeatKeyAnalyzer::Result& r, const Project& p, const BeatSource& s)
{
    const auto& a = r.analysis;
    auto o = object ({ { "state", a.found() ? "ready" : "noKey" },
                       { "file", r.file.getFullPathName() },
                       { "channel", s.channel },
                       { "fileSeconds", round2 (r.fileSeconds) },
                       { "analyzedSeconds", round2 (a.seconds) },
                       { "tuningCents", std::round (a.tuningCents * 10.0) / 10.0 },
                       { "tonalness", round2 (a.tonalness) },
                       { "text", beatKeyText (a) } });
    auto* d = o.getDynamicObject();

    if (! a.found())
        return o;

    const auto& best = a.best();
    d->setProperty ("root", utf8 (ix::pitchName (best.root, false)));
    d->setProperty ("rootIndex", best.root);
    d->setProperty ("scale", best.major ? "major" : "minor");
    d->setProperty ("name", keyName (best));
    d->setProperty ("confidence", round2 (best.probability));
    d->setProperty ("setKey", object ({ { "root", best.root }, { "scale", best.major ? "major" : "minor" } }));

    const auto relative = ix::relativeOf (best);
    const auto* rel = a.find (relative.root, relative.major);
    d->setProperty ("notesConfidence", round2 (best.probability + (rel != nullptr ? rel->probability : 0.0)));

    if (rel != nullptr)
        d->setProperty ("relative", candidateVar (*rel, &best));

    juce::Array<juce::var> alternatives;

    for (int i = 1; i <= 3 && i < (int) a.keys.size(); ++i)
        alternatives.add (candidateVar (a.keys[(size_t) i], &best));

    d->setProperty ("alternatives", alternatives);

    const int barTicks = barTicksOf (p);
    const bool flats = ix::keyPrefersFlats (best.root, best.major);
    juce::Array<juce::var> bars;

    for (auto& c : songBarChords (r, p, s))
    {
        auto bar = object ({ { "bar", c.bar + 1 }, { "start", c.bar * barTicks }, { "found", c.found } });

        if (c.found)
        {
            auto* b = bar.getDynamicObject();
            b->setProperty ("chord", utf8 (ix::chordName (c.root, c.minor, flats)));
            b->setProperty ("root", utf8 (ix::pitchName (c.root, false)));
            b->setProperty ("rootIndex", c.root);
            b->setProperty ("quality", c.minor ? "minor" : "major");
            b->setProperty ("confidence", round2 (c.confidence));
        }

        bars.add (bar);
    }

    d->setProperty ("bars", bars);
    d->setProperty ("explain", "O 808 casa na fundamental do acorde de cada compasso (root). A confiança é uma estimativa: "
                               "confira de ouvido, principalmente abaixo de 60%."_t);
    return o;
}

std::vector<BeatHint> beatHintsForPattern (BeatKeyAnalyzer& analyzer, const ProjectModel& model, const juce::String& patternId)
{
    const auto& p = model.get();
    const auto* pattern = p.findPattern (patternId);
    const auto beats = analyzer.beatsOf (model);

    if (pattern == nullptr || beats.empty())
        return {};

    const auto& beat = beats.front();
    const auto result = analyzer.find (beat.file);

    if (result == nullptr || ! result->ok || ! result->analysis.found())
        return {};

    // onde o padrão toca na música: o primeiro bloco dele; sem bloco, junto com o beat
    double songStart = beat.startTicks;
    const PlaylistItem* first = nullptr;

    for (auto& item : p.playlistItems)
        if (item.kind == "pattern" && item.ref == patternId && (first == nullptr || item.start < first->start))
            first = &item;

    if (first != nullptr)
        songStart = first->start - first->patternOffset;

    const int barTicks = barTicksOf (p);
    const int bars = juce::jlimit (1, 128, (pattern->lengthTicks() + barTicks - 1) / barTicks);
    std::vector<double> starts;

    for (int b = 0; b <= bars; ++b)
        starts.push_back (beat.fileTimeAt (songStart + (double) b * barTicks, p.bpm));

    std::vector<BeatHint> hints;
    const auto chords = ix::chordsForBars (result->analysis, starts, &result->analysis.best());

    for (auto& c : chords)
        if (c.found)
            hints.push_back ({ c.bar * barTicks, (c.bar + 1) * barTicks,
                               (int) std::floor ((songStart + (double) c.bar * barTicks) / barTicks), c.root, c.minor, c.confidence });

    return hints;
}

} // namespace whynot
