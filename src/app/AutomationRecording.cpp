#include "AutomationRecording.h"
#include "AppController.h"
#include "../automation/AutomationTarget.h"

namespace whynot
{

namespace
{
    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto* o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    juce::var targetVar (const AutomationTarget& t)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", t.kind);

        if (t.track.isNotEmpty())    o->setProperty ("track", t.track);
        if (t.channel.isNotEmpty())  o->setProperty ("channel", t.channel);
        if (t.insert.isNotEmpty())   o->setProperty ("insert", t.insert);

        o->setProperty ("param", t.param);
        return juce::var (o);
    }

    /** Os comandos dos controles que a gravação escreve: eles abrem a volta em vez de fechá-la. */
    bool isControl (const juce::String& id)
    {
        return id == "mixer.setVolume" || id == "mixer.setPan" || id == "mixer.setInsertParam" || id == "mixer.setMacro";
    }

    constexpr float sameValue = 1.0e-5f;   // na posição do knob: abaixo disso não mexeu
}

AutomationRecording::AutomationRecording (AppController& a) : app (a)
{
    clock = [this]() -> std::optional<int>
    {
        const auto& p = app.getModel().get();

        if (! app.isPlaying() || ! p.songMode)
            return std::nullopt;

        return juce::roundToInt (secondsToTicks (app.getEdit().getTransport().getPosition().inSeconds(), p.bpm));
    };

    app.getEdit().getParameterChangeHandler().addListener (*this);
}

AutomationRecording::~AutomationRecording()
{
    stopTimer();
    app.getEdit().getParameterChangeHandler().removeListener (*this);
}

void AutomationRecording::setArmed (bool on)
{
    if (armed == on)
        return;

    if (! on)
    {
        finishLap();
        passActive = false;
        stopTimer();
    }

    armed = on;

    if (armed)
        startTimer (20);
}

//==============================================================================
std::map<juce::String, AutomationRecording::Native> AutomationRecording::nativeValues() const
{
    // o volume, o pan e cada parâmetro visível dos efeitos nativos, de todas as trilhas
    std::map<juce::String, Native> out;
    const auto& p = app.getModel().get();

    for (auto& m : p.mixer)
    {
        AutomationTarget t;
        t.kind = "mixer";
        t.track = m.id;

        for (const char* param : { "volume", "pan" })
        {
            t.param = param;

            if (const auto range = automation::rangeFor (p, t))
                out[t.key()] = { t, *range, t.param == "pan" ? m.pan : m.volumeDb };
        }

        for (auto& ins : m.inserts)
        {
            const auto* info = ins.kind == "vst3" ? nullptr : dsp::findEffect (ins.type.toStdString());

            if (info == nullptr)
                continue;

            AutomationTarget it;
            it.kind = "insert";
            it.track = m.id;
            it.insert = ins.id;

            for (auto& param : info->params)
            {
                if (param.hidden)
                    continue;

                it.param = param.id;
                const auto found = ins.params.find (it.param);
                out[it.key()] = { it, automation::rangeOf (param), found != ins.params.end() ? found->second : param.def };
            }
        }
    }

    return out;
}

std::map<te::AutomatableParameter*, std::pair<AutomationTarget, float>> AutomationRecording::pluginValues() const
{
    // cada parâmetro dos VST3 vivos (instrumentos do rack e efeitos do mixer), com o alvo de automação dele
    std::map<te::AutomatableParameter*, std::pair<AutomationTarget, float>> out;
    const auto& p = app.getModel().get();
    auto& projector = app.getProjector();

    auto add = [&out] (te::Plugin* plugin, AutomationTarget t)
    {
        if (dynamic_cast<te::ExternalPlugin*> (plugin) == nullptr)
            return;

        for (auto* param : plugin->getAutomatableParameters())
        {
            t.param = param->paramID;

            if (automation::isIndex (t.param))
                out[param] = { t, param->getCurrentValue() };
        }
    };

    for (auto& c : p.channels)
    {
        if (! c.isPlugin())
            continue;

        AutomationTarget t;
        t.kind = "channel";
        t.channel = c.id;
        add (projector.instrumentFor (c.id), t);
    }

    for (auto& m : p.mixer)
    {
        for (auto& ins : m.inserts)
        {
            if (ins.kind != "vst3")
                continue;

            AutomationTarget t;
            t.kind = "insert";
            t.track = m.id;
            t.insert = ins.id;
            add (projector.insertPlugin (m.id, ins.id), t);
        }
    }

    return out;
}

void AutomationRecording::snapshot()
{
    natives = nativeValues();
    plugins = pluginValues();
    resnapshot = false;
}

