#include "WhynotPiano.h"

namespace whynot
{

const char* WhynotPianoPlugin::xmlTypeName = "whynotPiano";

namespace
{
    constexpr int chunkFrames = 4096;         // quanto o streamer lê de uma vez
    constexpr int renderChunk = 1024;         // render offline: enche os anéis a cada 1024 quadros
    constexpr int softVoices = 56;            // acima disso a voz mais fraca sai em 4 ms: sobram vozes para as notas novas
    constexpr float retriggerSeconds = 0.2f;  // a mesma tecla de novo: a nota anterior some em 0,2 s
    constexpr float outputGain = 0.5f;        // -6 dB: o Salamander vem alto (acorde de 3 notas com força 100 perto de 0 dBFS)
    constexpr float driveK = 2.5f;            // saturação: tanh(kx)/k, igual a x nos sinais baixos
}

/** Enche os anéis das vozes. Uma passada a cada ~2 ms (o Windows pode esticar para ~15 ms; o anel
    guarda 0,74 s e o começo na memória 0,37 s, então sobra folga). */
class WhynotPianoPlugin::Streamer : public juce::Thread
{
public:
    explicit Streamer (WhynotPianoPlugin& p) : juce::Thread ("WHYNOT Piano"), owner (p) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            {
                const std::lock_guard<std::mutex> l (owner.streamLock);
                owner.service();
            }

            wait (2);
        }
    }

private:
    WhynotPianoPlugin& owner;
};

WhynotPianoPlugin::WhynotPianoPlugin (te::PluginCreationInfo info) : te::Plugin (info)
{
    // Tudo alocado aqui (thread de mensagem): 64 vozes com anel de 256 KB cada.
    library = piano::load (loadError);
    voices = std::make_unique<Voice[]> ((size_t) maxVoices);
    cursors.resize ((size_t) maxVoices);
    chunk.setSize (2, chunkFrames);

    if (library != nullptr)
    {
        streamer = std::make_unique<Streamer> (*this);
        streamer->startThread (juce::Thread::Priority::high);
    }
}

WhynotPianoPlugin::~WhynotPianoPlugin()
{
    if (streamer != nullptr)
        streamer->stopThread (2000);

    notifyListenersOfDeletion();
}

void WhynotPianoPlugin::applyChannel (const Channel& c)
{
    const auto* p = audio::findPianoPreset (c.instrumentPreset.toRawUTF8());

    if (p == nullptr)
        p = &audio::pianoPresets[0];

    velocityCurve.store (p->velocityCurve, std::memory_order_relaxed);
    toneHz.store (p->toneHz, std::memory_order_relaxed);
    width.store (p->width, std::memory_order_relaxed);
    releaseScale.store (p->releaseScale, std::memory_order_relaxed);
    drive.store (p->drive, std::memory_order_relaxed);
    hammer.store (p->hammer, std::memory_order_relaxed);
    tune.store (std::isfinite (c.tune) ? juce::jlimit (-24.0f, 24.0f, c.tune) : 0.0f, std::memory_order_relaxed);
    bendRange.store ((float) juce::jlimit (1, 24, c.bendRange), std::memory_order_relaxed);   // = curves::bendRangeFor
}

void WhynotPianoPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    // O Tracktion só chama initialise() sem nenhum grafo usando o plugin. Mesma taxa: as notas seguem.
    if (info.sampleRate > 0.0 && info.sampleRate != preparedRate)
    {
        preparedRate = info.sampleRate;

        for (int i = 0; i < maxVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            v.voice.prepare (preparedRate);
            v.stream.stop();
            v.key = -1;
            v.keyDown = v.held = v.isRelease = v.fading = false;
        }

        pedalDown = false;
        std::memset (lowpass, 0, sizeof (lowpass));
        pitch.prepare (preparedRate);
    }

    prepared = preparedRate > 0.0;
}

