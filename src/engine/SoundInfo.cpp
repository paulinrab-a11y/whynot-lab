#include "SoundInfo.h"

#include "SampleAnalysis.h"
#include "../common/Text.h"
#include "../ideas/KeyDetect.h"
#include "../record/BeatTempo.h"

#if JUCE_WINDOWS
extern "C" __declspec (dllimport) unsigned long __stdcall GetFileAttributesW (const wchar_t*);
#endif

namespace whynot
{

namespace
{
    constexpr double minLoopSeconds = 1.0;   // abaixo disto, sempre um one-shot (a nota; o andamento só o do nome)
    constexpr double maxSeconds = 60.0;      // a leitura usa até 1 minuto
    constexpr double minKeyProbability = 0.4;   // um loop só de bateria já sai antes, pelo found() (sem nota)

    juce::String noteName (int pitchClass)
    {
        static const char* const names[] = { "Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si" };
        return juce::String::fromUTF8 (names[((pitchClass % 12) + 12) % 12]);
    }

    int letterValue (juce::juce_wchar c)
    {
        switch (juce::CharacterFunctions::toUpperCase (c))
        {
            case 'C': return 0;
            case 'D': return 2;
            case 'E': return 4;
            case 'F': return 5;
            case 'G': return 7;
            case 'A': return 9;
            case 'B': return 11;
            default:  return -1;
        }
    }

    /** "do", "dó", "re", "ré", "mi", "fa", "fá", "sol", "la", "lá", "si" (o nome das notas em português). */
    int solfegeValue (const juce::String& word)
    {
        static const std::pair<const char*, int> names[] { { "do", 0 }, { "dó", 0 }, { "re", 2 }, { "ré", 2 }, { "mi", 4 },
                                                          { "fa", 5 }, { "fá", 5 }, { "sol", 7 }, { "la", 9 }, { "lá", 9 },
                                                          { "si", 11 } };
        const auto w = word.toLowerCase();

        for (auto& [name, value] : names)
            if (w == juce::String::fromUTF8 (name))
                return value;

        return -1;
    }

    /** 0 = menor, 1 = maior, -1 = não é modo. "m" sozinho só conta colado numa letra maiúscula ("Am"). O "M"
        maiúsculo é ambíguo (maior na cifra, menor nos nomes em maiúsculas): fica com quem chama. */
    int modeWord (const juce::String& w, bool strictM)
    {
        if (w == "m")
            return strictM ? 0 : -1;

        if (w == "M")
            return -1;

        const auto lower = w.toLowerCase();

        if (lower == "min" || lower == "minor" || lower == "menor" || lower == "moll")
            return 0;

        if (lower == "maj" || lower == "major" || lower == "maior" || lower == "dur")
            return 1;

        return -1;
    }

    int accidentalWord (const juce::String& w)
    {
        const auto lower = w.toLowerCase();
        return lower == "sharp" || lower == "#" ? 1 : lower == "flat" ? -1 : 0;
    }

    /** A letra da nota com o acidente colado ("C", "C#", "Db", "F♯") e o que sobra depois ("m", "min", "maj"). */
    struct NotePart
    {
        int root = -1;
        bool upper = false;
        juce::String rest;
    };

    NotePart splitNote (const juce::String& token)
    {
        if (token.isEmpty())
            return {};

        int root = letterValue (token[0]);

        if (root < 0)
            return {};

        int at = 1;

        if (token.length() > 1)
        {
            const auto c = token[1];

            if (c == '#' || c == 0x266F)
            {
                ++root;
                at = 2;
            }
            else if (c == 'b' || c == 0x266D)
            {
                --root;
                at = 2;
            }
        }

        return { (root + 12) % 12, juce::CharacterFunctions::isUpperCase (token[0]), token.substring (at) };
    }

    juce::String trimDigits (juce::String t)
    {
        while (t.isNotEmpty() && juce::CharacterFunctions::isDigit (t[0]))
            t = t.substring (1);

        while (t.isNotEmpty() && juce::CharacterFunctions::isDigit (t.getLastCharacter()))
            t = t.dropLastCharacters (1);

        return t;
    }

