#include "DeckPlayer.h"

#include <limits>

namespace whynot::library
{

// a thread de áudio lê e escreve estes atômicos: com trava escondida, ela poderia esperar
static_assert (std::atomic<double>::is_always_lock_free, "DeckPlayer: atomic<double> precisa ser sem trava");
static_assert (std::atomic<float>::is_always_lock_free, "DeckPlayer: atomic<float> precisa ser sem trava");
static_assert (std::atomic<juce::int64>::is_always_lock_free, "DeckPlayer: atomic<int64> precisa ser sem trava");

//==============================================================================
/** Um arquivo num deck, a partir de um ponto (e, se houver, com um trecho que repete). O anel tem um produtor
    (a thread de leitura) e um consumidor (a thread de áudio); os índices contam quadros desde o começo da
    leitura e nunca voltam. Com loop, o anel continua contínuo: quem volta ao começo do trecho é a leitura. */
struct DeckPlayer::Stream
{
    static constexpr int capacity = 1 << 17;   // quadros por canal: ~2,7 s a 48 kHz
    static constexpr juce::int64 mask = capacity - 1;
    static constexpr int chunk = 8192;
    static constexpr int padding = 8;          // zeros depois do fim: a interpolação lê até 2 quadros à frente
    static constexpr int seam = 96;            // ~2 ms de rampa nas pontas do loop: a emenda não estala

    juce::File file;
    std::unique_ptr<juce::AudioFormatReader> reader;   // só a thread de leitura (e a de mensagem, antes de entregar)
    juce::AudioBuffer<float> scratch;                  // idem
    double fileRate = 48000.0;
    double lengthSec = 0.0;
    juce::int64 startFrame = 0;                        // primeiro quadro do arquivo que entra no anel
    juce::int64 loopStart = 0, loopEnd = 0;            // em quadros do arquivo; loopEnd <= loopStart = sem loop

    std::vector<float> ringL, ringR;
    std::atomic<juce::int64> written { 0 };            // produtor
    std::atomic<juce::int64> released { 0 };           // consumidor: abaixo daqui o produtor pode reescrever
    std::atomic<juce::int64> endFrame { 0 };           // quadros do startFrame até o fim do arquivo (com loop, sem fim)
    std::atomic<bool> complete { false };              // o fim (e os zeros depois dele) já está no anel

    // só a thread de áudio (a tela lê os atômicos)
    double pos = 0.0;                                  // em quadros do anel
    std::atomic<double> positionSec { 0.0 };
    std::atomic<bool> finished { false };

    bool looping() const noexcept   { return loopEnd > loopStart; }

    /** Quadro do arquivo que está no índice k do anel. Com loop, startFrame fica dentro do trecho. */
    juce::int64 fileFrameAt (juce::int64 k) const noexcept
    {
        const auto f = startFrame + k;

        if (! looping() || f < loopEnd)
            return f;

        return loopStart + (f - loopStart) % (loopEnd - loopStart);
    }

    /** Thread de leitura: decodifica mais um pedaço, se couber. false = nada a fazer agora. */
    bool fill()
    {
        if (complete.load (std::memory_order_relaxed))
            return false;

        const auto w = written.load (std::memory_order_relaxed);
        const auto free = (juce::int64) capacity - (w - released.load (std::memory_order_acquire));
        const auto end = endFrame.load (std::memory_order_relaxed);

        if (w >= end)
        {
            if (free < padding)
                return false;

            for (juce::int64 i = 0; i < padding; ++i)
            {
                ringL[(size_t) ((w + i) & mask)] = 0.0f;
                ringR[(size_t) ((w + i) & mask)] = 0.0f;
            }

            written.store (w + padding, std::memory_order_release);
            complete.store (true, std::memory_order_release);
            return true;
        }

        if (free < 2048)
            return false;

        // com loop, cada leitura vai no máximo até o fim do trecho; a próxima recomeça do início dele
        const auto from = fileFrameAt (w);
        const auto limit = looping() ? loopEnd - from : end - w;
        const int n = (int) juce::jmin ((juce::int64) chunk, free, limit);

        if (! reader->read (&scratch, 0, n, from, true, true))
        {
            if (! looping())
            {
                endFrame.store (w, std::memory_order_release);   // arquivo cortado: termina aqui
                return true;
            }

            scratch.clear (0, n);   // com loop não há fim: o trecho que não leu vira silêncio
        }

        auto* l = scratch.getWritePointer (0);
        auto* r = scratch.getWritePointer (1);

        if (looping())
        {
            for (int i = 0; i < n; ++i)
            {
                const auto f = from + i;
                float g = 1.0f;

                if (f - loopStart < seam)
                    g *= ((float) (f - loopStart) + 0.5f) / (float) seam;

                if (loopEnd - f <= seam)
                    g *= ((float) (loopEnd - f) - 0.5f) / (float) seam;

                l[i] *= g;
                r[i] *= g;
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const auto index = (size_t) ((w + i) & mask);
            ringL[index] = l[i];
            ringR[index] = r[i];
        }

        written.store (w + n, std::memory_order_release);
        return true;
    }
};

/** O que cada deck toca. Imutável depois de publicada: mudar qualquer deck é uma cena nova. */
struct DeckPlayer::Scene
{
    std::array<std::shared_ptr<Stream>, numDecks> streams;
};

/** Enche os anéis de todos os decks vivos. Guarda só referências fracas: o deck que sai fecha o arquivo. */
class DeckPlayer::Reader : public juce::Thread
{
public:
    Reader() : juce::Thread ("WHYNOT decks: leitura")   { startThread (juce::Thread::Priority::high); }
    ~Reader() override                                  { stopThread (3000); }