//==============================================================================
void WhynotPianoPlugin::service()
{
    const auto& set = library->set;

    for (int i = 0; i < maxVoices; ++i)
    {
        auto& stream = voices[(size_t) i].stream;
        auto& cursor = cursors[(size_t) i];
        int zone = -1;
        std::int64_t from = 0, to = 0;

        while (audio::planFill (stream, set, cursor, chunkFrames, zone, from, to))
        {
            const int n = (int) (to - from);

            if (! library->read (zone, from, n, chunk))
                chunk.clear (0, n);

            const float* l = chunk.getReadPointer (0);
            const float* r = chunk.getReadPointer (1);

            for (int f = 0; f < n; ++f)
            {
                auto* d = stream.slot (from + f);
                d[0] = l[f];
                d[1] = r[f];
            }

            audio::finishFill (stream, cursor, to);
        }
    }
}

//==============================================================================
void WhynotPianoPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& buffer = *fc.destBuffer;
    const int start = fc.bufferStartSample;
    const int num = fc.bufferNumSamples;
    const int channels = buffer.getNumChannels();

    if (num <= 0 || channels == 0)
        return;

    juce::ScopedNoDenormals noDenormals;

    for (int c = 0; c < channels; ++c)
        buffer.clear (c, start, num);

    if (library == nullptr || ! prepared)
        return;

    rendering = fc.isRendering;

    Settings s;
    s.velocityCurve = velocityCurve.load (std::memory_order_relaxed);
    s.toneHz = toneHz.load (std::memory_order_relaxed);
    s.width = width.load (std::memory_order_relaxed);
    s.releaseScale = releaseScale.load (std::memory_order_relaxed);
    s.drive = drive.load (std::memory_order_relaxed);
    s.tune = tune.load (std::memory_order_relaxed);
    s.hammer = hammer.load (std::memory_order_relaxed);

    // o alcance do bend: o do canal quando ele muda; entre uma mudança e outra, um RPN 0 do MIDI pode trocar. Parar ou
    // pular o transporte volta ao do canal (as curvas viram pitch wheel nele).
    if (fc.bufferForMidiMessages != nullptr && fc.bufferForMidiMessages->isAllNotesOff)
        appliedBendRange = -1.0f;

    if (const float range = bendRange.load (std::memory_order_relaxed); range != appliedBendRange)
    {
        appliedBendRange = range;
        pitch.setRange (range);
    }

    int numEvents = 0;

    auto push = [&] (int offset, Event::Type type, int key, int value)
    {
        if (numEvents < maxEvents)
        {
            auto& e = events[(size_t) numEvents];
            e.offset = offset;
            e.index = numEvents;
            e.type = type;
            e.key = key;
            e.value = value;
            ++numEvents;
        }
    };

    if (auto* midi = fc.bufferForMidiMessages)
    {
        if (midi->isAllNotesOff)
        {
            push (0, Event::Type::allNotesOff, 0, 0);
            push (0, Event::Type::controller, 121, 0);   // o tom volta ao meio (a nota seguinte manda o dela)
        }

        for (auto& m : *midi)
        {
            const int offset = juce::jlimit (0, num - 1, juce::roundToInt ((m.getTimeStamp() + fc.midiBufferOffset) * preparedRate));

            if (m.isNoteOn())
                push (offset, Event::Type::noteOn, m.getNoteNumber(), m.getVelocity());
            else if (m.isNoteOff())
                push (offset, Event::Type::noteOff, m.getNoteNumber(), 0);
            else if (m.isSustainPedalOn())
                push (offset, Event::Type::sustain, 0, 1);
            else if (m.isSustainPedalOff())
                push (offset, Event::Type::sustain, 0, 0);
            else if (m.isResetAllControllers())
            {
                push (offset, Event::Type::sustain, 0, 0);
                push (offset, Event::Type::controller, 121, 0);
            }
            else if (m.isAllSoundOff())
                push (offset, Event::Type::allSoundOff, 0, 0);
            else if (m.isAllNotesOff())
                push (offset, Event::Type::allNotesOff, 0, 0);
            else if (m.isPitchWheel())
                push (offset, Event::Type::pitchWheel, 0, m.getPitchWheelValue());
            else if (m.isController())
                push (offset, Event::Type::controller, m.getControllerNumber(), m.getControllerValue());   // CC1 e o RPN 0
        }

        // em ordem de tempo; no mesmo quadro, na ordem em que chegaram (std::sort não aloca)
        std::sort (events.begin(), events.begin() + numEvents, [] (const Event& a, const Event& b)
                   { return a.offset != b.offset ? a.offset < b.offset : a.index < b.index; });
    }

    float* left = buffer.getWritePointer (0, start);
    float* right = channels > 1 ? buffer.getWritePointer (1, start) : nullptr;
    blockMissed = 0;
    int pos = 0;

    for (int i = 0; i < numEvents; ++i)
    {
        const auto& e = events[(size_t) i];

        if (e.offset > pos)
        {
            renderVoices (left, right, pos, e.offset);
            pos = e.offset;
        }

        handle (e, clock + e.offset, s);
    }

    renderVoices (left, right, pos, num);
    postProcess (left, right, num, s);
    clock += num;

    if (blockMissed > 0)
        missed.fetch_add (blockMissed, std::memory_order_relaxed);

    int inUse = 0;

    for (int i = 0; i < maxVoices; ++i)
        if (voices[(size_t) i].voice.isActive())
            ++inUse;

    voicesInUse.store (inUse, std::memory_order_relaxed);

    float peak = 0.0f;

    for (int i = 0; i < num; ++i)
        peak = juce::jmax (peak, std::abs (left[i]), right != nullptr ? std::abs (right[i]) : 0.0f);

    level.store (juce::jmax (peak, level.load (std::memory_order_relaxed) * 0.85f), std::memory_order_relaxed);
}