    /** Quantos ataques o som tem: a energia (janelas de 10 ms) que sobe 6 dB sobre o mínimo dos 50 ms antes,
        acima de -40 dBFS, com 80 ms de folga entre um e outro. Um loop tem vários; um 808 ou um prato, um. */
    int countAttacks (const std::vector<float>& x, double rate)
    {
        const int hop = juce::jmax (1, (int) (rate * 0.01));
        std::vector<double> energy;

        for (size_t at = 0; at + (size_t) hop <= x.size(); at += (size_t) hop)
        {
            double e = 0.0;

            for (int i = 0; i < hop; ++i)
                e += (double) x[at + (size_t) i] * x[at + (size_t) i];

            energy.push_back (e / hop);
        }

        int attacks = 0, quietUntil = -1;

        for (int i = 5; i < (int) energy.size(); ++i)
        {
            double floor = energy[(size_t) i - 1];

            for (int k = 2; k <= 5; ++k)
                floor = juce::jmin (floor, energy[(size_t) (i - k)]);

            if (i > quietUntil && energy[(size_t) i] > 1.0e-4 && energy[(size_t) i] > 4.0 * floor + 1.0e-9)
            {
                ++attacks;
                quietUntil = i + 8;
            }
        }

        return attacks;
    }

    bool isWholeLoop (double beats)
    {
        // um loop fecha em tempos inteiros: 2, 3, 4, 6, 8, 12, 16, 24 ou 32 (de meio compasso a 8 compassos)
        static constexpr int counts[] { 2, 3, 4, 6, 8, 12, 16, 24, 32 };

        for (int n : counts)
            if (std::abs (beats - n) <= 0.03 * n)
                return true;

        return false;
    }

    bool inScale (int note, int root, bool major)
    {
        static constexpr int majorSteps[] { 0, 2, 4, 5, 7, 9, 11 }, minorSteps[] { 0, 2, 3, 5, 7, 8, 10 };
        const int degree = ((note - root) % 12 + 12) % 12;

        for (int s : (major ? majorSteps : minorSteps))
            if (s == degree)
                return true;

        return false;
    }

    /** O tom como o relativo menor (Dó maior e Lá menor dão 9): as mesmas notas, o mesmo número. */
    int asMinor (int root, bool major)   { return major ? (root + 9) % 12 : root % 12; }

   #if JUCE_WINDOWS
    unsigned int cloudAttributes (const wchar_t* path)   { return (unsigned int) GetFileAttributesW (path); }
   #endif
}

//==============================================================================
juce::String SoundInfo::keyText() const
{
    if (! hasKey())
        return {};

    return isNote ? noteName (keyRoot) : juce::String::fromUTF8 (ideas::keyNamePt (keyRoot, keyMajor).c_str());
}

juce::String SoundInfo::tags() const
{
    juce::StringArray parts;

    if (bpm > 0.0)
        parts.add ((std::abs (bpm - std::round (bpm)) < 0.05 ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 1).replace (".", ","))
                   + " BPM");

    if (hasKey())
        parts.add (keyText());

    return parts.joinIntoString (" · "_t);
}

juce::var SoundInfo::toVar (const juce::File& f) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("path", f.getFullPathName());
    o->setProperty ("name", f.getFileNameWithoutExtension());
    o->setProperty ("seconds", std::round (seconds * 100.0) / 100.0);
    o->setProperty ("bpm", std::round (bpm * 10.0) / 10.0);
    o->setProperty ("bpmSource", bpmSource);
    o->setProperty ("key", keyText());
    o->setProperty ("root", keyRoot);
    o->setProperty ("major", keyMajor);
    o->setProperty ("note", isNote);
    o->setProperty ("keySource", keySource);
    o->setProperty ("loop", isLoop());
    o->setProperty ("role", role);
    o->setProperty ("analysed", analysed);

    if (! readable)
        o->setProperty ("error", "o áudio não abriu"_t);

    return juce::var (o);
}

