#pragma once

#include <JuceHeader.h>

#include "../automation/AutomationMath.h"
#include "../model/Project.h"

#include <functional>
#include <map>
#include <optional>
#include <set>

namespace whynot
{

namespace te = tracktion;

class AppController;

/** Gravar automação (26/10, item 16 da pesquisa das comunidades): com o modo ligado e a música tocando no SONG,
    mexer num controle automatizável (o fader e o pan do mixer, um knob de efeito nativo, o controlador MIDI ligado
    a eles, ou um parâmetro de um VST3 na janela dele) escreve a curva desse controle.

    - Uma volta é do play até o stop, ou até o loop voltar ao começo. Cada volta com algo mexido vira UM passo de
      desfazer ("Gravar automação"): os knobs mexidos e a curva escrita, juntos.
    - Do primeiro toque até o fim da volta o controle escreve (o último valor segura até o fim, como um "latch");
      o trecho tocado troca o que a curva tinha nele, e fora dele a curva fica como estava.
    - O knob é medido a cada 20 ms e a curva guarda só o que muda o som (automation::reduce, 0,4% do knob).
    - Enquanto a pessoa segura um controle, a curva dele fica de fora (Projector::setAutomationHold): o som segue a
      mão. No fim da volta o knob (o valor fora da curva) volta ao de antes da volta e a curva nova manda.
    - Com o punch ligado, só fica o que foi mexido entre o início e o fim do punch.
    - Qualquer outra edição no meio fecha a volta antes (o Ctrl+Z desfaz a volta que acabou de ser gravada).

    Tudo na thread de mensagem. O autoteste troca o relógio (clock) e chama poll() direto, sem placa de som. */
class AutomationRecording : private juce::Timer,
                            private te::ParameterChangeHandler::Listener
{
public:
    explicit AutomationRecording (AppController&);
    ~AutomationRecording() override;

    void setArmed (bool);
    bool isArmed() const noexcept     { return armed; }

    /** Há uma volta aberta, com algum controle mexido: está escrevendo. */
    bool isWriting() const noexcept   { return lapOpen; }

    /** O tick que está tocando, ou nullopt (parado, ou no modo PAT). Padrão: o transporte do app. */
    std::function<std::optional<int>()> clock;

    /** Um passo da medida (o temporizador chama a cada 20 ms). */
    void poll();

    /** Fecha a volta aberta agora (o stop, uma edição no meio): grava a passada. endTick: até onde ela escreve (o fim do
        loop, quando ele voltou ao começo); padrão: a última medida. */
    void finishLap (std::optional<int> endTick = std::nullopt);

    /** O CommandRegistry::beforeCommand: abre a volta antes do primeiro knob e fecha antes de outra edição. Comandos da
        IA (num gesto "IA: ..." ou num lote) contam como edição de fora, não como a mão da pessoa. */
    void beforeCommand (const juce::String& id);

    /** O CommandRegistry::beforeGestureBegins: um gesto novo (arrastar nota, bloco, outro knob) fecha a volta aberta. */
    void beforeGesture();

    /** Quantas passadas já gravou (para o autoteste e a tela). */
    int lapsWritten() const noexcept   { return laps; }

private:
    struct Native
    {
        AutomationTarget target;
        automation::Range range;
        float value = 0.0f;   // na unidade do parâmetro (dB, pan, Hz...)
    };

    struct Touch
    {
        AutomationTarget target;
        bool native = true;
        float before = 0.0f;                    // o valor de antes da volta (nativo: na unidade; VST3: 0 a 1)
        te::AutomatableParameter::Ptr param;    // VST3
        std::vector<automation::Point> samples;
    };

    void timerCallback() override   { poll(); }
    void pluginParameterChanged (te::AutomatableParameter&, bool isFollowingAutomation) override;

    std::map<juce::String, Native> nativeValues() const;
    std::map<te::AutomatableParameter*, std::pair<AutomationTarget, float>> pluginValues() const;
    bool fromAi() const;
    void snapshot();
    void openLap();
    void touch (const AutomationTarget&, bool native, float before, te::AutomatableParameter* param);
    void applyHold();

    AppController& app;
    bool armed = false;
    bool passActive = false;    // tocando com o modo ligado
    bool lapOpen = false;       // o gesto de desfazer da volta está aberto
    bool committing = false;    // gravando a passada: os comandos dela não fecham a volta
    bool resnapshot = false;    // uma edição de fora mudou os valores: medir de novo antes de comparar
    bool holdDirty = false;
    int lastTick = -1;
    int laps = 0;

    std::map<juce::String, Native> natives;   // o valor de cada controle nativo no começo da volta
    std::map<te::AutomatableParameter*, std::pair<AutomationTarget, float>> plugins;   // o mesmo, para os VST3
    std::map<juce::String, Touch> touched;    // por AutomationTarget::key()
};

} // namespace whynot
