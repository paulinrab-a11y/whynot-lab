#include "Projector.h"
#include "../record/Takes.h"
#include "../record/ReversedAudio.h"
#include "../record/BlockFile.h"
#include "../record/NoteTuneFiles.h"
#include "../automation/AutomationTarget.h"
#include "../model/NoteCurves.h"
#include "../model/WnlFormat.h"
#include "../plugins/PluginCatalog.h"
#include "../dsp/Master.h"

#include <map>
#include <tuple>

// O esticar do áudio (PlaylistItem::stretch) usa o signalsmith-stretch do Tracktion, ligado no CMakeLists.txt.
static_assert (TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH, "Ligue TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1 no CMakeLists.txt");

namespace whynot
{

Projector::Projector (te::Edit& e, SampleCache& c, Mode m, int numLoops, const PluginCatalog* pluginCatalog)
    : edit (e), cache (c), catalog (pluginCatalog), mode (m), loops (juce::jmax (1, numLoops))
{
    // O Edit vem com uma trilha padrão; o projetor cria as suas.
    for (auto* t : te::getAudioTracks (edit))
        edit.deleteTrack (t);

    // O fader e o pan do master entram antes da cadeia do master (revisão de 22/10): com o fader acima de
    // 0 dB, o limitador no fim ainda segura o pico, e o medidor (o último) mostra o que sai no WAV.
    // O volume do próprio Tracktion, que vem depois da lista, fica em 0 dB.
    masterFader = edit.getPluginCache().createNewPlugin (te::VolumeAndPanPlugin::xmlTypeName, {});
    edit.getMasterPluginList().insertPlugin (masterFader, 0, nullptr);

    masterMeter = edit.getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {});
    edit.getMasterPluginList().insertPlugin (masterMeter, -1, nullptr);

    // o LUFS do master (bloco 1, 22/10): o Medidor nativo depois do medidor de pico; não muda o som, e a
    // arrumação dos inserts (syncInsertList) só mexe no que vem antes do medidor de pico
    masterTap = edit.getPluginCache().createNewPlugin (WhynotFxPlugin::pluginTypeFor ("meter"), {});

    if (masterTap != nullptr)
        edit.getMasterPluginList().insertPlugin (masterTap, -1, nullptr);
}

dsp::LoudnessMeter* Projector::masterLoudness() const
{
    if (auto* fx = dynamic_cast<WhynotFxPlugin*> (masterTap.get()))
        if (auto* meter = dynamic_cast<dsp::MeterProcessor*> (&fx->getHost().processor()))
            return &meter->loudness();

    return nullptr;
}

te::VolumeAndPanPlugin* Projector::volumeFor (const juce::String& mixerId) const
{
    if (mixerId == masterTrackId)
        return dynamic_cast<te::VolumeAndPanPlugin*> (masterFader.get());

    if (auto it = mixer.find (mixerId); it != mixer.end() && it->second.track != nullptr)
        return it->second.track->getVolumePlugin();

    return nullptr;
}

Projector::~Projector() = default;

te::AudioTrack::Ptr Projector::newTrack (const juce::String& name)
{
    auto tracks = te::getAudioTracks (edit);
    auto track = edit.insertNewAudioTrack (te::TrackInsertPoint (nullptr, tracks.isEmpty() ? nullptr : tracks.getLast()), nullptr);

    if (track != nullptr)
        track->setName (name);

    return track;
}

te::TimePosition Projector::toTime (int ticks) const
{
    return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (ticks / (double) ticksPerBeat));
}

te::LevelMeterPlugin* Projector::meterFor (const juce::String& mixerId) const
{
    if (mixerId == masterTrackId)
        return dynamic_cast<te::LevelMeterPlugin*> (masterMeter.get());

    if (auto it = mixer.find (mixerId); it != mixer.end() && it->second.track != nullptr)
        return it->second.track->getLevelMeterPlugin();

    return nullptr;
}

te::AudioTrack* Projector::trackFor (const juce::String& channelId) const
{
    auto it = channels.find (channelId);
    return it == channels.end() ? nullptr : it->second.track.get();
}

te::AudioTrack* Projector::mixerTrackFor (const juce::String& mixerId) const
{
    auto it = mixer.find (mixerId);   // o master não tem trilha própria (é a saída do Edit)
    return it == mixer.end() ? nullptr : it->second.track.get();
}

const Projector::MixerBinding* Projector::bindingFor (const juce::String& mixerId) const
{
    if (mixerId == masterTrackId)
        return &masterBinding;

    auto it = mixer.find (mixerId);
    return it == mixer.end() ? nullptr : &it->second;
}

te::Plugin* Projector::insertPlugin (const juce::String& mixerId, const juce::String& insertId) const
{
    if (auto* b = bindingFor (mixerId))
        for (auto& ib : b->inserts)
            if (ib.id == insertId)
                return ib.plugin.get();

    return nullptr;
}

WhynotFxPlugin* Projector::fxFor (const juce::String& mixerId, const juce::String& insertId) const
{
    return dynamic_cast<WhynotFxPlugin*> (insertPlugin (mixerId, insertId));
}

te::Plugin* Projector::instrumentFor (const juce::String& channelId) const
{
    auto it = channels.find (channelId);
    return it == channels.end() ? nullptr : it->second.instrument.get();
}

WhynotSynthPlugin* Projector::synthFor (const juce::String& channelId) const
{
    auto it = channels.find (channelId);
    return it == channels.end() ? nullptr : it->second.synth;
}

WhynotPianoPlugin* Projector::pianoFor (const juce::String& channelId) const
{
    auto it = channels.find (channelId);
    return it == channels.end() ? nullptr : it->second.piano;
}

namespace
{
    /** A curva de um fade do bloco (FadeCurve) no Tracktion. */
    te::AudioFadeCurve::Type fadeCurveType (int curve)
    {
        return curve == fadeCurveConvex ? te::AudioFadeCurve::convex
             : curve == fadeCurveConcave ? te::AudioFadeCurve::concave
             : curve == fadeCurveS ? te::AudioFadeCurve::sCurve
             : te::AudioFadeCurve::linear;
    }

    /** O que decide qual plugin a trilha tem: trocar o sample por um piano (ou o contrário) refaz a trilha. */
    juce::String bindingKind (const Channel& c)
    {
        return c.isPiano() ? c.kind + ":" + c.instrument : c.kind;
    }

    /** Lei de pan (revisão de 22/10): potência constante com o centro em 0 dB. No meio nada muda; nas
        pontas o lado de perto sobe +3 dB (na lei linear do Tracktion subia +6) e o outro some.
        O VolumeAndPan do Tracktion continua linear (gE = g (1 - p'), gD = g (1 + p')): o pan dele vira
        p' = tan (p π/4) e o volume desce 20 log10 cos (p π/4), no máximo 3 dB nas pontas. Assim
        gE = √2 g cos θ e gD = √2 g sen θ, com θ = (p + 1) π/4. Como só desce, o fader em +6 dB continua valendo. */
    struct PanLaw
    {
        float enginePan = 0.0f, volumeOffsetDb = 0.0f;
    };

    PanLaw constantPower (float pan)
    {
        const double phi = juce::jlimit (-1.0, 1.0, (double) pan) * juce::MathConstants<double>::pi * 0.25;
        return { (float) std::tan (phi), (float) (20.0 * std::log10 (std::cos (phi))) };
    }

    /** Fader e pan do modelo no VolumeAndPan, pela lei acima (só o que não está na mão de uma curva). */
    void applyFaderAndPan (te::VolumeAndPanPlugin& vol, float volumeDb, float pan)
    {
        const auto law = constantPower (pan);
        const float db = volumeDb <= -100.0f ? -100.0f : juce::jmax (-100.0f, volumeDb + law.volumeOffsetDb);

        if (! vol.volParam->isAutomationActive() && std::abs (vol.getVolumeDb() - db) > 0.001f)
            vol.setVolumeDb (db);

        const float enginePan = std::abs (law.enginePan) < 0.005f ? 0.0f : law.enginePan;   // o Tracktion arredonda isso para 0

        if (! vol.panParam->isAutomationActive() && std::abs (vol.getPan() - enginePan) > 0.0001f)
            vol.setPan (enginePan);
    }

    /** Pós-fader: logo depois do volume da trilha. Pré-fader: logo antes dele, depois dos inserts e do ponto
        de congelar (congelada, a trilha continua enviando). Só mexe se estiver fora do lugar. */
    void placeSend (te::AudioTrack& track, const te::Plugin::Ptr& plugin, bool preFader)
    {
        auto& list = track.pluginList;
        auto* volume = track.getVolumePlugin();
        const int volumeIndex = list.indexOf (volume);
        const int at = list.indexOf (plugin.get());
        int freezeIndex = -1;

        for (int i = 0; i < volumeIndex; ++i)
            if (dynamic_cast<te::FreezePointPlugin*> (list[i]) != nullptr)
                freezeIndex = i;

        if (at >= 0 && volumeIndex >= 0 && (preFader ? (at < volumeIndex && at > freezeIndex) : at > volumeIndex))
            return;

        if (at >= 0)
            plugin->removeFromParent();

        const int index = list.indexOf (volume);
        list.insertPlugin (plugin, index < 0 ? -1 : (preFader ? index : index + 1), nullptr);
    }
}

//==============================================================================
bool Projector::usesChance (const Pattern& pattern, const juce::String& channelId)
{
    if (auto it = pattern.steps.find (channelId); it != pattern.steps.end())
        for (auto& s : it->second)
            if (s.on && s.chance < 100)
                return true;

    return false;
}

bool Projector::usesChance (const Pattern& pattern)
{
    for (auto& [id, steps] : pattern.steps)
        if (usesChance (pattern, id))
            return true;

    return false;
}