//==============================================================================
namespace soundinfo
{
    /** Uma passada pelas palavras do nome: as formas por extenso ou as curtas. */
    std::optional<std::pair<int, bool>> keyInTokens (const juce::StringArray& tokens, bool shortForms, bool allCaps);
}

std::optional<std::pair<int, bool>> soundinfo::keyFromName (const juce::String& name)
{
    const auto text = name.replaceCharacters ("_-.,;()[]{}+&~", "              ");

    juce::StringArray tokens;
    tokens.addTokens (text, " ", "");
    tokens.removeEmptyStrings();

    for (auto& t : tokens)
        t = trimDigits (t);

    // nome todo em maiúsculas ("LOOP_AM_120"): lá o "M" colado é menor
    const bool allCaps = name.toUpperCase() == name && name.containsAnyOf ("ABCDEFGHIJKLMNOPQRSTUVWXYZ");

    // primeiro as formas por extenso, depois as curtas ("Am"): "Loop Em Lá Menor" é Lá menor
    for (int pass = 0; pass < 2; ++pass)
    {
        if (auto key = keyInTokens (tokens, pass == 1, allCaps))
            return key;
    }

    return std::nullopt;
}

std::optional<std::pair<int, bool>> soundinfo::keyInTokens (const juce::StringArray& tokens, bool shortForms, bool allCaps)
{
    for (int i = 0; i < tokens.size(); ++i)
    {
        const auto& t = tokens[i];

        // Dó menor, Sol# maior: o nome em português, sempre com "menor" ou "maior" depois (por extenso)
        if (auto base = t.endsWithChar ('#') || t.endsWithChar ('b') ? t.dropLastCharacters (1) : t; solfegeValue (base) >= 0 && i + 1 < tokens.size())
        {
            const int shift = t.endsWithChar ('#') ? 1 : (t.endsWithChar ('b') && base != t ? -1 : 0);

            if (const int mode = modeWord (tokens[i + 1], false); ! shortForms && mode >= 0
                && (tokens[i + 1].equalsIgnoreCase ("menor") || tokens[i + 1].equalsIgnoreCase ("maior")))
                return std::make_pair ((solfegeValue (base) + shift + 12) % 12, mode == 1);
        }

        const auto note = splitNote (t);

        if (note.root < 0)
            continue;

        // o modo colado: "C#min", "Bbmaj" (por extenso); "Am", "F#m" e, num nome em maiúsculas, "AM" (curtas)
        if (note.rest.isNotEmpty())
        {
            const bool shortMode = note.rest == "m" || note.rest == "M";

            if (shortMode != shortForms)
                continue;

            if (note.rest == "M")
            {
                if (allCaps && t != "FM" && t != "GM")   // síntese FM e General MIDI não são tom
                    return std::make_pair (note.root, false);

                continue;
            }

            if (const int mode = modeWord (note.rest, note.upper); mode >= 0)
                return std::make_pair (note.root, mode == 1);

            continue;
        }

        if (shortForms)
            continue;

        // a letra sozinha, com o acidente e o modo nas palavras seguintes: "F minor", "F sharp minor", "C# min"
        int root = note.root, next = i + 1;

        if (next < tokens.size())
            if (const int acc = accidentalWord (tokens[next]); acc != 0)
            {
                root = (root + acc + 12) % 12;
                ++next;
            }

        if (next < tokens.size())
            if (const int mode = modeWord (tokens[next], note.upper); mode >= 0)   // o "M" sozinho já não é modo
                return std::make_pair (root, mode == 1);
    }

    return std::nullopt;
}

SoundInfo soundinfo::fromName (const juce::File& file)
{
    SoundInfo info;
    const auto name = file.getFileNameWithoutExtension();

    if (const double bpm = beat::bpmFromName (name.toStdString()); bpm > 0.0)
    {
        info.bpm = bpm;
        info.bpmSource = "nome";
    }

    if (const auto key = keyFromName (name))
    {
        info.keyRoot = key->first;
        info.keyMajor = key->second;
        info.keySource = "nome";
    }

    const auto role = roleFromName (name);
    info.role = info.isLoop() ? juce::String ("loop") : juce::String (roleId (role));
    return info;
}

