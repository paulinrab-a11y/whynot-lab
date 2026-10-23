#include "ProjectLibrary.h"

#include "../common/Text.h"
#include "../dsp/LoudnessMeter.h"

#include <thread>

namespace whynot::library
{

namespace
{
    juce::var entryToVar (const ProjectEntry& e)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("path", e.file.getFullPathName());
        o->setProperty ("size", e.size);
        o->setProperty ("modified", e.modified.toMilliseconds());
        o->setProperty ("hash", e.hash);
        o->setProperty ("name", e.name);
        o->setProperty ("bpm", e.bpm);
        o->setProperty ("timeSig", juce::Array<juce::var> { e.timeSigNum, e.timeSigDen });
        o->setProperty ("keyRoot", e.keyRoot);
        o->setProperty ("scale", e.scale);
        o->setProperty ("hasSong", e.hasSong);
        return juce::var (o);
    }

    ProjectEntry entryFromVar (const juce::var& v)
    {
        ProjectEntry e;
        e.file = juce::File (v.getProperty ("path", {}).toString());
        e.size = (juce::int64) v.getProperty ("size", 0);
        e.modified = juce::Time ((juce::int64) v.getProperty ("modified", 0));
        e.hash = v.getProperty ("hash", {}).toString();
        e.name = v.getProperty ("name", e.file.getFileNameWithoutExtension()).toString();
        e.bpm = (double) v.getProperty ("bpm", 0.0);

        if (auto* ts = v.getProperty ("timeSig", {}).getArray(); ts != nullptr && ts->size() == 2)
        {
            e.timeSigNum = juce::jlimit (1, 32, (int) (*ts)[0]);
            e.timeSigDen = juce::jlimit (1, 32, (int) (*ts)[1]);
        }

        e.keyRoot = juce::jlimit (0, 11, (int) v.getProperty ("keyRoot", 0));
        e.scale = v.getProperty ("scale", "minor").toString();
        e.hasSong = (bool) v.getProperty ("hasSong", false);
        return e;
    }

    bool newerFirst (const ProjectLibrary::Item& a, const ProjectLibrary::Item& b)
    {
        if (a.entry.modified != b.entry.modified)
            return a.entry.modified > b.entry.modified;

        return a.entry.file.getFullPathName().compareNatural (b.entry.file.getFullPathName()) < 0;
    }
}

//==============================================================================
/** A varredura das pastas, numa thread de trabalho: só lê o que mudou desde a última vez. */
struct ProjectLibrary::ScanJob
{
    juce::Array<juce::File> roots;
    juce::StringArray recent;
    std::map<juce::String, ProjectEntry> known;

    std::vector<Item> result;
    std::map<juce::String, ProjectEntry> entries;
    std::thread thread;
    std::atomic<bool> done { false };

    void run()
    {
        auto files = findProjects (roots);

        for (auto& path : recent)
            if (const juce::File f (path); f.existsAsFile() && ! files.contains (f))
                files.add (f);

        for (auto& f : files)
        {
            const auto key = f.getFullPathName();
            ProjectEntry e;

            if (auto it = known.find (key); it != known.end() && it->second.error.isEmpty()
                                            && it->second.size == f.getSize() && it->second.modified == f.getLastModificationTime())
                e = it->second;
            else
                e = readEntry (f);

            entries[key] = e;
            Item item;
            item.entry = e;
            item.preview = readPreviewInfo (f);
            item.recent = recent.contains (key);
            result.push_back (std::move (item));
        }

        std::sort (result.begin(), result.end(), newerFirst);
        done.store (true);
    }
};

//==============================================================================
/** O LUFS integrado de uma referência externa (WAV, MP3...), numa thread: decodificar 3 min leva ~1 s. */
struct ProjectLibrary::Measure
{
    int deck = 0;
    juce::File file;
    float lufs = -100.0f;
    std::thread thread;
    std::atomic<bool> done { false };

    void run()
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        if (std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file)); reader != nullptr && reader->sampleRate > 0.0)
        {
            dsp::LoudnessMeter meter;
            meter.prepare (reader->sampleRate);
            juce::AudioBuffer<float> buffer (2, 16384);

            for (juce::int64 pos = 0; pos < reader->lengthInSamples;)
            {
                const int n = (int) juce::jmin ((juce::int64) buffer.getNumSamples(), reader->lengthInSamples - pos);

                if (! reader->read (&buffer, 0, n, pos, true, true))
                    break;

                meter.process (buffer.getReadPointer (0), buffer.getReadPointer (1), n);
                pos += n;
            }

            lufs = meter.integrated();
        }

        done.store (true);
    }
};

//==============================================================================
ProjectLibrary::ProjectLibrary (Host h)
    : host (std::move (h)),
      decks (formats),
      renderer ({ host.engine, host.cache, host.catalog,
                  [this] { return (host.isPlaying != nullptr && host.isPlaying()) || (host.isExporting != nullptr && host.isExporting()); },
                  [this] (const juce::File& flac)
                  {
                      // a prévia que está num deck vai ser trocada: depois ela volta no mesmo ponto
                      bool inDeck = false;

                      for (int d = 0; d < DeckPlayer::numDecks; ++d)
                      {
                          if (decks.fileOf (d) == flac)
                          {
                              reloadAt[(size_t) d] = decks.positionSeconds (d);
                              inDeck = true;
                          }
                      }

                      if (inDeck)
                          reloadPlaying = decks.isPlaying();

                      decks.release (flac);
                  } })
{
    formats.registerBasicFormats();

    renderer.onFinished = [this] (const juce::File& wnl, bool ok)
    {
        reread (wnl);
        bool reloaded = false;

        for (int d = 0; d < DeckPlayer::numDecks; ++d)
        {
            auto& deck = info[(size_t) d];

            if (deck.project != wnl || reloadAt[(size_t) d] < 0.0)
                continue;

            // a prévia nova pode ter outra duração e outro LUFS
            const auto preview = readPreviewInfo (wnl);
            deck.lufs = preview.lufs;
            deck.lufsKnown = preview.valid;

            // sem loop primeiro: o loop precisa da duração do arquivo novo, que só se sabe com ele no deck
            if (ok && decks.load (d, deck.audio, reloadAt[(size_t) d]))
            {
                if (loopBars > 0)
                    decks.setLoop (d, loopFor (d, reloadAt[(size_t) d]));
            }
            else
            {
                deck = {};
            }

            reloadAt[(size_t) d] = -1.0;
            reloaded = true;
        }

        if (reloaded)
        {
            applyGains();

            if (reloadPlaying)
                decks.play();
        }
    };

    loadIndex();
    loadCombos();

    if (host.devices != nullptr)
        host.devices->addAudioCallback (&decks);

    startTimerHz (10);
}

