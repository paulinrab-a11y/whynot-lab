#pragma once

#include <JuceHeader.h>

#include "CommandSearch.h"
#include "Theme.h"

namespace whynot
{

class AppController;

/** Paleta de comandos: a busca de ações. Abre no alto da janela, a pessoa digita ("exportar", "bpm", "mixer"),
    vê a lista filtrada com o atalho ao lado, e o Enter (ou o clique) executa.

    O que aparece:
    - as ações do KeyMap (as que têm ou podem ter tecla), executadas por `onAction`, o mesmo caminho da tecla;
    - os comandos do registro que rodam sem parâmetro obrigatório, executados por `app.run`. Ficam de fora os que
      uma ação do KeyMap já cobre e as consultas da IA, que só devolvem dados e não mudam nada na tela.

    É um componente filho de quem o abre (o MainComponent), nunca uma janela à parte: não depende de o app estar em
    primeiro plano e o autoteste o monta fora da área de trabalho. Fecha com o Esc, ao executar e ao perder o foco.
    O miolo da busca (dobrar o texto, pontuar e ordenar) fica em CommandSearch.h, sem JUCE.
*/
class CommandPalette : public juce::Component,
                       private juce::ListBoxModel
{
public:
    explicit CommandPalette (AppController&);
    ~CommandPalette() override;

    /** O id com que a ação de abrir a paleta entra no KeyMap; ela mesma não aparece na lista. */
    static constexpr const char* actionId = "view.commandPalette";

    /** Executa uma ação do KeyMap pelo id; false = ninguém cuida dela. Quem abre a paleta liga isto. */
    std::function<bool (const juce::String& actionId)> onAction;

    /** O resultado de um comando do registro (o título, ou o erro), para a barra de status. */
    std::function<void (const juce::String& text, bool isError)> onStatus;

    /** Monta a lista de agora, limpa a busca e aparece centrada no alto do pai. Aberta, fecha (a tecla alterna). */
    void open();
    void close();
    bool isOpen() const noexcept   { return isVisible(); }

    /** Setas, Page Up e Page Down, Home e End, Enter e Esc. true = a tecla era da paleta. */
    bool handleKey (const juce::KeyPress&);

    /** Para o autoteste (que não passa pelo teclado nem pelo mouse): o texto da busca, as linhas e a escolha. */
    void setQuery (const juce::String&);
    juce::String getQuery() const              { return search.getText(); }
    int numResults() const noexcept            { return (int) order.size(); }
    juce::String resultId (int row) const;
    juce::String resultTitle (int row) const;
    juce::String resultKeys (int row) const;
    int selectedRow() const                    { return list.getSelectedRow(); }
    void runRow (int row);

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;
    bool hitTest (int x, int y) override;

    static constexpr int cardWidth = 560, rowHeight = theme::metric::control, maxRows = 10, shadow = 18;

private:
    struct Item
    {
        juce::String id, title, keys;
        bool action = false;   // do KeyMap (onAction); senão, comando do registro (app.run)
    };

    struct Keys : juce::KeyListener
    {
        explicit Keys (CommandPalette& p) : owner (p) {}
        bool keyPressed (const juce::KeyPress& key, juce::Component*) override   { return owner.handleKey (key); }
        CommandPalette& owner;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void rebuildItems();
    void refilter();
    void updateBounds();
    void select (int row);
    juce::Rectangle<int> card() const;
    int visibleRows() const noexcept;
    const Item* itemAt (int row) const;

    AppController& app;
    std::vector<Item> items;                  // tudo o que a paleta oferece, na ordem de montar
    std::vector<cmdsearch::Entry> entries;    // o mesmo, em UTF-8, para a busca
    std::vector<int> order;                   // as linhas à vista: índices de `items`
    std::vector<std::string> recent;          // ids executados nesta sessão, o mais novo na frente

    juce::TextEditor search;
    juce::ListBox list { "paleta", this };
    Keys keys { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommandPalette)
};

} // namespace whynot