juce::String soundinfo::stampOf (const juce::File& file)
{
    return file.existsAsFile() ? juce::String (file.getSize()) + ":" + juce::String (file.getLastModificationTime().toMilliseconds())
                               : juce::String();
}

SoundInfo soundinfo::analyse (const juce::File& file, const std::atomic<bool>* cancel)
{
    auto info = fromName (file);
    info.analysed = true;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        info.readable = false;
        return info;
    }

    const double rate = reader->sampleRate;
    info.seconds = (double) reader->lengthInSamples / rate;
    auto nameRole = roleFromName (file.getFileNameWithoutExtension());

    if (nameRole == SampleRole::unknown)
        nameRole = roleFromName (file.getParentDirectory().getFileName());

    // até 1 minuto, em mono: para separar o loop do one-shot, para o andamento e para o tom
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    const auto total = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxSeconds * rate));
    std::vector<float> mono ((size_t) total);
    juce::AudioBuffer<float> chunk (channels, 32768);

    for (int pos = 0; pos < total; pos += chunk.getNumSamples())
    {
        if (cancel != nullptr && cancel->load())
            return info;

        const int n = juce::jmin (chunk.getNumSamples(), total - pos);
        reader->read (&chunk, 0, n, pos, true, channels > 1);

        for (int i = 0; i < n; ++i)
            mono[(size_t) (pos + i)] = channels > 1 ? 0.5f * (chunk.getSample (0, i) + chunk.getSample (1, i)) : chunk.getSample (0, i);
    }

    reader.reset();

    // one-shot: curto demais, com nome de um toque só (kick, 808, prato...), ou com menos de 3 ataques
    const bool namedOneShot = nameRole != SampleRole::unknown && nameRole != SampleRole::loop && nameRole != SampleRole::vocal;
    const int attacks = countAttacks (mono, rate);
    const bool oneShot = ! info.isLoop() && (info.seconds < minLoopSeconds || namedOneShot || (info.seconds < 8.0 && attacks < 3));

    if (oneShot)
    {
        // a nota, se não é bateria (um 808, um baixo, uma nota de piano)
        const auto profile = analyseSample (file);
        const auto role = guessSampleRole (file, profile);
        info.role = roleId (role);

        if (! info.hasKey() && ! isDrumRole (role) && rootIsReliable (profile.pitch))
        {
            info.keyRoot = profile.pitch.key % 12;
            info.isNote = true;
            info.keySource = "análise"_t;
        }

        return info;
    }

    // o andamento, só de um som com ataques (um pad, uma voz longa, não). O detector lê o sinal sem os graves (a
    // diferença de uma amostra para a outra): os tons sustentados batem devagar entre si e fingem um pulso (um acorde
    // de senos dava 87 BPM, com ou sem as batidas), e o ataque da bateria fica
    if (! info.isLoop() && attacks >= 3)
    {
        double found = 0.0;
        std::vector<float> edges (mono.size(), 0.0f);

        for (size_t i = 1; i < mono.size(); ++i)
            edges[i] = mono[i] - mono[i - 1];

        if (info.seconds >= 8.0)
        {
            found = beat::bpmFromAudio (edges.data(), total, rate);
        }
        else
        {
            // um loop curto (de meio a 4 compassos): repetido até 16 s, o detector acha o pulso; e o tamanho do
            // loop tem de fechar em tempos inteiros nesse andamento, senão não é loop
            std::vector<float> tiled;
            tiled.reserve ((size_t) (17.0 * rate));

            while ((double) tiled.size() < 16.0 * rate)
                tiled.insert (tiled.end(), edges.begin(), edges.end());

            if (const double candidate = beat::bpmFromAudio (tiled.data(), (int) tiled.size(), rate);
                candidate > 0.0 && isWholeLoop (info.seconds * candidate / 60.0))
                found = candidate;
        }

        if (found > 0.0)
        {
            info.bpm = found;
            info.bpmSource = "análise"_t;
        }
    }

    // o tom de um loop com nota (um loop só de bateria não tem)
    if (! info.hasKey())
    {
        ideas::Downsampler down (rate);
        down.push (mono.data(), mono.size());
        down.finish();
        const auto analysis = ideas::analyzeDownsampled (std::move (down.samples()), down.outputRate(), 0.0, (double) total / rate, cancel);

        if (analysis.found() && analysis.best().probability >= minKeyProbability)
        {
            info.keyRoot = analysis.best().root;
            info.keyMajor = analysis.best().major;
            info.keySource = "análise"_t;
        }
    }

    info.role = info.isLoop() ? juce::String ("loop") : juce::String (roleId (nameRole));
    return info;
}