    void add (const std::shared_ptr<Stream>& s)
    {
        {
            const juce::ScopedLock sl (lock);
            streams.push_back (s);
        }

        notify();
    }

    void run() override
    {
        std::vector<std::shared_ptr<Stream>> work;

        while (! threadShouldExit())
        {
            {
                const juce::ScopedLock sl (lock);

                for (auto it = streams.begin(); it != streams.end();)
                {
                    if (auto s = it->lock())
                    {
                        work.push_back (std::move (s));
                        ++it;
                    }
                    else
                    {
                        it = streams.erase (it);
                    }
                }
            }

            bool busy = false;

            for (auto& s : work)
                busy = s->fill() || busy;

            work.clear();   // se era a última referência, o arquivo fecha aqui (nunca na thread de áudio)

            if (! busy)
                wait (5);
        }
    }

private:
    juce::CriticalSection lock;
    std::vector<std::weak_ptr<Stream>> streams;
};

//==============================================================================
DeckPlayer::DeckPlayer (juce::AudioFormatManager& f)
    : formats (f), reader (std::make_unique<Reader>())
{
    for (int d = 0; d < numDecks; ++d)
    {
        gainTarget[(size_t) d].store (d == 0 ? 1.0f : 0.0f);
        gain[(size_t) d].now = gain[(size_t) d].target = d == 0 ? 1.0f : 0.0f;
        deckFade[(size_t) d] = 0.0f;
    }

    mixLeft.assign (maxBlock, 0.0f);
    mixRight.assign (maxBlock, 0.0f);
}

DeckPlayer::~DeckPlayer()
{
    reader = nullptr;   // para a leitura antes de as cenas (e os arquivos) saírem

    // a placa já soltou este callback: o que era só do áudio volta para cá
    const Scene* s = nullptr;

    while (toAudio.pop (s))
        delete s;

    while (garbage.pop (s))
        delete s;

    delete next;
    delete scene;
}

std::shared_ptr<DeckPlayer::Stream> DeckPlayer::openStream (const juce::File& file, double startSeconds, Loop loop)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));

    if (r == nullptr || r->sampleRate <= 0.0 || r->lengthInSamples <= 0)
        return {};

    auto s = std::make_shared<Stream>();
    const auto length = r->lengthInSamples;
    const auto rate = r->sampleRate;
    auto toFrame = [&] (double seconds) { return juce::jlimit ((juce::int64) 0, length, (juce::int64) std::llround (juce::jmax (0.0, seconds) * rate)); };

    s->file = file;
    s->fileRate = rate;
    s->lengthSec = (double) length / rate;
    s->startFrame = toFrame (startSeconds);

    if (loop.active())
    {
        s->loopStart = toFrame (loop.start);
        s->loopEnd = toFrame (loop.end);

        if (s->loopEnd - s->loopStart < 2 * Stream::seam)
            s->loopStart = s->loopEnd = 0;   // curto demais para repetir: toca sem loop
        else if (s->startFrame < s->loopStart || s->startFrame >= s->loopEnd)
            s->startFrame = s->loopStart;
    }

    s->endFrame.store (s->looping() ? std::numeric_limits<juce::int64>::max() : length - s->startFrame);
    s->positionSec.store ((double) s->startFrame / rate);
    s->ringL.assign ((size_t) Stream::capacity, 0.0f);
    s->ringR.assign ((size_t) Stream::capacity, 0.0f);
    s->scratch.setSize (2, Stream::chunk);
    s->reader = std::move (r);

    // ~0,5 s pronto antes de ir para o áudio: o play começa sem esperar o disco
    const auto ready = juce::jmin (s->endFrame.load(), (juce::int64) (0.5 * rate));

    while (s->written.load() < ready && s->fill())
    {}

    return s;
}