ProjectLibrary::~ProjectLibrary()
{
    stopTimer();

    if (scan != nullptr && scan->thread.joinable())
        scan->thread.join();

    if (measuring != nullptr && measuring->thread.joinable())
        measuring->thread.join();

    decks.pause();

    if (host.devices != nullptr)
        host.devices->removeAudioCallback (&decks);
}

//==============================================================================
void ProjectLibrary::setRoots (const juce::Array<juce::File>& r)
{
    roots = r;
}

void ProjectLibrary::refresh()
{
    if (scan != nullptr)
    {
        rescan = true;
        return;
    }

    scan = std::make_unique<ScanJob>();
    scan->roots = scanRoots();
    scan->recent = recent;
    scan->known = known;
    auto* raw = scan.get();
    scan->thread = std::thread ([raw] { raw->run(); });
}

void ProjectLibrary::finishScan()
{
    if (scan->thread.joinable())
        scan->thread.join();

    auto done = std::move (scan);
    list = std::move (done->result);
    known = std::move (done->entries);
    updateStates();
    rebuildRows();
    saveIndex();
    sendChangeMessage();

    if (std::exchange (rescan, false))
        refresh();
}

const ProjectLibrary::Item* ProjectLibrary::find (const juce::File& f) const
{
    for (auto& item : list)
        if (item.entry.file == f)
            return &item;

    return nullptr;
}

void ProjectLibrary::addRecent (const juce::File& f)
{
    const auto path = f.getFullPathName();
    recent.removeString (path);
    recent.insert (0, path);

    while (recent.size() > maxRecent)
        recent.remove (recent.size() - 1);

    for (auto& item : list)
        item.recent = recent.contains (item.entry.file.getFullPathName());
}

void ProjectLibrary::reread (const juce::File& f)
{
    if (! f.existsAsFile())
        return;

    Item fresh;
    fresh.entry = readEntry (f);
    fresh.preview = readPreviewInfo (f);
    fresh.recent = recent.contains (f.getFullPathName());
    known[f.getFullPathName()] = fresh.entry;

    list.erase (std::remove_if (list.begin(), list.end(), [&f] (const Item& i) { return i.entry.file == f; }), list.end());
    list.insert (std::upper_bound (list.begin(), list.end(), fresh, newerFirst), std::move (fresh));
    updateStates();
    rebuildRows();
    saveIndex();
    sendChangeMessage();
}

void ProjectLibrary::noteOpened (const juce::File& f)
{
    addRecent (f);
    reread (f);
}

void ProjectLibrary::projectSaved (const juce::File& f)
{
    addRecent (f);
    reread (f);

    if (renderOnSave)
    {
        renderer.enqueue (f, true);
        updateStates();   // a linha já mostra "na fila" ou "renderizando"
        sendChangeMessage();
    }
}

//==============================================================================
void ProjectLibrary::renderPreview (const juce::File& wnl, bool urgent)
{
    renderer.enqueue (wnl, urgent);
    updateStates();
    sendChangeMessage();
}

int ProjectLibrary::renderPending()
{
    int n = 0;

    for (auto& item : list)
    {
        if (item.entry.error.isEmpty()
            && (item.state == PreviewState::none || item.state == PreviewState::stale || item.state == PreviewState::failed))
        {
            renderer.enqueue (item.entry.file, false);
            ++n;
        }
    }

    updateStates();
    sendChangeMessage();
    return n;
}

juce::String ProjectLibrary::renderStatusText() const
{
    float progress = 0.0f;
    const auto now = renderer.renderingNow (&progress);
    const int queued = renderer.numQueued();
    juce::String text;

    if (now != juce::File())
    {
        const auto* item = find (now);
        text = "Renderizando a prévia de "_t + (item != nullptr ? item->entry.name : now.getFileNameWithoutExtension())
             + " (" + juce::String (juce::roundToInt (progress * 100.0f)) + "%)";
    }

    if (queued > 0)
        text << (text.isEmpty() ? juce::String() : juce::String (". ")) << "Na fila: " << queued;

    if (const auto why = renderer.waitingReason(); why.isNotEmpty())
        text << (text.isEmpty() ? juce::String() : juce::String (". ")) << why;

    return text;
}

void ProjectLibrary::updateStates()
{
    for (auto& item : list)
    {
        float progress = 0.0f;
        juce::String error;
        auto state = renderer.stateOf (item.entry.file, &progress, &error);

        if (state == PreviewState::none)
            state = diskState (item.entry, item.preview);

        item.state = state;
        item.progress = progress;
        item.error = error;
    }
}

//==============================================================================
juce::String ProjectLibrary::syncName (Sync s)
{
    return s == Sync::second ? "second" : (s == Sync::free ? "free" : "bar");
}

double ProjectLibrary::mapPosition (int from, int to, double seconds) const
{
    const auto& a = info[(size_t) from];
    const auto& b = info[(size_t) to];
    double p = juce::jmax (0.0, seconds);

    // mesmo compasso: o mesmo compasso e o mesmo ponto dentro dele, no andamento de cada um
    if (sync == Sync::bar && a.barSeconds > 0.0 && b.barSeconds > 0.0)
        p = p / a.barSeconds * b.barSeconds;

    // a música do outro é mais curta: continua do começo dela
    if (const double length = decks.lengthSeconds (to); length > 0.0 && p >= length)
        p = std::fmod (p, length);

    return p;
}

DeckPlayer::Loop ProjectLibrary::loopFor (int deck, double seconds) const
{
    if (loopBars <= 0 || ! decks.isLoaded (deck))
        return {};

    // o compasso do deck; uma referência sem BPM usa o do outro deck (ou 2 s, 120 BPM em 4/4)
    double bar = info[(size_t) deck].barSeconds;

    if (bar <= 0.0)
        bar = info[(size_t) (1 - deck)].barSeconds;

    if (bar <= 0.0)
        bar = 2.0;

    const double start = std::floor (seconds / bar + 1.0e-6) * bar;
    const double end = juce::jmin (decks.lengthSeconds (deck), start + loopBars * bar);
    return end > start ? DeckPlayer::Loop { start, end } : DeckPlayer::Loop {};
}

void ProjectLibrary::loadAt (int deck, double seconds)
{
    if (! decks.isLoaded (deck))
        return;

    auto loop = decks.loopOf (deck);

    // fora do trecho do loop (um clique em outro ponto da onda): o loop vai junto para o compasso novo
    if (loopBars > 0 && (! loop.active() || seconds < loop.start || seconds >= loop.end))
        loop = loopFor (deck, seconds);

    decks.load (deck, decks.fileOf (deck), seconds, loop);
}