//==============================================================================
namespace
{
    /** As notas de uma escala como 12 bits (bit 0 = Dó). */
    int maskOf (int root, const std::vector<int>& degrees)
    {
        int mask = 0;

        for (int d : degrees)
            mask |= 1 << (((root + d) % 12 + 12) % 12);

        return mask;
    }

    int keyMask (int root, bool major)
    {
        static const std::vector<int> majorSteps { 0, 2, 4, 5, 7, 9, 11 }, minorSteps { 0, 2, 3, 5, 7, 8, 10 };
        return maskOf (root, major ? majorSteps : minorSteps);
    }

    int countBits (int mask)
    {
        int n = 0;

        for (; mask != 0; mask &= mask - 1)
            ++n;

        return n;
    }

    /** A escala transposta (os 12 bits girados). */
    int rotate (int mask, int semitones)
    {
        semitones = ((semitones % 12) + 12) % 12;
        return ((mask << semitones) | (mask >> (12 - semitones))) & 0xfff;
    }
}

bool soundinfo::keyFits (const SoundInfo& info, int projectRoot, const std::vector<int>& projectDegrees, KeyFilter filter)
{
    if (filter == KeyFilter::any)
        return true;

    if (! info.hasKey())
        return false;

    const int project = maskOf (projectRoot, projectDegrees), size = countBits (project);

    if (size >= 12)
        return true;   // cromática: todo tom serve

    if (info.isNote)
    {
        // a nota de um one-shot: dentro da escala do projeto; nos vizinhos, também a de uma quinta acima ou abaixo
        const int bit = 1 << info.keyRoot;

        if ((project & bit) != 0)
            return true;

        return filter == KeyFilter::compatible && ((rotate (project, 7) | rotate (project, 5)) & bit) != 0;
    }

    const int sound = keyMask (info.keyRoot, info.keyMajor), shared = countBits (sound & project);
    const int tonic = info.keyMajor ? (info.keyRoot + 9) % 12 : info.keyRoot;   // o tom como o relativo menor

    if (size == 7)
    {
        if (sound == project)
            return true;   // as mesmas notas: o tom, o relativo ou o tom do modo

        // uma escala que não é de tom nenhum (a menor harmônica): o tom da mesma tônica que muda uma nota
        const bool keyScale = [project]
        {
            for (int r = 0; r < 12; ++r)
                if (keyMask (r, false) == project)
                    return true;

            return false;
        }();

        if (! keyScale && shared == 6 && tonic == projectRoot % 12)
            return true;

        return filter == KeyFilter::compatible && shared >= 6;   // os vizinhos mudam uma nota só
    }

    // a pentatônica (e outras menores): o tom que tem todas as notas dela; no "tom do projeto", só o da mesma tônica
    if ((sound & project) != project)
        return false;

    return filter == KeyFilter::compatible || tonic == projectRoot % 12;
}

bool soundinfo::keyFits (const SoundInfo& info, int projectRoot, bool projectMajor, KeyFilter filter)
{
    static const std::vector<int> majorSteps { 0, 2, 4, 5, 7, 9, 11 }, minorSteps { 0, 2, 3, 5, 7, 8, 10 };
    return keyFits (info, projectRoot, projectMajor ? majorSteps : minorSteps, filter);
}