//==============================================================================
void DeckPlayer::collectGarbage()
{
    const Scene* old = nullptr;

    while (garbage.pop (old))
    {
        delete old;
        --inFlight;
    }
}

bool DeckPlayer::publishPending()
{
    collectGarbage();

    // inFlight < queueSize garante lugar na fila de descarte para tudo que o áudio devolver
    if (pending == nullptr || inFlight >= queueSize - 1)
        return false;

    if (! toAudio.push (pending.get()))
        return false;

    pending.release();
    ++inFlight;
    return true;
}

void DeckPlayer::publishScene()
{
    if (batchDepth > 0)
    {
        batchDirty = true;
        return;
    }

    auto s = std::make_unique<Scene>();
    s->streams = current;
    pending = std::move (s);   // "o mais novo vence": um pendente velho é descartado aqui, nunca chegou ao áudio
    publishPending();
}

void DeckPlayer::endChange()
{
    if (batchDepth > 0 && --batchDepth == 0 && std::exchange (batchDirty, false))
        publishScene();
}

bool DeckPlayer::load (int deck, const juce::File& file, double startSeconds, Loop loop)
{
    if (! juce::isPositiveAndBelow (deck, numDecks))
        return false;

    auto s = openStream (file, startSeconds, loop);

    if (s == nullptr)
        return false;

    current[(size_t) deck] = s;
    reader->add (s);
    publishScene();
    return true;
}

void DeckPlayer::unload (int deck)
{
    if (juce::isPositiveAndBelow (deck, numDecks) && current[(size_t) deck] != nullptr)
    {
        current[(size_t) deck] = nullptr;
        publishScene();
    }
}

bool DeckPlayer::release (const juce::File& file)
{
    bool found = false;

    for (auto& s : current)
    {
        if (s != nullptr && s->file == file)
        {
            s = nullptr;
            found = true;
        }
    }

    if (found)
        publishScene();

    return found;
}

void DeckPlayer::play()
{
    // depois do fim, play recomeça do início (os decks juntos, no mesmo bloco)
    if (reachedEnd())
    {
        beginChange();

        for (int d = 0; d < numDecks; ++d)
            if (current[(size_t) d] != nullptr)
                seek (d, 0.0);

        endChange();
    }

    playing.store (true);
}

void DeckPlayer::pause()
{
    playing.store (false);
}

void DeckPlayer::seek (int deck, double seconds)
{
    if (juce::isPositiveAndBelow (deck, numDecks) && current[(size_t) deck] != nullptr)
        load (deck, current[(size_t) deck]->file, seconds, loopOf (deck));
}

void DeckPlayer::setLoop (int deck, Loop loop)
{
    if (juce::isPositiveAndBelow (deck, numDecks) && current[(size_t) deck] != nullptr)
        load (deck, current[(size_t) deck]->file, positionSeconds (deck), loop);
}

DeckPlayer::Loop DeckPlayer::loopOf (int deck) const
{
    if (! isLoaded (deck) || ! current[(size_t) deck]->looping())
        return {};

    const auto& s = *current[(size_t) deck];
    return { (double) s.loopStart / s.fileRate, (double) s.loopEnd / s.fileRate };
}

void DeckPlayer::setDeckGain (int deck, float newGain)
{
    if (juce::isPositiveAndBelow (deck, numDecks))
        gainTarget[(size_t) deck].store (juce::jlimit (0.0f, 4.0f, newGain));
}

float DeckPlayer::deckGain (int deck) const
{
    return juce::isPositiveAndBelow (deck, numDecks) ? gainTarget[(size_t) deck].load() : 0.0f;
}

