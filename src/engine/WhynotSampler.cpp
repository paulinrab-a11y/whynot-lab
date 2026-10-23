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

void WhynotSamplerPlugin::setSample (std::shared_ptr<const audio::SampleData> data)
{
    pendingSample = std::make_unique<const SampleRef> (SampleRef { std::move (data) });
    sampleLoaded = pendingSample->data != nullptr;
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
            releaseAll();

        for (auto& m : *midi)
        {
            const int when = juce::jlimit (position, num,
                                           (int) std::lround ((m.getTimeStamp() + fc.midiBufferOffset) * audioSampleRate));

            renderVoices (*data, out, numChannels, start + position, when - position);
            position = when;

            if (m.isNoteOn())
            {
                if (! swapping)
                    noteOn (m.getNoteNumber(), m.getFloatVelocity(), *data);
            }
            else if (m.isNoteOff())
                noteOff (m.getNoteNumber(), *data);
            else if (m.isAllSoundOff())
                chokeAll();   // choke (outro canal do grupo tocou) ou parada total: corte de 3 ms
            else if (m.isAllNotesOff())
                releaseAll();
        }
    }

    renderVoices (*data, out, numChannels, start + position, num - position);

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

    float* offset[2] = { out[0] + start, out[1] + start };

    for (int i = 0; i < numVoices; ++i)
    {
        if (! voices[(size_t) i].isActive())
            continue;

        voices[(size_t) i].render (data, offset, numChannels, num);

        if (! voices[(size_t) i].isActive())
            voiceKey[(size_t) i] = -1;
    }
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

void WhynotSamplerPlugin::noteOn (int key, float velocity, const audio::SampleData& data) noexcept
{
    const auto params = voiceParams();

    // Fatiador: cada tecla toca uma fatia, no tom original do sample, até a fatia seguinte.
    // Sem mono nem glide; "cortar a si mesmo" continua valendo.
    if (const int n = numSlices.load (std::memory_order_acquire); n > 0)
    {
        const int index = key - sliceKey.load (std::memory_order_relaxed);

        if (index < 0 || index >= n)
            return;   // tecla fora das fatias: nada toca

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
        voiceStart[(size_t) chosen] = ++voiceCounter;
        return;
    }

    // A nota de raiz do canal soa no tom original do sample.
    const int sampleKey = key - rootKey.load() + data.rootKey;

    if (mono.load())
    {
        // Legato: chegou nota nova com outra ainda segurada -> desliza, sem reiniciar o sample.
        const bool legato = monoVoice >= 0 && voices[(size_t) monoVoice].isActive() && numHeld > 0;
        pushHeld (key, velocity);

        if (legato)
        {
            voices[(size_t) monoVoice].noteOn (sampleKey, velocity, true, params);
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
    voiceStart[(size_t) chosen] = ++voiceCounter;
}

void WhynotSamplerPlugin::noteOff (int key, const audio::SampleData& data) noexcept
{
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
            voices[(size_t) monoVoice].noteOn (back - rootKey.load() + data.rootKey, heldVelocity[(size_t) numHeld - 1], true, params);
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