std::vector<std::pair<int, bool>> soundinfo::keysThatFit (int projectRoot, const std::vector<int>& projectDegrees, KeyFilter filter)
{
    std::vector<std::pair<int, bool>> keys;

    for (int major = 0; major < 2; ++major)
    {
        for (int r = 0; r < 12; ++r)
        {
            SoundInfo k;
            k.keyRoot = (projectRoot + (major == 1 ? 3 : 0) + r) % 12;   // começa no tom do projeto (e no relativo maior)
            k.keyMajor = major == 1;

            if (keyFits (k, projectRoot, projectDegrees, filter))
                keys.push_back ({ k.keyRoot, k.keyMajor });
        }
    }

    return keys;
}

juce::String soundinfo::projectKeyText (int root, const juce::String& scale)
{
    root = ((root % 12) + 12) % 12;

    if (scale == "major" || scale == "minor")
    {
        SoundInfo k;
        k.keyRoot = root;
        k.keyMajor = scale == "major";
        return k.keyText();
    }

    const auto mode = scale == "dorian"          ? "dórico"_t
                    : scale == "phrygian"        ? "frígio"_t
                    : scale == "harmonicMinor"   ? "menor harmônica"_t
                    : scale == "pentatonicMinor" ? "pentatônica menor"_t
                    : scale == "chromatic"       ? "cromática"_t
                                                 : juce::String (scale);
    return noteName (root) + " " + mode;
}

bool soundinfo::isCloudOnly (const juce::File& file)
{
   #if JUCE_WINDOWS
    // FILE_ATTRIBUTE_OFFLINE, RECALL_ON_OPEN e RECALL_ON_DATA_ACCESS: o OneDrive (e outros) baixa ao abrir
    const auto attributes = cloudAttributes (file.getFullPathName().toWideCharPointer());
    return attributes != 0xffffffffu && (attributes & (0x1000u | 0x40000u | 0x400000u)) != 0;
   #else
    juce::ignoreUnused (file);
    return false;
   #endif
}

bool soundinfo::bpmFits (double bpm, double projectBpm, BpmFilter filter)
{
    if (filter == BpmFilter::any)
        return true;

    if (bpm <= 0.0 || projectBpm <= 0.0)
        return false;

    for (const double target : { projectBpm, projectBpm * 0.5, projectBpm * 2.0 })
        if (std::abs (bpm - target) <= 0.04 * target)
            return true;

    return false;
}

bool soundinfo::kindFits (const SoundInfo& info, KindFilter filter)
{
    switch (filter)
    {
        case KindFilter::loops:     return info.isLoop();
        case KindFilter::oneShots:  return ! info.isLoop();
        case KindFilter::any:       break;
    }

    return true;
}

//==============================================================================
SoundInfoCache::SoundInfoCache (juce::File cacheFile)
    : juce::Thread ("WHYNOT sons: BPM e tom"), file (std::move (cacheFile))
{
    load();
    startThread (juce::Thread::Priority::low);
}

SoundInfoCache::~SoundInfoCache()
{
    stopping = true;
    signalThreadShouldExit();
    wake.signal();
    stopThread (15000);   // um HD externo dormindo leva alguns segundos para acordar; matar a thread é o último caso
    cancelPendingUpdate();
    save();
}

juce::String SoundInfoCache::keyOf (const juce::File& f)
{
    return f.getFullPathName().toLowerCase();   // o Windows não diferencia caixa
}

SoundInfo SoundInfoCache::get (const juce::File& f) const
{
    {
        const juce::ScopedLock sl (lock);

        if (auto it = known.find (keyOf (f)); it != known.end())
            return it->second;
    }

    return soundinfo::fromName (f);
}