//==============================================================================
bool AutomationRecording::fromAi() const
{
    // a ponte da IA roda cada comando num gesto "IA: ..." (ou num lote): não é a mão da pessoa no controle
    auto& registry = app.getCommands();
    return registry.isInBatch() || registry.gestureName().startsWith ("IA: ");
}

void AutomationRecording::beforeCommand (const juce::String& id)
{
    if (! armed || committing)
        return;

    if (isControl (id) && ! fromAi())
    {
        // o primeiro knob da volta: o gesto abre antes, para o próprio knob entrar no passo da volta
        if (passActive && ! lapOpen && clock && clock().has_value())
            openLap();

        return;
    }

    const auto* info = app.getCommands().find (id);
    const bool edits = info != nullptr && info->changesModel;

    if (edits || id.startsWith ("edit.") || id.startsWith ("project.") || id == "transport.stop")
    {
        finishLap();
        resnapshot = true;   // a edição pode mudar os valores: não é a pessoa mexendo num controle
    }
}

void AutomationRecording::beforeGesture()
{
    if (! armed || committing || ! lapOpen)
        return;

    finishLap();
    resnapshot = true;
}

void AutomationRecording::openLap()
{
    if (lapOpen)
        return;

    if (resnapshot)
        snapshot();   // uma edição de fora acabou de mudar os valores: a volta compara com eles

    app.getCommands().beginGesture ("Gravar automação"_t);
    lapOpen = true;
}

void AutomationRecording::touch (const AutomationTarget& target, bool native, float before, te::AutomatableParameter* param)
{
    const auto key = target.key();

    if (touched.count (key) > 0)
        return;

    openLap();

    Touch t;
    t.target = target;
    t.native = native;
    t.before = before;
    t.param = param;
    touched[key] = std::move (t);
    holdDirty = true;
}

void AutomationRecording::applyHold()
{
    std::set<juce::String> keys;

    for (auto& [key, t] : touched)
        keys.insert (key);

    app.getProjector().setAutomationHold (keys, app.getModel().get());
    holdDirty = false;
}

void AutomationRecording::pluginParameterChanged (te::AutomatableParameter& param, bool isFollowingAutomation)
{
    // a curva tocando (às vezes da thread de áudio) e o próprio app pondo valores não são a pessoa
    if (isFollowingAutomation || ! juce::MessageManager::existsAndIsCurrentThread())
        return;

    if (! armed || ! passActive || committing || app.getProjector().isApplyingParameters())
        return;

    if (dynamic_cast<te::ExternalPlugin*> (param.getPlugin()) == nullptr || ! clock || ! clock().has_value())
        return;

    const auto known = plugins.find (&param);

    if (known == plugins.end())
        return;   // plugin que entrou depois do começo da volta: o snapshot seguinte o conhece

    if (std::abs (param.getCurrentValue() - known->second.second) < sameValue && touched.count (known->second.first.key()) == 0)
        return;

    touch (known->second.first, false, known->second.second, &param);
}

//==============================================================================
void AutomationRecording::poll()
{
    if (! armed)
        return;

    std::optional<int> tick;

    if (clock)
        tick = clock();

    if (! tick.has_value())
    {
        if (passActive)
        {
            finishLap();
            passActive = false;
        }

        return;
    }

    if (! passActive)
    {
        passActive = true;
        lastTick = *tick;
        snapshot();
        return;
    }

    // o loop voltou ao começo: a volta que acabou vira uma passada (até o fim do loop, se ele está logo depois da
    // última medida), e a próxima começa limpa
    if (*tick + ticksPerStep < lastTick)
    {
        std::optional<int> loopEnd;
        const auto& p = app.getModel().get();
        const int end = juce::roundToInt (secondsToTicks (app.getEdit().getTransport().getLoopRange().getEnd().inSeconds(), p.bpm));

        if (end > lastTick && end - lastTick <= ticksPerBeat)
            loopEnd = end;

        finishLap (loopEnd);
        lastTick = *tick;
        snapshot();
        return;
    }

    lastTick = juce::jmax (lastTick, *tick);

    if (resnapshot && ! lapOpen)
        snapshot();

    // controles nativos: o valor no modelo mudou desde o começo da volta = a pessoa mexeu
    const auto now = nativeValues();

    for (auto& [key, n] : now)
    {
        const auto before = natives.find (key);

        if (before == natives.end())
            continue;   // trilha ou efeito que entrou no meio da volta

        if (touched.count (key) == 0
            && std::abs (n.range.toNormalised (n.value) - before->second.range.toNormalised (before->second.value)) >= sameValue)
            touch (n.target, true, before->second.value, nullptr);
    }

    if (holdDirty)
        applyHold();

    // a medida: a posição do knob de cada controle mexido, neste tick
    for (auto& [key, t] : touched)
    {
        float v = 0.0f;

        if (t.native)
        {
            const auto it = now.find (key);

            if (it == now.end())
                continue;

            v = it->second.range.toNormalised (it->second.value);
        }
        else
        {
            if (t.param == nullptr)
                continue;

            v = juce::jlimit (0.0f, 1.0f, t.param->getCurrentNormalisedValue());
        }

        if (! t.samples.empty() && t.samples.back().t >= *tick)
            t.samples.back().v = v;
        else
            t.samples.push_back ({ *tick, v });
    }
}

