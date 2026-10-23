#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "fx/FxWidgets.h"
#include "../audio/SampleVoice.h"
#include "../model/ProjectModel.h"

namespace whynot
{

class AppController;

/** Ajustes de um canal do sampler, num cartão que abre sobre o channel rack: nota de raiz
    (com "Detectar"), a onda do sample com o início, o laço e as fatias, afinação, envelope (ataque,
    decay, sustain), queda de tom, início e o laço (ligar, pontas e crossfade, com "Achar laço").
    Tudo passa por channel.setSampler e channel.detectRoot: desfazer, salvar e a IA veem o
    mesmo que a tela. Um arrasto de knob ou de ponta do laço é um passo de desfazer só.
*/
class SamplerPanel : public juce::Component,
                     private ProjectModel::Listener
{
public:
    SamplerPanel (AppController&, const juce::String& channelId);
    ~SamplerPanel() override;

    /** Abre o painel num cartão flutuante apontando para `area`. Sem `parent`, o cartão vai para a
        área de trabalho, em cima de tudo, e `area` é em coordenadas da tela; com `parent`, vira filho
        dele, e `area` é relativa a ele (é assim que o autoteste abre o painel sem janela nem foco). */
    static juce::CallOutBox& show (AppController&, const juce::String& channelId, juce::Rectangle<int> area,
                                   std::function<void (const juce::String& message, bool isError)> onStatus,
                                   juce::Component* parent = nullptr);

    std::function<void (const juce::String& message, bool isError)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Group
    {
        juce::String title;
        std::vector<ParamControl*> controls;
    };

    /** A onda do sample, o início da leitura e o laço. As pontas do laço se arrastam; arrastar
        dentro do laço move as duas juntas. */
    class Wave : public juce::Component,
                 public juce::SettableTooltipClient
    {
    public:
        explicit Wave (SamplerPanel&);

        void setSample (std::shared_ptr<const audio::SampleData>);
        void setMarks (float start, bool loop, float loopStart, float loopEnd);
        void setSlices (const std::vector<float>&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        enum class Grab { none, start, end, both };

        Grab grabAt (float x) const;
        float xOf (float fraction) const;
        float fractionAt (float x) const;
        void buildPeaks();

        SamplerPanel& panel;
        std::shared_ptr<const audio::SampleData> sample;
        std::vector<std::pair<float, float>> peaks;   // mínimo e máximo por coluna
        std::vector<float> slices;                     // começos das fatias (fatiador)
        float start = 0.0f, loopStart = 0.0f, loopEnd = 1.0f;
        bool loop = false;
        Grab grab = Grab::none, hover = Grab::none;
        float grabOffset = 0.0f, grabLength = 0.0f;
    };

    void projectChanged (const ProjectModel&) override;
    void refresh();
    void send (const char* key, float value);
    void sendLoop (float loopStart, float loopEnd);
    void detect();
    void findLoop();

    AppController& app;
    const juce::String channelId;
    juce::String channelName, sampleName, rootText, samplePath;
    bool exists = true;

    juce::ComboBox rootBox;
    IconButton detectButton { "Detectar", Icon::note, "Detectar", IconButton::Style::filled };
    IconButton findLoopButton { "Achar laço"_t, Icon::loop, "Achar laço"_t, IconButton::Style::ghost };
    Wave wave { *this };
    juce::OwnedArray<ParamControl> controls;   // na ordem de samplerParams()
    std::vector<Group> groups;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerPanel)
};

} // namespace whynot