namespace
{
    // splitmix64: sem estado, o mesmo resultado no play, no export e no MIDI
    std::uint64_t mix (std::uint64_t z) noexcept
    {
        z += 0x9e3779b97f4a7c15ull;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
}

bool Projector::humanizes (const Project& p)
{
    return std::any_of (p.channels.begin(), p.channels.end(), [] (const Channel& c) { return c.humanize > 0.0f; });
}

int Projector::swingWarp (int tick, int delayTicks) noexcept
{
    if (delayTicks == 0)
        return tick;

    // dentro da colcheia (dois passos): 0 fica, o meio vai para meio + atraso, o fim (a próxima colcheia) fica
    constexpr int cell = 2 * ticksPerStep, half = ticksPerStep;
    const int d = juce::jlimit (0, half - 1, delayTicks);
    const int base = (tick >= 0 ? tick / cell : -((-tick + cell - 1) / cell)) * cell;
    const int u = tick - base;
    const double warped = u <= half ? (double) u * (half + d) / half
                                    : (double) (half + d) + (double) (u - half) * (half - d) / half;
    return base + juce::roundToInt (warped);
}

void Projector::humanizeOffsets (float amount, int seed, const juce::String& channelId, int index, int loopIndex, int& ticks,
                                 int& velocity) noexcept
{
    ticks = velocity = 0;

    if (! (amount > 0.0f))
        return;

    const double a = juce::jlimit (0.0, 1.0, (double) amount / 100.0);
    const int maxTicks = juce::roundToInt (a * 40.0), maxVelocity = juce::roundToInt (a * 20.0);   // a escala do Editar > Humanizar

    std::uint64_t h = mix ((std::uint64_t) (std::uint32_t) seed ^ 0x6875u);   // "hu": outro sorteio que o da chance
    h = mix (h ^ (std::uint64_t) (std::uint32_t) channelId.hashCode());
    h = mix (h ^ (std::uint64_t) (std::uint32_t) index);
    h = mix (h ^ (std::uint64_t) (std::uint32_t) loopIndex);

    // dois números de 0 a 1 tirados do mesmo sorteio (32 bits cada); de -max a +max
    const double r1 = (double) (h & 0xffffffffull) / 4294967295.0, r2 = (double) (h >> 32) / 4294967295.0;
    ticks = juce::roundToInt ((r1 * 2.0 - 1.0) * maxTicks);
    velocity = juce::roundToInt ((r2 * 2.0 - 1.0) * maxVelocity);
}

bool Projector::chanceHits (int chance, int seed, const juce::String& channelId, int stepIndex, int rollIndex, int loopIndex)
{
    if (chance >= 100)
        return true;

    std::uint64_t h = mix ((std::uint64_t) (std::uint32_t) seed);
    h = mix (h ^ (std::uint64_t) (std::uint32_t) channelId.hashCode());
    h = mix (h ^ ((std::uint64_t) (std::uint32_t) stepIndex << 16) ^ (std::uint64_t) (std::uint32_t) rollIndex);
    h = mix (h ^ (std::uint64_t) (std::uint32_t) loopIndex);
    return (int) (h % 100) < chance;
}

std::vector<Note> Projector::notesFor (const Project& p, const Pattern& pattern, const Channel& channel, int lengthTicks, int offsetTicks,
                                       int seed)
{
    if (offsetTicks > 0)
    {
        std::vector<Note> out;

        for (auto n : notesFor (p, pattern, channel, lengthTicks + offsetTicks, 0, seed))
        {
        if (n.start < offsetTicks)
            continue;

            n.start -= offsetTicks;
            out.push_back (n);
        }

        return out;
    }

    const int patternLength = juce::jmax (ticksPerStep, pattern.lengthTicks());
    std::vector<Note> base;

    // Para a chance (26/10): de qual passo e de qual repetição do roll veio cada nota (-1 = do piano roll).
    struct Origin { int step = -1, roll = 0, chance = 100; };
    std::vector<Origin> origin;

    // Passos → notas. Num canal que respeita a duração (808), cada passo soa até o próximo.
    if (auto it = pattern.steps.find (channel.id); it != pattern.steps.end())
    {
        const auto& steps = it->second;
        const int swing = p.stepSwingTicks (channel);   // passos ímpares atrasam (o swing global mais o do canal)

        for (size_t i = 0; i < steps.size(); ++i)
        {
            const auto& s = steps[i];

            if (! s.on)
                continue;

            const int sub = ticksPerStep / s.roll;

            for (int k = 0; k < s.roll; ++k)
            {
                Note n;
                n.start = (int) i * ticksPerStep + k * sub + (i % 2 == 1 ? swing : 0);
                n.key = juce::jlimit (0, 127, channel.rootKey + s.pitch);   // o tom do passo (26/10)
                n.velocity = s.velocity;
                n.pan = channel.kind == "sampler" ? s.pan : 0;   // o pan do passo só existe no sampler
                n.length = channel.oneShot ? juce::jmax (1, sub * 9 / 10) : sub;   // gate: ajustado abaixo
                base.push_back (n);
                origin.push_back ({ (int) i, k, s.chance });
            }
        }

        if (! channel.oneShot)
        {
            for (size_t i = 0; i < base.size(); ++i)
            {
                const int next = i + 1 < base.size() ? base[i + 1].start : patternLength;
                base[i].length = juce::jmax (1, next - base[i].start);
            }
        }
    }

    const size_t stepNotes = base.size();
    const bool anyChance = std::any_of (origin.begin(), origin.end(), [] (const Origin& o) { return o.chance < 100; });

    // o swing das notas (29/10) entra só aqui: o modelo continua com as notas na grade
    const int noteSwing = Channel::swingTicksFor (channel.noteSwing);

    if (auto* notes = pattern.notesFor (channel.id))
        for (auto n : *notes)
            if (! n.muted)   // nota muda (Alt+M) não toca nem sai no MIDI
            {
                if (noteSwing > 0)
                {
                    const int start = swingWarp (n.start, noteSwing), end = swingWarp (n.end(), noteSwing);
                    n.start = start;
                    n.length = juce::jmax (1, end - start);
                }

                base.push_back (n);
                origin.push_back ({});
            }

    // Repete o padrão até preencher o bloco; corta o que passar do fim. Um passo com chance sorteia a
    // cada volta (o número da volta entra no sorteio), o passo inteiro de uma vez (o roll junto).
    std::vector<Note> out;
    std::vector<size_t> kept;

    for (int offset = 0, loop = 0; offset < lengthTicks; offset += patternLength, ++loop)
    {
        kept.clear();

        for (size_t b = 0; b < base.size(); ++b)
            if (origin[b].chance >= 100 || chanceHits (origin[b].chance, seed, channel.id, origin[b].step, 0, loop))
                kept.push_back (b);

        for (size_t k = 0; k < kept.size(); ++k)
        {
            const size_t b = kept[k];
            auto n = base[b];

            // No canal que respeita a duração (808), um passo que não saiu não pode cortar o anterior:
            // a nota segue até o próximo passo que tocou nesta volta.
            if (anyChance && ! channel.oneShot && b < stepNotes)
            {
                const int next = k + 1 < kept.size() && kept[k + 1] < stepNotes ? base[kept[k + 1]].start : patternLength;
                n.length = juce::jmax (1, next - n.start);
            }

            n.start += offset;

            // humanizar (29/10): cada nota de cada volta anda um pouco e muda a força, com o sorteio fixo
            if (channel.humanize > 0.0f)
            {
                int dt = 0, dv = 0;
                // a chave é o passo (e a repetição do roll) ou o id da nota: mexer numa nota não sorteia as outras de novo
                const int key = b < stepNotes ? origin[b].step * 8 + origin[b].roll : 0x100000 + base[b].id;
                humanizeOffsets (channel.humanize, seed, channel.id, key, loop, dt, dv);
                n.start = juce::jmax (0, n.start + dt);
                n.velocity = juce::jlimit (1, 127, n.velocity + dv);
            }

            if (n.start >= lengthTicks)
                continue;

            n.length = juce::jmin (n.length, lengthTicks - n.start);
            out.push_back (n);
        }
    }

    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

    // Mono: uma nota por vez. Slide = legato (a anterior segura até 1 tick depois); sem slide, corta.
    if (channel.mono)
    {
        for (size_t i = 1; i < out.size(); ++i)
        {
            auto& prev = out[i - 1];
            const auto& cur = out[i];

            if (cur.slide)
                prev.length = juce::jmax (1, cur.start - prev.start + 1);
            else if (prev.end() > cur.start)
                prev.length = juce::jmax (1, cur.start - prev.start);
        }
    }

    return out;
}

//==============================================================================
void Projector::sync (const Project& p, const juce::File& projectFolder)
{
    const juce::ScopedValueSetter<bool> applying (applyingParameters, true);   // gravar automação: não é a pessoa
    songMode = mode == Mode::song || (mode == Mode::followProject && p.songMode);

    syncTempo (p);
    planSolo (p);       // antes da saída e dos envios: a fonte de sidechain calada pelo solo vai para a trilha escondida
    syncMixer (p);
    syncChannels (p, projectFolder);
    syncRouting (p);
    syncSends (p);      // antes dos inserts: o retorno de envio fica no começo da trilha
    syncInserts (p);
    syncMuteSolo (p);
    syncAudition();
    syncAutomation (p);   // depois dos inserts: as curvas moram nos plugins
    syncFreeze (p);       // por último: renderiza o que já está montado

    edit.clickTrackEnabled = p.metronome;

    const auto* current = p.currentPatternPtr();
    const int lengthTicks = songMode ? p.songEndTicks()
                                     : (current != nullptr ? current->lengthTicks() * patternLoops (p, *current) : ticksPerBar4 * loops);
    currentLengthSeconds = toTime (lengthTicks).inSeconds();

    if (transportLoopManaged)
        return;

    auto& transport = edit.getTransport();
    const te::TimeRange loopRange { te::TimePosition(), te::TimePosition::fromSeconds (currentLengthSeconds) };

    if (transport.getLoopRange() != loopRange)
        transport.setLoopRange (loopRange);

    if (transport.looping.get() != p.loop)
        transport.looping = p.loop;
}

void Projector::syncTempo (const Project& p)
{
    tempoChanged = false;

    if (p.bpm != appliedBpm)
    {
        edit.tempoSequence.getTempos()[0]->setBpm (p.bpm);
        appliedBpm = p.bpm;
        tempoChanged = true;   // clipes são posicionados em segundos: precisam ser refeitos
    }

    if (p.timeSigNum != appliedNum || p.timeSigDen != appliedDen)
    {
        auto* ts = edit.tempoSequence.getTimeSigs()[0];
        ts->numerator = p.timeSigNum;
        ts->denominator = p.timeSigDen;
        appliedNum = p.timeSigNum;
        appliedDen = p.timeSigDen;
    }

    const int length = p.currentPatternPtr() != nullptr ? p.currentPatternPtr()->lengthTicks() : ticksPerBar4;
    currentPatternSeconds = toTime (length).inSeconds();
}

void Projector::syncMixer (const Project& p)
{
    for (auto it = mixer.begin(); it != mixer.end();)
    {
        if (p.findMixer (it->first) == nullptr)
        {
            if (it->second.track != nullptr)
                edit.deleteTrack (it->second.track.get());

            it = mixer.erase (it);
        }
        else
        {
            ++it;
        }
    }

    for (auto& m : p.mixer)
    {
        if (m.isMaster())
        {
            // o fader do master é o masterFader, antes da cadeia; o do Tracktion (depois dela) fica neutro
            if (auto* fader = volumeFor (masterTrackId))
                applyFaderAndPan (*fader, m.volumeDb, m.pan);

            if (auto vol = edit.getMasterVolumePlugin())
            {
                if (std::abs (vol->getVolumeDb()) > 0.001f) vol->setVolumeDb (0.0f);
                if (std::abs (vol->getPan()) > 0.0001f)     vol->setPan (0.0f);
            }

            continue;
        }

        auto& b = mixer[m.id];

        if (b.track == nullptr)
            b.track = newTrack (m.name);

        if (b.track == nullptr)
            continue;

        if (b.track->getName() != m.name)
            b.track->setName (m.name);

        // com curva de automação, quem manda é ela (o valor do modelo volta quando a curva sai)
        if (auto* vol = b.track->getVolumePlugin())
            applyFaderAndPan (*vol, m.volumeDb, m.pan);
    }
}

void Projector::syncChannels (const Project& p, const juce::File& projectFolder)
{
    problems.clear();

    for (auto it = channels.begin(); it != channels.end();)
    {
        const auto* c = p.findChannel (it->first);

        // canal removido, ou que mudou de tipo: a trilha é refeita
        if (c == nullptr || bindingKind (*c) != it->second.kind)
        {
            if (it->second.track != nullptr)
                edit.deleteTrack (it->second.track.get());

            it = channels.erase (it);
        }
        else
        {
            ++it;
        }
    }

    // Por sync (desempenho, 29/10): os crossfades de todos os blocos de uma vez, e o que já se perguntou ao disco neste
    // sync guardado pelo caminho. A voz picotada tem centenas de blocos do mesmo arquivo, e cada um perguntava de novo
    // se ele existe (e a data, na cópia estéreo do pan), a cada mudança do projeto. Dentro de um sync a resposta é a
    // mesma; no próximo, pergunta de novo (um arquivo que sumiu continua sendo avisado).
    std::vector<playlistedit::Crossfade> crossfades;
    bool crossfadesReady = false;
    std::map<juce::String, bool> existsSeen;
    std::map<juce::String, juce::String> playedSeen, stereoSeen;

    auto exists = [&existsSeen] (const juce::File& f)
    {
        auto [it, isNew] = existsSeen.try_emplace (f.getFullPathName(), false);

        if (isNew)
            it->second = f.existsAsFile();

        return it->second;
    };

    auto remembered = [] (std::map<juce::String, juce::String>& seen, const juce::String& key, auto&& compute) -> const juce::String&
    {
        auto it = seen.find (key);

        if (it == seen.end())
            it = seen.emplace (key, compute()).first;

        return it->second;
    };

    for (auto& c : p.channels)
    {
        auto& b = channels[c.id];

        if (b.track == nullptr)
        {
            b.track = newTrack (c.name);
            b.kind = bindingKind (c);

            if (b.track == nullptr)
                continue;

            if (c.isPiano())
            {
                auto plugin = edit.getPluginCache().createNewPlugin (WhynotPianoPlugin::xmlTypeName, {});
                b.track->pluginList.insertPlugin (plugin, 0, nullptr);
                b.piano = dynamic_cast<WhynotPianoPlugin*> (plugin.get());
            }
            else if (c.kind == "sampler")
            {
                auto plugin = edit.getPluginCache().createNewPlugin (WhynotSamplerPlugin::xmlTypeName, {});
                b.track->pluginList.insertPlugin (plugin, 0, nullptr);
                b.sampler = dynamic_cast<WhynotSamplerPlugin*> (plugin.get());
            }
            else if (c.isSynth())
            {
                auto plugin = edit.getPluginCache().createNewPlugin (WhynotSynthPlugin::xmlTypeName, {});
                b.track->pluginList.insertPlugin (plugin, 0, nullptr);
                b.synth = dynamic_cast<WhynotSynthPlugin*> (plugin.get());
            }
        }

        if (b.track->getName() != c.name)
            b.track->setName (c.name);

        const auto file = juce::File::isAbsolutePath (c.samplePath) ? juce::File (c.samplePath)
                                                                    : projectFolder.getChildFile (c.samplePath);
        const auto resolved = file.getFullPathName();

        std::vector<MidiSpec> midi;
        std::vector<AudioSpec> audio;

        if (c.isAudio())
        {
            if (! exists (file))
                problems.set (c.id, "Arquivo de áudio não encontrado: "_t + resolved);

            if (songMode && ! crossfadesReady)
            {
                crossfades = playlistedit::autoCrossfades (p);
                crossfadesReady = true;
            }

            const bool tuneEdited = notetune::anyEdited (c.tunedNotes);   // percorre as notas: uma vez por canal

            if (songMode)
                for (size_t itemIndex = 0; itemIndex < p.playlistItems.size(); ++itemIndex)
                    if (const auto& item = p.playlistItems[itemIndex];
                        item.kind == "audio" && item.ref == c.id && p.itemSounds (item))   // o bloco e a raia (bloco 4)
                    {
                        const auto edges = takes::edges (p, item);   // comping: o crossfade no corte entre takes
                        // o reverso do bloco (voz na playlist, 27/10) toca o arquivo invertido ao lado, sem canal novo
                        // a afinação nota a nota (27/10) toca o arquivo refeito ao lado, que nunca muda o original
                        // o arquivo próprio do bloco (playlist fatia 6, 27/10 noite: o consolidado ao lado, sem canal novo)
                        // já tem a afinação dentro; o reverso vale nele também
                        const auto own = item.file.isEmpty() ? juce::File()
                                                             : juce::File::isAbsolutePath (item.file) ? juce::File (item.file)
                                                                                                      : projectFolder.getChildFile (item.file);
                        // (cada um calculado uma vez por sync: o arquivo invertido e o refeito são os mesmos para todos
                        // os blocos do canal; a chave do refeito é o canal, porque depende das notas afinadas dele)
                        const auto played = own != juce::File()
                            ? remembered (playedSeen, own.getFullPathName() + (item.reversed ? "\n1" : "\n0"),
                                          [&] { return reversed::playedFile (own, item.reversed).getFullPathName(); })
                            : item.reversed ? remembered (playedSeen, resolved + "\n1", [&] { return reversed::playedFile (file, true).getFullPathName(); })
                                            : remembered (playedSeen, "canal\n" + c.id,
                                                          [&] { return notetune::playedFile (file, c.tunedNotes, c.tuneFormantFollows).getFullPathName(); });
                        audio.push_back ({ item.start, item.length, item.offsetSeconds, played, item.stretch, item.tape, edges.in, edges.out,
                                           item.gainDb, item.fadeIn, item.fadeOut });
                        const auto& cross = crossfades[itemIndex];
                        auto& spec = audio.back();
                        spec.userFadeInCurve = item.fadeInCurve;
                        spec.userFadeOutCurve = item.fadeOutCurve;
                        spec.autoFadeIn = cross.in;
                        spec.autoFadeOut = cross.out;
                        spec.pitch = item.pitch;
                        spec.pan = item.pan;

                        if (item.pan != 0.0f)   // o Tracktion ignora o pan de um clipe mono: toca a cópia em dois canais
                            spec.file = remembered (stereoSeen, spec.file, [&] { return blockfile::stereoOf (juce::File (spec.file)).getFullPathName(); });

                        if (own != juce::File() && ! exists (own))
                            problems.set (c.id, "Arquivo do bloco não encontrado: "_t + own.getFullPathName());

                        if (own == juce::File() && item.reversed && exists (file) && ! exists (juce::File (played)))
                            problems.set (c.id, "Arquivo invertido não encontrado: "_t + played);

                        if (own == juce::File() && ! item.reversed && tuneEdited && exists (file) && played == resolved)
                            problems.set (c.id, "Não consegui refazer a voz afinada nota a nota; tocando o original: "_t + resolved);
                    }

            std::sort (audio.begin(), audio.end(), [] (const AudioSpec& a, const AudioSpec& x) { return a.start < x.start; });
        }
        else if (b.sampler != nullptr)
        {
            // as camadas por força (bloco 4f): os caminhos e as forças dizem se mudou
            juce::String layersKey;

            for (auto& l : c.layers)
                layersKey << (juce::File::isAbsolutePath (l.samplePath) ? juce::File (l.samplePath) : projectFolder.getChildFile (l.samplePath)).getFullPathName()
                          << "@" << l.minVelocity << ";";

            if (resolved != b.resolvedSample || layersKey != b.resolvedLayers)
            {
                juce::String error;
                auto data = c.samplePath.isEmpty() ? nullptr : cache.get (file, error);
                std::vector<WhynotSamplerPlugin::Layer> layers;
                juce::StringArray missing;

                for (auto& l : c.layers)
                {
                    juce::String layerError;
                    const auto layerFile = juce::File::isAbsolutePath (l.samplePath) ? juce::File (l.samplePath) : projectFolder.getChildFile (l.samplePath);

                    if (auto layerData = cache.get (layerFile, layerError))
                        layers.push_back ({ std::move (layerData), (float) l.minVelocity / 127.0f });
                    else
                        missing.add (layerFile.getFileName());
                }

                b.sampler->setSample (data, std::move (layers));
                b.resolvedSample = resolved;
                b.resolvedLayers = layersKey;
                b.missingLayers = missing.joinIntoString (", ");

                if (data == nullptr)
                    problems.set (c.id, error.isEmpty() ? juce::String ("Canal sem sample.") : error);
            }
            else if (! b.sampler->hasSample())
            {
                problems.set (c.id, "Sample não encontrado: "_t + resolved);
            }

            // a camada que não abriu (bloco 4f): o aviso fica enquanto ela faltar, sem tapar o do sample do canal
            if (b.missingLayers.isNotEmpty() && problems[c.id].isEmpty())
                problems.set (c.id, "Camada por força não abriu: "_t + b.missingLayers + " (as notas fortes tocam o sample do canal)");

            b.sampler->setChannel (c.rootKey, c.oneShot, c.cutItself, c.mono, c.glideMs);

            SamplerShape shape;
            shape.tuneSemis = c.tune - c.rootCents / 100.0f;   // o sample desafinado volta para a nota
            shape.attackMs = c.attackMs;
            shape.releaseMs = c.releaseMs;
            shape.decayMs = c.decayMs;
            shape.sustain = c.sustain;
            shape.pitchDropSemis = c.pitchDrop;
            shape.pitchDropMs = c.pitchDropMs;
            shape.start = c.start;
            shape.loop = c.loop;
            shape.loopStart = c.loopStart;
            shape.loopEnd = c.loopEnd;
            shape.loopXfadeMs = c.loopXfadeMs;
            shape.bendRange = curves::bendRangeFor (c);   // o mesmo alcance com que as curvas viram pitch wheel (27/10)
            b.sampler->setShape (shape);
            b.sampler->setSlices (c.slices, c.sliceKey);
            midi = midiFor (p, c);
            addChokeCuts (p, c, midi);
        }
        else if (c.isPlugin())
        {
            syncInstrument (b, c);
            midi = midiFor (p, c);
            addChokeCuts (p, c, midi);
        }
        else if (b.synth != nullptr)
        {
            b.synth->setOwner (c.id);
            b.synth->applyChannel (c, p.bpm);
            midi = midiFor (p, c);
        }
        else if (b.piano != nullptr)
        {
            b.piano->setOwner (c.id);
            b.piano->applyChannel (c);

            if (! b.piano->hasLibrary())
                problems.set (c.id, b.piano->getLoadError());

            midi = midiFor (p, c);
            addChokeCuts (p, c, midi);
        }

        if (! b.built || tempoChanged || midi != b.midi || audio != b.audio)
            rebuildClips (b, c.name, std::move (midi), std::move (audio), ! b.built || tempoChanged);
    }
}

std::vector<Projector::MidiSpec> Projector::midiFor (const Project& p, const Channel& c) const
{
    std::vector<MidiSpec> midi;

    if (songMode)
    {
        // Blocos do mesmo padrão, com o mesmo tamanho e o mesmo começo dentro dele, tocam as mesmas notas:
        // a conta sai uma vez por combinação, não uma por bloco (fluidez, 21/10: 150 blocos × 28 canais a
        // cada edição viravam 4 mil contas).
        struct Key
        {
            const Pattern* pattern;
            int length, offset, seed;   // seed: o começo do bloco, só quando o canal tem passo com chance
            bool operator< (const Key& o) const noexcept
            {
                return std::tie (pattern, length, offset, seed) < std::tie (o.pattern, o.length, o.offset, o.seed);
            }
        };

        std::map<Key, std::vector<Note>> known;

        for (auto& item : p.playlistItems)
        {
            if (item.kind != "pattern" || ! p.itemSounds (item))   // o mudo do bloco e o da raia, com o solo (bloco 4)
                continue;

            if (auto* pat = p.findPattern (item.ref))
            {
                // o começo do padrão na linha do tempo: as duas metades de um bloco cortado sorteiam como o bloco inteiro
                const int seed = usesChance (*pat, c.id) ? item.start - item.patternOffset : 0;
                const Key key { pat, item.length, item.patternOffset, seed };
                auto it = known.find (key);

                if (it == known.end())
                    it = known.emplace (key, notesFor (p, *pat, c, item.length, item.patternOffset, seed)).first;

                if (! it->second.empty())
                    midi.push_back ({ item.start, item.length, it->second });
            }
        }

        std::sort (midi.begin(), midi.end(), [] (const MidiSpec& a, const MidiSpec& x) { return a.start < x.start; });
    }
    else if (auto* pat = p.currentPatternPtr())
    {
        const int length = pat->lengthTicks() * patternLoops (p, *pat);
        midi.push_back ({ 0, length, notesFor (p, *pat, c, length) });
    }

    // pitch bend (26/10): mudar o alcance do instrumento reescreve as curvas em pitch wheel; sem curva, o knob do synth
    // não reescreve o clipe
    for (auto& m : midi)
        if (std::any_of (m.notes.begin(), m.notes.end(), [] (const Note& n) { return ! n.pitch.empty(); }))
            m.bendRange = curves::bendRangeFor (c);

    // Um clipe sem curva não manda pitch wheel nem CC1; no SONG, depois de um clipe que terminou num bend, ele tocaria
    // com o bend do anterior (os clipes emendados não zeram o canal). Num canal que tem curva em algum clipe, o clipe
    // sem curva manda o descanso no começo da primeira nota (27/10).
    auto hasCurve = [] (const MidiSpec& m, curves::Kind k)
    {
        return std::any_of (m.notes.begin(), m.notes.end(), [k] (const Note& n) { return ! curves::of (n, k).empty() && n.length > 0; });
    };
    const bool anyPitch = std::any_of (midi.begin(), midi.end(), [&] (const MidiSpec& m) { return hasCurve (m, curves::Kind::pitch); });
    const bool anyMod = std::any_of (midi.begin(), midi.end(), [&] (const MidiSpec& m) { return hasCurve (m, curves::Kind::mod); });

    for (auto& m : midi)
    {
        m.restPitch = anyPitch && ! hasCurve (m, curves::Kind::pitch);
        m.restMod = anyMod && ! hasCurve (m, curves::Kind::mod);
    }

    return midi;
}

void Projector::addChokeCuts (const Project& p, const Channel& c, std::vector<MidiSpec>& midi) const
{
    if (c.chokeGroup <= 0 || midi.empty())
        return;

    // Início, no tempo do projeto, de cada nota dos outros canais do mesmo grupo.
    std::vector<int> others;

    for (auto& d : p.channels)
        if (d.id != c.id && d.chokeGroup == c.chokeGroup && ! d.isAudio() && ! d.mute)
            for (auto& spec : midiFor (p, d))
                for (auto& n : spec.notes)
                    others.push_back (spec.start + n.start);

    std::sort (others.begin(), others.end());
    others.erase (std::unique (others.begin(), others.end()), others.end());

    for (auto& spec : midi)
    {
        std::vector<int> own;

        for (auto& n : spec.notes)
            own.push_back (n.start);

        std::sort (own.begin(), own.end());

        for (int t : others)
        {
            const int rel = t - spec.start;

            // uma nota deste canal no mesmo tick vence: o corte não entra
            if (rel >= 0 && rel < spec.length && ! std::binary_search (own.begin(), own.end(), rel))
                spec.cuts.push_back (rel);
        }
    }
}

void Projector::syncInstrument (ChannelBinding& b, const Channel& c)
{
    // O mesmo plugin fica vivo entre as sincronizações (o estado dele mora nele, não no modelo).
    // Só refaz se for outro plugin, ou se não abriu e o catálogo mudou (scan novo, desbloqueio).
    const auto identity = c.plugin.identity();
    const int catalogVersion = catalog != nullptr ? catalog->getVersion() : 0;
    const bool loaded = plugin_host::instanceOf (b.instrument.get()) != nullptr;

    if (identity != b.pluginIdentity || (! loaded && catalogVersion != b.pluginTriedVersion))
    {
        // Solta antes de apagar: apagar fecha a janela do plugin, e quem guarda o estado ao fechar
        // não pode mais achar o plugin velho por este canal.
        if (auto old = std::exchange (b.instrument, nullptr))
            old->deleteFromParent();
        b.pluginIdentity = identity;
        b.pluginTriedVersion = catalogVersion;

        if (auto plugin = plugin_host::create (edit, c.plugin))
        {
            b.track->pluginList.insertPlugin (plugin, 0, nullptr);
            b.instrument = plugin;
        }
    }

    if (const auto problem = plugin_host::problemFor (b.instrument.get(), c.plugin, catalog); problem.isNotEmpty())
        problems.set (c.id, problem);
}

/*  Só o que mudou (pedido do usuário de 21/10 à noite: o som não pode falhar enquanto se edita).
    Antes, qualquer mudança num canal apagava e recriava todos os clipes dele. O Tracktion reconhece o
    clipe MIDI pelo id dele ao montar o grafo novo: com um clipe novo, a nota que estava tocando (o 808,
    um pad) perdia a lista de notas ativas, e o nó novo disparava de novo o que cruza a agulha. Agora:
    - clipe com o mesmo conteúdo fica como está;
    - clipe MIDI que mudou é reescrito no lugar (o mesmo id: o Tracktion corta só as notas que saíram);
    - só sobra apagar ou criar quando o número de clipes muda.
    O BPM mexe na posição em segundos de tudo: aí é tudo de novo, como antes. */
void Projector::rebuildClips (ChannelBinding& b, const juce::String& name, std::vector<MidiSpec> midi,
                              std::vector<AudioSpec> audio, bool everything)
{
    b.built = true;

    if (b.track == nullptr)
    {
        b.midi = std::move (midi);
        b.audio = std::move (audio);
        b.midiClips.clear();
        b.audioClips.clear();
        return;
    }

    const auto existing = b.track->getClips();

    // os clipes da trilha têm de ser exatamente os que este projetor criou; senão, refaz tudo
    if (! everything)
    {
        everything = existing.size() != (int) (b.midiClips.size() + b.audioClips.size());

        for (auto* list : { &b.midiClips, &b.audioClips })
            for (auto& clip : *list)
                everything = everything || clip == nullptr || ! existing.contains (clip.get());
    }

    if (everything)
    {
        for (int i = existing.size(); --i >= 0;)
            existing[i]->removeFromParent();

        b.midi.clear();
        b.audio.clear();
        b.midiClips.clear();
        b.audioClips.clear();
    }

    // MIDI: 1) o mesmo conteúdo fica com o mesmo clipe
    std::vector<te::Clip::Ptr> midiClips (midi.size());
    std::vector<bool> usedMidi (b.midi.size(), false);

    for (size_t j = 0; j < midi.size(); ++j)
        for (size_t i = 0; i < b.midi.size(); ++i)
            if (! usedMidi[i] && b.midi[i].start == midi[j].start && b.midi[i] == midi[j])
            {
                midiClips[j] = b.midiClips[i];
                usedMidi[i] = true;
                break;
            }

    // 2) o resto reaproveita os clipes que sobraram, na ordem, reescritos no lugar; 3) faltou clipe: cria
    size_t nextOld = 0;

    for (size_t j = 0; j < midi.size(); ++j)
    {
        if (midiClips[j] != nullptr)
            continue;

        while (nextOld < b.midi.size() && usedMidi[nextOld])
            ++nextOld;

        if (nextOld < b.midi.size())
        {
            usedMidi[nextOld] = true;

            if (auto* clip = dynamic_cast<te::MidiClip*> (b.midiClips[nextOld].get()))
            {
                writeMidi (*clip, midi[j]);
                midiClips[j] = b.midiClips[nextOld];
                continue;
            }

            b.midiClips[nextOld]->removeFromParent();
        }

        midiClips[j] = insertMidiClip (*b.track, name, midi[j]);
    }

    // 4) clipe que sobrou sai
    for (size_t i = 0; i < b.midi.size(); ++i)
        if (! usedMidi[i] && b.midiClips[i] != nullptr)
            b.midiClips[i]->removeFromParent();

    // Áudio: o mesmo bloco fica com o mesmo clipe (o esticar segue tocando); o que mudou é refeito
    std::vector<te::Clip::Ptr> audioClips (audio.size());
    std::vector<bool> usedAudio (b.audio.size(), false);

    for (size_t j = 0; j < audio.size(); ++j)
        for (size_t i = 0; i < b.audio.size(); ++i)
            if (! usedAudio[i] && b.audio[i] == audio[j])
            {
                audioClips[j] = b.audioClips[i];
                usedAudio[i] = true;
                break;
            }

    for (size_t i = 0; i < b.audio.size(); ++i)
        if (! usedAudio[i] && b.audioClips[i] != nullptr)
            b.audioClips[i]->removeFromParent();

    b.midi = std::move (midi);
    b.midiClips = std::move (midiClips);
    b.audio = std::move (audio);
    b.audioClips = std::move (audioClips);

    // só os blocos novos ou mudados ganham um clipe; um clipe que o Tracktion recusar (nullptr) faz a
    // próxima mudança refazer tudo
    for (size_t j = 0; j < b.audio.size(); ++j)
    {
        if (b.audioClips[j] != nullptr)
            continue;   // o mesmo bloco de antes: o clipe fica

        const auto& spec = b.audio[j];

        // o lugar na playlist é musical (ticks no BPM atual); o corte é no tempo do arquivo. O Tracktion conta
        // o corte no tempo do clipe, que no esticado anda stretch segundos por segundo do arquivo.
        // Comping: no corte entre dois takes, o que entra começa antes (o crossfade inteiro) e sobe, e o que
        // sai desce nos últimos instantes dele, com curvas de mesma potência. A fusão fica toda antes do corte:
        // o que sai pode acabar junto com o arquivo (a pessoa parou ali). O que entra não volta antes do começo
        // do arquivo dele.
        // MP3: o atraso que o codificador põe no começo entra no corte (pela etiqueta do LAME), para o beat
        // em MP3 cair no tempo como o WAV (revisão de 22/10; o sampler corta o mesmo atraso ao ler). O
        // crossfade não entra nesse atraso: o começo do arquivo, para ele, é depois do silêncio do codificador.
        const double priming = mp3EncoderDelaySeconds (juce::File (spec.file));
        const double offset = (spec.offsetSeconds + priming) * spec.stretch;
        const double before = juce::jmin (spec.fadeIn, spec.offsetSeconds * spec.stretch);
        const te::ClipPosition position { { toTime (spec.start) - te::TimeDuration::fromSeconds (before), toTime (spec.start + spec.length) },
                                          te::TimeDuration::fromSeconds (offset - before) };
        auto clip = b.track->insertWaveClip (name, juce::File (spec.file), position, false);

        if (clip == nullptr)
            continue;   // fica nullptr em audioClips: a próxima mudança refaz tudo

        // Primeiro o arquivo como ele é (sem o auto-tempo que o Tracktion liga pelo nome); depois, se o bloco
        // estiver esticado, o esticar por cima. Merge do extra-onda1 abde694 com o extra-esticar 1177d2a.
        playFileAsIs (*clip, position);

        if (before > 0.0 || spec.fadeOut > 0.0 || spec.userFadeIn > 0.0 || spec.userFadeOut > 0.0 || spec.autoFadeIn > 0.0 || spec.autoFadeOut > 0.0)
        {
            // O crossfade dos takes e o automático (dois áudios sobrepostos na raia) usam a curva de mesma
            // potência; os fades das alças, a curva escolhida no bloco (a reta, se ninguém mexeu). Vale o mais longo.
            const double fadeIn = juce::jmax (before, spec.userFadeIn, spec.autoFadeIn);
            const double fadeOut = juce::jmax (spec.fadeOut, spec.userFadeOut, spec.autoFadeOut);
            const bool powerIn = before > 0.0 || spec.autoFadeIn > spec.userFadeIn;
            const bool powerOut = spec.fadeOut > 0.0 || spec.autoFadeOut > spec.userFadeOut;
            clip->setFadeInType (powerIn ? te::AudioFadeCurve::convex : fadeCurveType (spec.userFadeInCurve));
            clip->setFadeOutType (powerOut ? te::AudioFadeCurve::convex : fadeCurveType (spec.userFadeOutCurve));
            clip->setFadeIn (te::TimeDuration::fromSeconds (fadeIn));
            clip->setFadeOut (te::TimeDuration::fromSeconds (fadeOut));
        }

        // o pan do bloco (o painel de propriedades, fatia 6) pela mesma lei do mixer: o pan do clipe no
        // Tracktion é linear como o do VolumeAndPan (+6 dB na ponta); com a lei, o ganho desce até 3 dB
        const auto law = constantPower (spec.pan);

        if (spec.gainDb != 0.0f || spec.pan != 0.0f)
            clip->setGainDB (spec.gainDb + (spec.pan != 0.0f ? law.volumeOffsetDb : 0.0f));   // o ganho do bloco (a alça de baixo, normalizar)

        if (spec.pan != 0.0f)
            clip->setPan (law.enginePan);

        if (spec.stretch != 1.0 || spec.pitch != 0.0f)
        {
            // Esticado em tempo real, sem arquivo temporário: o som novo sai assim que o bloco muda.
            // O signalsmith-stretch (MIT, no Tracktion) mantém o tom; a fita só muda a velocidade, e o tom vai junto.
            // O tom do bloco (voz na playlist, 27/10) pede o signalsmith; na fita, ele soma ao tom que a velocidade dá.
            clip->setUsesProxy (false);
            const bool tapeOnly = spec.tape && spec.pitch == 0.0f;
            clip->setTimeStretchMode (tapeOnly ? te::TimeStretcher::disabled : te::TimeStretcher::signalsmithDefault);
            clip->setSpeedRatio (1.0 / spec.stretch);

            if (! tapeOnly)
                clip->setPitchChange (spec.pitch + (spec.tape ? (float) (12.0 * std::log2 (1.0 / spec.stretch)) : 0.0f));
        }

        b.audioClips[j] = clip;
    }
}

void Projector::playFileAsIs (te::WaveAudioClip& clip, const te::ClipPosition& position)
{
    // Caça de bugs do produtor (21/10, achado da frente Fatiar e 808): o Tracktion lê o andamento e o tom no
    // nome do arquivo ("Beat 140bpm.wav", "Loop Am 92.wav") e no pedaço ACID dos loops de pacote. Com isso ele
    // liga sozinho o auto-tempo e o auto-pitch, muda o tamanho do clipe e pede um proxy esticado, que só um
    // Timer da thread de mensagem começa: o render na thread de mensagem esperava para sempre, e ao vivo o
    // beat tocava esticado para o BPM do projeto. Na WHYNOT o clipe toca o arquivo como ele é (o esticar é
    // à parte, por bloco), no lugar e no tamanho da playlist.
    clip.setAutoTempo (false);
    clip.setAutoPitch (false);
    clip.setLoopInfo (te::LoopInfo (clip.edit.engine));
    clip.setTimeStretchMode (te::TimeStretcher::disabled);
    clip.setPosition (position);
}

te::Clip::Ptr Projector::insertMidiClip (te::AudioTrack& track, const juce::String& name, const MidiSpec& spec)
{
    auto clip = track.insertMIDIClip (name, { toTime (spec.start), toTime (spec.start + spec.length) }, nullptr);

    if (clip == nullptr)
        return {};

    writeMidi (*clip, spec);
    return clip;
}

void Projector::writeMidi (te::MidiClip& clip, const MidiSpec& spec)
{
    const te::ClipPosition position { { toTime (spec.start), toTime (spec.start + spec.length) }, te::TimeDuration() };

    if (clip.getPosition() != position)
        clip.setPosition (position);

    auto& seq = clip.getSequence();
    seq.clear (nullptr);

    // Pan por passo (26/10): o sampler guarda o CC 10 numa fila com o instante em que chegou e o usa só na
    // nota que chega no mesmo instante, na ordem. Num canal que usa pan, toda nota leva o seu (o centro
    // também). No mesmo instante, o Tracktion manda os controles antes das notas, e os dois na ordem do clipe.
    const bool anyPan = std::any_of (spec.notes.begin(), spec.notes.end(), [] (const Note& n) { return n.pan != 0; });

    if (anyPan)
        for (auto& n : spec.notes)
            seq.addControllerEvent (te::BeatPosition::fromBeats (n.start / (double) ticksPerBeat), 10,
                                    juce::jlimit (0, 127, 64 + juce::roundToInt (n.pan * 0.63)) << 7,   // o MidiList guarda 14 bits
                                    nullptr);

    for (auto& n : spec.notes)
        seq.addNote (n.key,
                     te::BeatPosition::fromBeats (n.start / (double) ticksPerBeat),
                     te::BeatDuration::fromBeats (n.length / (double) ticksPerBeat),
                     n.velocity, 0, nullptr);

    // choke: o "All Sound Off" corta as vozes deste canal quando outro do grupo toca
    for (int t : spec.cuts)
        seq.addControllerEvent (te::BeatPosition::fromBeats (t / (double) ticksPerBeat), 120, 0, nullptr);

    // Pitch bend e mod wheel desenhados nas notas (26/10): eventos do clipe, então tocar ao vivo e renderizar mandam
    // os mesmos ao instrumento. O Tracktion guarda o pitch wheel em 14 bits e o CC em 7 bits deslocados (<< 7).
    for (auto& e : curves::controlEvents (spec.notes, curves::Kind::pitch, spec.bendRange))
        seq.addControllerEvent (te::BeatPosition::fromBeats (e.t / (double) ticksPerBeat), te::MidiControllerEvent::pitchWheelType, e.value, nullptr);

    for (auto& e : curves::controlEvents (spec.notes, curves::Kind::mod))
        seq.addControllerEvent (te::BeatPosition::fromBeats (e.t / (double) ticksPerBeat), 1, e.value << 7, nullptr);

    // o clipe sem curva num canal que tem curva em outro: o descanso no começo da primeira nota (27/10)
    if ((spec.restPitch || spec.restMod) && ! spec.notes.empty())
    {
        int first = spec.notes.front().start;

        for (auto& n : spec.notes)
            first = std::min (first, n.start);

        const auto at = te::BeatPosition::fromBeats (first / (double) ticksPerBeat);

        if (spec.restPitch)
            seq.addControllerEvent (at, te::MidiControllerEvent::pitchWheelType, curves::wheelCentre, nullptr);

        if (spec.restMod)
            seq.addControllerEvent (at, 1, 0, nullptr);
    }
}

void Projector::syncRouting (const Project& p)
{
    auto routeTo = [this, &p] (te::AudioTrack& track, const juce::String& output)
    {
        const auto target = p.resolveOutput (output);
        auto it = mixer.find (target);

        if (target == masterTrackId || it == mixer.end() || it->second.track == nullptr)
        {
            if (track.getOutput().getDestinationTrack() != nullptr || ! track.getOutput().usesDefaultAudioOut())
                track.getOutput().setOutputToDefaultDevice (false);
        }
        else if (track.getOutput().getDestinationTrack() != it->second.track.get())
        {
            track.getOutput().setOutputToTrack (it->second.track.get());
        }
    };

    // A trilha escondida (fader em -inf) recebe as fontes de sidechain que o solo calou; criada na 1ª vez.
    if (! soloPlan.toSink.isEmpty() && sidechainSink == nullptr)
    {
        sidechainSink = newTrack ("(sidechain do solo)");

        if (sidechainSink != nullptr)
            if (auto* vol = sidechainSink->getVolumePlugin())
                vol->setVolumeDb (-100.0f);
    }

    for (auto& m : p.mixer)
    {
        if (m.isMaster())
            continue;

        if (auto it = mixer.find (m.id); it != mixer.end() && it->second.track != nullptr)
        {
            if (soloPlan.toSink.contains (m.id) && sidechainSink != nullptr)
            {
                if (it->second.track->getOutput().getDestinationTrack() != sidechainSink.get())
                    it->second.track->getOutput().setOutputToTrack (sidechainSink.get());
            }
            else
            {
                routeTo (*it->second.track, m.output);
            }
        }
    }

    for (auto& c : p.channels)
        if (auto it = channels.find (c.id); it != channels.end() && it->second.track != nullptr)
            routeTo (*it->second.track, c.output);
}

void Projector::planSolo (const Project& p)
{
    soloPlan = {};

    // Caminho de cada trilha até o master: ela mesma e todas as que vêm depois.
    auto pathOf = [&p] (juce::String id)
    {
        juce::StringArray path;

        for (int guard = 0; guard < 256 && id != masterTrackId && ! path.contains (id); ++guard)
        {
            path.add (id);
            auto* m = p.findMixer (id);

            if (m == nullptr)
                break;

            id = p.resolveOutput (m->output);
        }

        return path;
    };

    juce::StringArray soloed;

    for (auto& m : p.mixer)
        if (m.solo && ! m.isMaster())
            soloed.add (m.id);

    // Com solo ativo, toca o que chega numa trilha em solo pela saída ou por envio: a voz que manda para
    // o bus de reverb em solo continua tocando (como no FL, com a voz seca junto; revisão de 22/10).
    auto feedsSoloed = [&] (const juce::String& id)
    {
        for (auto& s : soloed)
            if (id == s || p.reaches (id, s, false))
                return true;

        return false;
    };

    // Uma trilha de mixer também precisa tocar se vem depois da trilha em solo (é por ela
    // que o som em solo chega ao master) ou se recebe um envio dela (o reverb da voz em solo).
    // O sidechain não faz tocar: solar o kick não faz o 808 tocar.
    auto carriesSoloed = [&] (const juce::String& id)
    {
        for (auto& s : soloed)
            if (pathOf (s).contains (id) || p.reaches (s, id, false))
                return true;

        return false;
    };

    auto mutedOnPath = [&p] (const juce::StringArray& path)
    {
        for (auto& id : path)
            if (auto* m = p.findMixer (id); m != nullptr && m->mute)
                return true;

        return false;
    };

    // Mas o sidechain de quem toca continua agindo (revisão de 22/10, como no FL): com o 808 em solo, o
    // kick que dispara o compressor dele continua processando, sem som. No Tracktion uma trilha muda cala
    // também o que entra nela (AudioTrack::isMuted com o destino), e o disparo sai depois do fader: por isso
    // a fonte não fica muda, e sim toca para uma trilha escondida com o fader em -inf (sidechainSink), com os
    // envios calados. O mute do usuário corta o disparo.
    juce::StringArray needed;

    auto feedsNeeded = [&] (const juce::StringArray& path, const juce::String& self)
    {
        for (auto& n : needed)
        {
            if (n == self || ! path.contains (n))
                continue;

            bool userMuted = false;

            for (auto& id : path)
            {
                if (auto* m = p.findMixer (id); m != nullptr && m->mute)
                    userMuted = true;

                if (id == n)
                    break;
            }

            if (! userMuted)
                return true;
        }

        return false;
    };

    auto& muted = soloPlan.mixerMuted;

    for (int pass = 0; pass < 16; ++pass)
    {
        for (auto& m : p.mixer)
            if (! m.isMaster())
                muted[m.id] = m.mute || (! soloed.isEmpty() && ! feedsSoloed (m.id) && ! carriesSoloed (m.id)
                                                             && ! feedsNeeded (pathOf (m.id), m.id));

        if (soloed.isEmpty())
            break;

        // As fontes de sidechain de quem toca (e de quem processa para disparar), até não mudar mais.
        const int before = needed.size();

        for (auto& m : p.mixer)
        {
            if (! m.isMaster() && muted[m.id] && ! needed.contains (m.id))
                continue;

            for (auto& ins : m.inserts)
            {
                if (ins.bypass || ins.sidechain.isEmpty() || ins.sidechain == m.id || needed.contains (ins.sidechain))
                    continue;

                // só quem o solo calou: quem já toca dispara com o que está tocando
                if (auto* source = p.findMixer (ins.sidechain); source != nullptr && ! source->isMaster() && ! source->mute && muted[source->id])
                    needed.add (source->id);
            }
        }

        if (needed.size() == before)
            break;
    }

    // A fonte calada pelo solo vai para a trilha escondida; quem toca só para alimentar uma fonte não manda
    // para os buses (o reverb do kick não aparece no solo do 808).
    for (auto& m : p.mixer)
    {
        if (m.isMaster() || m.mute || soloed.isEmpty() || feedsSoloed (m.id) || carriesSoloed (m.id))
            continue;

        if (needed.contains (m.id) && muted[m.id])
        {
            muted[m.id] = false;
            soloPlan.toSink.add (m.id);
            soloPlan.silentSends.add (m.id);
        }
        else if (! muted[m.id])
        {
            soloPlan.silentSends.add (m.id);   // toca só para dentro de uma fonte de disparo
        }
    }

    for (auto& c : p.channels)
    {
        const auto first = p.resolveOutput (c.output);
        const auto path = pathOf (first);
        const bool sounds = first != masterTrackId && (feedsSoloed (first) || feedsNeeded (path, {}));
        soloPlan.channelMuted[c.id] = c.mute || mutedOnPath (path) || (! soloed.isEmpty() && ! sounds);
    }
}

void Projector::syncMuteSolo (const Project& p)
{
    for (auto& m : p.mixer)
    {
        if (m.isMaster())
            continue;

        if (auto it = mixer.find (m.id); it != mixer.end() && it->second.track != nullptr)
        {
            const bool mute = soloPlan.mixerMuted[m.id];

            if (it->second.track->isMuted (false) != mute)
                it->second.track->setMute (mute);
        }
    }

    for (auto& c : p.channels)
    {
        if (auto it = channels.find (c.id); it != channels.end() && it->second.track != nullptr)
        {
            const bool mute = soloPlan.channelMuted[c.id];

            if (it->second.track->isMuted (false) != mute)
                it->second.track->setMute (mute);
        }
    }
}

//==============================================================================
te::AutomatableParameter::Ptr Projector::parameterFor (const AutomationTarget& target) const
{
    if (target.kind == "mixer")
    {
        if (auto* vol = volumeFor (target.track))
            return target.param == "volume" ? vol->volParam : target.param == "pan" ? vol->panParam : nullptr;
    }
    else if (target.kind == "insert")
    {
        if (auto* plugin = insertPlugin (target.track, target.insert))
            return plugin->getAutomatableParameterByID (target.param);
    }
    else if (target.kind == "channel")
    {
        if (auto* plugin = instrumentFor (target.channel))
            return plugin->getAutomatableParameterByID (target.param);
    }

    return {};
}

void Projector::syncAutomation (const Project& p)
{
    // O que cada parâmetro deve ter: pontos em segundos, pelo mapa de tempo (como os clipes: mudar o
    // BPM leva a curva junto), com a posição do knob (v, 0 a 1) na faixa do parâmetro do Tracktion,
    // que interpola reto: o volume é o fader (0 a 1), o pan vai de -1 a 1, os efeitos e os VST3 de 0 a 1.
    // Só no modo SONG, como os clipes de automação do FL; no PAT o parâmetro fica no valor do knob.
    std::map<te::AutomatableParameter*, AppliedCurve> desired;
    const juce::ScopedValueSetter<bool> applying (applyingParameters, true);

    if (songMode)
    {
        // O volume e o pan de uma trilha do mixer saem juntos: a lei de pan mexe nos dois.
        std::map<juce::String, std::pair<const AutomationLane*, const AutomationLane*>> faderLanes;   // trilha → (volume, pan)

        for (auto& lane : p.automation)
        {
            if (lane.points.empty() || automationHeld.count (lane.target.key()) > 0)   // gravando: a pessoa segura
                continue;

            if (lane.target.kind == "mixer" && (lane.target.param == "volume" || lane.target.param == "pan"))
            {
                auto& both = faderLanes[lane.target.track];
                (lane.target.param == "volume" ? both.first : both.second) = &lane;
                continue;
            }

            auto param = parameterFor (lane.target);

            if (param == nullptr)
                continue;

            auto& curve = desired[param.get()];
            curve.param = param;
            curve.owner = param->getPlugin();

            // a tensão vira retas curtas (o Tracktion interpola reto)
            for (auto& pt : automation::bake (automation::pointsOf (lane)))
                curve.points.push_back ({ toTime (pt.t).inSeconds(), param->valueRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, pt.v)) });
        }