void ProjectLibrary::applyGains()
{
    // igualar pelo lado mais baixo: o mais alto desce até ele, nada sobe (sem risco de clipar)
    float target = 0.0f;
    bool any = false;

    for (auto& deck : info)
    {
        if (deck.loaded() && deck.lufsKnown && deck.lufs > -70.0f)
        {
            target = any ? juce::jmin (target, deck.lufs) : deck.lufs;
            any = true;
        }
    }

    for (int d = 0; d < DeckPlayer::numDecks; ++d)
    {
        float gain = d == active ? 1.0f : 0.0f;

        if (matchLoudness && any && info[(size_t) d].lufsKnown && info[(size_t) d].lufs > -70.0f)
            gain *= juce::Decibels::decibelsToGain (target - info[(size_t) d].lufs);

        decks.setDeckGain (d, gain);
    }
}

float ProjectLibrary::matchGainDb (int deck) const
{
    if (! matchLoudness || ! juce::isPositiveAndBelow (deck, DeckPlayer::numDecks) || ! info[(size_t) deck].lufsKnown)
        return 0.0f;

    float target = info[(size_t) deck].lufs;

    for (auto& d : info)
        if (d.loaded() && d.lufsKnown && d.lufs > -70.0f)
            target = juce::jmin (target, d.lufs);

    return target - info[(size_t) deck].lufs;
}

juce::Result ProjectLibrary::loadDeck (int deck, const juce::File& file, double fromSeconds)
{
    if (! juce::isPositiveAndBelow (deck, DeckPlayer::numDecks))
        return juce::Result::fail ("Deck inválido."_t);

    Deck d;

    if (file.hasFileExtension ("wnl"))
    {
        d.project = file;
        d.audio = previewAudio (file);

        if (! d.audio.existsAsFile())
            return juce::Result::fail ("Este projeto ainda não tem prévia. Renderize primeiro."_t);

        const auto* item = find (file);
        const auto entry = item != nullptr ? item->entry : readEntry (file);
        const auto preview = item != nullptr ? item->preview : readPreviewInfo (file);
        d.name = entry.name;
        d.bpm = entry.bpm;

        // um compasso em semínimas (4/4 = 4, 6/8 = 3), no andamento do projeto
        if (entry.bpm > 0.0 && entry.timeSigDen > 0)
            d.barSeconds = (double) entry.timeSigNum * 4.0 / entry.timeSigDen * 60.0 / entry.bpm;

        d.lufs = preview.lufs;
        d.lufsKnown = preview.valid;
    }
    else
    {
        // referência externa: sem BPM; o LUFS é medido numa thread
        d.audio = file;
        d.name = file.getFileNameWithoutExtension();

        if (! file.existsAsFile())
            return juce::Result::fail ("Arquivo não encontrado: "_t + file.getFullPathName());
    }

    // O arquivo entra no deck primeiro (o mapa de posição e o loop precisam da duração dele); a cena só vai
    // para o áudio no fim, já no ponto certo: com o outro deck tocando, alinhado com ele (pela sincronia).
    const int other = 1 - deck;
    const auto previous = info[(size_t) deck];
    info[(size_t) deck] = d;

    decks.beginChange();
    bool ok = decks.load (deck, d.audio, 0.0);
    double start = juce::jmax (0.0, fromSeconds);

    if (ok && fromSeconds < 0.0)
        start = sync != Sync::free && decks.isPlaying() && decks.isLoaded (other)
                    ? mapPosition (other, deck, decks.positionSeconds (other)) : 0.0;

    if (ok && (start > 0.0 || loopBars > 0))
        ok = decks.load (deck, d.audio, start, loopFor (deck, start));

    decks.endChange();

    if (! ok)
    {
        info[(size_t) deck] = previous;
        return juce::Result::fail ("Não consegui abrir "_t + d.audio.getFullPathName());
    }

    if (! d.lufsKnown && d.project == juce::File())
        startMeasure (deck, file);

    applyGains();
    rebuildRows();
    sendChangeMessage();
    return juce::Result::ok();
}

void ProjectLibrary::unloadDeck (int deck)
{
    if (! juce::isPositiveAndBelow (deck, DeckPlayer::numDecks))
        return;

    decks.unload (deck);
    info[(size_t) deck] = {};
    applyGains();
    rebuildRows();
    sendChangeMessage();
}

void ProjectLibrary::startMeasure (int deck, const juce::File& file)
{
    if (measuring != nullptr && measuring->thread.joinable())
        measuring->thread.join();   // uma medição por vez; a anterior termina em ~1 s

    measuring = std::make_unique<Measure>();
    measuring->deck = deck;
    measuring->file = file;
    auto* raw = measuring.get();
    measuring->thread = std::thread ([raw] { raw->run(); });
}

juce::Result ProjectLibrary::playPreview (const juce::File& wnl, double fromSeconds)
{
    const auto result = loadDeck (active, wnl, fromSeconds);

    if (result.failed())
        return result;

    if (host.isPlaying != nullptr && host.isPlaying() && host.stopLive != nullptr)
        host.stopLive();   // um som por vez: a música do projeto aberto para

    decks.play();
    sendChangeMessage();
    return juce::Result::ok();
}

void ProjectLibrary::setActiveDeck (int deck)
{
    if (! juce::isPositiveAndBelow (deck, DeckPlayer::numDecks) || deck == active)
        return;

    const int from = active;

    // Mesmo compasso com andamentos diferentes: os decks se afastam com o tempo, então o que vai soar
    // é reposicionado agora, ainda mudo (o salto não se ouve). Mesmo BPM ou mesmo segundo: eles já andam
    // juntos desde o play, amostra a amostra, e nada se mexe.
    if (sync == Sync::bar && decks.isPlaying() && decks.isLoaded (from) && decks.isLoaded (deck)
        && std::abs (info[(size_t) from].barSeconds - info[(size_t) deck].barSeconds) > 1.0e-6)
        loadAt (deck, mapPosition (from, deck, decks.positionSeconds (from)));

    active = deck;
    applyGains();
    sendChangeMessage();
}

void ProjectLibrary::setMatchLoudness (bool on)
{
    matchLoudness = on;
    applyGains();
    sendChangeMessage();
}

void ProjectLibrary::setSync (Sync s)
{
    sync = s;
    sendChangeMessage();
}

void ProjectLibrary::setLoopBars (int bars)
{
    loopBars = juce::jlimit (0, 64, bars);

    // os dois decks no mesmo bloco: o loop liga (ou desliga) a partir do compasso de cada um
    decks.beginChange();

    for (int d = 0; d < DeckPlayer::numDecks; ++d)
        if (decks.isLoaded (d))
            decks.setLoop (d, loopFor (d, decks.positionSeconds (d)));

    decks.endChange();
    sendChangeMessage();
}