void DeckPlayer::service()
{
    publishPending();

    if (playing.load() && reachedEnd())
        playing.store (false);
}

bool DeckPlayer::isLoaded (int deck) const
{
    return juce::isPositiveAndBelow (deck, numDecks) && current[(size_t) deck] != nullptr;
}

juce::File DeckPlayer::fileOf (int deck) const
{
    return isLoaded (deck) ? current[(size_t) deck]->file : juce::File();
}

double DeckPlayer::lengthSeconds (int deck) const
{
    return isLoaded (deck) ? current[(size_t) deck]->lengthSec : 0.0;
}

double DeckPlayer::positionSeconds (int deck) const
{
    return isLoaded (deck) ? current[(size_t) deck]->positionSec.load (std::memory_order_relaxed) : 0.0;
}

bool DeckPlayer::deckFinished (int deck) const
{
    return isLoaded (deck) && current[(size_t) deck]->finished.load();
}

bool DeckPlayer::reachedEnd() const
{
    bool any = false;

    for (auto& s : current)
    {
        if (s == nullptr)
            continue;

        if (! s->finished.load())
            return false;

        any = true;
    }

    return any;
}

//==============================================================================
void DeckPlayer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr && device->getCurrentSampleRate() > 0.0)
        deviceRate.store (device->getCurrentSampleRate());
}

void DeckPlayer::takeScenes() noexcept
{
    // só a mais nova interessa: as do meio nunca tocaram e voltam direto para o descarte
    const Scene* incoming = nullptr;

    while (toAudio.pop (incoming))
    {
        if (next != nullptr)
            garbage.push (next);   // nunca falha: inFlight < queueSize

        next = incoming;
    }
}

void DeckPlayer::renderStream (Stream& s, float* left, float* right, int numSamples, double step, Ramp& ramp,
                               float& fade, float fadeTarget, float fadeStep, bool& underrun) noexcept
{
    if (s.finished.load (std::memory_order_relaxed))
        return;

    const auto written = s.written.load (std::memory_order_acquire);
    const auto end = s.endFrame.load (std::memory_order_acquire);
    const bool exact = step == 1.0;
    const auto& ringL = s.ringL;
    const auto& ringR = s.ringR;
    double pos = s.pos;

    auto at = [] (const std::vector<float>& ring, juce::int64 k) noexcept
    {
        return k < 0 ? 0.0f : ring[(size_t) (k & Stream::mask)];
    };

    for (int i = 0; i < numSamples; ++i)
    {
        const auto k = (juce::int64) pos;

        if (k >= end)
        {
            s.finished.store (true, std::memory_order_relaxed);
            break;
        }

        float l, r;

        if (exact)
        {
            if (k >= written)
            {
                underrun = true;
                break;
            }

            l = ringL[(size_t) (k & Stream::mask)];
            r = ringR[(size_t) (k & Stream::mask)];
        }
        else
        {
            if (k + 2 >= written)
            {
                underrun = true;
                break;
            }

            // Hermite de 4 pontos (forma x), entre o quadro k e o k + 1
            const auto f = (float) (pos - (double) k);

            auto hermite = [&] (const std::vector<float>& ring) noexcept
            {
                const float ym1 = at (ring, k - 1), y0 = at (ring, k), y1 = at (ring, k + 1), y2 = at (ring, k + 2);
                const float c1 = 0.5f * (y1 - ym1);
                const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
                const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
                return ((c3 * f + c2) * f + c1) * f + y0;
            };

            l = hermite (ringL);
            r = hermite (ringR);
        }

        const float level = ramp.next();

        if (fade != fadeTarget)
            fade = fade < fadeTarget ? juce::jmin (fadeTarget, fade + fadeStep) : juce::jmax (fadeTarget, fade - fadeStep);

        const float g = level * fade;
        left[i] += l * g;
        right[i] += r * g;
        pos += exact ? 1.0 : step;
    }

    s.pos = pos;
    s.released.store (juce::jmax ((juce::int64) 0, (juce::int64) pos - 1), std::memory_order_release);

    const auto k = (juce::int64) pos;
    s.positionSec.store (((double) s.fileFrameAt (k) + (pos - (double) k)) / s.fileRate, std::memory_order_relaxed);
}