void WhynotPianoPlugin::renderVoices (float* left, float* right, int from, int to)
{
    const auto& set = library->set;

    for (int a = from; a < to; a += renderChunk)
    {
        const int n = std::min (renderChunk, to - a);

        // exportando: sem tempo real, então espera o anel em vez de deixar faltar
        if (rendering)
        {
            const std::lock_guard<std::mutex> l (streamLock);
            service();
        }

        float* outs[2] = { left + a, right != nullptr ? right + a : nullptr };
        const int numOut = right != nullptr ? 2 : 1;

        // o bend e o vibrato do canal; em descanso, nada (o som de antes, bit a bit)
        static_assert (renderChunk <= (int) std::tuple_size_v<decltype (pitchOffsets)>);
        const float* bent = pitch.fill (pitchOffsets.data(), n) ? pitchOffsets.data() : nullptr;

        for (int i = 0; i < maxVoices; ++i)
        {
            auto& v = voices[(size_t) i];

            if (v.voice.isActive())
                v.voice.render (set, v.stream, outs, numOut, n, blockMissed, bent);
        }
    }
}

void WhynotPianoPlugin::postProcess (float* left, float* right, int num, const Settings& s)
{
    // tom: dois polos de passa-baixas por canal (12 dB/oitava); a = 0 deixa passar igual
    const float nyquistSafe = 0.45f * (float) preparedRate;
    const float a = s.toneHz > 0.0f && s.toneHz < nyquistSafe
                      ? std::exp (-juce::MathConstants<float>::twoPi * s.toneHz / (float) preparedRate) : 0.0f;

    // largura e saturação mudam em rampa dentro do bloco (troca de preset sem estalo)
    const float targetWidth = juce::jlimit (0.0f, 1.0f, s.width), targetDrive = juce::jlimit (0.0f, 1.0f, s.drive);
    const float widthStep = (targetWidth - lastWidth) / (float) num, driveStep = (targetDrive - lastDrive) / (float) num;
    float w = lastWidth, d = lastDrive;

    for (int i = 0; i < num; ++i)
    {
        w += widthStep;
        d += driveStep;

        float l = left[i];
        float r = right != nullptr ? right[i] : l;

        lowpass[0][0] = l + a * (lowpass[0][0] - l);
        lowpass[0][1] = lowpass[0][0] + a * (lowpass[0][1] - lowpass[0][0]);
        lowpass[1][0] = r + a * (lowpass[1][0] - r);
        lowpass[1][1] = lowpass[1][0] + a * (lowpass[1][1] - lowpass[1][0]);
        l = lowpass[0][1];
        r = lowpass[1][1];

        const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * w;
        l = mid + side;
        r = mid - side;

        if (d > 0.0f)   // mistura com tanh(kx)/k: em d = 0 sai igual, sem degrau
        {
            l += d * (std::tanh (driveK * l) / driveK - l);
            r += d * (std::tanh (driveK * r) / driveK - r);
        }

        left[i] = l * outputGain;

        if (right != nullptr)
            right[i] = r * outputGain;
    }

    lastWidth = targetWidth;
    lastDrive = targetDrive;
}

