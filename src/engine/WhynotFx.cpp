#include "WhynotFx.h"

namespace whynot
{

namespace
{
    struct FxType : te::PluginManager::BuiltInType
    {
        explicit FxType (const juce::String& t) : BuiltInType (WhynotFxPlugin::pluginTypeFor (t)), fx (t) {}

        te::Plugin::Ptr create (te::PluginCreationInfo info) override   { return new WhynotFxPlugin (info, fx); }

        const juce::String fx;
    };

    std::unique_ptr<dsp::Processor> makeProcessor (const juce::String& fxType)
    {
        auto p = dsp::createProcessor (fxType.toStdString());
        jassert (p != nullptr);   // tipo registrado sem processador: erro de programação
        return p != nullptr ? std::move (p) : dsp::createProcessor ("meter");
    }
}

WhynotFxPlugin::WhynotFxPlugin (te::PluginCreationInfo info, const juce::String& type)
    : te::Plugin (info), fxType (type), pluginType (pluginTypeFor (type)), host (makeProcessor (type))
{
    // Automação (3.5): só os parâmetros visíveis; os escondidos (o BPM do delay) o app manda.
    const auto& fx = host.info();

    for (int i = 0; i < (int) fx.params.size() && i < maxAutomated; ++i)
    {
        const auto& param = fx.params[(size_t) i];
        modelValues[(size_t) i].store (param.def, std::memory_order_relaxed);

        if (param.hidden)
            continue;

        automation::Range range;
        range.min = param.min;
        range.max = param.max;
        range.scale = param.logScale && param.min > 0.0f ? automation::Range::Scale::log : automation::Range::Scale::linear;
        range.stepped = param.kind != dsp::ParamKind::continuous;

        te::AutomatableParameter::Ptr ap (new te::AutomatableParameter (param.id, juce::String::fromUTF8 (param.name), *this, { 0.0f, 1.0f }));
        ap->valueToStringFunction = [range, unit = juce::String::fromUTF8 (param.unit)] (float n)
        {
            return juce::String (range.fromNormalised (n), 2) + (unit.isNotEmpty() ? " " + unit : juce::String());
        };
        addAutomatableParameter (ap);
        autoParams.push_back ({ i, ap, range });
    }
}

WhynotFxPlugin::~WhynotFxPlugin()
{
    notifyListenersOfDeletion();
}

void WhynotFxPlugin::registerAll (te::Engine& engine)
{
    for (auto* e : dsp::allEffects())
        engine.getPluginManager().registerBuiltInType (std::make_unique<FxType> (e->type));
}

juce::String WhynotFxPlugin::getName() const
{
    return juce::String::fromUTF8 (host.info().name);
}

juce::String WhynotFxPlugin::getShortName (int)
{
    return juce::String::fromUTF8 (host.info().shortName);
}

double WhynotFxPlugin::getLatencySeconds()
{
    const double rate = preparedRate > 0.0 ? preparedRate : (sampleRate > 0.0 ? sampleRate : 48000.0);
    return host.isPrepared() ? host.latencySamples() / rate : 0.0;
}

void WhynotFxPlugin::getChannelNames (juce::StringArray* ins, juce::StringArray* outs)
{
    te::Plugin::getChannelNames (ins, outs);

    if (ins != nullptr && host.info().sidechain)
    {
        ins->add ("Sidechain L");
        ins->add ("Sidechain R");
    }
}

void WhynotFxPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    // O Tracktion só chama initialise() sem nenhum grafo usando o plugin (e afirma isso),
    // então dá para alocar aqui. Mesma taxa e bloco: mantém o estado (caudas não cortam).
    if (info.sampleRate != preparedRate || info.blockSizeSamples > preparedBlock || ! host.isPrepared())
    {
        preparedRate = info.sampleRate;
        preparedBlock = juce::jmax (info.blockSizeSamples, 512);
        host.prepare (preparedRate, preparedBlock);
        monoScratch.setSize (1, preparedBlock);
    }
}

void WhynotFxPlugin::applyInsert (const Insert& ins, double projectBpm)
{
    const auto& info = host.info();

    for (int i = 0; i < (int) info.params.size(); ++i)
    {
        const auto& param = info.params[(size_t) i];
        float value = param.def;

        if (std::string_view (param.id) == "bpm")
            value = (float) projectBpm;
        else if (auto it = ins.params.find (param.id); it != ins.params.end())
            value = it->second;

        if (i < maxAutomated)
            modelValues[(size_t) i].store (param.clamp (value), std::memory_order_relaxed);

        // com curva de automação, quem manda é ela; este valor volta quando a curva sair
        if (isAutomated (i))
            continue;

        if (host.getParam (i) != param.clamp (value))
            host.setParam (i, value);
    }

    host.setBypassed (ins.bypass);
}

void WhynotFxPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    juce::ScopedNoDenormals noDenormals;

    auto& buffer = *fc.destBuffer;
    const int start = fc.bufferStartSample;
    const int num = fc.bufferNumSamples;
    const int channels = buffer.getNumChannels();

    if (num <= 0 || channels == 0)
        return;

    applyAutomation();

    if (channels == 1)
    {
        // Trilha mono: processa como estéreo com uma cópia e devolve o canal esquerdo.
        if (num > monoScratch.getNumSamples())
            return;

        float* left = buffer.getWritePointer (0, start);
        float* right = monoScratch.getWritePointer (0);
        std::copy (left, left + num, right);
        host.process (left, right, num);
        return;
    }

    const float* sideLeft = channels > 2 ? buffer.getReadPointer (2, start) : nullptr;
    const float* sideRight = channels > 3 ? buffer.getReadPointer (3, start) : sideLeft;

    host.process (buffer.getWritePointer (0, start), buffer.getWritePointer (1, start), num, sideLeft, sideRight);

    // Os canais de sidechain não seguem adiante.
    for (int c = 2; c < channels; ++c)
        buffer.clear (c, start, num);
}

bool WhynotFxPlugin::isAutomated (int index) const noexcept
{
    for (auto& ap : autoParams)
        if (ap.index == index)
            return ap.param->isAutomationActive();

    return false;
}

void WhynotFxPlugin::applyAutomation() noexcept
{
    // Thread de áudio. O Tracktion já pôs na curva o valor deste trecho (updateParameterStreams).
    const auto& fx = host.info();

    for (auto& ap : autoParams)
    {
        const bool active = ap.param->isAutomationActive();
        const auto i = (size_t) ap.index;

        if (active)
            host.setParam (ap.index, fx.params[i].clamp (ap.range.fromNormalised (ap.param->getCurrentValue())));
        else if (wasAutomated[i])
            host.setParam (ap.index, modelValues[i].load (std::memory_order_relaxed));   // a curva saiu: volta o valor do Insert

        wasAutomated[i] = active;
    }
}

} // namespace whynot
