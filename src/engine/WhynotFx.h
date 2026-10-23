#pragma once

#include <JuceHeader.h>

#include "../automation/AutomationMath.h"
#include "../dsp/FxCatalog.h"
#include "../dsp/FxHost.h"
#include "../model/Project.h"

#include <array>

namespace whynot
{

namespace te = tracktion;

/** Os efeitos nativos da WHYNOT (EQ, compressor, saturador...) como plugins internos do Tracktion.

    Uma classe só para todos: o tipo do Tracktion é "whynotFx." + o tipo do efeito ("whynotFx.eq").
    O processamento fica em dsp::FxHost (sem JUCE, testado à parte); aqui só ligamos os buffers.

    - Thread de mensagem: setValues(), setBypassed() (atomics), leitura dos medidores.
    - initialise(): o Tracktion chama na thread de mensagem, sem nenhum grafo usando o plugin.
    - applyToBuffer(): thread de áudio.

    Com sidechain ligado, o Tracktion entrega 4 canais: 0-1 o sinal, 2-3 o disparo.

    Automação (3.5): cada parâmetro visível vira um te::AutomatableParameter de 0 a 1 (a posição do
    knob, logarítmica nas frequências), com o id do parâmetro do efeito. Com a curva ativa, o
    applyToBuffer manda o valor dela ao efeito; quando a curva sai, volta o valor do Insert (o modelo
    é a fonte da verdade: o estado guardado no parâmetro do Tracktion nunca passa por cima dele). */
class WhynotFxPlugin : public te::Plugin
{
public:
    static constexpr const char* typePrefix = "whynotFx.";

    WhynotFxPlugin (te::PluginCreationInfo, const juce::String& fxType);
    ~WhynotFxPlugin() override;

    /** Registra todos os tipos de efeito no motor (uma vez, ao abrir o app). */
    static void registerAll (te::Engine&);

    static juce::String pluginTypeFor (const juce::String& fxType)   { return typePrefix + fxType; }

    const juce::String& getFxType() const noexcept   { return fxType; }
    const dsp::FxInfo& getInfo() const noexcept      { return host.info(); }
    dsp::FxHost& getHost() noexcept                  { return host; }

    /** Aplica os valores do modelo (os ausentes voltam ao padrão) e o bypass. */
    void applyInsert (const Insert&, double projectBpm);

    /** Em que trilha e slot do modelo este plugin está (o editor manda os comandos por aqui). */
    void setOwner (const juce::String& mixerId, const juce::String& insertId)   { ownerTrack = mixerId; ownerInsert = insertId; }
    const juce::String& getOwnerTrack() const noexcept    { return ownerTrack; }
    const juce::String& getOwnerInsert() const noexcept   { return ownerInsert; }

    juce::String getName() const override;
    juce::String getPluginType() override                { return pluginType; }
    juce::String getVendor() override                    { return "WHYNOT LAB"; }
    juce::String getShortName (int) override;
    juce::String getSelectableDescription() override     { return getName(); }

    bool takesAudioInput() override                      { return true; }
    bool producesAudioWhenNoAudioInput() override        { return host.tailSeconds() > 0.0; }
    bool noTail() override                               { return host.tailSeconds() <= 0.0; }
    double getTailLength() const override                { return host.tailSeconds(); }
    double getLatencySeconds() override;
    int getNumOutputChannelsGivenInputs (int) override   { return 2; }
    BusLayout getBusses() const override                 { return BusLayout::singleStereoInOut(); }
    void getChannelNames (juce::StringArray* ins, juce::StringArray* outs) override;
    bool canBeAddedToClip() override                     { return false; }
    bool canBeAddedToRack() override                     { return false; }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override                         {}
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** Automação (3.5): o parâmetro (pela posição no FxInfo) está seguindo uma curva agora? */
    bool isAutomated (int paramIndex) const noexcept;

private:
    static constexpr int maxAutomated = 64;   // o mesmo teto de parâmetros do FxHost

    struct AutoParam
    {
        int index = 0;                         // no FxInfo
        te::AutomatableParameter::Ptr param;
        automation::Range range;
    };

    void applyAutomation() noexcept;

    const juce::String fxType, pluginType;
    juce::String ownerTrack, ownerInsert;
    dsp::FxHost host;
    std::vector<AutoParam> autoParams;                          // montado no construtor; depois só lido
    std::array<std::atomic<float>, maxAutomated> modelValues;   // o valor do Insert de cada parâmetro
    std::array<bool, maxAutomated> wasAutomated {};             // só a thread de áudio
    double preparedRate = 0.0;
    int preparedBlock = 0;
    juce::AudioBuffer<float> monoScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhynotFxPlugin)
};

} // namespace whynot