void AutomationRecording::finishLap (std::optional<int> endTick)
{
    if (! lapOpen || committing)
        return;

    const juce::ScopedValueSetter<bool> busy (committing, true);
    const auto& p = app.getModel().get();
    juce::Array<juce::var> passes;
    juce::StringArray names;

    for (auto& [key, t] : touched)
    {
        auto samples = t.samples;

        if (samples.empty())
            continue;

        // o último valor segura até o fim da volta
        int from = samples.front().t, to = juce::jmax (samples.back().t, endTick.value_or (lastTick));

        if (to > samples.back().t)
            samples.push_back ({ to, samples.back().v });

        // punch: só o trecho entre o início e o fim, com o valor de cada borda medido nela (sem rampa até a curva antiga)
        if (p.punchOn && p.punchOut > p.punchIn)
        {
            const float atIn = automation::positionAt (samples, p.punchIn, 0.0f), atOut = automation::positionAt (samples, p.punchOut, 0.0f);
            const bool coversIn = from <= p.punchIn && to >= p.punchIn, coversOut = from <= p.punchOut && to >= p.punchOut;
            std::erase_if (samples, [&p] (const automation::Point& s) { return s.t < p.punchIn || s.t > p.punchOut; });

            if (coversIn && (samples.empty() || samples.front().t > p.punchIn))
                samples.insert (samples.begin(), automation::Point { p.punchIn, atIn });

            if (coversOut && (samples.empty() || samples.back().t < p.punchOut))
                samples.push_back (automation::Point { p.punchOut, atOut });

            from = juce::jmax (from, p.punchIn);
            to = juce::jmin (to, p.punchOut);
        }

        if (samples.empty() || to < from)
            continue;

        juce::Array<juce::var> points;

        for (auto& s : samples)
            points.add (object ({ { "t", s.t }, { "v", s.v } }));

        auto pass = object ({ { "target", targetVar (t.target) }, { "points", points }, { "from", from }, { "to", to } });

        if (t.native)
            pass.getDynamicObject()->setProperty ("base", t.before);   // o knob volta ao de antes da volta

        passes.add (pass);
    }

    bool wrote = false;

    if (! passes.isEmpty())
    {
        const auto result = app.run ("automation.recordPass", object ({ { "passes", passes } }));
        wrote = result.ok;

        if (result.ok)
        {
            ++laps;

            // um VST3 que ganhou curva nesta volta: o valor de antes da volta é o que ele volta a ter quando a curva
            // sair (antes de soltar o hold, que põe a curva nova no Tracktion)
            for (auto& pass : passes)
                for (auto& [key, t] : touched)
                    if (! t.native && pass["target"]["param"].toString() == t.target.param
                        && pass["target"]["insert"].toString() == t.target.insert && pass["target"]["channel"].toString() == t.target.channel
                        && pass["target"]["track"].toString() == t.target.track)
                        app.getProjector().rememberPluginBase (t.target, t.before);

            if (auto* lanes = result.value["lanes"].getArray())
                for (auto& l : *lanes)
                    names.add (juce::String ((int) l["points"]) + " pontos");
        }
        else
        {
            app.notice ("Gravar automação: "_t + result.error, true);
        }
    }

    touched.clear();
    lapOpen = false;

    auto& registry = app.getCommands();

    if (wrote)
        registry.renameGesture ("Gravar automação"_t);

    registry.endGesture();   // a volta inteira: um passo de desfazer

    // A volta abriu dentro de outro gesto (o arrasto do fader, a rajada do controlador) que ainda está aberto: o passo
    // só sai quando ele fechar, e as voltas seguintes iriam juntas. Fecha agora e reabre o mesmo número de níveis; o
    // dono fecha os dele depois, como sempre.
    if (const int levels = registry.gestureLevels(); levels > 0 && wrote)
    {
        for (int i = 0; i < levels; ++i)
            registry.endGesture();

        for (int i = 0; i < levels; ++i)
            registry.beginGesture ("Gravar automação"_t);
    }
    applyHold();
    resnapshot = true;

    if (wrote)
        app.notice ("Automação gravada ("_t + names.joinIntoString (", ") + "). Ctrl+Z desfaz a passada inteira.", false);
}

} // namespace whynot