        for (auto& [trackId, lanes] : faderLanes)
            if (auto* m = p.findMixer (trackId))
                faderCurves (*m, lanes.first, lanes.second, desired);
    }

    // Curva que saiu (ou o modo voltou para PAT): o parâmetro volta ao valor do modelo.
    // O Tracktion relê a curva num temporizador de 10 ms, que não roda num render sem janela:
    // updateStream() faz isso agora. O plugin dono fica vivo (Ptr) até a curva sair dele.
    bool cleared = false;

    for (auto it = automated.begin(); it != automated.end();)
    {
        if (desired.count (it->first) == 0)
        {
            auto param = it->second.param;
            param->getCurve().clear (nullptr);
            param->updateStream();
            it = automated.erase (it);
            cleared = true;

            // (26/10) um VST3 volta ao valor de antes da curva; segurado pela gravação, fica na mão da pessoa
            if (auto base = pluginBases.find (param.get()); base != pluginBases.end())
            {
                bool held = false;

                for (auto& lane : p.automation)
                    held = held || (automationHeld.count (lane.target.key()) > 0 && parameterFor (lane.target) == param);

                if (! held)
                {
                    param->setParameter (base->second.value, juce::dontSendNotification);
                    pluginBases.erase (base);
                }
            }
        }
        else
        {
            ++it;
        }
    }

    for (auto& [raw, curve] : desired)
    {
        // (26/10) a primeira curva de um parâmetro VST3: o valor de agora é o de antes da curva
        if (automated.count (raw) == 0 && pluginBases.count (raw) == 0 && dynamic_cast<te::ExternalPlugin*> (curve.owner.get()) != nullptr)
            pluginBases[raw] = { curve.param, curve.owner, curve.param->getCurrentValue() };

        auto& applied = automated[raw];

        if (applied.param == curve.param && applied.points == curve.points)
            continue;

        auto& c = curve.param->getCurve();
        c.clear (nullptr);

        for (auto& [seconds, value] : curve.points)
            c.addPoint (te::TimePosition::fromSeconds (seconds), value, 0.0f, nullptr);

        curve.param->updateStream();
        applied = curve;
    }

    // O volume e o pan que saíram da automação voltam ao valor do mixer (os efeitos nativos voltam
    // sozinhos, no WhynotFxPlugin; um VST3 fica onde a curva o deixou, como no FL).
    if (cleared)
    {
        for (auto& m : p.mixer)
            if (auto* vol = volumeFor (m.id))
                applyFaderAndPan (*vol, m.volumeDb, m.pan);
    }
}