//==============================================================================
void WhynotPianoPlugin::handle (const Event& e, std::int64_t now, const Settings& s)
{
    switch (e.type)
    {
        case Event::Type::noteOn:
            noteOn (e.key, e.value, now, s);
            break;

        case Event::Type::noteOff:
            noteOff (e.key, now, s);
            break;

        case Event::Type::sustain:
            if (e.value != 0)
            {
                pedalDown = true;
            }
            else if (pedalDown)
            {
                // os abafadores caem em todas as cordas que o pedal segurava
                pedalDown = false;

                for (int i = 0; i < maxVoices; ++i)
                {
                    auto& v = voices[(size_t) i];

                    if (v.voice.isActive() && v.held)
                    {
                        v.held = false;
                        damp (v, now, s, true);
                    }
                }
            }
            break;

        case Event::Type::allNotesOff:
            // transporte parou ou voltou: solta tudo, sem os samples de release
            pedalDown = false;

            for (int i = 0; i < maxVoices; ++i)
            {
                auto& v = voices[(size_t) i];

                if (v.voice.isActive() && ! v.isRelease && (v.keyDown || v.held))
                {
                    v.keyDown = v.held = false;
                    damp (v, now, s, false);
                }
            }
            break;

        case Event::Type::pitchWheel:
            pitch.pitchWheel (e.value);
            break;

        case Event::Type::controller:
            pitch.controller (e.key, e.value);
            break;

        case Event::Type::allSoundOff:
            pedalDown = false;

            for (int i = 0; i < maxVoices; ++i)
            {
                auto& v = voices[(size_t) i];
                v.voice.fadeOut (5.0f);
                v.keyDown = v.held = false;
            }
            break;
    }
}

void WhynotPianoPlugin::noteOn (int key, int velocity127, std::int64_t now, const Settings& s)
{
    if (key < 0 || key > 127)
        return;

    if (velocity127 <= 0)
    {
        noteOff (key, now, s);
        return;
    }

    const auto& set = library->set;
    const float v01 = std::pow ((float) juce::jlimit (1, 127, velocity127) / 127.0f, juce::jlimit (0.3f, 3.0f, s.velocityCurve));
    const int layer = juce::jlimit (1, 127, juce::roundToInt (v01 * 127.0f));
    const int zone = set.attackFor (key, layer);

    if (zone < 0)
        return;

    // a mesma tecla de novo: a corda é tocada outra vez e a nota anterior some depressa
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& o = voices[(size_t) i];

        if (o.voice.isActive() && ! o.isRelease && o.key == key && (o.keyDown || o.held))
        {
            o.voice.release (retriggerSeconds);
            o.keyDown = o.held = false;
        }
    }

    // o bend e a roda que chegaram junto com a nota valem já, sem a rampa; com o canal calado, o vibrato recomeça
    bool sounding = false;

    for (int i = 0; i < maxVoices && ! sounding; ++i)
        sounding = voices[(size_t) i].voice.isActive();

    pitch.noteStarts (! sounding);

    const int slot = allocate();
    auto& v = voices[(size_t) slot];
    v.voice.start (set, zone, key, v01, s.tune, 1.0f, v.stream, ++generation);
    v.key = key;
    v.velocity01 = v01;
    v.layerVelocity = layer;
    v.keyDown = true;
    v.held = v.isRelease = v.fading = false;
    v.startedAt = now;
    v.order = ++voiceOrder;
    limitPolyphony (slot);
}

void WhynotPianoPlugin::noteOff (int key, std::int64_t now, const Settings& s)
{
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];

        if (v.voice.isActive() && ! v.isRelease && v.key == key && v.keyDown)
        {
            v.keyDown = false;

            if (pedalDown)
                v.held = true;
            else
                damp (v, now, s, true);
        }
    }
}