std::optional<SoundInfo> SoundInfoCache::find (const juce::File& f) const
{
    const juce::ScopedLock sl (lock);

    if (auto it = known.find (keyOf (f)); it != known.end())
        return it->second;

    return std::nullopt;
}

bool SoundInfoCache::isSettled (const juce::File& f) const
{
    const juce::ScopedLock sl (lock);
    return settled.count (keyOf (f)) > 0;
}

int SoundInfoCache::pending() const
{
    const juce::ScopedLock sl (lock);
    return (int) queued.size();
}

void SoundInfoCache::request (const juce::File& f)
{
    const auto k = keyOf (f);

    {
        const juce::ScopedLock sl (lock);

        if (queued.count (k) > 0 || settled.count (k) > 0)
            return;

        queued.insert (k);
        queue.push_back (f);

        // a fila não cresce sem fim (a lista do navegador pede de novo o que está na tela)
        while ((int) queue.size() > maxQueue)
        {
            queued.erase (keyOf (queue.front()));
            queue.pop_front();
        }
    }

    wake.signal();
}

void SoundInfoCache::requestMany (const std::vector<juce::File>& files)
{
    if (files.empty())
        return;

    {
        const juce::ScopedLock sl (lock);

        // a thread pega do fim: os primeiros da lista vão por último na fila, e só os que cabem
        const int room = juce::jmax (0, maxQueue - (int) queue.size());
        int taken = 0;
        std::vector<const juce::File*> wanted;

        for (auto& f : files)
        {
            if (taken >= room)
                break;

            const auto k = keyOf (f);

            if (queued.count (k) > 0 || settled.count (k) > 0)
                continue;

            queued.insert (k);
            wanted.push_back (&f);
            ++taken;
        }

        for (auto it = wanted.rbegin(); it != wanted.rend(); ++it)
            queue.push_back (**it);
    }

    wake.signal();
}

std::vector<SoundInfoCache::Status> SoundInfoCache::statusOf (const juce::Array<juce::File>& files) const
{
    std::vector<juce::String> keys;
    keys.reserve ((size_t) files.size());

    for (auto& f : files)
        keys.push_back (keyOf (f));   // fora da trava

    std::vector<Status> out ((size_t) files.size());
    const juce::ScopedLock sl (lock);

    for (size_t i = 0; i < keys.size(); ++i)
    {
        if (auto it = known.find (keys[i]); it != known.end())
            out[i].info = it->second;

        out[i].settled = settled.count (keys[i]) > 0;
    }

    return out;
}

SoundInfo SoundInfoCache::waitFor (const juce::File& f, int timeoutMs)
{
    const auto k = keyOf (f);
    const auto stamp = soundinfo::stampOf (f);

    {
        const juce::ScopedLock sl (lock);

        // conferido nesta sessão, mas o arquivo mudou depois (o produtor exportou de novo): lê de novo
        if (auto it = known.find (k); settled.count (k) > 0 && it != known.end() && it->second.stamp != stamp)
            settled.erase (k);

        if (settled.count (k) == 0)
        {
            // na frente da fila: a thread pega do fim
            queue.erase (std::remove (queue.begin(), queue.end(), f), queue.end());
            queue.push_back (f);
            queued.insert (k);
        }
    }

    wake.signal();
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);

    while (juce::Time::getMillisecondCounter() < end)
    {
        if (isSettled (f))
            break;

        juce::Thread::sleep (5);
    }

    return get (f);
}

