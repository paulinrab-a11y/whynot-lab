#include "WhynotSampler.h"

namespace whynot
{

const char* WhynotSamplerPlugin::xmlTypeName = "whynotSampler";

WhynotSamplerPlugin::WhynotSamplerPlugin (te::PluginCreationInfo info) : te::Plugin (info)
{
    voiceKey.fill (-1);
    startTimer (100);
}

WhynotSamplerPlugin::~WhynotSamplerPlugin()
{
    stopTimer();
    notifyListenersOfDeletion();
}

//==============================================================================
void WhynotSamplerPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    // Pode rodar enquanto o grafo antigo ainda toca este plugin: só avisamos a
    // thread de áudio, que prepara as vozes ela mesma no próximo bloco.
    requestedSampleRate = info.sampleRate;
    needsPrepare = true;
}

void WhynotSamplerPlugin::deinitialise() {}

void WhynotSamplerPlugin::setSample (std::shared_ptr<const audio::SampleData> data, std::vector<Layer> layers)
{
    auto ref = std::make_unique<SampleRef>();
    ref->data = std::move (data);

    for (auto& l : layers)
        if (l.data != nullptr && ref->numLayers < maxLayers)
            ref->layers[(size_t) ref->numLayers++] = { std::move (l.data), juce::jlimit (0.0f, 1.0f, l.minVelocity) };

    sampleLoaded = ref->data != nullptr;
    pendingSample = std::move (ref);
    tryPublishPending();
}

void WhynotSamplerPlugin::setChannel (int newRootKey, bool newOneShot, bool newCutItself, bool newMono, float newGlideMs)
{
    rootKey = juce::jlimit (0, 127, newRootKey);
    oneShot = newOneShot;
    cutItself = newCutItself;
    mono = newMono;
    glideMs = juce::jlimit (0.0f, 2000.0f, newGlideMs);
}

void WhynotSamplerPlugin::setShape (const SamplerShape& s)
{
    // A voz saneia de novo (NaN, faixas) a cada nota; aqui só guardamos.
    tuneSemis = s.tuneSemis;
    attackMs = s.attackMs;
    decayMs = s.decayMs;
    sustain = s.sustain;
    pitchDropSemis = s.pitchDropSemis;
    pitchDropMs = s.pitchDropMs;
    startFraction = s.start;
    loop = s.loop;
    loopStart = s.loopStart;
    loopEnd = s.loopEnd;
    loopXfadeMs = s.loopXfadeMs;
    releaseMs = s.releaseMs;
    bendRange = std::isfinite (s.bendRange) ? juce::jlimit (0.0f, audio::ChannelPitch::maxRange, s.bendRange) : 2.0f;
}

void WhynotSamplerPlugin::setSlices (const std::vector<float>& starts, int firstKey)
{
    // O Projector chama a cada mudança do projeto: só reescreve se as fatias mudaram.
    if (starts == publishedSlices && firstKey == publishedSliceKey)
        return;

    publishedSlices = starts;
    publishedSliceKey = firstKey;
    const int n = juce::jmin ((int) starts.size(), maxSlices);

    numSlices.store (0, std::memory_order_release);

    for (int i = 0; i < n; ++i)
        sliceAt[(size_t) i].store (juce::jlimit (0.0f, 1.0f, starts[(size_t) i]), std::memory_order_relaxed);

    sliceKey.store (juce::jlimit (0, 127, firstKey), std::memory_order_relaxed);
    numSlices.store (n, std::memory_order_release);
}

audio::VoiceParams WhynotSamplerPlugin::voiceParams() const noexcept
{
    // Cada atomic é lido sozinho: uma mudança no meio pode misturar valores velho e novo
    // numa nota, o que não estala nem quebra nada.
    audio::VoiceParams params;
    params.releaseMs = oneShot.load() ? 5.0f : releaseMs.load();   // one-shot: a saída curta do parar e do cortar
    params.glideMs = glideMs.load();
    params.tuneSemis = tuneSemis.load();
    params.attackMs = attackMs.load();
    params.decayMs = decayMs.load();
    params.sustain = sustain.load();
    params.pitchEnvSemis = pitchDropSemis.load();
    params.pitchEnvMs = pitchDropMs.load();
    params.startFraction = startFraction.load();
    params.loop = loop.load();
    params.loopStart = loopStart.load();
    params.loopEnd = loopEnd.load();
    params.loopXfadeMs = loopXfadeMs.load();
    return params;
}

