#pragma once

#include <JuceHeader.h>

#include "../../model/Project.h"

namespace whynot
{

class AppController;

/** A cadeia de efeitos da trilha escolhida no mixer: papel, os slots (maxInserts) e os envios.

    - Slot vazio: clique abre o menu de efeitos (por categoria, já com os presets) e as
      "cadeias prontas" de trap, funk, rap e pop para esta trilha.
    - Slot com efeito: clique abre a janela; o LED liga e desliga (bypass); botão direito
      abre presets, sidechain, mover, trocar e remover; arrastar muda a ordem.
    - Envios: nível de cada envio, e "+ Envio" para um bus (ou cria um bus de reverb/delay). */
class InsertPanel : public juce::Component,
                    public juce::DragAndDropTarget
{
public:
    explicit InsertPanel (AppController&);
    ~InsertPanel() override;

    void setTrack (const juce::String& mixerId);
    const juce::String& getTrack() const noexcept   { return trackId; }

    void refresh (const Project&);

    /** Altura que o conteúdo pede (o MixerView rola se não couber). */
    int getIdealHeight() const;

    std::function<void (const juce::String& message, bool isError)> onStatus;

    /** A altura pedida mudou sem o projeto mudar (abrir ou fechar os macros): o MixerView refaz o layout. */
    std::function<void()> onLayoutChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    /** O rack (bloco 4c): os 8 macros entre os efeitos e os envios. Abre sozinho numa trilha com macros ligados;
        o título "MACROS" abre e fecha. */
    bool areMacrosOpen() const noexcept   { return macrosOpen; }
    void setMacrosOpen (bool);

    /** O menu do botão direito de um macro (index de 0 a 7): renomear, zerar, ligar um parâmetro (por efeito, com
        os ligados marcados) e o que ele move agora. Cada item roda actions[id - 1]. Público para o autoteste. */
    juce::PopupMenu createMacroMenu (int index, std::vector<std::function<void()>>& actions);

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    static constexpr int idealWidth = 248;

    /** O menu de adicionar efeito num slot vazio (o mesmo do clique): o filtro "Só favoritos", os favoritos
        primeiro e depois as categorias, com estrela em cada efeito, preset e VST3. Cada item escolhido roda
        actions[id - 1]; o id filterItemId liga e desliga o filtro. Público para o autoteste conferir a ordem. */
    juce::PopupMenu createAddMenu (int slot, std::vector<std::function<void()>>& actions);
    static constexpr int filterItemId = 100000;
    bool favouritesOnly = false;

private:
    class Slot;
    class SendRow;
    class MacroKnob;

    void run (const juce::String& command, const juce::var& args, const juce::String& success = {});
    void showAddMenu (int slot, juce::Component* target);
    void showSlotMenu (int slot, juce::Component* target);
    void showSendMenu();
    void showChainMenu();
    void askChainName();
    void addFxMenuItems (juce::PopupMenu&, std::vector<std::function<void()>>& actions, int slot,
                         const juce::String& replacing = {}, bool onlyFavourites = false);
    int slotAt (juce::Point<int>) const;
    void createBus (const juce::String& role);
    void showMacroMenu (int index, juce::Component* target);
    void askMacroName (int index);

    AppController& app;
    juce::String trackId;
    bool isMaster = false;
    juce::String trackName, roleLabel;

    juce::ComboBox roleBox;
    juce::StringArray roleIds;
    juce::TextButton chainButton, addSendButton;
    juce::OwnedArray<Slot> slots;
    juce::OwnedArray<SendRow> sends;
    juce::OwnedArray<MacroKnob> macroKnobs;
    std::unique_ptr<juce::AlertWindow> nameDialog;   // "Salvar esta cadeia" e o nome do macro (um de cada vez)
    int dropSlot = -1;
    bool macrosOpen = false, macrosHadLinks = false;
    juce::String macrosFor;                          // a trilha em que o rack abriu ou fechou sozinho
    juce::Rectangle<int> macroCaption;

    static constexpr int headerHeight = 34, roleHeight = 32, captionHeight = 22, slotHeight = 30, sendHeight = 30;
    static constexpr int macroColumns = 4, macroRowHeight = 58;
};

} // namespace whynot