void DeckPlayer::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                   int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    // o JUCE soma este callback com os outros (o Tracktion): aqui se escreve, não se acumula
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const double rate = juce::jmax (8000.0, deviceRate.load (std::memory_order_relaxed));
    const auto fadeStep = (float) (1.0 / (0.005 * rate));   // 5 ms: play, pausa e troca de arquivo num deck
    const int rampSamples = (int) std::lround (0.020 * rate);   // 20 ms: ganho de cada deck (a troca A/B)
    const bool wantSound = playing.load (std::memory_order_relaxed);

    takeScenes();

    // A cena nova entra quando os decks que ela muda estão em silêncio. Deck mudo (ganho 0) ou o player
    // calado mudam na hora; o deck que soa desce 5 ms primeiro. Quem não muda segue sem emenda.
    if (next != nullptr)
    {
        // master em zero = nada se ouve agora (parado, ou o play acabou de ser apertado): troca na hora
        const bool silent = masterFade <= 0.0f;
        bool ready = true;

        for (int d = 0; d < numDecks; ++d)
        {
            const bool changes = scene == nullptr || scene->streams[(size_t) d] != next->streams[(size_t) d];

            if (! changes)
                continue;

            // nada soando neste deck: o deck vazio, o que já chegou ao fim, o mudo ou o player parado
            const auto* old = scene != nullptr ? scene->streams[(size_t) d].get() : nullptr;

            if (silent || gain[(size_t) d].now <= 0.0f || old == nullptr || old->finished.load (std::memory_order_relaxed))
                deckFade[(size_t) d] = 0.0f;

            if (deckFade[(size_t) d] > 0.0f)
                ready = false;
        }

        if (ready)
        {
            if (scene != nullptr)
                garbage.push (scene);   // nunca falha: inFlight < queueSize

            scene = next;
            next = nullptr;
        }
    }

    // o ganho de cada deck mira o alvo; com nada soando, vai direto (a rampa só existe para quem ouve)
    for (int d = 0; d < numDecks; ++d)
    {
        gain[(size_t) d].aim (gainTarget[(size_t) d].load (std::memory_order_relaxed), rampSamples);

        if (masterFade <= 0.0f)
            gain[(size_t) d].jump();
    }

    if (scene == nullptr || (! wantSound && masterFade <= 0.0f))
    {
        masterFade = 0.0f;
        return;
    }

    std::array<float, numDecks> fadeTarget;

    for (int d = 0; d < numDecks; ++d)
        fadeTarget[(size_t) d] = next != nullptr && next->streams[(size_t) d] != scene->streams[(size_t) d] ? 0.0f : 1.0f;

    for (int offset = 0; offset < numSamples;)
    {
        const int n = juce::jmin (maxBlock, numSamples - offset);
        auto* mixL = mixLeft.data();
        auto* mixR = mixRight.data();
        juce::FloatVectorOperations::clear (mixL, n);
        juce::FloatVectorOperations::clear (mixR, n);
        bool underrun = false;

        for (int d = 0; d < numDecks; ++d)
            if (auto* s = scene->streams[(size_t) d].get())
                renderStream (*s, mixL, mixR, n, s->fileRate / rate, gain[(size_t) d],
                              deckFade[(size_t) d], fadeTarget[(size_t) d], fadeStep, underrun);

        if (underrun)
            underrunCount.fetch_add (1, std::memory_order_relaxed);

        const float target = wantSound ? 1.0f : 0.0f;

        for (int i = 0; i < n; ++i)
        {
            if (masterFade != target)
                masterFade = masterFade < target ? juce::jmin (target, masterFade + fadeStep) : juce::jmax (target, masterFade - fadeStep);

            mixL[i] *= masterFade;
            mixR[i] *= masterFade;
        }

        if (numOutputs == 1 && outputs[0] != nullptr)
        {
            for (int i = 0; i < n; ++i)
                outputs[0][offset + i] = 0.5f * (mixL[i] + mixR[i]);
        }
        else if (numOutputs >= 2)
        {
            if (outputs[0] != nullptr)
                juce::FloatVectorOperations::copy (outputs[0] + offset, mixL, n);

            if (outputs[1] != nullptr)
                juce::FloatVectorOperations::copy (outputs[1] + offset, mixR, n);
        }

        offset += n;

        if (masterFade <= 0.0f && ! wantSound)
            break;   // já desceu: o resto do bloco é silêncio e os decks ficam onde estão
    }
}

} // namespace whynot::library