void WhynotSamplerPlugin::tryPublishPending()
{
    // "O mais novo vence": se a troca não entrar agora (áudio parado), fica
    // guardada aqui e o Timer tenta de novo.
    if (pendingSample == nullptr)
        return;

    // Vozes soando: primeiro elas saem (3 ms, na thread de áudio, ainda no sample velho); a troca vem no
    // próximo Timer. Com o áudio parado ou já em silêncio, troca na hora.
    if (! voicesSilent.load() && audioRunning)
    {
        fadeForSwap = true;
        return;
    }

    sampleSlot.publish (pendingSample);   // o fadeForSwap a thread de áudio limpa, quando vê o sample novo
}

void WhynotSamplerPlugin::timerCallback()
{
    // Os blocos andaram desde o último Timer? Senão o áudio está parado (ou é um render que ainda não começou).
    const auto blocks = blocksRendered.load();
    audioRunning = blocks != blocksSeen;
    blocksSeen = blocks;

    sampleSlot.collectGarbage();
    tryPublishPending();
}

//==============================================================================
void WhynotSamplerPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    juce::ScopedNoDenormals noDenormals;
    blocksRendered.fetch_add (1, std::memory_order_relaxed);

    if (needsPrepare.exchange (false))
    {
        audioSampleRate = requestedSampleRate.load();

        for (auto& v : voices)
            v.prepare (audioSampleRate);

        pitch.prepare (audioSampleRate);
        voiceKey.fill (-1);
        numHeld = 0;
        monoVoice = monoKey = -1;
    }

    // Mono ligado ou desligado com notas soando: a pilha de teclas recomeça vazia,
    // senão a próxima nota podia deslizar de uma tecla que já foi solta.
    if (const bool isMono = mono.load(); isMono != lastMono)
    {
        lastMono = isMono;
        numHeld = 0;
        monoVoice = monoKey = -1;
    }

    // O alcance do bend: o do canal quando ele muda; entre uma mudança e outra, um RPN 0 do MIDI pode trocar. Parar ou
    // pular o transporte volta ao do canal (as curvas viram pitch wheel nele).
    if (fc.bufferForMidiMessages != nullptr && fc.bufferForMidiMessages->isAllNotesOff)
        appliedBendRange = -1.0f;

    if (const float range = bendRange.load(); range != appliedBendRange)
    {
        appliedBendRange = range;
        pitch.setRange (range);
    }

    auto& buffer = *fc.destBuffer;
    const int start = fc.bufferStartSample;
    const int num = fc.bufferNumSamples;
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, start, num);

    const auto* ref = sampleSlot.current();
    const auto* data = ref != nullptr ? ref->data.get() : nullptr;

    if (ref != lastRef)   // a troca aconteceu: notas novas voltam a valer
    {
        lastRef = ref;
        fadeForSwap.store (false);
        swapFading = false;
        // uma voz que sobrou (a troca sem fade, com o áudio recomeçando) continua na camada se ela está no conjunto novo
        // (o cache devolve o mesmo sample para o arquivo que não mudou); senão, lê o sample do canal. Só compara o endereço.
        for (auto& own : voiceData)
        {
            bool kept = false;

            for (int k = 0; ref != nullptr && k < ref->numLayers && ! kept; ++k)
                kept = own != nullptr && ref->layers[(size_t) k].data.get() == own;

            if (! kept)
                own = nullptr;
        }
    }

    if (data == nullptr || numChannels == 0)
    {
        voicesSilent.store (true);
        swapFading = false;
        return;
    }

    // Um sample novo espera: as vozes saem em 3 ms, ainda neste sample (o velho), e notas novas esperam a troca.
    const bool swapping = fadeForSwap.load();

    if (swapping && ! swapFading)
        for (auto& v : voices)
            v.fadeOut (3.0f);

    swapFading = swapping;

    float* out[2] = { buffer.getWritePointer (0), buffer.getWritePointer (numChannels > 1 ? 1 : 0) };
    int position = 0;

    if (auto* midi = fc.bufferForMidiMessages)
    {
        if (midi->isAllNotesOff)
        {
            releaseAll();
            pendingCount = 0;
            pitch.reset();   // transporte parou ou pulou: o tom volta ao meio (a nota seguinte manda o dela)
        }

        for (auto& m : *midi)
        {
            const int when = juce::jlimit (position, num,
                                           (int) std::lround ((m.getTimeStamp() + fc.midiBufferOffset) * audioSampleRate));

            renderVoices (*data, out, numChannels, start + position, when - position);
            position = when;

            if (m.isNoteOn())
            {
                const float pan = takePan (sampleClock + when);   // também no swapping, para a fila andar junto

                if (! swapping)
                    noteOn (m.getNoteNumber(), m.getFloatVelocity(), *ref, pan);
            }
            else if (m.isNoteOff())
                noteOff (m.getNoteNumber(), *ref);
            else if (m.isController() && m.getControllerNumber() == 10)
            {
                // o pan da nota que vem neste instante (passo, 26/10); fila cheia: o mais velho sai
                if (pendingCount == (int) pendingPans.size())
                {
                    std::move (pendingPans.begin() + 1, pendingPans.end(), pendingPans.begin());
                    --pendingCount;
                }

                pendingPans[(size_t) pendingCount++] = { sampleClock + when, juce::jlimit (-1.0f, 1.0f, (m.getControllerValue() - 64) / 63.0f) };
            }
            else if (m.isPitchWheel())
                pitch.pitchWheel (m.getPitchWheelValue());
            else if (m.isAllSoundOff())
                chokeAll();   // choke (outro canal do grupo tocou) ou parada total: corte de 3 ms
            else if (m.isAllNotesOff())
                releaseAll();
            else if (m.isController())
                pitch.controller (m.getControllerNumber(), m.getControllerValue());   // CC1, o RPN 0 do alcance, o reset
        }
    }

    renderVoices (*data, out, numChannels, start + position, num - position);
    sampleClock += num;

    bool anyActive = false;

    for (auto& v : voices)
        anyActive = anyActive || v.isActive();

    voicesSilent.store (! anyActive);
}

