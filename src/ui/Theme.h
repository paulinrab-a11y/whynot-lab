#pragma once

#include <JuceHeader.h>

#include "Icons.h"

#include <optional>
#include "../common/Text.h"

namespace whynot::theme
{

// Base visual da WHYNOT LAB (ver docs/design.md).
// Paleta do GUIDE WHYNOT: preto #000000, cinza #a6a6a6, branco #ffffff, laranja #ff751f.
// A janela é preta (o "chão"); cada tela é um cartão um pouco mais claro; controles sobem mais um degrau.
// Laranja é sinal: só o que está ligado, aceso ou tocando.
//
// Os valores abaixo são os do tema WHYNOT, o padrão. Os outros temas (Claro, Colorido, Frio) trocam estes
// tokens com applyScheme(): quem desenha lê o token na hora de pintar, nunca guarda a cor.
namespace colour
{
    inline juce::Colour background { 0xff000000 };   // preto da marca: barra de cima, trilho e frestas
    inline juce::Colour panel      { 0xff141414 };   // cartões (channel rack, piano roll, playlist, mixer)
    inline juce::Colour raised     { 0xff1f1f1f };   // controle em repouso
    inline juce::Colour raisedHigh { 0xff2c2c2c };   // controle com o mouse em cima, item escolhido
    inline juce::Colour line       { 0xff262626 };   // divisórias finas
    inline juce::Colour text       { 0xffffffff };   // branco da marca: valores e o que importa
    inline juce::Colour textDim    { 0xffa6a6a6 };   // cinza da marca: rótulos e ícones em repouso
    inline juce::Colour textFaint  { 0xff8a8a8a };   // dicas, legendas, desabilitado (4,5:1 sobre o cartão, no mínimo)
    inline juce::Colour signal     { 0xffff751f };   // laranja da marca
    inline juce::Colour signalTint { 0x2eff751f };   // fundo do que está ligado (laranja a 18%)
    inline juce::Colour meterLow   { 0xffd9d9d9 };
    inline juce::Colour meterMid   { 0xffff751f };
    inline juce::Colour meterHigh  { 0xffff3b30 };   // clipe: vermelho funcional, fora da paleta de propósito
    inline juce::Colour warning    { 0xffff9a57 };
    inline juce::Colour floating     { 0xff1a1a1a };   // menus, dicas e cartões com seta: um degrau acima dos cartões
    inline juce::Colour floatingEdge { 0xff333333 };   // a borda deles
    inline juce::Colour shade        { 0xff000000 };   // escurecer: fios e sombras por cima das linhas (no Claro, cinza)
    inline juce::Colour onSignal     { 0xff000000 };   // tinta sobre o sinal: o nome da nota, as marcas do roll nos passos

    /** Grade da linha do tempo (playlist): degraus abaixo e pouco acima do `panel`, para a grade
        se ler sem competir com os blocos. Do mais escuro para o mais claro. */
    namespace grid
    {
        inline juce::Colour laneEven  { 0xff0c0c0c };   // raia par
        inline juce::Colour laneOdd   { 0xff101010 };   // raia ímpar
        inline juce::Colour fine      { 0xff131313 };   // divisões do passo (1/2 a 1/16)
        inline juce::Colour step      { 0xff151515 };   // passo
        inline juce::Colour beat      { 0xff181818 };   // tempo
        inline juce::Colour laneEdge  { 0xff1c1c1c };   // fio entre raias
        inline juce::Colour clipHead  { 0xff222222 };   // faixa do nome do clipe de áudio (o corpo é `panel`)

        // linhas das teclas no piano roll
        inline juce::Colour rowOut    { 0xff0a0a0a };   // fora da escala
        inline juce::Colour rowDark   { 0xff111111 };   // tecla preta (escala cromática)
        inline juce::Colour row       { 0xff181818 };   // na escala (ou tecla branca, na cromática)
        inline juce::Colour rowRoot   { 0xff222222 };   // a tônica
    }

    /** O teclado do piano roll: claro, como um teclado de verdade, com o laranja da nota que soa. */
    namespace keys
    {
        inline juce::Colour white      { 0xffd9d9d9 };
        inline juce::Colour black      { 0xff1b1b1b };
        inline juce::Colour seam       { 0xff8c8c8c };   // fio entre as teclas
        inline juce::Colour hoverWhite { 0xffbfbfbf };
        inline juce::Colour hoverBlack { 0xff3a3a3a };
        inline juce::Colour label      { 0xff2e2e2e };   // nome do Dó (C3, C4...)
    }
}

// Medidas em px lógicos. Espaços sempre na escala 4/8/12/16/24.
namespace metric
{
    constexpr int gap = 8;            // fresta entre cartões; distância entre grupos de controles
    constexpr int pad = 16;           // margem interna dos cartões
    constexpr int control = 32;       // altura de botões e listas
    constexpr int toolbar = 48;       // faixa de ferramentas no topo de um cartão
    constexpr int topBar = 56;        // barra do transporte
    constexpr int rail = 76;          // trilho das telas, à esquerda
    constexpr int status = 28;        // barra de status