void SoundInfoCache::run()
{
    while (! threadShouldExit())
    {
        juce::File next;

        {
            const juce::ScopedLock sl (lock);

            if (! queue.empty())
            {
                next = queue.back();
                queue.pop_back();
            }
        }

        if (next == juce::File())
        {
            wake.wait (400);
            continue;
        }

        const auto k = keyOf (next);
        const auto stamp = soundinfo::stampOf (next);
        bool fresh = false;

        {
            const juce::ScopedLock sl (lock);
            const auto it = known.find (k);
            fresh = it != known.end() && it->second.analysed && it->second.stamp == stamp;
        }

        // só na nuvem (OneDrive): abrir baixaria o arquivo. Fica conferido nesta sessão, sem leitura e sem cache.
        const bool cloud = ! fresh && soundinfo::isCloudOnly (next);
        SoundInfo info;

        if (! fresh && ! cloud)
        {
            info = soundinfo::analyse (next, &stopping);
            info.stamp = stamp;
        }

        if (threadShouldExit() || stopping)
            break;

        bool drained = false, saveNow = false;

        {
            const juce::ScopedLock sl (lock);
            queued.erase (k);
            settled.insert (k);

            if (! fresh && ! cloud && ((int) known.size() < maxEntries || known.count (k) > 0))
            {
                known[k] = info;
                paths[k] = next.getFullPathName();
                ++unsaved;
            }

            drained = queue.empty();
            saveNow = unsaved >= 50 || (drained && unsaved > 0);
        }

        if (saveNow)
            save();   // nesta thread: a tela não para montando o texto

        // a fila esvaziando também avisa: quem pediu mais do que cabia (o filtro do navegador) pede o resto
        if (! fresh || drained)
            triggerAsyncUpdate();
    }
}

void SoundInfoCache::handleAsyncUpdate()
{
    if (onAnalysed)
        onAnalysed();
}

void SoundInfoCache::save()
{
    const juce::ScopedLock one (saveLock);
    std::map<juce::String, SoundInfo> copy;
    std::map<juce::String, juce::String> copyPaths;

    {
        const juce::ScopedLock sl (lock);

        if (unsaved == 0 && file.existsAsFile())
            return;

        copy = known;   // a cópia sai rápido; o texto é montado fora da trava
        copyPaths = paths;
        unsaved = 0;
    }

    juce::Array<juce::var> sounds;

    {
        for (auto& [k, info] : copy)
        {
            if (! info.analysed)
                continue;

            auto* o = new juce::DynamicObject();
            o->setProperty ("path", copyPaths.count (k) > 0 ? copyPaths.at (k) : k);
            o->setProperty ("stamp", info.stamp);
            o->setProperty ("readable", info.readable);
            o->setProperty ("seconds", info.seconds);
            o->setProperty ("bpm", info.bpm);
            o->setProperty ("bpmSource", info.bpmSource);
            o->setProperty ("root", info.keyRoot);
            o->setProperty ("major", info.keyMajor);
            o->setProperty ("note", info.isNote);
            o->setProperty ("keySource", info.keySource);
            o->setProperty ("role", info.role);
            sounds.add (juce::var (o));
        }
    }

    if (file == juce::File())
        return;

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "whynot-sons");
    root->setProperty ("version", 1);
    root->setProperty ("sounds", sounds);

    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (file);

    if (temp.getFile().replaceWithText (juce::JSON::toString (juce::var (root), juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::none))))
        temp.overwriteTargetFileWithTemporary();
}

void SoundInfoCache::load()
{
    if (! file.existsAsFile())
        return;

    const auto json = juce::JSON::parse (file);

    if (json["format"].toString() != "whynot-sons")
        return;

    if (auto* sounds = json["sounds"].getArray())
    {
        const juce::ScopedLock sl (lock);

        for (auto& s : *sounds)
        {
            const auto path = s["path"].toString();

            if (path.isEmpty() || (int) known.size() >= maxEntries)
                continue;

            SoundInfo info;
            info.analysed = true;
            info.stamp = s["stamp"].toString();
            info.readable = (bool) s["readable"];
            info.seconds = (double) s["seconds"];
            info.bpm = (double) s["bpm"];
            info.bpmSource = s["bpmSource"].toString();
            info.keyRoot = juce::jlimit (-1, 11, (int) s["root"]);
            info.keyMajor = (bool) s["major"];
            info.isNote = (bool) s["note"];
            info.keySource = s["keySource"].toString();
            info.role = s["role"].toString();

            const auto k = juce::File (path).getFullPathName().toLowerCase();
            known[k] = info;
            paths[k] = path;
        }
    }
}

} // namespace whynot