void WhynotSamplerPlugin::renderVoices (const audio::SampleData& data, float* const* out, int numChannels,
                                        int start, int num) noexcept
{
    if (num <= 0)
        return;

    // Em pedaços do tamanho do buffer do desvio de tom; em descanso (sem bend nem vibrato) as vozes não recebem
    // desvio nenhum e soam bit a bit como antes.
    for (int done = 0; done < num;)
    {
        const int n = juce::jmin (num - done, (int) pitchOffsets.size());
        const float* bent = pitch.fill (pitchOffsets.data(), n) ? pitchOffsets.data() : nullptr;
        float* offset[2] = { out[0] + start + done, out[1] + start + done };

        for (int i = 0; i < numVoices; ++i)
        {
            if (! voices[(size_t) i].isActive())
                continue;

            const auto* own = voiceData[(size_t) i];
            voices[(size_t) i].render (own != nullptr ? *own : data, offset, numChannels, n, bent);

            if (! voices[(size_t) i].isActive())
                voiceKey[(size_t) i] = -1;
        }

        done += n;
    }
}

bool WhynotSamplerPlugin::anyVoiceActive() const noexcept
{
    return std::any_of (voices.begin(), voices.end(), [] (const audio::SampleVoice& v) { return v.isActive(); });
}

int WhynotSamplerPlugin::allocateVoice() noexcept
{
    // Voz livre; se não houver, rouba a mais antiga.
    int chosen = 0;
    juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();

    for (int i = 0; i < numVoices; ++i)
    {
        if (! voices[(size_t) i].isActive())
        {
            chosen = i;
            break;
        }

        if (voiceStart[(size_t) i] < oldest)
        {
            oldest = voiceStart[(size_t) i];
            chosen = i;
        }
    }

    return chosen;
}