void Projector::faderCurves (const MixerTrack& m, const AutomationLane* volumeLane, const AutomationLane* panLane,
                             std::map<te::AutomatableParameter*, AppliedCurve>& desired)
{
    auto* vol = volumeFor (m.id);

    if (volumeLane != nullptr && automationHeld.count (volumeLane->target.key()) > 0)   // gravando: a pessoa segura
        volumeLane = nullptr;

    if (panLane != nullptr && automationHeld.count (panLane->target.key()) > 0)
        panLane = nullptr;

    if (vol == nullptr || (volumeLane == nullptr && panLane == nullptr))
        return;

    // Os pontos do modelo em segundos: o volume na posição do fader (0 a 1), o pan de -1 a 1.
    using Points = std::vector<std::pair<double, float>>;

    auto pointsOf = [this] (const AutomationLane* lane, const juce::NormalisableRange<float>& range)
    {
        Points out;

        if (lane != nullptr)
            for (auto& pt : automation::bake (automation::pointsOf (*lane)))   // a tensão vira retas curtas
                out.push_back ({ toTime (pt.t).inSeconds(), range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, pt.v)) });

        return out;
    };

    // Como o Tracktion lê uma curva: reta entre os pontos, o primeiro valor antes e o último depois.
    auto valueAt = [] (const Points& pts, double t)
    {
        if (t <= pts.front().first)
            return pts.front().second;

        for (size_t i = 1; i < pts.size(); ++i)
        {
            if (t <= pts[i].first)
            {
                const auto& [t0, v0] = pts[i - 1];
                const auto& [t1, v1] = pts[i];
                return t1 > t0 ? v0 + (v1 - v0) * (float) ((t - t0) / (t1 - t0)) : v1;
            }
        }

        return pts.back().second;
    };

    const auto volumePoints = pointsOf (volumeLane, vol->volParam->valueRange);
    const auto panPoints = pointsOf (panLane, vol->panParam->valueRange);

    // O pan anda reto no modelo; a lei não é reta: 8 pontos por trecho seguem a curva de perto.
    Points panSamples;

    for (size_t i = 0; i < panPoints.size(); ++i)
    {
        panSamples.push_back (panPoints[i]);

        if (i + 1 < panPoints.size() && panPoints[i + 1].first > panPoints[i].first)
            for (int k = 1; k < 8; ++k)
            {
                const double t = panPoints[i].first + (panPoints[i + 1].first - panPoints[i].first) * k / 8.0;
                panSamples.push_back ({ t, valueAt (panPoints, t) });
            }
    }

    if (! panSamples.empty())
    {
        auto& curve = desired[vol->panParam.get()];
        curve.param = vol->panParam;
        curve.owner = vol;

        for (auto& [t, pan] : panSamples)
            curve.points.push_back ({ t, constantPower (pan).enginePan });
    }

    // O volume desce o que a lei pede para o pan daquele instante (mesmo sem curva de volume).
    struct Event { double t; float fader, pan; };
    std::vector<Event> events;
    const float staticFader = te::decibelsToVolumeFaderPosition (m.volumeDb);

    for (auto& [t, fader] : volumePoints)
        events.push_back ({ t, fader, panPoints.empty() ? m.pan : valueAt (panPoints, t) });

    for (auto& [t, pan] : panSamples)
        events.push_back ({ t, volumePoints.empty() ? staticFader : valueAt (volumePoints, t), pan });

    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.t < b.t; });

    auto& curve = desired[vol->volParam.get()];
    curve.param = vol->volParam;
    curve.owner = vol;

    for (auto& e : events)
    {
        // posição do fader = exp ((dB - 6) / 20): descer x dB é multiplicar por exp (x / 20)
        const float fader = e.fader * std::exp (constantPower (e.pan).volumeOffsetDb / 20.0f);
        curve.points.push_back ({ e.t, juce::jlimit (0.0f, 1.0f, fader) });
    }
}