void ProjectLibrary::toggleDeck()
{
    if (! decks.isLoaded (0) && ! decks.isLoaded (1))
        return;

    if (decks.isPlaying())
    {
        decks.pause();
    }
    else
    {
        if (host.isPlaying != nullptr && host.isPlaying() && host.stopLive != nullptr)
            host.stopLive();

        decks.play();
    }

    sendChangeMessage();
}

void ProjectLibrary::stopDeck()
{
    if (comboPlaying.isNotEmpty())
        stopCombo();

    decks.pause();
    decks.beginChange();

    for (int d = 0; d < DeckPlayer::numDecks; ++d)
        loadAt (d, 0.0);

    decks.endChange();
    sendChangeMessage();
}

void ProjectLibrary::seekDeck (int deck, double seconds)
{
    if (! decks.isLoaded (deck))
        return;

    seconds = juce::jlimit (0.0, juce::jmax (0.0, decks.lengthSeconds (deck) - 0.01), seconds);

    // os dois no mesmo bloco: o outro deck vai para o ponto correspondente (menos no modo livre)
    decks.beginChange();
    loadAt (deck, seconds);

    if (const int other = 1 - deck; sync != Sync::free && decks.isLoaded (other))
        loadAt (other, mapPosition (deck, other, seconds));

    decks.endChange();
}

//==============================================================================
void ProjectLibrary::timerCallback()
{
    decks.service();

    // a música do projeto aberto começou: o deck para (um som por vez)
    if (decks.isPlaying() && host.isPlaying != nullptr && host.isPlaying())
    {
        decks.pause();
        sendChangeMessage();
    }

    if (scan != nullptr && scan->done.load())
        finishScan();

    // combo tocando: o item do deck que soa acabou, entra o próximo
    if (comboPlaying.isNotEmpty() && decks.deckFinished (active))
        advanceCombo();

    if (measuring != nullptr && measuring->done.load())
    {
        measuring->thread.join();
        auto& deck = info[(size_t) measuring->deck];

        if (deck.audio == measuring->file && deck.project == juce::File())
        {
            deck.lufs = measuring->lufs;
            deck.lufsKnown = measuring->lufs > -70.0f;
            applyGains();
            sendChangeMessage();
        }

        measuring = nullptr;
    }

    // só avisa a tela quando o estado ou o andamento de alguma prévia mudou
    std::vector<std::pair<PreviewState, int>> before;
    before.reserve (list.size());

    for (auto& item : list)
        before.emplace_back (item.state, juce::roundToInt (item.progress * 100.0f));

    updateStates();

    for (size_t i = 0; i < list.size(); ++i)
    {
        if (before[i].first != list[i].state || before[i].second != juce::roundToInt (list[i].progress * 100.0f))
        {
            sendChangeMessage();
            break;
        }
    }
}

//==============================================================================
juce::String ProjectLibrary::stateName (PreviewState s)
{
    switch (s)
    {
        case PreviewState::ok:        return "ok";
        case PreviewState::stale:     return "stale";
        case PreviewState::queued:    return "queued";
        case PreviewState::rendering: return "rendering";
        case PreviewState::failed:    return "failed";
        case PreviewState::none:      break;
    }

    return "none";
}

//==============================================================================
juce::Array<juce::File> ProjectLibrary::scanRoots() const
{
    auto all = roots;

    for (auto& f : extraFolders)
        all.addIfNotAlreadyThere (f);

    return all;
}

void ProjectLibrary::addFolder (const juce::File& folder)
{
    if (! folder.isDirectory() || roots.contains (folder) || extraFolders.contains (folder))
        return;

    extraFolders.add (folder);
    saveIndex();
    refresh();
    sendChangeMessage();
}

void ProjectLibrary::removeFolder (const juce::File& folder)
{
    if (! extraFolders.contains (folder))
        return;

    extraFolders.removeFirstMatchingValue (folder);

    if (filter.view == Filter::View::folder && filter.folder == folder)
        filter = {};

    saveIndex();
    refresh();
    sendChangeMessage();
}

ProjectLibrary::Meta ProjectLibrary::metaOf (const juce::File& f) const
{
    const auto it = meta.find (f.getFullPathName());
    return it != meta.end() ? it->second : Meta {};
}

void ProjectLibrary::metaChanged()
{
    // tira as entradas vazias: o arquivo guarda só o que foi marcado
    for (auto it = meta.begin(); it != meta.end();)
        it = it->second.empty() ? meta.erase (it) : std::next (it);

    saveIndex();
    rebuildRows();
    sendChangeMessage();
}

void ProjectLibrary::setFavorite (const juce::File& f, bool on)
{
    meta[f.getFullPathName()].favorite = on;
    metaChanged();
}

void ProjectLibrary::setColour (const juce::File& f, const juce::String& hex)
{
    meta[f.getFullPathName()].colour = hex.trim();
    metaChanged();
}

void ProjectLibrary::setTags (const juce::File& f, const juce::StringArray& tags)
{
    juce::StringArray clean;

    for (auto tag : tags)
    {
        tag = tag.trim().trimCharactersAtStart ("#").toLowerCase();

        if (tag.isNotEmpty())
            clean.addIfNotAlreadyThere (tag);
    }

    meta[f.getFullPathName()].tags = clean;
    metaChanged();
}

void ProjectLibrary::setStatus (const juce::File& f, Status s)
{
    meta[f.getFullPathName()].status = s;
    metaChanged();
}

std::vector<std::pair<juce::String, int>> ProjectLibrary::allTags() const
{
    std::map<juce::String, int> counts;

    for (auto& item : list)
        for (auto& tag : metaOf (item.entry.file).tags)
            ++counts[tag];

    return { counts.begin(), counts.end() };
}

int ProjectLibrary::countWithStatus (Status s) const
{
    int n = 0;

    for (auto& item : list)
        n += metaOf (item.entry.file).status == s ? 1 : 0;

    return n;
}

int ProjectLibrary::countFavorites() const
{
    int n = 0;

    for (auto& item : list)
        n += metaOf (item.entry.file).favorite ? 1 : 0;

    return n;
}

juce::String ProjectLibrary::statusId (Status s)
{
    switch (s)
    {
        case Status::idea:       return "ideia";
        case Status::production: return "producao";
        case Status::mix:        return "mix";
        case Status::done:       return "pronto";
        case Status::none:       break;
    }

    return {};
}

juce::String ProjectLibrary::statusLabel (Status s)
{
    switch (s)
    {
        case Status::idea:       return "Ideia";
        case Status::production: return "Em produção"_t;
        case Status::mix:        return "Mix";
        case Status::done:       return "Pronto";
        case Status::none:       break;
    }

    return {};
}