void WhynotSamplerPlugin::pushHeld (int key, float velocity) noexcept
{
    removeHeld (key);

    if (numHeld == (int) held.size())   // cheia: esquece a mais antiga
    {
        for (int i = 1; i < numHeld; ++i)
        {
            held[(size_t) i - 1] = held[(size_t) i];
            heldVelocity[(size_t) i - 1] = heldVelocity[(size_t) i];
        }

        --numHeld;
    }

    held[(size_t) numHeld] = key;
    heldVelocity[(size_t) numHeld] = velocity;
    ++numHeld;
}

void WhynotSamplerPlugin::removeHeld (int key) noexcept
{
    for (int i = 0; i < numHeld; ++i)
    {
        if (held[(size_t) i] == key)
        {
            for (int j = i + 1; j < numHeld; ++j)
            {
                held[(size_t) j - 1] = held[(size_t) j];
                heldVelocity[(size_t) j - 1] = heldVelocity[(size_t) j];
            }

            --numHeld;
            return;
        }
    }
}

float WhynotSamplerPlugin::takePan (std::int64_t now) noexcept
{
    // Joga fora o que chegou antes deste instante (um CC sem nota, o "último valor" do começo do play);
    // o da frente vale se chegou junto com esta nota.
    int drop = 0;

    while (drop < pendingCount && pendingPans[(size_t) drop].at < now - 8)
        ++drop;

    float pan = 0.0f;

    if (drop < pendingCount && pendingPans[(size_t) drop].at <= now)
        pan = pendingPans[(size_t) drop++].pan;

    if (drop > 0)
    {
        std::move (pendingPans.begin() + drop, pendingPans.begin() + pendingCount, pendingPans.begin());
        pendingCount -= drop;
    }

    return pan;
}

void WhynotSamplerPlugin::noteOn (int key, float velocity, const SampleRef& ref, float pan) noexcept
{
    auto params = voiceParams();
    params.pan = pan;   // o CC 10 que chegou junto com esta nota vale só para ela
    const auto& data = *ref.data;

    // Fatiador: cada tecla toca uma fatia, no tom original do sample, até a fatia seguinte.
    // Sem mono nem glide; "cortar a si mesmo" continua valendo.
    if (const int n = numSlices.load (std::memory_order_acquire); n > 0)
    {
        const int index = key - sliceKey.load (std::memory_order_relaxed);

        if (index < 0 || index >= n)
            return;   // tecla fora das fatias: nada toca

        pitch.noteStarts (! anyVoiceActive());   // o bend e a roda que chegaram junto com a nota valem já (sem a rampa)

        auto sliceParams = params;
        sliceParams.regionStart = sliceAt[(size_t) index].load (std::memory_order_relaxed);
        sliceParams.regionEnd = index + 1 < n ? sliceAt[(size_t) index + 1].load (std::memory_order_relaxed) : 1.0f;

        if (cutItself.load())
            for (auto& v : voices)
                if (v.isActive())
                    v.noteOff();

        const int chosen = allocateVoice();
        voices[(size_t) chosen].noteOn (data.rootKey, velocity, false, sliceParams);
        voiceKey[(size_t) chosen] = key;
        voiceData[(size_t) chosen] = nullptr;   // fatiado: só o sample do canal
        voiceStart[(size_t) chosen] = ++voiceCounter;
        return;
    }

    // O bend e a roda que chegaram junto com a nota valem já, sem a rampa; com o canal calado, o vibrato recomeça.
    pitch.noteStarts (! anyVoiceActive());

    // A camada da força (bloco 4f), e a nota de raiz do canal soa no tom original dela.
    const auto& layer = ref.forVelocity (velocity);
    const auto* own = &layer == &data ? nullptr : &layer;
    const int sampleKey = key - rootKey.load() + layer.rootKey;

    if (mono.load())
    {
        // Legato: chegou nota nova com outra ainda segurada -> desliza, sem reiniciar o sample.
        const bool legato = monoVoice >= 0 && voices[(size_t) monoVoice].isActive() && numHeld > 0;
        pushHeld (key, velocity);

        if (legato)
        {
            // deslizando, a voz continua na camada em que começou; sem glide (0 ms) a nota recomeça, já na camada da força dela
            if (params.glideMs <= 0.0f)
                voiceData[(size_t) monoVoice] = own;

            const auto* current = voiceData[(size_t) monoVoice];
            voices[(size_t) monoVoice].noteOn (key - rootKey.load() + (current != nullptr ? current : &data)->rootKey, velocity, true, params);
            voiceKey[(size_t) monoVoice] = key;
            monoKey = key;
            return;
        }

        // Sem legato: a nota anterior sai com o release (sem estalo) e a nova começa noutra voz.
        if (monoVoice >= 0 && voices[(size_t) monoVoice].isActive())
            voices[(size_t) monoVoice].noteOff();

        const int v = allocateVoice();
        voices[(size_t) v].noteOn (sampleKey, velocity, false, params);
        voiceKey[(size_t) v] = key;
        voiceData[(size_t) v] = own;
        voiceStart[(size_t) v] = ++voiceCounter;
        monoVoice = v;
        monoKey = key;
        return;
    }

    // "Cortar a si mesmo": a nota anterior sai com o release curto (5 ms), sem estalo,
    // e a nova começa numa voz livre. Evita 808 sobreposto somando de fase.
    if (cutItself.load())
        for (auto& v : voices)
            if (v.isActive())
                v.noteOff();

    const int chosen = allocateVoice();
    voices[(size_t) chosen].noteOn (sampleKey, velocity, false, params);
    voiceKey[(size_t) chosen] = key;
    voiceData[(size_t) chosen] = own;
    voiceStart[(size_t) chosen] = ++voiceCounter;
}