void Projector::setAutomationHold (const std::set<juce::String>& keys, const Project& p)
{
    if (keys == automationHeld)
        return;

    automationHeld = keys;
    syncAutomation (p);
}

void Projector::rememberPluginBase (const AutomationTarget& target, float value)
{
    if (auto param = parameterFor (target); param != nullptr && dynamic_cast<te::ExternalPlugin*> (param->getPlugin()) != nullptr)
        if (pluginBases.count (param.get()) == 0)
            pluginBases[param.get()] = { param, param->getPlugin(), juce::jlimit (0.0f, 1.0f, value) };
}

std::optional<float> Projector::pluginBaseFor (const AutomationTarget& target) const
{
    if (auto param = parameterFor (target))
        if (auto it = pluginBases.find (param.get()); it != pluginBases.end())
            return it->second.value;

    return std::nullopt;
}

//==============================================================================
bool Projector::isFrozen (const juce::String& mixerId) const
{
    auto it = mixer.find (mixerId);
    return it != mixer.end() && it->second.track != nullptr && it->second.track->isFrozen (te::Track::individualFreeze);
}

juce::File Projector::freezeFileFor (const juce::String& mixerId) const
{
    auto it = mixer.find (mixerId);
    return isFrozen (mixerId) ? te::TemporaryFileManager::getFreezeFileForTrack (*it->second.track) : juce::File();
}