ProjectLibrary::Status ProjectLibrary::statusFromText (const juce::String& text)
{
    const auto s = text.trim().toLowerCase();

    if (s == "ideia" || s == "idea")                                              return Status::idea;
    if (s == "producao" || s == "produção"_t || s == "em produção"_t || s == "production")   return Status::production;
    if (s == "mix" || s == "mixagem")                                             return Status::mix;
    if (s == "pronto" || s == "pronta" || s == "done")                            return Status::done;
    return Status::none;
}

juce::String ProjectLibrary::stackKey (const juce::File& f)
{
    // "Beat_3" → "Beat" (como o Salvar nova versão nomeia): as versões da mesma pasta ficam juntas
    auto base = f.getFileNameWithoutExtension();
    const auto suffix = base.fromLastOccurrenceOf ("_", false, false);

    if (base.containsChar ('_') && suffix.isNotEmpty() && suffix.containsOnly ("0123456789"))
        base = base.upToLastOccurrenceOf ("_", false, false);

    return (f.getParentDirectory().getFullPathName() + "|" + base).toLowerCase();
}

bool ProjectLibrary::matches (const Item& item, const Filter& f) const
{
    const auto& e = item.entry;
    const auto m = metaOf (e.file);

    switch (f.view)
    {
        case Filter::View::recent:    if (! item.recent) return false; break;
        case Filter::View::favorites: if (! m.favorite) return false; break;
        case Filter::View::status:    if (m.status != f.status) return false; break;
        case Filter::View::tag:       if (! m.tags.contains (f.tag, true)) return false; break;
        case Filter::View::folder:    if (! e.file.isAChildOf (f.folder)) return false; break;
        case Filter::View::combo:     break;   // a ordem e os itens vêm do combo (rebuildRows)
        case Filter::View::all:       break;
    }

    // a busca: cada palavra precisa bater (E). bpm:140-150 também acha 70-75 e 280-300 (o trap vive em
    // 140 e em 70); tom:Am; tag:x; status:mix; fav. O resto procura no nome e nas tags.
    for (auto token : juce::StringArray::fromTokens (f.query, " ", "\""))
    {
        token = token.trim().toLowerCase();

        if (token.isEmpty())
            continue;

        const bool hasKey = token.containsChar (':');
        const auto key = token.upToFirstOccurrenceOf (":", false, false);
        const auto value = token.fromFirstOccurrenceOf (":", false, false).trim();

        if (hasKey && key == "bpm")
        {
            double lo = value.upToFirstOccurrenceOf ("-", false, false).getDoubleValue();
            double hi = value.containsChar ('-') ? value.fromFirstOccurrenceOf ("-", false, false).getDoubleValue() : lo;

            if (! value.containsChar ('-'))
            {
                lo -= 1.0;
                hi += 1.0;
            }

            bool ok = false;

            if (e.bpm > 0.0)
                for (const double b : { e.bpm, e.bpm * 2.0, e.bpm / 2.0 })
                    ok = ok || (b >= juce::jmin (lo, hi) - 0.5 && b <= juce::jmax (lo, hi) + 0.5);

            if (! ok)
                return false;
        }
        else if (hasKey && (key == "tom" || key == "key"))
        {
            if (e.error.isNotEmpty() || e.keyName().toLowerCase() != value.replace ("min", "m"))
                return false;
        }
        else if (hasKey && key == "tag")
        {
            bool ok = false;

            for (auto& tag : m.tags)
                ok = ok || tag.contains (value);

            if (! ok)
                return false;
        }
        else if (hasKey && key == "status")
        {
            if (m.status == Status::none || m.status != statusFromText (value))
                return false;
        }
        else if (token == "fav" || token == "estrela" || key == "fav" || key == "estrela")
        {
            if (! m.favorite)
                return false;
        }
        else
        {
            bool ok = e.name.toLowerCase().contains (token);

            for (auto& tag : m.tags)
                ok = ok || tag.contains (token.trimCharactersAtStart ("#"));

            if (! ok)
                return false;
        }
    }

    return true;
}

void ProjectLibrary::setFilter (const Filter& f)
{
    filter = f;
    rebuildRows();
    sendChangeMessage();
}

const ProjectLibrary::Item& ProjectLibrary::itemFor (const Row& row) const
{
    if (row.extra >= 0 && row.extra < (int) extras.size())
        return extras[(size_t) row.extra];

    return list[(size_t) juce::jlimit (0, juce::jmax (0, (int) list.size() - 1), row.index)];
}

void ProjectLibrary::rebuildRows()
{
    visible.clear();

    // um combo: os itens na ordem dele, sem pilhas; o que não está na lista (uma referência, um projeto de
    // outra pasta) entra como item à parte
    if (filter.view == Filter::View::combo)
    {
        extras.clear();
        const auto* combo = findCombo (filter.combo);

        if (combo == nullptr)
            return;

        auto queryOnly = filter;
        queryOnly.view = Filter::View::all;

        for (int i = 0; i < (int) combo->items.size(); ++i)
        {
            const auto file = resolve (combo->items[(size_t) i]);
            Row row;
            row.comboIndex = i;

            for (int k = 0; k < (int) list.size() && row.index < 0; ++k)
                if (list[(size_t) k].entry.file == file)
                    row.index = k;

            if (row.index < 0)
            {
                Item x;

                if (file.hasFileExtension ("wnl") && file.existsAsFile())
                {
                    x.entry = readEntry (file);
                    x.preview = readPreviewInfo (file);
                    x.state = diskState (x.entry, x.preview);
                }
                else
                {
                    x.entry.file = file;
                    x.entry.name = file.getFileNameWithoutExtension();
                    x.reference = ! file.hasFileExtension ("wnl");
                    x.entry.modified = file.getLastModificationTime();
                }

                if (! file.existsAsFile())
                    x.entry.error = "Arquivo não encontrado"_t;

                extras.push_back (std::move (x));
                row.extra = (int) extras.size() - 1;
            }

            if (matches (itemFor (row), queryOnly))
                visible.push_back (row);
        }

        return;
    }

    // a lista já vem da mais nova para a mais velha: a primeira de cada pilha é a cabeça
    std::map<juce::String, std::vector<int>> stacks;
    std::vector<juce::String> order;

    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! matches (list[(size_t) i], filter))
            continue;

        const auto key = stackKey (list[(size_t) i].entry.file);
        auto& members = stacks[key];

        if (members.empty())
            order.push_back (key);

        members.push_back (i);
    }

    for (auto& key : order)
    {
        const auto& members = stacks[key];
        Row head;
        head.index = members.front();
        head.versions = (int) members.size();
        head.stack = key;
        // aberta pela pessoa, ou porque uma versão de baixo está num deck (o que toca nunca fica escondido)
        bool inDeck = false;

        for (size_t k = 1; k < members.size(); ++k)
            for (auto& deck : info)
                inDeck = inDeck || (deck.project != juce::File() && deck.project == list[(size_t) members[k]].entry.file);

        head.expanded = members.size() > 1 && (openStacks.count (key) > 0 || inDeck);
        visible.push_back (head);

        if (head.expanded)
        {
            for (size_t k = 1; k < members.size(); ++k)
            {
                Row child;
                child.index = members[k];
                child.child = true;
                child.stack = key;
                visible.push_back (child);
            }
        }
    }
}