    constexpr float radiusSmall = 5.0f;   // passos, notas pequenas
    constexpr float radius = 8.0f;        // botões, listas, campos
    constexpr float radiusCard = 12.0f;   // cartões e menus
}

enum class Weight { regular, medium, semibold, bold };

/** Texto da interface: Montserrat (embutida, licença OFL). */
juce::Font font (float height, Weight = Weight::regular);

/** Números (BPM, posição, dB). */
juce::Font numberFont (float height);

/** Títulos e marca: Horizon quando o arquivo da fonte for adicionado; por ora, Montserrat Bold. */
juce::Font displayFont (float height);

/** Legenda em maiúsculas pequenas (POSIÇÃO, BPM...), sempre com textFaint. */
juce::Font captionFont();

/** Escala de texto: título de cartão, corpo, detalhe. */
namespace type
{
    constexpr float title = 15.0f;
    constexpr float body = 13.0f;
    constexpr float small = 12.0f;
}

juce::Colour fromHex (const juce::String& hex, juce::Colour fallback = colour::textDim);

/** Logo WHYNOT branca (vetor do guia), para fundo escuro. */
const juce::Drawable* logo();

/** Os temas de cores. O WHYNOT é o padrão; os outros mudam os tokens de `colour`, mas não o vermelho funcional
    (`meterHigh`: clipe, erro e gravando), nem as medidas e as fontes. */
enum class ColourScheme { whynot, light, colourful, cool };

inline constexpr ColourScheme allSchemes[] { ColourScheme::whynot, ColourScheme::light, ColourScheme::colourful, ColourScheme::cool };

/** Todos os tokens de cor de um tema, na ordem de `colour`, `colour::grid` e `colour::keys`. */
struct Palette
{
    juce::Colour background, panel, raised, raisedHigh, line, text, textDim, textFaint, signal, signalTint,
                 meterLow, meterMid, meterHigh, warning, floating, floatingEdge, shade, onSignal;
    juce::Colour laneEven, laneOdd, fine, step, beat, laneEdge, clipHead, rowOut, rowDark, row, rowRoot;
    juce::Colour keyWhite, keyBlack, keySeam, keyHoverWhite, keyHoverBlack, keyLabel;
};

const Palette& palette (ColourScheme);
ColourScheme currentScheme() noexcept;
bool isLightScheme() noexcept;

/** Troca os tokens. Quem pinta depois já usa as cores novas; para a tela aberta, chame também
    LookAndFeel::refreshColours() e reskinAll(). */
void applyScheme (ColourScheme);

/** Depois de applyScheme: refaz as cores que os componentes guardaram com setColour (da paleta de `from` para a
    atual, pelo token, com o mesmo alfa), em todas as janelas abertas, e repinta. */
void reskinAll (ColourScheme from);

/** O mesmo, só num componente e nos filhos dele (sem repintar). */
void reskin (juce::Component&, ColourScheme from);

juce::String schemeKey (ColourScheme);    // o que fica salvo: "whynot", "claro", "colorido", "frio"
juce::String schemeName (ColourScheme);   // o que aparece no menu: "WHYNOT", "Claro", "Colorido", "Frio"
std::optional<ColourScheme> schemeFromKey (const juce::String&);

/** O tema que o usuário escolheu (a chave "theme" do WHYNOT LAB.settings); WHYNOT se não houver. */
ColourScheme savedScheme();

/** A logo da marca na cor pedida (o SVG é branco; no tema Claro ela sai na cor do texto). */
void drawLogo (juce::Graphics&, juce::Rectangle<float> area, juce::Colour);

/** Ícone do app (logo preta em quadrado branco) num tamanho em pixels. */
juce::Image iconImage (int size);

/** Placa com o nome de um canal ou trilha: fundo de controle, texto branco, faixa na cor do canal.
    `badge` (opcional) é um ícone de estado no canto direito, como o floco de congelado. */
void drawNamePlate (juce::Graphics&, juce::Rectangle<float>, const juce::String& name, juce::Colour edge,
                    bool selected, bool problem, float fontHeight, std::optional<Icon> badge = std::nullopt);

/** Pílula de saída ("para onde vai o som"): ícone de rota + nome do destino. */
void drawRoutePill (juce::Graphics&, juce::Rectangle<float>, const juce::String& destination, bool hovered,
                    bool dimText, float fontHeight);

/** Números com largura fixa por dígito: o texto não "dança" quando o valor muda. */
void drawTabularText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&,
                      juce::Justification);

/** Cantos que ficam fora de um retângulo arredondado (para recortar cartões sobre o fundo). */
juce::Path cornerMask (juce::Rectangle<float>, float radius);

/** Prepara uma lista no estilo da casa: ícone opcional à esquerda; `bare` = sem fundo até o mouse passar. */
void styleComboBox (juce::ComboBox&, Icon = Icon::none, bool bare = false);

/** Hexágono da marca centrado em `area` (o LED de mute dos canais). */
juce::Path hexagon (juce::Rectangle<float> area);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    /** Põe nos ids do JUCE (menus, dicas, listas, campos) as cores do tema atual. */
    void refreshColours();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                           juce::Slider&) override;

    // Lista: propriedade "icon" (int de Icon) põe um ícone à esquerda; "style" = "bare" tira o fundo.
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override;

    int getDefaultScrollbarWidth() override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;

    void drawStretchableLayoutResizerBar (juce::Graphics&, int w, int h, bool vertical, bool over, bool dragging) override;

    // Cartão que abre apontando para um controle (ajustes do sampler, por exemplo): mesmo fundo dos cartões,
    // borda fina e a seta da mesma cor. O conteúdo pinta `panel` e se funde com a moldura.
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image& cachedShadow) override;
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override;
    float getCallOutBoxCornerSize (const juce::CallOutBox&) override;
};

} // namespace whynot::theme