Projector::FreezeKey Projector::freezeKey (const Project& p, const MixerTrack& m) const
{
    // Só o que chega no som congelado: os canais que saem por esta trilha (pela saída, não por envio), com o
    // que eles tocam (os clipes que o syncChannels acabou de projetar), as trilhas que chegam nela, os efeitos
    // dela e as curvas desses efeitos. O fader, o pan e os envios ficam depois do congelamento.
    FreezeKey key;
    key.bpm = p.bpm;
    key.timeSigNum = p.timeSigNum;
    key.timeSigDen = p.timeSigDen;
    key.audition = auditionBypass;   // a comparação A/B desliga os efeitos: o que congelou nela não vale depois

    for (auto& c : p.channels)
    {
        if (const auto out = p.resolveOutput (c.output); out != m.id && ! p.reaches (out, m.id, false))
            continue;

        key.channels.push_back (c);

        if (auto it = channels.find (c.id); it != channels.end())
        {
            key.midi.push_back (it->second.midi);
            key.audio.push_back (it->second.audio);
        }
        else
        {
            key.midi.emplace_back();
            key.audio.emplace_back();
        }
    }

    for (auto& other : p.mixer)
    {
        if (other.id != m.id && ! p.reaches (other.id, m.id, false))
            continue;

        auto copy = other;

        if (copy.id == m.id)
        {
            copy.volumeDb = 0.0f;   // o fader, o pan e os envios ficam depois do congelamento
            copy.pan = 0.0f;
            copy.sends.clear();
            copy.frozen = false;
        }

        key.mixer.push_back (copy);
    }

    for (auto& lane : p.automation)
        if (lane.target.kind == "insert" && lane.target.track == m.id)
            key.automation.push_back (lane);

    return key;
}