void ProjectLibrary::toggleStack (const juce::String& key)
{
    if (! openStacks.erase (key))
        openStacks.insert (key);

    rebuildRows();
    sendChangeMessage();
}

juce::var ProjectLibrary::describeRows() const
{
    juce::Array<juce::var> out;

    for (auto& row : visible)
    {
        const auto& item = itemFor (row);
        const auto m = metaOf (item.entry.file);
        auto o = new juce::DynamicObject();
        o->setProperty ("path", item.entry.file.getFullPathName());
        o->setProperty ("name", item.entry.name);
        o->setProperty ("bpm", item.entry.bpm);
        o->setProperty ("key", item.entry.keyName());
        o->setProperty ("favorite", m.favorite);
        o->setProperty ("status", statusId (m.status));
        o->setProperty ("tags", m.tags.joinIntoString (","));
        o->setProperty ("versions", row.versions);
        o->setProperty ("child", row.child);
        o->setProperty ("preview", stateName (item.state));
        o->setProperty ("reference", item.reference);

        if (row.comboIndex >= 0)
        {
            if (const auto* combo = findCombo (filter.combo))
            {
                const auto& ci = combo->items[(size_t) row.comboIndex];
                o->setProperty ("comboIndex", row.comboIndex);
                o->setProperty ("note", ci.note);
                o->setProperty ("latest", ci.latest);
            }
        }
        out.add (juce::var (o));
    }

    auto root = new juce::DynamicObject();
    root->setProperty ("rows", out);
    root->setProperty ("count", (int) visible.size());
    return juce::var (root);
}

//==============================================================================
juce::File ProjectLibrary::comboFile (const juce::String& name) const
{
    return comboFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".wnlcombo");
}

const ProjectLibrary::Combo* ProjectLibrary::findCombo (const juce::String& name) const
{
    for (auto& c : comboList)
        if (c.name.equalsIgnoreCase (name.trim()))
            return &c;

    return nullptr;
}

ProjectLibrary::Combo* ProjectLibrary::comboNamed (const juce::String& name)
{
    return const_cast<Combo*> (findCombo (name));
}

void ProjectLibrary::loadCombos()
{
    comboList.clear();

    for (const auto& entry : juce::RangedDirectoryIterator (comboFolder(), false, "*.wnlcombo"))
    {
        const auto v = juce::JSON::parse (entry.getFile());

        if (! v.isObject() || v.getProperty ("format", {}).toString() != "whynot-combo")
            continue;

        Combo c;
        c.name = v.getProperty ("name", entry.getFile().getFileNameWithoutExtension()).toString();

        if (auto* items = v.getProperty ("items", {}).getArray())
        {
            for (auto& i : *items)
            {
                const auto path = i.getProperty ("path", {}).toString();

                if (juce::File::isAbsolutePath (path))
                    c.items.push_back ({ juce::File (path), (bool) i.getProperty ("latest", false), i.getProperty ("note", {}).toString() });
            }
        }

        if (c.name.isNotEmpty() && findCombo (c.name) == nullptr)
            comboList.push_back (std::move (c));
    }

    std::sort (comboList.begin(), comboList.end(), [] (const Combo& a, const Combo& b) { return a.name.compareNatural (b.name) < 0; });
}

juce::Result ProjectLibrary::saveCombo (const Combo& c) const
{
    auto o = new juce::DynamicObject();
    juce::var root (o);
    o->setProperty ("format", "whynot-combo");
    o->setProperty ("version", 1);
    o->setProperty ("name", c.name);

    juce::Array<juce::var> items;

    for (auto& i : c.items)
    {
        auto e = new juce::DynamicObject();
        e->setProperty ("path", i.file.getFullPathName());
        e->setProperty ("latest", i.latest);
        e->setProperty ("note", i.note);
        items.add (juce::var (e));
    }

    o->setProperty ("items", items);

    if (! comboFolder().createDirectory())
        return juce::Result::fail ("Não consegui criar a pasta "_t + comboFolder().getFullPathName());

    if (! comboFile (c.name).replaceWithText (juce::JSON::toString (root)))
        return juce::Result::fail ("Não consegui gravar o combo "_t + c.name);

    return juce::Result::ok();
}

void ProjectLibrary::combosChanged (const Combo& c)
{
    saveCombo (c);
    rebuildRows();
    sendChangeMessage();
}

juce::Result ProjectLibrary::createCombo (const juce::String& name)
{
    const auto clean = name.trim();

    if (clean.isEmpty())
        return juce::Result::fail ("Dê um nome ao combo."_t);

    if (findCombo (clean) != nullptr)
        return juce::Result::fail ("Já existe um combo chamado "_t + clean);

    Combo c;
    c.name = clean;
    const auto saved = saveCombo (c);

    if (saved.failed())
        return saved;

    comboList.push_back (c);
    std::sort (comboList.begin(), comboList.end(), [] (const Combo& a, const Combo& b) { return a.name.compareNatural (b.name) < 0; });
    rebuildRows();
    sendChangeMessage();
    return juce::Result::ok();
}

juce::Result ProjectLibrary::renameCombo (const juce::String& from, const juce::String& to)
{
    auto* c = comboNamed (from);
    const auto clean = to.trim();

    if (c == nullptr)
        return juce::Result::fail ("Combo não encontrado: "_t + from);

    if (clean.isEmpty())
        return juce::Result::fail ("Dê um nome ao combo."_t);

    if (! clean.equalsIgnoreCase (c->name) && findCombo (clean) != nullptr)
        return juce::Result::fail ("Já existe um combo chamado "_t + clean);

    const auto oldFile = comboFile (c->name);
    const auto oldName = c->name;
    c->name = clean;
    const auto saved = saveCombo (*c);

    if (saved.failed())
    {
        c->name = oldName;
        return saved;
    }

    if (oldFile != comboFile (clean))
        oldFile.deleteFile();   // o conteúdo já está no arquivo novo

    if (filter.view == Filter::View::combo && filter.combo.equalsIgnoreCase (oldName))
        filter.combo = clean;

    if (comboPlaying.equalsIgnoreCase (oldName))
        comboPlaying = clean;

    loadCombos();
    rebuildRows();
    sendChangeMessage();
    return juce::Result::ok();
}