void WhynotSamplerPlugin::noteOff (int key, const SampleRef& ref) noexcept
{
    const auto& data = *ref.data;

    if (mono.load() && numSlices.load (std::memory_order_relaxed) == 0)
    {
        removeHeld (key);

        if (key != monoKey || monoVoice < 0)
            return;   // soltou uma nota que já não soa (fim do legato de um slide)

        if (numHeld > 0)
        {
            // ainda há tecla segurada: volta deslizando para ela
            const auto params = voiceParams();
            const int back = held[(size_t) numHeld - 1];

            if (params.glideMs <= 0.0f)   // sem glide a nota de volta recomeça: na camada da força dela
            {
                const auto& layer = ref.forVelocity (heldVelocity[(size_t) numHeld - 1]);
                voiceData[(size_t) monoVoice] = &layer == &data ? nullptr : &layer;
            }

            const auto* current = voiceData[(size_t) monoVoice];
            voices[(size_t) monoVoice].noteOn (back - rootKey.load() + (current != nullptr ? current : &data)->rootKey,
                                               heldVelocity[(size_t) numHeld - 1], true, params);
            voiceKey[(size_t) monoVoice] = back;
            monoKey = back;
        }
        else if (! oneShot.load())
        {
            voices[(size_t) monoVoice].noteOff();
            monoKey = -1;
        }
        else
        {
            voices[(size_t) monoVoice].leaveLoop();   // one-shot com laço: solta e toca até o fim
        }

        return;
    }

    if (oneShot.load())
    {
        // bateria: o sample toca até o fim; com laço, a nota solta sai dele e segue até o fim
        for (int i = 0; i < numVoices; ++i)
            if (voiceKey[(size_t) i] == key)
                voices[(size_t) i].leaveLoop();

        return;
    }

    for (int i = 0; i < numVoices; ++i)
        if (voiceKey[(size_t) i] == key)
            voices[(size_t) i].noteOff();
}

void WhynotSamplerPlugin::chokeAll() noexcept
{
    for (auto& v : voices)
        v.fadeOut (3.0f);

    numHeld = 0;
    monoKey = -1;
}

void WhynotSamplerPlugin::releaseAll() noexcept
{
    // Parar ou pular no tempo: tudo sai com o release curto, sem estalo.
    for (auto& v : voices)
        v.noteOff();

    numHeld = 0;
    monoKey = -1;
}

} // namespace whynot