bool Projector::isRefreezeDue() const
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    for (auto& [id, pending] : pendingFreezes)
        if (now - pending.changedAtMs >= refreezeDelaySeconds * 1000.0)
            return true;

    return false;
}

void Projector::readCompressedClipsDirectly (te::Edit& e)
{
    for (auto* track : te::getAudioTracks (e))
        for (auto* clip : track->getClips())
            if (auto* audio = dynamic_cast<te::AudioClipBase*> (clip))
                if (audio->getAudioFile().getInfo().needsCachedProxy)
                    audio->setUsesProxy (false);
}

void Projector::syncFreeze (const Project& p)
{
    freezeIssues.clear();

    // O render monta o próprio Edit, sem nada congelado: o que sai no WAV é sempre o som vivo.
    if (mode != Mode::followProject)
        return;

    const double now = juce::Time::getMillisecondCounterHiRes();

    for (auto& m : p.mixer)
    {
        auto it = mixer.find (m.id);

        if (m.isMaster() || it == mixer.end() || it->second.track == nullptr)
            continue;

        auto& track = *it->second.track;
        const bool frozenNow = track.isFrozen (te::Track::individualFreeze);

        if (! m.frozen)
        {
            switchOnInputs (m.id);

            if (frozenNow)
                (void) track.setFrozen (false, te::Track::individualFreeze);

            frozenSignatures.erase (m.id);
            pendingFreezes.erase (m.id);
            continue;
        }

        auto key = freezeKey (p, m);

        if (frozenNow && frozenSignatures.count (m.id) != 0 && frozenSignatures[m.id] == key)
            continue;

        // O conteúdo de uma trilha que já estava congelada mudou: com espera, ela toca ao vivo agora e o render
        // sai depois que a pessoa parar de mexer (um render por trilha na thread da tela, não um por clique).
        // A primeira vez (a pessoa acabou de congelar, ou abriu o projeto) renderiza na hora.
        const bool wasRendered = frozenSignatures.count (m.id) != 0 || pendingFreezes.count (m.id) != 0;

        if (refreezeDelaySeconds > 0.0 && wasRendered)
        {
            auto& pending = pendingFreezes[m.id];

            if (pending.changedAtMs <= 0.0 || ! (pending.key == key))
            {
                pending.key = std::move (key);
                pending.changedAtMs = now;
            }

            if (frozenNow)
            {
                switchOnInputs (m.id);   // ao vivo até o render novo
                (void) track.setFrozen (false, te::Track::individualFreeze);
            }

            frozenSignatures.erase (m.id);

            if (now - pending.changedAtMs < refreezeDelaySeconds * 1000.0)
                continue;

            key = pending.key;
        }

        pendingFreezes.erase (m.id);
        switchOnInputs (m.id);   // o render do congelamento precisa dos instrumentos ligados

        if (track.isFrozen (te::Track::individualFreeze))
            (void) track.setFrozen (false, te::Track::individualFreeze);   // o conteúdo mudou: renderiza de novo

        // o render do congelamento roda aqui, na thread de mensagem: um clipe de MP3 não pode esperar o proxy
        readCompressedClipsDirectly (edit);

        if (const auto result = track.setFrozen (true, te::Track::individualFreeze); result.failed())
        {
            freezeIssues.set (m.id, result.getErrorMessage());
            frozenSignatures.erase (m.id);
        }
        else
        {
            frozenSignatures[m.id] = std::move (key);
            switchOffInputs (m.id, track);

            // o ponto de congelar entrou antes do fader: os envios pré-fader vão para depois dele
            for (auto& snd : m.sends)
                if (auto sent = it->second.sends.find (snd.to); sent != it->second.sends.end() && sent->second != nullptr)
                    placeSend (track, sent->second, snd.preFader);
        }
    }

    for (auto it = frozenSignatures.begin(); it != frozenSignatures.end();)
        it = p.findMixer (it->first) == nullptr ? frozenSignatures.erase (it) : std::next (it);

    for (auto it = pendingFreezes.begin(); it != pendingFreezes.end();)
        it = p.findMixer (it->first) == nullptr ? pendingFreezes.erase (it) : std::next (it);

    for (auto it = frozenOff.begin(); it != frozenOff.end();)
    {
        if (p.findMixer (it->first) == nullptr)
        {
            for (auto& plugin : it->second)
                plugin->setEnabled (true);

            it = frozenOff.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

void Projector::switchOffInputs (const juce::String& mixerId, te::AudioTrack& track)
{
    // Congelada, o motor toca o WAV e nem monta os canais que entram nela; desligar os plugins deles
    // deixa isso explícito (e visível para a tela e o autoteste). Volume e medidor ficam como estão.
    auto& off = frozenOff[mixerId];

    for (auto* input : track.getInputTracks())
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (input))
        {
            for (auto* plugin : at->pluginList)
            {
                if (plugin == nullptr || ! plugin->isEnabled()
                    || dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (plugin) != nullptr)
                    continue;

                plugin->setEnabled (false);
                off.push_back (plugin);
            }
        }
    }
}

void Projector::switchOnInputs (const juce::String& mixerId)
{
    if (auto it = frozenOff.find (mixerId); it != frozenOff.end())
    {
        for (auto& plugin : it->second)
            plugin->setEnabled (true);

        frozenOff.erase (it);
    }
}

//==============================================================================
te::Plugin::Ptr Projector::createPluginFor (const Insert& ins)
{
    if (ins.kind == "whynot")
    {
        if (dsp::findEffect (ins.type.toStdString()) == nullptr)
            return {};

        return edit.getPluginCache().createNewPlugin (WhynotFxPlugin::pluginTypeFor (ins.type), {});
    }

    if (ins.kind == "vst3" && ! ins.plugin.isEmpty())
        return plugin_host::create (edit, ins.plugin);

    return {};
}

void Projector::syncAudition()
{
    const bool needTrim = std::abs (auditionTrimDb) > 0.001f;

    if (auditionTrim == nullptr && ! needTrim)
        return;

    if (auditionTrim == nullptr)
    {
        auditionTrim = edit.getPluginCache().createNewPlugin (WhynotFxPlugin::pluginTypeFor ("stereo"), {});

        if (auditionTrim == nullptr)
            return;

        edit.getMasterPluginList().insertPlugin (auditionTrim, -1, nullptr);   // depois do medidor do master
    }

    if (auto* fx = dynamic_cast<WhynotFxPlugin*> (auditionTrim.get()))
    {
        Insert trim;
        trim.id = "audition-trim";
        trim.type = "stereo";
        trim.params["gain"] = juce::jlimit (-24.0f, 24.0f, auditionTrimDb);
        trim.bypass = ! needTrim;
        fx->applyInsert (trim, appliedBpm > 0.0 ? appliedBpm : 120.0);
    }
}

void Projector::syncSends (const Project& p)
{
    // Cada trilha que recebe envio ganha um bus aux, na ordem do mixer (estável entre syncs).
    std::map<juce::String, int> buses;

    for (auto& target : p.mixer)
    {
        if (target.isMaster() || (int) buses.size() >= 32)
            continue;

        for (auto& source : p.mixer)
            if (std::any_of (source.sends.begin(), source.sends.end(), [&target] (const Send& s) { return s.to == target.id; }))
            {
                buses[target.id] = (int) buses.size();
                break;
            }
    }

    busNumbers = buses;

    for (auto& [id, b] : mixer)
    {
        if (b.track == nullptr)
            continue;

        auto bus = buses.find (id);

        if (bus == buses.end())
        {
            if (b.auxReturn != nullptr)
            {
                b.auxReturn->deleteFromParent();
                b.auxReturn = nullptr;
            }

            continue;
        }

        if (b.auxReturn == nullptr)
        {
            b.auxReturn = edit.getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});

            if (b.auxReturn != nullptr)
                b.track->pluginList.insertPlugin (b.auxReturn, 0, nullptr);
        }

        if (auto* ret = dynamic_cast<te::AuxReturnPlugin*> (b.auxReturn.get()); ret != nullptr && ret->busNumber.get() != bus->second)
            ret->busNumber = bus->second;
    }

    for (auto& m : p.mixer)
    {
        auto it = mixer.find (m.id);

        if (m.isMaster() || it == mixer.end() || it->second.track == nullptr)
            continue;

        auto& b = it->second;

        for (auto s = b.sends.begin(); s != b.sends.end();)
        {
            if (m.findSend (s->first) == nullptr || buses.count (s->first) == 0)
            {
                if (s->second != nullptr)
                    s->second->deleteFromParent();

                s = b.sends.erase (s);
            }
            else
            {
                ++s;
            }
        }

        for (auto& snd : m.sends)
        {
            auto bus = buses.find (snd.to);

            if (bus == buses.end())
                continue;

            auto& plugin = b.sends[snd.to];

            if (plugin == nullptr)
            {
                plugin = edit.getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});

                if (plugin == nullptr)
                    continue;
            }

            placeSend (*b.track, plugin, snd.preFader);

            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin.get()))
            {
                if (send->busNumber.get() != bus->second)
                    send->busNumber = bus->second;

                // Na comparação "sem efeitos" os envios ficam desligados: com os inserts do bus de reverb em
                // bypass, o retorno devolveria o sinal seco e a comparação sairia mais alta.
                // No solo, quem só toca para disparar um sidechain também não manda (planSolo).
                const float level = auditionBypass || soloPlan.silentSends.contains (m.id) ? -100.0f : snd.levelDb;

                if (std::abs (send->getGainDb() - level) > 0.01f)
                    send->setGainDb (level);
            }
        }
    }
}