juce::Result ProjectLibrary::deleteCombo (const juce::String& name)
{
    const auto* c = findCombo (name);

    if (c == nullptr)
        return juce::Result::fail ("Combo não encontrado: "_t + name);

    const auto file = comboFile (c->name);

    if (file.existsAsFile() && ! file.moveToTrash())
        return juce::Result::fail ("Não consegui mandar o combo para a lixeira."_t);

    if (comboPlaying.equalsIgnoreCase (name))
        stopCombo();

    if (filter.view == Filter::View::combo && filter.combo.equalsIgnoreCase (name))
        filter = {};

    loadCombos();
    rebuildRows();
    sendChangeMessage();
    return juce::Result::ok();
}

juce::Result ProjectLibrary::addToCombo (const juce::String& combo, const juce::File& file, bool latest, const juce::String& note, int index)
{
    auto* c = comboNamed (combo);

    if (c == nullptr)
        return juce::Result::fail ("Combo não encontrado: "_t + combo);

    if (! file.existsAsFile())
        return juce::Result::fail ("Arquivo não encontrado: "_t + file.getFullPathName());

    ComboItem item { file, latest && file.hasFileExtension ("wnl"), note };

    if (juce::isPositiveAndBelow (index, (int) c->items.size()))
        c->items.insert (c->items.begin() + index, item);
    else
        c->items.push_back (item);

    combosChanged (*c);
    return juce::Result::ok();
}

juce::Result ProjectLibrary::removeFromCombo (const juce::String& combo, int index)
{
    auto* c = comboNamed (combo);

    if (c == nullptr || ! juce::isPositiveAndBelow (index, (int) c->items.size()))
        return juce::Result::fail ("Item do combo não encontrado."_t);

    c->items.erase (c->items.begin() + index);
    combosChanged (*c);
    return juce::Result::ok();
}

juce::Result ProjectLibrary::moveInCombo (const juce::String& combo, int from, int to)
{
    auto* c = comboNamed (combo);

    if (c == nullptr || ! juce::isPositiveAndBelow (from, (int) c->items.size()))
        return juce::Result::fail ("Item do combo não encontrado."_t);

    to = juce::jlimit (0, (int) c->items.size() - 1, to);
    auto item = c->items[(size_t) from];
    c->items.erase (c->items.begin() + from);
    c->items.insert (c->items.begin() + to, item);
    combosChanged (*c);
    return juce::Result::ok();
}

juce::Result ProjectLibrary::setComboNote (const juce::String& combo, int index, const juce::String& note)
{
    auto* c = comboNamed (combo);

    if (c == nullptr || ! juce::isPositiveAndBelow (index, (int) c->items.size()))
        return juce::Result::fail ("Item do combo não encontrado."_t);

    c->items[(size_t) index].note = note.trim();
    combosChanged (*c);
    return juce::Result::ok();
}

juce::Result ProjectLibrary::setComboLatest (const juce::String& combo, int index, bool latest)
{
    auto* c = comboNamed (combo);

    if (c == nullptr || ! juce::isPositiveAndBelow (index, (int) c->items.size()))
        return juce::Result::fail ("Item do combo não encontrado."_t);

    c->items[(size_t) index].latest = latest && c->items[(size_t) index].file.hasFileExtension ("wnl");
    combosChanged (*c);
    return juce::Result::ok();
}

juce::File ProjectLibrary::resolve (const ComboItem& item) const
{
    if (! item.latest || ! item.file.hasFileExtension ("wnl"))
        return item.file;

    // a versão mais nova da mesma pilha (Beat, Beat_2, Beat_3 na mesma pasta), pela data do arquivo
    const auto key = stackKey (item.file);
    auto best = item.file;
    auto bestTime = item.file.existsAsFile() ? item.file.getLastModificationTime() : juce::Time();

    for (const auto& entry : juce::RangedDirectoryIterator (item.file.getParentDirectory(), false, "*.wnl"))
    {
        const auto f = entry.getFile();

        if (stackKey (f) == key && entry.getModificationTime() > bestTime)
        {
            best = f;
            bestTime = entry.getModificationTime();
        }
    }

    return best;
}

juce::Result ProjectLibrary::playCombo (const juce::String& combo, int from)
{
    const auto* c = findCombo (combo);

    if (c == nullptr)
        return juce::Result::fail ("Combo não encontrado: "_t + combo);

    if (c->items.empty())
        return juce::Result::fail ("O combo está vazio."_t);

    comboPlaying = c->name;
    comboPos = juce::jlimit (0, (int) c->items.size() - 1, from) - 1;

    if (advanceCombo().failed())
        return juce::Result::fail ("Nenhum item do combo tem prévia para tocar."_t);

    return juce::Result::ok();
}

juce::Result ProjectLibrary::advanceCombo()
{
    const auto* c = findCombo (comboPlaying);

    while (c != nullptr && ++comboPos < (int) c->items.size())
    {
        // pula o que não toca: projeto sem prévia, arquivo que sumiu
        const auto file = resolve (c->items[(size_t) comboPos]);
        const bool playable = file.hasFileExtension ("wnl") ? previewAudio (file).existsAsFile() : file.existsAsFile();

        if (! playable || loadDeck (active, file, 0.0).failed())
            continue;

        if (host.isPlaying != nullptr && host.isPlaying() && host.stopLive != nullptr)
            host.stopLive();

        decks.play();
        sendChangeMessage();
        return juce::Result::ok();
    }

    stopCombo();
    return juce::Result::fail ("O combo terminou."_t);
}

void ProjectLibrary::stopCombo()
{
    comboPlaying = {};
    comboPos = -1;
    sendChangeMessage();
}

juce::var ProjectLibrary::describeCombos() const
{
    juce::Array<juce::var> out;

    for (auto& c : comboList)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("name", c.name);
        o->setProperty ("file", comboFile (c.name).getFullPathName());

        juce::Array<juce::var> items;

        for (auto& i : c.items)
        {
            auto e = new juce::DynamicObject();
            e->setProperty ("path", i.file.getFullPathName());
            e->setProperty ("plays", resolve (i).getFullPathName());
            e->setProperty ("latest", i.latest);
            e->setProperty ("note", i.note);
            items.add (juce::var (e));
        }

        o->setProperty ("items", items);
        out.add (juce::var (o));
    }

    auto root = new juce::DynamicObject();
    root->setProperty ("combos", out);
    root->setProperty ("playing", comboPlaying);
    root->setProperty ("position", comboPos);
    return juce::var (root);
}

