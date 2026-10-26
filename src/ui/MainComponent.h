#pragma once

#include <JuceHeader.h>

#include "AiBridgeUi.h"
#include "BeatKeyNotice.h"
#include "ChannelRack.h"
#include "ExportDialogs.h"
#include "Fluid.h"
#include "MixerView.h"
#include "PianoRoll.h"
#include "PluginBrowser.h"
#include "Playlist.h"
#include "SampleBrowser.h"
#include "Theme.h"
#include "TransportBar.h"
#include "StatusBar.h"
#include "WelcomeCard.h"
#include "ViewRail.h"
#include "../engine/BeatKey.h"
#include "../library/ProjectsView.h"
#include "../model/ProjectModel.h"

namespace whynot
{

class AppController;

/** A tela principal: trilho das telas à esquerda, transporte em cima e, como cartões sobre o fundo
    preto, o navegador de sons (Alt+F8) ou o de plugins (F8), uma tela por vez (playlist, channel rack,
    piano roll ou projetos) e o mixer; barra de status no pé. É o DragAndDropContainer do que vem dos navegadores.
    Também dispara os atalhos de teclado (KeyMap: preset estilo FL ou WHYNOT, configurável), toca o
    teclado do computador como piano (Ctrl+T) e cuida dos diálogos de arquivo e de exportar.
*/
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private ProjectModel::Listener,
                      private juce::Timer
{
public:
    explicit MainComponent (AppController&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override;   // também dos filhos: devolve o foco dos atalhos

    /** Salva no arquivo atual; asNew ou projeto nunca salvo = pergunta onde. Devolve true se salvou. */
    bool saveProject (bool asNew);

    /** Mostra uma das telas de cima (usado também pelo --screenshot). */
    void showView (View);

    /** Escolhe, pelo id ou pelo nome, a trilha cujos efeitos aparecem no painel do mixer. */
    void selectMixerTrack (const juce::String& idOrName);

    /** Piano roll do canal (id ou nome) com o painel do Ideias aberto. */
    void showIdeas (const juce::String& channelIdOrName);

    /** Mostra ou esconde o cartão do navegador de plugins (F8; também pelo --screenshot). */
    void setPluginsVisible (bool);

    /** Mostra ou esconde o cartão do navegador de sons (Alt+F8; também pelo --screenshot). */
    void setSoundsVisible (bool);

    /** Liga ou desliga o teclado do computador como piano (Ctrl+T). */
    void setTypingKeyboard (bool);
    bool isTypingKeyboardOn() const noexcept   { return typingKeyboard; }

    /** A ponte da IA (Fase 4, fatia 4.5), quando o app abriu com --control: o Main.cpp liga o aviso do pé
        ("IA conectada" e "Desligar a IA") e a pergunta da IA nesta tela. Sem isto, o aviso nunca aparece. */
    struct AiHooks
    {
        std::function<bool()> connected, enabled, awaiting;
        std::function<void (bool)> setEnabled;
    };

    void setAiHooks (AiHooks);

    /** Mostra a pergunta da IA (seção 5.5) no alto da tela; `answer` é chamado uma vez (true = Permitir). */
    void askAi (const juce::String& title, const juce::String& commandId, const juce::String& reason,
                const juce::String& details, int seconds, std::function<void (bool)> answer);

    /** Fecha a pergunta sem resposta: passou o tempo, a IA desistiu ou foi desligada. */
    void cancelAiQuestion();

    /** A captura para a IA (fatia 4.6): mostra a tela pedida ("playlist", "channelRack", "pianoRoll", "projects";
        vazio = a atual), grava a tela inteira em PNG com até `maxWidth` de largura e devolve o tamanho da imagem.
        Vazio = não deu (o motivo em `error`). `channel` escolhe antes o canal do piano roll (vazio = o escolhido;
        sem `view`, mostra o piano roll) e volta com o canal que o piano roll mostra. */
    juce::Rectangle<int> capturePng (const juce::String& screenName, juce::String& channel, int maxWidth, const juce::File& png, juce::String& error);

    /** Sem janela (--headless): monta a tela fora da área de trabalho, no tamanho de uma janela comum, só para a
        captura, e desmonta. Não rouba o foco nem aparece na tela. */
    static juce::Rectangle<int> captureOffscreen (AppController&, const juce::String& view, juce::String& channel, int maxWidth,
                                                  const juce::File& png, juce::String& error);

    /** Para o autoteste: o cartão da pergunta aberto (ou nullptr) e o aviso do pé. */
    AiConfirmCard* getAiCard() const noexcept    { return aiCard.get(); }
    AiStatusNotice& getAiNotice() noexcept       { return aiNotice; }
    void updateAiNotice();

private:

    void projectChanged (const ProjectModel&) override;
    void timerCallback() override;

    /** Um quadro da tela: agulha (suavizada entre os blocos de áudio), medidores e o visor do transporte. */
    void animateFrame (double dt);

    void refreshAll();
    void updateSplit();
    void updateTitle();
    void showStatus (const juce::String&, bool isError);
    void runCommand (const juce::String& id, const juce::var& params = {});
    void stepPattern (int delta);   // + e - do teclado numérico, como no FL
    void addMarkerAtNeedle();       // Alt+T: marcador na régua da playlist (bloco 1, 22/10)
    void goToEmptyPattern();        // F4: o próximo padrão vazio, ou um novo
    void selectChannel (const juce::String&);
    juce::Component& upperComponent();

    void showFileMenu();
    void showAbout();
    void askHeadphones();          // "Está de fone?": a escuta "Quando armada" só vale de fone
    void newProject();
    void openProject();
    void openAutosave();
    void importMidi();             // Arquivo > Importar MIDI: no piano roll do canal escolhido
    void openProjectFile (const juce::File&);   // da tela Projetos: pergunta se salva o atual
    void saveNewVersion();
    void showExport (ExportDialog::Format);
    void showAudioSettings();
    void togglePanel (const juce::String&);
    void setUiScale (float);
    void buildActions();
    void offerRecovery();
    void releaseTypedNotes();
    juce::String typingRangeText() const;

    // tom do beat (21/10, noite): o aviso no pé quando o tom do beat é outro que o do projeto
    const BeatKeyAnalyzer::Result* mainBeatKey (std::shared_ptr<const BeatKeyAnalyzer::Result>& keep) const;
    void updateBeatNotice();
    void beatKeyAnalysed();
    void useBeatKey();
    void layoutBeatNotice();

    AppController& app;
    theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    TransportBar transport;
    ViewRail rail;
    PluginBrowser browser;
    SampleBrowser sounds;
    Playlist playlist;
    ChannelRack rack;
    juce::Viewport rackViewport;
    PianoRoll pianoRoll;
    ProjectsView projects;
    MixerView mixer;
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar divider { &layout, 1, false };

    View view = View::channelRack;
    bool showUpper = true, showMixer = true, showBrowser = false, showSounds = false;
    bool mixerAutoHidden = false;   // escondido para dar espaço ao piano roll/playlist em tela baixa
    int lastRackHeight = -1;
    juce::String selectedChannel;
    StatusBar statusBar;           // o pé: mensagem, erro que fica até fechar, teclado-piano e escala (Design)
    WelcomeCard welcome;           // "Comece aqui": só num projeto novo, até fechar (Design)
    bool wasPlaying = false;       // para avisar "sem saída de som" quando o play começa
    int recordMessageSerial = 0;   // a última mensagem da gravação que já foi para o pé
    int headphoneQuestionSeen = 0; // a última vez que a escuta perguntou "Está de fone?" (fatia 8)
    float uiScale = 1.0f;

    std::map<juce::String, std::function<void()>> actions;   // id do KeyMap → o que a tecla faz
    bool typingKeyboard = false;
    bool focusedOnce = false;                                 // a janela recebe o foco dos atalhos ao aparecer
    std::map<int, int> typedNotes;                            // tecla apertada → nota tocando
    int typingOctave = 0;                                     // -3 a +3 oitavas sobre o mapa (Z = C3, Q = C4)
    int typingVelocity = 100;                                 // força das notas do teclado-piano
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::PropertiesFile> settings;
    BeatKeyNotice beatNotice;
    juce::String beatNoticeId, dismissedBeatKey;   // arquivo, versão e tom do aviso; o "Agora não" vale para ele

    // a IA (Fase 4): o aviso no pé e a pergunta que espera o seu ok
    AiHooks aiHooks;
    AiStatusNotice aiNotice;
    std::unique_ptr<AiConfirmCard> aiCard, retiredAiCard;   // o fechado sai no próximo (pode fechar dentro do clique dele)

    // Fluidez (21/10): a agulha e os medidores andam no VBlank da tela, não no Timer de 30 Hz. O relógio
    // é o último membro: sai primeiro, antes das telas que ele anima.
    fluid::PlayheadSmoother playheadSmoother;
    fluid::FrameClock frames { *this, [this] (double, double dt) { animateFrame (dt); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace whynot