void Projector::syncInserts (const Project& p)
{
    fxProblems.clear();

    for (auto& m : p.mixer)
    {
        if (m.isMaster())
        {
            syncInsertList (masterBinding, edit.getMasterPluginList(), m, p, masterMeter.get());
            continue;
        }

        if (auto it = mixer.find (m.id); it != mixer.end() && it->second.track != nullptr)
            syncInsertList (it->second, it->second.track->pluginList, m, p, it->second.track->getVolumePlugin());
    }
}

void Projector::syncInsertList (MixerBinding& b, te::PluginList& list, const MixerTrack& m, const Project& p, te::Plugin* anchor)
{
    const int catalogVersion = catalog != nullptr ? catalog->getVersion() : 0;

    // 1) Sai o que saiu do modelo (ou mudou de tipo, ou virou outro VST3, ou é um VST3 que não
    //    abriu e o catálogo mudou desde a tentativa: scan novo ou desbloqueio).
    for (auto it = b.inserts.begin(); it != b.inserts.end();)
    {
        const auto* ins = m.findInsert (it->id);
        const bool otherPlugin = ins != nullptr && ins->kind == "vst3"
                                  && (ins->plugin.identity() != it->identity
                                      || (plugin_host::instanceOf (it->plugin.get()) == nullptr && it->triedVersion != catalogVersion));

        if (ins == nullptr || ins->kind != it->kind || ins->type != it->type || it->plugin == nullptr || otherPlugin)
        {
            // solta antes de apagar: a janela que fecha guarda o estado, e não pode achar o plugin velho
            auto old = std::exchange (it->plugin, nullptr);
            it = b.inserts.erase (it);

            if (old != nullptr)
                old->deleteFromParent();
        }
        else
        {
            ++it;
        }
    }

    // 2) Entra o que é novo, já na ordem do modelo.
    std::vector<InsertBinding> ordered;

    for (auto& ins : m.inserts)
    {
        auto found = std::find_if (b.inserts.begin(), b.inserts.end(), [&ins] (const InsertBinding& x) { return x.id == ins.id; });

        if (found != b.inserts.end())
        {
            ordered.push_back (*found);
            continue;
        }

        InsertBinding nb { ins.id, ins.kind, ins.type, createPluginFor (ins), ins.plugin.identity(), catalogVersion };

        if (nb.plugin == nullptr)
        {
            fxProblems.set (ins.id, ins.kind == "whynot" ? "Efeito desconhecido: "_t + ins.type
                                                         : "Plugin indisponível: "_t + ins.type);
            continue;
        }

        ordered.push_back (std::move (nb));
    }

    b.inserts = std::move (ordered);

    // 3) Na lista do Tracktion: [retorno de envio] + inserts, antes do volume; no master,
    //    o fader dele + inserts, antes do medidor.
    std::vector<te::Plugin::Ptr> desired;

    if (b.auxReturn != nullptr)
        desired.push_back (b.auxReturn);

    if (&b == &masterBinding && masterFader != nullptr)
        desired.push_back (masterFader);

    for (auto& ib : b.inserts)
        desired.push_back (ib.plugin);

    bool inPlace = list.size() >= (int) desired.size() + (anchor != nullptr ? 1 : 0);

    for (int i = 0; inPlace && i < (int) desired.size(); ++i)
        inPlace = list[i] == desired[(size_t) i].get();

    // Trilha congelada (3.5): o Tracktion põe um FreezePointPlugin logo antes do fader; ele fica onde está.
    // Os envios pré-fader também moram ali (syncSends).
    int anchorIndex = (int) desired.size();

    while (anchorIndex < list.size() && (dynamic_cast<te::FreezePointPlugin*> (list[anchorIndex]) != nullptr
                                          || dynamic_cast<te::AuxSendPlugin*> (list[anchorIndex]) != nullptr))
        ++anchorIndex;

    if (inPlace && anchor != nullptr)
        inPlace = anchorIndex < list.size() && list[anchorIndex] == anchor;

    if (! inPlace)
    {
        for (auto& plugin : desired)
        {
            if (! list.contains (plugin.get()))
                continue;

            // VST3: o estado vai para o ValueTree antes, caso o Tracktion recrie a instância ao reinserir
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get()); ext != nullptr && ext->getAudioPluginInstance() != nullptr)
                ext->flushPluginStateToValueTree();

            plugin->removeFromParent();
        }

        for (int i = 0; i < (int) desired.size(); ++i)
            list.insertPlugin (desired[(size_t) i], i, nullptr);
    }

    // O motor pode recusar (limite de plugins): o efeito fica no modelo, com o aviso.
    for (auto& ib : b.inserts)
        if (! list.contains (ib.plugin.get()))
            fxProblems.set (ib.id, "O motor recusou este efeito (limite de plugins da trilha)."_t);

    // 4) Valores, bypass e sidechain.
    for (auto& ib : b.inserts)
    {
        const auto* ins = m.findInsert (ib.id);

        if (ins == nullptr)
            continue;

        // baixa latência ao gravar: desligado, o plugin sai do grafo (o motor tira os desligados enquanto
        // dura) e o atraso dele some da escuta; o take é seco de qualquer jeito
        // (congelada, o motor desliga os efeitos antes do fader: não religa por cima, 3.5)
        const bool lowLatencyCut = lowLatencyOff.count (ib.id) > 0;
        const bool frozen = b.track != nullptr && b.track->isFrozen (te::Track::individualFreeze);

        if (auto* fx = dynamic_cast<WhynotFxPlugin*> (ib.plugin.get()))
        {
            fx->setOwner (m.id, ins->id);

            if (! frozen && fx->isEnabled() == lowLatencyCut)
                fx->setEnabled (! lowLatencyCut);

            if (auditionBypass)
            {
                auto heard = *ins;   // a comparação não toca no modelo: bypass só no motor
                heard.bypass = true;
                fx->applyInsert (heard, p.bpm);
            }
            else
            {
                fx->applyInsert (*ins, p.bpm);
            }
        }
        else if (ins->kind == "vst3")
        {
            const bool off = ins->bypass || auditionBypass || lowLatencyCut;

            if (! frozen && ib.plugin->isEnabled() == off)
                ib.plugin->setEnabled (! off);

            if (const auto problem = plugin_host::problemFor (ib.plugin.get(), ins->plugin, catalog); problem.isNotEmpty())
                fxProblems.set (ib.id, problem);
        }

        te::EditItemID source;

        if (ins->sidechain.isNotEmpty())
            if (auto* sb = bindingFor (ins->sidechain); sb != nullptr && sb->track != nullptr)
                source = sb->track->itemID;

        // VST3: só com entrada de sidechain (mais de 2 entradas). Sem ela, a fiação somaria o disparo na
        // entrada principal do plugin. Ainda abrindo (0 entradas), fica para o próximo sync.
        if (source.isValid() && ins->kind == "vst3")
        {
            juce::StringArray inputs;
            ib.plugin->getChannelNames (&inputs, nullptr);

            if (inputs.size() <= 2)
            {
                source = {};

                if (inputs.size() > 0)
                    fxProblems.set (ib.id, "Este plugin não tem entrada de sidechain: o disparo ficou desligado."_t);
            }
        }

        if (ib.plugin->getSidechainSourceID() != source)
        {
            ib.plugin->setSidechainSourceID (source);

            if (source.isValid() && ib.plugin->getNumWires() == 0)
                ib.plugin->guessSidechainRouting();
        }
    }
}

} // namespace whynot