//==============================================================================
juce::var ProjectLibrary::describe() const
{
    juce::Array<juce::var> out;

    for (auto& item : list)
    {
        auto o = new juce::DynamicObject();
        const auto& e = item.entry;
        o->setProperty ("path", e.file.getFullPathName());
        o->setProperty ("name", e.name);
        o->setProperty ("bpm", e.bpm);
        o->setProperty ("key", e.keyName());
        o->setProperty ("timeSig", juce::String (e.timeSigNum) + "/" + juce::String (e.timeSigDen));
        o->setProperty ("modified", e.modified.toISO8601 (true));
        o->setProperty ("recent", item.recent);

        if (const auto m = metaOf (e.file); ! m.empty())
        {
            o->setProperty ("favorite", m.favorite);
            o->setProperty ("colour", m.colour);
            o->setProperty ("tags", m.tags.joinIntoString (","));
            o->setProperty ("status", statusId (m.status));
        }

        if (e.error.isNotEmpty())
            o->setProperty ("error", e.error);

        auto p = new juce::DynamicObject();
        p->setProperty ("state", stateName (item.state));

        if (item.preview.valid)
        {
            p->setProperty ("file", previewAudio (e.file).getFullPathName());
            p->setProperty ("seconds", item.preview.seconds);
            p->setProperty ("lufs", item.preview.lufs);
            p->setProperty ("truePeakDb", item.preview.truePeakDb);
            p->setProperty ("what", item.preview.what);
        }

        if (item.state == PreviewState::rendering)
            p->setProperty ("progress", item.progress);

        if (item.error.isNotEmpty())
            p->setProperty ("error", item.error);

        o->setProperty ("preview", juce::var (p));
        out.add (juce::var (o));
    }

    auto root = new juce::DynamicObject();
    root->setProperty ("projects", out);
    root->setProperty ("scanning", isScanning());
    root->setProperty ("renderIdle", renderer.isIdle());
    root->setProperty ("renderStatus", renderStatusText());

    juce::Array<juce::var> folders;

    for (auto& r : scanRoots())
        folders.add (r.getFullPathName());

    root->setProperty ("folders", folders);
    return juce::var (root);
}

juce::var ProjectLibrary::deckStatus() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("active", active == 0 ? "A" : "B");
    o->setProperty ("playing", decks.isPlaying());
    o->setProperty ("sync", syncName (sync));
    o->setProperty ("matchLoudness", matchLoudness);
    o->setProperty ("loopBars", loopBars);
    o->setProperty ("underruns", decks.underruns());

    // o deck que soa também no topo (o formato da fatia 1)
    o->setProperty ("project", info[(size_t) active].project.getFullPathName());
    o->setProperty ("file", decks.fileOf (active).getFullPathName());
    o->setProperty ("position", decks.positionSeconds (active));
    o->setProperty ("length", decks.lengthSeconds (active));

    juce::Array<juce::var> perDeck;

    for (int d = 0; d < DeckPlayer::numDecks; ++d)
    {
        auto e = new juce::DynamicObject();
        const auto& deck = info[(size_t) d];
        e->setProperty ("deck", d == 0 ? "A" : "B");
        e->setProperty ("loaded", decks.isLoaded (d));
        e->setProperty ("name", deck.name);
        e->setProperty ("project", deck.project.getFullPathName());
        e->setProperty ("file", decks.fileOf (d).getFullPathName());
        e->setProperty ("position", decks.positionSeconds (d));
        e->setProperty ("length", decks.lengthSeconds (d));
        e->setProperty ("bpm", deck.bpm);
        e->setProperty ("lufs", deck.lufsKnown ? juce::var (deck.lufs) : juce::var());
        e->setProperty ("gainDb", matchGainDb (d));

        if (const auto loop = decks.loopOf (d); loop.active())
        {
            e->setProperty ("loopStart", loop.start);
            e->setProperty ("loopEnd", loop.end);
        }

        perDeck.add (juce::var (e));
    }

    o->setProperty ("decks", perDeck);
    return juce::var (o);
}

//==============================================================================
void ProjectLibrary::loadIndex()
{
    const auto v = juce::JSON::parse (indexFile());

    if (! v.isObject() || (int) v.getProperty ("version", 0) != 1)
        return;

    if (auto* r = v.getProperty ("recent", {}).getArray())
        for (auto& path : *r)
            if (path.toString().isNotEmpty() && recent.size() < maxRecent)
                recent.add (path.toString());

    if (auto* projects = v.getProperty ("projects", {}).getArray())
    {
        for (auto& p : *projects)
        {
            auto e = entryFromVar (p);

            if (e.file != juce::File() && e.hash.isNotEmpty())
                known[e.file.getFullPathName()] = e;
        }
    }

    if (auto* marks = v.getProperty ("meta", {}).getArray())
    {
        for (auto& p : *marks)
        {
            const auto path = p.getProperty ("path", {}).toString();

            if (path.isEmpty())
                continue;

            Meta m;
            m.favorite = (bool) p.getProperty ("favorite", false);
            m.colour = p.getProperty ("colour", {}).toString();
            m.status = statusFromText (p.getProperty ("status", {}).toString());

            if (auto* tags = p.getProperty ("tags", {}).getArray())
                for (auto& tag : *tags)
                    if (tag.toString().isNotEmpty())
                        m.tags.add (tag.toString());

            if (! m.empty())
                meta[path] = m;
        }
    }

    if (auto* folders = v.getProperty ("folders", {}).getArray())
        for (auto& f : *folders)
            if (juce::File::isAbsolutePath (f.toString()))
                extraFolders.addIfNotAlreadyThere (juce::File (f.toString()));
}

void ProjectLibrary::saveIndex() const
{
    auto o = new juce::DynamicObject();
    juce::var root (o);
    o->setProperty ("version", 1);

    juce::Array<juce::var> r;

    for (auto& path : recent)
        r.add (path);

    o->setProperty ("recent", r);

    juce::Array<juce::var> projects;

    for (auto& [path, e] : known)
        if (e.error.isEmpty() && e.hash.isNotEmpty())
            projects.add (entryToVar (e));

    o->setProperty ("projects", projects);

    juce::Array<juce::var> marks;

    for (auto& [path, m] : meta)
    {
        if (m.empty())
            continue;

        auto e = new juce::DynamicObject();
        e->setProperty ("path", path);
        e->setProperty ("favorite", m.favorite);
        e->setProperty ("colour", m.colour);
        e->setProperty ("status", statusId (m.status));

        juce::Array<juce::var> tags;

        for (auto& tag : m.tags)
            tags.add (tag);

        e->setProperty ("tags", tags);
        marks.add (juce::var (e));
    }

    o->setProperty ("meta", marks);

    juce::Array<juce::var> folders;

    for (auto& f : extraFolders)
        folders.add (f.getFullPathName());

    o->setProperty ("folders", folders);

    if (host.dataFolder.createDirectory())
        indexFile().replaceWithText (juce::JSON::toString (root));
}

} // namespace whynot::library