void WhynotPianoPlugin::damp (Voice& v, std::int64_t now, const Settings& s, bool withReleaseSamples)
{
    const auto& set = library->set;
    const int zone = v.voice.zoneIndex();

    if (zone < 0 || zone >= (int) set.zones.size())
        return;

    v.voice.release (set.zones[(size_t) zone].releaseSeconds * juce::jlimit (0.1f, 4.0f, s.releaseScale));

    if (withReleaseSamples)
    {
        // copia antes: startReleases pode reaproveitar esta voz
        const double heldSeconds = (double) (now - v.startedAt) / preparedRate;
        startReleases (v.key, v.layerVelocity, v.velocity01, heldSeconds, now, s);
    }
}

void WhynotPianoPlugin::startReleases (int key, int layer, float v01, double heldSeconds, std::int64_t now, const Settings& s)
{
    if (key < 0 || key > 127)
        return;

    const auto& set = library->set;

    for (const auto r : set.releases[(size_t) key])
    {
        if (r < 0)
            break;

        const auto& z = set.zones[(size_t) r];

        if (layer < z.loVel || layer > z.hiVel)
            continue;

        if (! s.hammer && z.keyTrack == 0.0f)   // o martelo voltando (não segue a tecla)
            continue;

        // rt_decay: quanto mais tempo a nota soou, mais fraca a ressonância ao soltar
        const float gain = std::pow (10.0f, -z.rtDecayDb * (float) std::max (0.0, heldSeconds) / 20.0f);

        if (gain < 0.001f)
            continue;

        const int slot = allocate();
        auto& v = voices[(size_t) slot];
        v.voice.start (set, r, key, v01, s.tune, gain, v.stream, ++generation);
        v.key = key;
        v.velocity01 = v01;
        v.layerVelocity = layer;
        v.isRelease = true;
        v.keyDown = v.held = v.fading = false;
        v.startedAt = now;
        v.order = ++voiceOrder;
        limitPolyphony (slot);
    }
}

int WhynotPianoPlugin::allocate()
{
    for (int i = 0; i < maxVoices; ++i)
        if (! voices[(size_t) i].voice.isActive())
            return i;

    // Nenhuma livre (raro: limitPolyphony deixa folga). A mais fraca entre as que estão saindo; senão a mais antiga.
    int best = -1;

    for (int i = 0; i < maxVoices; ++i)
    {
        const auto& v = voices[(size_t) i];

        if ((v.voice.isReleasing() || v.isRelease)
            && (best < 0 || v.voice.loudness() < voices[(size_t) best].voice.loudness()))
            best = i;
    }

    if (best < 0)
        for (int i = 0; i < maxVoices; ++i)
            if (best < 0 || voices[(size_t) i].order < voices[(size_t) best].order)
                best = i;

    return best;
}

void WhynotPianoPlugin::limitPolyphony (int justStarted)
{
    int sounding = 0;

    for (int i = 0; i < maxVoices; ++i)
        if (voices[(size_t) i].voice.isActive() && ! voices[(size_t) i].fading)
            ++sounding;

    while (sounding > softVoices)
    {
        // a mais fraca entre as que já estão saindo; senão a mais antiga (nunca a que acabou de começar)
        int victim = -1;

        for (int i = 0; i < maxVoices; ++i)
        {
            const auto& v = voices[(size_t) i];

            if (i != justStarted && v.voice.isActive() && ! v.fading && (v.voice.isReleasing() || v.isRelease)
                && (victim < 0 || v.voice.loudness() < voices[(size_t) victim].voice.loudness()))
                victim = i;
        }

        if (victim < 0)
            for (int i = 0; i < maxVoices; ++i)
            {
                const auto& v = voices[(size_t) i];

                if (i != justStarted && v.voice.isActive() && ! v.fading
                    && (victim < 0 || v.order < voices[(size_t) victim].order))
                    victim = i;
            }

        if (victim < 0)
            break;

        voices[(size_t) victim].voice.fadeOut (4.0f);
        voices[(size_t) victim].fading = true;
        --sounding;
    }
}

} // namespace whynot
