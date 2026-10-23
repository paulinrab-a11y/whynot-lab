#pragma once

#include <JuceHeader.h>

#include "ProjectLibrary.h"
#include "../commands/CommandRegistry.h"
#include "../ui/Controls.h"

namespace whynot
{

/** Tela Projetos (F11): os seus projetos numa lista, cada um com a prévia em áudio para ouvir sem abrir.

    - Faixa de cima: o título, a busca e as ações (atualizar, renderizar a prévia, renderizar as pendentes, abrir).
    - Barra lateral: Todos, Recentes, Favoritos, os status, as tags e as pastas (com "Adicionar pasta").
    - Lista: nome, BPM, tom, duração, LUFS, a onda, a data e o estado da prévia. O ▶ da linha toca a prévia
      no deck que soa; Shift+▶ põe no outro; duplo clique ou Enter abre o projeto.
    - Embaixo, os decks A e B no mesmo relógio: a troca (Tab) é só de volume, igualado pelo LUFS; mesmo
      compasso, mesmo segundo ou livre; loop de N compassos; uma referência externa no B.
    O visual segue o docs/design.md: cartão `panel`, laranja só no que toca ou anda. */
class ProjectsView : public juce::Component,
                     private juce::ListBoxModel,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    /** `library` pode ser nullptr (app sem biblioteca): a tela só mostra o aviso. */
    explicit ProjectsView (library::ProjectLibrary*);
    ~ProjectsView() override;

    std::function<void (const juce::File&)> onOpenProject;
    std::function<void (const juce::String& text, bool isError)> onStatus;

    /** Roda um comando do app (Importar daqui usa o project.importFrom, que vira um passo de desfazer). */
    std::function<CommandResult (const juce::String& id, const juce::var& params)> onCommand;

    /** Escolhe a linha do projeto (autoteste e captura). */
    void selectProject (const juce::File&);

    /** Troca o deck que soa, A ou B (a tecla Tab com a tela à vista). */
    void switchDeck();

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    static constexpr int rowHeight = 40;
    static constexpr int sidebarWidth = 184;

    /** Arrastar uma linha: o texto leva este prefixo e o caminho do arquivo (a barra lateral solta num combo). */
    static constexpr const char* dragPrefix = "wnlprojeto:";
    static constexpr int deckHeight = 164;

private:
    class DeckStrip;
    class Sidebar;

    struct Columns
    {
        juce::Rectangle<int> play, star, name, bpm, key, length, lufs, wave, status, modified, state;
    };

    static Columns columnsFor (int width, int height);

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void selectedRowsChanged (int) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    const library::ProjectLibrary::Item* itemAt (int row) const;
    const library::ProjectLibrary::Row* rowAt (int row) const;
    void showRowMenu (int row);
    void editTags (const juce::File&);
    void editComboNote (const juce::String& combo, int index);
    void askNewCombo (const juce::File& addAfterCreating);
    void askRenameCombo (const juce::String& combo);
    void importFrom (const juce::File&);
    juce::Rectangle<int> searchArea() const;
    void layoutSidebar();
    const library::ProjectLibrary::Item* selectedItem() const;
    void playRow (int row, bool otherDeck = false);
    void openRow (int row);
    void renderSelected();
    void updateButtons();
    void status (const juce::String&, bool isError = false);

    library::ProjectLibrary* lib;
    juce::ListBox list;
    IconButton refreshButton { "Procurar projetos de novo nas pastas"_t, Icon::search, "Atualizar" },
               pendingButton { "Renderizar a prévia de todos os projetos sem prévia ou desatualizados"_t, Icon::wave, "Pendentes" },
               renderButton { "Renderizar a prévia do projeto escolhido (Ctrl+S também renderiza)"_t, Icon::wave, "Renderizar"_t },
               openButton { "Abrir o projeto escolhido (Enter ou duplo clique)"_t, Icon::folder, "Abrir" };
    std::unique_ptr<DeckStrip> deck;
    std::unique_ptr<Sidebar> sidebar;
    juce::Viewport sidebarViewport;   // a barra lateral rola quando há muitas tags, pastas ou combos
    juce::TextEditor search;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectsView)
};

} // namespace whynot
